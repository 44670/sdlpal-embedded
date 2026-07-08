#include "pal_engine_pack_provider.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int pal_engine_native_tf_fd = -1;

static bool
native_read_at(
   void *user,
   uint32_t offset,
   uint8_t *dst,
   uint32_t size
)
{
   int fd = *(int *)user;
   uint32_t done = 0;

   while (done < size)
   {
      ssize_t got = pread(fd, dst + done, (size_t)(size - done), (off_t)(offset + done));
      if (got <= 0)
      {
         return false;
      }
      done += (uint32_t)got;
   }
   return true;
}

static bool
map_nor_pack(
   const char *path
)
{
   int fd;
   struct stat st;
   const uint8_t *image;

   if (path == NULL || path[0] == '\0')
   {
      return false;
   }
   fd = open(path, O_RDONLY);
   if (fd < 0)
   {
      return false;
   }
   if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > UINT32_MAX)
   {
      close(fd);
      return false;
   }
   image = (const uint8_t *)mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
   close(fd);
   if (image == MAP_FAILED)
   {
      return false;
   }
   return PalEngineBridge_SetNorPackConst(image, (uint32_t)st.st_size);
}

static bool
open_tf_pack(
   const char *path
)
{
   int fd;
   struct stat st;

   if (path == NULL || path[0] == '\0')
   {
      return false;
   }
   fd = open(path, O_RDONLY);
   if (fd < 0)
   {
      return false;
   }
   if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > UINT32_MAX)
   {
      close(fd);
      return false;
   }
   pal_engine_native_tf_fd = fd;
   if (!PalEngineBridge_SetTfPackReadAt((uint32_t)st.st_size, native_read_at, &pal_engine_native_tf_fd))
   {
      close(fd);
      pal_engine_native_tf_fd = -1;
      return false;
   }
   return true;
}

bool
PalEngineBridge_LoadDefaultPacks(
   void
)
{
   bool nor_ok;
   bool tf_ok;
   const char *nor_path = getenv("PAL_CONTRACT_NOR_PACK");
   const char *tf_path = getenv("PAL_CONTRACT_TF_PACK");

   PalEngineBridge_ClearPacks();
   nor_ok = map_nor_pack(nor_path) || map_nor_pack("/tmp/pal_nor_default.pak");
   tf_ok = open_tf_pack(tf_path) || open_tf_pack("/tmp/pal_tf_default.pak");
   return nor_ok && tf_ok;
}
