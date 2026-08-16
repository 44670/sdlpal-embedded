#ifndef PAL_AUDIO_STATIC_H
#define PAL_AUDIO_STATIC_H

#include "pal_memory.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_AUDIO_SAMPLE_RATE 16384u
#define PAL_AUDIO_SFX_SAMPLE_RATE 8192u
#define PAL_AUDIO_SFX_MAX_SAMPLES (PAL_AUDIO_SFX_SAMPLE_RATE * 5u)
#define PAL_AUDIO_CHANNELS 1u
#define PAL_AUDIO_MIX_SAMPLES (PAL_SRAM_AUDIO_BYTES / 2u)

typedef struct PalAudioSfx {
    const uint8_t *pcm;
    uint32_t sample_count;
} PalAudioSfx;

void PalAudio_Clear(uint16_t samples);
bool PalAudio_OpenSfx(const uint8_t *payload, uint32_t payload_size, PalAudioSfx *sfx);
/* cursor counts 16.384 kHz output frames; each PCM8 source sample spans two. */
bool PalAudio_MixSfx(const PalAudioSfx *sfx, uint32_t *cursor, uint16_t samples);
const int16_t *PalAudio_MixBuffer(void);

#ifdef __cplusplus
}
#endif

#endif
