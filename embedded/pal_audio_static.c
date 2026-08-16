#include "pal_audio_static.h"

#include "pal_memory.h"

#include <stddef.h>
#include <string.h>

static int16_t clamp_i16(int32_t sample)
{
    if (sample > 32767) {
        return 32767;
    }
    if (sample < -32768) {
        return -32768;
    }
    return (int16_t)sample;
}

void PalAudio_Clear(uint16_t samples)
{
    if (samples > PAL_AUDIO_MIX_SAMPLES) {
        samples = PAL_AUDIO_MIX_SAMPLES;
    }
    memset(pal_sram_audio, 0, (uint32_t)samples * 2u);
}

bool PalAudio_OpenSfx(const uint8_t *payload, uint32_t payload_size, PalAudioSfx *sfx)
{
    if (payload == NULL || sfx == NULL ||
        payload_size > PAL_AUDIO_SFX_MAX_SAMPLES) {
        return false;
    }
    sfx->pcm = payload;
    sfx->sample_count = payload_size;
    return true;
}

bool PalAudio_MixSfx(const PalAudioSfx *sfx, uint32_t *cursor, uint16_t samples)
{
    uint32_t i;
    uint32_t output_pos;
    int16_t *mix = (int16_t *)pal_sram_audio;

    if (sfx == NULL || cursor == NULL || sfx->pcm == NULL || samples > PAL_AUDIO_MIX_SAMPLES) {
        return false;
    }

    output_pos = *cursor;
    for (i = 0;
         i < samples && output_pos / 2u < sfx->sample_count;
         i++, output_pos++) {
        uint8_t encoded = sfx->pcm[output_pos / 2u];
        int32_t pcm8 = encoded < 128u ? (int32_t)encoded : (int32_t)encoded - 256;
        int32_t mixed = (int32_t)mix[i] + pcm8 * 256;
        mix[i] = clamp_i16(mixed);
    }
    *cursor = output_pos;
    return true;
}

const int16_t *PalAudio_MixBuffer(void)
{
    return (const int16_t *)pal_sram_audio;
}
