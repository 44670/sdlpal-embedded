/* -*- mode: c; tab-width: 4; c-basic-offset: 4; c-file-style: "linux" -*- */

#include "common.h"
#include "battle.h"
#include "global.h"
#include "input.h"
#include "palcfg.h"
#include "script.h"
#include "util.h"
#include "video.h"
#ifdef PAL_EXTREME_TWO_SCREENS
#include "cardputer_extreme_native_view.h"
#endif

#include <errno.h>
#include <png.h>

#ifdef SDL_PollEvent
#undef SDL_PollEvent
#endif
#ifdef SDL_GetTicks
#undef SDL_GetTicks
#endif
#ifdef SDL_Delay
#undef SDL_Delay
#endif

#define PAL_DETERMINISTIC_EVENT_CODE 0x50445250u
#define PAL_DETERMINISTIC_LINE_BYTES 128u
#define PAL_DETERMINISTIC_PNG_MAX_WIDTH 1024u
#ifndef PAL_EXTREME_TWO_SCREENS
#define PAL_DETERMINISTIC_LCD_WIDTH 320u
#define PAL_DETERMINISTIC_LCD_HEIGHT 240u
#define PAL_DETERMINISTIC_LCD_Y_OFFSET 20u
#endif
static Uint32 pal_deterministic_ticks;
static bool pal_deterministic_init_done;
static bool pal_deterministic_realtime;
static time_t pal_deterministic_epoch;
static FILE *pal_deterministic_replay;
static FILE *pal_deterministic_record;
static FILE *pal_deterministic_checkpoints;
static FILE *pal_deterministic_script_trace;
static FILE *pal_deterministic_event_trace;
static bool pal_deterministic_replay_eof;
static bool pal_deterministic_have_replay_state;
static Uint32 pal_deterministic_replay_tick;
static int pal_deterministic_replay_dir;
static DWORD pal_deterministic_replay_keys;
static Uint32 pal_deterministic_last_record_tick;
static int pal_deterministic_last_record_dir;
static DWORD pal_deterministic_last_record_keys;
static Uint32 pal_deterministic_frame;
static Uint32 pal_deterministic_max_presents;
static bool pal_deterministic_quit_sent;
static uint32_t pal_deterministic_script_hash = 2166136261u;
static uint32_t pal_deterministic_script_count;
static uint32_t pal_deterministic_script_sequence;
static uint32_t pal_deterministic_event_sequence;
static WORD pal_deterministic_last_scene = 0xffffu;
static bool pal_deterministic_have_last_scene;
static BOOL pal_deterministic_last_battle;
static bool pal_deterministic_have_last_battle;
static WORD pal_deterministic_script_event_stack[16];
static unsigned pal_deterministic_script_event_depth;
static bool pal_deterministic_log_registered;
static bool pal_deterministic_screenshot_written;
static bool pal_deterministic_save_written;
static bool pal_deterministic_reload_requested;
static bool pal_deterministic_force_battle_enabled;
static bool pal_deterministic_force_battle_active;
static bool pal_deterministic_force_battle_done;
static bool pal_deterministic_force_battle_auto = true;
static Uint32 pal_deterministic_force_battle_frame;
static WORD pal_deterministic_force_battle_team;
static bool pal_deterministic_force_chapter_enabled;
static bool pal_deterministic_force_chapter_active;
static bool pal_deterministic_force_chapter_done;
static Uint32 pal_deterministic_force_chapter_frame;
static png_byte pal_deterministic_png_row[PAL_DETERMINISTIC_PNG_MAX_WIDTH * 3u];

void __real_SDL_RenderPresent(SDL_Renderer *renderer);
WORD __real_PAL_RunTriggerScript(WORD wScriptEntry, WORD wEventObjectID);
WORD __real_PAL_RunAutoScript(WORD wScriptEntry, WORD wEventObjectID);
BOOL __real_PAL_SaveGame(int iSaveSlot, WORD wSavedTimes);
void __real_PAL_InitGameData(INT iSaveSlot);
void __real_PAL_ReloadInNextTick(INT iSaveSlot);

static void
pal_deterministic_log_callback(
   LOGLEVEL level,
   const char *full_log,
   const char *user_log
);

static unsigned long
read_env_ulong(
   const char *name,
   unsigned long default_value
)
{
   const char *text = SDL_getenv(name);
   char *end = NULL;
   unsigned long value;

   if (text == NULL || text[0] == '\0') {
      return default_value;
   }
   errno = 0;
   value = strtoul(text, &end, 0);
   if (errno != 0 || end == text || *end != '\0') {
      return default_value;
   }
   return value;
}

static WORD
pal_deterministic_event_id_env(
   void
)
{
   unsigned long event_id =
      read_env_ulong("PAL_DETERMINISTIC_EVENT_ID", 0);

   if (event_id == 0 || event_id > MAX_EVENT_OBJECTS ||
      (gpGlobals->g.nEventObject > 0 &&
       event_id > (unsigned long)gpGlobals->g.nEventObject)) {
      return 0;
   }
   return (WORD)event_id;
}

static void
pal_deterministic_close(
   void
)
{
   if (pal_deterministic_replay != NULL) {
      fclose(pal_deterministic_replay);
      pal_deterministic_replay = NULL;
   }
   if (pal_deterministic_record != NULL) {
      fclose(pal_deterministic_record);
      pal_deterministic_record = NULL;
   }
   if (pal_deterministic_checkpoints != NULL) {
      fclose(pal_deterministic_checkpoints);
      pal_deterministic_checkpoints = NULL;
   }
   if (pal_deterministic_script_trace != NULL) {
      fclose(pal_deterministic_script_trace);
      pal_deterministic_script_trace = NULL;
   }
   if (pal_deterministic_event_trace != NULL) {
      fclose(pal_deterministic_event_trace);
      pal_deterministic_event_trace = NULL;
   }
}

