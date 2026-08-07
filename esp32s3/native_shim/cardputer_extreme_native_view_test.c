#include "cardputer_extreme_native_view.h"

#include <stdio.h>
#include <string.h>

#ifndef PAL_NATIVE_VIEW_TEST_WIDTH
#define PAL_NATIVE_VIEW_TEST_WIDTH 240u
#endif
#ifndef PAL_NATIVE_VIEW_TEST_HEIGHT
#define PAL_NATIVE_VIEW_TEST_HEIGHT 135u
#endif

static uint8_t pixels[
    PAL_NATIVE_VIEW_TEST_WIDTH * PAL_NATIVE_VIEW_TEST_HEIGHT];
static uint8_t palette[256u * 4u];
static uint8_t output[PAL_NATIVE_VIEW_TEST_WIDTH * 2u * 2u];

static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}

int main(void)
{
    static const uint8_t channel_palette[] = {
        0xffu, 0x00u, 0x00u, 0xffu,
        0x00u, 0xffu, 0x00u, 0xffu,
        0x00u, 0x00u, 0xffu, 0xffu,
    };
    static const uint8_t expected_wire_rgb565[] = {
        0xf8u, 0x00u,
        0x07u, 0xe0u,
        0x00u, 0x1fu,
    };
    uint16_t destination_x;
    uint16_t destination_y;
    uint16_t x;
    uint16_t y;

    if (!CardputerExtreme_NativeViewValidate(
            PAL_NATIVE_VIEW_TEST_WIDTH,
            PAL_NATIVE_VIEW_TEST_HEIGHT)) {
        return fail("native view validation failed");
    }
    for (y = 0; y < PAL_NATIVE_VIEW_TEST_HEIGHT; y++) {
        for (x = 0; x < PAL_NATIVE_VIEW_TEST_WIDTH; x++) {
            pixels[(size_t)y * PAL_NATIVE_VIEW_TEST_WIDTH + x] =
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
            pixels, PAL_NATIVE_VIEW_TEST_WIDTH,
            palette,
            PAL_NATIVE_VIEW_TEST_WIDTH, PAL_NATIVE_VIEW_TEST_HEIGHT,
            0u, 2u, output,
            PAL_NATIVE_VIEW_TEST_WIDTH * 2u,
            sizeof(output))) {
        return fail("native strip conversion failed");
    }
    for (destination_y = 0; destination_y < 2u; destination_y++) {
        for (destination_x = 0;
             destination_x < PAL_NATIVE_VIEW_TEST_WIDTH;
             destination_x++) {
            size_t offset =
                ((size_t)destination_y *
                 PAL_NATIVE_VIEW_TEST_WIDTH + destination_x) * 2u;
            uint8_t expected;

            expected = (uint8_t)(pixels[
                (size_t)destination_y *
                    PAL_NATIVE_VIEW_TEST_WIDTH + destination_x] &
                0xf8u);
            if (output[offset] != expected || output[offset + 1u] != 0u) {
                return fail("native strip changed a source pixel");
            }
        }
    }
    memset(pixels, 0, PAL_NATIVE_VIEW_TEST_WIDTH);
    pixels[0] = 0u;
    pixels[1] = 1u;
    pixels[2] = 2u;
    memset(output, 0, sizeof(output));
    if (!CardputerExtreme_CopyIndexedNativeStrip(
            pixels, PAL_NATIVE_VIEW_TEST_WIDTH,
            channel_palette,
            PAL_NATIVE_VIEW_TEST_WIDTH, PAL_NATIVE_VIEW_TEST_HEIGHT,
            0u, 1u, output,
            PAL_NATIVE_VIEW_TEST_WIDTH * 2u,
            sizeof(output))) {
        return fail("RGB565 channel conversion failed");
    }
    if (memcmp(output, expected_wire_rgb565,
            sizeof(expected_wire_rgb565)) != 0) {
        return fail("RGB565 framebuffer is not MSB-first RGB wire order");
    }

    memset(output, 0, sizeof(output));
    pixels[0] = 2u;
    pixels[1] = 0u;
    pixels[2] = 1u;
    pixels[3] = 2u;
    if (!CardputerExtreme_CopyIndexedNativeRegion(
            pixels, PAL_NATIVE_VIEW_TEST_WIDTH,
            channel_palette,
            PAL_NATIVE_VIEW_TEST_WIDTH, PAL_NATIVE_VIEW_TEST_HEIGHT,
            1u, 0u, 3u, 1u, output, 3u * 2u, sizeof(output))) {
        return fail("native region conversion failed");
    }
    if (memcmp(output, expected_wire_rgb565, sizeof(expected_wire_rgb565)) != 0) {
        return fail("native region conversion used the wrong source offset");
    }

    palette[0] = 0xffu;
    palette[1] = 0xffu;
    palette[2] = 0xffu;
    CardputerExtreme_NativeViewInvalidatePalette();
    memset(output, 0, sizeof(output));
    pixels[0] = 0u;
    if (!CardputerExtreme_CopyIndexedNativeStrip(
            pixels, PAL_NATIVE_VIEW_TEST_WIDTH,
            palette,
            PAL_NATIVE_VIEW_TEST_WIDTH, PAL_NATIVE_VIEW_TEST_HEIGHT,
            0u, 1u, output,
            PAL_NATIVE_VIEW_TEST_WIDTH * 2u,
            sizeof(output))) {
        return fail("palette invalidation conversion failed");
    }
    if (output[0] != 0xffu || output[1] != 0xffu) {
        return fail("palette invalidation did not rebuild the LUT");
    }
    return 0;
}
