/*
 * RIX sequencer backed by the Nintendo DS sound channels.
 *
 * ARM9 still decodes the small 70 Hz RIX command stream.  It no longer renders
 * 22.05 kHz OPL PCM.  Instead, OPL register writes update nine compact FM-like
 * wavetables and the Calico ARM7 sound service drives the DS PCM channels.
 * OPL rhythm mode reuses channels 6--8 for three fixed PCM8 percussion loops.
 * This keeps all target memory statically owned and removes sample-by-sample
 * synthesis from the game thread.
 */

#include "../../audio.h"
#include "../../palcfg.h"
#include "../../palcommon.h"
#include "../../adplug/rix.h"
#include "pal_target_memory.h"

#include <calico.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

extern "C"
{
uint8_t pal_nds_track[PAL_NDS_RIX_TRACK_BYTES]
   __attribute__((aligned(4), section(".bss.pal_nds_music")));
int8_t pal_nds_voice_waves
   [PAL_NDS_RIX_MELODIC_CHANNELS][PAL_NDS_RIX_WAVE_BYTES]
   __attribute__((aligned(ARM_CACHE_LINE_SZ), section(".bss.pal_nds_music")));
int8_t pal_nds_rhythm_waves
   [PAL_NDS_RIX_RHYTHM_VOICES][PAL_NDS_RIX_WAVE_BYTES]
   __attribute__((aligned(ARM_CACHE_LINE_SZ), section(".bss.pal_nds_music")));
int16_t pal_nds_sine[PAL_NDS_RIX_SINE_ENTRIES]
   __attribute__((aligned(4), section(".bss.pal_nds_music")));
uint32_t pal_nds_base_timers[PAL_NDS_RIX_TIMER_ENTRIES]
   __attribute__((aligned(4), section(".bss.pal_nds_music")));
uint8_t pal_nds_tl_volume[PAL_NDS_RIX_TL_ENTRIES]
   __attribute__((aligned(4), section(".bss.pal_nds_music")));
}

