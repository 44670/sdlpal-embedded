#ifndef PAL_GURU_SCREEN_H
#define PAL_GURU_SCREEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_GURU_TEXT_COLUMNS 27u

typedef struct PalGuruScreenText {
    char reason[PAL_GURU_TEXT_COLUMNS];
    char file[PAL_GURU_TEXT_COLUMNS];
    char line[PAL_GURU_TEXT_COLUMNS];
    char revision[PAL_GURU_TEXT_COLUMNS];
} PalGuruScreenText;

void PalGuruScreen_BuildText(
    PalGuruScreenText *text,
    const char *file,
    uint32_t line,
    const char *revision,
    const char *reason);

bool PalGuruScreen_RenderRgb565Strip(
    const PalGuruScreenText *text,
    uint16_t *pixels,
    size_t pixel_capacity,
    uint16_t width,
    uint16_t height,
    uint16_t first_y,
    uint16_t rows,
    uint16_t background,
    uint16_t title_color,
    uint16_t detail_color);

#ifdef __cplusplus
}
#endif

#endif
