#ifndef PAL_VIDEO_STATIC_H
#define PAL_VIDEO_STATIC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_VIDEO_WIDTH 320u
#define PAL_VIDEO_HEIGHT 200u
#define PAL_VIDEO_FRAMEBUFFER_BYTES (PAL_VIDEO_WIDTH * PAL_VIDEO_HEIGHT)

void PalVideo_Clear(uint8_t color);
void PalVideo_SaveScreen(void);
void PalVideo_RestoreScreen(void);
bool PalVideo_SetPaletteRgb(uint16_t first_color, uint16_t color_count, const uint8_t *rgb);
bool PalVideo_ConvertLineRgb565(uint16_t y, const uint16_t **line, uint16_t *pixels);
uint16_t PalVideo_GetRgb565(uint8_t color);

#ifdef __cplusplus
}
#endif

#endif
