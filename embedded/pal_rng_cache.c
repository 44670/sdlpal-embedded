#include "pal_rng_cache.h"

#include "pal_memory.h"

#include <string.h>

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint8_t *select_frame_buffer(PalRngFrameBuffer frame_buffer)
{
    if (frame_buffer == PAL_RNG_FRAME_BUFFER_A) {
        return pal_psram_rng_frame_a;
    }
    if (frame_buffer == PAL_RNG_FRAME_BUFFER_B) {
        return pal_psram_rng_frame_b;
    }
    return 0;
}

bool PalRng_LoadFrame(
    const PalPack *tf_pack,
    uint16_t movie_num,
    uint16_t frame_num,
    PalRngFrameBuffer frame_buffer,
    PalRngFrame *frame)
{
    PalPackSpan movie;
    uint32_t frame_count;
    uint32_t table_size;
    uint32_t frame_start;
    uint32_t frame_end;
    uint32_t frame_size;
    uint8_t *dst;

    if (frame == 0) {
        return false;
    }
    if (!PalPack_MapConst(tf_pack, PAL_PACK_ARCHIVE_RNG, movie_num, &movie)) {
        return false;
    }
    if (movie.data == 0 || movie.size < 8u || movie.format != PAL_PACK_FORMAT_RNG_FRAMES) {
        return false;
    }

    frame_count = read_le32(movie.data);
    if (frame_num >= frame_count) {
        return false;
    }
    if (frame_count > 0xffffu) {
        return false;
    }
    table_size = 4u + (frame_count + 1u) * 4u;
    if (table_size > movie.size) {
        return false;
    }

    frame_start = read_le32(movie.data + 4u + (uint32_t)frame_num * 4u);
    frame_end = read_le32(movie.data + 4u + ((uint32_t)frame_num + 1u) * 4u);
    if (frame_start < table_size || frame_end < frame_start || frame_end > movie.size) {
        return false;
    }

    frame_size = frame_end - frame_start;
    if (frame_size > PAL_PSRAM_RNG_FRAME_BYTES) {
        return false;
    }
    dst = select_frame_buffer(frame_buffer);
    if (dst == 0) {
        return false;
    }

    if (frame_size != 0) {
        memcpy(dst, movie.data + frame_start, frame_size);
    }
    frame->movie_num = movie_num;
    frame->frame_num = frame_num;
    frame->frame_count = (uint16_t)frame_count;
    frame->data = dst;
    frame->size = frame_size;
    return true;
}
