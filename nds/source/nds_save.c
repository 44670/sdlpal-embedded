#include "pal_target_save.h"

#include "pal_target_board.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum {
   PAL_NDS_SAVE_SLOT_COUNT = 5u,
   PAL_NDS_SAVE_SLOT_BYTES = 0x30000u,
   PAL_NDS_SAVE_FOOTER_BYTES = 64u,
   PAL_NDS_SAVE_FOOTER_OFFSET =
      PAL_NDS_SAVE_SLOT_BYTES - PAL_NDS_SAVE_FOOTER_BYTES,
   PAL_NDS_SAVE_PAYLOAD_CAPACITY = PAL_NDS_SAVE_FOOTER_OFFSET,
   PAL_NDS_SAVE_IO_BYTES = 4096u,
   PAL_NDS_SAVE_VERSION = 1u,
   PAL_NDS_SAVE_COMMITTED = 0x53415645u,
};

/*
 * Saves are ordinary files on the launch FAT volume (the internal SD card in
 * the accepted TWiLight Menu TWL path).  This is the writable storage a homebrew
 * launched from SD can rely on: the Slot-1 backup chip that retail games use
 * does not exist behind a software loader, and nds-bootstrap only patches
 * cardEeprom* for retail ROMs, never for homebrew.  Each slot is one file
 * whose byte layout matches the retired Slot-1 image (payload from offset 0,
 * commit footer at the fixed tail offset).  Writes go to a temporary file,
 * are read back and verified, and are renamed over the live slot last, so a
 * power loss mid-write keeps the previous committed save.
 */

typedef struct PalNdsSaveFooter {
   uint8_t magic[8];
   uint32_t version;
   uint32_t slot;
   uint32_t generation;
   uint32_t payload_size;
   uint32_t payload_crc32;
   uint32_t header_crc32;
   uint32_t committed;
   uint8_t reserved[28];
} PalNdsSaveFooter;

static const uint8_t pal_nds_save_magic[8] = {
   'P', 'A', 'L', 'N', 'D', 'S', 'V', '1'
};
static uint8_t pal_nds_save_verify[PAL_NDS_SAVE_IO_BYTES]
   __attribute__((aligned(4), section(".bss.pal_nds_save")));
static bool pal_nds_save_available;
static const char *pal_nds_save_dir;

void
NdsTargetSave_SetMountedVolume(
   const char *volume_name)
{
   pal_nds_save_available = false;
   if (volume_name != NULL && strcmp(volume_name, "fat") == 0)
   {
      pal_nds_save_dir = "fat:/sdlpal";
   }
   else if (volume_name != NULL && strcmp(volume_name, "sd") == 0)
   {
      pal_nds_save_dir = "sd:/sdlpal";
   }
   else
   {
      pal_nds_save_dir = NULL;
   }
}

static void
pal_nds_save_log(
   const char *fmt,
   ...)
{
   char line[96];
   va_list args;

   va_start(args, fmt);
   vsnprintf(line, sizeof(line), fmt, args);
   va_end(args);
   NdsTarget_BootLog(line);
}

_Static_assert(
   sizeof(PalNdsSaveFooter) == PAL_NDS_SAVE_FOOTER_BYTES,
   "save footer ABI changed");
_Static_assert(
   PAL_NDS_SAVE_IO_BYTES >= PAL_NDS_SAVE_FOOTER_BYTES,
   "io chunk must hold a whole footer");

static uint32_t
pal_nds_crc32_update(
   uint32_t crc,
   const uint8_t *data,
   size_t size)
{
   while (size-- != 0u)
   {
      unsigned bit;

      crc ^= *data++;
      for (bit = 0u; bit < 8u; bit++)
      {
         crc = (crc >> 1) ^ (0xedb88320u &
            (uint32_t)-(int32_t)(crc & 1u));
      }
   }
   return crc;
}

static uint32_t
pal_nds_crc32(
   const void *data,
   size_t size)
{
   return pal_nds_crc32_update(
      0xffffffffu, (const uint8_t *)data, size) ^ 0xffffffffu;
}

static bool
pal_nds_save_slot_path(
   int slot,
   char *path,
   size_t capacity,
   bool temporary)
{
   int length;

   if (!pal_nds_save_available || path == NULL ||
      slot < 1 || slot > (int)PAL_NDS_SAVE_SLOT_COUNT)
   {
      return false;
   }
   length = snprintf(path, capacity, "%s/%d.%s",
      pal_nds_save_dir, slot, temporary ? "tmp" : "sav");
   return length > 0 && (size_t)length < capacity;
}

