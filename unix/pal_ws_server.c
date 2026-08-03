/*
 * Minimal, dependency-free WebSocket control server for the Unix SDL build.
 * This is deliberately a single-client, loopback-only test interface.
 */

#include "main.h"
#include "unix/pal_ws_server.h"

#if defined(PAL_EXTREME_TWO_SCREENS)
#include "cardputer_extreme_native_view.h"
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define PAL_WS_INPUT_BYTES 4096u
#define PAL_WS_OUTPUT_BYTES (12u + 320u * 200u * 3u + 16u)
#define PAL_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef struct tagPALWSSHA1
{
   uint32_t state[5];
   uint64_t bytes;
   uint8_t block[64];
   size_t used;
} PALWSSHA1;

static int pal_ws_listen = -1;
static int pal_ws_client = -1;
static BOOL pal_ws_open;
static BOOL pal_ws_close_after_send;
static uint8_t pal_ws_input[PAL_WS_INPUT_BYTES];
static size_t pal_ws_input_len;
static uint8_t pal_ws_output[PAL_WS_OUTPUT_BYTES];
static size_t pal_ws_output_len;
static size_t pal_ws_output_sent;
static int pal_ws_pending_battle = -1;
static BOOL pal_ws_pending_battle_auto;
static int pal_ws_pending_script = -1;
static int pal_ws_pending_script_event;
static BOOL pal_ws_active_script;
static int pal_ws_pending_shop = -1;
enum
{
   PAL_WS_UI_NONE = 0,
   PAL_WS_UI_MAIN,
   PAL_WS_UI_STATUS,
   PAL_WS_UI_ITEMS,
   PAL_WS_UI_MAGIC,
   PAL_WS_UI_SAVE,
   PAL_WS_UI_CONFIRM,
   PAL_WS_UI_SHOP
};
static int pal_ws_pending_ui = PAL_WS_UI_NONE;
static int pal_ws_active_ui = PAL_WS_UI_NONE;
static int pal_ws_last_battle_team = -1;
static int pal_ws_last_battle_result = -1;

static const char *
pal_ws_ui_name(
   int ui
)
{
   switch (ui)
   {
   case PAL_WS_UI_MAIN: return "main";
   case PAL_WS_UI_STATUS: return "status";
   case PAL_WS_UI_ITEMS: return "items";
   case PAL_WS_UI_MAGIC: return "magic";
   case PAL_WS_UI_SAVE: return "save";
   case PAL_WS_UI_CONFIRM: return "confirm";
   case PAL_WS_UI_SHOP: return "shop";
   default: return "";
   }
}

static uint32_t
pal_ws_rotl(
   uint32_t value,
   unsigned shift
)
{
   return (value << shift) | (value >> (32u - shift));
}

