#ifndef CARDPUTER_EXTREME_MEMORY_H
#define CARDPUTER_EXTREME_MEMORY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_EXTREME_SCREEN_BYTES (320u * 200u)
#define PAL_EXTREME_DISPLAY_DMA_BYTES (4u * 1024u)

extern uint8_t pal_sram_framebuffer[PAL_EXTREME_SCREEN_BYTES];
extern uint8_t pal_sram_aux_framebuffer[PAL_EXTREME_SCREEN_BYTES];
extern uint8_t pal_sram_display_dma[PAL_EXTREME_DISPLAY_DMA_BYTES];

void CardputerExtreme_TouchReservedBuffers(void);

#ifdef __cplusplus
}
#endif

#endif
