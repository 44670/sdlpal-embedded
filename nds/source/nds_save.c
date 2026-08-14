#include "pal_target_save.h"

#include "pal_target_board.h"

#include <nds.h>

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum {
   PAL_NDS_SAVE_BYTES = 1024u * 1024u,
   PAL_NDS_SAVE_SLOT_COUNT = 5u,
   PAL_NDS_SAVE_SLOT_BYTES = 0x30000u,
   PAL_NDS_SAVE_FOOTER_BYTES = 64u,
   PAL_NDS_SAVE_FOOTER_OFFSET =
      PAL_NDS_SAVE_SLOT_BYTES - PAL_NDS_SAVE_FOOTER_BYTES,
   PAL_NDS_SAVE_PAYLOAD_CAPACITY = PAL_NDS_SAVE_FOOTER_OFFSET,
   PAL_NDS_SAVE_SECTOR_BYTES = 64u * 1024u,
   PAL_NDS_SAVE_SPI_IO_BYTES = 256u,
   PAL_NDS_SAVE_FAT_IO_BYTES = 4096u,
   PAL_NDS_SAVE_TYPE_FLASH = 3u,
   PAL_NDS_SAVE_VERSION = 1u,
   PAL_NDS_SAVE_COMMITTED = 0x53415645u,
};

#define PAL_NDS_SAVE_DIR "fat:/sdlpal"

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
static bool pal_nds_dldi_ready;
static bool pal_nds_retail_ready;
static bool pal_nds_save_available;
static uint8_t pal_nds_save_verify[PAL_NDS_SAVE_SPI_IO_BYTES]
   __attribute__((aligned(4)));

_Static_assert(
   sizeof(PalNdsSaveFooter) == PAL_NDS_SAVE_FOOTER_BYTES,
   "save footer ABI changed");
_Static_assert(
   PAL_NDS_SAVE_SLOT_COUNT * PAL_NDS_SAVE_SLOT_BYTES <= PAL_NDS_SAVE_BYTES,
   "save slots exceed the Slot-1 backup image");

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

void
NdsTargetSave_SetDldiReady(
   bool ready)
{
   pal_nds_dldi_ready = ready;
   pal_nds_save_available = false;
}

void
NdsTargetSave_SetRetailReady(
   bool ready)
{
   pal_nds_retail_ready = ready;
   pal_nds_save_available = false;
}

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
   size_t capacity)
{
   int length;

   if (!pal_nds_dldi_ready || !pal_nds_save_available || path == NULL ||
      slot < 1 || slot > (int)PAL_NDS_SAVE_SLOT_COUNT)
   {
      return false;
   }
   length = snprintf(path, capacity, PAL_NDS_SAVE_DIR "/%d.rpg", slot);
   return length > 0 && (size_t)length < capacity;
}

static bool
pal_nds_save_slot_offset(
   int slot,
   uint32_t *offset)
{
   if (!pal_nds_retail_ready || !pal_nds_save_available || offset == NULL ||
      slot < 1 || slot > (int)PAL_NDS_SAVE_SLOT_COUNT)
   {
      return false;
   }
   *offset = (uint32_t)(slot - 1) * PAL_NDS_SAVE_SLOT_BYTES;
   return true;
}

static void
pal_nds_spi_prepare(
   void)
{
   /* nitroromGetSelf() owns the card for the lifetime of this Slot-1 route;
    * closing that ownership here would break later NitroFS reads. Calico's
    * card-ROM startup also leaves command bytes in AUXSPIDATA before the first
    * backup access, so explicitly deassert backup chip select before each
    * libnds operation. */
   REG_EXMEMCNT &= (uint16_t)~ARM7_OWNS_CARD;
   REG_AUXSPICNT = CARD_ENABLE | CARD_SPI_ENABLE | CARD_SPI_HOLD;
   REG_AUXSPICNT = CARD_SPI_HOLD;
}

static bool
pal_nds_spi_read(
   uint32_t offset,
   void *destination,
   uint32_t size)
{
   if (destination == NULL || size == 0u ||
      offset > PAL_NDS_SAVE_BYTES || size > PAL_NDS_SAVE_BYTES - offset)
   {
      return false;
   }
   pal_nds_spi_prepare();
   cardReadEeprom(offset, (uint8_t *)destination, size,
      PAL_NDS_SAVE_TYPE_FLASH);
   return true;
}

