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

typedef struct PalRngMovieStream {
    uint16_t movie_num;
    uint16_t frame_count;
    uint32_t chunk_offset;
    uint32_t chunk_size;
    uint32_t table_size;
    const uint8_t *table;
} PalRngMovieStream;

bool PalRng_LoadFrame(
    const PalPack *tf_pack,
    uint16_t movie_num,
    uint16_t frame_num,
    PalRngFrameBuffer frame_buffer,
    PalRngFrame *frame);
bool PalRng_OpenMovieReadAt(
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    uint16_t movie_num,
    PalRngMovieStream *movie);
bool PalRng_LoadFrameReadAt(
    const PalRngMovieStream *movie,
    PalPackReadAt read_at,
    void *user,
    uint16_t frame_num,
    PalRngFrameBuffer frame_buffer,
    PalRngFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
