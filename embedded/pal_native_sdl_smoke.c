#include "pal_memory.h"
#include "pal_pack.h"
#include "pal_video_static.h"

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

static const uint8_t kPaletteRgb[24] = {
    0x00, 0x00, 0x00,
    0x24, 0x18, 0x10,
    0x49, 0x30, 0x20,
    0x6d, 0x48, 0x30,
    0x92, 0x60, 0x40,
    0xb6, 0x78, 0x50,
    0xdb, 0x90, 0x60,
    0xff, 0xa8, 0x70,
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
    const uint16_t *line = 0;
    uint16_t pixels = 0;
    uint16_t converted_lines = 0;
    uint16_t chunk_count = 0;
    uint8_t saved_pixel;
    uint8_t saved_pixel_row1;

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
    if (!PalVideo_SetPaletteRgb(0, 8, kPaletteRgb)) {
        return 12;
    }
    if (pal_sram_framebuffer[0] != 0x00u || pal_sram_framebuffer[1] != 0x24u) {
        return 4;
    }
    if (pal_sram_framebuffer[(199u * 320u) + 319u] != span.data[(199u + 319u) & 7u]) {
        return 5;
    }
    PalVideo_SaveScreen();
    PalVideo_Clear(7);
    if (pal_sram_framebuffer[0] != 7u) {
        return 13;
    }
    PalVideo_RestoreScreen();
    if (pal_sram_framebuffer[0] != 0x00u || pal_sram_framebuffer[1] != 0x24u) {
        return 14;
    }
    if (!PalVideo_SaveRect(2, 3, 5, 4)) {
        return 16;
    }
    PalVideo_Clear(7);
    if (!PalVideo_RestoreRect(2, 3, 5, 4)) {
        return 17;
    }
    if (pal_sram_framebuffer[(3u * 320u) + 2u] != span.data[(3u + 2u) & 7u] ||
        pal_sram_framebuffer[0] != 7u ||
        PalVideo_SaveRect(319, 199, 2, 1) ||
        PalVideo_RestoreRect(0, 0, 0, 1)) {
        return 18;
    }
    PalVideo_RestoreScreen();
    PalVideo_SaveBigBuffer();
    PalVideo_Clear(6);
    PalVideo_RestoreBigBuffer();
    if (pal_sram_framebuffer[0] != 0x00u || pal_sram_framebuffer[1] != 0x24u) {
        return 19;
    }
    saved_pixel = pal_sram_framebuffer[1];
    saved_pixel_row1 = pal_sram_framebuffer[320u];
    pal_sram_framebuffer[1] = 1u;
    pal_sram_framebuffer[320u] = 2u;
    if (!PalVideo_ConvertLineRgb565(0, &line, &pixels) || line == 0 || pixels != 320u || line[0] != 0x0000u || line[1] != 0x20c2u) {
        pal_sram_framebuffer[1] = saved_pixel;
        pal_sram_framebuffer[320u] = saved_pixel_row1;
        return 15;
    }
    if (!PalVideo_ConvertLinesRgb565(0, 7u, &line, &pixels, &converted_lines) ||
        line == 0 ||
        pixels != 320u ||
        converted_lines != 6u ||
        line[0] != 0x0000u ||
        line[320u] != 0x4984u) {
        pal_sram_framebuffer[1] = saved_pixel;
        pal_sram_framebuffer[320u] = saved_pixel_row1;
        return 20;
    }
    pal_sram_framebuffer[1] = saved_pixel;
    pal_sram_framebuffer[320u] = saved_pixel_row1;

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
