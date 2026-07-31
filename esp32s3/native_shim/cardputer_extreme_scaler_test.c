#include "../main/cardputer_extreme_scaler.h"
#include "../main/cardputer_extreme_memory.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    TEST_ROWS = 9,
    TEST_OUTPUT_BYTES =
        CARDPUTER_EXTREME_SCALER_VIEW_WIDTH * TEST_ROWS * 2,
    TEST_PAD_X = 12,
    TEST_PAD_WIDTH = CARDPUTER_EXTREME_SCALER_VIEW_WIDTH + TEST_PAD_X * 2,
    TEST_PAD_PITCH_BYTES = TEST_PAD_WIDTH * 2,
};

/* The framebuffer capacity is an engine fixture; extents come from the pack. */
static uint8_t source_pixels[PAL_EXTREME_SCREEN_BYTES];
static uint8_t palette_rgba[256 * 4];
static uint8_t output[TEST_OUTPUT_BYTES + 2];
static uint8_t padded_output[TEST_PAD_PITCH_BYTES * TEST_ROWS + 2];

static uint16_t
expected_source_coordinate(
    uint16_t destination,
    uint16_t source_size,
    uint16_t destination_size)
{
    return (uint16_t)(
        ((uint64_t)(destination * 2u + 1u) * source_size) /
        ((uint64_t)destination_size * 2u));
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
    uint16_t source_width = CardputerExtreme_ScalerSourceWidth();
    uint16_t source_height = CardputerExtreme_ScalerSourceHeight();
    uint16_t view_width = CARDPUTER_EXTREME_SCALER_VIEW_WIDTH;
    uint16_t view_height = CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT;
    uint16_t row;

    memset(output, 0xA5, sizeof(output));
    if (!CardputerExtreme_ScaleIndexedStrip(
            source_pixels,
            source_width,
            palette_rgba,
            destination_y,
            rows,
            output,
            (size_t)view_width * 2u,
            (size_t)view_width * rows * 2u)) {
        fprintf(stderr, "valid scaler call rejected\n");
        return 1;
    }

    for (row = 0; row < rows; row++) {
        uint16_t view_y = (uint16_t)(destination_y + row);
        uint16_t source_y = expected_source_coordinate(
            view_y,
            source_height,
            view_height);
        uint16_t view_x;

        for (view_x = 0;
             view_x < view_width;
             view_x++) {
            uint16_t source_x = expected_source_coordinate(
                view_x,
                source_width,
                view_width);
            uint8_t index =
                source_pixels[
                    (uint32_t)source_y * source_width +
                    source_x];
            const uint8_t *color = palette_rgba + (uint32_t)index * 4u;
            uint16_t expected =
                expected_rgb565(color[0], color[1], color[2]);
            const uint8_t *actual =
                output +
                ((uint32_t)row *
                     view_width +
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

    if (output[(size_t)view_width * rows * 2u] !=
            0xA5 ||
        output[(size_t)view_width * rows * 2u + 1u] !=
            0xA5) {
        fprintf(stderr, "scaler wrote past destination strip\n");
        return 1;
    }
    return 0;
}

static int
verify_padded_strip(
    uint16_t destination_y,
    uint16_t rows)
{
    const uint16_t source_width = CardputerExtreme_ScalerSourceWidth();
    const uint16_t source_height = CardputerExtreme_ScalerSourceHeight();
    const uint16_t view_width = CARDPUTER_EXTREME_SCALER_VIEW_WIDTH;
    const uint16_t view_height = CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT;
    uint16_t row;

    memset(padded_output, 0xA5, sizeof(padded_output));
    if (!CardputerExtreme_ScaleIndexedStrip(
            source_pixels,
            source_width,
            palette_rgba,
            destination_y,
            rows,
            padded_output + TEST_PAD_X * 2u,
            TEST_PAD_PITCH_BYTES,
            sizeof(padded_output) - TEST_PAD_X * 2u)) {
        fprintf(stderr, "valid padded-pitch scaler call rejected\n");
        return 1;
    }

    for (row = 0; row < rows; row++) {
        const uint8_t *row_start =
            padded_output + (size_t)row * TEST_PAD_PITCH_BYTES;
        uint16_t view_y = (uint16_t)(destination_y + row);
        uint16_t source_y = expected_source_coordinate(
            view_y,
            source_height,
            view_height);
        uint16_t x;

        for (x = 0; x < TEST_PAD_X * 2u; x++) {
            if (row_start[x] != 0xA5u ||
                row_start[
                    (TEST_PAD_X + view_width) * 2u + x] != 0xA5u) {
                fprintf(stderr, "scaler overwrote a padded row gutter\n");
                return 1;
            }
        }
        for (x = 0; x < view_width; x++) {
            uint16_t source_x = expected_source_coordinate(
                x,
                source_width,
                view_width);
            uint8_t index =
                source_pixels[
                    (uint32_t)source_y * source_width + source_x];
            const uint8_t *color =
                palette_rgba + (uint32_t)index * 4u;
            uint16_t expected =
                expected_rgb565(color[0], color[1], color[2]);
            const uint8_t *actual =
                row_start + (TEST_PAD_X + x) * 2u;

            if (actual[0] != (uint8_t)(expected >> 8) ||
                actual[1] != (uint8_t)expected) {
                fprintf(
                    stderr,
                    "padded pixel mismatch view=(%u,%u)\n",
                    (unsigned)x,
                    (unsigned)view_y);
                return 1;
            }
        }
    }
    if (padded_output[TEST_PAD_PITCH_BYTES * rows] != 0xA5u ||
        padded_output[TEST_PAD_PITCH_BYTES * rows + 1u] != 0xA5u) {
        fprintf(stderr, "padded scaler wrote past requested rows\n");
        return 1;
    }
    return 0;
}

int
main(void)
{
    const PalUiGeneratedSampling *sampling =
        &pal_ui_generated_sampling[PAL_UI_SAMPLE_STAGE];
    uint16_t source_width = CardputerExtreme_ScalerSourceWidth();
    uint16_t source_height = CardputerExtreme_ScalerSourceHeight();
    uint16_t view_width = CARDPUTER_EXTREME_SCALER_VIEW_WIDTH;
    uint16_t view_height = CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT;
    uint32_t y;
    uint32_t x;

    if (!CardputerExtreme_ScalerValidateGeneratedMap() ||
        source_width == 0 || source_height == 0 ||
        CardputerExtreme_ScalerSourcePixels() != PAL_EXTREME_SCREEN_BYTES ||
        sampling->destination_width != view_width ||
        sampling->destination_height != view_height ||
        sampling->scale_q16 !=
            ((uint64_t)sampling->numerator <<
                PAL_UI_GENERATED_FIXED_Q_SHIFT) /
                sampling->denominator ||
        sampling->step_x_q16 !=
            ((uint64_t)source_width <<
                PAL_UI_GENERATED_FIXED_Q_SHIFT) /
                view_width ||
        sampling->step_y_q16 !=
            ((uint64_t)source_height <<
                PAL_UI_GENERATED_FIXED_Q_SHIFT) /
                view_height ||
        sampling->phase_x_q16 != sampling->step_x_q16 / 2u ||
        sampling->phase_y_q16 != sampling->step_y_q16 / 2u) {
        fprintf(stderr, "generated stage sampler does not match fixture\n");
        return 1;
    }

    for (x = 0; x < view_width; x++) {
        uint16_t source_x;
        if (!CardputerExtreme_ScalerSourceX((uint16_t)x, &source_x) ||
            source_x != expected_source_coordinate(
                (uint16_t)x,
                source_width,
                view_width)) {
            fprintf(stderr, "generated X sample map mismatch at %u\n",
                    (unsigned)x);
            return 1;
        }
    }
    for (y = 0; y < view_height; y++) {
        uint16_t source_y;
        if (!CardputerExtreme_ScalerSourceY((uint16_t)y, &source_y) ||
            source_y != expected_source_coordinate(
                (uint16_t)y,
                source_height,
                view_height)) {
            fprintf(stderr, "generated Y sample map mismatch at %u\n",
                    (unsigned)y);
            return 1;
        }
    }
    {
        uint16_t coordinate;
        if (CardputerExtreme_ScalerSourceX(view_width, &coordinate) ||
            CardputerExtreme_ScalerSourceY(view_height, &coordinate) ||
            CardputerExtreme_ScalerSourceX(0, NULL) ||
            CardputerExtreme_ScalerSourceY(0, NULL)) {
            fprintf(stderr, "generated sample map bounds are not enforced\n");
            return 1;
        }
    }

    for (y = 0; y < source_height; y++) {
        for (x = 0; x < source_width; x++) {
            source_pixels[
                y * source_width + x] =
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
            view_height - TEST_ROWS,
            TEST_ROWS) != 0 ||
        verify_padded_strip(7, TEST_ROWS) != 0) {
        return 1;
    }

    if (expected_source_coordinate(
            view_width - 1u,
            source_width,
            view_width) != source_width - 1u ||
        expected_source_coordinate(
            view_height - 1u,
            source_height,
            view_height) != source_height - 1u) {
        fprintf(stderr, "center-sampled mapping does not reach source edge\n");
        return 1;
    }

    if (CardputerExtreme_ScaleIndexedStrip(
            source_pixels,
            source_width,
            palette_rgba,
            0,
            TEST_ROWS,
            output,
            (size_t)view_width * 2u,
            TEST_OUTPUT_BYTES - 1u) ||
        CardputerExtreme_ScaleIndexedStrip(
            source_pixels,
            (uint16_t)(source_width - 1u),
            palette_rgba,
            0,
            TEST_ROWS,
            output,
            (size_t)view_width * 2u,
            TEST_OUTPUT_BYTES) ||
        CardputerExtreme_ScaleIndexedStrip(
            source_pixels,
            source_width,
            palette_rgba,
            0,
            TEST_ROWS,
            output,
            (size_t)view_width * 2u - 1u,
            TEST_OUTPUT_BYTES) ||
        CardputerExtreme_ScaleIndexedStrip(
            source_pixels,
            source_width,
            palette_rgba,
            view_height,
            1,
            output,
            (size_t)view_width * 2u,
            TEST_OUTPUT_BYTES)) {
        fprintf(stderr, "invalid scaler call accepted\n");
        return 1;
    }

    printf(
        "cardputer extreme scaler: PASS source=%ux%u view=%ux%u ratio=%u/%u\n",
        source_width,
        source_height,
        view_width,
        view_height,
        sampling->numerator,
        sampling->denominator);
    return 0;
}
