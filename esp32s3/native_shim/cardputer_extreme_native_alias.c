#include "../main/cardputer_extreme_board.h"
#include "../main/cardputer_extreme_native_view.h"
#include "../main/cores3se_board.h"
#if defined(PAL_TARGET_XIAOMIAO)
#include "../main/xiaomiao_board.h"
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint8_t pal_native_cardputer_argb[
   CARDPUTER_EXTREME_LCD_WIDTH * CARDPUTER_EXTREME_LCD_HEIGHT * 4u];
static uint8_t pal_native_cardputer_current_key;
static uint8_t pal_native_cardputer_pending_key;

/*
 * Host-only board adapter for running the exact Cardputer extreme engine
 * profile through the deterministic harness.  Storage/time/FatFS emulation
 * remains in cores3se_native_shim.c; this file only supplies the different
 * board API. The shared scene-mapping math is linked separately so host
 * screenshots exercise the same coordinates as the ESP-IDF presenter.
 */
bool
CardputerExtreme_Begin(
   void
)
{
   return CoreS3Se_Begin();
}

bool
CardputerExtreme_MountTf(
   void
)
{
   return CoreS3Se_MountTf();
}

void
CardputerExtreme_PrepareTfAccess(
   void
)
{
   CoreS3Se_PrepareTfAccess();
}

bool
CardputerExtreme_PollKey(
   uint8_t *ascii,
   bool *pressed
)
{
   uint16_t x = 0;
   uint16_t y = 0;
   uint8_t desired = 0;

   if (ascii == NULL || pressed == NULL)
   {
      return false;
   }
   if (pal_native_cardputer_pending_key != 0)
   {
      pal_native_cardputer_current_key = pal_native_cardputer_pending_key;
      pal_native_cardputer_pending_key = 0;
      *ascii = pal_native_cardputer_current_key;
      *pressed = true;
      return true;
   }
   if (CoreS3Se_TouchPoint(&x, &y))
   {
      if (y < CORES3SE_PAL_Y_OFFSET)
      {
         desired = '`';
      }
      else if (y >= CORES3SE_PAL_Y_OFFSET + 200u)
      {
         desired = '\r';
      }
      else if (y < CORES3SE_PAL_Y_OFFSET + 200u / 3u)
      {
         desired = ';';
      }
      else if (y >= CORES3SE_PAL_Y_OFFSET + 2u * (200u / 3u))
      {
         desired = '.';
      }
      else if (x < CORES3SE_LCD_WIDTH / 3u)
      {
         desired = ',';
      }
      else if (x >= 2u * (CORES3SE_LCD_WIDTH / 3u))
      {
         desired = '/';
      }
      else
      {
         desired = '\r';
      }
   }
   if (desired == pal_native_cardputer_current_key)
   {
      return false;
   }
   if (pal_native_cardputer_current_key != 0)
   {
      *ascii = pal_native_cardputer_current_key;
      *pressed = false;
      pal_native_cardputer_current_key = 0;
      pal_native_cardputer_pending_key = desired;
      return true;
   }
   if (desired != 0)
   {
      pal_native_cardputer_current_key = desired;
      *ascii = desired;
      *pressed = true;
      return true;
   }
   return false;
}

bool
CardputerExtreme_FlushIndexedFramebuffer(
   const uint8_t *pixels,
   uint16_t pitch,
   const uint8_t *palette_rgba
)
{
   uint16_t destination_y;

   if (pixels == NULL || palette_rgba == NULL ||
      pitch < PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH)
   {
      return false;
   }
   for (destination_y = 0;
      destination_y < CARDPUTER_EXTREME_LCD_HEIGHT;
      destination_y++)
   {
      uint16_t destination_x;
      for (destination_x = 0;
         destination_x < CARDPUTER_EXTREME_LCD_WIDTH;
         destination_x++)
      {
         const uint8_t *color;
         uint8_t *destination;
         color = palette_rgba +
            (size_t)pixels[(size_t)destination_y * pitch + destination_x] * 4u;
         destination = pal_native_cardputer_argb +
            ((size_t)destination_y * CARDPUTER_EXTREME_LCD_WIDTH +
             destination_x) * 4u;
         destination[0] = color[2];
         destination[1] = color[1];
         destination[2] = color[0];
         destination[3] = 0xffu;
      }
   }
   return CoreS3Se_FlushArgb8888Texture(
      pal_native_cardputer_argb,
      CARDPUTER_EXTREME_LCD_WIDTH,
      CARDPUTER_EXTREME_LCD_HEIGHT,
      CARDPUTER_EXTREME_LCD_WIDTH * 4u);
}

void
CardputerExtreme_ShowError(
   const char *line1,
   const char *line2
)
{
   fprintf(stderr, "Cardputer extreme error: %s / %s\n",
      line1 != NULL ? line1 : "",
      line2 != NULL ? line2 : "");
}

#if defined(PAL_TARGET_XIAOMIAO)
/*
 * The Linux harness exercises Xiaomiao's exact 160x128 engine/storage path;
 * only physical GPIO/SPI operations remain represented by the existing SDL
 * board adapter.  The ESP-IDF firmware never compiles this translation unit.
 */
