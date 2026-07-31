#include "cardputer_extreme_scaler.h"

#define PAL_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

typedef char cardputer_extreme_stage_sample_x_count_matches[
    (PAL_ARRAY_COUNT(pal_ui_generated_stage_sample_x) ==
         PAL_UI_GENERATED_STAGE_SAMPLE_X_COUNT &&
     PAL_UI_GENERATED_STAGE_SAMPLE_X_COUNT ==
         PAL_UI_GENERATED_STAGE_DESTINATION_WIDTH) ? 1 : -1];
typedef char cardputer_extreme_stage_sample_y_count_matches[
    (PAL_ARRAY_COUNT(pal_ui_generated_stage_sample_y) ==
         PAL_UI_GENERATED_STAGE_SAMPLE_Y_COUNT &&
     PAL_UI_GENERATED_STAGE_SAMPLE_Y_COUNT ==
         PAL_UI_GENERATED_STAGE_DESTINATION_HEIGHT) ? 1 : -1];

bool
CardputerExtreme_ScalerSourceX(
    uint16_t destination_x,
    uint16_t *source_x)
{
    uint16_t value;

    if (source_x == NULL ||
        destination_x >= PAL_UI_GENERATED_STAGE_SAMPLE_X_COUNT) {
        return false;
    }
    value = pal_ui_generated_stage_sample_x[destination_x];
    if (value >= PAL_UI_GENERATED_STAGE_SOURCE_WIDTH) {
        return false;
    }
    *source_x = value;
    return true;
}

bool
CardputerExtreme_ScalerSourceY(
    uint16_t destination_y,
    uint16_t *source_y)
{
    uint16_t value;

    if (source_y == NULL ||
        destination_y >= PAL_UI_GENERATED_STAGE_SAMPLE_Y_COUNT) {
        return false;
    }
    value = pal_ui_generated_stage_sample_y[destination_y];
    if (value >= PAL_UI_GENERATED_STAGE_SOURCE_HEIGHT) {
        return false;
    }
    *source_y = value;
    return true;
}

static uint16_t
expected_nearest_center(
    uint16_t destination,
    uint16_t source_extent,
    uint16_t destination_extent)
{
    return (uint16_t)(
        ((uint64_t)(2u * destination + 1u) * source_extent) /
        ((uint64_t)2u * destination_extent));
}

bool
CardputerExtreme_ScalerValidateGeneratedMap(void)
{
    uint16_t index;

    for (index = 0;
         index < PAL_UI_GENERATED_STAGE_SAMPLE_X_COUNT;
         index++) {
        uint16_t source;
        if (!CardputerExtreme_ScalerSourceX(index, &source) ||
            source != expected_nearest_center(
                index,
                PAL_UI_GENERATED_STAGE_SOURCE_WIDTH,
                PAL_UI_GENERATED_STAGE_SAMPLE_X_COUNT)) {
            return false;
        }
    }
    for (index = 0;
         index < PAL_UI_GENERATED_STAGE_SAMPLE_Y_COUNT;
         index++) {
        uint16_t source;
        if (!CardputerExtreme_ScalerSourceY(index, &source) ||
            source != expected_nearest_center(
                index,
                PAL_UI_GENERATED_STAGE_SOURCE_HEIGHT,
                PAL_UI_GENERATED_STAGE_SAMPLE_Y_COUNT)) {
            return false;
        }
    }
    return true;
}

static void
store_rgb565_be(
    uint8_t *destination,
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    uint16_t color =
        (uint16_t)(((uint16_t)(r & 0xF8u) << 8) |
                   ((uint16_t)(g & 0xFCu) << 3) |
                   ((uint16_t)b >> 3));

    destination[0] = (uint8_t)(color >> 8);
    destination[1] = (uint8_t)color;
}

bool
CardputerExtreme_ScaleIndexedStrip(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba,
    uint16_t destination_y,
    uint16_t rows,
    uint8_t *rgb565_be,
    size_t rgb565_pitch_bytes,
    size_t rgb565_capacity)
{
    const uint16_t source_width =
        PAL_UI_GENERATED_STAGE_SOURCE_WIDTH;
    const uint16_t source_height =
        PAL_UI_GENERATED_STAGE_SOURCE_HEIGHT;
    const uint16_t destination_width =
        PAL_UI_GENERATED_STAGE_DESTINATION_WIDTH;
    const uint16_t destination_height =
        PAL_UI_GENERATED_STAGE_DESTINATION_HEIGHT;
    size_t required;
    uint16_t row;

    if (pixels == NULL || palette_rgba == NULL || rgb565_be == NULL ||
        source_width == 0 || source_height == 0 ||
        destination_width != CARDPUTER_EXTREME_SCALER_VIEW_WIDTH ||
        destination_height != CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT ||
        pitch < source_width ||
        rows == 0 ||
        destination_y >= destination_height ||
        rows > destination_height - destination_y) {
        return false;
    }

    if (rgb565_pitch_bytes < (size_t)destination_width * 2u) {
        return false;
    }
    required = (size_t)destination_width * 2u;
    if (rows > 1u) {
        if (rgb565_pitch_bytes >
            (SIZE_MAX - required) / (size_t)(rows - 1u)) {
            return false;
        }
        required += rgb565_pitch_bytes * (size_t)(rows - 1u);
    }
    if (rgb565_capacity < required) {
        return false;
    }

    for (row = 0; row < rows; row++) {
        uint16_t view_y = (uint16_t)(destination_y + row);
        uint16_t source_y;
        const uint8_t *source =
            pixels;
        uint8_t *destination =
            rgb565_be +
            (size_t)row * rgb565_pitch_bytes;
        uint16_t view_x;

        if (!CardputerExtreme_ScalerSourceY(view_y, &source_y)) {
            return false;
        }
        source += (size_t)source_y * pitch;
        for (view_x = 0;
             view_x < destination_width;
             view_x++) {
            uint16_t source_x;
            const uint8_t *color =
                palette_rgba;

            if (!CardputerExtreme_ScalerSourceX(view_x, &source_x)) {
                return false;
            }
            color += (size_t)source[source_x] * 4u;
            store_rgb565_be(
                destination + (size_t)view_x * 2u,
                color[0],
                color[1],
                color[2]);
        }
    }

    return true;
}
