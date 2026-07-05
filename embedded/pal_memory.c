#include "pal_memory.h"

#if defined(__GNUC__)
#define PAL_BSS_SRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#define PAL_BSS_PSRAM __attribute__((section(".bss.pal_psram"), aligned(4)))
#else
#define PAL_BSS_SRAM
#define PAL_BSS_PSRAM
#endif

uint8_t pal_sram_framebuffer[PAL_SRAM_FRAMEBUFFER_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_big_buffer[PAL_SRAM_BIG_BUFFER_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_audio[PAL_SRAM_AUDIO_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_display_dma[PAL_SRAM_DISPLAY_DMA_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_hot_globals[PAL_SRAM_HOT_GLOBALS_BYTES] PAL_BSS_SRAM;
uint8_t pal_sram_misc[PAL_SRAM_MISC_BYTES] PAL_BSS_SRAM;

uint8_t pal_psram_save_state[PAL_PSRAM_SAVE_STATE_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_map_tiles[PAL_PSRAM_MAP_TILES_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_gop_copy[PAL_PSRAM_GOP_COPY_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_screen_bak[PAL_PSRAM_SCREEN_BAK_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_resource_staging[PAL_PSRAM_RESOURCE_STAGING_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_rng_frame_a[PAL_PSRAM_RNG_FRAME_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_rng_frame_b[PAL_PSRAM_RNG_FRAME_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_tf_readahead[PAL_PSRAM_TF_READAHEAD_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_effect[PAL_PSRAM_EFFECT_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_fbp_background[PAL_PSRAM_FBP_BACKGROUND_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_sfx_bank[PAL_PSRAM_SFX_BANK_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_text_misc[PAL_PSRAM_TEXT_MISC_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_sprite_pin[PAL_PSRAM_SPRITE_PIN_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_menu_background[PAL_PSRAM_MENU_BACKGROUND_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_menu_image[PAL_PSRAM_MENU_IMAGE_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_menu_box[PAL_PSRAM_MENU_BOX_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_ending_fbp_a[PAL_PSRAM_ENDING_FBP_BYTES] PAL_BSS_PSRAM;
uint8_t pal_psram_ending_fbp_b[PAL_PSRAM_ENDING_FBP_BYTES] PAL_BSS_PSRAM;