static bool
pal_nds_save_read_footer(
   int slot,
   PalNdsSaveFooter *footer)
{
   char path[64];
   FILE *file;
   uint32_t expected_crc;
   bool ok = false;

   if (footer == NULL ||
      !pal_nds_save_slot_path(slot, path, sizeof(path), false))
   {
      return false;
   }
   file = fopen(path, "rb");
   if (file == NULL)
   {
      return false;
   }
   if (fseek(file, (long)PAL_NDS_SAVE_FOOTER_OFFSET, SEEK_SET) == 0 &&
      fread(footer, 1u, sizeof(*footer), file) == sizeof(*footer))
   {
      expected_crc = pal_nds_crc32(
         footer, offsetof(PalNdsSaveFooter, header_crc32));
      ok = memcmp(footer->magic, pal_nds_save_magic, 8u) == 0 &&
         footer->version == PAL_NDS_SAVE_VERSION &&
         footer->slot == (uint32_t)slot &&
         footer->payload_size >= 2u &&
         footer->payload_size <= PAL_NDS_SAVE_PAYLOAD_CAPACITY &&
         footer->header_crc32 == expected_crc &&
         footer->committed == PAL_NDS_SAVE_COMMITTED;
   }
   fclose(file);
   return ok;
}

bool
PalTargetSave_Init(
   void)
{
   if (pal_nds_save_dir != NULL)
   {
      if (mkdir(pal_nds_save_dir, 0777) != 0)
      {
         /* EEXIST is fine; any other error surfaces on the first slot I/O. */
      }
      pal_nds_save_available = true;
      pal_nds_save_log("save: launch fat ok");
   }
   else
   {
      pal_nds_save_available = false;
      pal_nds_save_log("save: no writable launch fat");
   }
   return pal_nds_save_available;
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
   /* Backend 0 is the launch FAT file store; the retired Slot-1 types 1-3 no
      longer exist on this target. */
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
   PalNdsSaveFooter footer;
   char path[64];
   FILE *file;
   uint8_t count[2];
   bool ok = false;

   if (saved_times == NULL ||
      !pal_nds_save_read_footer(slot, &footer) ||
      !pal_nds_save_slot_path(slot, path, sizeof(path), false))
   {
      return false;
   }
   file = fopen(path, "rb");
   if (file != NULL)
   {
      ok = fread(count, 1u, sizeof(count), file) == sizeof(count);
      fclose(file);
   }
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
   PalNdsSaveFooter footer;
   char path[64];
   FILE *file;
   uint32_t offset = 0u;
   uint32_t crc = 0xffffffffu;
   uint8_t *output = (uint8_t *)destination;
   bool ok = false;

   if (out_size != NULL)
   {
      *out_size = 0u;
   }
   if (destination == NULL || out_size == NULL ||
      !pal_nds_save_read_footer(slot, &footer) ||
      footer.payload_size > capacity ||
      !pal_nds_save_slot_path(slot, path, sizeof(path), false))
   {
      return false;
   }
   file = fopen(path, "rb");
   if (file == NULL)
   {
      return false;
   }
   ok = true;
   while (ok && offset < footer.payload_size)
   {
      uint32_t amount = footer.payload_size - offset;

      if (amount > PAL_NDS_SAVE_IO_BYTES)
      {
         amount = PAL_NDS_SAVE_IO_BYTES;
      }
      ok = fread(output + offset, 1u, amount, file) == amount;
      if (ok)
      {
         crc = pal_nds_crc32_update(crc, output + offset, amount);
         offset += amount;
         NdsTarget_AudioPump();
      }
   }
   fclose(file);
   if (!ok || (crc ^ 0xffffffffu) != footer.payload_crc32)
   {
      memset(destination, 0, footer.payload_size);
      return false;
   }
   *out_size = footer.payload_size;
   return true;
}

