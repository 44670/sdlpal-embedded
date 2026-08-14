#include "pal_target_save.h"

#include "pal_target_board.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum {
   PAL_NDS_SAVE_SLOT_COUNT = 5u,
   PAL_NDS_SAVE_SLOT_BYTES = 0x30000u,
   PAL_NDS_SAVE_IO_BYTES = 4096u,
};

#define PAL_NDS_SAVE_DIR "fat:/sdlpal"

static bool pal_nds_dldi_ready;
static bool pal_nds_save_available;

void
NdsTargetSave_SetDldiReady(
   bool ready)
{
   pal_nds_dldi_ready = ready;
   pal_nds_save_available = false;
}

static bool
pal_nds_save_slot_path(
   int slot,
   char *path,
   size_t capacity)
{
   int length;

   if (!pal_nds_save_available || path == NULL ||
      slot < 1 || slot > (int)PAL_NDS_SAVE_SLOT_COUNT)
   {
      return false;
   }
   length = snprintf(path, capacity, PAL_NDS_SAVE_DIR "/%d.rpg", slot);
   return length > 0 && (size_t)length < capacity;
}

bool
PalTargetSave_Init(
   void)
{
   if (!pal_nds_dldi_ready)
   {
      NdsTarget_BootLog("save: DLDI FAT unavailable");
      return false;
   }
   if (mkdir(PAL_NDS_SAVE_DIR, 0777) != 0 && errno != EEXIST)
   {
      NdsTarget_BootLog("save: create fat:/sdlpal failed");
      return false;
   }
   pal_nds_save_available = true;
   NdsTarget_BootLog("save: DLDI FAT files ok");
   return true;
}

bool
PalTargetSave_Available(
   void)
{
   return pal_nds_save_available;
}

int
PalTargetSave_Type(
   void)
{
   return 0;
}

uint32_t
PalTargetSave_Capacity(
   void)
{
   return PAL_NDS_SAVE_SLOT_COUNT * PAL_NDS_SAVE_SLOT_BYTES;
}

bool
PalTargetSave_ProbeSlot(
   int slot,
   uint16_t *saved_times)
{
   char path[64];
   FILE *file;
   uint8_t count[2];
   bool ok;

   if (saved_times == NULL ||
      !pal_nds_save_slot_path(slot, path, sizeof(path)))
   {
      return false;
   }
   file = fopen(path, "rb");
   if (file == NULL)
   {
      return false;
   }
   ok = fread(count, 1u, sizeof(count), file) == sizeof(count);
   fclose(file);
   if (!ok)
   {
      return false;
   }
   *saved_times = (uint16_t)(count[0] | ((uint16_t)count[1] << 8));
   return true;
}

bool
PalTargetSave_ReadSlot(
   int slot,
   void *destination,
   size_t capacity,
   size_t *out_size)
{
   char path[64];
   struct stat st;
   FILE *file;
   uint8_t *output = (uint8_t *)destination;
   size_t offset = 0u;
   bool ok = true;

   if (out_size != NULL)
   {
      *out_size = 0u;
   }
   if (destination == NULL || out_size == NULL ||
      !pal_nds_save_slot_path(slot, path, sizeof(path)) ||
      stat(path, &st) != 0 || st.st_size < 2 ||
      (uint64_t)st.st_size > capacity ||
      st.st_size > PAL_NDS_SAVE_SLOT_BYTES)
   {
      return false;
   }
   file = fopen(path, "rb");
   if (file == NULL)
   {
      return false;
   }
   while (ok && offset < (size_t)st.st_size)
   {
      size_t amount = (size_t)st.st_size - offset;

      if (amount > PAL_NDS_SAVE_IO_BYTES)
      {
         amount = PAL_NDS_SAVE_IO_BYTES;
      }
      ok = fread(output + offset, 1u, amount, file) == amount;
      offset += ok ? amount : 0u;
      NdsTarget_AudioPump();
   }
   if (fclose(file) != 0)
   {
      ok = false;
   }
   if (!ok)
   {
      memset(destination, 0, offset);
      return false;
   }
   *out_size = offset;
   return true;
}

bool
PalTargetSave_WriteSlot(
   int slot,
   const void *source,
   size_t size)
{
   char path[64];
   FILE *file;
   const uint8_t *input = (const uint8_t *)source;
   size_t offset = 0u;
   bool ok = true;

   if (source == NULL || size < 2u || size > PAL_NDS_SAVE_SLOT_BYTES ||
      !pal_nds_save_slot_path(slot, path, sizeof(path)))
   {
      return false;
   }
   file = fopen(path, "wb");
   if (file == NULL)
   {
      return false;
   }
   while (ok && offset < size)
   {
      size_t amount = size - offset;

      if (amount > PAL_NDS_SAVE_IO_BYTES)
      {
         amount = PAL_NDS_SAVE_IO_BYTES;
      }
      ok = fwrite(input + offset, 1u, amount, file) == amount;
      offset += ok ? amount : 0u;
      NdsTarget_AudioPump();
   }
   if (fclose(file) != 0)
   {
      ok = false;
   }
   return ok;
}
