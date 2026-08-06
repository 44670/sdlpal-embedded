#include "pal_target_save.h"

#include "pal_target_board.h"

#include <nds.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
   PAL_NDS_SAVE_SLOT_COUNT = 5u,
   PAL_NDS_SAVE_SLOT_BYTES = 0x30000u,
   PAL_NDS_SAVE_FOOTER_BYTES = 64u,
   PAL_NDS_SAVE_FOOTER_OFFSET =
      PAL_NDS_SAVE_SLOT_BYTES - PAL_NDS_SAVE_FOOTER_BYTES,
   PAL_NDS_SAVE_PAYLOAD_CAPACITY = PAL_NDS_SAVE_FOOTER_OFFSET,
   PAL_NDS_SAVE_REQUIRED_BYTES = 1024u * 1024u,
   PAL_NDS_SAVE_SECTOR_BYTES = 64u * 1024u,
   PAL_NDS_SAVE_IO_BYTES = 256u,
   PAL_NDS_SAVE_VERSION = 1u,
   PAL_NDS_SAVE_COMMITTED = 0x53415645u,
};

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
static int pal_nds_save_type = -1;
static uint32_t pal_nds_save_size;
static bool pal_nds_save_available;

_Static_assert(
   sizeof(PalNdsSaveFooter) == PAL_NDS_SAVE_FOOTER_BYTES,
   "Slot-1 save footer ABI changed");
_Static_assert(
   PAL_NDS_SAVE_SLOT_COUNT * PAL_NDS_SAVE_SLOT_BYTES <=
      PAL_NDS_SAVE_REQUIRED_BYTES,
   "five fixed PAL slots must fit the required save flash");

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
pal_nds_save_slot_base(
   int slot,
   uint32_t *base)
{
   if (!pal_nds_save_available || base == NULL ||
      slot < 1 || slot > (int)PAL_NDS_SAVE_SLOT_COUNT)
   {
      return false;
   }
   *base = (uint32_t)(slot - 1) * PAL_NDS_SAVE_SLOT_BYTES;
   return true;
}

static bool
pal_nds_save_read_footer(
   int slot,
   PalNdsSaveFooter *footer)
{
   uint32_t base;
   uint32_t expected_crc;

   if (footer == NULL || !pal_nds_save_slot_base(slot, &base))
   {
      return false;
   }
   cardReadEeprom(
      base + PAL_NDS_SAVE_FOOTER_OFFSET,
      (uint8_t *)footer,
      sizeof(*footer),
      (uint32_t)pal_nds_save_type);
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
   /*
    * Retail software selects its save-chip protocol as part of the cartridge
    * profile.  Auto-detection cannot reliably distinguish a FLASH chip whose
    * JEDEC ID is all 0xff from a regular EEPROM.  This ROM therefore has one
    * explicit save profile: type-3 SPI FLASH with 1 MiB capacity.
    */
   pal_nds_save_type = 3;
   pal_nds_save_size = PAL_NDS_SAVE_REQUIRED_BYTES;
   pal_nds_save_available = true;
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
   return pal_nds_save_type;
}

uint32_t
PalTargetSave_Capacity(
   void)
{
   return pal_nds_save_size;
}

bool
PalTargetSave_ProbeSlot(
   int slot,
   uint16_t *saved_times)
{
   PalNdsSaveFooter footer;
   uint32_t base;
   uint8_t count[2];

   if (saved_times == NULL ||
      !pal_nds_save_read_footer(slot, &footer) ||
      !pal_nds_save_slot_base(slot, &base))
   {
      return false;
   }
   cardReadEeprom(
      base, count, sizeof(count), (uint32_t)pal_nds_save_type);
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
   uint32_t base;
   uint32_t offset = 0u;
   uint32_t crc = 0xffffffffu;
   uint8_t *output = (uint8_t *)destination;

   if (out_size != NULL)
   {
      *out_size = 0u;
   }
   if (destination == NULL || out_size == NULL ||
      !pal_nds_save_read_footer(slot, &footer) ||
      footer.payload_size > capacity ||
      !pal_nds_save_slot_base(slot, &base))
   {
      return false;
   }
   while (offset < footer.payload_size)
   {
      uint32_t amount = footer.payload_size - offset;

      if (amount > PAL_NDS_SAVE_IO_BYTES)
      {
         amount = PAL_NDS_SAVE_IO_BYTES;
      }
      cardReadEeprom(
         base + offset,
         output + offset,
         amount,
         (uint32_t)pal_nds_save_type);
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
PalTargetSave_WriteSlot(
   int slot,
   const void *source,
   size_t size)
{
   PalNdsSaveFooter old_footer;
   PalNdsSaveFooter footer;
   PalNdsSaveFooter verify_footer;
   const uint8_t *input = (const uint8_t *)source;
   uint32_t base;
   uint32_t offset;
   uint32_t generation = 1u;

   if (source == NULL || size < 2u ||
      size > PAL_NDS_SAVE_PAYLOAD_CAPACITY ||
      !pal_nds_save_slot_base(slot, &base))
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
      cardEepromSectorErase(base + offset);
      NdsTarget_AudioPump();
   }

   for (offset = 0u; offset < size; offset += PAL_NDS_SAVE_IO_BYTES)
   {
      uint32_t amount = (uint32_t)size - offset;

      if (amount > PAL_NDS_SAVE_IO_BYTES)
      {
         amount = PAL_NDS_SAVE_IO_BYTES;
      }
      cardWriteEeprom(
         base + offset,
         (uint8_t *)(uintptr_t)(input + offset),
         amount,
         (uint32_t)pal_nds_save_type);
      NdsTarget_AudioPump();
   }

   for (offset = 0u; offset < size; offset += PAL_NDS_SAVE_IO_BYTES)
   {
      uint32_t amount = (uint32_t)size - offset;

      if (amount > sizeof(pal_nds_save_verify))
      {
         amount = sizeof(pal_nds_save_verify);
      }
      cardReadEeprom(
         base + offset,
         pal_nds_save_verify,
         amount,
         (uint32_t)pal_nds_save_type);
      if (memcmp(pal_nds_save_verify, input + offset, amount) != 0)
      {
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

   cardWriteEeprom(
      base + PAL_NDS_SAVE_FOOTER_OFFSET,
      (uint8_t *)&footer,
      sizeof(footer),
      (uint32_t)pal_nds_save_type);
   cardReadEeprom(
      base + PAL_NDS_SAVE_FOOTER_OFFSET,
      (uint8_t *)&verify_footer,
      sizeof(verify_footer),
      (uint32_t)pal_nds_save_type);
   return memcmp(&footer, &verify_footer, sizeof(footer)) == 0;
}
