/* Fixed-memory RIX/OPL2 background music for Nintendo DS. */

#include "../../audio.h"
#include "../../palcfg.h"
#include "../../palcommon.h"
#include "../../adplug/rix.h"
#include "../../embedded/pal_mame_opl2_static.h"
#include "pal_target_memory.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

extern "C"
{
uint8_t pal_nds_track[PAL_NDS_RIX_TRACK_BYTES]
   __attribute__((aligned(4), section(".bss.pal_nds_music")));
}

namespace
{

constexpr uint32_t kSampleRate = 22050u;
constexpr size_t kTickSamples = 315u;
constexpr int32_t kQ15One = 1 << 15;
constexpr uint32_t kFadePhaseOne = UINT32_C(1) << 31;
constexpr uint32_t kMaximumHalfFadeSamples = kSampleRate * 30u;
constexpr int kEmptyMusTrack = 29;

static_assert(
   kSampleRate == PAL_MAME_OPL2_SAMPLE_RATE &&
      kSampleRate / 70u == kTickSamples,
   "NDS RIX timing must remain one complete 70 Hz tick");

class PalNdsOpl2 final : public Copl
{
public:
   PalNdsOpl2() : Copl(TYPE_OPL2) {}

   void init() override
   {
      PalMameOpl2_Reset();
   }

   void write(int reg, int value) override
   {
      PalMameOpl2_Write(
         static_cast<uint8_t>(reg), static_cast<uint8_t>(value));
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

enum class FadeState : uint8_t
{
   None,
   Out,
   In,
};

struct MusicRuntime
{
   int current_track;
   int pending_track;
   bool current_loop;
   bool pending_loop;
   bool playing;
   bool enabled;
   FadeState fade;
   uint32_t fade_remaining;
   uint32_t fade_in_samples;
   uint32_t fade_phase_q31;
   uint32_t fade_step_base;
   uint32_t fade_step_remainder;
   uint32_t fade_step_error;
   uint32_t fade_step_denominator;
   uint16_t volume_q15;
};

static PalNdsOpl2 pal_nds_opl;
static CrixPlayer pal_nds_decoder(&pal_nds_opl);
static MusicRuntime pal_nds_music;
static int16_t pal_nds_tick[kTickSamples]
   __attribute__((aligned(4), section(".bss.pal_nds_music")));
static size_t pal_nds_tick_pos = kTickSamples;

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
      return 0u;
   }
   samples = seconds * (static_cast<FLOAT>(kSampleRate) * 0.5f);
   if (samples >= static_cast<FLOAT>(kMaximumHalfFadeSamples))
   {
      return kMaximumHalfFadeSamples;
   }
   return static_cast<uint32_t>(samples + 0.5f);
}

static void
music_clear_fade()
{
   pal_nds_music.fade = FadeState::None;
   pal_nds_music.fade_remaining = 0u;
   pal_nds_music.fade_phase_q31 = kFadePhaseOne;
   pal_nds_music.fade_step_base = 0u;
   pal_nds_music.fade_step_remainder = 0u;
   pal_nds_music.fade_step_error = 0u;
   pal_nds_music.fade_step_denominator = 0u;
}

static void
music_stop_now()
{
   pal_nds_music.current_track = -1;
   pal_nds_music.pending_track = -1;
   pal_nds_music.current_loop = false;
   pal_nds_music.pending_loop = false;
   pal_nds_music.playing = false;
   pal_nds_music.fade_in_samples = 0u;
   music_clear_fade();
   PalMameOpl2_Reset();
}

static void
music_configure_fade(
   FadeState fade,
   uint32_t total_samples,
   uint32_t phase_q31)
{
   const uint32_t distance = fade == FadeState::Out
      ? phase_q31
      : kFadePhaseOne - phase_q31;
   uint32_t remaining;

   pal_nds_music.fade = fade;
   pal_nds_music.fade_phase_q31 = phase_q31;
   pal_nds_music.fade_step_error = 0u;
   if (total_samples == 0u || distance == 0u)
   {
      pal_nds_music.fade_remaining = 0u;
      pal_nds_music.fade_step_base = 0u;
      pal_nds_music.fade_step_remainder = 0u;
      pal_nds_music.fade_step_denominator = 0u;
      if (fade == FadeState::Out)
      {
         pal_nds_music.fade_phase_q31 = 0u;
      }
      else
      {
         music_clear_fade();
      }
      return;
   }

   remaining = static_cast<uint32_t>(
      (static_cast<uint64_t>(total_samples) * distance +
       kFadePhaseOne - 1u) >> 31);
   if (remaining == 0u)
   {
      remaining = 1u;
   }
   else if (remaining > total_samples)
   {
      remaining = total_samples;
   }
   pal_nds_music.fade_remaining = remaining;
   pal_nds_music.fade_step_base = distance / remaining;
   pal_nds_music.fade_step_remainder = distance % remaining;
   pal_nds_music.fade_step_denominator = remaining;
}

static bool
music_advance_fade_one_sample()
{
   uint32_t step;

   if (pal_nds_music.fade == FadeState::None ||
      pal_nds_music.fade_remaining == 0u ||
      pal_nds_music.fade_step_denominator == 0u)
   {
      return false;
   }
   step = pal_nds_music.fade_step_base;
   pal_nds_music.fade_step_error += pal_nds_music.fade_step_remainder;
   if (pal_nds_music.fade_step_error >=
      pal_nds_music.fade_step_denominator)
   {
      pal_nds_music.fade_step_error -=
         pal_nds_music.fade_step_denominator;
      step++;
   }

   if (pal_nds_music.fade == FadeState::Out)
   {
      pal_nds_music.fade_phase_q31 =
         step >= pal_nds_music.fade_phase_q31
            ? 0u : pal_nds_music.fade_phase_q31 - step;
   }
   else
   {
      pal_nds_music.fade_phase_q31 =
         step >= kFadePhaseOne - pal_nds_music.fade_phase_q31
            ? kFadePhaseOne : pal_nds_music.fade_phase_q31 + step;
   }

   pal_nds_music.fade_remaining--;
   if (pal_nds_music.fade_remaining != 0u)
   {
      return false;
   }
   if (pal_nds_music.fade == FadeState::Out)
   {
      pal_nds_music.fade_phase_q31 = 0u;
      return true;
   }
   music_clear_fade();
   return false;
}

static bool
music_load_track(
   int track)
{
   FILE *mus;
   int size;

   if (track <= 0 || track > UINT16_MAX)
   {
      return false;
   }
   mus = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_MUS);
   if (mus == nullptr)
   {
      return false;
   }
   size = PAL_MKFReadChunk(
      pal_nds_track,
      sizeof(pal_nds_track),
      static_cast<uint16_t>(track),
      mus);
   return size > 0 && pal_nds_decoder.load_buffer(
      pal_nds_track, static_cast<uint32_t>(size));
}

