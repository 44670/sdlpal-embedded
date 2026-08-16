/*
 * SDLPAL
 * Copyright (c) 2011-2026, SDLPAL development team.
 * All rights reserved.
 *
 * Fixed-memory embedded MUS/RIX background-music path.
 */

#include "../../audio.h"
#include "../../palcfg.h"
#include "../../palcommon.h"
#include "../../adplug/rix.h"
#include "../../embedded/pal_mame_opl2_static.h"
#include "../../embedded/pal_music_cache.h"
#include "../main/pal_target_audio.h"
#include "pal_engine_pack_provider.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

#if defined(__GNUC__)
#define PAL_EXTREME_MUSIC_SRAM \
    __attribute__((section(".bss.pal_music"), aligned(8)))
#else
#define PAL_EXTREME_MUSIC_SRAM
#endif

extern "C" {
uint8_t pal_sram_sfx_pcm8[PAL_TARGET_SFX_BUFFER_SAMPLES]
    PAL_EXTREME_MUSIC_SRAM;
}

namespace
{

constexpr uint32_t kSampleRate = PAL_TARGET_AUDIO_SAMPLE_RATE;
constexpr size_t kBlockSamples = PAL_TARGET_AUDIO_BLOCK_SAMPLES;
constexpr uint32_t kRixRate = PAL_TARGET_AUDIO_RIX_HZ;
constexpr uint32_t kRixSampleBase = kSampleRate / kRixRate;
constexpr uint32_t kRixSampleRemainder = kSampleRate % kRixRate;
constexpr unsigned kCommandCount = 8;
constexpr int32_t kQ15One = 1 << 15;
constexpr uint32_t kFadePhaseOne = UINT32_C(1) << 31;
constexpr uint32_t kMaximumHalfFadeSamples = kSampleRate * 30u;
constexpr uint16_t kMusChunkCount = 88u;
constexpr uint16_t kEmptyMusTrackA = 0u;
constexpr uint16_t kEmptyMusTrackB = 29u;

static_assert(
    kRixRate == 70u && kRixSampleBase != 0u &&
        kRixSampleRemainder != 0u,
    "16.384 kHz RIX playback requires a fractional 70 Hz clock");
static_assert(
    kBlockSamples == 256u && (kBlockSamples & 1u) == 0u,
    "fixed blocks must preserve complete duplicated SFX sample pairs");
static_assert(
    PAL_TARGET_SFX_SAMPLE_RATE * 2u == kSampleRate &&
        PAL_TARGET_SFX_BUFFER_SAMPLES == 40960u,
    "the fixed SFX owner must hold five seconds of 8.192 kHz PCM8");
static_assert(
    kSampleRate == PAL_MAME_OPL2_SAMPLE_RATE,
    "RIX sink and fixed OPL2 backend sample rates must match");
static_assert(
    (kFadePhaseOne >> 16) == static_cast<uint32_t>(kQ15One) &&
        PAL_MAX_VOLUME > 0,
    "fade and volume fixed-point scales must be valid");

enum class CommandType : uint8_t
{
    Play,
    Volume,
    Enable,
};

struct MusicCommand
{
    CommandType type;
    uint8_t loop;
    uint8_t enabled;
    int16_t track;
    uint16_t volume_q15;
    uint32_t half_fade_samples;
    uint32_t play_generation;
};

enum class FadeState : uint8_t
{
    None,
    Out,
    In,
};

class PalExtremeOpl2 final : public Copl
{
public:
    PalExtremeOpl2() : Copl(TYPE_OPL2) {}

    void init() override
    {
        PalMameOpl2_Reset();
    }

    void write(int reg, int value) override
    {
        PalMameOpl2_Write(
            static_cast<uint8_t>(reg),
            static_cast<uint8_t>(value));
    }

    void update(short *samples, int frames) override
    {
        if (samples != nullptr && frames > 0)
        {
            PalMameOpl2_Render(
                reinterpret_cast<int16_t *>(samples),
                static_cast<size_t>(frames));
        }
    }

