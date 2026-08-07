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
#define PAL_NDS_RIX_MELODIC_CHANNELS 9u
#define PAL_NDS_RIX_DRUM_CHANNELS 5u
#define PAL_NDS_RIX_RHYTHM_VOICES 3u
#define PAL_NDS_RIX_WAVE_BYTES 256u
#define PAL_NDS_RIX_SINE_ENTRIES 256u
#define PAL_NDS_RIX_TIMER_ENTRIES 1024u
#define PAL_NDS_RIX_TL_ENTRIES 64u

#if PAL_EXTREME_SCREEN_BYTES != (256u * 192u)
#error "Nintendo DS native framebuffer geometry changed unexpectedly"
#endif

extern uint8_t pal_sram_framebuffer[PAL_EXTREME_SCREEN_BYTES];
extern uint8_t pal_sram_aux_framebuffer[PAL_EXTREME_SCREEN_BYTES];
extern uint8_t pal_sram_fbp_scanline[PAL_EXTREME_FBP_SCANLINE_BYTES];

void NdsTarget_TouchReservedBuffers(void);
#define PalTarget_TouchReservedBuffers NdsTarget_TouchReservedBuffers

#endif
