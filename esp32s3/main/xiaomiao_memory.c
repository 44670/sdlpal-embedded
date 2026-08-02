#include "xiaomiao_memory.h"

#include <esp_attr.h>

#if defined(__GNUC__)
#define XIAOMIAO_SRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#define XIAOMIAO_PSRAM EXT_RAM_BSS_ATTR __attribute__((aligned(4)))
#else
#define XIAOMIAO_SRAM
#define XIAOMIAO_PSRAM
#endif

typedef char XiaomiaoMappedPsramBudget[
    XIAOMIAO_MAPPED_PSRAM_BYTES <= 0x003c0000u ? 1 : -1];

uint8_t pal_sram_framebuffer[PAL_EXTREME_SCREEN_BYTES] XIAOMIAO_PSRAM;
uint8_t pal_sram_aux_framebuffer[PAL_EXTREME_SCREEN_BYTES] XIAOMIAO_PSRAM;
uint8_t pal_sram_display_dma[PAL_EXTREME_DISPLAY_DMA_BYTES] XIAOMIAO_SRAM;
uint8_t pal_sram_fbp_scanline[PAL_EXTREME_FBP_SCANLINE_BYTES] XIAOMIAO_SRAM;

void
Xiaomiao_TouchReservedBuffers(
    void)
{
#define TOUCH(array) do { (array)[0] = 0; (array)[sizeof(array) - 1u] = 0; } while (0)
    TOUCH(pal_sram_framebuffer);
    TOUCH(pal_sram_aux_framebuffer);
    TOUCH(pal_sram_display_dma);
    TOUCH(pal_sram_fbp_scanline);
#undef TOUCH
}
