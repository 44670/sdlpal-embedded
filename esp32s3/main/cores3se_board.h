#ifndef CORES3SE_BOARD_H
#define CORES3SE_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CORES3SE_LCD_WIDTH 320u
#define CORES3SE_LCD_HEIGHT 240u
#define CORES3SE_PAL_Y_OFFSET 20u

bool CoreS3Se_Begin(void);
bool CoreS3Se_MountTf(void);
void CoreS3Se_PrepareTfAccess(void);
void CoreS3Se_PrepareLcdAccess(void);
bool CoreS3Se_FlushPalFramebuffer(void);
bool CoreS3Se_FlushArgb8888Texture(const void *pixels, uint16_t width, uint16_t height, uint16_t pitch);
bool CoreS3Se_TouchPoint(uint16_t *x, uint16_t *y);
void CoreS3Se_ShowError(const char *line1, const char *line2);
#if defined(PAL_CORES3SE_NATIVE)
bool CoreS3Se_NativeInitialView(uint16_t *scene_num, int *viewport_x, int *viewport_y);
#endif

#ifdef __cplusplus
}
#endif

#endif
