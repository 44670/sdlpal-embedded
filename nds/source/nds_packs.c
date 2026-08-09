#include "pal_engine_pack_provider.h"

#include "pal_font10_cache.h"
#include "pal_level2_resident_pack.h"
#include "pal_memory_profile.h"
#include "pal_target_board.h"
#include "nds_retail.h"
#include <nds.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define PAL_NDS_PACK_READ_SLICE 1024u
#define PAL_NDS_PACK_NAME "pal_full.pak"

static const char *pal_nds_pack_error = "resource pack initialization failed";
static uint32_t pal_nds_pack_rom_offset;
static uint32_t pal_nds_pack_size;

enum {
   PAL_NDS_NITRO_ROOT_DIR_BYTES = 8u,
   PAL_NDS_NITRO_FAT_ENTRY_BYTES = 8u,
};

static bool
pal_nds_card_read(
   uint32_t offset,
   void *dst,
   uint32_t size)
{
   return NdsRetail_CardRead(offset, dst, size);
}

const char *
NdsTarget_PackError(
   void)
{
   return pal_nds_pack_error;
}

static bool
pal_nds_pack_read_at(
   void *user,
   uint32_t offset,
   uint8_t *dst,
   uint32_t size)
{
   uint32_t done = 0u;

   (void)user;
   if ((dst == NULL && size != 0u) || offset > pal_nds_pack_size ||
      size > pal_nds_pack_size - offset)
   {
      return false;
   }
   while (done < size)
   {
      uint32_t remaining = size - done;
      uint32_t request = remaining > PAL_NDS_PACK_READ_SLICE
         ? PAL_NDS_PACK_READ_SLICE : remaining;

      if (!pal_nds_card_read(
            pal_nds_pack_rom_offset + offset + done, dst + done, request))
      {
         return false;
      }
      done += request;
      NdsTarget_AudioPump();
   }
   return true;
}

static bool
pal_nds_find_pack(
   const EnvNdsHeader *header,
   uint32_t card_size)
{
   uint8_t root[PAL_NDS_NITRO_ROOT_DIR_BYTES];
   uint8_t name_entry[1u + sizeof(PAL_NDS_PACK_NAME)];
   uint8_t fat_entry[PAL_NDS_NITRO_FAT_ENTRY_BYTES];
   uint32_t root_subtable;
   uint32_t start;
   uint32_t end;
   uint16_t root_file_id;
   uint16_t dir_count;

   if (header == NULL || header->unitcode != 0u ||
      header->fnt_size < sizeof(root) ||
      header->fat_size != sizeof(fat_entry) ||
      header->fnt_size > card_size ||
      header->fat_size > card_size ||
      header->fnt_rom_offset > card_size - header->fnt_size ||
      header->fat_rom_offset > card_size - header->fat_size ||
      !pal_nds_card_read(header->fnt_rom_offset, root, sizeof(root)))
   {
      return false;
   }
   memcpy(&root_subtable, root, sizeof(root_subtable));
   memcpy(&root_file_id, root + 4u, sizeof(root_file_id));
   memcpy(&dir_count, root + 6u, sizeof(dir_count));
   if (root_subtable > header->fnt_size - sizeof(name_entry) ||
      root_file_id != 0u || dir_count != 1u ||
      !pal_nds_card_read(
         header->fnt_rom_offset + root_subtable,
         name_entry,
         sizeof(name_entry)) ||
      name_entry[0] != sizeof(PAL_NDS_PACK_NAME) - 1u ||
      memcmp(name_entry + 1u, PAL_NDS_PACK_NAME,
         sizeof(PAL_NDS_PACK_NAME) - 1u) != 0 ||
      name_entry[sizeof(name_entry) - 1u] != 0u ||
      !pal_nds_card_read(
         header->fat_rom_offset, fat_entry, sizeof(fat_entry)))
   {
      return false;
   }
   memcpy(&start, fat_entry, sizeof(start));
   memcpy(&end, fat_entry + 4u, sizeof(end));
   if (start >= end || end > card_size)
   {
      return false;
   }
   pal_nds_pack_rom_offset = start;
   pal_nds_pack_size = end - start;
   return true;
}

bool
PalEngineBridge_TargetInitPacks(
   void)
{
   EnvNdsHeader header;
   PalPackToc full_toc;
   PalPack resident;
   PalFont10Cache font10;
   uint32_t resident_size;
   uint32_t full_size;
   uint32_t card_size;

   PalEngineBridge_ClearPacks();
   pal_nds_pack_rom_offset = 0u;
   pal_nds_pack_size = 0u;
   if (isDSiMode())
   {
      pal_nds_pack_error = "TWL mode is not supported";
      return false;
   }
   NdsTarget_BootLog("storage: opening retail CARD/NitroFS");
   /* Retail CARD reads cannot access the protected 0x0000..0x7fff region.
    * The boot firmware/kernel has already copied the application header to
    * the standard environment slot; use it to locate the FNT/FAT, then read
    * all file data through the SDK-shaped CARD path. Bytes 0x70 onward may
    * overlap environment data, so use the nominal cartridge capacity. */
   memcpy(&header, g_envAppNdsHeader, sizeof(header));
   if (header.device_capacity >= 15u)
   {
      pal_nds_pack_error = "NTR Slot-1 capacity is invalid";
      return false;
   }
   card_size = 0x20000u << header.device_capacity;
   if (!pal_nds_find_pack(&header, card_size))
   {
      pal_nds_pack_error = "Slot-1 NitroFS layout is invalid";
      return false;
   }
   NdsTarget_BootLog("storage: retail NitroFS map ok");
   full_size = pal_nds_pack_size;
   if (!PalPack_OpenTocRead(
         &full_toc,
         pal_nds_pack_read_at,
         NULL,
         full_size,
         pal_mem_level2_tf_toc,
         PAL_MEM_LEVEL2_TF_TOC_BYTES))
   {
      pal_nds_pack_error = "pal_full.pak TOC is invalid";
      return false;
   }
   if (!PalLevel2ResidentPack_Build(
         &full_toc,
         PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(PAL_PACK_ARCHIVE_DATA) |
            PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(PAL_PACK_ARCHIVE_SSS) |
            PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(PAL_PACK_ARCHIVE_TEXT) |
            PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(PAL_PACK_ARCHIVE_FONT),
         pal_nds_pack_read_at,
         NULL,
         pal_mem_level2_resident_pack,
         PAL_MEM_LEVEL2_RESIDENT_PACK_BYTES,
         &resident_size))
   {
      pal_nds_pack_error = "resident resource image does not fit";
      return false;
   }
   if (!PalPack_OpenConst(
         &resident, pal_mem_level2_resident_pack, resident_size))
   {
      pal_nds_pack_error = "resident resource image is invalid";
      return false;
   }
   if (!PalFont10_Open(&resident, &font10) ||
      font10.cell_width != 10u || font10.cell_height != 10u)
   {
      pal_nds_pack_error = "FONT10 resource is invalid";
      return false;
   }
   if (!PalEngineBridge_SetCorePackConst(
         pal_mem_level2_resident_pack, resident_size))
   {
      pal_nds_pack_error = "resident pack provider rejected image";
      return false;
   }
   if (!PalEngineBridge_SetTfPackReadAt(
         full_size, pal_nds_pack_read_at, NULL))
   {
      pal_nds_pack_error = "Slot-1 stream provider setup failed";
      return false;
   }
   pal_nds_pack_error = NULL;
   return true;
}