static void
pal_deterministic_init(
   void
)
{
   const char *replay_path;
   const char *record_path;
   const char *checkpoint_path;
   const char *script_trace_path;
   const char *event_trace_path;

   if (pal_deterministic_init_done) {
      return;
   }
   pal_deterministic_ticks = (Uint32)read_env_ulong("PAL_DETERMINISTIC_START_MS", 0);
   pal_deterministic_epoch = (time_t)read_env_ulong("PAL_DETERMINISTIC_EPOCH", 1);
   pal_deterministic_max_presents = (Uint32)read_env_ulong("PAL_DETERMINISTIC_MAX_PRESENTS", 0);
   {
      const char *force_team = SDL_getenv("PAL_DETERMINISTIC_FORCE_BATTLE_TEAM");
      if (force_team != NULL && force_team[0] != '\0') {
         pal_deterministic_force_battle_enabled = true;
         pal_deterministic_force_battle_team =
            (WORD)read_env_ulong("PAL_DETERMINISTIC_FORCE_BATTLE_TEAM", 0);
         pal_deterministic_force_battle_frame =
            (Uint32)read_env_ulong("PAL_DETERMINISTIC_FORCE_BATTLE_FRAME", 0);
         pal_deterministic_force_battle_auto =
            read_env_ulong("PAL_DETERMINISTIC_FORCE_BATTLE_AUTO", 1) != 0;
      }
   }
   {
      const char *force_chapter =
         SDL_getenv("PAL_DETERMINISTIC_FORCE_CHAPTER_BOUNDARY_FRAME");
      if (force_chapter != NULL && force_chapter[0] != '\0') {
         pal_deterministic_force_chapter_enabled = true;
         pal_deterministic_force_chapter_frame = (Uint32)read_env_ulong(
            "PAL_DETERMINISTIC_FORCE_CHAPTER_BOUNDARY_FRAME", 0);
      }
   }
   pal_deterministic_last_record_tick = (Uint32)-1;
   pal_deterministic_last_record_dir = -1;
   pal_deterministic_last_record_keys = (DWORD)-1;

   replay_path = SDL_getenv("PAL_DETERMINISTIC_REPLAY");
   /*
    * Automated native runs deliberately advance the deterministic clock
    * without sleeping.  A visible native SDL window, however, is operated by
    * a human and must use the underlying host clock unless it is replaying a
    * deterministic route.
    */
   pal_deterministic_realtime =
      (replay_path == NULL || replay_path[0] == '\0') &&
      read_env_ulong("PAL_CORES3SE_NATIVE_DISPLAY", 0) != 0;
   if (replay_path != NULL && replay_path[0] != '\0') {
      pal_deterministic_replay = fopen(replay_path, "r");
   }
   record_path = SDL_getenv("PAL_DETERMINISTIC_RECORD");
   if (record_path != NULL && record_path[0] != '\0') {
      pal_deterministic_record = fopen(record_path, "w");
   }
   checkpoint_path = SDL_getenv("PAL_DETERMINISTIC_CHECKPOINTS");
   if (checkpoint_path != NULL && checkpoint_path[0] != '\0') {
      pal_deterministic_checkpoints = fopen(checkpoint_path, "w");
   }
   script_trace_path = SDL_getenv("PAL_DETERMINISTIC_SCRIPT_TRACE");
   if (script_trace_path != NULL && script_trace_path[0] != '\0') {
      pal_deterministic_script_trace = fopen(script_trace_path, "w");
   }
   event_trace_path = SDL_getenv("PAL_DETERMINISTIC_EVENT_TRACE");
   if (event_trace_path != NULL && event_trace_path[0] != '\0') {
      pal_deterministic_event_trace = fopen(event_trace_path, "w");
   }
   if ((pal_deterministic_checkpoints != NULL || pal_deterministic_script_trace != NULL) &&
       !pal_deterministic_log_registered) {
      UTIL_LogSetLevel(LOGLEVEL_DEBUG);
      UTIL_LogAddOutputCallback(pal_deterministic_log_callback, LOGLEVEL_DEBUG);
      pal_deterministic_log_registered = true;
   }
   atexit(pal_deterministic_close);
   pal_deterministic_init_done = true;
}

static uint32_t
pal_deterministic_crc_bytes(
   uint32_t seed,
   const uint8_t *data,
   uint32_t size
)
{
   uint32_t i;
   uint32_t hash = seed;

   for (i = 0; i < size; i++) {
      hash ^= data[i];
      hash *= 16777619u;
   }
   return hash;
}

static uint32_t
pal_deterministic_screen_crc(
   void
)
{
   const uint8_t *pixels;
   uint32_t hash = 2166136261u;
   int y;
   int bytes_per_row;

   if (gpScreen == NULL || gpScreen->pixels == NULL || gpScreen->w <= 0 || gpScreen->h <= 0) {
      return 0;
   }

   bytes_per_row = gpScreen->w;
   if (gpScreen->format != NULL && gpScreen->format->BytesPerPixel > 0) {
      bytes_per_row *= gpScreen->format->BytesPerPixel;
   }

   pixels = (const uint8_t *)gpScreen->pixels;
   for (y = 0; y < gpScreen->h; y++) {
      hash = pal_deterministic_crc_bytes(hash, pixels + (uint32_t)y * (uint32_t)gpScreen->pitch, (uint32_t)bytes_per_row);
   }
   return hash;
}

