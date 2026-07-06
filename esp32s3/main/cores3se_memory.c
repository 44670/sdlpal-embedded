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
uint8_t pal_sram_display_dma[PAL_SRAM_DISPLAY_DMA_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_misc[PAL_SRAM_MISC_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_palette_current[PAL_SRAM_PALETTE_RGB_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_palette_work[PAL_SRAM_PALETTE_RGB_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_palette_rgb565[PAL_SRAM_PALETTE_RGB565_BYTES] PAL_BSS_SRAM;

uint8_t pal_psram_save_state[PAL_PSRAM_SAVE_STATE_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_map_tiles[PAL_PSRAM_MAP_TILES_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_gop_copy[PAL_PSRAM_GOP_COPY_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_tf_toc[PAL_PSRAM_TF_TOC_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_sprite_pin[PAL_PSRAM_SPRITE_PIN_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_menu_background[PAL_PSRAM_MENU_BACKGROUND_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_menu_image[PAL_PSRAM_MENU_IMAGE_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_menu_box[PAL_PSRAM_MENU_BOX_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_fbp_background[PAL_PSRAM_FBP_BACKGROUND_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_effect[PAL_PSRAM_EFFECT_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_rng_frame_a[PAL_PSRAM_RNG_FRAME_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_rng_frame_b[PAL_PSRAM_RNG_FRAME_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_tf_readahead[PAL_PSRAM_TF_READAHEAD_BYTES] PAL_BSS_PSRAM;
