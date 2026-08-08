/* Fixed-memory RIX/DBOPL2 music produced by a peer-priority ARM9 thread. */

#include "../../audio.h"
#include "../../palcfg.h"
#include "../../palcommon.h"
#include "../../adplug/rix.h"
#include "nds_dbopl2.h"
#include "pal_target_memory.h"

#include <calico.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

extern "C"
{
uint8_t pal_nds_track[PAL_NDS_RIX_TRACK_BYTES]
   __attribute__((aligned(4), section(".bss.pal_nds_music")));
PalNdsOplWrite pal_nds_opl_staging[PAL_NDS_OPL_WRITES_PER_TICK]
   __attribute__((aligned(4), section(".bss.pal_nds_music")));
PalNdsOplTick pal_nds_opl_tick_queue[PAL_NDS_OPL_TICK_QUEUE_LENGTH]
   __attribute__((aligned(4), section(".bss.pal_nds_music")));
volatile uint32_t pal_nds_opl_queue_overruns
   __attribute__((section(".bss.pal_nds_music")));
volatile uint32_t pal_nds_opl_queue_underruns
   __attribute__((section(".bss.pal_nds_music")));
volatile uint32_t pal_nds_dbopl_render_ticks_total
   __attribute__((section(".bss.pal_nds_music")));
volatile uint32_t pal_nds_dbopl_render_ticks_max
   __attribute__((section(".bss.pal_nds_music")));
volatile uint32_t pal_nds_dbopl_render_ticks_min
   __attribute__((section(".bss.pal_nds_music")));
volatile uint32_t pal_nds_dbopl_render_calls
   __attribute__((section(".bss.pal_nds_music")));
}

namespace
{

constexpr uint32_t kRixTicksPerSecond = PAL_NDS_OPL_TICK_RATE;
constexpr size_t kOplTickSamples = PAL_NDS_OPL_TICK_SAMPLES;
constexpr size_t kAudioTickSamples = PAL_NDS_AUDIO_TICK_SAMPLES;
constexpr uint16_t kProducerTicksPerPump = PAL_NDS_OPL_PRODUCER_BATCH;
constexpr uint32_t kMaximumHalfFadeTicks = kRixTicksPerSecond * 30u;
constexpr int kEmptyMusTrack = 29;

static_assert(
   PAL_NDS_AUDIO_SAMPLE_RATE == PAL_NDS_OPL_SAMPLE_RATE &&
      kOplTickSamples ==
         (PAL_NDS_OPL_SAMPLE_RATE + kRixTicksPerSecond - 1u) /
            kRixTicksPerSecond,
   "DBOPL2 must render every 32.768 kHz PCM frame directly");
static_assert(
   kAudioTickSamples == 256u,
   "the fixed PCM ring uses native 256-frame service blocks");
static_assert(
   (PAL_NDS_OPL_TICK_QUEUE_LENGTH &
      (PAL_NDS_OPL_TICK_QUEUE_LENGTH - 1u)) == 0u,
   "OPL tick queue length must remain a power of two");
static_assert(
   kProducerTicksPerPump <= PAL_NDS_OPL_TICK_QUEUE_LENGTH &&
      PAL_NDS_OPL_STARTUP_TICKS <= PAL_NDS_OPL_TICK_QUEUE_LENGTH,
   "bounded RIX production must fit the fixed OPL queue");
static_assert(
   sizeof(PalNdsOplTick) ==
      4u + PAL_NDS_OPL_WRITES_PER_TICK * sizeof(PalNdsOplWrite),
   "OPL tick queue layout changed unexpectedly");

static volatile uint32_t pal_nds_opl_queue_head;
static volatile uint32_t pal_nds_opl_queue_tail;

class PalNdsQueuedOpl final : public Copl
{
public:
   PalNdsQueuedOpl() : Copl(TYPE_OPL2) {}

   void clear()
   {
      staging_count_ = 0u;
      reset_pending_ = false;
      staging_overflow_ = false;
   }

