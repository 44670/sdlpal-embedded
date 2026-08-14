/* Fixed-owner ARM946E-S specialization of DOSBox's fast integer OPL2 core. */

#include "nds_dbopl2.h"
#include "pal_target_memory.h"
#include "../../adplug/dosbox/dosbox.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <type_traits>

namespace PalNdsDbOpl2Core
{
#define DB_FASTCALL
#define DBOPL_WAVE 12
#define PAL_DBOPL_OPL2_ONLY 1
#define PAL_DBOPL_DISABLE_PERCUSSION 1
#define PAL_DBOPL_SIMPLE_VOLUME_HANDLER 1
#define PAL_DBOPL_PRECALCULATE_ENVELOPES 1
#define PAL_DBOPL_ENVELOPE_BUFFER_SAMPLES PAL_NDS_OPL_RENDER_SAMPLES
#define PAL_DBOPL_RESTRICT __restrict__
#define PAL_DBOPL_HOT_DATA __attribute__((section(".sbss.pal_nds_dbopl2"), aligned(4)))
#define PAL_DBOPL_MUL_DATA PAL_DBOPL_HOT_DATA
#undef SDLPAL_DBOPL_H
#include "../../adplug/dosbox/dbopl.h"
#include "../../adplug/dosbox/dbopl.cpp.h"
#undef DBOPL_WAVE
#undef DB_FASTCALL
#undef PAL_DBOPL_OPL2_ONLY
#undef PAL_DBOPL_DISABLE_PERCUSSION
#undef PAL_DBOPL_SIMPLE_VOLUME_HANDLER
#undef PAL_DBOPL_PRECALCULATE_ENVELOPES
#undef PAL_DBOPL_ENVELOPE_BUFFER_SAMPLES
#undef PAL_DBOPL_RESTRICT
#undef PAL_DBOPL_HOT_DATA
#undef PAL_DBOPL_MUL_DATA

#if defined(__GNUC__)
#define PAL_NDS_DBOPL2_DTCM_ATTR \
   __attribute__((section(".sbss.pal_nds_dbopl2"), aligned(8)))
#define PAL_NDS_DBOPL2_STATE_ATTR \
   __attribute__((section(".bss.pal_nds_dbopl2"), aligned(8)))
#else
#define PAL_NDS_DBOPL2_DTCM_ATTR
#define PAL_NDS_DBOPL2_STATE_ATTR
#endif

Chip pal_nds_dbopl2_state PAL_NDS_DBOPL2_DTCM_ATTR;
Chip pal_nds_dbopl2_reset_state PAL_NDS_DBOPL2_STATE_ATTR;
Bit32s pal_nds_dbopl2_scratch[PAL_NDS_OPL_RENDER_SAMPLES]
   PAL_NDS_DBOPL2_DTCM_ATTR;
static bool pal_nds_dbopl2_ready;

static inline int16_t
finalize_pcm16(int32_t sample, uint8_t volume)
{
   int32_t value = sample > 32767 ? 32767 :
      sample < -32768 ? -32768 : sample;

   if (volume != 127u)
   {
      value = value * volume / 127;
   }
   value *= 2;
   return static_cast<int16_t>(
      value > 32767 ? 32767 : value < -32768 ? -32768 : value);
}

static inline int16_t
finalize_full_volume_pcm16(int32_t sample)
{
   return static_cast<int16_t>(
      sample > 16383 ? 32767 : sample <= -16384 ? -32768 : sample * 2);
}

static_assert(
   std::is_trivially_copyable<Chip>::value,
   "fixed DBOPL2 state must remain safe to copy without allocation");
static_assert(
   PAL_NDS_AUDIO_UPSAMPLE_FACTOR == 2u,
   "the NDS OPL output path directly duplicates every sample twice");

#undef PAL_NDS_DBOPL2_STATE_ATTR
#undef PAL_NDS_DBOPL2_DTCM_ATTR
}

extern "C" void
NdsDbOpl2_Init(void)
{
   using namespace PalNdsDbOpl2Core;

   if (!pal_nds_dbopl2_ready)
   {
      (void)InitTables();
      pal_nds_dbopl2_reset_state.Setup(PAL_NDS_OPL_SAMPLE_RATE);
      pal_nds_dbopl2_ready = true;
   }
   pal_nds_dbopl2_state = pal_nds_dbopl2_reset_state;
}

extern "C" void
NdsDbOpl2_Reset(void)
{
   PalNdsDbOpl2Core::pal_nds_dbopl2_state =
      PalNdsDbOpl2Core::pal_nds_dbopl2_reset_state;
}

extern "C" void
NdsDbOpl2_Write(uint8_t reg, uint8_t value)
{
   PalNdsDbOpl2Core::pal_nds_dbopl2_state.WriteReg(reg, value);
}

extern "C" void
NdsDbOpl2_Render(
   int16_t *__restrict__ samples,
   size_t frames,
   uint8_t volume)
{
   using namespace PalNdsDbOpl2Core;

   while (frames != 0u)
   {
      const size_t amount = frames < PAL_NDS_OPL_RENDER_SAMPLES
         ? frames : PAL_NDS_OPL_RENDER_SAMPLES;

      pal_nds_dbopl2_state.GenerateBlock2(amount, pal_nds_dbopl2_scratch);
      if (volume == 127u)
      {
         for (size_t i = 0u; i < amount; i++)
         {
            const int16_t sample = finalize_full_volume_pcm16(
               pal_nds_dbopl2_scratch[i]);

            samples[i * 2u] = sample;
            samples[i * 2u + 1u] = sample;
         }
      }
      else
      {
         for (size_t i = 0u; i < amount; i++)
         {
            const int16_t sample = finalize_pcm16(
               pal_nds_dbopl2_scratch[i], volume);

            samples[i * 2u] = sample;
            samples[i * 2u + 1u] = sample;
         }
      }
      samples += amount * 2u;
      frames -= amount;
   }
}
