#include "cardputer_extreme_scaler.h"

static uint16_t
scaled_source_coordinate(
    uint16_t destination,
    uint16_t source_size,
    uint16_t destination_size)
{
    uint32_t coordinate =
        ((uint32_t)(destination * 2u + 1u) * source_size) /
        ((uint32_t)destination_size * 2u);

    if (coordinate >= source_size) {
        coordinate = source_size - 1u;
    }
    return (uint16_t)coordinate;
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
    size_t rgb565_capacity)
{
    size_t required;
    uint16_t row;

    if (pixels == NULL || palette_rgba == NULL || rgb565_be == NULL ||
        pitch < CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH ||
        rows == 0 ||
        destination_y >= CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT ||
        rows > CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT - destination_y) {
        return false;
    }

    required =
        (size_t)CARDPUTER_EXTREME_SCALER_VIEW_WIDTH * rows * 2u;
    if (rgb565_capacity < required) {
        return false;
    }

    for (row = 0; row < rows; row++) {
        uint16_t view_y = (uint16_t)(destination_y + row);
        uint16_t source_y = scaled_source_coordinate(
            view_y,
            CARDPUTER_EXTREME_SCALER_SOURCE_HEIGHT,
            CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT);
        const uint8_t *source =
            pixels + (size_t)source_y * pitch;
        uint8_t *destination =
            rgb565_be +
            (size_t)row * CARDPUTER_EXTREME_SCALER_VIEW_WIDTH * 2u;
        uint16_t view_x;

        for (view_x = 0;
             view_x < CARDPUTER_EXTREME_SCALER_VIEW_WIDTH;
             view_x++) {
            uint16_t source_x = scaled_source_coordinate(
                view_x,
                CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH,
                CARDPUTER_EXTREME_SCALER_VIEW_WIDTH);
            const uint8_t *color =
                palette_rgba + (size_t)source[source_x] * 4u;

            store_rgb565_be(
                destination + (size_t)view_x * 2u,
                color[0],
                color[1],
                color[2]);
        }
    }

    return true;
}
