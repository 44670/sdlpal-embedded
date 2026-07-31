#ifndef CARDPUTER_EXTREME_NATIVE_VIEW_H
#define CARDPUTER_EXTREME_NATIVE_VIEW_H

#include "pal_native_ui.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARDPUTER_EXTREME_VIEW_WIDTH \
    PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH
#define CARDPUTER_EXTREME_VIEW_HEIGHT \
    PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT

bool CardputerExtreme_NativeViewSourceX(
    uint16_t destination_x,
    uint16_t *source_x);
bool CardputerExtreme_NativeViewSourceY(
    uint16_t destination_y,
    uint16_t *source_y);
bool CardputerExtreme_NativeViewValidate(void);

/* Convert a 1:1 indexed crop to big-endian RGB565.  There is deliberately no
 * sample table, scale factor, interpolation, or whole-frame resize here. */
bool CardputerExtreme_CopyIndexedNativeStrip(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba,
    uint16_t destination_y,
    uint16_t rows,
    uint8_t *rgb565_be,
    size_t rgb565_pitch_bytes,
    size_t rgb565_capacity);

#ifdef __cplusplus
}
#endif

#endif
