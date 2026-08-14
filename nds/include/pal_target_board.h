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

struct tagEVENTOBJECT;

bool NdsTarget_Begin(void);
void NdsTarget_BootLog(const char *line);
void NdsTarget_ShowError(const char *title, const char *detail);
void NdsTarget_ShowReady(
   bool save_available, int save_type, uint32_t save_bytes);
void NdsTarget_MinimapSetMap(
   int map_number,
   const uint32_t *map_tiles,
   const struct tagEVENTOBJECT *event_objects,
   unsigned event_object_count,
   int world_x,
   int world_y);
void NdsTarget_FatalAt(
   const char *file,
   uint32_t line,
   const char *reason) __attribute__((noreturn));
bool NdsTarget_FlushIndexedFramebuffer(
   const uint8_t *pixels,
   uint16_t pitch,
   const uint8_t *palette_rgba);

typedef void (*NdsTargetAudioRender)(
   void *user, int16_t *samples, size_t sample_count);

bool NdsTarget_AudioStart(NdsTargetAudioRender render, void *user);
void NdsTarget_AudioStop(void);
void NdsTarget_AudioPump(void);
uint32_t NdsTarget_AudioDeadlineMisses(void);

#define PalTarget_Begin NdsTarget_Begin
#define PalTarget_ShowError NdsTarget_ShowError
#define PalTarget_FlushIndexedFramebuffer NdsTarget_FlushIndexedFramebuffer

#ifdef __cplusplus
}
#endif

#endif