static bool
pal_deterministic_surface_rgb(
   SDL_Surface *surface,
   int x,
   int y,
   png_byte *dst
)
{
   uint8_t *pixel;
   Uint32 value = 0;

   if (surface == NULL || surface->pixels == NULL || surface->format == NULL ||
       x < 0 || y < 0 || x >= surface->w || y >= surface->h || dst == NULL) {
      return false;
   }

   pixel = (uint8_t *)surface->pixels + (size_t)y * (size_t)surface->pitch +
      (size_t)x * (size_t)surface->format->BytesPerPixel;
   switch (surface->format->BytesPerPixel) {
   case 1:
      value = pixel[0];
      if (surface->format->palette != NULL &&
          value < (Uint32)surface->format->palette->ncolors) {
         SDL_Color color = surface->format->palette->colors[value];
         dst[0] = color.r;
         dst[1] = color.g;
         dst[2] = color.b;
      } else {
         dst[0] = (png_byte)value;
         dst[1] = (png_byte)value;
         dst[2] = (png_byte)value;
      }
      return true;
   case 2:
      memcpy(&value, pixel, 2);
      break;
   case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
      value = ((Uint32)pixel[0] << 16) | ((Uint32)pixel[1] << 8) | pixel[2];
#else
      value = pixel[0] | ((Uint32)pixel[1] << 8) | ((Uint32)pixel[2] << 16);
#endif
      break;
   case 4:
      memcpy(&value, pixel, 4);
      break;
   default:
      return false;
   }
   SDL_GetRGB(value, surface->format, &dst[0], &dst[1], &dst[2]);
   return true;
}

