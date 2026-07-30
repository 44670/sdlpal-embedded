#include "../main/cardputer_extreme_scaler.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    TEST_ROWS = 9,
    TEST_OUTPUT_BYTES =
        CARDPUTER_EXTREME_SCALER_VIEW_WIDTH * TEST_ROWS * 2,
};

static uint8_t source_pixels[
    CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH *
    CARDPUTER_EXTREME_SCALER_SOURCE_HEIGHT];
static uint8_t palette_rgba[256 * 4];
static uint8_t output[TEST_OUTPUT_BYTES + 2];

static uint16_t
expected_source_coordinate(
    uint16_t destination,
    uint16_t source_size,
    uint16_t destination_size)
{
    return (uint16_t)(
        ((uint32_t)(destination * 2u + 1u) * source_size) /
        ((uint32_t)destination_size * 2u));
}

static uint16_t
expected_rgb565(
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8u) << 8) |
                      ((uint16_t)(g & 0xFCu) << 3) |
                      ((uint16_t)b >> 3));
}

static int
verify_strip(
    uint16_t destination_y,
    uint16_t rows)
{
    uint16_t row;

    memset(output, 0xA5, sizeof(output));
    if (!CardputerExtreme_ScaleIndexedStrip(
            source_pixels,
            CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH,
            palette_rgba,
            destination_y,
            rows,
            output,
            (size_t)CARDPUTER_EXTREME_SCALER_VIEW_WIDTH * rows * 2u)) {
        fprintf(stderr, "valid scaler call rejected\n");
        return 1;
    }

    for (row = 0; row < rows; row++) {
        uint16_t view_y = (uint16_t)(destination_y + row);
        uint16_t source_y = expected_source_coordinate(
            view_y,
            CARDPUTER_EXTREME_SCALER_SOURCE_HEIGHT,
            CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT);
        uint16_t view_x;

        for (view_x = 0;
             view_x < CARDPUTER_EXTREME_SCALER_VIEW_WIDTH;
             view_x++) {
            uint16_t source_x = expected_source_coordinate(
                view_x,
                CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH,
                CARDPUTER_EXTREME_SCALER_VIEW_WIDTH);
            uint8_t index =
                source_pixels[
                    (uint32_t)source_y *
                        CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH +
                    source_x];
            const uint8_t *color = palette_rgba + (uint32_t)index * 4u;
            uint16_t expected =
                expected_rgb565(color[0], color[1], color[2]);
            const uint8_t *actual =
                output +
                ((uint32_t)row *
                     CARDPUTER_EXTREME_SCALER_VIEW_WIDTH +
                 view_x) *
                    2u;

            if (actual[0] != (uint8_t)(expected >> 8) ||
                actual[1] != (uint8_t)expected) {
                fprintf(
                    stderr,
                    "pixel mismatch view=(%u,%u) source=(%u,%u)\n",
                    (unsigned)view_x,
                    (unsigned)view_y,
                    (unsigned)source_x,
                    (unsigned)source_y);
                return 1;
            }
        }
    }

    if (output[(size_t)CARDPUTER_EXTREME_SCALER_VIEW_WIDTH * rows * 2u] !=
            0xA5 ||
        output[(size_t)CARDPUTER_EXTREME_SCALER_VIEW_WIDTH * rows * 2u + 1u] !=
            0xA5) {
        fprintf(stderr, "scaler wrote past destination strip\n");
        return 1;
    }
    return 0;
}

int
main(void)
{
    uint32_t y;
    uint32_t x;

    for (y = 0; y < CARDPUTER_EXTREME_SCALER_SOURCE_HEIGHT; y++) {
        for (x = 0; x < CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH; x++) {
            source_pixels[
                y * CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH + x] =
                (uint8_t)((x * 3u + y * 5u) & 0xFFu);
        }
    }
    for (x = 0; x < 256u; x++) {
        palette_rgba[x * 4u + 0u] = (uint8_t)x;
        palette_rgba[x * 4u + 1u] = (uint8_t)(255u - x);
        palette_rgba[x * 4u + 2u] = (uint8_t)(x ^ 0x5Au);
        palette_rgba[x * 4u + 3u] = 0xFFu;
    }

    if (verify_strip(0, TEST_ROWS) != 0 ||
        verify_strip(
            CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT - TEST_ROWS,
            TEST_ROWS) != 0) {
        return 1;
    }

    if (expected_source_coordinate(
            CARDPUTER_EXTREME_SCALER_VIEW_WIDTH - 1u,
            CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH,
            CARDPUTER_EXTREME_SCALER_VIEW_WIDTH) !=
            CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH - 1u ||
        expected_source_coordinate(
            CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT - 1u,
            CARDPUTER_EXTREME_SCALER_SOURCE_HEIGHT,
            CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT) !=
            CARDPUTER_EXTREME_SCALER_SOURCE_HEIGHT - 1u) {
        fprintf(stderr, "center-sampled mapping does not reach source edge\n");
        return 1;
    }

    if (CardputerExtreme_ScaleIndexedStrip(
            source_pixels,
            CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH,
            palette_rgba,
            0,
            TEST_ROWS,
            output,
            TEST_OUTPUT_BYTES - 1u) ||
        CardputerExtreme_ScaleIndexedStrip(
            source_pixels,
            CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH - 1u,
            palette_rgba,
            0,
            TEST_ROWS,
            output,
            TEST_OUTPUT_BYTES) ||
        CardputerExtreme_ScaleIndexedStrip(
            source_pixels,
            CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH,
            palette_rgba,
            CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT,
            1,
            output,
            TEST_OUTPUT_BYTES)) {
        fprintf(stderr, "invalid scaler call accepted\n");
        return 1;
    }

    printf(
        "cardputer extreme scaler: PASS source=%ux%u view=%ux%u ratio=27/40\n",
        CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH,
        CARDPUTER_EXTREME_SCALER_SOURCE_HEIGHT,
        CARDPUTER_EXTREME_SCALER_VIEW_WIDTH,
        CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT);
    return 0;
}