namespace
{

constexpr uint32_t kRixTicksPerSecond = 70u;
constexpr uint32_t kMaximumHalfFadeTicks = kRixTicksPerSecond * 30u;
constexpr int kEmptyMusTrack = 29;
constexpr unsigned kMelodicChannels = PAL_NDS_RIX_MELODIC_CHANNELS;
constexpr unsigned kDrumChannels = PAL_NDS_RIX_DRUM_CHANNELS;
constexpr unsigned kRhythmVoices = PAL_NDS_RIX_RHYTHM_VOICES;
constexpr unsigned kWaveBytes = PAL_NDS_RIX_WAVE_BYTES;
constexpr unsigned kWaveCycles = 2u;
constexpr uint32_t kOplInternalRate = 49716u;

static_assert((kWaveBytes & 3u) == 0u, "PCM8 loop must be word aligned");
static_assert(SOUND_NUM_CHANNELS == 16u,
   "native RIX voice map assumes the 16-channel DS mixer");

constexpr uint8_t kModulatorSlot[kMelodicChannels] = {
   0u, 1u, 2u, 8u, 9u, 10u, 16u, 17u, 18u,
};
constexpr uint8_t kCarrierSlot[kMelodicChannels] = {
   3u, 4u, 5u, 11u, 12u, 13u, 19u, 20u, 21u,
};
constexpr uint8_t kMultiplierTimesTwo[16] = {
   1u, 2u, 4u, 6u, 8u, 10u, 12u, 14u,
   16u, 18u, 20u, 20u, 24u, 24u, 30u, 30u,
};
constexpr uint8_t kDrumRhythmVoice[kDrumChannels] = {
   0u, 1u, 2u, 2u, 1u,
};
constexpr uint8_t kDrumDurations[kDrumChannels] = {
   8u, 5u, 10u, 12u, 3u,
};
constexpr uint16_t kRhythmSampleRates[kRhythmVoices] = {
   8192u, 16000u, 12000u,
};
struct HardwareVoice
{
   uint16_t last_volume;
   uint16_t last_timer;
   bool held;
   bool active;
   bool key_on_pending;
   bool key_off_pending;
   bool wave_dirty;
   bool pitch_dirty;
   bool volume_dirty;
};

static bool pal_nds_sound_ready;

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
hardware_build_tables()
{
   /* Constant tables: built once, the fixed .bss owners keep them afterwards. */
   static bool built = false;

   if (built)
   {
      return;
   }
   built = true;

   int32_t sine = 0;
   int32_t cosine = 32767;
   uint32_t amplitude = 127u << 16;
   const uint32_t timer_numerator = static_cast<uint32_t>(
      ((static_cast<uint64_t>(SOUND_CLOCK) << 13) +
       kOplInternalRate / 2u) / kOplInternalRate);

   /* Q15 rotation by 2*pi/256; no target-side libm or generated table. */
   for (unsigned i = 0u; i < 256u; i++)
   {
      const int32_t next_sine =
         (sine * 32758 + cosine * 804 + 16384) >> 15;
      const int32_t next_cosine =
         (cosine * 32758 - sine * 804 + 16384) >> 15;

      pal_nds_sine[i] = static_cast<int16_t>(sine >> 8);
      sine = next_sine;
      cosine = next_cosine;
   }

   /* One startup division per F-number; pitch updates become lookup + shift. */
   pal_nds_base_timers[0] = 0u;
   for (unsigned f_number = 1u; f_number < 1024u; f_number++)
   {
      pal_nds_base_timers[f_number] =
         (timer_numerator + f_number / 2u) / f_number;
   }

   /* 0.75 dB per OPL total-level step, approximated in Q16. */
   for (unsigned level = 0u; level < 64u; level++)
   {
      pal_nds_tl_volume[level] = static_cast<uint8_t>(
         (amplitude + (1u << 15)) >> 16);
      amplitude = (amplitude * 60097u + (1u << 15)) >> 16;
   }
}

static void
hardware_build_rhythm_waves()
{
   uint16_t noise = 0x5a3du;

   for (unsigned i = 0u; i < kWaveBytes; i++)
   {
      const int kick = pal_nds_sine[(i * 4u) & 0xffu];
      const unsigned kick_envelope = kWaveBytes - i;
      const int metallic =
         ((i * 5u) & 0x20u ? 70 : -70) +
         ((i * 7u) & 0x40u ? 42 : -42);

      noise = static_cast<uint16_t>(
         (noise >> 1) ^ ((0u - (noise & 1u)) & 0xb400u));
      pal_nds_rhythm_waves[0][i] = static_cast<int8_t>(
         kick * static_cast<int>(kick_envelope) /
         static_cast<int>(kWaveBytes));
      pal_nds_rhythm_waves[1][i] = static_cast<int8_t>(
         static_cast<int8_t>(noise & 0xffu) * 3 / 4);
      pal_nds_rhythm_waves[2][i] = static_cast<int8_t>(metallic);
   }
   armDCacheFlush(pal_nds_rhythm_waves, sizeof(pal_nds_rhythm_waves));
}

static int
hardware_wave_sample(
   unsigned waveform,
   unsigned phase)
{
   const unsigned index = phase & 0xffu;
   const int value = pal_nds_sine[index];

   switch (waveform & 3u)
   {
   case 1u:
      return index < 128u ? value : 0;
   case 2u:
      return value < 0 ? -value : value;
   case 3u:
      return (index & 64u) == 0u
         ? (value < 0 ? -value : value) : 0;
   default:
      return value;
   }
}

class PalNdsHardwareOpl final : public Copl
{
public:
   PalNdsHardwareOpl() : Copl(TYPE_OPL2), pending_drums(0u),
      last_mixer_volume(UINT_MAX), rhythm(false)
   {
      memset(regs, 0, sizeof(regs));
      memset(voices, 0, sizeof(voices));
      memset(rhythm_ticks, 0, sizeof(rhythm_ticks));
   }