static bool
pal_deterministic_write_png(
   const char *path,
   bool lcd_frame
)
{
   FILE *fp;
   png_structp png;
   png_infop info;
   int y;
   bool locked = false;
   int png_width;
   int png_height;
   if (path == NULL || path[0] == '\0' || gpScreen == NULL || gpScreen->pixels == NULL ||
       gpScreen->w <= 0 || gpScreen->h <= 0 ||
       gpScreen->w > (int)PAL_DETERMINISTIC_PNG_MAX_WIDTH) {
      return false;
   }
#ifdef PAL_EXTREME_TWO_SCREENS
   png_width = gpScreen->w;
   png_height = gpScreen->h;
#else
   png_width = lcd_frame ? (int)PAL_DETERMINISTIC_LCD_WIDTH : gpScreen->w;
   png_height = lcd_frame ? (int)PAL_DETERMINISTIC_LCD_HEIGHT : gpScreen->h;
#endif
   if (png_width > (int)PAL_DETERMINISTIC_PNG_MAX_WIDTH) {
      return false;
   }
   if (SDL_MUSTLOCK(gpScreen) && SDL_LockSurface(gpScreen) != 0) {
      return false;
   }
   locked = SDL_MUSTLOCK(gpScreen);

   fp = fopen(path, "wb");
   if (fp == NULL) {
      if (locked) {
         SDL_UnlockSurface(gpScreen);
      }
      return false;
   }
   png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
   if (png == NULL) {
      fclose(fp);
      if (locked) {
         SDL_UnlockSurface(gpScreen);
      }
      return false;
   }
   info = png_create_info_struct(png);
   if (info == NULL || setjmp(png_jmpbuf(png))) {
      png_destroy_write_struct(&png, info != NULL ? &info : NULL);
      fclose(fp);
      if (locked) {
         SDL_UnlockSurface(gpScreen);
      }
      return false;
   }

   png_init_io(png, fp);
   png_set_IHDR(png, info, (png_uint_32)png_width, (png_uint_32)png_height, 8,
                PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
   png_write_info(png, info);
   for (y = 0; y < png_height; y++) {
      int x;
#ifdef PAL_EXTREME_TWO_SCREENS
      int source_y = y;
#else
      int source_y = lcd_frame ? y - (int)PAL_DETERMINISTIC_LCD_Y_OFFSET : y;
#endif
      for (x = 0; x < png_width; x++) {
         int source_x = x;
#ifdef PAL_EXTREME_TWO_SCREENS
         (void)lcd_frame;
#endif
         if (source_y < 0 || source_y >= gpScreen->h ||
             source_x < 0 || source_x >= gpScreen->w) {
            memset(pal_deterministic_png_row + (size_t)x * 3u, 0, 3);
         } else if (!pal_deterministic_surface_rgb(gpScreen, source_x, source_y, pal_deterministic_png_row + (size_t)x * 3u)) {
            memset(pal_deterministic_png_row + (size_t)x * 3u, 0, 3);
         }
      }
      png_write_row(png, pal_deterministic_png_row);
   }
   png_write_end(png, info);
   png_destroy_write_struct(&png, &info);
   fclose(fp);
   if (locked) {
      SDL_UnlockSurface(gpScreen);
   }
   return true;
}

static void
pal_deterministic_maybe_write_screenshot(
   void
)
{
   const char *path;
   const char *lcd_frame;
   unsigned long frame;

   if (pal_deterministic_screenshot_written) {
      return;
   }
   pal_deterministic_init();
   path = SDL_getenv("PAL_DETERMINISTIC_SCREENSHOT");
   if (path == NULL || path[0] == '\0') {
      return;
   }
   frame = read_env_ulong("PAL_DETERMINISTIC_SCREENSHOT_FRAME", 0);
   if ((unsigned long)pal_deterministic_frame != frame) {
      return;
   }
   lcd_frame = SDL_getenv("PAL_DETERMINISTIC_SCREENSHOT_LCD");
   pal_deterministic_screenshot_written = pal_deterministic_write_png(
      path,
      lcd_frame != NULL && strcmp(lcd_frame, "0") != 0 && lcd_frame[0] != '\0');
}

static void
pal_deterministic_maybe_save_game(
   void
)
{
   const char *frame_text;
   unsigned long frame;
   int slot;
   WORD saved_times;
   const char *generic_state_text;
   WORD generic_event_id;
   EVENTOBJECT generic_event;

   if (pal_deterministic_save_written) {
      return;
   }
   pal_deterministic_init();
   frame_text = SDL_getenv("PAL_DETERMINISTIC_SAVE_FRAME");
   if (frame_text == NULL || frame_text[0] == '\0') {
      return;
   }
   frame = read_env_ulong("PAL_DETERMINISTIC_SAVE_FRAME", 0);
   if ((unsigned long)pal_deterministic_frame != frame) {
      return;
   }

   slot = (int)read_env_ulong("PAL_DETERMINISTIC_SAVE_SLOT", 5);
   if (slot <= 0) {
      slot = 5;
   }
   saved_times = (WORD)read_env_ulong("PAL_DETERMINISTIC_SAVE_TIMES", 1);
   generic_event_id = pal_deterministic_event_id_env();
   generic_state_text = SDL_getenv("PAL_DETERMINISTIC_EVENT_STATE");
   if (generic_event_id != 0 &&
      generic_state_text != NULL && generic_state_text[0] != '\0') {
      if (!PAL_EventObjectRead(generic_event_id, &generic_event)) {
         TerminateOnError(
            "deterministic save: event-object read failed");
      }
      generic_event.sState = (SHORT)strtol(generic_state_text, NULL, 0);
      if (!PAL_EventObjectWrite(generic_event_id, &generic_event)) {
         TerminateOnError(
            "deterministic save: event-object write failed");
      }
   }
   PAL_SaveGame(slot, saved_times);
   pal_deterministic_save_written = true;
}

static void
pal_deterministic_maybe_reload_game(
   void
)
{
   const char *frame_text;
   unsigned long frame;
   int slot;

   if (pal_deterministic_reload_requested) {
      return;
   }
   pal_deterministic_init();
   frame_text = SDL_getenv("PAL_DETERMINISTIC_RELOAD_FRAME");
   if (frame_text == NULL || frame_text[0] == '\0') {
      return;
   }
   frame = read_env_ulong("PAL_DETERMINISTIC_RELOAD_FRAME", 0);
   if ((unsigned long)pal_deterministic_frame != frame) {
      return;
   }

   slot = (int)read_env_ulong("PAL_DETERMINISTIC_RELOAD_SLOT", 5);
   if (slot <= 0) {
      slot = 5;
   }
   PAL_ReloadInNextTick(slot);
   pal_deterministic_reload_requested = true;
}

static uint32_t
pal_deterministic_crc_u32(
   uint32_t seed,
   uint32_t value
)
{
   return pal_deterministic_crc_bytes(seed, (const uint8_t *)&value, sizeof(value));
}

static uint32_t
pal_deterministic_object_count(
   void
)
{
   int bytes;
   int count;
   int object_size;

   if (gpGlobals->f.fpSSS == NULL) {
      return MAX_OBJECTS;
   }

   bytes = PAL_MKFGetChunkSize(2, gpGlobals->f.fpSSS);
   object_size = gConfig.fIsWIN95 ? (int)sizeof(OBJECT) : (int)sizeof(OBJECT_DOS);
   if (bytes <= 0 || object_size <= 0 || (bytes % object_size) != 0) {
      return MAX_OBJECTS;
   }

   count = bytes / object_size;
   if (count < 0) {
      return 0;
   }
   if (count > MAX_OBJECTS) {
      return MAX_OBJECTS;
   }
   return (uint32_t)count;
}

static void
pal_deterministic_record_script(
   uint32_t kind,
   WORD entry,
   WORD event_object,
   WORD operation,
   WORD operand0,
   WORD operand1,
   WORD operand2,
   WORD result
)
{
   pal_deterministic_script_hash = pal_deterministic_crc_u32(pal_deterministic_script_hash, kind);
   pal_deterministic_script_hash = pal_deterministic_crc_u32(pal_deterministic_script_hash, (uint32_t)entry);
   pal_deterministic_script_hash = pal_deterministic_crc_u32(pal_deterministic_script_hash, (uint32_t)event_object);
   pal_deterministic_script_hash = pal_deterministic_crc_u32(pal_deterministic_script_hash, (uint32_t)operation);
   pal_deterministic_script_hash = pal_deterministic_crc_u32(pal_deterministic_script_hash, (uint32_t)operand0);
   pal_deterministic_script_hash = pal_deterministic_crc_u32(pal_deterministic_script_hash, (uint32_t)operand1);
   pal_deterministic_script_hash = pal_deterministic_crc_u32(pal_deterministic_script_hash, (uint32_t)operand2);
   pal_deterministic_script_hash = pal_deterministic_crc_u32(pal_deterministic_script_hash, (uint32_t)result);
   pal_deterministic_script_count++;

   if (pal_deterministic_script_trace != NULL) {
      fprintf(pal_deterministic_script_trace,
         "%lu script frame=%u tick=%u kind=%lu entry=%u event=%u op=%04x a=%u b=%u c=%u result=%u\n",
         (unsigned long)pal_deterministic_script_sequence++,
         (unsigned)pal_deterministic_frame,
         (unsigned)pal_deterministic_ticks,
         (unsigned long)kind,
         (unsigned)entry,
         (unsigned)event_object,
         (unsigned)operation,
         (unsigned)operand0,
         (unsigned)operand1,
         (unsigned)operand2,
         (unsigned)result);
      fflush(pal_deterministic_script_trace);
   }
}

static void
pal_deterministic_record_script_entry(
   uint32_t kind,
   WORD entry,
   WORD event_object,
   WORD result
)
{
   SCRIPTENTRY script = {0xffffu, {0xffffu, 0xffffu, 0xffffu}};

   if (gpGlobals->g.lprgScriptEntry != NULL &&
       entry < (WORD)gpGlobals->g.nScriptEntry) {
      script = gpGlobals->g.lprgScriptEntry[entry];
   }

   pal_deterministic_record_script(kind,
      entry,
      event_object,
      script.wOperation,
      script.rgwOperand[0],
      script.rgwOperand[1],
      script.rgwOperand[2],
      result);
}

static WORD
pal_deterministic_current_script_event(
   void
)
{
   if (pal_deterministic_script_event_depth == 0) {
      return 0xffffu;
   }
   return pal_deterministic_script_event_stack[pal_deterministic_script_event_depth - 1u];
}

static void
pal_deterministic_push_script_event(
   WORD event_object
)
{
   if (pal_deterministic_script_event_depth <
       sizeof(pal_deterministic_script_event_stack) / sizeof(pal_deterministic_script_event_stack[0])) {
      pal_deterministic_script_event_stack[pal_deterministic_script_event_depth++] = event_object;
   }
}

static void
pal_deterministic_pop_script_event(
   void
)
{
   if (pal_deterministic_script_event_depth > 0) {
      pal_deterministic_script_event_depth--;
   }
}

static void
pal_deterministic_log_callback(
   LOGLEVEL level,
   const char *full_log,
   const char *user_log
)
{
   unsigned entry;
   unsigned event_object;
   unsigned operation;
   unsigned operand0;
   unsigned operand1;
   unsigned operand2;
   const char *text = user_log != NULL ? user_log : full_log;
   const char *script;

   (void)level;

   if (text == NULL) {
      return;
   }

   script = strstr(text, "[AUTOSCRIPT]");
   if (script != NULL &&
       sscanf(script, "[AUTOSCRIPT] %x %x: %x %x %x %x",
          &event_object, &entry, &operation, &operand0, &operand1, &operand2) == 6) {
      pal_deterministic_record_script(4u,
         (WORD)entry,
         (WORD)event_object,
         (WORD)operation,
         (WORD)operand0,
         (WORD)operand1,
         (WORD)operand2,
         0xffffu);
      return;
   }

   script = strstr(text, "[SCRIPT]");
   if (script != NULL &&
       sscanf(script, "[SCRIPT] %x: %x %x %x %x",
          &entry, &operation, &operand0, &operand1, &operand2) == 5) {
      pal_deterministic_record_script(3u,
         (WORD)entry,
         pal_deterministic_current_script_event(),
         (WORD)operation,
         (WORD)operand0,
         (WORD)operand1,
         (WORD)operand2,
         0xffffu);
   }
}

static uint32_t
pal_deterministic_event_object_crc(
   void
)
{
   uint32_t hash = 2166136261u;

   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->g.nEventObject);
#if defined(PAL_PAGED_EVENT_STATE)
   {
      EVENTOBJECT event_object;
      WORD event_object_id;

      for (event_object_id = 1;
           event_object_id <= (WORD)gpGlobals->g.nEventObject;
           event_object_id++) {
         if (!PAL_EventObjectRead(event_object_id, &event_object)) {
            return 0;
         }
         hash = pal_deterministic_crc_bytes(hash,
            (const uint8_t *)&event_object, (uint32_t)sizeof(event_object));
      }
   }
#else
   if (gpGlobals->g.lprgEventObject != NULL && gpGlobals->g.nEventObject > 0) {
      hash = pal_deterministic_crc_bytes(hash,
         (const uint8_t *)gpGlobals->g.lprgEventObject,
         (uint32_t)gpGlobals->g.nEventObject * (uint32_t)sizeof(*gpGlobals->g.lprgEventObject));
   }
#endif
   return hash;
}