    bool getstereo() override
    {
        return false;
    }
};

struct MusicRuntime
{
    PalPack nor_pack;
    PalMusicTrack mapped_track;
    int current_track;
    int pending_track;
    bool current_loop;
    bool pending_loop;
    bool playing;
    bool enabled;
    FadeState fade;
    uint32_t fade_total;
    uint32_t fade_remaining;
    uint32_t fade_in_samples;
    uint32_t fade_phase_q31;
    uint32_t fade_step_base;
    uint32_t fade_step_remainder;
    uint32_t fade_step_error;
    uint32_t fade_step_denominator;
    uint32_t applied_play_generation;
    uint16_t volume_q15;
    uint32_t rix_sample_phase;
    uint32_t rix_samples_remaining;
    uint32_t rendered_ticks;
    uint32_t completed_loops;
    uint32_t missing_tracks;
};

struct SfxRuntime
{
    uint32_t sample_count;
    uint32_t cursor;
    int32_t last_loaded_sound_id;
    uint16_t volume_q15;
    uint8_t repeat_phase;
    bool active;
};

static const char *const kTag = "pal_music";

static PalExtremeOpl2 pal_music_opl;
static CrixPlayer pal_music_decoder(&pal_music_opl);
static MusicRuntime pal_music_runtime PAL_EXTREME_MUSIC_SRAM;
static StaticQueue_t pal_music_command_queue_object PAL_EXTREME_MUSIC_SRAM;
static uint8_t
    pal_sram_music_command_queue_storage[
        kCommandCount * sizeof(MusicCommand)] PAL_EXTREME_MUSIC_SRAM;
static QueueHandle_t pal_music_command_queue;
static MusicCommand pal_music_desired PAL_EXTREME_MUSIC_SRAM;
static uint32_t pal_music_command_drops;
static SfxRuntime pal_sfx_runtime PAL_EXTREME_MUSIC_SRAM;
static StaticSemaphore_t pal_sfx_mutex_object PAL_EXTREME_MUSIC_SRAM;
static SemaphoreHandle_t pal_sfx_mutex;

static int
music_clamped_config_volume()
{
    int volume = gConfig.iMusicVolume;

    if (volume < 0)
    {
        volume = 0;
    }
    else if (volume > PAL_MAX_VOLUME)
    {
        volume = PAL_MAX_VOLUME;
    }
    return volume;
}

static uint16_t
music_volume_q15()
{
    const int volume = music_clamped_config_volume();

    return static_cast<uint16_t>(
        (volume * kQ15One + PAL_MAX_VOLUME / 2) / PAL_MAX_VOLUME);
}

static int
music_sdl_volume()
{
    return music_clamped_config_volume() *
           SDL_MIX_MAXVOLUME / PAL_MAX_VOLUME;
}

static int
sound_clamped_config_volume()
{
    int volume = gConfig.iSoundVolume;

    if (volume < 0)
    {
        volume = 0;
    }
    else if (volume > PAL_MAX_VOLUME)
    {
        volume = PAL_MAX_VOLUME;
    }
    return volume;
}

static uint16_t
sound_volume_q15()
{
    const int volume = sound_clamped_config_volume();

    return static_cast<uint16_t>(
        (volume * kQ15One + PAL_MAX_VOLUME / 2) / PAL_MAX_VOLUME);
}

static int
sound_sdl_volume()
{
    return sound_clamped_config_volume() *
           SDL_MIX_MAXVOLUME / PAL_MAX_VOLUME;
}

static int16_t
sound_clamp_pcm16(int32_t sample)
{
    return static_cast<int16_t>(
        sample > INT16_MAX ? INT16_MAX :
        sample < INT16_MIN ? INT16_MIN : sample);
}

static void
sound_lock()
{
    if (pal_sfx_mutex != nullptr)
    {
        (void)xSemaphoreTake(pal_sfx_mutex, portMAX_DELAY);
    }
}

static void
sound_unlock()
{
    if (pal_sfx_mutex != nullptr)
    {
        (void)xSemaphoreGive(pal_sfx_mutex);
    }
}

static void
sound_stop_now()
{
    sound_lock();
    pal_sfx_runtime.cursor = 0u;
    pal_sfx_runtime.repeat_phase = 0u;
    pal_sfx_runtime.active = false;
    sound_unlock();
}

static void
sound_invalidate_loaded()
{
    sound_lock();
    pal_sfx_runtime.sample_count = 0u;
    pal_sfx_runtime.cursor = 0u;
    pal_sfx_runtime.last_loaded_sound_id = -1;
    pal_sfx_runtime.repeat_phase = 0u;
    pal_sfx_runtime.active = false;
    sound_unlock();
}

static void
sound_mix(int16_t *samples, size_t sample_count)
{
    size_t i;

    sound_lock();
    for (i = 0u;
         i < sample_count && pal_sfx_runtime.active;
         i++)
    {
        const uint8_t encoded =
            pal_sram_sfx_pcm8[pal_sfx_runtime.cursor];
        const int32_t pcm8 = encoded < 128u
            ? static_cast<int32_t>(encoded)
            : static_cast<int32_t>(encoded) - 256;
        const int32_t effect =
            (pcm8 * 256 * pal_sfx_runtime.volume_q15) >> 15;

        samples[i] = sound_clamp_pcm16(
            static_cast<int32_t>(samples[i]) + effect);
        pal_sfx_runtime.repeat_phase ^= 1u;
        if (pal_sfx_runtime.repeat_phase == 0u)
        {
            pal_sfx_runtime.cursor++;
            if (pal_sfx_runtime.cursor >= pal_sfx_runtime.sample_count)
            {
                pal_sfx_runtime.active = false;
            }
        }
    }
    sound_unlock();
}

static uint32_t
music_half_fade_samples(FLOAT seconds)
{
    FLOAT samples;

    if (!(seconds > 0.0f))
    {
        return 0;
    }
    samples = seconds * (static_cast<FLOAT>(kSampleRate) * 0.5f);
    if (samples >= static_cast<FLOAT>(kMaximumHalfFadeSamples))
    {
        return kMaximumHalfFadeSamples;
    }
    return static_cast<uint32_t>(samples + 0.5f);
}

static bool
music_validate_nor_profile()
{
    MusicRuntime &state = pal_music_runtime;
    PalMusicTrack track;
    PalPackSpan span;
    uint16_t chunk_count = 0;

    if (!PalPack_GetChunkCount(
            &state.nor_pack,
            PAL_PACK_ARCHIVE_MUS,
            &chunk_count) ||
        chunk_count != kMusChunkCount)
    {
        ESP_LOGE(
            kTag,
            "MUS profile mismatch: chunks=%u expected=%u",
            static_cast<unsigned>(chunk_count),
            static_cast<unsigned>(kMusChunkCount));
        return false;
    }

    for (uint16_t track_number = 0;
         track_number < kMusChunkCount;
         track_number++)
    {
        if (track_number == kEmptyMusTrackA ||
            track_number == kEmptyMusTrackB)
        {
            if (!PalPack_MapConst(
                    &state.nor_pack,
                    PAL_PACK_ARCHIVE_MUS,
                    track_number,
                    &span) ||
                span.size != 0)
            {
                ESP_LOGE(
                    kTag,
                    "MUS profile mismatch: track %u must be empty",
                    static_cast<unsigned>(track_number));
                return false;
            }
            continue;
        }

        /*
         * MapMus checks the pack entry/header while load_buffer performs the
         * decoder's full bounded RIX record/instrument-offset validation.
         * This runs once before the real-time audio task starts.
         */
        if (!PalMusic_MapMus(
                &state.nor_pack,
                track_number,
                &track) ||
            !pal_music_decoder.load_buffer(track.data, track.size))
        {
            ESP_LOGE(
                kTag,
                "MUS profile mismatch: invalid RIX track %u",
                static_cast<unsigned>(track_number));
            return false;
        }
    }

    PalMameOpl2_Reset();
    return true;
}

static bool
music_send_command(
    const MusicCommand &command)
{
    if (pal_music_command_queue == nullptr)
    {
        return false;
    }
    if (xQueueSend(
            pal_music_command_queue,
            &command,
            pdMS_TO_TICKS(20)) == pdTRUE)
    {
        return true;
    }

    /*
     * Every command is a complete desired-state snapshot.  Resetting a full
     * queue therefore retains the latest play generation, enable flag and
     * volume together instead of allowing one field to drift indefinitely.
     * The audio task either owns a copied older snapshot or observes this one
     * no later than its next tick.
     */
    (void)xQueueReset(pal_music_command_queue);
    if (xQueueSend(
            pal_music_command_queue,
            &command,
            0) == pdTRUE)
    {
        pal_music_command_drops++;
        ESP_LOGW(kTag, "coalesced stale music commands");
        return true;
    }

    pal_music_command_drops++;
    ESP_LOGE(kTag, "music command queue full");
    return false;
}

static void
music_stop_now()
{
    MusicRuntime &state = pal_music_runtime;

    state.current_track = -1;
    state.pending_track = -1;
    state.current_loop = false;
    state.pending_loop = false;
    state.playing = false;
    state.fade = FadeState::None;
    state.fade_total = 0;
    state.fade_remaining = 0;
    state.fade_in_samples = 0;
    state.fade_phase_q31 = kFadePhaseOne;
    state.fade_step_base = 0;
    state.fade_step_remainder = 0;
    state.fade_step_error = 0;
    state.fade_step_denominator = 0;
    state.rix_sample_phase = 0u;
    state.rix_samples_remaining = 0u;
    state.mapped_track.data = nullptr;
    state.mapped_track.size = 0;
    state.mapped_track.track_num = 0;
    state.mapped_track.format = 0;
    PalMameOpl2_Reset();
}

static int32_t
music_fade_gain_q15()
{
    const MusicRuntime &state = pal_music_runtime;

    if (state.fade == FadeState::None)
    {
        return kQ15One;
    }
    return static_cast<int32_t>(
        state.fade_phase_q31 >> 16);
}

static void
music_clear_fade()
{
    MusicRuntime &state = pal_music_runtime;

    state.fade = FadeState::None;
    state.fade_total = 0;
    state.fade_remaining = 0;
    state.fade_phase_q31 = kFadePhaseOne;
    state.fade_step_base = 0;
    state.fade_step_remainder = 0;
    state.fade_step_error = 0;
    state.fade_step_denominator = 0;
}

static void
music_configure_fade(
    FadeState fade,
    uint32_t total_samples,
    uint32_t phase_q31)
{
    MusicRuntime &state = pal_music_runtime;
    const uint32_t distance = fade == FadeState::Out
        ? phase_q31
        : kFadePhaseOne - phase_q31;
    uint32_t remaining;

    state.fade = fade;
    state.fade_total = total_samples;
    state.fade_phase_q31 = phase_q31;
    state.fade_step_error = 0;

    if (total_samples == 0 || distance == 0)
    {
        state.fade_remaining = 0;
        state.fade_step_base = 0;
        state.fade_step_remainder = 0;
        state.fade_step_denominator = 0;
        if (fade == FadeState::Out)
        {
            state.fade_phase_q31 = 0;
        }
        else
        {
            music_clear_fade();
        }
        return;
    }

    /*
     * A replacement fade keeps the requested full-scale slope.  The shift is
     * division by the Q31 unity value, and is only done at command boundaries.
     */
    remaining = static_cast<uint32_t>(
        (static_cast<uint64_t>(total_samples) * distance +
         kFadePhaseOne - 1u) >>
        31);
    if (remaining == 0)
    {
        remaining = 1;
    }
    else if (remaining > total_samples)
    {
        remaining = total_samples;
    }

    /*
     * Bresenham-style phase steps make the segment reach its endpoint in
     * exactly 'remaining' samples.  The audio callback therefore needs only
     * 32-bit add/compare operations, not one 64-bit division per PCM sample.
     */
    state.fade_remaining = remaining;
    state.fade_step_base = distance / remaining;
    state.fade_step_remainder = distance % remaining;
    state.fade_step_denominator = remaining;
}

static void
music_begin_fade_out(uint32_t total_samples)
{
    const uint32_t current_phase =
        pal_music_runtime.fade == FadeState::None
            ? kFadePhaseOne
            : pal_music_runtime.fade_phase_q31;

    music_configure_fade(
        FadeState::Out,
        total_samples,
        current_phase);
}

static bool
music_advance_fade_one_sample()
{
    MusicRuntime &state = pal_music_runtime;
    uint32_t step;

    if (state.fade == FadeState::None ||
        state.fade_remaining == 0 ||
        state.fade_step_denominator == 0)
    {
        return false;
    }

    step = state.fade_step_base;
    state.fade_step_error += state.fade_step_remainder;
    if (state.fade_step_error >= state.fade_step_denominator)
    {
        state.fade_step_error -= state.fade_step_denominator;
        step++;
    }

    if (state.fade == FadeState::Out)
    {
        state.fade_phase_q31 =
            step >= state.fade_phase_q31
                ? 0
                : state.fade_phase_q31 - step;
    }
    else
    {
        state.fade_phase_q31 =
            step >= kFadePhaseOne - state.fade_phase_q31
                ? kFadePhaseOne
                : state.fade_phase_q31 + step;
    }

    state.fade_remaining--;
    if (state.fade_remaining != 0)
    {
        return false;
    }
    if (state.fade == FadeState::Out)
    {
        state.fade_phase_q31 = 0;
        return true;
    }

    music_clear_fade();
    return false;
}

static bool
music_start_pending()
{
    MusicRuntime &state = pal_music_runtime;
    const int track_number = state.pending_track;
    const bool loop = state.pending_loop;
    const uint32_t fade_in = state.fade_in_samples;

    state.pending_track = -1;
    state.pending_loop = false;
    state.fade_in_samples = 0;
    if (track_number <= 0)
    {
        music_stop_now();
        return true;
    }
    if (track_number > UINT16_MAX ||
        !PalMusic_MapMus(
            &state.nor_pack,
            static_cast<uint16_t>(track_number),
            &state.mapped_track) ||
        !pal_music_decoder.load_buffer(
            state.mapped_track.data,
            state.mapped_track.size))
    {
        state.missing_tracks++;
        PalTargetAudio_RecordSourceFault(track_number);
        music_stop_now();
        return false;
    }

    state.current_track = track_number;
    state.current_loop = loop;
    state.playing = true;
    state.rix_sample_phase = 0u;
    state.rix_samples_remaining = 0u;
    if (fade_in == 0)
    {
        music_clear_fade();
    }
    else
    {
        music_configure_fade(
            FadeState::In,
            fade_in,
            0);
    }
    return true;
}

static void
music_apply_play_control(const MusicCommand &command)
{
    MusicRuntime &state = pal_music_runtime;

    /*
     * Desktop RIX changes only the loop policy when the requested track is
     * already active and there is no pending switch.  Once a fade-out has a
     * destination, even requesting the current track must finish the fade,
     * rewind that track, and apply the requested fade-in.
     */
    if (state.playing &&
        command.track > 0 &&
        command.track == state.current_track &&
        state.pending_track < 0)
    {
        state.current_loop = command.loop != 0;
        return;
    }

    state.pending_track = command.track;
    state.pending_loop = command.loop != 0;
    state.fade_in_samples = command.half_fade_samples;

    /*
     * RIX_Play() keeps an already-running fade-out authoritative.  A later
     * request changes only the destination and its fade-in duration, even
     * when that request asks for a zero-duration fade.  Battle startup relies
     * on this: stop(1s), wait 200ms, then play(battle, 0s).
     */
    if (state.fade == FadeState::Out)
    {
        return;
    }
    if (!state.playing || command.half_fade_samples == 0)
    {
        (void)music_start_pending();
        return;
    }
    music_begin_fade_out(command.half_fade_samples);
}

static void
music_apply_snapshot(const MusicCommand &command)
{
    MusicRuntime &state = pal_music_runtime;

    state.volume_q15 = command.volume_q15;
    state.enabled = command.enabled != 0;
    if (command.play_generation == state.applied_play_generation)
    {
        return;
    }

    /*
     * Enable and volume snapshots must not replay a naturally completed or
     * failed source.  A Play call alone advances this generation.  Apply its
     * control transition even while muted, as desktop RIX does; rendering
     * below remains frozen until music is enabled with nonzero volume.
     */
    state.applied_play_generation = command.play_generation;
    music_apply_play_control(command);
}

static void
music_drain_commands()
{
    MusicCommand command;

    for (unsigned count = 0;
         count < kCommandCount &&
         pal_music_command_queue != nullptr &&
         xQueueReceive(
             pal_music_command_queue,
             &command,
             0) == pdTRUE;
         count++)
    {
        music_apply_snapshot(command);
    }
}

static bool
music_prepare_rix_interval()
{
    MusicRuntime &state = pal_music_runtime;
    bool retried_loop = false;

    if (state.fade == FadeState::Out && state.fade_remaining == 0u)
    {
        (void)music_start_pending();
    }
    if (!state.playing)
    {
        return false;
    }

    for (;;)
    {
        if (pal_music_decoder.update())
        {
            break;
        }

        if (state.current_loop && !retried_loop)
        {
            retried_loop = true;
            pal_music_decoder.rewindReInit(0, false);
            if (pal_music_decoder.update())
            {
                state.completed_loops++;
                break;
            }
        }

        /*
         * A short non-looping source can end before its requested fade-out.
         * The pending request remains authoritative and starts on this 70 Hz
         * boundary rather than being erased by music_stop_now().
         */
        if (state.pending_track >= 0)
        {
            retried_loop = false;
            if (music_start_pending() && state.playing)
            {
                continue;
            }
        }

        music_stop_now();
        return false;
    }

    state.rendered_ticks++;
    return true;
}

static uint32_t
music_next_rix_sample_count()
{
    MusicRuntime &state = pal_music_runtime;
    uint32_t result = kRixSampleBase;

    state.rix_sample_phase += kRixSampleRemainder;
    if (state.rix_sample_phase >= kRixRate)
    {
        state.rix_sample_phase -= kRixRate;
        result++;
    }
    return result;
}

static void
music_render(
    void *user,
    int16_t *samples,
    size_t sample_count)
{
    MusicRuntime &state = pal_music_runtime;
    size_t offset = 0u;

    (void)user;
    if (samples == nullptr)
    {
        return;
    }
    if (sample_count != kBlockSamples)
    {
        const size_t safe_samples =
            sample_count < kBlockSamples ? sample_count : kBlockSamples;
        memset(samples, 0, safe_samples * sizeof(*samples));
        return;
    }

    music_drain_commands();
    memset(samples, 0, sample_count * sizeof(*samples));
    /*
     * The desktop mixer does not call the RIX player while music is disabled
     * or its volume is zero.  Preserve that pause/resume behavior here: a
     * zero-volume block must not advance the sequencer, OPL state, or an
     * in-progress fade behind the user's back. Effects remain independent.
     */
    while (state.enabled && state.volume_q15 != 0u && offset < sample_count)
    {
        size_t amount;

        if (state.rix_samples_remaining == 0u)
        {
            (void)music_prepare_rix_interval();
            state.rix_samples_remaining =
                music_next_rix_sample_count();
        }
        amount = sample_count - offset;
        if (amount > state.rix_samples_remaining)
        {
            amount = state.rix_samples_remaining;
        }
        if (state.playing)
        {
            PalMameOpl2_Render(samples + offset, amount);
        }
        for (size_t i = 0u; i < amount; i++)
        {
            const int32_t fade_gain = music_fade_gain_q15();
            const int32_t gain = static_cast<int32_t>(
                (static_cast<int64_t>(fade_gain) * state.volume_q15 +
                 (1 << 14)) >>
                15);

            samples[offset + i] = static_cast<int16_t>(
                (static_cast<int64_t>(samples[offset + i]) * gain) >> 15);
            (void)music_advance_fade_one_sample();
        }

        state.rix_samples_remaining -= static_cast<uint32_t>(amount);
        offset += amount;
    }
    sound_mix(samples, sample_count);
}

} // namespace

