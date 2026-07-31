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

bool CardputerExtreme_NativeViewSourceX(
    uint16_t destination_x,
    uint16_t *source_x)
{
    PalNativeUiViewport viewport;

    if (source_x == NULL ||
        destination_x >= PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH ||
        !PalNativeUi_GetViewport(&viewport)) {
        return false;
    }
    *source_x = (uint16_t)(viewport.source_x + destination_x);
    return *source_x < PAL_NATIVE_UI_GENERATED_LOGICAL_WIDTH;
}

bool CardputerExtreme_NativeViewSourceY(
    uint16_t destination_y,
    uint16_t *source_y)
{
    PalNativeUiViewport viewport;

    if (source_y == NULL ||
        destination_y >= PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT ||
        !PalNativeUi_GetViewport(&viewport)) {
        return false;
    }
    *source_y = (uint16_t)(viewport.source_y + destination_y);
    return *source_y < PAL_NATIVE_UI_GENERATED_LOGICAL_HEIGHT;
}

bool CardputerExtreme_NativeViewValidate(void)
{
    PalNativeUiViewport viewport;
    uint16_t x;
    uint16_t y;

    if (!PalNativeUi_GetViewport(&viewport) ||
        viewport.width != PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH ||
        viewport.height != PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT) {
        return false;
    }
    for (x = 0; x < viewport.width; x++) {
        uint16_t source;
        if (!CardputerExtreme_NativeViewSourceX(x, &source) ||
            source != viewport.source_x + x) {
            return false;
        }
    }
    for (y = 0; y < viewport.height; y++) {
        uint16_t source;
        if (!CardputerExtreme_NativeViewSourceY(y, &source) ||
            source != viewport.source_y + y) {
            return false;
        }
    }
    return true;
}

bool CardputerExtreme_CopyIndexedNativeStrip(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba,
    uint16_t destination_y,
    uint16_t rows,
    uint8_t *rgb565_be,
    size_t rgb565_pitch_bytes,
    size_t rgb565_capacity)
{
    PalNativeUiViewport viewport;
    size_t row_bytes =
        (size_t)PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH * 2u;
    size_t required;
    uint16_t row;

    if (pixels == NULL || palette_rgba == NULL || rgb565_be == NULL ||
        pitch < PAL_NATIVE_UI_GENERATED_LOGICAL_WIDTH || rows == 0u ||
        !PalNativeUi_GetViewport(&viewport) ||
        destination_y >= viewport.height ||
        rows > viewport.height - destination_y ||
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
        uint16_t source_y = (uint16_t)(
            viewport.source_y + destination_y + row);
        const uint8_t *source = pixels +
            (size_t)source_y * pitch + viewport.source_x;
        uint8_t *destination = rgb565_be +
            (size_t)row * rgb565_pitch_bytes;
        uint16_t x;

        for (x = 0; x < viewport.width; x++) {
            const uint8_t *color = palette_rgba +
                (size_t)source[x] * 4u;
            store_rgb565_be(
                destination + (size_t)x * 2u,
                color[0], color[1], color[2]);
        }
    }
    return true;
}