static void
pal_ws_sha1_block(
   PALWSSHA1 *ctx,
   const uint8_t block[64]
)
{
   uint32_t words[80];
   uint32_t a;
   uint32_t b;
   uint32_t c;
   uint32_t d;
   uint32_t e;
   unsigned i;

   for (i = 0; i < 16; i++)
   {
      words[i] = ((uint32_t)block[i * 4] << 24) |
         ((uint32_t)block[i * 4 + 1] << 16) |
         ((uint32_t)block[i * 4 + 2] << 8) |
         block[i * 4 + 3];
   }
   for (; i < 80; i++)
   {
      words[i] = pal_ws_rotl(
         words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
   }

   a = ctx->state[0];
   b = ctx->state[1];
   c = ctx->state[2];
   d = ctx->state[3];
   e = ctx->state[4];
   for (i = 0; i < 80; i++)
   {
      uint32_t f;
      uint32_t k;
      uint32_t temp;

      if (i < 20)
      {
         f = (b & c) | ((~b) & d);
         k = 0x5a827999u;
      }
      else if (i < 40)
      {
         f = b ^ c ^ d;
         k = 0x6ed9eba1u;
      }
      else if (i < 60)
      {
         f = (b & c) | (b & d) | (c & d);
         k = 0x8f1bbcdcu;
      }
      else
      {
         f = b ^ c ^ d;
         k = 0xca62c1d6u;
      }
      temp = pal_ws_rotl(a, 5) + f + e + k + words[i];
      e = d;
      d = c;
      c = pal_ws_rotl(b, 30);
      b = a;
      a = temp;
   }
   ctx->state[0] += a;
   ctx->state[1] += b;
   ctx->state[2] += c;
   ctx->state[3] += d;
   ctx->state[4] += e;
}

static void
pal_ws_sha1_init(
   PALWSSHA1 *ctx
)
{
   ctx->state[0] = 0x67452301u;
   ctx->state[1] = 0xefcdab89u;
   ctx->state[2] = 0x98badcfeu;
   ctx->state[3] = 0x10325476u;
   ctx->state[4] = 0xc3d2e1f0u;
   ctx->bytes = 0;
   ctx->used = 0;
}

static void
pal_ws_sha1_update(
   PALWSSHA1 *ctx,
   const uint8_t *data,
   size_t size
)
{
   ctx->bytes += size;
   while (size > 0)
   {
      size_t take = sizeof(ctx->block) - ctx->used;
      if (take > size)
      {
         take = size;
      }
      memcpy(ctx->block + ctx->used, data, take);
      ctx->used += take;
      data += take;
      size -= take;
      if (ctx->used == sizeof(ctx->block))
      {
         pal_ws_sha1_block(ctx, ctx->block);
         ctx->used = 0;
      }
   }
}

static void
pal_ws_sha1_final(
   PALWSSHA1 *ctx,
   uint8_t digest[20]
)
{
   uint64_t bits = ctx->bytes * 8u;
   unsigned i;

   ctx->block[ctx->used++] = 0x80u;
   if (ctx->used > 56)
   {
      memset(ctx->block + ctx->used, 0, 64 - ctx->used);
      pal_ws_sha1_block(ctx, ctx->block);
      ctx->used = 0;
   }
   memset(ctx->block + ctx->used, 0, 56 - ctx->used);
   for (i = 0; i < 8; i++)
   {
      ctx->block[63 - i] = (uint8_t)(bits >> (i * 8));
   }
   pal_ws_sha1_block(ctx, ctx->block);
   for (i = 0; i < 5; i++)
   {
      digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
      digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
      digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
      digest[i * 4 + 3] = (uint8_t)ctx->state[i];
   }
}

static void
pal_ws_base64_20(
   const uint8_t input[20],
   char output[29]
)
{
   static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   size_t in_pos = 0;
   size_t out_pos = 0;

   while (in_pos + 3 <= 20)
   {
      uint32_t value = ((uint32_t)input[in_pos] << 16) |
         ((uint32_t)input[in_pos + 1] << 8) | input[in_pos + 2];
      output[out_pos++] = alphabet[(value >> 18) & 63u];
      output[out_pos++] = alphabet[(value >> 12) & 63u];
      output[out_pos++] = alphabet[(value >> 6) & 63u];
      output[out_pos++] = alphabet[value & 63u];
      in_pos += 3;
   }
   if (in_pos < 20)
   {
      uint32_t value = (uint32_t)input[in_pos] << 16;
      output[out_pos++] = alphabet[(value >> 18) & 63u];
      if (in_pos + 1 < 20)
      {
         value |= (uint32_t)input[in_pos + 1] << 8;
         output[out_pos++] = alphabet[(value >> 12) & 63u];
         output[out_pos++] = alphabet[(value >> 6) & 63u];
      }
      else
      {
         output[out_pos++] = alphabet[(value >> 12) & 63u];
         output[out_pos++] = '=';
      }
      output[out_pos++] = '=';
   }
   output[out_pos] = '\0';
}

static int
pal_ws_nonblocking(
   int fd
)
{
   int flags = fcntl(fd, F_GETFL, 0);
   return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void
pal_ws_close_client(
   void
)
{
   if (pal_ws_client >= 0)
   {
      close(pal_ws_client);
   }
   pal_ws_client = -1;
   pal_ws_open = FALSE;
   pal_ws_close_after_send = FALSE;
   pal_ws_input_len = 0;
   pal_ws_output_len = 0;
   pal_ws_output_sent = 0;
}

static BOOL
pal_ws_queue_raw(
   const void *data,
   size_t size
)
{
   if (size > sizeof(pal_ws_output) || pal_ws_output_len != 0)
   {
      return FALSE;
   }
   memcpy(pal_ws_output, data, size);
   pal_ws_output_len = size;
   pal_ws_output_sent = 0;
   return TRUE;
}

static size_t
pal_ws_frame_header(
   uint8_t opcode,
   size_t payload_size
)
{
   pal_ws_output[0] = 0x80u | opcode;
   if (payload_size <= 125u)
   {
      pal_ws_output[1] = (uint8_t)payload_size;
      return 2;
   }
   if (payload_size <= 65535u)
   {
      pal_ws_output[1] = 126u;
      pal_ws_output[2] = (uint8_t)(payload_size >> 8);
      pal_ws_output[3] = (uint8_t)payload_size;
      return 4;
   }
   pal_ws_output[1] = 127u;
   memset(pal_ws_output + 2, 0, 4);
   pal_ws_output[6] = (uint8_t)(payload_size >> 24);
   pal_ws_output[7] = (uint8_t)(payload_size >> 16);
   pal_ws_output[8] = (uint8_t)(payload_size >> 8);
   pal_ws_output[9] = (uint8_t)payload_size;
   return 10;
}

static BOOL
pal_ws_queue_frame(
   uint8_t opcode,
   const void *payload,
   size_t payload_size
)
{
   size_t header;

   if (pal_ws_output_len != 0)
   {
      return FALSE;
   }
   header = pal_ws_frame_header(opcode, payload_size);
   if (header + payload_size > sizeof(pal_ws_output))
   {
      return FALSE;
   }
   memcpy(pal_ws_output + header, payload, payload_size);
   pal_ws_output_len = header + payload_size;
   pal_ws_output_sent = 0;
   return TRUE;
}

static void
pal_ws_reply_text(
   const char *text
)
{
   (void)pal_ws_queue_frame(1u, text, strlen(text));
}

static void
pal_ws_reply_error(
   const char *message
)
{
   char response[256];
   snprintf(response, sizeof(response),
      "{\"ok\":false,\"error\":\"%s\"}", message);
   pal_ws_reply_text(response);
}

static const char *
pal_ws_header_value(
   const char *request,
   const char *name
)
{
   size_t name_len = strlen(name);
   const char *line = request;

   while (line != NULL && *line != '\0')
   {
      const char *next = strstr(line, "\r\n");
      if (strncasecmp(line, name, name_len) == 0 &&
          line[name_len] == ':')
      {
         const char *value = line + name_len + 1;
         while (*value == ' ' || *value == '\t')
         {
            value++;
         }
         return value;
      }
      line = next == NULL ? NULL : next + 2;
   }
   return NULL;
}

static BOOL
pal_ws_handshake(
   void
)
{
   char request[PAL_WS_INPUT_BYTES + 1];
   const char *key;
   const char *key_end;
   PALWSSHA1 sha1;
   uint8_t digest[20];
   char accept[29];
   char joined[128];
   char response[256];
   int response_len;
   size_t key_len;

   memcpy(request, pal_ws_input, pal_ws_input_len);
   request[pal_ws_input_len] = '\0';
   if (strstr(request, "\r\n\r\n") == NULL)
   {
      return FALSE;
   }
   if (strncmp(request, "GET ", 4) != 0 ||
       pal_ws_header_value(request, "Origin") != NULL)
   {
      pal_ws_close_client();
      return TRUE;
   }
   key = pal_ws_header_value(request, "Sec-WebSocket-Key");
   if (key == NULL)
   {
      pal_ws_close_client();
      return TRUE;
   }
   key_end = strstr(key, "\r\n");
   if (key_end == NULL || key_end <= key)
   {
      pal_ws_close_client();
      return TRUE;
   }
   key_len = (size_t)(key_end - key);
   if (key_len + strlen(PAL_WS_GUID) >= sizeof(joined))
   {
      pal_ws_close_client();
      return TRUE;
   }
   memcpy(joined, key, key_len);
   memcpy(joined + key_len, PAL_WS_GUID, strlen(PAL_WS_GUID));
   pal_ws_sha1_init(&sha1);
   pal_ws_sha1_update(&sha1, (const uint8_t *)joined,
      key_len + strlen(PAL_WS_GUID));
   pal_ws_sha1_final(&sha1, digest);
   pal_ws_base64_20(digest, accept);
   response_len = snprintf(response, sizeof(response),
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
   if (response_len <= 0 || (size_t)response_len >= sizeof(response) ||
       !pal_ws_queue_raw(response, (size_t)response_len))
   {
      pal_ws_close_client();
      return TRUE;
   }
   pal_ws_open = TRUE;
   pal_ws_input_len = 0;
   return TRUE;
}

static BOOL
pal_ws_json_value(
   const char *json,
   const char *key,
   const char **output
)
{
   char pattern[48];
   const char *value;

   if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) <= 0)
   {
      return FALSE;
   }
   value = json;
   while ((value = strstr(value, pattern)) != NULL)
   {
      value += strlen(pattern);
      while (*value == ' ' || *value == '\t')
      {
         value++;
      }
      if (*value == ':')
      {
         value++;
         while (*value == ' ' || *value == '\t')
         {
            value++;
         }
         *output = value;
         return TRUE;
      }
      if (*value != '\0')
      {
         value++;
      }
   }
   return FALSE;
}

static BOOL
pal_ws_json_string(
   const char *json,
   const char *key,
   char *output,
   size_t output_size
)
{
   const char *value;
   const char *end;
   size_t length;

   if (!pal_ws_json_value(json, key, &value))
   {
      return FALSE;
   }
   if (*value++ != '\"')
   {
      return FALSE;
   }
   end = strchr(value, '\"');
   if (end == NULL)
   {
      return FALSE;
   }
   length = (size_t)(end - value);
   if (length == 0 || length >= output_size || memchr(value, '\\', length))
   {
      return FALSE;
   }
   memcpy(output, value, length);
   output[length] = '\0';
   return TRUE;
}

static BOOL
pal_ws_json_int(
   const char *json,
   const char *key,
   int *output
)
{
   const char *value;
   char *end;
   long parsed;

   if (!pal_ws_json_value(json, key, &value))
   {
      return FALSE;
   }
   errno = 0;
   parsed = strtol(value, &end, 10);
   if (errno != 0 || end == value || parsed < INT32_MIN || parsed > INT32_MAX)
   {
      return FALSE;
   }
   *output = (int)parsed;
   return TRUE;
}

static DWORD
pal_ws_key(
   const char *name
)
{
   static const struct
   {
      const char *name;
      DWORD key;
   } keys[] = {
      { "up", kKeyUp }, { "down", kKeyDown },
      { "left", kKeyLeft }, { "right", kKeyRight },
      { "escape", kKeyMenu }, { "menu", kKeyMenu },
      { "enter", kKeySearch }, { "search", kKeySearch },
      { "pageup", kKeyPgUp }, { "pagedown", kKeyPgDn },
      { "home", kKeyHome }, { "end", kKeyEnd },
      { "repeat", kKeyRepeat }, { "auto", kKeyAuto },
      { "defend", kKeyDefend }, { "use", kKeyUseItem },
      { "throw", kKeyThrowItem }, { "flee", kKeyFlee },
      { "force", kKeyForce }, { "status", kKeyStatus },
   };
   size_t i;

   for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
   {
      if (strcmp(name, keys[i].name) == 0)
      {
         return keys[i].key;
      }
   }
   return kKeyNone;
}

static BOOL
pal_ws_surface_rgb(
   SDL_Surface *surface,
   int x,
   int y,
   uint8_t *output
)
{
   const uint8_t *pixel;
   Uint32 value = 0;

   pixel = (const uint8_t *)surface->pixels +
      (size_t)y * surface->pitch +
      (size_t)x * surface->format->BytesPerPixel;
   switch (surface->format->BytesPerPixel)
   {
   case 1:
      value = pixel[0];
      if (surface->format->palette != NULL &&
          value < (Uint32)surface->format->palette->ncolors)
      {
         SDL_Color color = surface->format->palette->colors[value];
         output[0] = color.r;
         output[1] = color.g;
         output[2] = color.b;
         return TRUE;
      }
      output[0] = output[1] = output[2] = (uint8_t)value;
      return TRUE;
   case 2:
      memcpy(&value, pixel, 2);
      break;
   case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
      value = ((Uint32)pixel[0] << 16) |
         ((Uint32)pixel[1] << 8) | pixel[2];
#else
      value = pixel[0] | ((Uint32)pixel[1] << 8) |
         ((Uint32)pixel[2] << 16);
#endif
      break;
   case 4:
      memcpy(&value, pixel, 4);
      break;
   default:
      return FALSE;
   }
   SDL_GetRGB(value, surface->format,
      &output[0], &output[1], &output[2]);
   return TRUE;
}

static int
pal_ws_screen_width(
   void
)
{
#if defined(PAL_EXTREME_TWO_SCREENS)
   return gpScreen == NULL ? 0 : gpScreen->w;
#else
   return gpScreen == NULL ? 0 : gpScreen->w;
#endif
}

static int
pal_ws_screen_height(
   void
)
{
#if defined(PAL_EXTREME_TWO_SCREENS)
   return gpScreen == NULL ? 0 : gpScreen->h;
#else
   return gpScreen == NULL ? 0 : gpScreen->h;
#endif
}

static BOOL
pal_ws_screen_source(
   int destination_x,
   int destination_y,
   int *source_x,
   int *source_y
)
{
#if defined(PAL_EXTREME_TWO_SCREENS)
   if (destination_x < 0 || destination_y < 0 ||
       gpScreen == NULL || destination_x >= gpScreen->w ||
       destination_y >= gpScreen->h)
   {
      return FALSE;
   }
   *source_x = destination_x;
   *source_y = destination_y;
#else
   *source_x = destination_x;
   *source_y = destination_y;
#endif
   return *source_x >= 0 && *source_y >= 0 && gpScreen != NULL &&
      *source_x < gpScreen->w && *source_y < gpScreen->h;
}

static void
pal_ws_screenshot(
   void
)
{
   size_t payload_size;
   size_t header_size;
   uint8_t *payload;
   BOOL locked;
   int width;
   int height;
   int x;
   int y;

   width = pal_ws_screen_width();
   height = pal_ws_screen_height();
   if (gpScreen == NULL || gpScreen->pixels == NULL ||
       width <= 0 || height <= 0 || width > 320 || height > 200)
   {
      pal_ws_reply_error("screen unavailable");
      return;
   }
   payload_size = 12u + (size_t)width * (size_t)height * 3u;
   header_size = pal_ws_frame_header(2u, payload_size);
   if (header_size + payload_size > sizeof(pal_ws_output))
   {
      pal_ws_reply_error("screen too large");
      return;
   }
   payload = pal_ws_output + header_size;
   memcpy(payload, "PALSHOT1", 8);
   payload[8] = (uint8_t)(width >> 8);
   payload[9] = (uint8_t)width;
   payload[10] = (uint8_t)(height >> 8);
   payload[11] = (uint8_t)height;
   locked = SDL_MUSTLOCK(gpScreen);
   if (locked && SDL_LockSurface(gpScreen) != 0)
   {
      pal_ws_reply_error("screen lock failed");
      return;
   }
   for (y = 0; y < height; y++)
   {
      for (x = 0; x < width; x++)
      {
         int source_x;
         int source_y;

         if (!pal_ws_screen_source(x, y, &source_x, &source_y) ||
             !pal_ws_surface_rgb(gpScreen, source_x, source_y,
                payload + 12u +
                ((size_t)y * width + x) * 3u))
         {
            if (locked)
            {
               SDL_UnlockSurface(gpScreen);
            }
            pal_ws_reply_error("unsupported screen format");
            return;
         }
      }
   }
   if (locked)
   {
      SDL_UnlockSurface(gpScreen);
   }
   pal_ws_output_len = header_size + payload_size;
   pal_ws_output_sent = 0;
}

static void
pal_ws_command(
   const uint8_t *payload,
   size_t payload_size
)
{
   char json[PAL_WS_INPUT_BYTES + 1];
   char command[32];

   if (payload_size == 0 || payload_size > PAL_WS_INPUT_BYTES)
   {
      pal_ws_reply_error("invalid command size");
      return;
   }
   memcpy(json, payload, payload_size);
   json[payload_size] = '\0';
   if (!pal_ws_json_string(json, "cmd", command, sizeof(command)))
   {
      pal_ws_reply_error("missing cmd");
      return;
   }
   if (strcmp(command, "status") == 0)
   {
      char response[560];
      int world_x = 0;
      int world_y = 0;
      if (gpGlobals != NULL)
      {
         world_x = PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset);
         world_y = PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset);
      }
      snprintf(response, sizeof(response),
         "{\"ok\":true,\"scene\":%u,\"battlefield\":%u,\"x\":%d,\"y\":%d,"
         "\"main_game\":%s,\"battle\":%s,\"screen_width\":%d,"
         "\"screen_height\":%d,\"battle_ui_state\":%d,"
         "\"battle_menu_state\":%d,\"battle_result\":%d,"
         "\"battle_enemies\":%u,\"last_battle_team\":%d,"
         "\"last_battle_result\":%d,\"battle_player\":%d,"
         "\"battle_selected_action\":%d,"
         "\"battle_selected_index\":%d,\"save_slot\":%u,"
         "\"party_members\":%u,\"ui\":\"%s\",\"script\":%s,"
         "\"dialog\":%s}",
         gpGlobals == NULL ? 0u : (unsigned)gpGlobals->wNumScene,
         gpGlobals == NULL ? 0u : (unsigned)gpGlobals->wNumBattleField,
         world_x, world_y,
         gpGlobals != NULL && gpGlobals->fInMainGame ? "true" : "false",
         gpGlobals != NULL && gpGlobals->fInBattle ? "true" : "false",
         pal_ws_screen_width(), pal_ws_screen_height(),
         gpGlobals != NULL && gpGlobals->fInBattle ?
            (int)g_Battle.UI.state : -1,
         gpGlobals != NULL && gpGlobals->fInBattle ?
            (int)g_Battle.UI.MenuState : -1,
         gpGlobals != NULL && gpGlobals->fInBattle ?
            (int)g_Battle.BattleResult : -1,
         gpGlobals != NULL && gpGlobals->fInBattle ?
            (unsigned)g_Battle.wMaxEnemyIndex + 1u : 0u,
         pal_ws_last_battle_team, pal_ws_last_battle_result,
         gpGlobals != NULL && gpGlobals->fInBattle ?
            (int)g_Battle.UI.wCurPlayerIndex : -1,
         gpGlobals != NULL && gpGlobals->fInBattle ?
            (int)g_Battle.UI.wSelectedAction : -1,
         gpGlobals != NULL && gpGlobals->fInBattle ?
            g_Battle.UI.iSelectedIndex : -1,
         gpGlobals == NULL ? 0u : (unsigned)gpGlobals->bCurrentSaveSlot,
         gpGlobals == NULL ? 0u :
            (unsigned)gpGlobals->wMaxPartyMemberIndex + 1u,
         pal_ws_ui_name(pal_ws_active_ui),
         pal_ws_active_script ? "true" : "false",
         PAL_IsInDialog() ? "true" : "false");
      pal_ws_reply_text(response);
      return;
   }
   if (strcmp(command, "event") == 0)
   {
      EVENTOBJECT event_object;
      int event;
      char response[512];

      if (!pal_ws_json_int(json, "event", &event) || gpGlobals == NULL ||
          event <= 0 || event > gpGlobals->g.nEventObject ||
          !PAL_EventObjectRead((WORD)event, &event_object))
      {
         pal_ws_reply_error("event object out of range");
         return;
      }
      snprintf(response, sizeof(response),
         "{\"ok\":true,\"event\":%d,\"vanish_time\":%d,"
         "\"x\":%u,\"y\":%u,\"layer\":%d,"
         "\"trigger_script\":%u,\"auto_script\":%u,\"state\":%d,"
         "\"trigger_mode\":%u,\"sprite\":%u,\"sprite_frames\":%u,"
         "\"direction\":%u,\"current_frame\":%u,"
         "\"trigger_idle_frames\":%u,\"auto_idle_frames\":%u}",
         event, (int)event_object.sVanishTime,
         (unsigned)event_object.x, (unsigned)event_object.y,
         (int)event_object.sLayer,
         (unsigned)event_object.wTriggerScript,
         (unsigned)event_object.wAutoScript, (int)event_object.sState,
         (unsigned)event_object.wTriggerMode,
         (unsigned)event_object.wSpriteNum,
         (unsigned)event_object.nSpriteFrames,
         (unsigned)event_object.wDirection,
         (unsigned)event_object.wCurrentFrameNum,
         (unsigned)event_object.nScriptIdleFrame,
         (unsigned)event_object.wScriptIdleFrameCountAuto);
      pal_ws_reply_text(response);
      return;
   }
   if (strcmp(command, "screenshot") == 0)
   {
      pal_ws_screenshot();
      return;
   }
   if (strcmp(command, "input") == 0)
   {
      char key_name[24];
      char action_name[12] = "tap";
      DWORD key;
      INT action = PAL_WS_KEY_TAP;
      char response[128];

      if (!pal_ws_json_string(json, "key", key_name, sizeof(key_name)))
      {
         pal_ws_reply_error("missing key");
         return;
      }
      (void)pal_ws_json_string(json, "action", action_name,
         sizeof(action_name));
      key = pal_ws_key(key_name);
      if (key == kKeyNone)
      {
         pal_ws_reply_error("unknown key");
         return;
      }
      if (strcmp(action_name, "down") == 0)
      {
         action = PAL_WS_KEY_DOWN;
      }
      else if (strcmp(action_name, "up") == 0)
      {
         action = PAL_WS_KEY_UP;
      }
      else if (strcmp(action_name, "tap") != 0)
      {
         pal_ws_reply_error("unknown action");
         return;
      }
      PAL_WsInputKey(key, action);
      snprintf(response, sizeof(response),
         "{\"ok\":true,\"key\":\"%s\",\"action\":\"%s\"}",
         key_name, action_name);
      pal_ws_reply_text(response);
      return;
   }
   if (strcmp(command, "load") == 0)
   {
      int slot;
      char response[96];

      if (!pal_ws_json_int(json, "slot", &slot) || slot < 1 || slot > 5)
      {
         pal_ws_reply_error("save slot out of range");
         return;
      }
      if (gpGlobals == NULL || !gpGlobals->fInMainGame ||
          gpGlobals->fInBattle || pal_ws_pending_battle >= 0 ||
          pal_ws_pending_script >= 0 || pal_ws_pending_shop >= 0 ||
          pal_ws_pending_ui != PAL_WS_UI_NONE ||
          pal_ws_active_ui != PAL_WS_UI_NONE || pal_ws_active_script)
      {
         pal_ws_reply_error("load requires idle field gameplay");
         return;
      }
      PAL_ReloadInNextTick(slot);
      snprintf(response, sizeof(response),
         "{\"ok\":true,\"slot\":%d}", slot);
      pal_ws_reply_text(response);
      return;
   }
   if (strcmp(command, "script") == 0)
   {
      int entry;
      int event = 0;
      char response[112];

      if (!pal_ws_json_int(json, "entry", &entry) || gpGlobals == NULL ||
          entry <= 0 || entry >= gpGlobals->g.nScriptEntry)
      {
         pal_ws_reply_error("script entry out of range");
         return;
      }
      if (pal_ws_json_int(json, "event", &event) &&
          (event < 0 || event > 0xffff))
      {
         pal_ws_reply_error("event object out of range");
         return;
      }
      if (!gpGlobals->fInMainGame || gpGlobals->fInBattle ||
          pal_ws_pending_battle >= 0 || pal_ws_pending_script >= 0 ||
          pal_ws_pending_shop >= 0 ||
          pal_ws_pending_ui != PAL_WS_UI_NONE ||
          pal_ws_active_ui != PAL_WS_UI_NONE || pal_ws_active_script)
      {
         pal_ws_reply_error("script trigger requires idle field gameplay");
         return;
      }
      pal_ws_pending_script = entry;
      pal_ws_pending_script_event = event;
      snprintf(response, sizeof(response),
         "{\"ok\":true,\"entry\":%d,\"event\":%d}", entry, event);
      pal_ws_reply_text(response);
      return;
   }
   if (strcmp(command, "battle") == 0)
   {
      int team;
      int battlefield = 0;
      int auto_battle = 0;
      BOOL have_battlefield;
      char response[160];

      if (!pal_ws_json_int(json, "team", &team) || gpGlobals == NULL ||
          team < 0 || team >= gpGlobals->g.nEnemyTeam)
      {
         pal_ws_reply_error("battle team out of range");
         return;
      }
      if (gpGlobals == NULL || !gpGlobals->fInMainGame ||
          gpGlobals->fInBattle || pal_ws_pending_battle >= 0 ||
          pal_ws_pending_script >= 0 || pal_ws_pending_shop >= 0 ||
          pal_ws_pending_ui != PAL_WS_UI_NONE ||
          pal_ws_active_ui != PAL_WS_UI_NONE || pal_ws_active_script)
      {
         pal_ws_reply_error("battle start requires idle field gameplay");
         return;
      }
      if (pal_ws_json_int(json, "auto", &auto_battle) &&
          auto_battle != 0 && auto_battle != 1)
      {
         pal_ws_reply_error("auto must be zero or one");
         return;
      }
      have_battlefield = pal_ws_json_int(
         json, "battlefield", &battlefield);
      if (have_battlefield &&
          (battlefield < 0 || battlefield >= gpGlobals->g.nBattleField))
      {
         pal_ws_reply_error("battlefield out of range");
         return;
      }
      if (have_battlefield)
      {
         gpGlobals->wNumBattleField = (WORD)battlefield;
      }
      pal_ws_pending_battle = team;
      pal_ws_pending_battle_auto = auto_battle != 0;
      snprintf(response, sizeof(response),
         "{\"ok\":true,\"team\":%d,\"battlefield\":%u,\"auto\":%s}",
         team, (unsigned)gpGlobals->wNumBattleField,
         pal_ws_pending_battle_auto ? "true" : "false");
      pal_ws_reply_text(response);
      return;
   }
   if (strcmp(command, "shop") == 0)
   {
      int store;
      char response[96];

      if (!pal_ws_json_int(json, "store", &store) || gpGlobals == NULL ||
          store < 0 || store >= gpGlobals->g.nStore)
      {
         pal_ws_reply_error("store out of range");
         return;
      }
      if (!gpGlobals->fInMainGame || gpGlobals->fInBattle ||
          pal_ws_pending_battle >= 0 || pal_ws_pending_script >= 0 ||
          pal_ws_pending_shop >= 0 ||
          pal_ws_pending_ui != PAL_WS_UI_NONE ||
          pal_ws_active_ui != PAL_WS_UI_NONE || pal_ws_active_script)
      {
         pal_ws_reply_error("shop requires idle field gameplay");
         return;
      }
      pal_ws_pending_shop = store;
      snprintf(response, sizeof(response),
         "{\"ok\":true,\"store\":%d}", store);
      pal_ws_reply_text(response);
      return;
   }
   if (strcmp(command, "ui") == 0)
   {
      char name[16];
      int ui = PAL_WS_UI_NONE;
      char response[80];

      if (!pal_ws_json_string(json, "name", name, sizeof(name)))
      {
         pal_ws_reply_error("missing UI name");
         return;
      }
      if (strcmp(name, "main") == 0) ui = PAL_WS_UI_MAIN;
      else if (strcmp(name, "status") == 0) ui = PAL_WS_UI_STATUS;
      else if (strcmp(name, "items") == 0) ui = PAL_WS_UI_ITEMS;
      else if (strcmp(name, "magic") == 0) ui = PAL_WS_UI_MAGIC;
      else if (strcmp(name, "save") == 0) ui = PAL_WS_UI_SAVE;
      else if (strcmp(name, "confirm") == 0) ui = PAL_WS_UI_CONFIRM;
      else
      {
         pal_ws_reply_error("unknown UI name");
         return;
      }
      if (gpGlobals == NULL || !gpGlobals->fInMainGame ||
          gpGlobals->fInBattle || pal_ws_pending_battle >= 0 ||
          pal_ws_pending_script >= 0 || pal_ws_pending_shop >= 0 ||
          pal_ws_pending_ui != PAL_WS_UI_NONE ||
          pal_ws_active_ui != PAL_WS_UI_NONE || pal_ws_active_script)
      {
         pal_ws_reply_error("UI open requires idle field gameplay");
         return;
      }
      pal_ws_pending_ui = ui;
      snprintf(response, sizeof(response),
         "{\"ok\":true,\"ui\":\"%s\"}", name);
      pal_ws_reply_text(response);
      return;
   }
   if (strcmp(command, "scene") == 0)
   {
      int scene;
      int x;
      int y;
      BOOL have_x;
      BOOL have_y;
      char response[160];

      if (!pal_ws_json_int(json, "scene", &scene) ||
          scene <= 0 || scene >= MAX_SCENES)
      {
         pal_ws_reply_error("scene out of range");
         return;
      }
      if (gpGlobals == NULL || !gpGlobals->fInMainGame ||
          gpGlobals->fInBattle || pal_ws_pending_ui != PAL_WS_UI_NONE ||
          pal_ws_active_ui != PAL_WS_UI_NONE ||
          pal_ws_pending_script >= 0 || pal_ws_active_script)
      {
         pal_ws_reply_error("scene switch requires field gameplay");
         return;
      }
      have_x = pal_ws_json_int(json, "x", &x);
      have_y = pal_ws_json_int(json, "y", &y);
      if (have_x != have_y)
      {
         pal_ws_reply_error("x and y must be supplied together");
         return;
      }
      if (have_x)
      {
         gpGlobals->viewport = PAL_XY(
            x - PAL_X(gpGlobals->partyoffset),
            y - PAL_Y(gpGlobals->partyoffset));
      }
      gpGlobals->wNumScene = (WORD)scene;
      gpGlobals->fEnteringScene = TRUE;
      gpGlobals->fNeedToFadeIn = FALSE;
      snprintf(response, sizeof(response),
         "{\"ok\":true,\"scene\":%d,\"x\":%d,\"y\":%d}",
         scene,
         PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset),
         PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset));
      pal_ws_reply_text(response);
      return;
   }
   pal_ws_reply_error("unknown cmd");
}