extern "C" {

AUDIODEVICE gAudioDevice;

INT
AUDIO_OpenDevice(VOID)
{
    if (gAudioDevice.fOpened)
    {
        return 0;
    }

    memset(&gAudioDevice, 0, sizeof(gAudioDevice));
    memset(&pal_music_runtime, 0, sizeof(pal_music_runtime));
    memset(&pal_music_desired, 0, sizeof(pal_music_desired));
    memset(&pal_sfx_runtime, 0, sizeof(pal_sfx_runtime));
    pal_sfx_runtime.last_loaded_sound_id = -1;
    pal_music_command_queue = nullptr;
    pal_music_command_drops = 0;
    pal_sfx_mutex = xSemaphoreCreateMutexStatic(&pal_sfx_mutex_object);
    if (pal_sfx_mutex == nullptr)
    {
        ESP_LOGE(kTag, "cannot create fixed SFX mutex");
        return -1;
    }
    pal_music_runtime.current_track = -1;
    pal_music_runtime.pending_track = -1;
    pal_music_runtime.enabled = true;
    gConfig.iMusicVolume = music_clamped_config_volume();
    gConfig.iSoundVolume = sound_clamped_config_volume();
    pal_music_runtime.volume_q15 = music_volume_q15();
    pal_sfx_runtime.volume_q15 = sound_volume_q15();
    pal_music_desired.type = CommandType::Play;
    pal_music_desired.track = 0;
    pal_music_desired.enabled = 1u;
    pal_music_desired.volume_q15 = pal_music_runtime.volume_q15;
    pal_music_desired.play_generation = 0;

    gAudioDevice.spec.freq = static_cast<int>(kSampleRate);
    gAudioDevice.spec.format = AUDIO_S16SYS;
    gAudioDevice.spec.channels = 1;
#if !SDL_VERSION_ATLEAST(3, 0, 0)
    gAudioDevice.spec.samples =
        static_cast<Uint16>(PAL_TARGET_AUDIO_BLOCK_SAMPLES);
#endif
    gAudioDevice.iMusicVolume = music_sdl_volume();
    gAudioDevice.iSoundVolume = sound_sdl_volume();
    gAudioDevice.fSoundEnabled = TRUE;

    if (!PalContract_TargetOpenNorPack(&pal_music_runtime.nor_pack))
    {
        ESP_LOGE(kTag, "NOR pack is unavailable to music");
        return -1;
    }

    PalMameOpl2_Init();
    if (!music_validate_nor_profile())
    {
        return -4;
    }

    pal_music_command_queue = xQueueCreateStatic(
        kCommandCount,
        sizeof(MusicCommand),
        pal_sram_music_command_queue_storage,
        &pal_music_command_queue_object);
    if (pal_music_command_queue == nullptr)
    {
        ESP_LOGE(kTag, "create static music command queue");
        return -2;
    }

    if (!PalTargetAudio_Begin(music_render, nullptr))
    {
        ESP_LOGE(kTag, "target audio backend failed");
        music_stop_now();
        pal_music_command_queue = nullptr;
        return -3;
    }

    gAudioDevice.fOpened = TRUE;
    gAudioDevice.fMusicEnabled = TRUE;
    ESP_LOGI(
        kTag,
        "fixed audio ready: music=%u Hz SFX=%u Hz/%u bytes "
        "OPL_state=%u OPL_tables=%u",
        static_cast<unsigned>(kSampleRate),
        static_cast<unsigned>(PAL_TARGET_SFX_SAMPLE_RATE),
        static_cast<unsigned>(sizeof(pal_sram_sfx_pcm8)),
        static_cast<unsigned>(PalMameOpl2_StateBytes()),
        static_cast<unsigned>(PalMameOpl2_TableBytes()));
    return 0;
}

BOOL
AUDIO_CD_Available(VOID)
{
    return FALSE;
}

VOID
AUDIO_CloseDevice(VOID)
{
    if (!gAudioDevice.fOpened)
    {
        sound_invalidate_loaded();
        pal_music_command_queue = nullptr;
        gAudioDevice.fMusicEnabled = FALSE;
        gAudioDevice.fSoundEnabled = FALSE;
        return;
    }

    if (!PalTargetAudio_Stop())
    {
        ESP_LOGE(kTag, "audio close deferred: task is still active");
        return;
    }
    PalTargetAudio_LogTelemetry("close");
    ESP_LOGI(
        kTag,
        "RIX stats: ticks=%u loops=%u missing=%u coalesced=%u",
        static_cast<unsigned>(pal_music_runtime.rendered_ticks),
        static_cast<unsigned>(pal_music_runtime.completed_loops),
        static_cast<unsigned>(pal_music_runtime.missing_tracks),
        static_cast<unsigned>(pal_music_command_drops));
    music_stop_now();
    sound_invalidate_loaded();
    pal_music_command_queue = nullptr;
    gAudioDevice.fOpened = FALSE;
    gAudioDevice.fMusicEnabled = FALSE;
    gAudioDevice.fSoundEnabled = FALSE;
}

SDL_AudioSpec *
AUDIO_GetDeviceSpec(VOID)
{
    return &gAudioDevice.spec;
}

VOID
AUDIO_IncreaseVolume(VOID)
{
    MusicCommand command = pal_music_desired;
    int volume = music_clamped_config_volume();
    int sound_volume = sound_clamped_config_volume();

    if (PAL_MAX_VOLUME - volume < 3)
    {
        volume = PAL_MAX_VOLUME;
    }
    else
    {
        volume += 3;
    }
    if (PAL_MAX_VOLUME - sound_volume < 3)
    {
        sound_volume = PAL_MAX_VOLUME;
    }
    else
    {
        sound_volume += 3;
    }
    gConfig.iMusicVolume = volume;
    gConfig.iSoundVolume = sound_volume;
    gAudioDevice.iMusicVolume = music_sdl_volume();
    gAudioDevice.iSoundVolume = sound_sdl_volume();
    sound_lock();
    pal_sfx_runtime.volume_q15 = sound_volume_q15();
    sound_unlock();
    command.type = CommandType::Volume;
    command.volume_q15 = music_volume_q15();
    pal_music_desired.volume_q15 = command.volume_q15;
    if (gAudioDevice.fOpened)
    {
        (void)music_send_command(command);
    }
}

VOID
AUDIO_DecreaseVolume(VOID)
{
    MusicCommand command = pal_music_desired;
    int volume = music_clamped_config_volume();
    int sound_volume = sound_clamped_config_volume();

    if (volume < 3)
    {
        volume = 0;
    }
    else
    {
        volume -= 3;
    }
    if (sound_volume < 3)
    {
        sound_volume = 0;
    }
    else
    {
        sound_volume -= 3;
    }
    gConfig.iMusicVolume = volume;
    gConfig.iSoundVolume = sound_volume;
    gAudioDevice.iMusicVolume = music_sdl_volume();
    gAudioDevice.iSoundVolume = sound_sdl_volume();
    sound_lock();
    pal_sfx_runtime.volume_q15 = sound_volume_q15();
    sound_unlock();
    command.type = CommandType::Volume;
    command.volume_q15 = music_volume_q15();
    pal_music_desired.volume_q15 = command.volume_q15;
    if (gAudioDevice.fOpened)
    {
        (void)music_send_command(command);
    }
}

VOID
AUDIO_PlayMusic(
    INT track,
    BOOL loop,
    FLOAT fade_time)
{
    MusicCommand command = pal_music_desired;

    if (!gAudioDevice.fOpened)
    {
        return;
    }
    if (track > INT16_MAX)
    {
        ESP_LOGE(kTag, "RIX track out of range: %d", track);
        return;
    }
    command.type = CommandType::Play;
    command.enabled =
        gAudioDevice.fMusicEnabled ? 1u : 0u;
    command.track =
        track > 0 && track != kEmptyMusTrackB
            ? static_cast<int16_t>(track)
            : 0;
    command.loop = loop ? 1u : 0u;
    command.volume_q15 = music_volume_q15();
    command.half_fade_samples = music_half_fade_samples(fade_time);
    command.play_generation++;
    if (command.play_generation == 0)
    {
        command.play_generation = 1;
    }
    pal_music_desired = command;
    (void)music_send_command(command);
}

BOOL
AUDIO_PlayCDTrack(INT track)
{
    (void)track;
    return FALSE;
}

VOID
AUDIO_PlaySound(INT sound)
{
    const int64_t signed_sound = static_cast<int64_t>(sound);
    const uint64_t absolute_sound = signed_sound < 0
        ? static_cast<uint64_t>(-signed_sound)
        : static_cast<uint64_t>(signed_sound);
    uint32_t sample_count = 0u;

    if (!gAudioDevice.fOpened || !gAudioDevice.fSoundEnabled ||
        sound_clamped_config_volume() == 0 ||
        absolute_sound == 0u || absolute_sound > UINT16_MAX)
    {
        sound_stop_now();
        return;
    }
    sound_lock();
    if (pal_sfx_runtime.last_loaded_sound_id ==
        static_cast<int32_t>(absolute_sound))
    {
        pal_sfx_runtime.cursor = 0u;
        pal_sfx_runtime.volume_q15 = sound_volume_q15();
        pal_sfx_runtime.repeat_phase = 0u;
        pal_sfx_runtime.active = pal_sfx_runtime.sample_count != 0u;
        sound_unlock();
        return;
    }
    pal_sfx_runtime.sample_count = 0u;
    pal_sfx_runtime.cursor = 0u;
    pal_sfx_runtime.last_loaded_sound_id = -1;
    pal_sfx_runtime.repeat_phase = 0u;
    pal_sfx_runtime.active = false;
    sound_unlock();
    if (!PalEngineBridge_ReadSfxPcm8(
            static_cast<uint16_t>(absolute_sound),
            pal_sram_sfx_pcm8,
            sizeof(pal_sram_sfx_pcm8),
            &sample_count))
    {
        ESP_LOGE(kTag, "SFX %u is unavailable or exceeds the fixed owner",
            static_cast<unsigned>(absolute_sound));
        PalTargetAudio_RecordSourceFault(
            -static_cast<int32_t>(absolute_sound));
        return;
    }

    sound_lock();
    pal_sfx_runtime.sample_count = sample_count;
    pal_sfx_runtime.cursor = 0u;
    pal_sfx_runtime.last_loaded_sound_id =
        static_cast<int32_t>(absolute_sound);
    pal_sfx_runtime.volume_q15 = sound_volume_q15();
    pal_sfx_runtime.repeat_phase = 0u;
    pal_sfx_runtime.active = sample_count != 0u;
    sound_unlock();
}

VOID
AUDIO_EnableMusic(BOOL enable)
{
    MusicCommand command = pal_music_desired;

    gAudioDevice.fMusicEnabled = enable ? TRUE : FALSE;
    pal_music_desired.enabled = enable ? 1u : 0u;
    if (gAudioDevice.fOpened)
    {
        command.type = CommandType::Enable;
        command.enabled = pal_music_desired.enabled;
        command.volume_q15 = music_volume_q15();
        pal_music_desired.volume_q15 = command.volume_q15;
        (void)music_send_command(command);
    }
}

BOOL
AUDIO_MusicEnabled(VOID)
{
    return gAudioDevice.fMusicEnabled;
}

VOID
AUDIO_EnableSound(BOOL enable)
{
    gAudioDevice.fSoundEnabled = enable ? TRUE : FALSE;
    sound_lock();
    pal_sfx_runtime.volume_q15 = sound_volume_q15();
    if (!enable)
    {
        pal_sfx_runtime.sample_count = 0u;
        pal_sfx_runtime.cursor = 0u;
        pal_sfx_runtime.repeat_phase = 0u;
        pal_sfx_runtime.active = false;
    }
    sound_unlock();
}

BOOL
AUDIO_SoundEnabled(VOID)
{
    return gAudioDevice.fSoundEnabled;
}

void
AUDIO_Lock(void)
{
}

void
AUDIO_Unlock(void)
{
}

} // extern "C"
