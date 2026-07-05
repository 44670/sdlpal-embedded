#include "pal_memory.h"
#include "pal_pack.h"

#include <SDL.h>
#include <stdint.h>

static const uint8_t kNativePack[] = {
    0x50, 0x4c, 0x50, 0x4b, 0x01, 0x00, 0x20, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
    0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x2c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x24, 0x49, 0x6d, 0x92, 0xb6, 0xdb, 0xff,
};

static const SDL_Color kPalette[256] = {
    { 0x00, 0x00, 0x00, 0xff },
    { 0x24, 0x18, 0x10, 0xff },
    { 0x49, 0x30, 0x20, 0xff },
    { 0x6d, 0x48, 0x30, 0xff },
    { 0x92, 0x60, 0x40, 0xff },
    { 0xb6, 0x78, 0x50, 0xff },
    { 0xdb, 0x90, 0x60, 0xff },
    { 0xff, 0xa8, 0x70, 0xff },
};

static void fill_framebuffer(const uint8_t *pattern, uint32_t pattern_size)
{
    uint32_t y;

    for (y = 0; y < 200u; y++) {
        uint32_t x;
        for (x = 0; x < 320u; x++) {
            pal_sram_framebuffer[y * 320u + x] = pattern[(x + y) & (pattern_size - 1u)];
        }
    }
}

int main(void)
{
    PalPack pack;
    PalPackSpan span;
    SDL_Surface *surface;
    uint16_t chunk_count = 0;

    if (!PalPack_OpenConst(&pack, kNativePack, (uint32_t)sizeof(kNativePack))) {
        return 1;
    }
    if (!PalPack_MapConst(&pack, PAL_PACK_ARCHIVE_DATA, 0, &span)) {
        return 2;
    }
    if (!PalPack_GetChunkCount(&pack, PAL_PACK_ARCHIVE_DATA, &chunk_count) || chunk_count != 1u) {
        return 11;
    }
    if (span.size != 8u || span.format != PAL_PACK_FORMAT_NATIVE || span.data[7] != 0xffu) {
        return 3;
    }

    fill_framebuffer(span.data, span.size);
    if (pal_sram_framebuffer[0] != 0x00u || pal_sram_framebuffer[1] != 0x24u) {
        return 4;
    }
    if (pal_sram_framebuffer[(199u * 320u) + 319u] != span.data[(199u + 319u) & 7u]) {
        return 5;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return 6;
    }

    surface = SDL_CreateRGBSurfaceFrom(
        pal_sram_framebuffer,
        320,
        200,
        8,
        320,
        0,
        0,
        0,
        0);
    if (surface == 0) {
        SDL_Quit();
        return 7;
    }
    if (surface->pixels != pal_sram_framebuffer || surface->pitch != 320) {
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 8;
    }
    if (surface->format == 0 || surface->format->palette == 0) {
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 9;
    }
    if (SDL_SetPaletteColors(surface->format->palette, kPalette, 0, 256) != 0) {
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 10;
    }

    SDL_FreeSurface(surface);
    SDL_Quit();
    return 0;
}
