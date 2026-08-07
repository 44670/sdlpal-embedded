#ifndef XIAOMIAO_BOARD_H
#define XIAOMIAO_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOMIAO_LCD_WIDTH 160u
#define XIAOMIAO_LCD_HEIGHT 128u
#define XIAOMIAO_TF_MOUNT_POINT "/sdcard"

bool Xiaomiao_Begin(void);
bool Xiaomiao_MountTf(void);
bool Xiaomiao_TfMounted(void);
void Xiaomiao_PrepareTfAccess(void);
bool Xiaomiao_PollKey(uint8_t *ascii, bool *pressed);
bool Xiaomiao_FlushIndexedFramebuffer(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba);
bool Xiaomiao_FlushIndexedFramebufferRegion(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height);
void Xiaomiao_ShowError(const char *line1, const char *line2);
void Xiaomiao_GuruMeditation(
    const char *file,
    uint32_t line,
    const char *reason);

#ifdef __cplusplus
}
#endif

#endif
