#ifndef PAL_SAVE_FATFS_H
#define PAL_SAVE_FATFS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct PalFatFsSaveSlot {
    uint8_t *data;
    uint32_t size;
    uint16_t saved_times;
    uint16_t viewport_x;
    uint16_t viewport_y;
    uint16_t scene_num;
    uint32_t cash;
} PalFatFsSaveSlot;

bool PalSaveFatFs_ReadHeader(const char *path, uint16_t *saved_times);
bool PalSaveFatFs_ReadFile(const char *path, PalFatFsSaveSlot *slot);
bool PalSaveFatFs_WriteFile(const char *path, const uint8_t *data, uint32_t size);

#endif
