#include "pal_engine_pack_provider.h"
#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct ReadAtContext {
    FILE *fp;
    uint32_t calls;
    uint64_t bytes;
} ReadAtContext;

FILE *__wrap_PAL_MKFOpenPackArchive(unsigned int archive_id);
int __wrap_PAL_MKFGetChunkSize(unsigned int chunk_id, FILE *fp);
int __wrap_PAL_MKFMapChunk(FILE *fp, unsigned int chunk_id, const uint8_t **data, unsigned int *size);

static uint32_t file_size(FILE *fp)
{
    long end;

    if (fseek(fp, 0, SEEK_END) != 0) {
        return 0;
    }
    end = ftell(fp);
    if (end <= 0 || end > UINT32_MAX) {
        return 0;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        return 0;
    }
    return (uint32_t)end;
}

static bool read_at(void *user, uint32_t offset, uint8_t *dst, uint32_t size)
{
    ReadAtContext *ctx = (ReadAtContext *)user;

    if (ctx == NULL || ctx->fp == NULL || (dst == NULL && size != 0)) {
        return 0;
    }
    if (fseek(ctx->fp, (long)offset, SEEK_SET) != 0) {
        return 0;
    }
    if (size != 0 && fread(dst, 1, size, ctx->fp) != size) {
        return 0;
    }
    ctx->calls++;
    ctx->bytes += size;
    return 1;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/tmp/pal_tf_default.pak";
    ReadAtContext ctx = {0};
    FILE *rng;
    uint32_t pack_size;
    uint32_t toc_calls;
    uint32_t first_map_calls;
    const uint8_t *first_data = NULL;
    const uint8_t *second_data = NULL;
    const uint8_t *third_data = NULL;
    unsigned int first_size = 0;
    unsigned int second_size = 0;
    unsigned int third_size = 0;

    ctx.fp = fopen(path, "rb");
    if (ctx.fp == NULL) {
        fprintf(stderr, "open failed: %s\n", path);
        return 2;
    }
    pack_size = file_size(ctx.fp);
    if (pack_size == 0) {
        fprintf(stderr, "bad pack size: %s\n", path);
        fclose(ctx.fp);
        return 2;
    }

    PalEngineBridge_ClearPacks();
    if (!PalEngineBridge_SetTfPackReadAt(pack_size, read_at, &ctx)) {
        fprintf(stderr, "TF TOC open failed: %s\n", path);
        fclose(ctx.fp);
        return 2;
    }
    toc_calls = ctx.calls;

    rng = __wrap_PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_RNG);
    if (rng == NULL || __wrap_PAL_MKFGetChunkSize(0, rng) <= 0 ||
        __wrap_PAL_MKFGetChunkSize(1, rng) <= 0) {
        fprintf(stderr, "RNG archive unavailable\n");
        fclose(ctx.fp);
        return 2;
    }

    if (!__wrap_PAL_MKFMapChunk(rng, 0, &first_data, &first_size) ||
        first_data == NULL || first_size == 0) {
        fprintf(stderr, "first RNG map failed\n");
        fclose(ctx.fp);
        return 2;
    }
    first_map_calls = ctx.calls;
    if (first_map_calls <= toc_calls) {
        fprintf(stderr, "first map did not read payload\n");
        fclose(ctx.fp);
        return 2;
    }

    if (!__wrap_PAL_MKFMapChunk(rng, 0, &second_data, &second_size) ||
        second_data != first_data || second_size != first_size ||
        ctx.calls != first_map_calls) {
        fprintf(stderr, "repeated TF map did not hit cache\n");
        fclose(ctx.fp);
        return 2;
    }

    if (!__wrap_PAL_MKFMapChunk(rng, 1, &third_data, &third_size) ||
        third_data == NULL || third_size == 0 || ctx.calls <= first_map_calls) {
        fprintf(stderr, "second RNG chunk did not refill cache\n");
        fclose(ctx.fp);
        return 2;
    }
    if (fclose(rng) != 0) {
        fprintf(stderr, "pseudo pack fclose failed\n");
        fclose(ctx.fp);
        return 2;
    }

    printf("pack_provider_cache_smoke: toc_calls=%lu payload_calls=%lu bytes=%llu\n",
        (unsigned long)toc_calls,
        (unsigned long)(ctx.calls - toc_calls),
        (unsigned long long)ctx.bytes);
    fclose(ctx.fp);
    return 0;
}