   void init() override
   {
      staging_count_ = 0u;
      reset_pending_ = true;
      staging_overflow_ = false;
   }

   void write(int reg, int value) override
   {
      if (staging_count_ >= PAL_NDS_OPL_WRITES_PER_TICK)
      {
         staging_overflow_ = true;
         return;
      }
      pal_nds_opl_staging[staging_count_].reg = static_cast<uint8_t>(reg);
      pal_nds_opl_staging[staging_count_].value = static_cast<uint8_t>(value);
      staging_count_++;
   }

   void update(short *samples, int frames) override
   {
      (void)samples;
      (void)frames;
   }

   bool getstereo() override
   {
      return false;
   }

   void commit_tick(unsigned volume)
   {
      const uint32_t head = pal_nds_opl_queue_head;
      PalNdsOplTick *tick;

      if (head - pal_nds_opl_queue_tail >= PAL_NDS_OPL_TICK_QUEUE_LENGTH)
      {
         pal_nds_opl_queue_overruns++;
         staging_count_ = 0u;
         staging_overflow_ = false;
         return;
      }
      tick = &pal_nds_opl_tick_queue[
         head & (PAL_NDS_OPL_TICK_QUEUE_LENGTH - 1u)];
      if (staging_overflow_)
      {
         /* An incomplete OPL transaction is worse than a silent reset. */
         tick->count = 0u;
         tick->reset = 1u;
         tick->volume = 0u;
      }
      else
      {
         tick->count = staging_count_;
         tick->reset = reset_pending_ ? 1u : 0u;
         tick->volume = static_cast<uint8_t>(volume > 127u ? 127u : volume);
         memcpy(
            tick->writes,
            pal_nds_opl_staging,
            staging_count_ * sizeof(pal_nds_opl_staging[0]));
      }
      armCompilerBarrier();
      pal_nds_opl_queue_head = head + 1u;
      staging_count_ = 0u;
      reset_pending_ = false;
      staging_overflow_ = false;
   }

private:
   uint16_t staging_count_ = 0u;
   bool reset_pending_ = false;
   bool staging_overflow_ = false;
};

enum class FadeState : uint8_t
{
   None,
   Out,
   In,
};

struct MusicRuntime
{
   volatile uint16_t pending_ticks;
   int current_track;
   int pending_track;
   bool current_loop;
   bool pending_loop;
   bool playing;
   bool enabled;
   bool pumping;
   FadeState fade;
   uint32_t fade_remaining;
   uint32_t fade_total;
   uint32_t fade_in_ticks;
   uint32_t fade_level_q16;
   uint32_t fade_step_q16;
};

static PalNdsQueuedOpl pal_nds_opl;
static CrixPlayer pal_nds_decoder(&pal_nds_opl);
static MusicRuntime pal_nds_music;
static uint8_t pal_nds_worker_volume;
static uint32_t pal_nds_rix_sample_phase;
static uint32_t pal_nds_rix_samples_remaining;

constexpr uint32_t kAudioTimer =
   soundTimerFromHz(PAL_NDS_AUDIO_SAMPLE_RATE);
constexpr uint32_t kRixSampleDenominator =
   kAudioTimer * kRixTicksPerSecond;
constexpr uint32_t kRixSampleBase =
   SOUND_CLOCK / kRixSampleDenominator;
constexpr uint32_t kRixSampleRemainder =
   SOUND_CLOCK % kRixSampleDenominator;

static_assert(
   kRixSampleBase != 0u && kRixSampleBase + 1u <= kOplTickSamples,
   "one physical 70 Hz RIX interval must fit the fixed OPL scratch owner");

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

static int
music_sdl_volume()
{
   return music_clamped_config_volume() *
      SDL_MIX_MAXVOLUME / PAL_MAX_VOLUME;
}

static uint32_t
music_half_fade_ticks(FLOAT seconds)
{
   FLOAT ticks;

   if (!(seconds > 0.0f))
   {
      return 0u;
   }
   ticks = seconds * (static_cast<FLOAT>(kRixTicksPerSecond) * 0.5f);
   if (ticks >= static_cast<FLOAT>(kMaximumHalfFadeTicks))
   {
      return kMaximumHalfFadeTicks;
   }
   return static_cast<uint32_t>(ticks + 0.5f);
}

static void
music_clear_fade()
{
   pal_nds_music.fade = FadeState::None;
   pal_nds_music.fade_remaining = 0u;
   pal_nds_music.fade_total = 0u;
   pal_nds_music.fade_level_q16 = 127u << 16;
   pal_nds_music.fade_step_q16 = 0u;
}

static void
music_stop_now()
{
   pal_nds_music.current_track = -1;
   pal_nds_music.pending_track = -1;
   pal_nds_music.current_loop = false;
   pal_nds_music.pending_loop = false;
   pal_nds_music.playing = false;
   pal_nds_music.fade_in_ticks = 0u;
   music_clear_fade();
   pal_nds_opl.init();
}

static void
music_configure_fade(
   FadeState fade,
   uint32_t ticks,
   uint16_t level)
{
   pal_nds_music.fade = fade;
   pal_nds_music.fade_remaining = ticks;
   pal_nds_music.fade_total = ticks;
   pal_nds_music.fade_level_q16 = static_cast<uint32_t>(level) << 16;
   pal_nds_music.fade_step_q16 = ticks != 0u
      ? ((127u << 16) + ticks - 1u) / ticks : 0u;
   if (ticks == 0u)
   {
      pal_nds_music.fade_level_q16 =
         fade == FadeState::Out ? 0u : 127u << 16;
   }
}

static bool
music_load_track(int track)
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
   /*
    * RIX byte 2 selects OPL2 rhythm mode.  Its six-operator percussion
    * renderer exceeds the ARM9 budget, so this target rejects those tracks
    * before any OPL writes reach the audio worker.
    */
   return size > 2 && pal_nds_track[2] == 0u && pal_nds_decoder.load_buffer(
      pal_nds_track, static_cast<uint32_t>(size));
}