bool
Xiaomiao_Begin(
   void
)
{
   return CardputerExtreme_Begin();
}

bool
Xiaomiao_MountTf(
   void
)
{
   return CardputerExtreme_MountTf();
}

bool
Xiaomiao_TfMounted(
   void
)
{
   return true;
}

void
Xiaomiao_PrepareTfAccess(
   void
)
{
   CardputerExtreme_PrepareTfAccess();
}

bool
Xiaomiao_PollKey(
   uint8_t *ascii,
   bool    *pressed
)
{
   return CardputerExtreme_PollKey(ascii, pressed);
}

bool
Xiaomiao_FlushIndexedFramebuffer(
   const uint8_t *pixels,
   uint16_t       pitch,
   const uint8_t *palette_rgba
)
{
   return CardputerExtreme_FlushIndexedFramebuffer(
      pixels, pitch, palette_rgba);
}

void
Xiaomiao_ShowError(
   const char *line1,
   const char *line2
)
{
   CardputerExtreme_ShowError(line1, line2);
}
#endif

/*
 * The target build gets these operations from FatFS.  The deterministic
 * Cardputer host maps the same short "0:/" save names into its isolated save
 * directory, so the production temporary-file/rename transaction is tested
 * without changing the shared CoreS3 SE native shim.
 */
static const char *
CardputerExtreme_MapSavePath(
   const char *path,
   char       *mapped,
   size_t      mapped_bytes
)
{
   const char *save_dir;
   int result;

   if (path == NULL || mapped == NULL || mapped_bytes == 0)
   {
      return NULL;
   }
   if (strncmp(path, "0:/", 3) != 0)
   {
      return path;
   }

   save_dir = getenv("PAL_CORES3SE_NATIVE_SAVE_DIR");
   if (save_dir == NULL || save_dir[0] == '\0')
   {
      save_dir = "/mnt/hgfs/deb13/PALSteam/PAL_DOS";
   }
   result = snprintf(mapped, mapped_bytes, "%s/%s", save_dir, path + 3);
   return result >= 0 && (size_t)result < mapped_bytes ? mapped : NULL;
}

static bool
CardputerExtreme_SaveFailureRequested(
   const char *operation
)
{
   const char *requested = getenv("PAL_CARDPUTER_NATIVE_SAVE_FAIL");

   return requested != NULL && requested[0] != '\0' &&
      operation != NULL && strcmp(requested, operation) == 0;
}

bool
CardputerExtreme_SaveUnlink(
   const char *path,
   bool        missing_ok
)
{
   char mapped[512];
   const char *native_path = CardputerExtreme_MapSavePath(
      path, mapped, sizeof(mapped));
   const char *extension = path != NULL ? strrchr(path, '.') : NULL;

   if (native_path == NULL)
   {
      return false;
   }
   if (extension != NULL && strcmp(extension, ".bak") == 0 &&
      CardputerExtreme_SaveFailureRequested("unlink_backup"))
   {
      errno = EIO;
      return false;
   }
   if (remove(native_path) == 0)
   {
      return true;
   }
   return missing_ok && errno == ENOENT;
}

bool
CardputerExtreme_SaveRename(
   const char *old_path,
   const char *new_path
)
{
   char mapped_old[512];
   char mapped_new[512];
   struct stat destination_stat;
   const char *native_old = CardputerExtreme_MapSavePath(
      old_path, mapped_old, sizeof(mapped_old));
   const char *native_new = CardputerExtreme_MapSavePath(
      new_path, mapped_new, sizeof(mapped_new));
   const char *old_extension = old_path != NULL ? strrchr(old_path, '.') : NULL;
   const char *new_extension = new_path != NULL ? strrchr(new_path, '.') : NULL;

   if (native_old == NULL || native_new == NULL)
   {
      return false;
   }
   if (old_extension != NULL && new_extension != NULL &&
      ((strcmp(old_extension, ".rpg") == 0 &&
         strcmp(new_extension, ".bak") == 0 &&
         CardputerExtreme_SaveFailureRequested("rename_final_backup")) ||
       (strcmp(old_extension, ".tmp") == 0 &&
         strcmp(new_extension, ".rpg") == 0 &&
         CardputerExtreme_SaveFailureRequested("rename_temp_final"))))
   {
      errno = EIO;
      return false;
   }
   /*
    * FatFS f_rename() does not replace an existing destination.  Preserve
    * that behavior on POSIX even though rename(2) normally would replace it.
    */
   if (stat(native_new, &destination_stat) == 0 || errno != ENOENT)
   {
      return false;
   }
   if (rename(native_old, native_new) == 0)
   {
      return true;
   }
   return false;
}

#if defined(PAL_TARGET_XIAOMIAO)
bool
Xiaomiao_SaveUnlink(
   const char *path,
   bool        missing_ok
)
{
   return CardputerExtreme_SaveUnlink(path, missing_ok);
}

bool
Xiaomiao_SaveRename(
   const char *old_path,
   const char *new_path
)
{
   return CardputerExtreme_SaveRename(old_path, new_path);
}
#endif
