#include "pal_memory.h"
#include "pal_pack.h"

#include <SDL.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct MappedPack {
    const uint8_t *data;
    uint32_t size;
    PalPack pack;
} MappedPack;

static const SDL_Color kPalette[8] = {
    { 0x00, 0x00, 0x00, 0xff },
    { 0x24, 0x18, 0x10, 0xff },
    { 0x49, 0x30, 0x20, 0xff },
    { 0x6d, 0x48, 0x30, 0xff },
    { 0x92, 0x60, 0x40, 0xff },
    { 0xb6, 0x78, 0x50, 0xff },
    { 0xdb, 0x90, 0x60, 0xff },
    { 0xff, 0xa8, 0x70, 0xff },
};

static int map_pack_file(const char *path, MappedPack *mapped)
{
    int fd;
    struct stat st;
    const uint8_t *data;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 1;
    }
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 0x7fffffffL) {
        close(fd);
        return 2;
    }
    data = (const uint8_t *)mmap(0, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED) {
        return 3;
    }

    mapped->data = data;
    mapped->size = (uint32_t)st.st_size;
    if (!PalPack_OpenConst(&mapped->pack, data, mapped->size)) {
        munmap((void *)data, (size_t)st.st_size);
        mapped->data = 0;
        mapped->size = 0;
        return 4;
    }
    return 0;
}

static void unmap_pack_file(MappedPack *mapped)
{
    if (mapped->data != 0) {
        munmap((void *)mapped->data, mapped->size);
        mapped->data = 0;
        mapped->size = 0;
    }
}

static int expect_chunk_count(const PalPack *pack, uint16_t archive_id, uint16_t expected)
{
    uint16_t actual = 0;
    if (!PalPack_GetChunkCount(pack, archive_id, &actual)) {
        return 1;
    }
    return actual == expected ? 0 : 2;
}

static int map_native_nonempty(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id)
{
    PalPackSpan span;
    if (!PalPack_MapConst(pack, archive_id, chunk_id, &span)) {
        return 1;
    }
    if (span.data == 0 || span.size == 0 || span.format != PAL_PACK_FORMAT_NATIVE) {
        return 2;
    }
    return 0;
}

static uint32_t checksum32(const uint8_t *data, uint32_t size)
{
    uint32_t i;
    uint32_t sum = 0;

    for (i = 0; i < size; i++) {
        sum = (sum << 5) - sum + data[i];
    }
    return sum;
}

static int exercise_tf_reads(const PalPack *tf)
{
    uint32_t copied = 0;

    if (!PalPack_CopyRaw(tf, PAL_PACK_ARCHIVE_FBP, 0, pal_sram_framebuffer, PAL_SRAM_FRAMEBUFFER_BYTES, &copied)) {
        return 1;
    }
    if (copied != PAL_SRAM_FRAMEBUFFER_BYTES) {
        return 2;
    }
    if (!PalPack_CopyRaw(tf, PAL_PACK_ARCHIVE_MAP, 1, pal_psram_map_tiles, PAL_PSRAM_MAP_TILES_BYTES, &copied)) {
        return 3;
    }
    if (copied != PAL_PSRAM_MAP_TILES_BYTES) {
        return 4;
    }
    if (!PalPack_CopyRaw(tf, PAL_PACK_ARCHIVE_GOP, 1, pal_psram_gop_copy, PAL_PSRAM_GOP_COPY_BYTES, &copied)) {
        return 5;
    }
    if (copied == 0 || copied > PAL_PSRAM_GOP_COPY_BYTES) {
        return 6;
    }
    if (!PalPack_CopyRaw(tf, PAL_PACK_ARCHIVE_VOC, 1, pal_psram_sfx_bank, PAL_PSRAM_SFX_BANK_BYTES, &copied)) {
        return 7;
    }
    if (copied == 0 || copied > PAL_PSRAM_SFX_BANK_BYTES) {
        return 8;
    }
    if (checksum32(pal_sram_framebuffer, PAL_SRAM_FRAMEBUFFER_BYTES) == 0) {
        return 9;
    }
    if (checksum32(pal_psram_map_tiles, PAL_PSRAM_MAP_TILES_BYTES) == 0) {
        return 10;
    }
    return 0;
}

static int exercise_sdl_surface(void)
{
    SDL_Surface *surface;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return 1;
    }
    surface = SDL_CreateRGBSurfaceFrom(pal_sram_framebuffer, 320, 200, 8, 320, 0, 0, 0, 0);
    if (surface == 0) {
        SDL_Quit();
        return 2;
    }
    if (surface->pixels != pal_sram_framebuffer || surface->pitch != 320) {
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 3;
    }
    if (surface->format == 0 || surface->format->palette == 0) {
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 4;
    }
    if (SDL_SetPaletteColors(surface->format->palette, kPalette, 0, 8) != 0) {
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 5;
    }
    SDL_FreeSurface(surface);
    SDL_Quit();
    return 0;
}

int main(int argc, char **argv)
{
    MappedPack nor = { 0, 0, { 0, 0, 0, 0 } };
    MappedPack tf = { 0, 0, { 0, 0, 0, 0 } };
    int rc;

    if (argc != 3) {
        return 1;
    }
    if (map_pack_file(argv[1], &nor) != 0) {
        return 2;
    }
    if (map_pack_file(argv[2], &tf) != 0) {
        unmap_pack_file(&nor);
        return 3;
    }

    rc =
        expect_chunk_count(&nor.pack, PAL_PACK_ARCHIVE_MGO, 637) ||
        expect_chunk_count(&nor.pack, PAL_PACK_ARCHIVE_ABC, 160) ||
        expect_chunk_count(&nor.pack, PAL_PACK_ARCHIVE_FIRE, 55) ||
        expect_chunk_count(&tf.pack, PAL_PACK_ARCHIVE_FBP, 72) ||
        expect_chunk_count(&tf.pack, PAL_PACK_ARCHIVE_MAP, 226) ||
        expect_chunk_count(&tf.pack, PAL_PACK_ARCHIVE_GOP, 226) ||
        expect_chunk_count(&tf.pack, PAL_PACK_ARCHIVE_RNG, 12) ||
        expect_chunk_count(&tf.pack, PAL_PACK_ARCHIVE_VOC, 276);
    if (rc == 0) {
        rc =
            map_native_nonempty(&nor.pack, PAL_PACK_ARCHIVE_MGO, 1) ||
            map_native_nonempty(&nor.pack, PAL_PACK_ARCHIVE_ABC, 1) ||
            map_native_nonempty(&nor.pack, PAL_PACK_ARCHIVE_FIRE, 0);
    }
    if (rc == 0) {
        rc = exercise_tf_reads(&tf.pack);
    }
    if (rc == 0) {
        rc = exercise_sdl_surface();
    }

    unmap_pack_file(&tf);
    unmap_pack_file(&nor);
    return rc == 0 ? 0 : 4;
}