static uint32_t
pal_deterministic_state_crc(
   void
)
{
   uint32_t hash = 2166136261u;
   uint32_t object_count;

   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->iCurMainMenuItem);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->iCurSystemMenuItem);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->iCurInvMenuItem);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->iCurPlayingRNG);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->bCurrentSaveSlot);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->fInMainGame);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->fEnteringScene);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->fNeedToFadeIn);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->fInBattle);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->fAutoBattle);
#ifndef PAL_CLASSIC
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->bBattleSpeed);
#endif
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wLastUnequippedItem);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->viewport);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->partyoffset);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wLayer);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wMaxPartyMemberIndex);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wPartyDirection);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wNumScene);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wNumPalette);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->fNightPalette);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wNumMusic);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wNumBattleMusic);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wNumBattleField);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wCollectValue);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wScreenWave);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->sWaveProgression);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wChaseRange);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->wChasespeedChangeCycles);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->nFollower);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->dwCash);
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->dwFrameNum);

   hash = pal_deterministic_crc_bytes(hash, (const uint8_t *)gpGlobals->rgEquipmentEffect, sizeof(gpGlobals->rgEquipmentEffect));
   hash = pal_deterministic_crc_bytes(hash, (const uint8_t *)gpGlobals->rgPlayerStatus, sizeof(gpGlobals->rgPlayerStatus));
   hash = pal_deterministic_crc_bytes(hash, (const uint8_t *)gpGlobals->rgParty, sizeof(gpGlobals->rgParty));
   hash = pal_deterministic_crc_bytes(hash, (const uint8_t *)gpGlobals->rgTrail, sizeof(gpGlobals->rgTrail));
   hash = pal_deterministic_crc_bytes(hash, (const uint8_t *)&gpGlobals->Exp, sizeof(gpGlobals->Exp));
   hash = pal_deterministic_crc_bytes(hash, (const uint8_t *)gpGlobals->rgPoisonStatus, sizeof(gpGlobals->rgPoisonStatus));
   hash = pal_deterministic_crc_bytes(hash, (const uint8_t *)gpGlobals->rgInventory, sizeof(gpGlobals->rgInventory));
   hash = pal_deterministic_crc_bytes(hash, (const uint8_t *)gpGlobals->g.rgScene, sizeof(gpGlobals->g.rgScene));
   object_count = pal_deterministic_object_count();
   hash = pal_deterministic_crc_u32(hash, object_count);
   hash = pal_deterministic_crc_bytes(hash,
      (const uint8_t *)gpGlobals->g.rgObject,
      object_count * (uint32_t)sizeof(gpGlobals->g.rgObject[0]));
   hash = pal_deterministic_crc_bytes(hash, (const uint8_t *)&gpGlobals->g.PlayerRoles, sizeof(gpGlobals->g.PlayerRoles));
   hash = pal_deterministic_crc_u32(hash, pal_deterministic_event_object_crc());
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->g.nStore);
   if (gpGlobals->g.lprgStore != NULL && gpGlobals->g.nStore > 0) {
      hash = pal_deterministic_crc_bytes(hash,
         (const uint8_t *)gpGlobals->g.lprgStore,
         (uint32_t)gpGlobals->g.nStore * (uint32_t)sizeof(*gpGlobals->g.lprgStore));
   }
   hash = pal_deterministic_crc_u32(hash, (uint32_t)gpGlobals->g.nMagic);
   if (gpGlobals->g.lprgMagic != NULL && gpGlobals->g.nMagic > 0) {
      hash = pal_deterministic_crc_bytes(hash,
         (const uint8_t *)gpGlobals->g.lprgMagic,
         (uint32_t)gpGlobals->g.nMagic * (uint32_t)sizeof(*gpGlobals->g.lprgMagic));
   }

   return hash;
}

