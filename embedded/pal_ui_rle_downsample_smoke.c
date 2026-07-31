#include "pal_ui_rle_downsample.h"

#include "pal_ui_layout_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    MAX_SOURCE_PIXELS = 320 * 200,
    MAX_RLE_BYTES = MAX_SOURCE_PIXELS * 2 + 8,
    MAX_OUTPUT_BYTES = 160 * 1024,
};

typedef struct ParityCase {
    uint16_t source_width;
    uint16_t source_height;
    uint16_t destination_width;
    uint16_t destination_height;
    PalUiPhysicalRect destination;
    PalUiPhysicalRect strip;
    bool prefixed;
} ParityCase;

static uint8_t palette[256 * 4];
static uint8_t source_indices[MAX_SOURCE_PIXELS];
static uint8_t source_opaque[MAX_SOURCE_PIXELS];
static uint8_t encoded_rle[MAX_RLE_BYTES];
static uint8_t actual_output[MAX_OUTPUT_BYTES];
static uint8_t expected_output[MAX_OUTPUT_BYTES];

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xf8u) << 8) |
                      ((uint16_t)(g & 0xfcu) << 3) |
                      ((uint16_t)b >> 3));
}

static void store_rgb565(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static uint16_t exact_source_at(
    uint16_t destination,
    uint16_t source_extent,
    uint16_t destination_extent)
{
    uint64_t numerator =
        ((uint64_t)destination * 2u + 1u) * source_extent;
    return (uint16_t)(numerator / ((uint64_t)destination_extent * 2u));
}

static void make_source(uint16_t width, uint16_t height)
{
    uint16_t y;
    uint16_t x;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint32_t offset = (uint32_t)y * width + x;
            source_indices[offset] =
                (uint8_t)(x * 29u + y * 47u + (x * y) % 31u);
            source_opaque[offset] =
                (uint8_t)(((x + y * 3u) % 7u) != 0u &&
                          ((x ^ (y * 5u)) % 11u) != 0u);
        }
    }
}

static bool append_byte(size_t *size, uint8_t value)
{
    if (*size >= sizeof(encoded_rle)) {
        return false;
    }
    encoded_rle[(*size)++] = value;
    return true;
}

static bool encode_source(
    uint16_t width,
    uint16_t height,
    bool prefixed,
    size_t *encoded_size)
{
    size_t size = 0;
    uint16_t y;

    if (prefixed) {
        if (!append_byte(&size, 2) || !append_byte(&size, 0) ||
            !append_byte(&size, 0) || !append_byte(&size, 0)) {
            return false;
        }
    }
    if (!append_byte(&size, (uint8_t)width) ||
        !append_byte(&size, (uint8_t)(width >> 8)) ||
        !append_byte(&size, (uint8_t)height) ||
        !append_byte(&size, (uint8_t)(height >> 8))) {
        return false;
    }

    for (y = 0; y < height; y++) {
        uint16_t x = 0;
        while (x < width) {
            uint32_t first = (uint32_t)y * width + x;
            bool opaque = source_opaque[first] != 0;
            uint16_t count = 1;
            uint16_t index;

            while (x + count < width && count < 127u &&
                   (source_opaque[first + count] != 0) == opaque) {
                count++;
            }
            if (opaque) {
                if (!append_byte(&size, (uint8_t)count)) {
                    return false;
                }
                for (index = 0; index < count; index++) {
                    if (!append_byte(
                            &size,
                            source_indices[first + index])) {
                        return false;
                    }
                }
            } else if (!append_byte(
                           &size,
                           (uint8_t)(0x80u + count))) {
                return false;
            }
            x = (uint16_t)(x + count);
        }
    }
    *encoded_size = size;
    return true;
}

static bool reference_compose(
    const ParityCase *test,
    size_t pitch,
    size_t capacity)
{
    uint16_t destination_y;

    memset(expected_output, 0xa5, capacity);
    for (destination_y = 0;
         destination_y < test->destination_height;
         destination_y++) {
        int32_t physical_y =
            (int32_t)test->destination.y + destination_y;
        uint16_t source_y;
        uint16_t destination_x;

        if (physical_y < test->strip.y ||
            physical_y >= (int32_t)test->strip.y + test->strip.height) {
            continue;
        }
        source_y = exact_source_at(
            destination_y,
            test->source_height,
            test->destination_height);
        for (destination_x = 0;
             destination_x < test->destination_width;
             destination_x++) {
            int32_t physical_x =
                (int32_t)test->destination.x + destination_x;
            uint16_t source_x;
            uint32_t source_offset;
            uint8_t palette_index;
            const uint8_t *color;
            size_t output_offset;

            if (physical_x < test->strip.x ||
                physical_x >=
                    (int32_t)test->strip.x + test->strip.width) {
                continue;
            }
            source_x = exact_source_at(
                destination_x,
                test->source_width,
                test->destination_width);
            source_offset = (uint32_t)source_y *
                test->source_width + source_x;
            if (source_offset >= MAX_SOURCE_PIXELS) {
                return false;
            }
            if (source_opaque[source_offset] == 0) {
                continue;
            }
            palette_index = source_indices[source_offset];
            color = palette + (size_t)palette_index * 4u;
            output_offset =
                (size_t)(physical_y - test->strip.y) * pitch +
                (size_t)(physical_x - test->strip.x) * 2u;
            if (output_offset + 2u > capacity) {
                return false;
            }
            store_rgb565(
                expected_output + output_offset,
                rgb565(color[0], color[1], color[2]));
        }
    }
    return true;
}