static bool
pal_nds_spi_write(
   uint32_t offset,
   const void *source,
   uint32_t size)
{
   if (source == NULL || size == 0u || size > PAL_NDS_SAVE_SPI_IO_BYTES ||
      offset > PAL_NDS_SAVE_BYTES || size > PAL_NDS_SAVE_BYTES - offset)
   {
      return false;
   }
   pal_nds_spi_prepare();
   cardWriteEeprom(offset, (uint8_t *)source, size,
      PAL_NDS_SAVE_TYPE_FLASH);
   cardReadEeprom(offset, pal_nds_save_verify, size,
      PAL_NDS_SAVE_TYPE_FLASH);
   return memcmp(source, pal_nds_save_verify, size) == 0;
}

static bool
pal_nds_spi_erase(
   uint32_t offset)
{
   if (offset >= PAL_NDS_SAVE_BYTES)
   {
      return false;
   }
   pal_nds_spi_prepare();
   cardEepromSectorErase(offset);
   return true;
}

static bool
pal_nds_save_read_footer(
   int slot,
   PalNdsSaveFooter *footer)
{
   uint32_t base;
   uint32_t expected_crc;

   if (footer == NULL || !pal_nds_save_slot_offset(slot, &base) ||
      !pal_nds_spi_read(base + PAL_NDS_SAVE_FOOTER_OFFSET,
         footer, sizeof(*footer)))
   {
      return false;
   }
   expected_crc = pal_nds_crc32(
      footer, offsetof(PalNdsSaveFooter, header_crc32));
   return memcmp(footer->magic, pal_nds_save_magic, 8u) == 0 &&
      footer->version == PAL_NDS_SAVE_VERSION &&
      footer->slot == (uint32_t)slot &&
      footer->payload_size >= 2u &&
      footer->payload_size <= PAL_NDS_SAVE_PAYLOAD_CAPACITY &&
      footer->header_crc32 == expected_crc &&
      footer->committed == PAL_NDS_SAVE_COMMITTED;
}

bool
PalTargetSave_Init(
   void)
{
   pal_nds_save_available = false;
   if (pal_nds_dldi_ready)
   {
      if (mkdir(PAL_NDS_SAVE_DIR, 0777) != 0 && errno != EEXIST)
      {
         NdsTarget_BootLog("save: create fat:/sdlpal failed");
         return false;
      }
      pal_nds_save_available = true;
      NdsTarget_BootLog("save: DLDI FAT files ok");
      return true;
   }
   if (pal_nds_retail_ready)
   {
      /* Retail software selects its backup protocol and capacity from its
       * own cartridge metadata. The emulator is configured with the matching
       * 1MiB FLASH device; probing it here is unreliable before the first
       * type-3 command on several emulators. Every write is read back. */
      pal_nds_save_available = true;
      NdsTarget_BootLog("save: SPI FLASH 1MiB ok");
      return true;
   }
   NdsTarget_BootLog("save: no storage backend");
   return false;
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
   return pal_nds_retail_ready ? PAL_NDS_SAVE_TYPE_FLASH : 0;
}

uint32_t
PalTargetSave_Capacity(
   void)
{
   return pal_nds_retail_ready ? PAL_NDS_SAVE_BYTES :
      PAL_NDS_SAVE_SLOT_COUNT * PAL_NDS_SAVE_SLOT_BYTES;
}