static uint32_t
pal_deterministic_hash_file(
   const char *path,
   uint32_t *size
)
{
   FILE *fp;
   uint8_t buffer[512];
   uint32_t hash = 2166136261u;
   uint32_t total = 0;
   size_t n;

   if (size != NULL) {
      *size = 0;
   }
   if (path == NULL) {
      return 0;
   }

   fp = fopen(path, "rb");
   if (fp == NULL) {
      return 0;
   }

   while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
      hash = pal_deterministic_crc_bytes(hash, buffer, (uint32_t)n);
      total += (uint32_t)n;
   }
   fclose(fp);
   if (size != NULL) {
      *size = total;
   }
   return hash;
}

static void
pal_deterministic_emit_event(
   const char *tag,
   const char *detail
)
{
   pal_deterministic_init();
   if (pal_deterministic_event_trace == NULL) {
      return;
   }

   fprintf(pal_deterministic_event_trace,
      "%lu %s frame=%u tick=%u scene=%u state=%08x %s\n",
      (unsigned long)pal_deterministic_event_sequence++,
      tag != NULL ? tag : "event",
      (unsigned)pal_deterministic_frame,
      (unsigned)pal_deterministic_ticks,
      (unsigned)gpGlobals->wNumScene,
      (unsigned)pal_deterministic_state_crc(),
      detail != NULL ? detail : "");
   fflush(pal_deterministic_event_trace);
}

static void
pal_deterministic_emit_save_event(
   const char *tag,
   int slot,
   WORD saved_times
)
{
   uint32_t save_size = 0;
   uint32_t save_hash;
   WORD menu_saved_times;
   WORD generic_event_id;
   EVENTOBJECT generic_event;
   int generic_state;
   const char *path;
   char detail[160];

   path = PAL_CombinePath(0, gConfig.pszSavePath, PAL_va(1, "%d.rpg", slot));
   save_hash = pal_deterministic_hash_file(path, &save_size);
   menu_saved_times = PAL_GetSavedTimes(slot);
   generic_event_id = pal_deterministic_event_id_env();
   generic_state = 0;
   if (generic_event_id != 0 &&
      PAL_EventObjectRead(generic_event_id, &generic_event)) {
      generic_state = generic_event.sState;
   }
   snprintf(detail, sizeof(detail),
      "slot=%d saved_times=%u menu_saved_times=%u "
      "generic_id=%u generic_state=%d save_size=%lu save=%08x",
      slot,
      (unsigned)saved_times,
      (unsigned)menu_saved_times,
      (unsigned)generic_event_id,
      generic_state,
      (unsigned long)save_size,
      (unsigned)save_hash);
   pal_deterministic_emit_event(tag, detail);
}

static void
pal_deterministic_emit_frame_checkpoint(
   const char *tag
)
{
   uint32_t screen_crc;
   uint32_t state_crc;
   uint32_t script_hash;
   uint32_t script_count;

   pal_deterministic_init();
   if (pal_deterministic_checkpoints == NULL) {
      pal_deterministic_frame++;
      return;
   }

   screen_crc = pal_deterministic_screen_crc();
   state_crc = pal_deterministic_state_crc();
   if (!pal_deterministic_have_last_scene ||
       pal_deterministic_last_scene != gpGlobals->wNumScene) {
      char detail[160];
      snprintf(detail, sizeof(detail),
         "from=%u to=%u viewport_x=%d viewport_y=%d party_x=%d party_y=%d event=%08x",
         pal_deterministic_have_last_scene ? (unsigned)pal_deterministic_last_scene : 0xffffu,
         (unsigned)gpGlobals->wNumScene,
         (int)PAL_X(gpGlobals->viewport),
         (int)PAL_Y(gpGlobals->viewport),
         (int)PAL_X(gpGlobals->partyoffset),
         (int)PAL_Y(gpGlobals->partyoffset),
         (unsigned)pal_deterministic_event_object_crc());
      pal_deterministic_emit_event("scene", detail);
      pal_deterministic_last_scene = gpGlobals->wNumScene;
      pal_deterministic_have_last_scene = true;
   }
   if (!pal_deterministic_have_last_battle) {
      pal_deterministic_last_battle = gpGlobals->fInBattle;
      pal_deterministic_have_last_battle = true;
   } else if (pal_deterministic_last_battle != gpGlobals->fInBattle) {
      char detail[128];
      snprintf(detail, sizeof(detail),
         "from=%u to=%u result=%d enemies=%u event=%08x",
         (unsigned)pal_deterministic_last_battle,
         (unsigned)gpGlobals->fInBattle,
         (int)g_Battle.BattleResult,
         (unsigned)g_Battle.wMaxEnemyIndex + 1u,
         (unsigned)pal_deterministic_event_object_crc());
      pal_deterministic_emit_event("battle", detail);
      pal_deterministic_last_battle = gpGlobals->fInBattle;
   }
   script_hash = pal_deterministic_script_hash;
   script_count = pal_deterministic_script_count;
   pal_deterministic_script_hash = 2166136261u;
   pal_deterministic_script_count = 0;
   fprintf(pal_deterministic_checkpoints,
           "%u %s tick=%u screen=%08x state=%08x script=%08x script_count=%lu scene=%u viewport_x=%d viewport_y=%d party_x=%d party_y=%d battle=%u battle_result=%d battle_enemies=%u cash=%lu game_frame=%lu dir=%d keys=%lx\n",
           (unsigned)pal_deterministic_frame,
           tag != NULL ? tag : "frame",
           (unsigned)pal_deterministic_ticks,
           (unsigned)screen_crc,
           (unsigned)state_crc,
           (unsigned)script_hash,
           (unsigned long)script_count,
           (unsigned)gpGlobals->wNumScene,
           (int)PAL_X(gpGlobals->viewport),
           (int)PAL_Y(gpGlobals->viewport),
           (int)PAL_X(gpGlobals->partyoffset),
           (int)PAL_Y(gpGlobals->partyoffset),
           (unsigned)gpGlobals->fInBattle,
           (int)g_Battle.BattleResult,
           (unsigned)g_Battle.wMaxEnemyIndex + 1u,
           (unsigned long)gpGlobals->dwCash,
           (unsigned long)gpGlobals->dwFrameNum,
           (int)g_InputState.dir,
           (unsigned long)g_InputState.dwKeyPress);
   fflush(pal_deterministic_checkpoints);
   pal_deterministic_frame++;
}

