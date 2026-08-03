#ifndef CARDPUTER_EXTREME_NATIVE_VIEW_H
#define CARDPUTER_EXTREME_NATIVE_VIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool CardputerExtreme_NativeViewValidate(
    uint16_t width,
    uint16_t height);

/* Convert an already-native indexed framebuffer to one RGB565 DMA strip. */
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
    size_t rgb565_capacity);

#ifdef __cplusplus
}
#endif

#endif
