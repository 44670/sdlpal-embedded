#include "pal_ui_rle_downsample.h"

#include "pal_ui_layout_runtime.h"

#include <limits.h>

typedef struct PalUiRleAxisCursor {
    uint32_t source;
    uint32_t remainder;
    uint32_t denominator;
    uint32_t whole_step;
    uint32_t remainder_step_twice;
} PalUiRleAxisCursor;

typedef struct PalUiRleSampleCursor {
    PalUiRleAxisCursor x;
    PalUiRleAxisCursor y;
    uint32_t x_begin_source;
    uint32_t x_begin_remainder;
    uint32_t source_width;
    uint16_t destination_x;
    uint16_t destination_y;
    uint16_t destination_left;
    uint16_t destination_right;
    uint16_t destination_bottom;
    bool active;
} PalUiRleSampleCursor;

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static bool checked_area(uint16_t width, uint16_t height, uint32_t *area)
{
    uint32_t value = (uint32_t)width * height;
    if (width == 0 || height == 0) {
        return false;
    }
    *area = value;
    return true;
}

/*
 * Validate floor((source << q) / destination) without doing a target-side
 * divide.  Both q and the fixed-point value are emitted by the Python layout
 * compiler.
 */
static bool step_matches_extents(
    uint16_t source,
    uint16_t destination,
    uint8_t fixed_q_shift,
    uint32_t step_q16)
{
    uint64_t scaled_source =
        (uint64_t)source << fixed_q_shift;
    uint64_t lower = (uint64_t)step_q16 * destination;
    uint64_t upper = (uint64_t)(step_q16 + 1u) * destination;

    return lower <= scaled_source && scaled_source < upper;
}

static bool sampling_is_valid(const PalUiRleSampling *sampling)
{
    if (sampling == NULL ||
        sampling->source_width == 0 ||
        sampling->source_height == 0 ||
        sampling->destination_width == 0 ||
        sampling->destination_height == 0 ||
        sampling->source_width < sampling->destination_width ||
        sampling->source_height < sampling->destination_height ||
        sampling->fixed_q_shift != PAL_UI_LAYOUT_Q16_SHIFT ||
        sampling->step_x_q16 == 0 ||
        sampling->step_y_q16 == 0) {
        return false;
    }

    return step_matches_extents(
               sampling->source_width,
               sampling->destination_width,
               sampling->fixed_q_shift,
               sampling->step_x_q16) &&
           step_matches_extents(
               sampling->source_height,
               sampling->destination_height,
               sampling->fixed_q_shift,
               sampling->step_y_q16) &&
           sampling->phase_x_q16 == sampling->step_x_q16 / 2u &&
           sampling->phase_y_q16 == sampling->step_y_q16 / 2u;
}

bool PalUiRleSampling_FromLayout(
    const PalUiLayoutSampling *layout,
    PalUiRleSampling *out)
{
    if (layout == NULL || out == NULL ||
        layout->filter != PAL_UI_LAYOUT_FILTER_NEAREST_CENTER ||
        (layout->flags & PAL_UI_LAYOUT_SAMPLING_VISIBLE) == 0) {
        return false;
    }

    out->source_width = layout->source_width;
    out->source_height = layout->source_height;
    out->destination_width = layout->destination_width;
    out->destination_height = layout->destination_height;
    out->fixed_q_shift = layout->fixed_q_shift;
    out->step_x_q16 = layout->step_x_q16;
    out->step_y_q16 = layout->step_y_q16;
    out->phase_x_q16 = layout->phase_x_q16;
    out->phase_y_q16 = layout->phase_y_q16;
    return sampling_is_valid(out);
}

static void store_rgb565_be(
    uint8_t *destination,
    const uint8_t *rgba)
{
    uint16_t color =
        (uint16_t)(((uint16_t)(rgba[0] & 0xf8u) << 8) |
                   ((uint16_t)(rgba[1] & 0xfcu) << 3) |
                   ((uint16_t)rgba[2] >> 3));
    destination[0] = (uint8_t)(color >> 8);
    destination[1] = (uint8_t)color;
}

static bool rect_is_valid(PalUiPhysicalRect rect)
{
    int32_t right = (int32_t)rect.x + rect.width;
    int32_t bottom = (int32_t)rect.y + rect.height;
    return rect.width != 0 && rect.height != 0 &&
           right >= INT16_MIN && right <= INT16_MAX + 1 &&
           bottom >= INT16_MIN && bottom <= INT16_MAX + 1;
}

