#ifndef PAL_MUSIC_CACHE_H
#define PAL_MUSIC_CACHE_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_MUSIC_FORMAT_MIDI 1u
#define PAL_MUSIC_FORMAT_RIX 2u

typedef struct PalMusicTrack {
    const uint8_t *data;
    uint32_t size;
    uint16_t track_num;
    uint16_t format;
} PalMusicTrack;

bool PalMusic_MapMidi(const PalPack *nor_pack, uint16_t track_num, PalMusicTrack *track);
bool PalMusic_MapMus(const PalPack *nor_pack, uint16_t track_num, PalMusicTrack *track);

#ifdef __cplusplus
}
#endif

#endif
