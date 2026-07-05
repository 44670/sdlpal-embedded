#ifndef PAL_RNG_CACHE_H
#define PAL_RNG_CACHE_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PalRngFrameBuffer {
    PAL_RNG_FRAME_BUFFER_A = 0,
    PAL_RNG_FRAME_BUFFER_B = 1,
} PalRngFrameBuffer;

typedef struct PalRngFrame {
    uint16_t movie_num;
    uint16_t frame_num;
    uint16_t frame_count;
    uint8_t *data;
    uint32_t size;
} PalRngFrame;

bool PalRng_LoadFrame(
    const PalPack *tf_pack,
    uint16_t movie_num,
    uint16_t frame_num,
    PalRngFrameBuffer frame_buffer,
    PalRngFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