static void
pal_ws_consume_frame(
   void
)
{
   uint8_t first;
   uint8_t second;
   uint8_t opcode;
   size_t header = 2;
   uint64_t payload_size;
   uint8_t mask[4];
   uint8_t *payload;
   size_t total;
   size_t i;

   if (pal_ws_input_len < 2)
   {
      return;
   }
   first = pal_ws_input[0];
   second = pal_ws_input[1];
   opcode = first & 0x0fu;
   payload_size = second & 0x7fu;
   if ((first & 0x80u) == 0 || (second & 0x80u) == 0)
   {
      pal_ws_close_client();
      return;
   }
   if (payload_size == 126u)
   {
      if (pal_ws_input_len < 4)
      {
         return;
      }
      payload_size = ((uint64_t)pal_ws_input[2] << 8) | pal_ws_input[3];
      header = 4;
   }
   else if (payload_size == 127u)
   {
      unsigned j;
      if (pal_ws_input_len < 10)
      {
         return;
      }
      payload_size = 0;
      for (j = 0; j < 8; j++)
      {
         payload_size = (payload_size << 8) | pal_ws_input[2 + j];
      }
      header = 10;
   }
   if (payload_size > PAL_WS_INPUT_BYTES ||
       header + 4u + payload_size > PAL_WS_INPUT_BYTES)
   {
      pal_ws_close_client();
      return;
   }
   total = header + 4u + (size_t)payload_size;
   if (pal_ws_input_len < total)
   {
      return;
   }
   memcpy(mask, pal_ws_input + header, sizeof(mask));
   payload = pal_ws_input + header + 4u;
   for (i = 0; i < (size_t)payload_size; i++)
   {
      payload[i] ^= mask[i & 3u];
   }

   if (opcode == 1u)
   {
      pal_ws_command(payload, (size_t)payload_size);
   }
   else if (opcode == 8u)
   {
      pal_ws_close_after_send = TRUE;
      (void)pal_ws_queue_frame(8u, payload,
         payload_size <= 125u ? (size_t)payload_size : 0u);
   }
   else if (opcode == 9u)
   {
      (void)pal_ws_queue_frame(10u, payload, (size_t)payload_size);
   }
   else
   {
      pal_ws_reply_error("unsupported frame");
   }
   if (pal_ws_client >= 0 && total < pal_ws_input_len)
   {
      memmove(pal_ws_input, pal_ws_input + total,
         pal_ws_input_len - total);
      pal_ws_input_len -= total;
   }
   else if (pal_ws_client >= 0)
   {
      pal_ws_input_len = 0;
   }
}

