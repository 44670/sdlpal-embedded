#ifndef PAL_TEXT_CACHE_H
#define PAL_TEXT_CACHE_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_TEXT_MAGIC 0x54585450u
#define PAL_TEXT_VERSION 1u
#define PAL_TEXT_HEADER_SIZE 32u

typedef struct PalTextCache {
    const uint8_t *base;
    uint32_t size;
    uint16_t word_count;
    uint16_t message_count;
    uint32_t word_table_offset;
    uint32_t message_table_offset;
    uint32_t text_offset;
    uint32_t text_size;
} PalTextCache;

bool PalText_Open(const PalPack *nor_pack, PalTextCache *cache);
bool PalText_GetWord(const PalTextCache *cache, uint16_t index, const uint8_t **utf16le, uint32_t *byte_size);
bool PalText_GetMessage(const PalTextCache *cache, uint16_t index, const uint8_t **utf16le, uint32_t *byte_size);

#ifdef __cplusplus
}
#endif

#endif