static bool
music_start_pending()
{
   const int track = pal_nds_music.pending_track;
   const bool loop = pal_nds_music.pending_loop;
   const uint32_t fade_in = pal_nds_music.fade_in_samples;

   pal_nds_music.pending_track = -1;
   pal_nds_music.pending_loop = false;
   pal_nds_music.fade_in_samples = 0u;
   if (track <= 0)
   {
      music_stop_now();
      return true;
   }
   if (!music_load_track(track))
   {
      music_stop_now();
      return false;
   }

   pal_nds_music.current_track = track;
   pal_nds_music.current_loop = loop;
   pal_nds_music.playing = true;
   if (fade_in == 0u)
   {
      music_clear_fade();
   }
   else
   {
      music_configure_fade(FadeState::In, fade_in, 0u);
   }
   return true;
}

static void
music_apply_play(
   int track,
   bool loop,
   uint32_t half_fade_samples)
{
   if (pal_nds_music.playing && track > 0 &&
      track == pal_nds_music.current_track &&
      pal_nds_music.pending_track < 0)
   {
      pal_nds_music.current_loop = loop;
      return;
   }

   pal_nds_music.pending_track = track;
   pal_nds_music.pending_loop = loop;
   pal_nds_music.fade_in_samples = half_fade_samples;
   if (pal_nds_music.fade == FadeState::Out)
   {
      return;
   }
   if (!pal_nds_music.playing || half_fade_samples == 0u)
   {
      (void)music_start_pending();
      return;
   }
   music_configure_fade(
      FadeState::Out,
      half_fade_samples,
      pal_nds_music.fade == FadeState::None
         ? kFadePhaseOne : pal_nds_music.fade_phase_q31);
}

static bool
music_render_source(
   int16_t *samples)
{
   bool retried_loop = false;

   if (!pal_nds_music.playing)
   {
      memset(samples, 0, kTickSamples * sizeof(*samples));
      return false;
   }
   for (;;)
   {
      if (pal_nds_decoder.update())
      {
         break;
      }
      if (pal_nds_music.current_loop && !retried_loop)
      {
         retried_loop = true;
         pal_nds_decoder.rewindReInit(0, false);
         if (pal_nds_decoder.update())
         {
            break;
         }
      }
      if (pal_nds_music.pending_track >= 0)
      {
         retried_loop = false;
         if (music_start_pending() && pal_nds_music.playing)
         {
            continue;
         }
      }
      music_stop_now();
      memset(samples, 0, kTickSamples * sizeof(*samples));
      return false;
   }
   PalMameOpl2_Render(samples, kTickSamples);
   return true;
}