static bool
pal_deterministic_read_replay_state(
   void
)
{
   char line[PAL_DETERMINISTIC_LINE_BYTES];

   pal_deterministic_init();
   if (pal_deterministic_replay == NULL || pal_deterministic_replay_eof) {
      return false;
   }
   if (pal_deterministic_have_replay_state) {
      return true;
   }

   while (fgets(line, sizeof(line), pal_deterministic_replay) != NULL) {
      unsigned long tick;
      unsigned long keys;
      int dir;
      char tag[16];

      if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
         continue;
      }
      if (sscanf(line, "%lu %15s %d %lx", &tick, tag, &dir, &keys) == 4 &&
          strcmp(tag, "state") == 0) {
         pal_deterministic_replay_tick = (Uint32)tick;
         pal_deterministic_replay_dir = dir;
         pal_deterministic_replay_keys = (DWORD)keys;
         pal_deterministic_have_replay_state = true;
         return true;
      }
      if (sscanf(line, "%lu %d %lx", &tick, &dir, &keys) == 3) {
         pal_deterministic_replay_tick = (Uint32)tick;
         pal_deterministic_replay_dir = dir;
         pal_deterministic_replay_keys = (DWORD)keys;
         pal_deterministic_have_replay_state = true;
         return true;
      }
   }

   pal_deterministic_replay_eof = true;
   return false;
}

static void
pal_deterministic_record_state(
   void
)
{
   int dir;
   DWORD keys;

   pal_deterministic_init();
   if (pal_deterministic_record == NULL) {
      return;
   }

   dir = (int)g_InputState.dir;
   keys = g_InputState.dwKeyPress;
   if (pal_deterministic_ticks == pal_deterministic_last_record_tick &&
       dir == pal_deterministic_last_record_dir &&
       keys == pal_deterministic_last_record_keys) {
      return;
   }

   fprintf(pal_deterministic_record, "%u state %d %lx\n",
           (unsigned)pal_deterministic_ticks,
           dir,
           (unsigned long)keys);
   fflush(pal_deterministic_record);
   pal_deterministic_last_record_tick = pal_deterministic_ticks;
   pal_deterministic_last_record_dir = dir;
   pal_deterministic_last_record_keys = keys;
}

static void
pal_deterministic_init_input(
   void
)
{
   pal_deterministic_init();
}

static int
pal_deterministic_filter_input_event(
   const SDL_Event *event,
   volatile PALINPUTSTATE *state
)
{
   if (event == NULL || state == NULL ||
       event->type != SDL_USEREVENT ||
       event->user.code != PAL_DETERMINISTIC_EVENT_CODE) {
      return 0;
   }

   state->prevdir = state->dir;
   state->dir = (PALDIRECTION)(uintptr_t)event->user.data1;
   state->dwKeyPress |= (DWORD)(uintptr_t)event->user.data2;
   return 1;
}

__attribute__((constructor))
static void
pal_deterministic_register_input(
   void
)
{
   PAL_RegisterInputFilter(
      pal_deterministic_init_input,
      pal_deterministic_filter_input_event,
      pal_deterministic_record_state);
}

Uint32
PAL_DeterministicGetTicks(
   void
)
{
   pal_deterministic_init();
   if (pal_deterministic_realtime) {
      pal_deterministic_ticks = SDL_GetTicks();
   }
   return pal_deterministic_ticks;
}

Uint64
PAL_DeterministicGetPerformanceCounter(
   void
)
{
   return PAL_DeterministicGetTicks();
}

Uint64
PAL_DeterministicGetPerformanceFrequency(
   void
)
{
   return 1000;
}

void
PAL_DeterministicDelay(
   Uint32 ms
)
{
   pal_deterministic_init();
   pal_deterministic_record_state();
   if (pal_deterministic_realtime) {
      SDL_Delay(ms);
      pal_deterministic_ticks = SDL_GetTicks();
      pal_deterministic_record_state();
      return;
   }
   if (ms == 0) {
      return;
   }
   pal_deterministic_ticks += ms;
   pal_deterministic_record_state();
}

