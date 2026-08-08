#include "pal_engine_pack_provider.h"

#include "pal_font10_cache.h"
#include "pal_level2_resident_pack.h"
#include "pal_memory_profile.h"
#include "pal_target_board.h"
#include "pal_target_save.h"

#include <calico/dev/blk.h>
#include <dvm.h>
#include <filesystem.h>
#include <nds.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#define PAL_NDS_PACK_PATH "nitro:/pal_full.pak"
#define PAL_NDS_PACK_READ_SLICE 1024u

static int pal_nds_pack_fd = -1;
static const char *pal_nds_pack_error = "resource pack initialization failed";
static const char *pal_nds_launch_path;
static BlkDevice pal_nds_storage_device;

enum {
   PAL_NDS_FAT_CACHE_PAGES = 4u,
   PAL_NDS_FAT_SECTORS_PER_PAGE = 8u,
};

static bool
pal_nds_storage_startup(
   void)
{
   blkInit();
   return blkDevInit(pal_nds_storage_device);
}

static bool
pal_nds_storage_inserted(
   void)
{
   return blkDevIsPresent(pal_nds_storage_device);
}

static bool
pal_nds_storage_read(
   sec_t first_sector,
   sec_t sector_count,
   void *buffer)
{
   return blkDevReadSectors(
      pal_nds_storage_device, buffer, first_sector, sector_count);
}

static bool
pal_nds_storage_write(
   sec_t first_sector,
   sec_t sector_count,
   const void *buffer)
{
   return blkDevWriteSectors(
      pal_nds_storage_device, buffer, first_sector, sector_count);
}

static bool
pal_nds_storage_ok(
   void)
{
   return true;
}

static DISC_INTERFACE pal_nds_storage_iface = {
   .ioType = 0x4c415050u,
   .features = FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE,
   .startup = pal_nds_storage_startup,
   .isInserted = pal_nds_storage_inserted,
   .readSectors = pal_nds_storage_read,
   .writeSectors = pal_nds_storage_write,
   .clearStatus = pal_nds_storage_ok,
   .shutdown = pal_nds_storage_ok,
};

void
NdsTarget_SetLaunchPath(
   const char *path)
{
   pal_nds_launch_path = path;
}

static const char *
pal_nds_launch_volume(
   void)
{
   if (pal_nds_launch_path != NULL &&
      strncmp(pal_nds_launch_path, "sd:/", 4u) == 0)
   {
      return "sd";
   }
   return "fat";
}

static bool
pal_nds_mount_launch_storage(
   void)
{
   const char *volume;

   if (pal_nds_launch_path == NULL || pal_nds_launch_path[0] == '\0')
   {
      return false;
   }
   volume = pal_nds_launch_volume();
   pal_nds_storage_device = isDSiMode()
      ? BlkDevice_TwlSdCard : BlkDevice_Dldi;
   NdsTarget_BootLog(isDSiMode()
      ? "storage: mounting TWL SD" : "storage: mounting NTR DLDI");
   if (dvmProbeMountDiscIface(
         volume,
         &pal_nds_storage_iface,
         PAL_NDS_FAT_CACHE_PAGES,
         PAL_NDS_FAT_SECTORS_PER_PAGE) == 0u)
   {
      return false;
   }
   NdsTargetSave_SetMountedVolume(volume);
   NdsTarget_BootLog(isDSiMode()
      ? "storage: TWL SD ok" : "storage: NTR DLDI ok");
   return true;
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
   NitroRom *rom;

   PalEngineBridge_ClearPacks();
   NdsTargetSave_SetMountedVolume(NULL);
   if (pal_nds_launch_path != NULL &&
      !pal_nds_mount_launch_storage())
   {
      pal_nds_pack_error = isDSiMode()
         ? "TWL SD mount failed" : "NTR DLDI mount failed";
      return false;
   }
   rom = nitroromGetSelf();
   if (rom == NULL || !nitroFSMount(rom))
   {
      pal_nds_pack_error = "NitroFS self-ROM mount failed";
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