/*
 * Pixel-centre nearest sampling is
 *
 *   floor(((2 * destination + 1) * source_extent) /
 *         (2 * destination_extent)).
 *
 * The emitted Q16 step supplies floor(source_extent / destination_extent).
 * Splitting source_extent into that whole step and a remainder gives an exact
 * Bresenham-style cursor.  Initialising and advancing it needs no division,
 * and therefore neither the RLE source pixels nor destination pixels execute
 * a divide.  This remains exact for ratios such as 320 -> 216 where repeatedly
 * adding the truncated Q16 value would drift.
 */
static void axis_advance(PalUiRleAxisCursor *axis)
{
    axis->source += axis->whole_step;
    axis->remainder += axis->remainder_step_twice;
    if (axis->remainder >= axis->denominator) {
        axis->remainder -= axis->denominator;
        axis->source++;
    }
}

static bool axis_init(
    PalUiRleAxisCursor *axis,
    uint16_t source_extent,
    uint16_t destination_extent,
    uint8_t fixed_q_shift,
    uint32_t step_q16,
    uint16_t destination_index)
{
    uint32_t remainder_step;
    uint16_t index;

    axis->denominator = (uint32_t)destination_extent * 2u;
    axis->whole_step = step_q16 >> fixed_q_shift;
    if (axis->whole_step == 0 ||
        axis->whole_step * destination_extent > source_extent) {
        return false;
    }
    remainder_step = source_extent -
        axis->whole_step * destination_extent;
    if (remainder_step >= destination_extent) {
        return false;
    }
    axis->remainder_step_twice = remainder_step * 2u;

    /* floor(source_extent / (2 * destination_extent)). */
    axis->source = axis->whole_step >> 1;
    axis->remainder = source_extent -
        axis->source * axis->denominator;
    if (axis->remainder >= axis->denominator) {
        return false;
    }

    for (index = 0; index < destination_index; index++) {
        axis_advance(axis);
    }
    return axis->source < source_extent;
}

static bool sample_cursor_init(
    PalUiRleSampleCursor *cursor,
    const PalUiRleSampling *sampling,
    uint16_t left,
    uint16_t top,
    uint16_t right,
    uint16_t bottom)
{
    cursor->active = left < right && top < bottom;
    if (!cursor->active) {
        return true;
    }
    if (!axis_init(
            &cursor->x,
            sampling->source_width,
            sampling->destination_width,
            sampling->fixed_q_shift,
            sampling->step_x_q16,
            left) ||
        !axis_init(
            &cursor->y,
            sampling->source_height,
            sampling->destination_height,
            sampling->fixed_q_shift,
            sampling->step_y_q16,
            top)) {
        return false;
    }
    cursor->x_begin_source = cursor->x.source;
    cursor->x_begin_remainder = cursor->x.remainder;
    cursor->source_width = sampling->source_width;
    cursor->destination_x = left;
    cursor->destination_y = top;
    cursor->destination_left = left;
    cursor->destination_right = right;
    cursor->destination_bottom = bottom;
    return true;
}

static uint32_t sample_cursor_source_index(
    const PalUiRleSampleCursor *cursor)
{
    return cursor->y.source * cursor->source_width + cursor->x.source;
}

static bool sample_cursor_advance(PalUiRleSampleCursor *cursor)
{
    uint32_t previous_source_index = sample_cursor_source_index(cursor);

    cursor->destination_x++;
    if (cursor->destination_x < cursor->destination_right) {
        axis_advance(&cursor->x);
    } else {
        cursor->destination_y++;
        if (cursor->destination_y >= cursor->destination_bottom) {
            cursor->active = false;
            return true;
        }
        axis_advance(&cursor->y);
        cursor->x.source = cursor->x_begin_source;
        cursor->x.remainder = cursor->x_begin_remainder;
        cursor->destination_x = cursor->destination_left;
    }

    /* Downscale-only centre samples must be strictly row-major. */
    return sample_cursor_source_index(cursor) > previous_source_index;
}

static bool compose_samples_in_span(
    PalUiRleSampleCursor *samples,
    uint32_t source_first,
    uint32_t source_count,
    const uint8_t *literal,
    const uint8_t *palette_rgba,
    PalUiPhysicalRect destination,
    PalUiPhysicalRect strip,
    uint8_t *rgb565_be,
    size_t rgb565_pitch_bytes)
{
    uint32_t source_end = source_first + source_count;

    while (samples->active) {
        uint32_t sample_index = sample_cursor_source_index(samples);

        if (sample_index >= source_end) {
            break;
        }
        if (sample_index < source_first) {
            return false;
        }
        if (literal != NULL) {
            int32_t physical_x =
                (int32_t)destination.x + samples->destination_x;
            int32_t physical_y =
                (int32_t)destination.y + samples->destination_y;
            size_t output_offset =
                (size_t)(physical_y - strip.y) * rgb565_pitch_bytes +
                (size_t)(physical_x - strip.x) * 2u;
            uint8_t palette_index = literal[sample_index - source_first];

            store_rgb565_be(
                rgb565_be + output_offset,
                palette_rgba + (size_t)palette_index * 4u);
        }
        if (!sample_cursor_advance(samples)) {
            return false;
        }
    }
    return true;
}

