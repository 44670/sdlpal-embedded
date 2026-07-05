#ifndef PAL_SAVE_CACHE_H
#define PAL_SAVE_CACHE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PalSaveSlot {
    uint8_t *data;
    uint32_t size;
    uint16_t saved_times;
    uint16_t viewport_x;
    uint16_t viewport_y;
    uint16_t party_members;
    uint16_t scene_num;
    uint16_t music_num;
    uint16_t battle_music_num;
    uint16_t battle_field_num;
    uint32_t cash;
    uint32_t checksum;
} PalSaveSlot;

bool PalSave_ReadFile(const char *path, PalSaveSlot *slot);
bool PalSave_WriteFile(const char *path, const uint8_t *data, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
