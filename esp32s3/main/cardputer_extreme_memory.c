#include "cardputer_extreme_memory.h"

#if defined(__GNUC__)
#define PAL_EXTREME_SRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#else
#define PAL_EXTREME_SRAM
#endif

uint8_t pal_sram_framebuffer[PAL_EXTREME_SCREEN_BYTES] PAL_EXTREME_SRAM;
uint8_t pal_sram_aux_framebuffer[PAL_EXTREME_SCREEN_BYTES] PAL_EXTREME_SRAM;
uint8_t pal_sram_display_dma[PAL_EXTREME_DISPLAY_DMA_BYTES] PAL_EXTREME_SRAM;

void
CardputerExtreme_TouchReservedBuffers(
   void
)
{
   pal_sram_framebuffer[0] = 0;
   pal_sram_framebuffer[PAL_EXTREME_SCREEN_BYTES - 1u] = 0;
   pal_sram_aux_framebuffer[0] = 0;
   pal_sram_aux_framebuffer[PAL_EXTREME_SCREEN_BYTES - 1u] = 0;
   pal_sram_display_dma[0] = 0;
   pal_sram_display_dma[PAL_EXTREME_DISPLAY_DMA_BYTES - 1u] = 0;
}
