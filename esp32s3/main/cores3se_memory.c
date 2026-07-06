#include "../../embedded/pal_memory.h"

#include <esp_attr.h>

#if defined(__GNUC__)
#define PAL_BSS_SRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#define PAL_BSS_PSRAM EXT_RAM_BSS_ATTR __attribute__((aligned(4)))
#else
#define PAL_BSS_SRAM
#define PAL_BSS_PSRAM
#endif

uint8_t pal_sram_framebuffer[PAL_SRAM_FRAMEBUFFER_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_big_buffer[PAL_SRAM_BIG_BUFFER_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_display_dma[PAL_SRAM_DISPLAY_DMA_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_palette_current[PAL_SRAM_PALETTE_RGB_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_palette_work[PAL_SRAM_PALETTE_RGB_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_palette_rgb565[PAL_SRAM_PALETTE_RGB565_BYTES] PAL_BSS_SRAM;

uint8_t pal_psram_screen_bak[PAL_PSRAM_SCREEN_BAK_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_map_tiles[PAL_PSRAM_MAP_TILES_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_gop_copy[PAL_PSRAM_GOP_COPY_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_tf_toc[PAL_PSRAM_TF_TOC_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_sprite_pin[PAL_PSRAM_SPRITE_PIN_BYTES] PAL_BSS_PSRAM;