static bool
pal_nds_save_write_image(
   const char *path,
   int slot,
   const uint8_t *input,
   size_t size,
   const PalNdsSaveFooter *footer)
{
   FILE *file;
   uint32_t offset;
   bool ok;

   file = fopen(path, "wb");
   if (file == NULL)
   {
      return false;
   }
   ok = true;
   for (offset = 0u; ok && offset < size; offset += PAL_NDS_SAVE_IO_BYTES)
   {
      uint32_t amount = (uint32_t)size - offset;

      if (amount > PAL_NDS_SAVE_IO_BYTES)
      {
         amount = PAL_NDS_SAVE_IO_BYTES;
      }
      ok = fwrite(input + offset, 1u, amount, file) == amount;
      NdsTarget_AudioPump();
   }
   /* The commit footer is written last inside the temporary image; the
      rename below is the final commit step. */
   if (ok && fseek(file, (long)PAL_NDS_SAVE_FOOTER_OFFSET, SEEK_SET) == 0)
   {
      ok = fwrite(footer, 1u, sizeof(*footer), file) == sizeof(*footer);
   }
   else
   {
      ok = false;
   }
   if (fclose(file) != 0)
   {
      ok = false;
   }
   if (!ok)
   {
      pal_nds_save_log("save w%d: write fail", slot);
      (void)remove(path);
   }
   return ok;
}

static bool
pal_nds_save_verify_image(
   const char *path,
   int slot,
   const uint8_t *input,
   size_t size,
   const PalNdsSaveFooter *footer)
{
   FILE *file;
   uint32_t offset;
   PalNdsSaveFooter verify_footer;
   bool ok;

   file = fopen(path, "rb");
   if (file == NULL)
   {
      return false;
   }
   ok = true;
   for (offset = 0u; ok && offset < size; offset += PAL_NDS_SAVE_IO_BYTES)
   {
      uint32_t amount = (uint32_t)size - offset;

      if (amount > PAL_NDS_SAVE_IO_BYTES)
      {
         amount = PAL_NDS_SAVE_IO_BYTES;
      }
      ok = fread(pal_nds_save_verify, 1u, amount, file) == amount;
      if (ok && memcmp(pal_nds_save_verify, input + offset, amount) != 0)
      {
         pal_nds_save_log(
            "save w%d: data verify fail @%lx got %02x want %02x",
            slot, (unsigned long)offset,
            pal_nds_save_verify[0], input[offset]);
         ok = false;
      }
      NdsTarget_AudioPump();
   }
   if (ok && fseek(file, (long)PAL_NDS_SAVE_FOOTER_OFFSET, SEEK_SET) == 0 &&
      fread(&verify_footer, 1u, sizeof(verify_footer), file) ==
         sizeof(verify_footer))
   {
      if (memcmp(footer, &verify_footer, sizeof(*footer)) != 0)
      {
         pal_nds_save_log("save w%d: footer verify fail", slot);
         ok = false;
      }
   }
   else
   {
      ok = false;
   }
   fclose(file);
   return ok;
}

bool
PalTargetSave_WriteSlot(
   int slot,
   const void *source,
   size_t size)
{
   PalNdsSaveFooter old_footer;
   PalNdsSaveFooter footer;
   const uint8_t *input = (const uint8_t *)source;
   char path[64];
   char temp_path[64];
   uint32_t generation = 1u;

   if (source == NULL || size < 2u ||
      size > PAL_NDS_SAVE_PAYLOAD_CAPACITY ||
      !pal_nds_save_slot_path(slot, path, sizeof(path), false) ||
      !pal_nds_save_slot_path(slot, temp_path, sizeof(temp_path), true))
   {
      return false;
   }
   if (pal_nds_save_read_footer(slot, &old_footer))
   {
      generation = old_footer.generation + 1u;
      if (generation == 0u)
      {
         generation = 1u;
      }
   }

   memset(&footer, 0, sizeof(footer));
   memcpy(footer.magic, pal_nds_save_magic, sizeof(footer.magic));
   footer.version = PAL_NDS_SAVE_VERSION;
   footer.slot = (uint32_t)slot;
   footer.generation = generation;
   footer.payload_size = (uint32_t)size;
   footer.payload_crc32 = pal_nds_crc32(source, size);
   footer.header_crc32 = pal_nds_crc32(
      &footer, offsetof(PalNdsSaveFooter, header_crc32));
   footer.committed = PAL_NDS_SAVE_COMMITTED;

   if (!pal_nds_save_write_image(temp_path, slot, input, size, &footer) ||
      !pal_nds_save_verify_image(temp_path, slot, input, size, &footer))
   {
      (void)remove(temp_path);
      return false;
   }
   if (rename(temp_path, path) != 0)
   {
      pal_nds_save_log("save w%d: commit rename fail", slot);
      (void)remove(temp_path);
      return false;
   }
   pal_nds_save_log("save w%d: ok %lu bytes", slot, (unsigned long)size);
   return true;
}