   void init() override
   {
      if (pal_nds_sound_ready)
      {
         soundStop((1u << SOUND_NUM_CHANNELS) - 1u);
      }
      memset(regs, 0, sizeof(regs));
      memset(voices, 0, sizeof(voices));
      memset(rhythm_ticks, 0, sizeof(rhythm_ticks));
      for (unsigned channel = 0u; channel < kMelodicChannels; channel++)
      {
         voices[channel].last_volume = UINT16_MAX;
         voices[channel].last_timer = UINT16_MAX;
      }
      pending_drums = 0u;
      last_mixer_volume = UINT_MAX;
      rhythm = false;
   }

   void write(int register_value, int value) override
   {
      const unsigned reg = static_cast<unsigned>(register_value) & 0xffu;
      const uint8_t byte = static_cast<uint8_t>(value);
      const uint8_t old = regs[reg];

      if (old == byte)
      {
         return;
      }
      regs[reg] = byte;

      if (reg >= 0xa0u && reg <= 0xa8u)
      {
         voices[reg - 0xa0u].pitch_dirty = true;
         return;
      }
      if (reg >= 0xb0u && reg <= 0xb8u)
      {
         HardwareVoice &voice = voices[reg - 0xb0u];
         const bool old_held = (old & 0x20u) != 0u;
         const bool new_held = (byte & 0x20u) != 0u;

         voice.pitch_dirty = true;
         voice.held = new_held;
         if (new_held && !old_held)
         {
            voice.key_on_pending = true;
         }
         else if (!new_held && old_held)
         {
            voice.key_off_pending = true;
         }
         return;
      }
      if (reg == 0xbdu)
      {
         const bool old_rhythm = (old & 0x20u) != 0u;
         rhythm = (byte & 0x20u) != 0u;
         if (rhythm)
         {
            pending_drums |= static_cast<uint8_t>(
               byte & static_cast<uint8_t>(~old) & 0x1fu);
         }
         if (rhythm != old_rhythm)
         {
            memset(rhythm_ticks, 0, sizeof(rhythm_ticks));
            for (unsigned channel = 6u;
               channel < kMelodicChannels; channel++)
            {
               voices[channel].wave_dirty = true;
               if (!rhythm && voices[channel].held)
               {
                  voices[channel].key_on_pending = true;
               }
            }
         }
         return;
      }
      if (reg >= 0xc0u && reg <= 0xc8u)
      {
         voices[reg - 0xc0u].wave_dirty = true;
         return;
      }

      mark_operator_change(reg);
   }

   void update(short *, int) override {}

   bool getstereo() override
   {
      return false;
   }

   void commit_tick(unsigned mixer_volume)
   {
      if (mixer_volume > 127u)
      {
         mixer_volume = 127u;
      }
      if (mixer_volume != last_mixer_volume)
      {
         soundSetMixerVolume(mixer_volume);
         last_mixer_volume = mixer_volume;
      }
      for (unsigned channel = 0u; channel < kMelodicChannels; channel++)
      {
         HardwareVoice &voice = voices[channel];

         if (rhythm && channel >= 6u)
         {
            if (voice.active)
            {
               soundStop(1u << channel);
               voice.active = false;
            }
            clear_pending(voice);
            continue;
         }

         apply_note_edges(channel, voice);
         if (voice.active && voice.wave_dirty)
         {
            soundStop(1u << channel);
            prepare_voice(channel, voice, true);
         }
         else if (voice.active && voice.pitch_dirty)
         {
            const uint16_t timer = voice_timer(channel);

            if (timer != 0u && timer != voice.last_timer)
            {
               soundChSetTimer(channel, timer);
               voice.last_timer = timer;
            }
         }
         update_voice_volume(channel, voice);
         clear_pending(voice);
      }
      play_pending_drums();
      soundSynchronize();
   }

