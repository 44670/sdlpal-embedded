#include "cardputer_extreme_native_view.h"

static void store_rgb565_be(
    uint8_t *destination,
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    uint16_t color =
        (uint16_t)(((uint16_t)(r & 0xf8u) << 8) |
                   ((uint16_t)(g & 0xfcu) << 3) |
                   ((uint16_t)b >> 3));
    destination[0] = (uint8_t)(color >> 8);
    destination[1] = (uint8_t)color;
}

bool CardputerExtreme_NativeViewValidate(
    uint16_t width,
    uint16_t height)
{
    return width != 0u && height != 0u;
}

bool CardputerExtreme_CopyIndexedNativeStrip(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba,
    uint16_t width,
    uint16_t height,
    uint16_t destination_y,
    uint16_t rows,
    uint8_t *rgb565_be,
    size_t rgb565_pitch_bytes,
    size_t rgb565_capacity)
{
    size_t row_bytes = (size_t)width * 2u;
    size_t required;
    uint16_t row;

    if (pixels == NULL || palette_rgba == NULL || rgb565_be == NULL ||
        width == 0u || height == 0u || pitch < width || rows == 0u ||
        destination_y >= height || rows > height - destination_y ||
        rgb565_pitch_bytes < row_bytes) {
        return false;
    }
    required = row_bytes;
    if (rows > 1u) {
        if (rgb565_pitch_bytes >
            (SIZE_MAX - row_bytes) / (size_t)(rows - 1u)) {
            return false;
        }
        required += rgb565_pitch_bytes * (size_t)(rows - 1u);
    }
    if (required > rgb565_capacity) {
        return false;
    }

    for (row = 0; row < rows; row++) {
        const uint8_t *source = pixels +
            (size_t)(destination_y + row) * pitch;
        uint8_t *destination = rgb565_be +
            (size_t)row * rgb565_pitch_bytes;
        uint16_t destination_x;

        for (destination_x = 0;
             destination_x < width;
             destination_x++) {
            const uint8_t *color = palette_rgba +
                (size_t)source[destination_x] * 4u;
            store_rgb565_be(
                destination + (size_t)destination_x * 2u,
                color[0], color[1], color[2]);
        }
    }
    return true;
}
