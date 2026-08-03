#ifndef XIAOMIAO_BOARD_H
#define XIAOMIAO_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#ifndef PAL_NATIVE_UI_GENERATED_HEADER
#define PAL_NATIVE_UI_GENERATED_HEADER "generated/pal_native_ui_160x128.h"
#endif
#include PAL_NATIVE_UI_GENERATED_HEADER

#if !defined(PAL_NATIVE_UI_SCHEMA_VERSION) || PAL_NATIVE_UI_SCHEMA_VERSION != 1u
#error "Xiaomiao requires the generated native PAL UI contract"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOMIAO_LCD_WIDTH PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH
#define XIAOMIAO_LCD_HEIGHT PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT
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
void Xiaomiao_ShowError(const char *line1, const char *line2);

#ifdef __cplusplus
}
#endif

#endif
