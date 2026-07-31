#ifndef CARDPUTER_EXTREME_SCALER_H
#define CARDPUTER_EXTREME_SCALER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Host-generated display, stage, and sampling coefficients. */
#include "generated/pal_ui_layout_240x135.h"

#if !defined(PAL_UI_GENERATED_COEFFICIENTS_ONLY) || \
    PAL_UI_GENERATED_COEFFICIENTS_ONLY != 1u
#error "Cardputer extreme scaler requires Python-generated coefficients"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The physical stage geometry is intentionally an alias of the generated
 * profile.  Do not add target-local dimensions or scale coefficients here.
 */
#define CARDPUTER_EXTREME_SCALER_VIEW_WIDTH PAL_UI_GENERATED_STAGE_WIDTH
#define CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT PAL_UI_GENERATED_STAGE_HEIGHT

static inline uint16_t
CardputerExtreme_ScalerSourceWidth(void)
{
    return PAL_UI_GENERATED_STAGE_SOURCE_WIDTH;
}

static inline uint16_t
CardputerExtreme_ScalerSourceHeight(void)
{
    return PAL_UI_GENERATED_STAGE_SOURCE_HEIGHT;
}

static inline uint32_t
CardputerExtreme_ScalerSourcePixels(void)
{
    return
        (uint32_t)PAL_UI_GENERATED_STAGE_SOURCE_WIDTH *
        PAL_UI_GENERATED_STAGE_SOURCE_HEIGHT;
}

/*
 * Read the exact Python-generated nearest-centre maps.  These accessors let
 * compatibility presentation paths reuse the same tables without deriving a
 * target-side scale or performing a pixel-loop divide.
 */
bool CardputerExtreme_ScalerSourceX(
    uint16_t destination_x,
    uint16_t *source_x);
bool CardputerExtreme_ScalerSourceY(
    uint16_t destination_y,
    uint16_t *source_y);
/* Boot-time exact-map validation; rendering itself only reads the table. */
bool CardputerExtreme_ScalerValidateGeneratedMap(void);

/*
 * Convert a contiguous set of generated-stage rows from the indexed logical
 * framebuffer into big-endian RGB565 bytes for the LCD view window.
 *
 * The mapping samples pixel centers using the generated stage sampler.  The
 * source/destination extents and audit Q16 values all come from the generated
 * header; palette_rgba contains 256 consecutive {r,g,b,a} entries.
 * rgb565_pitch_bytes permits the caller to place the view in a wider LCD row;
 * it must be at least destination_width * 2.  Capacity is measured from
 * rgb565_be and includes any padding between requested rows.
 */
bool CardputerExtreme_ScaleIndexedStrip(
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