static bool
music_start_pending()
{
   const int track = pal_nds_music.pending_track;
   const bool loop = pal_nds_music.pending_loop;
   const uint32_t fade_in = pal_nds_music.fade_in_ticks;

   pal_nds_music.pending_track = -1;
   pal_nds_music.pending_loop = false;
   pal_nds_music.fade_in_ticks = 0u;
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
   uint32_t half_fade_ticks)
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
   pal_nds_music.fade_in_ticks = half_fade_ticks;
   if (pal_nds_music.fade == FadeState::Out)
   {
      return;
   }
   if (!pal_nds_music.playing || half_fade_ticks == 0u)
   {
      (void)music_start_pending();
      return;
   }
   music_configure_fade(
      FadeState::Out,
      half_fade_ticks,
      pal_nds_music.fade == FadeState::None
         ? 127u : static_cast<uint16_t>(
            pal_nds_music.fade_level_q16 >> 16));
}

static bool
music_advance_decoder()
{
   bool retried_loop = false;

   if (!pal_nds_music.playing)
   {
      return false;
   }
   for (;;)
   {
      if (pal_nds_decoder.update())
      {
         return true;
      }
      if (pal_nds_music.current_loop && !retried_loop)
      {
         retried_loop = true;
         pal_nds_decoder.rewindReInit(0, false);
         if (pal_nds_decoder.update())
         {
            return true;
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
      return false;
   }
}

static bool
music_advance_fade()
{
   if (pal_nds_music.fade == FadeState::None ||
      pal_nds_music.fade_total == 0u ||
      pal_nds_music.fade_remaining == 0u)
   {
      return false;
   }
   pal_nds_music.fade_remaining--;
   if (pal_nds_music.fade == FadeState::Out)
   {
      pal_nds_music.fade_level_q16 =
         pal_nds_music.fade_step_q16 >= pal_nds_music.fade_level_q16
            ? 0u
            : pal_nds_music.fade_level_q16 - pal_nds_music.fade_step_q16;
   }
   else
   {
      const uint32_t maximum = 127u << 16;

      pal_nds_music.fade_level_q16 =
         pal_nds_music.fade_step_q16 >=
            maximum - pal_nds_music.fade_level_q16
            ? maximum
            : pal_nds_music.fade_level_q16 + pal_nds_music.fade_step_q16;
   }

   if (pal_nds_music.fade_remaining != 0u)
   {
      return false;
   }
   if (pal_nds_music.fade == FadeState::Out)
   {
      pal_nds_music.fade_level_q16 = 0u;
      return true;
   }
   music_clear_fade();
   return false;
}

static unsigned
music_mixer_volume()
{
   const uint32_t configured = static_cast<uint32_t>(
      music_clamped_config_volume());
   const uint32_t fade_level = pal_nds_music.fade_level_q16 >> 16;

   return static_cast<unsigned>(
      configured * fade_level /
      static_cast<uint32_t>(PAL_MAX_VOLUME));
}

static void
music_tick()
{
   if (!pal_nds_music.enabled || music_clamped_config_volume() == 0)
   {
      pal_nds_opl.commit_tick(0u);
      return;
   }
   if (pal_nds_music.fade == FadeState::Out &&
      pal_nds_music.fade_remaining == 0u)
   {
      (void)music_start_pending();
   }
   (void)music_advance_decoder();
   pal_nds_opl.commit_tick(music_mixer_volume());
   if (music_advance_fade())
   {
      (void)music_start_pending();
   }
}

static bool
music_worker_pop_tick()
{
   const uint32_t tail = pal_nds_opl_queue_tail;
   PalNdsOplTick *tick;
   uint16_t count;

   if (tail == pal_nds_opl_queue_head)
   {
      pal_nds_opl_queue_underruns++;
      return false;
   }
   armCompilerBarrier();
   tick = &pal_nds_opl_tick_queue[
      tail & (PAL_NDS_OPL_TICK_QUEUE_LENGTH - 1u)];
   count = tick->count;
   if (tick->reset != 0u)
   {
      NdsDbOpl2_Reset();
   }
   for (uint16_t i = 0u; i < count; i++)
   {
      NdsDbOpl2_Write(tick->writes[i].reg, tick->writes[i].value);
   }
   pal_nds_worker_volume = tick->volume;
   armCompilerBarrier();
   pal_nds_opl_queue_tail = tail + 1u;
   return true;
}

static uint32_t
music_next_rix_sample_count()
{
   uint32_t result = kRixSampleBase;

   pal_nds_rix_sample_phase += kRixSampleRemainder;
   if (pal_nds_rix_sample_phase >= kRixSampleDenominator)
   {
      pal_nds_rix_sample_phase -= kRixSampleDenominator;
      result++;
   }
   return result;
}

static void
music_request_ticks(uint16_t count)
{
   ArmIrqState irq_state = armIrqLockByPsr();
   const uint16_t available = static_cast<uint16_t>(
      UINT16_MAX - pal_nds_music.pending_ticks);

   pal_nds_music.pending_ticks += count > available ? available : count;
   armIrqUnlockByPsr(irq_state);
}

static void
music_stream_render(
   void *user,
   int16_t *samples,
   size_t sample_count)
{
   uint32_t render_ticks_total = 0u;

   (void)user;
   if (sample_count % kAudioTickSamples != 0u)
   {
      memset(samples, 0, sample_count * sizeof(*samples));
      return;
   }
   while (sample_count != 0u)
   {
      size_t amount;
      const uint64_t render_start = tickGetCount();

      if (pal_nds_rix_samples_remaining == 0u)
      {
         (void)music_worker_pop_tick();
         music_request_ticks(1u);
         pal_nds_rix_samples_remaining = music_next_rix_sample_count();
      }
      amount = sample_count < pal_nds_rix_samples_remaining
         ? sample_count : pal_nds_rix_samples_remaining;
      NdsDbOpl2_Render(samples, amount, pal_nds_worker_volume);
      render_ticks_total += static_cast<uint32_t>(
         tickGetCount() - render_start);
      pal_nds_rix_samples_remaining -= static_cast<uint32_t>(amount);
      samples += amount;
      sample_count -= amount;
   }
   pal_nds_dbopl_render_ticks_total += render_ticks_total;
   if (render_ticks_total > pal_nds_dbopl_render_ticks_max)
   {
      pal_nds_dbopl_render_ticks_max = render_ticks_total;
   }
   if (render_ticks_total < pal_nds_dbopl_render_ticks_min)
   {
      pal_nds_dbopl_render_ticks_min = render_ticks_total;
   }
   pal_nds_dbopl_render_calls++;
}

} // namespace

extern "C" {

AUDIODEVICE gAudioDevice;

void
NdsTarget_AudioPump(void)
{
   ArmIrqState irq_state;
   uint16_t ticks;

   if (!gAudioDevice.fOpened || pal_nds_music.pumping)
   {
      return;
   }
   irq_state = armIrqLockByPsr();
   ticks = pal_nds_music.pending_ticks;
   if (ticks > kProducerTicksPerPump)
   {
      ticks = kProducerTicksPerPump;
   }
   pal_nds_music.pending_ticks -= ticks;
   armIrqUnlockByPsr(irq_state);

   pal_nds_music.pumping = true;
   while (ticks-- != 0u)
   {
      music_tick();
   }
   pal_nds_music.pumping = false;
}

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
   memset(pal_nds_opl_staging, 0, sizeof(pal_nds_opl_staging));
   memset(pal_nds_opl_tick_queue, 0, sizeof(pal_nds_opl_tick_queue));
   pal_nds_opl_queue_head = 0u;
   pal_nds_opl_queue_tail = 0u;
   pal_nds_opl_queue_overruns = 0u;
   pal_nds_opl_queue_underruns = 0u;
   pal_nds_dbopl_render_ticks_total = 0u;
   pal_nds_dbopl_render_ticks_max = 0u;
   pal_nds_dbopl_render_ticks_min = UINT32_MAX;
   pal_nds_dbopl_render_calls = 0u;
   pal_nds_worker_volume = 0u;
   pal_nds_rix_sample_phase = 0u;
   pal_nds_rix_samples_remaining = 0u;
   pal_nds_opl.clear();

   pal_nds_music.current_track = -1;
   pal_nds_music.pending_track = -1;
   pal_nds_music.enabled = true;
   pal_nds_music.fade_level_q16 = 127u << 16;
   gConfig.iMusicVolume = music_clamped_config_volume();

   NdsDbOpl2_Init();

   gAudioDevice.spec.freq = static_cast<int>(PAL_NDS_AUDIO_SAMPLE_RATE);
   gAudioDevice.spec.format = AUDIO_S16SYS;
   gAudioDevice.spec.channels = 1;
#if !SDL_VERSION_ATLEAST(3, 0, 0)
   gAudioDevice.spec.samples = static_cast<Uint16>(kAudioTickSamples);
#endif
   gAudioDevice.iMusicVolume = music_sdl_volume();
   gAudioDevice.iSoundVolume = 0;
   gAudioDevice.fSoundEnabled = FALSE;
   gAudioDevice.fMusicEnabled = TRUE;
   gAudioDevice.fOpened = TRUE;

   /* Seed a bounded 20-tick lookahead before the worker starts. */
   pal_nds_music.pending_ticks = PAL_NDS_OPL_STARTUP_TICKS;
   NdsTarget_AudioPump();

   if (!NdsTarget_AudioStart(music_stream_render, nullptr))
   {
      gAudioDevice.fOpened = FALSE;
      gAudioDevice.fMusicEnabled = FALSE;
      return -1;
   }
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
      return;
   }
   NdsTarget_AudioStop();
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
}

VOID
AUDIO_DecreaseVolume(VOID)
{
   int volume = music_clamped_config_volume();

   volume = volume < 3 ? 0 : volume - 3;
   gConfig.iMusicVolume = volume;
   gAudioDevice.iMusicVolume = music_sdl_volume();
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
      music_half_fade_ticks(fade_time));
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