static void
pal_ws_flush(
   void
)
{
   ssize_t written;

   if (pal_ws_client < 0 || pal_ws_output_sent >= pal_ws_output_len)
   {
      return;
   }
   written = send(pal_ws_client,
      pal_ws_output + pal_ws_output_sent,
      pal_ws_output_len - pal_ws_output_sent, MSG_NOSIGNAL);
   if (written > 0)
   {
      pal_ws_output_sent += (size_t)written;
      if (pal_ws_output_sent == pal_ws_output_len)
      {
         pal_ws_output_len = 0;
         pal_ws_output_sent = 0;
         if (pal_ws_close_after_send)
         {
            pal_ws_close_client();
         }
      }
   }
   else if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
            errno != EINTR)
   {
      pal_ws_close_client();
   }
}

VOID
PAL_WsServer_Init(
   VOID
)
{
   const char *port_text = getenv("PAL_WS_PORT");
   char *end = NULL;
   long port;
   struct sockaddr_in address;
   int enabled = 1;

   if (port_text == NULL || port_text[0] == '\0')
   {
      return;
   }
   errno = 0;
   port = strtol(port_text, &end, 10);
   if (errno != 0 || end == port_text || *end != '\0' ||
       port <= 0 || port > 65535)
   {
      UTIL_LogOutput(LOGLEVEL_WARNING,
         "PAL_WS_PORT is invalid; WebSocket control is disabled\n");
      return;
   }
   pal_ws_listen = socket(AF_INET, SOCK_STREAM, 0);
   if (pal_ws_listen < 0 ||
       setsockopt(pal_ws_listen, SOL_SOCKET, SO_REUSEADDR,
          &enabled, sizeof(enabled)) != 0 ||
       pal_ws_nonblocking(pal_ws_listen) != 0)
   {
      PAL_WsServer_Shutdown();
      return;
   }
   memset(&address, 0, sizeof(address));
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   address.sin_port = htons((uint16_t)port);
   if (bind(pal_ws_listen, (struct sockaddr *)&address,
          sizeof(address)) != 0 || listen(pal_ws_listen, 1) != 0)
   {
      UTIL_LogOutput(LOGLEVEL_WARNING,
         "Could not bind PAL WebSocket control to 127.0.0.1:%ld\n", port);
      PAL_WsServer_Shutdown();
      return;
   }
   UTIL_LogOutput(LOGLEVEL_INFO,
      "PAL WebSocket control listening on ws://127.0.0.1:%ld\n", port);
}

