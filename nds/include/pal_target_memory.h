#ifndef PAL_NDS_TARGET_MEMORY_H
#define PAL_NDS_TARGET_MEMORY_H

#include "pal_memory_profile.h"
#include "pal_target_board.h"

#include <stdint.h>

#define PAL_EXTREME_SCREEN_WIDTH PAL_TARGET_LCD_WIDTH
#define PAL_EXTREME_SCREEN_HEIGHT PAL_TARGET_LCD_HEIGHT
#define PAL_EXTREME_SCREEN_BYTES \
   (PAL_EXTREME_SCREEN_WIDTH * PAL_EXTREME_SCREEN_HEIGHT)
#define PAL_EXTREME_FBP_SCANLINE_BYTES 320u
#define PAL_NDS_RIX_TRACK_BYTES 10108u
#define PAL_NDS_OPL_SAMPLE_RATE 32768u
#define PAL_NDS_OPL_TICK_RATE 70u
#define PAL_NDS_OPL_TICK_SAMPLES 469u
#define PAL_NDS_AUDIO_SAMPLE_RATE 32768u
#define PAL_NDS_AUDIO_TICK_SAMPLES 256u
#define PAL_NDS_OPL_PRODUCER_BATCH 20u
#define PAL_NDS_OPL_STARTUP_TICKS 20u
#define PAL_NDS_OPL_WRITES_PER_TICK 256u
#define PAL_NDS_OPL_TICK_QUEUE_LENGTH 32u
#define PAL_NDS_AUDIO_RING_TICKS 32u
#define PAL_NDS_AUDIO_RING_SAMPLES \
   (PAL_NDS_AUDIO_TICK_SAMPLES * PAL_NDS_AUDIO_RING_TICKS)
#define PAL_NDS_AUDIO_THREAD_STACK_BYTES 6144u

typedef struct PalNdsOplWrite {
   uint8_t reg;
   uint8_t value;
} PalNdsOplWrite;

typedef struct PalNdsOplTick {
   uint16_t count;
   uint8_t reset;
   uint8_t volume;
   PalNdsOplWrite writes[PAL_NDS_OPL_WRITES_PER_TICK];
} PalNdsOplTick;

#if PAL_EXTREME_SCREEN_BYTES != (256u * 192u)
#error "Nintendo DS native framebuffer geometry changed unexpectedly"
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t pal_sram_framebuffer[PAL_EXTREME_SCREEN_BYTES];
extern uint8_t pal_sram_aux_framebuffer[PAL_EXTREME_SCREEN_BYTES];
extern uint8_t pal_sram_fbp_scanline[PAL_EXTREME_FBP_SCANLINE_BYTES];
extern uint8_t pal_nds_track[PAL_NDS_RIX_TRACK_BYTES];
extern PalNdsOplWrite pal_nds_opl_staging[PAL_NDS_OPL_WRITES_PER_TICK];
extern PalNdsOplTick pal_nds_opl_tick_queue[PAL_NDS_OPL_TICK_QUEUE_LENGTH];
extern int16_t pal_nds_audio_ring[PAL_NDS_AUDIO_RING_SAMPLES];
extern uint8_t pal_nds_audio_thread_stack[PAL_NDS_AUDIO_THREAD_STACK_BYTES];
extern volatile uint32_t pal_nds_opl_queue_overruns;
extern volatile uint32_t pal_nds_opl_queue_underruns;
extern volatile uint32_t pal_nds_audio_deadline_misses;
extern volatile uint32_t pal_nds_present_count;

void NdsTarget_TouchReservedBuffers(void);

#ifdef __cplusplus
}
#endif

#define PalTarget_TouchReservedBuffers NdsTarget_TouchReservedBuffers

#endif
