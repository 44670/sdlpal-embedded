#include "pal_audio_static.h"

#include "pal_memory.h"

#include <stddef.h>
#include <string.h>

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int16_t read_i16(const uint8_t *p)
{
    return (int16_t)read_le16(p);
}

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
    uint16_t version;
    uint16_t header_size;
    uint32_t sample_rate;
    uint32_t sample_count;
    uint32_t pcm_offset;
    uint32_t pcm_size;

    if (payload == NULL || sfx == NULL || payload_size < PAL_AUDIO_SFX_HEADER_SIZE) {
        return false;
    }
    sfx->pcm = NULL;
    sfx->sample_count = 0;

    if (read_le32(payload) != PAL_AUDIO_SFX_MAGIC) {
        return false;
    }
    version = read_le16(payload + 4u);
    header_size = read_le16(payload + 6u);
    sample_rate = read_le32(payload + 8u);
    sample_count = read_le32(payload + 12u);
    pcm_offset = read_le32(payload + 16u);
    pcm_size = read_le32(payload + 20u);

    if (version != PAL_AUDIO_SFX_VERSION || header_size != PAL_AUDIO_SFX_HEADER_SIZE || sample_rate != PAL_AUDIO_SAMPLE_RATE) {
        return false;
    }
    if ((pcm_size & 1u) != 0 || pcm_size != sample_count * 2u) {
        return false;
    }
    if (pcm_offset > payload_size || pcm_size > payload_size - pcm_offset) {
        return false;
    }

    sfx->pcm = payload + pcm_offset;
    sfx->sample_count = sample_count;
    return true;
}

bool PalAudio_MixSfx(const PalAudioSfx *sfx, uint32_t *cursor, uint16_t samples)
{
    uint32_t i;
    uint32_t pos;
    int16_t *mix = (int16_t *)pal_sram_audio;

    if (sfx == NULL || cursor == NULL || sfx->pcm == NULL || samples > PAL_AUDIO_MIX_SAMPLES) {
        return false;
    }

    pos = *cursor;
    for (i = 0; i < samples && pos < sfx->sample_count; i++, pos++) {
        int32_t mixed = (int32_t)mix[i] + read_i16(sfx->pcm + pos * 2u);
        mix[i] = clamp_i16(mixed);
    }
    *cursor = pos;
    return true;
}

const int16_t *PalAudio_MixBuffer(void)
{
    return (const int16_t *)pal_sram_audio;
}