VOID
PAL_WsServer_Poll(
   VOID
)
{
   ssize_t received;

   if (pal_ws_listen < 0)
   {
      return;
   }
   if (pal_ws_client < 0)
   {
      pal_ws_client = accept(pal_ws_listen, NULL, NULL);
      if (pal_ws_client < 0)
      {
         return;
      }
      if (pal_ws_nonblocking(pal_ws_client) != 0)
      {
         pal_ws_close_client();
         return;
      }
   }
   pal_ws_flush();
   if (pal_ws_client < 0 || pal_ws_output_len != 0 ||
       pal_ws_input_len == sizeof(pal_ws_input))
   {
      return;
   }
   received = recv(pal_ws_client, pal_ws_input + pal_ws_input_len,
      sizeof(pal_ws_input) - pal_ws_input_len, 0);
   if (received > 0)
   {
      pal_ws_input_len += (size_t)received;
      if (!pal_ws_open)
      {
         (void)pal_ws_handshake();
      }
      else
      {
         pal_ws_consume_frame();
      }
   }
   else if (received == 0)
   {
      pal_ws_close_client();
   }
   else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
   {
      pal_ws_close_client();
   }
   pal_ws_flush();
   if (pal_ws_pending_battle >= 0 && pal_ws_output_len == 0)
   {
      int team = pal_ws_pending_battle;
      BOOL auto_battle = pal_ws_pending_battle_auto;
      BOOL old_auto_battle;

      /* Clear before entering the recursive battle loop: PAL_ProcessEvent()
       * keeps polling this same server while the battle is active. */
      pal_ws_pending_battle = -1;
      pal_ws_pending_battle_auto = FALSE;
      old_auto_battle = gpGlobals->fAutoBattle;
      gpGlobals->fAutoBattle = auto_battle;
      pal_ws_last_battle_team = team;
      PAL_ClearKeyState();
      pal_ws_last_battle_result = (int)PAL_StartBattle((WORD)team, FALSE);
      gpGlobals->fAutoBattle = old_auto_battle;
   }
   if (pal_ws_pending_script >= 0 && pal_ws_output_len == 0)
   {
      int entry = pal_ws_pending_script;
      int event = pal_ws_pending_script_event;

      pal_ws_pending_script = -1;
      pal_ws_pending_script_event = 0;
      pal_ws_active_script = TRUE;
      (void)PAL_RunTriggerScript((WORD)entry, (WORD)event);
      pal_ws_active_script = FALSE;
   }
   if (pal_ws_pending_shop >= 0 && pal_ws_output_len == 0)
   {
      int store = pal_ws_pending_shop;

      pal_ws_pending_shop = -1;
      pal_ws_active_ui = PAL_WS_UI_SHOP;
      PAL_MakeScene();
      VIDEO_UpdateScreen(NULL);
      PAL_BuyMenu((WORD)store);
      pal_ws_active_ui = PAL_WS_UI_NONE;
      PAL_MakeScene();
      VIDEO_UpdateScreen(NULL);
   }
   if (pal_ws_pending_ui != PAL_WS_UI_NONE && pal_ws_output_len == 0)
   {
      int ui = pal_ws_pending_ui;

      pal_ws_pending_ui = PAL_WS_UI_NONE;
      pal_ws_active_ui = ui;
      switch (ui)
      {
      case PAL_WS_UI_MAIN:
         PAL_InGameMenu();
         break;
      case PAL_WS_UI_STATUS:
         PAL_PlayerStatus();
         break;
      case PAL_WS_UI_ITEMS:
         (void)PAL_ItemSelectMenu(NULL, 0xffffu);
         break;
      case PAL_WS_UI_MAGIC:
         PAL_InGameMagicMenu();
         break;
      case PAL_WS_UI_SAVE:
         (void)PAL_SaveSlotMenu(gpGlobals->bCurrentSaveSlot);
         break;
      case PAL_WS_UI_CONFIRM:
         (void)PAL_ConfirmMenu();
         break;
      default:
         break;
      }
      pal_ws_active_ui = PAL_WS_UI_NONE;
      PAL_MakeScene();
      VIDEO_UpdateScreen(NULL);
   }
}

VOID
PAL_WsServer_Shutdown(
   VOID
)
{
   pal_ws_close_client();
   pal_ws_pending_battle = -1;
   pal_ws_pending_battle_auto = FALSE;
   pal_ws_pending_script = -1;
   pal_ws_pending_script_event = 0;
   pal_ws_active_script = FALSE;
   pal_ws_pending_shop = -1;
   pal_ws_pending_ui = PAL_WS_UI_NONE;
   pal_ws_active_ui = PAL_WS_UI_NONE;
   pal_ws_last_battle_team = -1;
   pal_ws_last_battle_result = -1;
   if (pal_ws_listen >= 0)
   {
      close(pal_ws_listen);
      pal_ws_listen = -1;
   }
}
