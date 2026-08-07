#include "cardputer_extreme_native_view.h"

static uint8_t native_palette_rgb565_be[256u * 2u];
static const uint8_t *native_palette_source;
static bool native_palette_valid;

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

void CardputerExtreme_NativeViewInvalidatePalette(void)
{
    native_palette_valid = false;
    native_palette_source = NULL;
}

static void prepare_native_palette(const uint8_t *palette_rgba)
{
    uint16_t index;

    if (native_palette_valid && native_palette_source == palette_rgba) {
        return;
    }
    for (index = 0u; index < 256u; index++) {
        const uint8_t *color = palette_rgba + (size_t)index * 4u;
        store_rgb565_be(native_palette_rgb565_be + (size_t)index * 2u,
            color[0], color[1], color[2]);
    }
    native_palette_source = palette_rgba;
    native_palette_valid = true;
}

bool CardputerExtreme_NativeViewValidate(
    uint16_t width,
    uint16_t height)
{
    return width != 0u && height != 0u;
}

bool CardputerExtreme_CopyIndexedNativeRegion(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba,
    uint16_t screen_width,
    uint16_t screen_height,
    uint16_t source_x,
    uint16_t destination_y,
    uint16_t width,
    uint16_t rows,
    uint8_t *rgb565_be,
    size_t rgb565_pitch_bytes,
    size_t rgb565_capacity)
{
    size_t row_bytes = (size_t)width * 2u;
    size_t required;
    uint16_t row;

    if (pixels == NULL || palette_rgba == NULL || rgb565_be == NULL ||
        screen_width == 0u || screen_height == 0u || width == 0u ||
        rows == 0u || pitch < screen_width || source_x >= screen_width ||
        width > screen_width - source_x || destination_y >= screen_height ||
        rows > screen_height - destination_y ||
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

    prepare_native_palette(palette_rgba);

    for (row = 0; row < rows; row++) {
        const uint8_t *source = pixels +
            (size_t)(destination_y + row) * pitch;
        uint8_t *destination = rgb565_be +
            (size_t)row * rgb565_pitch_bytes;
        uint16_t destination_x;

        for (destination_x = 0;
             destination_x < width;
             destination_x++) {
            const uint8_t *color = native_palette_rgb565_be +
                (size_t)source[source_x + destination_x] * 2u;
            uint8_t *pixel = destination + (size_t)destination_x * 2u;
            pixel[0] = color[0];
            pixel[1] = color[1];
        }
    }
    return true;
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
    return CardputerExtreme_CopyIndexedNativeRegion(
        pixels, pitch, palette_rgba, width, height, 0u, destination_y,
        width, rows, rgb565_be, rgb565_pitch_bytes, rgb565_capacity);
}
