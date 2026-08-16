/* Fixed-memory ARM9 PCM stream for the Calico ARM7 sound service. */

#include "pal_target_board.h"
#include "pal_target_memory.h"

#include <calico.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
   PAL_NDS_AUDIO_CHANNEL = 0u,
   /*
    * Calico does not time-slice equal-priority threads.  Keep the bounded
    * PCM producer one level above the game thread so a long scene/resource
    * load cannot prevent a sleeping audio worker from meeting its deadline.
    */
   PAL_NDS_AUDIO_THREAD_PRIO = MAIN_THREAD_PRIO - 1u,
   PAL_NDS_AUDIO_TIMER =
      (SOUND_CLOCK + PAL_NDS_AUDIO_SAMPLE_RATE / 2u) /
         PAL_NDS_AUDIO_SAMPLE_RATE,
};

_Static_assert(
   MAIN_THREAD_PRIO > THREAD_MAX_PRIO,
   "the audio worker needs one priority level above the main thread");

typedef struct PalNdsAudioStream {
   Thread worker;
   NdsTargetAudioRender render;
   void *user;
   uint64_t start_tick;
   uint32_t tick_period_base;
   uint32_t tick_period_remainder;
   volatile bool running;
} PalNdsAudioStream;

_Static_assert(
   (PAL_NDS_AUDIO_RING_TICKS &
         (PAL_NDS_AUDIO_RING_TICKS - 1u)) == 0u,
   "the PCM ring block count must remain a power of two");
_Static_assert(
   (PAL_NDS_AUDIO_TICK_SAMPLES * PAL_NDS_AUDIO_TIMER) % 32u == 0u,
   "one PCM block must have an integral Calico scheduler period");

int16_t pal_nds_audio_ring[PAL_NDS_AUDIO_RING_SAMPLES]
   __attribute__((aligned(ARM_CACHE_LINE_SZ), section(".bss.pal_nds_audio")));
uint8_t pal_nds_audio_thread_stack[PAL_NDS_AUDIO_THREAD_STACK_BYTES]
   __attribute__((aligned(8), section(".bss.pal_nds_audio")));
volatile uint32_t pal_nds_audio_deadline_misses
   __attribute__((section(".bss.pal_nds_audio")));
static PalNdsAudioStream pal_nds_audio_stream
   __attribute__((section(".bss.pal_nds_audio")));

static uint32_t
pal_nds_audio_next_tick_period(
   uint32_t *phase)
{
   uint32_t period = pal_nds_audio_stream.tick_period_base;

   *phase += pal_nds_audio_stream.tick_period_remainder;
   if (*phase >= 32u)
   {
      *phase -= 32u;
      period++;
   }
   return period;
}

static int
pal_nds_audio_worker(
   void *arg)
{
   uint64_t deadline = pal_nds_audio_stream.start_tick;
   uint32_t playback_tick = 0u;
   uint32_t handled_tick = 0u;
   uint32_t phase = 0u;

   (void)arg;
   while (pal_nds_audio_stream.running)
   {
      uint64_t now;
      uint32_t wait_ticks;

      deadline += pal_nds_audio_next_tick_period(&phase);
      now = tickGetCount();
      wait_ticks = deadline > now
         ? (uint32_t)(deadline - now) : 0u;
      if (wait_ticks != 0u)
      {
         threadSleepTicks(wait_ticks);
      }
      if (!pal_nds_audio_stream.running)
      {
         break;
      }
      playback_tick++;
      now = tickGetCount();
      for (;;)
      {
         uint32_t next_phase = phase;
         const uint32_t next_period =
            pal_nds_audio_next_tick_period(&next_phase);

         if (deadline + next_period > now)
         {
            break;
         }
         deadline += next_period;
         phase = next_phase;
         playback_tick++;
      }
      if (playback_tick - handled_tick >= PAL_NDS_AUDIO_RING_TICKS)
      {
         pal_nds_audio_deadline_misses +=
            playback_tick - handled_tick - PAL_NDS_AUDIO_RING_TICKS + 1u;
         handled_tick = playback_tick - PAL_NDS_AUDIO_RING_TICKS + 1u;
      }
      while (handled_tick != playback_tick &&
         pal_nds_audio_stream.running)
      {
         int16_t *destination = pal_nds_audio_ring +
            (handled_tick & (PAL_NDS_AUDIO_RING_TICKS - 1u)) *
               PAL_NDS_AUDIO_TICK_SAMPLES;

         /* Refill one consumed tick; it has a full ring loop as deadline. */
         pal_nds_audio_stream.render(
            pal_nds_audio_stream.user,
            destination,
            PAL_NDS_AUDIO_TICK_SAMPLES);
         armDCacheFlush(
            destination,
            PAL_NDS_AUDIO_TICK_SAMPLES * sizeof(*destination));
         handled_tick++;
      }
   }
   return 0;
}

