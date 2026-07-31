#ifndef PAL_UI_RLE_DOWNSAMPLE_H
#define PAL_UI_RLE_DOWNSAMPLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * All fields are generated on the host.  The target does not choose a scale
 * or solve a layout.  Source/destination extents are the exact rational
 * nearest-pixel-centre coefficients:
 *
 *   source = ((2 * destination + 1) * source_extent)
 *            / (2 * destination_extent)
 *
 * step/phase are emitted alongside them for camera/header audits.  The raster
 * path validates those derived values but uses the exact rational formula, so
 * 320->216 does not accumulate Q16 truncation at periodic pixels.
 *
 * The source RLE and palette remain read-only; rendering writes directly into
 * an RGB565 DMA strip and needs no decoded sprite-sized scratch buffer.
 */
typedef struct PalUiRleSampling {
    uint16_t source_width;
    uint16_t source_height;
    uint16_t destination_width;
    uint16_t destination_height;
    uint8_t fixed_q_shift;
    uint32_t step_x_q16;
    uint32_t step_y_q16;
    uint32_t phase_x_q16;
    uint32_t phase_y_q16;
} PalUiRleSampling;

struct PalUiLayoutSampling;

/*
 * Copy a visible, nearest-centre transform emitted by the Python layout
 * compiler into the narrow streaming-raster shape.  Target call sites should
 * use this adapter instead of deriving any scale or extent locally.
 */
bool PalUiRleSampling_FromLayout(
    const struct PalUiLayoutSampling *layout,
    PalUiRleSampling *out);

typedef struct PalUiPhysicalRect {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} PalUiPhysicalRect;

/*
 * Composite one PAL RLE sprite over an existing big-endian RGB565 strip.
 *
 * destination is in physical display coordinates.  strip is the physical
 * rectangle represented by rgb565_be; rgb565_pitch_bytes may be wider than
 * strip.width * 2.  Transparent RLE skips leave existing strip pixels intact.
 * Only generated downscale/equal-size transforms are accepted.
 */
bool PalUiRle_ComposeRgb565Strip(
    const uint8_t *rle,
    size_t rle_bytes,
    const uint8_t *palette_rgba,
    const PalUiRleSampling *sampling,
    PalUiPhysicalRect destination,
    PalUiPhysicalRect strip,
    uint8_t *rgb565_be,
    size_t rgb565_pitch_bytes,
    size_t rgb565_capacity);

#ifdef __cplusplus
}
#endif

#endif
