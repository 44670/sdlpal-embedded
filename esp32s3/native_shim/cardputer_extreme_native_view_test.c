#include "cardputer_extreme_native_view.h"

#include <stdio.h>
#include <string.h>

static uint8_t pixels[
    PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH *
    PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT];
static uint8_t palette[256u * 4u];
static uint8_t output[PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH * 2u * 2u];

static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}

int main(void)
{
    uint16_t destination_x;
    uint16_t destination_y;
    uint16_t x;
    uint16_t y;

    if (!CardputerExtreme_NativeViewValidate()) {
        return fail("native view validation failed");
    }
    for (y = 0; y < PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT; y++) {
        for (x = 0; x < PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH; x++) {
            pixels[(size_t)y * PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH + x] =
                (uint8_t)(x + y);
        }
    }
    for (x = 0; x < 256u; x++) {
        palette[(size_t)x * 4u + 0u] = (uint8_t)x;
        palette[(size_t)x * 4u + 1u] = 0u;
        palette[(size_t)x * 4u + 2u] = 0u;
        palette[(size_t)x * 4u + 3u] = 0xffu;
    }
    memset(output, 0, sizeof(output));
    if (!CardputerExtreme_CopyIndexedNativeStrip(
            pixels, PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH,
            palette, 0u, 2u, output,
            PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH * 2u,
            sizeof(output))) {
        return fail("native strip conversion failed");
    }
    for (destination_y = 0; destination_y < 2u; destination_y++) {
        for (destination_x = 0;
             destination_x < PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH;
             destination_x++) {
            size_t offset =
                ((size_t)destination_y *
                 PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH + destination_x) * 2u;
            uint8_t expected;

            expected = (uint8_t)(pixels[
                (size_t)destination_y *
                    PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH + destination_x] &
                0xf8u);
            if (output[offset] != expected || output[offset + 1u] != 0u) {
                return fail("native strip changed a source pixel");
            }
        }
    }
    return 0;
}
