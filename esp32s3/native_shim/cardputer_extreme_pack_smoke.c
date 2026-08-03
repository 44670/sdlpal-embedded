#include "pal_engine_pack_provider.h"
#include "cardputer_extreme_memory.h"
#include "pal_font10_cache.h"
#include "pal_native_ui.h"
#include "pal_pack.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct ReadAtContext {
   FILE *fp;
   uint32_t calls;
   uint64_t bytes;
} ReadAtContext;

FILE *__wrap_PAL_MKFOpenPackArchive(unsigned int archive_id);
int __wrap_PAL_MKFGetChunkCount(FILE *fp);
int __wrap_PAL_MKFGetChunkSize(unsigned int chunk_id, FILE *fp);
int __wrap_PAL_MKFMapChunk(
   FILE *fp,
   unsigned int chunk_id,
   const uint8_t **data,
   unsigned int *size);
int __wrap_PAL_MKFReadChunk(
   uint8_t *buffer,
   unsigned int buffer_size,
   unsigned int chunk_id,
   FILE *fp);

static uint8_t tf_chunk[320u * 200u];
static uint8_t rng_frame[320u * 200u];

static uint32_t
file_size(
   FILE *fp
)
{
   long end;

   if (fseek(fp, 0, SEEK_END) != 0)
      return 0;
   end = ftell(fp);
   if (end <= 0 || end > UINT32_MAX || fseek(fp, 0, SEEK_SET) != 0)
      return 0;
   return (uint32_t)end;
}

static bool
read_at(
   void *user,
   uint32_t offset,
   uint8_t *dst,
   uint32_t size
)
{
   ReadAtContext *ctx = (ReadAtContext *)user;

   if (ctx == NULL || ctx->fp == NULL || (dst == NULL && size != 0) ||
      fseek(ctx->fp, (long)offset, SEEK_SET) != 0 ||
      (size != 0 && fread(dst, 1, size, ctx->fp) != size))
   {
      return false;
   }
   ctx->calls++;
   ctx->bytes += size;
   return true;
}