   void silence()
   {
      if (last_mixer_volume != 0u)
      {
         soundSetMixerVolume(0u);
         soundSynchronize();
         last_mixer_volume = 0u;
      }
   }

private:
   uint8_t regs[256];
   HardwareVoice voices[kMelodicChannels];
   uint8_t pending_drums;
   uint8_t rhythm_ticks[kRhythmVoices];
   unsigned last_mixer_volume;
   bool rhythm;

   static void clear_pending(HardwareVoice &voice)
   {
      voice.key_on_pending = false;
      voice.key_off_pending = false;
      voice.wave_dirty = false;
      voice.pitch_dirty = false;
      voice.volume_dirty = false;
   }

   void mark_operator_change(unsigned reg)
   {
      const unsigned group = reg & 0xe0u;
      const unsigned slot = reg & 0x1fu;

      if (group != 0x20u && group != 0x40u && group != 0xe0u)
      {
         return;
      }
      for (unsigned channel = 0u; channel < kMelodicChannels; channel++)
      {
         HardwareVoice &voice = voices[channel];

         if (slot == kModulatorSlot[channel])
         {
            if (group == 0x20u || group == 0x40u || group == 0xe0u)
            {
               voice.wave_dirty = true;
            }
            return;
         }
         if (slot == kCarrierSlot[channel])
         {
            if (group == 0x20u || group == 0xe0u)
            {
               voice.wave_dirty = true;
            }
            if (group == 0x40u)
            {
               voice.volume_dirty = true;
            }
            return;
         }
      }
   }

   uint8_t operator_register(
      unsigned base,
      unsigned channel,
      bool carrier) const
   {
      return regs[base + (carrier
         ? kCarrierSlot[channel] : kModulatorSlot[channel])];
   }

   uint16_t voice_timer(unsigned channel) const
   {
      const unsigned f_number = regs[0xa0u + channel] |
         ((static_cast<unsigned>(regs[0xb0u + channel]) & 3u) << 8);
      const unsigned block =
         (static_cast<unsigned>(regs[0xb0u + channel]) >> 2) & 7u;
      uint32_t timer;

      if (f_number == 0u)
      {
         return 0u;
      }
      timer = pal_nds_base_timers[f_number];
      if (block != 0u)
      {
         timer = (timer + (1u << (block - 1u))) >> block;
      }
      if (timer < 2u)
      {
         timer = 2u;
      }
      else if (timer > UINT16_MAX)
      {
         timer = UINT16_MAX;
      }
      return static_cast<uint16_t>(timer);
   }

   int waveform_value(unsigned channel, unsigned sample) const
   {
      const uint8_t mod20 = operator_register(0x20u, channel, false);
      const uint8_t car20 = operator_register(0x20u, channel, true);
      const uint8_t mod40 = operator_register(0x40u, channel, false);
      const uint8_t mod_e0 = operator_register(0xe0u, channel, false);
      const uint8_t car_e0 = operator_register(0xe0u, channel, true);
      const uint8_t connection = regs[0xc0u + channel];
      const unsigned base_phase = sample * kWaveCycles;
      const unsigned mod_phase =
         (base_phase * kMultiplierTimesTwo[mod20 & 0x0fu]) >> 1;
      const int modulator = hardware_wave_sample(mod_e0, mod_phase);
      const unsigned modulation_depth =
         (63u - (mod40 & 0x3fu)) + ((connection >> 1) & 7u) * 4u;
      const int phase_modulation =
         (modulator * static_cast<int>(modulation_depth)) >> 5;
      const unsigned carrier_phase = static_cast<unsigned>(
         static_cast<int>(
            (base_phase * kMultiplierTimesTwo[car20 & 0x0fu]) >> 1) +
         phase_modulation);
      int output = hardware_wave_sample(car_e0, carrier_phase);

      if ((connection & 1u) != 0u)
      {
         output += modulator / 2;
      }
      return output;
   }