static bool run_parity_case(const ParityCase *test, unsigned case_index)
{
    PalUiRleSampling sampling;
    size_t encoded_size;
    size_t pitch = (size_t)test->strip.width * 2u + 6u;
    size_t capacity =
        pitch * (test->strip.height - 1u) + test->strip.width * 2u;

    if ((uint32_t)test->source_width * test->source_height >
            MAX_SOURCE_PIXELS ||
        capacity > sizeof(actual_output)) {
        fprintf(stderr, "parity case %u exceeds smoke buffers\n", case_index);
        return false;
    }
    make_source(test->source_width, test->source_height);
    if (!encode_source(
            test->source_width,
            test->source_height,
            test->prefixed,
            &encoded_size)) {
        fprintf(stderr, "parity case %u RLE encode failed\n", case_index);
        return false;
    }
    sampling.source_width = test->source_width;
    sampling.source_height = test->source_height;
    sampling.destination_width = test->destination_width;
    sampling.destination_height = test->destination_height;
    sampling.fixed_q_shift = PAL_UI_LAYOUT_Q16_SHIFT;
    sampling.step_x_q16 =
        ((uint32_t)test->source_width << PAL_UI_LAYOUT_Q16_SHIFT) /
        test->destination_width;
    sampling.step_y_q16 =
        ((uint32_t)test->source_height << PAL_UI_LAYOUT_Q16_SHIFT) /
        test->destination_height;
    sampling.phase_x_q16 = sampling.step_x_q16 / 2u;
    sampling.phase_y_q16 = sampling.step_y_q16 / 2u;

    memset(actual_output, 0xa5, capacity);
    if (!reference_compose(test, pitch, capacity) ||
        !PalUiRle_ComposeRgb565Strip(
            encoded_rle,
            encoded_size,
            palette,
            &sampling,
            test->destination,
            test->strip,
            actual_output,
            pitch,
            capacity)) {
        fprintf(stderr, "parity case %u compose rejected\n", case_index);
        return false;
    }
    if (memcmp(actual_output, expected_output, capacity) != 0) {
        size_t offset;
        for (offset = 0; offset < capacity; offset++) {
            if (actual_output[offset] != expected_output[offset]) {
                fprintf(
                    stderr,
                    "parity case %u mismatch byte %zu: %02x != %02x\n",
                    case_index,
                    offset,
                    actual_output[offset],
                    expected_output[offset]);
                break;
            }
        }
        return false;
    }
    return true;
}

static bool test_generated_adapter(void)
{
    PalUiLayoutSampling layout;
    PalUiRleSampling sampling;

    memset(&layout, 0, sizeof(layout));
    layout.filter = PAL_UI_LAYOUT_FILTER_NEAREST_CENTER;
    layout.flags = PAL_UI_LAYOUT_SAMPLING_VISIBLE |
        PAL_UI_LAYOUT_SAMPLING_CATALOG;
    layout.source_width = 320;
    layout.source_height = 200;
    layout.destination_width = 216;
    layout.destination_height = 135;
    layout.fixed_q_shift = PAL_UI_LAYOUT_Q16_SHIFT;
    layout.step_x_q16 =
        (320u << PAL_UI_LAYOUT_Q16_SHIFT) / 216u;
    layout.step_y_q16 =
        (200u << PAL_UI_LAYOUT_Q16_SHIFT) / 135u;
    layout.phase_x_q16 = layout.step_x_q16 / 2u;
    layout.phase_y_q16 = layout.step_y_q16 / 2u;

    if (!PalUiRleSampling_FromLayout(&layout, &sampling) ||
        sampling.source_width != layout.source_width ||
        sampling.source_height != layout.source_height ||
        sampling.destination_width != layout.destination_width ||
        sampling.destination_height != layout.destination_height ||
        sampling.fixed_q_shift != layout.fixed_q_shift ||
        sampling.step_x_q16 != layout.step_x_q16 ||
        sampling.step_y_q16 != layout.step_y_q16 ||
        sampling.phase_x_q16 != layout.phase_x_q16 ||
        sampling.phase_y_q16 != layout.phase_y_q16) {
        fprintf(stderr, "generated sampling adapter mismatch\n");
        return false;
    }
    layout.filter = PAL_UI_LAYOUT_FILTER_BOX_2X2;
    if (PalUiRleSampling_FromLayout(&layout, &sampling)) {
        fprintf(stderr, "non-nearest generated transform accepted\n");
        return false;
    }
    layout.filter = PAL_UI_LAYOUT_FILTER_NEAREST_CENTER;
    layout.flags = 0;
    if (PalUiRleSampling_FromLayout(&layout, &sampling)) {
        fprintf(stderr, "invisible generated transform accepted\n");
        return false;
    }
    layout.flags = PAL_UI_LAYOUT_SAMPLING_VISIBLE;
    layout.fixed_q_shift = PAL_UI_LAYOUT_Q16_SHIFT - 1u;
    if (PalUiRleSampling_FromLayout(&layout, &sampling)) {
        fprintf(stderr, "non-Q16 generated transform accepted\n");
        return false;
    }
    return true;
}

