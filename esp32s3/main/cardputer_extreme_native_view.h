#ifndef CARDPUTER_EXTREME_NATIVE_VIEW_H
#define CARDPUTER_EXTREME_NATIVE_VIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef PAL_NATIVE_UI_GENERATED_HEADER
#define PAL_NATIVE_UI_GENERATED_HEADER "generated/pal_native_ui_240x135.h"
#endif
#include PAL_NATIVE_UI_GENERATED_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#define CARDPUTER_EXTREME_VIEW_WIDTH \
    PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH
#define CARDPUTER_EXTREME_VIEW_HEIGHT \
    PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT

bool CardputerExtreme_NativeViewValidate(void);

/* Convert an already-native indexed framebuffer to one RGB565 DMA strip. */
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