   void rebuild_wave(unsigned channel)
   {
      int32_t sum = 0;
      int maximum = 1;

      for (unsigned i = 0u; i < kWaveBytes; i++)
      {
         sum += waveform_value(channel, i);
      }
      const int mean = static_cast<int>(sum / static_cast<int>(kWaveBytes));
      for (unsigned i = 0u; i < kWaveBytes; i++)
      {
         int value = waveform_value(channel, i) - mean;
         const int magnitude = value < 0 ? -value : value;

         if (magnitude > maximum)
         {
            maximum = magnitude;
         }
      }
      const int scale_q8 = (120 << 8) / maximum;
      for (unsigned i = 0u; i < kWaveBytes; i++)
      {
         int value =
            ((waveform_value(channel, i) - mean) * scale_q8) >> 8;

         if (value < -127)
         {
            value = -127;
         }
         else if (value > 127)
         {
            value = 127;
         }
         pal_nds_voice_waves[channel][i] = static_cast<int8_t>(value);
      }
      armDCacheFlush(
         pal_nds_voice_waves[channel],
         sizeof(pal_nds_voice_waves[channel]));
   }

   void apply_note_edges(
      unsigned channel,
      HardwareVoice &voice)
   {
      if (voice.key_on_pending && voice.held)
      {
         if (voice.active)
         {
            soundStop(1u << channel);
         }
         voice.active = true;
         voice.wave_dirty = true;
         voice.pitch_dirty = true;
         voice.volume_dirty = true;
         prepare_voice(channel, voice, false);
      }
      else if (voice.key_off_pending && !voice.held && voice.active)
      {
         soundStop(1u << channel);
         voice.active = false;
      }
   }

   void prepare_voice(
      unsigned channel,
      HardwareVoice &voice,
      bool restart)
   {
      const uint16_t timer = voice_timer(channel);

      if (timer == 0u)
      {
         return;
      }
      if (restart || voice.wave_dirty)
      {
         rebuild_wave(channel);
      }
      voice.last_timer = timer;
      voice.last_volume = UINT16_MAX;
      soundPreparePcm(
         channel | SOUND_START,
         0u,
         64u,
         timer,
         SoundMode_Repeat,
         SoundFmt_Pcm8,
         pal_nds_voice_waves[channel],
         0u,
         kWaveBytes / 4u);
      voice.wave_dirty = false;
      voice.pitch_dirty = false;
   }

   void update_voice_volume(unsigned channel, HardwareVoice &voice)
   {
      const uint8_t car40 = operator_register(0x40u, channel, true);
      uint32_t volume;

      if (!voice.active)
      {
         return;
      }
      volume = static_cast<uint32_t>(pal_nds_tl_volume[car40 & 0x3fu]) << 4;
      if (volume != voice.last_volume)
      {
         soundChSetVolume(channel, volume);
         voice.last_volume = static_cast<uint16_t>(volume);
      }
   }

   uint16_t drum_volume(unsigned drum) const
   {
      static constexpr uint8_t drum_channels[kDrumChannels] = {
         6u, 7u, 8u, 8u, 7u,
      };
      static constexpr bool use_carrier[kDrumChannels] = {
         true, true, false, true, false,
      };
      const uint8_t level = operator_register(
         0x40u, drum_channels[drum], use_carrier[drum]);

      return static_cast<uint16_t>(
         static_cast<uint32_t>(pal_nds_tl_volume[level & 0x3fu]) *
         16u);
   }