static bool required_capacity(
    PalUiPhysicalRect strip,
    size_t pitch,
    size_t *required)
{
    size_t row_bytes = (size_t)strip.width * 2u;
    size_t preceding_rows;

    if (pitch < row_bytes) {
        return false;
    }
    if (strip.height <= 1u) {
        *required = row_bytes;
        return true;
    }
    if (__builtin_mul_overflow(
            pitch,
            (size_t)(strip.height - 1u),
            &preceding_rows) ||
        preceding_rows > SIZE_MAX - row_bytes) {
        return false;
    }
    *required = preceding_rows + row_bytes;
    return true;
}

bool PalUiRle_ComposeRgb565Strip(
    const uint8_t *rle,
    size_t rle_bytes,
    const uint8_t *palette_rgba,
    const PalUiRleSampling *sampling,
    PalUiPhysicalRect destination,
    PalUiPhysicalRect strip,
    uint8_t *rgb565_be,
    size_t rgb565_pitch_bytes,
    size_t rgb565_capacity)
{
    const uint8_t *cursor;
    const uint8_t *end;
    PalUiRleSampleCursor samples;
    uint32_t source_area;
    uint32_t source_index = 0;
    int32_t clip_left;
    int32_t clip_top;
    int32_t clip_right;
    int32_t clip_bottom;
    size_t required_bytes;
    size_t header_offset = 0;

    if (rle == NULL || palette_rgba == NULL || rgb565_be == NULL ||
        !sampling_is_valid(sampling) ||
        !rect_is_valid(destination) || !rect_is_valid(strip) ||
        destination.width != sampling->destination_width ||
        destination.height != sampling->destination_height ||
        !required_capacity(strip, rgb565_pitch_bytes, &required_bytes) ||
        rgb565_capacity < required_bytes) {
        return false;
    }
    if (!checked_area(
            sampling->source_width,
            sampling->source_height,
            &source_area)) {
        return false;
    }
    if (rle_bytes >= 4u &&
        rle[0] == 0x02u && rle[1] == 0 &&
        rle[2] == 0 && rle[3] == 0) {
        header_offset = 4u;
    }
    if (rle_bytes - header_offset < 4u) {
        return false;
    }
    if (read_le16(rle + header_offset) != sampling->source_width ||
        read_le16(rle + header_offset + 2u) != sampling->source_height) {
        return false;
    }

    clip_left = destination.x > strip.x ? destination.x : strip.x;
    clip_top = destination.y > strip.y ? destination.y : strip.y;
    clip_right = (int32_t)destination.x + destination.width;
    if (clip_right > (int32_t)strip.x + strip.width) {
        clip_right = (int32_t)strip.x + strip.width;
    }
    clip_bottom = (int32_t)destination.y + destination.height;
    if (clip_bottom > (int32_t)strip.y + strip.height) {
        clip_bottom = (int32_t)strip.y + strip.height;
    }
    if (clip_left >= clip_right || clip_top >= clip_bottom) {
        if (!sample_cursor_init(&samples, sampling, 0, 0, 0, 0)) {
            return false;
        }
    } else if (!sample_cursor_init(
                   &samples,
                   sampling,
                   (uint16_t)(clip_left - destination.x),
                   (uint16_t)(clip_top - destination.y),
                   (uint16_t)(clip_right - destination.x),
                   (uint16_t)(clip_bottom - destination.y))) {
        return false;
    }

    cursor = rle + header_offset + 4u;
    end = rle + rle_bytes;
    while (source_index < source_area) {
        uint8_t token;
        uint32_t count;
        bool transparent;

        if (cursor >= end) {
            return false;
        }
        token = *cursor++;
        transparent = (token & 0x80u) != 0 &&
            token <= (uint32_t)0x80u + sampling->source_width;
        if (transparent) {
            count = (uint32_t)token - 0x80u;
        } else {
            count = token;
        }
        if (count == 0 ||
            count > source_area - source_index ||
            (!transparent && (size_t)(end - cursor) < count)) {
            return false;
        }
        if (!compose_samples_in_span(
                &samples,
                source_index,
                count,
                transparent ? NULL : cursor,
                palette_rgba,
                destination,
                strip,
                rgb565_be,
                rgb565_pitch_bytes)) {
            return false;
        }
        if (!transparent) {
            cursor += count;
        }
        source_index += count;
    }
    return !samples.active;
}
