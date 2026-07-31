#include "../main/cardputer_extreme_board.h"
#include "../main/cores3se_board.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * Host-only board adapter for running the exact Cardputer extreme engine
 * profile through the deterministic harness.  Storage/time/FatFS emulation
 * remains in cores3se_native_shim.c; this file only supplies the different
 * board API.  The real indexed 1:1 viewport/DMA implementation is compiled
 * and linked by the ESP-IDF target build.
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
   (void)ascii;
   (void)pressed;
   return false;
}

bool
CardputerExtreme_FlushIndexedFramebuffer(
   const uint8_t *pixels,
   uint16_t pitch,
   const uint8_t *palette_rgba
)
{
   /*
    * unix/deterministic.c hashes and screenshots gpScreen directly.  Avoid a
    * host-only 32-bit presentation buffer here so the harness still exposes
    * the same two logical-screen storage shape as the target.
    */
   return pixels != NULL && palette_rgba != NULL && pitch >= 320u;
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
      save_dir = "/mnt/hgfs/deb13/PAL";
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
