/*
 * SDLPAL
 * Copyright (c) 2011-2026, SDLPAL development team.
 * All rights reserved.
 *
 * Cardputer ADV fixed-memory MUS/RIX background-music path.
 */

#include "../../audio.h"
#include "../../palcfg.h"
#include "../../adplug/rix.h"
#include "../../embedded/pal_mame_opl2_static.h"
#include "../../embedded/pal_music_cache.h"
#include "../main/cardputer_extreme_audio.h"
#include "pal_engine_pack_provider.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

namespace
{

constexpr uint32_t kSampleRate = CARDPUTER_EXTREME_AUDIO_SAMPLE_RATE;
constexpr size_t kTickSamples = CARDPUTER_EXTREME_AUDIO_TICK_SAMPLES;
constexpr unsigned kCommandCount = 8;
constexpr int32_t kQ15One = 1 << 15;
constexpr uint32_t kFadePhaseOne = UINT32_C(1) << 31;
constexpr uint32_t kMaximumHalfFadeSamples = kSampleRate * 30u;

static_assert(
    CARDPUTER_EXTREME_AUDIO_TICK_HZ == 70u &&
        kSampleRate % CARDPUTER_EXTREME_AUDIO_TICK_HZ == 0u &&
        kTickSamples ==
            kSampleRate / CARDPUTER_EXTREME_AUDIO_TICK_HZ,
    "each RIX 70 Hz update must render one complete PCM tick");
static_assert(
    kSampleRate == PAL_MAME_OPL2_SAMPLE_RATE,
    "RIX sink and fixed OPL2 backend sample rates must match");
static_assert(
    (kFadePhaseOne >> 16) == static_cast<uint32_t>(kQ15One) &&
        PAL_MAX_VOLUME > 0,
    "fade and volume fixed-point scales must be valid");

#if defined(__GNUC__)
#define PAL_EXTREME_MUSIC_SRAM \
    __attribute__((section(".bss.pal_music"), aligned(8)))
#else
#define PAL_EXTREME_MUSIC_SRAM
#endif

enum class CommandType : uint8_t
{
    Play,
    Volume,
};

struct MusicCommand
{
    CommandType type;
    uint8_t loop;
    int16_t track;
    uint16_t volume_q15;
    uint32_t half_fade_samples;
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
    FadeState fade;
    uint32_t fade_total;
    uint32_t fade_remaining;
    uint32_t fade_in_samples;
    uint32_t fade_phase_q31;
    uint32_t fade_step_base;
    uint32_t fade_step_remainder;
    uint32_t fade_step_error;
    uint32_t fade_step_denominator;
    uint16_t volume_q15;
    uint32_t rendered_ticks;
    uint32_t completed_loops;
    uint32_t missing_tracks;
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
static uint32_t pal_music_command_drops;

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
music_send_command(
    const MusicCommand &command,
    bool supersede_on_full)
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
     * Play and stop requests describe the latest desired state.  In
     * particular, disabling music must not fail because obsolete requests
     * filled the queue.  xQueueReset is task-safe for this normal queue; the
     * audio task either owns a copied command or observes this one next tick.
     */
    if (supersede_on_full)
    {
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

static void
music_reverse_fade_to_current(uint32_t total_samples)
{
    MusicRuntime &state = pal_music_runtime;
    const uint32_t current_phase = state.fade == FadeState::None
        ? kFadePhaseOne
        : state.fade_phase_q31;

    if (total_samples == 0 || current_phase >= kFadePhaseOne)
    {
        music_clear_fade();
        return;
    }

    music_configure_fade(
        FadeState::In,
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
        music_stop_now();
        return false;
    }

    state.current_track = track_number;
    state.current_loop = loop;
    state.playing = true;
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
music_apply_play(const MusicCommand &command)
{
    MusicRuntime &state = pal_music_runtime;

    state.volume_q15 = command.volume_q15;

    /*
     * Reissuing the active track updates its loop policy instead of rewinding.
     * It also cancels an obsolete pending switch.  If that switch had begun
     * fading out, reverse smoothly from the current gain.
     */
    if (state.playing &&
        command.track > 0 &&
        command.track == state.current_track)
    {
        state.current_loop = command.loop != 0;
        state.pending_track = -1;
        state.pending_loop = false;
        state.fade_in_samples = 0;
        if (state.fade == FadeState::Out)
        {
            music_reverse_fade_to_current(
                command.half_fade_samples);
        }
        return;
    }

    state.pending_track = command.track;
    state.pending_loop = command.loop != 0;
    state.fade_in_samples = command.half_fade_samples;

    if (!state.playing || command.half_fade_samples == 0)
    {
        (void)music_start_pending();
        return;
    }
    music_begin_fade_out(command.half_fade_samples);
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
        if (command.type == CommandType::Volume)
        {
            pal_music_runtime.volume_q15 = command.volume_q15;
        }
        else
        {
            music_apply_play(command);
        }
    }
}

static bool
music_render_tick_source(int16_t *samples)
{
    MusicRuntime &state = pal_music_runtime;
    bool retried_loop = false;

    if (!state.playing)
    {
        memset(samples, 0, kTickSamples * sizeof(*samples));
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
        memset(samples, 0, kTickSamples * sizeof(*samples));
        return false;
    }

    PalMameOpl2_Render(samples, kTickSamples);
    state.rendered_ticks++;
    return true;
}

static void
music_render(
    void *user,
    int16_t *samples,
    size_t sample_count)
{
    MusicRuntime &state = pal_music_runtime;
    bool finish_fade_out = false;
    size_t i;

    (void)user;
    if (samples == nullptr)
    {
        return;
    }
    if (sample_count != kTickSamples)
    {
        const size_t safe_samples =
            sample_count < kTickSamples ? sample_count : kTickSamples;
        memset(samples, 0, safe_samples * sizeof(*samples));
        return;
    }

    music_drain_commands();
    if (state.fade == FadeState::Out && state.fade_remaining == 0)
    {
        (void)music_start_pending();
    }
    (void)music_render_tick_source(samples);

    for (i = 0; i < sample_count; i++)
    {
        int32_t fade_gain = music_fade_gain_q15();
        int32_t gain = static_cast<int32_t>(
            (static_cast<int64_t>(fade_gain) * state.volume_q15 +
             (1 << 14)) >>
            15);
        int32_t sample = static_cast<int32_t>(
            (static_cast<int64_t>(samples[i]) * gain) >> 15);

        samples[i] = static_cast<int16_t>(sample);
        if (music_advance_fade_one_sample())
        {
            finish_fade_out = true;
            memset(
                samples + i + 1,
                0,
                (sample_count - i - 1) * sizeof(*samples));
            break;
        }
    }

    /*
     * Track changes are quantized to the next 70 Hz boundary.  That preserves
     * the first new RIX tick's full 315-sample duration; the maximum extension
     * of a requested fade is one 14.3 ms tick.
     */
    if (finish_fade_out)
    {
        (void)music_start_pending();
    }
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
    pal_music_command_queue = nullptr;
    pal_music_command_drops = 0;
    pal_music_runtime.current_track = -1;
    pal_music_runtime.pending_track = -1;
    gConfig.iMusicVolume = music_clamped_config_volume();
    pal_music_runtime.volume_q15 = music_volume_q15();

    gAudioDevice.spec.freq = static_cast<int>(kSampleRate);
    gAudioDevice.spec.format = AUDIO_S16SYS;
    gAudioDevice.spec.channels = 1;
#if !SDL_VERSION_ATLEAST(3, 0, 0)
    gAudioDevice.spec.samples =
        static_cast<Uint16>(CARDPUTER_EXTREME_AUDIO_TICK_SAMPLES);
#endif
    gAudioDevice.iMusicVolume = music_sdl_volume();
    gAudioDevice.iSoundVolume = 0;
    gAudioDevice.fSoundEnabled = FALSE;

    if (!PalContract_TargetOpenNorPack(&pal_music_runtime.nor_pack))
    {
        ESP_LOGE(kTag, "NOR pack is unavailable to music");
        return -1;
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

    PalMameOpl2_Init();
    if (!CardputerExtremeAudio_Begin(music_render, nullptr))
    {
        ESP_LOGE(kTag, "Cardputer audio backend failed");
        music_stop_now();
        pal_music_command_queue = nullptr;
        return -3;
    }

    gAudioDevice.fOpened = TRUE;
    gAudioDevice.fMusicEnabled = TRUE;
    ESP_LOGI(
        kTag,
        "fixed RIX music ready: rate=%u OPL_state=%u OPL_tables=%u",
        static_cast<unsigned>(kSampleRate),
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
        pal_music_command_queue = nullptr;
        gAudioDevice.fMusicEnabled = FALSE;
        return;
    }

    if (!CardputerExtremeAudio_Stop())
    {
        ESP_LOGE(kTag, "audio close deferred: task is still active");
        return;
    }
    CardputerExtremeAudio_LogTelemetry("close");
    ESP_LOGI(
        kTag,
        "RIX stats: ticks=%u loops=%u missing=%u coalesced=%u",
        static_cast<unsigned>(pal_music_runtime.rendered_ticks),
        static_cast<unsigned>(pal_music_runtime.completed_loops),
        static_cast<unsigned>(pal_music_runtime.missing_tracks),
        static_cast<unsigned>(pal_music_command_drops));
    music_stop_now();
    pal_music_command_queue = nullptr;
    gAudioDevice.fOpened = FALSE;
    gAudioDevice.fMusicEnabled = FALSE;
}

SDL_AudioSpec *
AUDIO_GetDeviceSpec(VOID)
{
    return &gAudioDevice.spec;
}

VOID
AUDIO_IncreaseVolume(VOID)
{
    MusicCommand command = {};
    int volume = music_clamped_config_volume();

    if (PAL_MAX_VOLUME - volume < 3)
    {
        volume = PAL_MAX_VOLUME;
    }
    else
    {
        volume += 3;
    }
    gConfig.iMusicVolume = volume;
    gAudioDevice.iMusicVolume = music_sdl_volume();
    command.type = CommandType::Volume;
    command.volume_q15 = music_volume_q15();
    if (gAudioDevice.fOpened)
    {
        (void)music_send_command(command, false);
    }
}

VOID
AUDIO_DecreaseVolume(VOID)
{
    MusicCommand command = {};
    int volume = music_clamped_config_volume();

    if (volume < 3)
    {
        volume = 0;
    }
    else
    {
        volume -= 3;
    }
    gConfig.iMusicVolume = volume;
    gAudioDevice.iMusicVolume = music_sdl_volume();
    command.type = CommandType::Volume;
    command.volume_q15 = music_volume_q15();
    if (gAudioDevice.fOpened)
    {
        (void)music_send_command(command, false);
    }
}

VOID
AUDIO_PlayMusic(
    INT track,
    BOOL loop,
    FLOAT fade_time)
{
    MusicCommand command = {};

    if (!gAudioDevice.fOpened ||
        (track > 0 && !gAudioDevice.fMusicEnabled))
    {
        return;
    }
    if (track > INT16_MAX)
    {
        ESP_LOGE(kTag, "RIX track out of range: %d", track);
        return;
    }
    command.type = CommandType::Play;
    command.track =
        track > 0 ? static_cast<int16_t>(track) : 0;
    command.loop = loop ? 1u : 0u;
    command.volume_q15 = music_volume_q15();
    command.half_fade_samples = music_half_fade_samples(fade_time);
    (void)music_send_command(command, true);
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
    (void)sound;
}

VOID
AUDIO_EnableMusic(BOOL enable)
{
    gAudioDevice.fMusicEnabled = enable ? TRUE : FALSE;
    if (!enable)
    {
        AUDIO_PlayMusic(0, FALSE, 0.0f);
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
    (void)enable;
    gAudioDevice.fSoundEnabled = FALSE;
}

BOOL
AUDIO_SoundEnabled(VOID)
{
    return FALSE;
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