bool
NdsTarget_AudioStart(
   NdsTargetAudioRender render,
   void *user)
{
   uint32_t sound_timer;
   ArmIrqState irq_state;

   if (render == NULL)
   {
      return false;
   }
   NdsTarget_AudioStop();
   memset(&pal_nds_audio_stream, 0, sizeof(pal_nds_audio_stream));
   pal_nds_audio_deadline_misses = 0u;
   memset(pal_nds_audio_ring, 0, sizeof(pal_nds_audio_ring));
   armDCacheFlush(pal_nds_audio_ring, sizeof(pal_nds_audio_ring));
   pal_nds_audio_stream.render = render;
   pal_nds_audio_stream.user = user;

   tickInit();
   soundInit();
   soundSetPower(true);
   soundSetMixerSleep(false);
   soundSetMixerVolume(127u);
   sound_timer = soundTimerFromHz(PAL_NDS_AUDIO_SAMPLE_RATE);
   /*
    * The sound timer runs at SYSTEM_CLOCK/2 and the Calico tick clock at
    * SYSTEM_CLOCK/64, so 32 sound clocks are exactly one scheduler tick.
    * The 256-frame block is integral at the programmed PCM timer.  Deriving
    * its cadence from that timer keeps the worker and looping channel locked.
    */
   if ((PAL_NDS_AUDIO_TICK_SAMPLES * sound_timer) % 32u != 0u)
   {
      return false;
   }
   pal_nds_audio_stream.tick_period_base =
      PAL_NDS_AUDIO_TICK_SAMPLES * sound_timer / 32u;
   pal_nds_audio_stream.tick_period_remainder =
      PAL_NDS_AUDIO_TICK_SAMPLES * sound_timer % 32u;
   pal_nds_audio_stream.running = true;
   soundPreparePcm(
      PAL_NDS_AUDIO_CHANNEL,
      0x7f0u,
      0x40u,
      sound_timer,
      SoundMode_Repeat,
      SoundFmt_Pcm16,
      pal_nds_audio_ring,
      0u,
      PAL_NDS_AUDIO_RING_SAMPLES / 2u);

   threadPrepare(
      &pal_nds_audio_stream.worker,
      pal_nds_audio_worker,
      NULL,
      pal_nds_audio_thread_stack + sizeof(pal_nds_audio_thread_stack),
      PAL_NDS_AUDIO_THREAD_PRIO);
   threadAttachLocalStorage(&pal_nds_audio_stream.worker, NULL);

   irq_state = armIrqLockByPsr();
   soundStart(1u << PAL_NDS_AUDIO_CHANNEL);
   soundSynchronize();
   pal_nds_audio_stream.start_tick = tickGetCount();
   threadStart(&pal_nds_audio_stream.worker);
   armIrqUnlockByPsr(irq_state);
   return true;
}

void
NdsTarget_AudioStop(
   void)
{
   if (!pal_nds_audio_stream.running)
   {
      return;
   }
   pal_nds_audio_stream.running = false;
   armCompilerBarrier();
   threadJoin(&pal_nds_audio_stream.worker);
   soundStop(1u << PAL_NDS_AUDIO_CHANNEL);
   soundSynchronize();
   memset(pal_nds_audio_ring, 0, sizeof(pal_nds_audio_ring));
   armDCacheFlush(pal_nds_audio_ring, sizeof(pal_nds_audio_ring));
}

uint32_t
NdsTarget_AudioDeadlineMisses(
   void)
{
   return pal_nds_audio_deadline_misses;
}