static bool test_invalid_inputs(void)
{
    PalUiRleSampling sampling = {
        8, 6, 4, 3,
        PAL_UI_LAYOUT_Q16_SHIFT,
        2u << PAL_UI_LAYOUT_Q16_SHIFT,
        2u << PAL_UI_LAYOUT_Q16_SHIFT,
        1u << PAL_UI_LAYOUT_Q16_SHIFT,
        1u << PAL_UI_LAYOUT_Q16_SHIFT,
    };
    PalUiPhysicalRect destination = { 2, 0, 4, 3 };
    PalUiPhysicalRect strip = { 0, 0, 10, 3 };
    size_t encoded_size;

    make_source(8, 6);
    if (!encode_source(8, 6, false, &encoded_size)) {
        return false;
    }
    sampling.step_x_q16--;
    if (PalUiRle_ComposeRgb565Strip(
            encoded_rle, encoded_size, palette, &sampling,
            destination, strip, actual_output, 20, 60)) {
        fprintf(stderr, "invalid generated coefficients accepted\n");
        return false;
    }
    sampling.step_x_q16++;
    sampling.source_width = 3;
    if (PalUiRle_ComposeRgb565Strip(
            encoded_rle, encoded_size, palette, &sampling,
            destination, strip, actual_output, 20, 60)) {
        fprintf(stderr, "upsampling transform accepted\n");
        return false;
    }
    sampling.source_width = 8;
    if (PalUiRle_ComposeRgb565Strip(
            encoded_rle, encoded_size - 1u, palette, &sampling,
            destination, strip, actual_output, 20, 60)) {
        fprintf(stderr, "truncated RLE accepted\n");
        return false;
    }

    encoded_rle[4] = 0x80u;
    if (PalUiRle_ComposeRgb565Strip(
            encoded_rle, 5u, palette, &sampling,
            destination, strip, actual_output, 20, 60)) {
        fprintf(stderr, "zero-length transparent token accepted\n");
        return false;
    }
    if (!encode_source(8, 6, false, &encoded_size)) {
        return false;
    }

    /* A non-overlapping strip still performs complete RLE validation. */
    strip.x = 20;
    if (PalUiRle_ComposeRgb565Strip(
            encoded_rle, encoded_size - 1u, palette, &sampling,
            destination, strip, actual_output, 20, 60)) {
        fprintf(stderr, "clipped malformed RLE accepted\n");
        return false;
    }

    strip.x = 0;
    strip.height = 2;
    if (PalUiRle_ComposeRgb565Strip(
            encoded_rle, encoded_size, palette, &sampling,
            destination, strip, actual_output, SIZE_MAX, SIZE_MAX)) {
        fprintf(stderr, "overflowing output pitch accepted\n");
        return false;
    }
    return true;
}

int main(void)
{
    static const ParityCase cases[] = {
        { 8, 6, 4, 3, { 2, 0, 4, 3 }, { 0, 0, 10, 3 }, false },
        { 13, 11, 7, 5, { -2, 3, 7, 5 }, { -5, 1, 12, 7 }, true },
        { 320, 200, 216, 135, { 12, 0, 216, 135 },
          { 0, 0, 240, 17 }, false },
        { 320, 200, 216, 135, { 12, 0, 216, 135 },
          { 0, 64, 240, 19 }, true },
        { 135, 131, 72, 70, { 9, 37, 72, 70 },
          { 0, 30, 100, 44 }, true },
        { 48, 47, 48, 47, { 21, 49, 48, 47 },
          { 15, 45, 60, 40 }, false },
        { 320, 200, 160, 100, { -10, -5, 160, 100 },
          { 15, 10, 130, 80 }, true },
        { 9, 9, 1, 1, { 4, 4, 1, 1 }, { 0, 0, 8, 8 }, false },
        { 228, 179, 154, 121, { 3, 2, 154, 121 },
          { 41, 33, 89, 57 }, true },
    };
    unsigned index;

    for (index = 0; index < 256u; index++) {
        palette[index * 4u] = (uint8_t)(index * 3u);
        palette[index * 4u + 1u] = (uint8_t)(255u - index);
        palette[index * 4u + 2u] = (uint8_t)(index ^ 0x5au);
        palette[index * 4u + 3u] = 255u;
    }
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        if (!run_parity_case(&cases[index], index)) {
            return 1;
        }
    }
    if (!test_generated_adapter() || !test_invalid_inputs()) {
        return 1;
    }

    puts("pal UI RLE division-free streaming downsample: PASS");
    return 0;
}