static bool
pal_nds_fat_probe_slot(
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

static bool
pal_nds_spi_probe_slot(
   int slot,
   uint16_t *saved_times)
{
   PalNdsSaveFooter footer;
   uint32_t base;
   uint8_t count[2];

   if (saved_times == NULL ||
      !pal_nds_save_read_footer(slot, &footer) ||
      !pal_nds_save_slot_offset(slot, &base) ||
      !pal_nds_spi_read(base, count, sizeof(count)))
   {
      return false;
   }
   *saved_times = (uint16_t)(count[0] | ((uint16_t)count[1] << 8));
   return true;
}

bool
PalTargetSave_ProbeSlot(
   int slot,
   uint16_t *saved_times)
{
   return pal_nds_retail_ready ?
      pal_nds_spi_probe_slot(slot, saved_times) :
      pal_nds_fat_probe_slot(slot, saved_times);
}

static bool
pal_nds_fat_read_slot(
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

      if (amount > PAL_NDS_SAVE_FAT_IO_BYTES)
      {
         amount = PAL_NDS_SAVE_FAT_IO_BYTES;
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

static bool
pal_nds_spi_read_slot(
   int slot,
   void *destination,
   size_t capacity,
   size_t *out_size)
{
   PalNdsSaveFooter footer;
   uint8_t *output = (uint8_t *)destination;
   uint32_t base;
   uint32_t offset = 0u;
   uint32_t crc = 0xffffffffu;

   if (destination == NULL || out_size == NULL ||
      !pal_nds_save_read_footer(slot, &footer) ||
      footer.payload_size > capacity ||
      !pal_nds_save_slot_offset(slot, &base))
   {
      return false;
   }
   while (offset < footer.payload_size)
   {
      uint32_t amount = footer.payload_size - offset;

      if (amount > PAL_NDS_SAVE_SPI_IO_BYTES)
      {
         amount = PAL_NDS_SAVE_SPI_IO_BYTES;
      }
      if (!pal_nds_spi_read(base + offset, output + offset, amount))
      {
         memset(destination, 0, offset);
         return false;
      }
      crc = pal_nds_crc32_update(crc, output + offset, amount);
      offset += amount;
      NdsTarget_AudioPump();
   }
   if ((crc ^ 0xffffffffu) != footer.payload_crc32)
   {
      memset(destination, 0, footer.payload_size);
      return false;
   }
   *out_size = footer.payload_size;
   return true;
}

bool
PalTargetSave_ReadSlot(
   int slot,
   void *destination,
   size_t capacity,
   size_t *out_size)
{
   if (out_size != NULL)
   {
      *out_size = 0u;
   }
   return pal_nds_retail_ready ?
      pal_nds_spi_read_slot(slot, destination, capacity, out_size) :
      pal_nds_fat_read_slot(slot, destination, capacity, out_size);
}

static bool
pal_nds_fat_write_slot(
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

      if (amount > PAL_NDS_SAVE_FAT_IO_BYTES)
      {
         amount = PAL_NDS_SAVE_FAT_IO_BYTES;
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

static bool
pal_nds_spi_write_slot(
   int slot,
   const void *source,
   size_t size)
{
   PalNdsSaveFooter old_footer;
   PalNdsSaveFooter footer;
   const uint8_t *input = (const uint8_t *)source;
   uint32_t base;
   uint32_t offset;
   uint32_t generation = 1u;

   if (source == NULL || size < 2u ||
      size > PAL_NDS_SAVE_PAYLOAD_CAPACITY ||
      !pal_nds_save_slot_offset(slot, &base))
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

   for (offset = 0u; offset < PAL_NDS_SAVE_SLOT_BYTES;
      offset += PAL_NDS_SAVE_SECTOR_BYTES)
   {
      if (!pal_nds_spi_erase(base + offset))
      {
         pal_nds_save_log("save w%d: SPI erase acquire fail @%lx",
            slot, (unsigned long)offset);
         return false;
      }
      NdsTarget_AudioPump();
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

   for (offset = 0u; offset < size; offset += PAL_NDS_SAVE_SPI_IO_BYTES)
   {
      uint32_t amount = (uint32_t)size - offset;

      if (amount > PAL_NDS_SAVE_SPI_IO_BYTES)
      {
         amount = PAL_NDS_SAVE_SPI_IO_BYTES;
      }
      if (!pal_nds_spi_write(base + offset, input + offset, amount))
      {
         pal_nds_save_log("save w%d: SPI write fail @%lx",
            slot, (unsigned long)offset);
         return false;
      }
      NdsTarget_AudioPump();
   }
   if (!pal_nds_spi_write(
         base + PAL_NDS_SAVE_FOOTER_OFFSET, &footer, sizeof(footer)))
   {
      pal_nds_save_log("save w%d: SPI footer fail", slot);
      return false;
   }
   pal_nds_save_log("save w%d: SPI ok %lu bytes",
      slot, (unsigned long)size);
   return true;
}

bool
PalTargetSave_WriteSlot(
   int slot,
   const void *source,
   size_t size)
{
   return pal_nds_retail_ready ?
      pal_nds_spi_write_slot(slot, source, size) :
      pal_nds_fat_write_slot(slot, source, size);
}
