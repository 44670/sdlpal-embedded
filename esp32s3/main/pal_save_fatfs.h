#ifndef PAL_SAVE_FATFS_H
#define PAL_SAVE_FATFS_H

#include <stdbool.h>
#include <stdint.h>

bool PalSaveFatFs_ReadHeader(const char *path, uint16_t *saved_times);

#endif
