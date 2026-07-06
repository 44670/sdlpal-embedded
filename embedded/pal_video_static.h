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
void PalVideo_SaveBigBuffer(void);
void PalVideo_RestoreBigBuffer(void);
bool PalVideo_SaveRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
bool PalVideo_RestoreRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
bool PalVideo_SetPaletteRgb(uint16_t first_color, uint16_t color_count, const uint8_t *rgb);
bool PalVideo_ConvertLineRgb565(uint16_t y, const uint16_t **line, uint16_t *pixels);
bool PalVideo_ConvertLinesRgb565(uint16_t first_y, uint16_t line_count, const uint16_t **lines, uint16_t *pixels, uint16_t *converted_lines);
uint16_t PalVideo_GetRgb565(uint8_t color);

#ifdef __cplusplus
}
#endif

#endif
