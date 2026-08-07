#include "SDL.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}

static int test_indexed_blit(void)
{
    enum { WIDTH = 7, HEIGHT = 5, SOURCE_PITCH = 11, DESTINATION_PITCH = 13 };
    Uint8 source_pixels[SOURCE_PITCH * HEIGHT];
    Uint8 destination_pixels[DESTINATION_PITCH * HEIGHT];
    Uint8 scaled_pixels[11 * 8];
    SDL_Surface *source;
    SDL_Surface *destination;
    SDL_Surface *scaled;
    SDL_Rect source_rect;
    SDL_Rect destination_rect;
    int x;
    int y;

    memset(source_pixels, 0xee, sizeof(source_pixels));
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            source_pixels[y * SOURCE_PITCH + x] = (Uint8)(y * 32 + x);
        }
    }
    memset(destination_pixels, 0xa5, sizeof(destination_pixels));
    source = SDL_CreateRGBSurfaceFrom(source_pixels, WIDTH, HEIGHT, 8,
        SOURCE_PITCH, 0, 0, 0, 0);
    destination = SDL_CreateRGBSurfaceFrom(destination_pixels, WIDTH, HEIGHT, 8,
        DESTINATION_PITCH, 0, 0, 0, 0);
    if (source == NULL || destination == NULL ||
        SDL_BlitSurface(source, NULL, destination, NULL) != 0) {
        return fail("1:1 indexed blit failed");
    }
    for (y = 0; y < HEIGHT; y++) {
        if (memcmp(destination_pixels + y * DESTINATION_PITCH,
                source_pixels + y * SOURCE_PITCH, WIDTH) != 0 ||
            destination_pixels[y * DESTINATION_PITCH + WIDTH] != 0xa5) {
            return fail("1:1 indexed blit changed pixels or padding");
        }
    }

    memset(destination_pixels, 0xa5, sizeof(destination_pixels));
    source_rect.x = 1;
    source_rect.y = 1;
    source_rect.w = 5;
    source_rect.h = 3;
    destination_rect.x = -2;
    destination_rect.y = -1;
    destination_rect.w = 5;
    destination_rect.h = 3;
    if (SDL_BlitSurface(source, &source_rect, destination,
            &destination_rect) != 0 ||
        destination_pixels[0] != source_pixels[2 * SOURCE_PITCH + 3] ||
        destination_pixels[1] != source_pixels[2 * SOURCE_PITCH + 4] ||
        destination_pixels[DESTINATION_PITCH] != source_pixels[3 * SOURCE_PITCH + 3]) {
        return fail("clipped 1:1 indexed blit changed the mapping");
    }

    memset(scaled_pixels, 0xa5, sizeof(scaled_pixels));
    scaled = SDL_CreateRGBSurfaceFrom(scaled_pixels, 11, 8, 8, 11,
        0, 0, 0, 0);
    destination_rect.x = 0;
    destination_rect.y = 0;
    destination_rect.w = 11;
    destination_rect.h = 8;
    if (scaled == NULL || SDL_BlitSurface(source, NULL, scaled, &destination_rect) != 0) {
        return fail("scaled indexed blit failed");
    }
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 11; x++) {
            int source_x = (int)((int64_t)x * WIDTH / 11);
            int source_y = (int)((int64_t)y * HEIGHT / 8);

            if (scaled_pixels[y * 11 + x] !=
                source_pixels[source_y * SOURCE_PITCH + source_x]) {
                return fail("integer blit walk changed scaled mapping");
            }
        }
    }

    SDL_FreeSurface(source);
    SDL_FreeSurface(destination);
    SDL_FreeSurface(scaled);
    return 0;
}

static int test_overlapping_blit(void)
{
    enum { WIDTH = 8, HEIGHT = 8 };
    Uint8 pixels[WIDTH * HEIGHT];
    Uint8 expected[WIDTH * HEIGHT];
    SDL_Surface *surface;
    SDL_Rect source_rect = { 0, 0, 6, 6 };
    SDL_Rect destination_rect = { 1, 1, 6, 6 };
    int x;
    int y;

    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            pixels[y * WIDTH + x] = (Uint8)(y * WIDTH + x);
        }
    }
    memcpy(expected, pixels, sizeof(expected));
    for (y = 0; y < 6; y++) {
        for (x = 0; x < 6; x++) {
            expected[(y + 1) * WIDTH + x + 1] = (Uint8)(y * WIDTH + x);
        }
    }
    surface = SDL_CreateRGBSurfaceFrom(pixels, WIDTH, HEIGHT, 8, WIDTH,
        0, 0, 0, 0);
    if (surface == NULL || SDL_BlitSurface(surface, &source_rect,
            surface, &destination_rect) != 0 ||
        memcmp(pixels, expected, sizeof(pixels)) != 0) {
        return fail("overlapping indexed blit is not memmove-safe");
    }
    SDL_FreeSurface(surface);
    return 0;
}

static int test_fill_rect(void)
{
    Uint8 pixels[6 * 4];
    Uint8 pixels16[3 * 2 * 2];
    SDL_Surface *surface;
    SDL_Surface *surface16;
    SDL_Rect rect = { -1, 1, 4, 2 };

    memset(pixels, 0xa5, sizeof(pixels));
    surface = SDL_CreateRGBSurfaceFrom(pixels, 6, 4, 8, 6,
        0, 0, 0, 0);
    if (surface == NULL || SDL_FillRect(surface, &rect, 0x3c) != 0 ||
        pixels[6 + 0] != 0x3c || pixels[6 + 1] != 0x3c ||
        pixels[6 + 2] != 0x3c || pixels[6 + 3] != 0xa5 ||
        pixels[12 + 0] != 0x3c || pixels[12 + 2] != 0x3c ||
        pixels[12 + 3] != 0xa5) {
        return fail("8-bit fill fast path changed the wrong bytes");
    }

    memset(pixels16, 0xa5, sizeof(pixels16));
    surface16 = SDL_CreateRGBSurfaceFrom(pixels16, 3, 2, 16, 6,
        0, 0, 0, 0);
    if (surface16 == NULL || SDL_FillRect(surface16, NULL, 0x1234) != 0 ||
        pixels16[0] != 0x34 || pixels16[1] != 0x12 ||
        pixels16[4] != 0x34 || pixels16[5] != 0x12) {
        return fail("16-bit fill fast path changed the byte order");
    }

    SDL_FreeSurface(surface);
    SDL_FreeSurface(surface16);
    return 0;
}

int main(void)
{
    if (test_indexed_blit() != 0 ||
        test_overlapping_blit() != 0 ||
        test_fill_rect() != 0) {
        return 1;
    }
    puts("SDL shim blit test passed");
    return 0;
}