time_t
PAL_DeterministicTime(
   time_t *timer
)
{
   pal_deterministic_init();
   if (timer != NULL) {
      *timer = pal_deterministic_epoch;
   }
   return pal_deterministic_epoch;
}

int
PAL_DeterministicPollEvent(
   SDL_Event *event
)
{
   pal_deterministic_init();

   if (!pal_deterministic_quit_sent &&
       pal_deterministic_max_presents != 0 &&
       pal_deterministic_frame >= pal_deterministic_max_presents) {
      if (event != NULL) {
         memset(event, 0, sizeof(*event));
         event->type = SDL_QUIT;
      }
      pal_deterministic_quit_sent = true;
      return 1;
   }

   if (pal_deterministic_read_replay_state() &&
       SDL_TICKS_PASSED(pal_deterministic_ticks, pal_deterministic_replay_tick)) {
      if (event != NULL) {
         memset(event, 0, sizeof(*event));
         event->type = SDL_USEREVENT;
         event->user.code = PAL_DETERMINISTIC_EVENT_CODE;
         event->user.data1 = (void *)(uintptr_t)pal_deterministic_replay_dir;
         event->user.data2 = (void *)(uintptr_t)pal_deterministic_replay_keys;
      }
      pal_deterministic_have_replay_state = false;
      return 1;
   }

   if (pal_deterministic_replay != NULL) {
      return 0;
   }
   return SDL_PollEvent(event);
}

void
__wrap_SDL_RenderPresent(
   SDL_Renderer *renderer
)
{
   BOOL old_auto_battle;
   WORD old_scene;
   WORD chapter_result;
   char chapter_detail[96];

   pal_deterministic_maybe_write_screenshot();
   pal_deterministic_maybe_save_game();
   pal_deterministic_maybe_reload_game();
   pal_deterministic_emit_frame_checkpoint("present");
   if (pal_deterministic_force_chapter_enabled &&
       !pal_deterministic_force_chapter_done &&
       !pal_deterministic_force_chapter_active &&
       pal_deterministic_frame > pal_deterministic_force_chapter_frame &&
       gpGlobals->wNumScene != 0 &&
       !gpGlobals->fInBattle) {
      /*
       * Host-only chapter-transition probe.  The scene value is restored
       * immediately; the real script must request scene 21 so the complete
       * host pack and the Cardputer chapter cache can continue normally.
       */
      pal_deterministic_force_chapter_active = true;
      pal_deterministic_force_chapter_done = true;
      old_scene = gpGlobals->wNumScene;
      gpGlobals->wNumScene = 22;
      chapter_result = PAL_RunTriggerScript(10600, 411);
      snprintf(chapter_detail, sizeof(chapter_detail),
         "from=22 to=%u entry=10600 result=%u",
         (unsigned)gpGlobals->wNumScene, (unsigned)chapter_result);
      pal_deterministic_emit_event("chapter", chapter_detail);
      if (gpGlobals->wNumScene != 21) {
         TerminateOnError(
            "Cardputer chapter transition stopped at scene %u",
            gpGlobals->wNumScene);
      }
      gpGlobals->wNumScene = old_scene;
      pal_deterministic_force_chapter_active = false;
   }
   if (pal_deterministic_force_battle_enabled &&
       !pal_deterministic_force_battle_done &&
       !pal_deterministic_force_battle_active &&
       pal_deterministic_frame > pal_deterministic_force_battle_frame &&
       gpGlobals->wNumScene != 0 &&
       !gpGlobals->fInBattle) {
      /*
       * Host-only reachability probe: enter the unmodified battle loop after
       * normal game initialization.  The active/done guards are set before
       * PAL_StartBattle() because that loop presents recursively.
       */
      pal_deterministic_force_battle_active = true;
      pal_deterministic_force_battle_done = true;
      old_auto_battle = gpGlobals->fAutoBattle;
      gpGlobals->fAutoBattle = pal_deterministic_force_battle_auto;
      (void)PAL_StartBattle(pal_deterministic_force_battle_team, FALSE);
      gpGlobals->fAutoBattle = old_auto_battle;
      pal_deterministic_force_battle_active = false;
   }
   __real_SDL_RenderPresent(renderer);
}

WORD
__wrap_PAL_RunTriggerScript(
   WORD wScriptEntry,
   WORD wEventObjectID
)
{
   WORD result;
   pal_deterministic_push_script_event(wEventObjectID);
   result = __real_PAL_RunTriggerScript(wScriptEntry, wEventObjectID);
   pal_deterministic_pop_script_event();
   pal_deterministic_record_script_entry(1u, wScriptEntry, wEventObjectID, result);
   return result;
}

WORD
__wrap_PAL_RunAutoScript(
   WORD wScriptEntry,
   WORD wEventObjectID
)
{
   WORD result;
   pal_deterministic_push_script_event(wEventObjectID);
   result = __real_PAL_RunAutoScript(wScriptEntry, wEventObjectID);
   pal_deterministic_pop_script_event();
   pal_deterministic_record_script_entry(2u, wScriptEntry, wEventObjectID, result);
   return result;
}

BOOL
__wrap_PAL_SaveGame(
   int iSaveSlot,
   WORD wSavedTimes
)
{
   BOOL result = __real_PAL_SaveGame(iSaveSlot, wSavedTimes);
   pal_deterministic_emit_save_event("save", iSaveSlot, wSavedTimes);
   return result;
}

void
__wrap_PAL_InitGameData(
   INT iSaveSlot
)
{
   __real_PAL_InitGameData(iSaveSlot);
   pal_deterministic_emit_save_event("init", iSaveSlot, 0);
}

void
__wrap_PAL_ReloadInNextTick(
   INT iSaveSlot
)
{
   __real_PAL_ReloadInNextTick(iSaveSlot);
   pal_deterministic_emit_save_event("reload", iSaveSlot, 0);
}