static void
music_render_tick(
   int16_t *samples)
{
   bool finish_fade_out = false;

   if (!pal_nds_music.enabled || pal_nds_music.volume_q15 == 0u)
   {
      memset(samples, 0, kTickSamples * sizeof(*samples));
      return;
   }
   if (pal_nds_music.fade == FadeState::Out &&
      pal_nds_music.fade_remaining == 0u)
   {
      (void)music_start_pending();
   }
   (void)music_render_source(samples);

   for (size_t i = 0u; i < kTickSamples; i++)
   {
      const int32_t fade_gain = pal_nds_music.fade == FadeState::None
         ? kQ15One
         : static_cast<int32_t>(pal_nds_music.fade_phase_q31 >> 16);
      const int32_t gain = static_cast<int32_t>(
         (static_cast<int64_t>(fade_gain) * pal_nds_music.volume_q15 +
          (1 << 14)) >> 15);

      samples[i] = static_cast<int16_t>(
         (static_cast<int64_t>(samples[i]) * gain) >> 15);
      if (music_advance_fade_one_sample())
      {
         finish_fade_out = true;
         memset(
            samples + i + 1,
            0,
            (kTickSamples - i - 1u) * sizeof(*samples));
         break;
      }
   }
   if (finish_fade_out)
   {
      (void)music_start_pending();
   }
}

static void
music_stream_render(
   void *user,
   int16_t *samples,
   size_t sample_count)
{
   (void)user;
   while (sample_count != 0u)
   {
      size_t available;
      size_t amount;

      if (pal_nds_tick_pos == kTickSamples)
      {
         music_render_tick(pal_nds_tick);
         pal_nds_tick_pos = 0u;
      }
      available = kTickSamples - pal_nds_tick_pos;
      amount = sample_count < available ? sample_count : available;
      memcpy(
         samples,
         pal_nds_tick + pal_nds_tick_pos,
         amount * sizeof(*samples));
      samples += amount;
      sample_count -= amount;
      pal_nds_tick_pos += amount;
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
   memset(&pal_nds_music, 0, sizeof(pal_nds_music));
   memset(pal_nds_track, 0, sizeof(pal_nds_track));
   memset(pal_nds_tick, 0, sizeof(pal_nds_tick));
   pal_nds_tick_pos = kTickSamples;
   pal_nds_music.current_track = -1;
   pal_nds_music.pending_track = -1;
   pal_nds_music.enabled = true;
   pal_nds_music.fade_phase_q31 = kFadePhaseOne;
   gConfig.iMusicVolume = music_clamped_config_volume();
   pal_nds_music.volume_q15 = music_volume_q15();

   PalMameOpl2_Init();

   gAudioDevice.spec.freq = static_cast<int>(kSampleRate);
   gAudioDevice.spec.format = AUDIO_S16SYS;
   gAudioDevice.spec.channels = 1;
#if !SDL_VERSION_ATLEAST(3, 0, 0)
   gAudioDevice.spec.samples = static_cast<Uint16>(kTickSamples);
#endif
   gAudioDevice.iMusicVolume = music_sdl_volume();
   gAudioDevice.iSoundVolume = 0;
   gAudioDevice.fSoundEnabled = FALSE;
   gAudioDevice.fMusicEnabled = TRUE;

   if (!NdsTarget_AudioStart(music_stream_render, nullptr))
   {
      gAudioDevice.fMusicEnabled = FALSE;
      return -1;
   }
   gAudioDevice.fOpened = TRUE;
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
   NdsTarget_AudioStop();
   music_stop_now();
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
   int volume = music_clamped_config_volume();

   volume = PAL_MAX_VOLUME - volume < 3
      ? PAL_MAX_VOLUME : volume + 3;
   gConfig.iMusicVolume = volume;
   gAudioDevice.iMusicVolume = music_sdl_volume();
   pal_nds_music.volume_q15 = music_volume_q15();
}

VOID
AUDIO_DecreaseVolume(VOID)
{
   int volume = music_clamped_config_volume();

   volume = volume < 3 ? 0 : volume - 3;
   gConfig.iMusicVolume = volume;
   gAudioDevice.iMusicVolume = music_sdl_volume();
   pal_nds_music.volume_q15 = music_volume_q15();
}

VOID
AUDIO_PlayMusic(
   INT track,
   BOOL loop,
   FLOAT fade_time)
{
   if (!gAudioDevice.fOpened || track > INT16_MAX)
   {
      return;
   }
   music_apply_play(
      track > 0 && track != kEmptyMusTrack ? track : 0,
      loop != FALSE,
      music_half_fade_samples(fade_time));
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
   pal_nds_music.enabled = enable != FALSE;
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

void AUDIO_Lock(void) {}
void AUDIO_Unlock(void) {}

} // extern "C"