   void play_pending_drums()
   {
      static constexpr uint8_t bits[kDrumChannels] = {
         0x10u, 0x08u, 0x04u, 0x02u, 0x01u,
      };
      uint8_t selected_drums[kRhythmVoices] = {
         UINT8_MAX, UINT8_MAX, UINT8_MAX,
      };
      uint32_t stop_mask = 0u;

      if (!rhythm)
      {
         for (unsigned voice = 0u; voice < kRhythmVoices; voice++)
         {
            if (rhythm_ticks[voice] != 0u)
            {
               stop_mask |= 1u << (6u + voice);
            }
         }
         if (stop_mask != 0u)
         {
            soundStop(stop_mask);
         }
         memset(rhythm_ticks, 0, sizeof(rhythm_ticks));
         pending_drums = 0u;
         return;
      }

      for (unsigned voice = 0u; voice < kRhythmVoices; voice++)
      {
         if (rhythm_ticks[voice] != 0u && --rhythm_ticks[voice] == 0u)
         {
            stop_mask |= 1u << (6u + voice);
         }
      }
      for (unsigned drum = 0u; drum < kDrumChannels; drum++)
      {
         if ((pending_drums & bits[drum]) != 0u)
         {
            const unsigned voice = kDrumRhythmVoice[drum];

            selected_drums[voice] = static_cast<uint8_t>(drum);
            stop_mask |= 1u << (6u + voice);
         }
      }
      if (stop_mask != 0u)
      {
         soundStop(stop_mask);
      }

      for (unsigned voice = 0u; voice < kRhythmVoices; voice++)
      {
         const unsigned drum = selected_drums[voice];

         if (drum == UINT8_MAX)
         {
            continue;
         }
         soundPreparePcm(
            (6u + voice) | SOUND_START,
            0u,
            64u,
            soundTimerFromHz(kRhythmSampleRates[voice]),
            SoundMode_Repeat,
            SoundFmt_Pcm8,
            pal_nds_rhythm_waves[voice],
            0u,
            kWaveBytes / 4u);
         soundChSetVolume(6u + voice, drum_volume(drum));
         rhythm_ticks[voice] = kDrumDurations[drum];
      }
      pending_drums = 0u;
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
   TickTask tick_task;
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

static PalNdsHardwareOpl pal_nds_opl;
static CrixPlayer pal_nds_decoder(&pal_nds_opl);
static MusicRuntime pal_nds_music;

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
   return size > 0 && pal_nds_decoder.load_buffer(
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
      pal_nds_opl.silence();
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

static void
music_tick_irq(TickTask *)
{
   if (pal_nds_music.pending_ticks != UINT16_MAX)
   {
      pal_nds_music.pending_ticks++;
   }
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
   pal_nds_music.pending_ticks = 0u;
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
   memset(pal_nds_voice_waves, 0, sizeof(pal_nds_voice_waves));
   memset(pal_nds_rhythm_waves, 0, sizeof(pal_nds_rhythm_waves));

   pal_nds_music.current_track = -1;
   pal_nds_music.pending_track = -1;
   pal_nds_music.enabled = true;
   pal_nds_music.fade_level_q16 = 127u << 16;
   gConfig.iMusicVolume = music_clamped_config_volume();

   tickInit();
   soundInit();
   soundSetPower(true);
   soundSetMixerSleep(false);
   soundSetMixerVolume(127u);
   hardware_build_tables();
   hardware_build_rhythm_waves();
   pal_nds_sound_ready = true;
   pal_nds_opl.init();

   gAudioDevice.spec.freq = static_cast<int>(kRixTicksPerSecond);
   gAudioDevice.spec.format = AUDIO_S16SYS;
   gAudioDevice.spec.channels = 1;
#if !SDL_VERSION_ATLEAST(3, 0, 0)
   gAudioDevice.spec.samples = 1u;
#endif
   gAudioDevice.iMusicVolume = music_sdl_volume();
   gAudioDevice.iSoundVolume = 0;
   gAudioDevice.fSoundEnabled = FALSE;
   gAudioDevice.fMusicEnabled = TRUE;
   gAudioDevice.fOpened = TRUE;

   pal_nds_music.pending_ticks = 1u;
   tickTaskStart(
      &pal_nds_music.tick_task,
      music_tick_irq,
      ticksFromHz(kRixTicksPerSecond),
      ticksFromHz(kRixTicksPerSecond));
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
   tickTaskStop(&pal_nds_music.tick_task);
   music_stop_now();
   soundStop((1u << SOUND_NUM_CHANNELS) - 1u);
   soundSynchronize();
   pal_nds_sound_ready = false;
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
   if (!pal_nds_music.enabled)
   {
      pal_nds_opl.silence();
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

void AUDIO_Lock(void) {}
void AUDIO_Unlock(void) {}

} // extern "C"
