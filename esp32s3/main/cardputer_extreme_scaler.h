#ifndef CARDPUTER_EXTREME_SCALER_H
#define CARDPUTER_EXTREME_SCALER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH 320u
#define CARDPUTER_EXTREME_SCALER_SOURCE_HEIGHT 200u
#define CARDPUTER_EXTREME_SCALER_VIEW_WIDTH 216u
#define CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT 135u

/*
 * Convert a contiguous set of destination rows from the 320x200 indexed
 * logical framebuffer into big-endian RGB565 bytes for the LCD view window.
 *
 * The mapping samples pixel centers.  Both axes use the exact 27/40 scale
 * ratio, so the source 8:5 aspect ratio is preserved without a target-sized
 * framebuffer.  palette_rgba contains 256 consecutive {r,g,b,a} entries.
 */
bool CardputerExtreme_ScaleIndexedStrip(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba,
    uint16_t destination_y,
    uint16_t rows,
    uint8_t *rgb565_be,
    size_t rgb565_capacity);

#ifdef __cplusplus
}
#endif

#endif