int
main(
   int argc,
   char **argv
)
{
   const uint8_t *nor_image;
   const uint8_t *mapped = NULL;
   PalFont10Cache font10;
   PalEngineBridgeNativeRngFrame rng_view;
   PalPack nor_pack;
   unsigned int mapped_size = 0;
   ReadAtContext tf = {0};
   struct stat st;
   FILE *fbp;
   int nor_fd;
   uint32_t tf_size;
   uint32_t toc_calls;
   int got;

   if (argc != 3)
   {
      fprintf(stderr, "usage: %s pal_nor.pak pal_tf.pak\n", argv[0]);
      return 2;
   }
   nor_fd = open(argv[1], O_RDONLY);
   if (nor_fd < 0 || fstat(nor_fd, &st) != 0 ||
      st.st_size <= 0 || st.st_size > UINT32_MAX)
   {
      fprintf(stderr, "cannot open NOR pack\n");
      return 2;
   }
   nor_image = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, nor_fd, 0);
   close(nor_fd);
   if (nor_image == MAP_FAILED)
   {
      fprintf(stderr, "cannot mmap NOR pack\n");
      return 2;
   }
   tf.fp = fopen(argv[2], "rb");
   tf_size = tf.fp != NULL ? file_size(tf.fp) : 0;
   if (tf_size == 0)
   {
      fprintf(stderr, "cannot open TF pack\n");
      return 2;
   }

   PalEngineBridge_ClearPacks();
   if (!PalPack_OpenConst(&nor_pack, nor_image, (uint32_t)st.st_size) ||
      !PalFont10_Open(&nor_pack, &font10) ||
      font10.cell_width != 10u || font10.cell_height != 10u ||
      !PalEngineBridge_SetNorPackConst(nor_image, (uint32_t)st.st_size) ||
      !PalEngineBridge_SetTfPackReadAt(tf_size, read_at, &tf))
   {
      fprintf(stderr, "cannot initialize split packs\n");
      return 2;
   }
   toc_calls = tf.calls;
   fbp = __wrap_PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_FBP);
   if (fbp == NULL || __wrap_PAL_MKFGetChunkCount(fbp) < 38)
   {
      fprintf(stderr, "split FBP archive did not open\n");
      return 2;
   }

   /* FBP #0 is an mmap-able NOR chunk. */
   if (__wrap_PAL_MKFGetChunkSize(0, fbp) != 320 * 200 ||
      !__wrap_PAL_MKFMapChunk(fbp, 0, &mapped, &mapped_size) ||
      mapped == NULL || mapped_size != 320u * 200u)
   {
      fprintf(stderr, "NOR-owned FBP chunk failed\n");
      return 2;
   }

   /*
    * Equipment background FBP #1 is intentionally owned by TF to preserve
    * the music profile's NOR reserve.  Mapping is forbidden, while the same
    * bounded read-at path used by PAL_ReadNativeFbpToBuffer must work.
    */
   mapped = NULL;
   mapped_size = 0;
   if (__wrap_PAL_MKFGetChunkSize(1, fbp) != 320 * 200 ||
      PalEngineBridge_GetNativeChunkSize(fbp, 1) != 320 * 200 ||
      __wrap_PAL_MKFMapChunk(fbp, 1, &mapped, &mapped_size) ||
      __wrap_PAL_MKFReadChunk(tf_chunk, sizeof(tf_chunk), 1, fbp) !=
         (int)sizeof(tf_chunk) ||
      !PalEngineBridge_ReadNativeChunkRange(fbp, 1, 12345u,
         rng_frame, 320u) ||
      memcmp(rng_frame, tf_chunk + 12345u, 320u) != 0 ||
      tf.calls <= toc_calls)
   {
      fprintf(stderr, "TF-owned FBP streaming failed\n");
      return 2;
   }

   /* RNG #1 has a frame larger than screen B; range reads stay bounded. */
   got = PalEngineBridge_ReadNativeRngFrame(
      1, 0, rng_frame, (uint32_t)sizeof(rng_frame));
   if (got <= 0 || got > (int)sizeof(rng_frame) ||
      !PalEngineBridge_OpenNativeRngFrame(1, 0, &rng_view) ||
      rng_view.size != (uint32_t)got ||
      rng_view.size <= PAL_EXTREME_SCREEN_BYTES)
   {
      fprintf(stderr, "TF RNG frame streaming failed: %d\n", got);
      return 2;
   }
   {
      uint32_t offset = 0u;

      while (offset < rng_view.size)
      {
         uint32_t amount = rng_view.size - offset;

         if (amount > PAL_EXTREME_SCREEN_BYTES)
         {
            amount = PAL_EXTREME_SCREEN_BYTES;
         }
         if (!PalEngineBridge_ReadNativeRngFrameRange(&rng_view,
               offset, tf_chunk + offset, amount))
         {
            fprintf(stderr, "TF RNG bounded range read failed\n");
            return 2;
         }
         offset += amount;
      }
      if (memcmp(tf_chunk, rng_frame, rng_view.size) != 0)
      {
         fprintf(stderr, "TF RNG range/whole mismatch\n");
         return 2;
      }
      if (!PalEngineBridge_ReadNativeRngFrameRange(&rng_view,
            rng_view.size, NULL, 0u) ||
         PalEngineBridge_ReadNativeRngFrameRange(&rng_view,
            rng_view.size, rng_frame, 1u) ||
         PalEngineBridge_ReadNativeRngFrameRange(&rng_view,
            rng_view.size + 1u, rng_frame, 0u))
      {
         fprintf(stderr, "TF RNG range bounds failed\n");
         return 2;
      }
   }

   printf("cardputer_extreme_pack_smoke: toc_calls=%lu total_calls=%lu "
          "bytes=%llu rng_frame=%d window=%u\n",
      (unsigned long)toc_calls,
      (unsigned long)tf.calls,
      (unsigned long long)tf.bytes,
      got,
      (unsigned)PAL_EXTREME_SCREEN_BYTES);
   fclose(tf.fp);
   munmap((void *)nor_image, (size_t)st.st_size);
   return 0;
}
