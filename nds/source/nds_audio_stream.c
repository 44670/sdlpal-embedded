/*
 * Fixed-memory ARM9 PCM stream for the Calico ARM7 sound service.
 * The two-page refill scheme follows maxmod's ZPL-2.1 mm_stream design,
 * but owns its ring statically and does not initialize maxmod or allocate.
 */

#include "pal_target_board.h"

#include <calico.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
   PAL_NDS_AUDIO_RATE = 22050u,
   PAL_NDS_AUDIO_PAGE_SAMPLES = 2048u,
   PAL_NDS_AUDIO_RING_SAMPLES = PAL_NDS_AUDIO_PAGE_SAMPLES * 2u,
   PAL_NDS_SOUND_CLOCKS_PER_TICK = 32u,
   PAL_NDS_AUDIO_CHANNEL = 0u,
};

typedef struct PalNdsAudioStream {
   TickTask task;
   NdsTargetAudioRender render;
   void *user;
   volatile uint32_t page_count;
   uint32_t timer;
   uint32_t write_pos;
   bool running;
} PalNdsAudioStream;

static int16_t pal_nds_audio_ring[PAL_NDS_AUDIO_RING_SAMPLES]
   __attribute__((aligned(ARM_CACHE_LINE_SZ), section(".bss.pal_nds_audio")));
static PalNdsAudioStream pal_nds_audio_stream
   __attribute__((section(".bss.pal_nds_audio")));

static void
pal_nds_audio_page_tick(
   TickTask *task)
{
   (void)task;
   pal_nds_audio_stream.page_count++;
}

static uint32_t
pal_nds_audio_read_pos(
   void)
{
   ArmIrqState irq_state;
   uint32_t page;
   uint32_t ticks;
   uint32_t period = pal_nds_audio_stream.task.period;

   irq_state = armIrqLockByPsr();
   page = pal_nds_audio_stream.page_count;
   ticks = (uint32_t)tickGetCount() -
      (pal_nds_audio_stream.task.target - period);
   armIrqUnlockByPsr(irq_state);

   while (ticks >= period)
   {
      page++;
      ticks -= period;
   }
   return ((page & 1u) * PAL_NDS_AUDIO_PAGE_SAMPLES +
      ticks * PAL_NDS_SOUND_CLOCKS_PER_TICK /
         pal_nds_audio_stream.timer) & ~3u;
}

static uint32_t
pal_nds_audio_fill(
   uint32_t pos,
   uint32_t count)
{
   int16_t *destination = pal_nds_audio_ring + pos;

   if (count == 0u)
   {
      return 0u;
   }
   if (pal_nds_audio_stream.render != NULL)
   {
      pal_nds_audio_stream.render(
         pal_nds_audio_stream.user, destination, count);
   }
   else
   {
      memset(destination, 0, count * sizeof(*destination));
   }
   armDCacheFlush(destination, count * sizeof(*destination));
   return count;
}

static void
pal_nds_audio_refill(
   uint32_t read_pos)
{
   uint32_t write_pos = pal_nds_audio_stream.write_pos;

   do
   {
      uint32_t count = read_pos > write_pos
         ? read_pos - write_pos
         : PAL_NDS_AUDIO_RING_SAMPLES - write_pos;

      if (pal_nds_audio_fill(write_pos, count) != count)
      {
         break;
      }
      write_pos += count;
      if (write_pos == PAL_NDS_AUDIO_RING_SAMPLES)
      {
         write_pos = 0u;
      }
   } while (write_pos != read_pos);

   pal_nds_audio_stream.write_pos = write_pos;
}

bool
NdsTarget_AudioStart(
   NdsTargetAudioRender render,
   void *user)
{
   uint32_t task_period;
   ArmIrqState irq_state;

   if (render == NULL)
   {
      return false;
   }
   NdsTarget_AudioStop();
   memset(&pal_nds_audio_stream, 0, sizeof(pal_nds_audio_stream));
   pal_nds_audio_stream.render = render;
   pal_nds_audio_stream.user = user;
   pal_nds_audio_stream.timer = soundTimerFromHz(PAL_NDS_AUDIO_RATE);
   task_period =
      (PAL_NDS_AUDIO_PAGE_SAMPLES / PAL_NDS_SOUND_CLOCKS_PER_TICK) *
      pal_nds_audio_stream.timer;

   tickInit();
   soundInit();
   soundSetPower(true);
   soundSetMixerSleep(false);
   soundSetMixerVolume(127u);

   pal_nds_audio_refill(0u);
   soundPreparePcm(
      PAL_NDS_AUDIO_CHANNEL,
      0x7f0u,
      0x40u,
      pal_nds_audio_stream.timer,
      SoundMode_Repeat,
      SoundFmt_Pcm16,
      pal_nds_audio_ring,
      0u,
      PAL_NDS_AUDIO_PAGE_SAMPLES);

   irq_state = armIrqLockByPsr();
   soundStart(1u << PAL_NDS_AUDIO_CHANNEL);
   soundSynchronize();
   tickTaskStart(
      &pal_nds_audio_stream.task,
      pal_nds_audio_page_tick,
      task_period,
      task_period);
   pal_nds_audio_stream.running = true;
   armIrqUnlockByPsr(irq_state);
   return true;
}

void
NdsTarget_AudioPump(
   void)
{
   if (pal_nds_audio_stream.running)
   {
      pal_nds_audio_refill(pal_nds_audio_read_pos());
   }
}

void
NdsTarget_AudioStop(
   void)
{
   if (!pal_nds_audio_stream.running)
   {
      return;
   }
   tickTaskStop(&pal_nds_audio_stream.task);
   soundStop(1u << PAL_NDS_AUDIO_CHANNEL);
   soundSynchronize();
   memset(pal_nds_audio_ring, 0, sizeof(pal_nds_audio_ring));
   armDCacheFlush(pal_nds_audio_ring, sizeof(pal_nds_audio_ring));
   memset(&pal_nds_audio_stream, 0, sizeof(pal_nds_audio_stream));
}
