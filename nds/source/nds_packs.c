#include "pal_engine_pack_provider.h"

#include "pal_font10_cache.h"
#include "pal_level2_resident_pack.h"
#include "pal_memory_profile.h"
#include "pal_target_board.h"
#include "pal_target_save.h"

#include <fat.h>
#include <filesystem.h>
#include <nds.h>

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PAL_NDS_PACK_PATH "nitro:/pal_full.pak"
#define PAL_NDS_PACK_READ_SLICE 1024u

static int pal_nds_pack_fd = -1;
static int pal_nds_rom_fd = -1;
static NitroRom pal_nds_rom;
static const char *pal_nds_pack_error = "resource pack initialization failed";
static const char *pal_nds_launch_path;

void
NdsTarget_SetLaunchPath(
   const char *path)
{
   pal_nds_launch_path = path;
}

const char *
NdsTarget_PackError(
   void)
{
   return pal_nds_pack_error;
}

static bool
pal_nds_rom_read(
   void *user,
   uint32_t offset,
   void *destination,
   uint32_t size)
{
   int fd = *(const int *)user;
   uint8_t *output = (uint8_t *)destination;
   uint32_t done = 0u;

   if (fd < 0 || (destination == NULL && size != 0u) ||
      lseek(fd, (off_t)offset, SEEK_SET) != (off_t)offset)
   {
      return false;
   }
   while (done < size)
   {
      ssize_t got = read(fd, output + done, size - done);

      if (got <= 0)
      {
         return false;
      }
      done += (uint32_t)got;
   }
   return true;
}

static void
pal_nds_rom_close(
   void *user)
{
   int *fd = (int *)user;

   if (*fd >= 0)
   {
      close(*fd);
      *fd = -1;
   }
}

static const NitroRomIface pal_nds_rom_iface = {
   .read = pal_nds_rom_read,
   .close = pal_nds_rom_close,
};

static uint32_t
pal_nds_read_le32(
   const uint8_t *source)
{
   return (uint32_t)source[0] |
      ((uint32_t)source[1] << 8) |
      ((uint32_t)source[2] << 16) |
      ((uint32_t)source[3] << 24);
}

static bool
pal_nds_mount_self_rom(
   const char *path)
{
   uint8_t header[0x50];
   struct stat st;
   NitroRomParams params;

   pal_nds_rom_fd = open(path, O_RDONLY);
   if (pal_nds_rom_fd < 0 || fstat(pal_nds_rom_fd, &st) != 0 ||
      st.st_size < (off_t)sizeof(header) ||
      (uint64_t)st.st_size > UINT32_MAX ||
      !pal_nds_rom_read(
         &pal_nds_rom_fd, 0u, header, sizeof(header)))
   {
      return false;
   }
   params.fnt_offset = pal_nds_read_le32(header + 0x40u);
   params.fnt_sz = pal_nds_read_le32(header + 0x44u);
   params.fat_offset = pal_nds_read_le32(header + 0x48u);
   params.fat_sz = pal_nds_read_le32(header + 0x4cu);
   params.img_offset = 0u;
   if (params.fnt_sz == 0u || params.fat_sz == 0u ||
      params.fnt_sz > (uint32_t)st.st_size ||
      params.fat_sz > (uint32_t)st.st_size ||
      params.fnt_offset > (uint32_t)st.st_size - params.fnt_sz ||
      params.fat_offset > (uint32_t)st.st_size - params.fat_sz)
   {
      pal_nds_rom_close(&pal_nds_rom_fd);
      return false;
   }
   if (!nitroromOpen(
         &pal_nds_rom, &params, &pal_nds_rom_iface, &pal_nds_rom_fd))
   {
      pal_nds_rom_close(&pal_nds_rom_fd);
      return false;
   }
   if (!nitroFSMount(&pal_nds_rom))
   {
      nitroromClose(&pal_nds_rom);
      return false;
   }
   return true;
}

static bool
pal_nds_pack_read_at(
   void *user,
   uint32_t offset,
   uint8_t *dst,
   uint32_t size)
{
   int fd = *(const int *)user;
   uint32_t done = 0u;

   if (fd < 0 || (dst == NULL && size != 0u) ||
      lseek(fd, (off_t)offset, SEEK_SET) != (off_t)offset)
   {
      return false;
   }
   while (done < size)
   {
      uint32_t remaining = size - done;
      size_t request = remaining > PAL_NDS_PACK_READ_SLICE
         ? PAL_NDS_PACK_READ_SLICE : (size_t)remaining;
      ssize_t got = read(fd, dst + done, request);

      if (got <= 0)
      {
         return false;
      }
      done += (uint32_t)got;
      NdsTarget_AudioPump();
   }
   return true;
}

bool
PalEngineBridge_TargetInitPacks(
   void)
{
   struct stat st;
   PalPackToc full_toc;
   PalPack resident;
   PalFont10Cache font10;
   uint32_t resident_size;
   uint32_t full_size;

   PalEngineBridge_ClearPacks();
   NdsTargetSave_SetDldiReady(false);
   if (isDSiMode())
   {
      pal_nds_pack_error = "TWL mode is not supported";
      return false;
   }
   if (pal_nds_launch_path == NULL ||
      strncmp(pal_nds_launch_path, "fat:/", 5u) != 0)
   {
      pal_nds_pack_error = "DLDI launch path is missing";
      return false;
   }

   NdsTarget_BootLog("storage: mounting DLDI FAT");
   if (!fatInitDefault())
   {
      pal_nds_pack_error = "DLDI FAT mount failed";
      return false;
   }
   NdsTargetSave_SetDldiReady(true);
   NdsTarget_BootLog("storage: DLDI FAT ok");

   /* Pico Loader supplies argv[0] as fat:/path/to/sdlpal.nds. Open it
    * explicitly through the mounted DLDI volume and mount its embedded
    * NitroFS. There is deliberately no Slot-1 fallback. */
   if (!pal_nds_mount_self_rom(pal_nds_launch_path))
   {
      pal_nds_pack_error = "DLDI self-ROM NitroFS mount failed";
      return false;
   }
   pal_nds_pack_fd = open(PAL_NDS_PACK_PATH, O_RDONLY);
   if (pal_nds_pack_fd < 0 || fstat(pal_nds_pack_fd, &st) != 0 ||
      st.st_size < 32 || (uint64_t)st.st_size > UINT32_MAX)
   {
      pal_nds_pack_error = "open nitro:/pal_full.pak failed";
      return false;
   }
   full_size = (uint32_t)st.st_size;
   if (!PalPack_OpenTocRead(
         &full_toc,
         pal_nds_pack_read_at,
         &pal_nds_pack_fd,
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
         &pal_nds_pack_fd,
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
         full_size, pal_nds_pack_read_at, &pal_nds_pack_fd))
   {
      pal_nds_pack_error = "NitroFS stream provider setup failed";
      return false;
   }
   pal_nds_pack_error = NULL;
   return true;
}
