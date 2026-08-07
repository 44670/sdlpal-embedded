#ifndef PAL_NDS_TARGET_BOARD_H
#define PAL_NDS_TARGET_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAL_TARGET_LCD_WIDTH 256u
#define PAL_TARGET_LCD_HEIGHT 192u

#ifdef __cplusplus
extern "C" {
#endif

bool NdsTarget_Begin(void);
void NdsTarget_ShowError(const char *title, const char *detail);
void NdsTarget_ShowReady(
   bool save_available, int save_type, uint32_t save_bytes);
void NdsTarget_FatalAt(
   const char *file,
   uint32_t line,
   const char *reason) __attribute__((noreturn));
bool NdsTarget_FlushIndexedFramebuffer(
   const uint8_t *pixels,
   uint16_t pitch,
   const uint8_t *palette_rgba);

void NdsTarget_AudioPump(void);

#define PalTarget_Begin NdsTarget_Begin
#define PalTarget_ShowError NdsTarget_ShowError
#define PalTarget_FlushIndexedFramebuffer NdsTarget_FlushIndexedFramebuffer

#ifdef __cplusplus
}
#endif

#endif
