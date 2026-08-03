#ifndef CARDPUTER_EXTREME_MEMORY_H
#define CARDPUTER_EXTREME_MEMORY_H

#include <stdint.h>
#include "cardputer_extreme_board.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_EXTREME_SCREEN_WIDTH CARDPUTER_EXTREME_LCD_WIDTH
#define PAL_EXTREME_SCREEN_HEIGHT CARDPUTER_EXTREME_LCD_HEIGHT
#define PAL_EXTREME_SCREEN_BYTES \
    (PAL_EXTREME_SCREEN_WIDTH * PAL_EXTREME_SCREEN_HEIGHT)
#define PAL_EXTREME_DISPLAY_DMA_BYTES (4u * 1024u)
#define PAL_EXTREME_FBP_SCANLINE_BYTES 320u

extern uint8_t pal_sram_framebuffer[PAL_EXTREME_SCREEN_BYTES];
extern uint8_t pal_sram_aux_framebuffer[PAL_EXTREME_SCREEN_BYTES];
extern uint8_t pal_sram_display_dma[PAL_EXTREME_DISPLAY_DMA_BYTES];
extern uint8_t pal_sram_fbp_scanline[PAL_EXTREME_FBP_SCANLINE_BYTES];

void CardputerExtreme_TouchReservedBuffers(void);

#ifdef __cplusplus
}
#endif

#endif
