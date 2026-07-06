#ifndef PAL_MEMORY_H
#define PAL_MEMORY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_SRAM_BUDGET_BYTES (300u * 1024u)
#define PAL_PSRAM_BUDGET_BYTES (8u * 1024u * 1024u)

#define PAL_SRAM_FRAMEBUFFER_BYTES (320u * 200u)
#define PAL_SRAM_BIG_BUFFER_BYTES 65536u
#define PAL_SRAM_AUDIO_BYTES (16u * 1024u)
#define PAL_SRAM_DISPLAY_DMA_BYTES (4u * 1024u)
#define PAL_SRAM_HOT_GLOBALS_BYTES (24u * 1024u)
#define PAL_SRAM_MISC_BYTES (8u * 1024u)
#define PAL_SRAM_PALETTE_RGB_BYTES (256u * 3u)
#define PAL_SRAM_PALETTE_RGB565_BYTES (256u * 2u)

#define PAL_PSRAM_SAVE_STATE_BYTES (640u * 1024u)
#define PAL_PSRAM_MAP_TILES_BYTES 65536u
#define PAL_PSRAM_GOP_COPY_BYTES 65536u
#define PAL_PSRAM_SCREEN_BAK_BYTES (320u * 200u)
#define PAL_PSRAM_RESOURCE_STAGING_BYTES (256u * 1024u)
#define PAL_PSRAM_TF_TOC_BYTES (32u * 1024u)
#define PAL_PSRAM_RNG_FRAME_BYTES 65000u
#define PAL_PSRAM_TF_READAHEAD_BYTES (128u * 1024u)
#define PAL_PSRAM_EFFECT_BYTES 65536u
#define PAL_PSRAM_FBP_BACKGROUND_BYTES (320u * 200u)
#define PAL_PSRAM_SFX_BANK_BYTES (4u * 1024u * 1024u)
#define PAL_PSRAM_TEXT_MISC_BYTES (256u * 1024u)
#define PAL_PSRAM_SPRITE_PIN_BYTES (1024u * 1024u)
#define PAL_PSRAM_MENU_BACKGROUND_BYTES (320u * 200u)
#define PAL_PSRAM_MENU_IMAGE_BYTES 64000u
#define PAL_PSRAM_MENU_BOX_BYTES (72u * 72u)
#define PAL_PSRAM_ENDING_FBP_BYTES (320u * 200u)

#define PAL_SRAM_DECLARED_BYTES \
    (PAL_SRAM_FRAMEBUFFER_BYTES + PAL_SRAM_BIG_BUFFER_BYTES + PAL_SRAM_AUDIO_BYTES + \
     PAL_SRAM_DISPLAY_DMA_BYTES + PAL_SRAM_HOT_GLOBALS_BYTES + PAL_SRAM_MISC_BYTES + \
     PAL_SRAM_PALETTE_RGB_BYTES + PAL_SRAM_PALETTE_RGB_BYTES + \
     PAL_SRAM_PALETTE_RGB565_BYTES)

#define PAL_PSRAM_DECLARED_BYTES \
    (PAL_PSRAM_SAVE_STATE_BYTES + PAL_PSRAM_MAP_TILES_BYTES + PAL_PSRAM_GOP_COPY_BYTES + \
     PAL_PSRAM_SCREEN_BAK_BYTES + PAL_PSRAM_RESOURCE_STAGING_BYTES + \
     PAL_PSRAM_TF_TOC_BYTES + PAL_PSRAM_RNG_FRAME_BYTES + PAL_PSRAM_RNG_FRAME_BYTES + \
     PAL_PSRAM_TF_READAHEAD_BYTES + \
     PAL_PSRAM_EFFECT_BYTES + PAL_PSRAM_FBP_BACKGROUND_BYTES + PAL_PSRAM_SFX_BANK_BYTES + \
     PAL_PSRAM_TEXT_MISC_BYTES + PAL_PSRAM_SPRITE_PIN_BYTES + \
     PAL_PSRAM_MENU_BACKGROUND_BYTES + PAL_PSRAM_MENU_IMAGE_BYTES + PAL_PSRAM_MENU_BOX_BYTES + \
     PAL_PSRAM_ENDING_FBP_BYTES + PAL_PSRAM_ENDING_FBP_BYTES)

typedef char pal_sram_declared_fits[(PAL_SRAM_DECLARED_BYTES <= PAL_SRAM_BUDGET_BYTES) ? 1 : -1];
typedef char pal_psram_declared_fits[(PAL_PSRAM_DECLARED_BYTES <= PAL_PSRAM_BUDGET_BYTES) ? 1 : -1];

extern uint8_t pal_sram_framebuffer[PAL_SRAM_FRAMEBUFFER_BYTES];
extern uint8_t pal_sram_big_buffer[PAL_SRAM_BIG_BUFFER_BYTES];
extern uint8_t pal_sram_audio[PAL_SRAM_AUDIO_BYTES];
extern uint8_t pal_sram_display_dma[PAL_SRAM_DISPLAY_DMA_BYTES];
extern uint8_t pal_sram_hot_globals[PAL_SRAM_HOT_GLOBALS_BYTES];
extern uint8_t pal_sram_misc[PAL_SRAM_MISC_BYTES];
extern uint8_t pal_sram_palette_current[PAL_SRAM_PALETTE_RGB_BYTES];
extern uint8_t pal_sram_palette_work[PAL_SRAM_PALETTE_RGB_BYTES];
extern uint8_t pal_sram_palette_rgb565[PAL_SRAM_PALETTE_RGB565_BYTES];

extern uint8_t pal_psram_save_state[PAL_PSRAM_SAVE_STATE_BYTES];
extern uint8_t pal_psram_map_tiles[PAL_PSRAM_MAP_TILES_BYTES];
extern uint8_t pal_psram_gop_copy[PAL_PSRAM_GOP_COPY_BYTES];
extern uint8_t pal_psram_screen_bak[PAL_PSRAM_SCREEN_BAK_BYTES];
extern uint8_t pal_psram_resource_staging[PAL_PSRAM_RESOURCE_STAGING_BYTES];
extern uint8_t pal_psram_tf_toc[PAL_PSRAM_TF_TOC_BYTES];
extern uint8_t pal_psram_rng_frame_a[PAL_PSRAM_RNG_FRAME_BYTES];
extern uint8_t pal_psram_rng_frame_b[PAL_PSRAM_RNG_FRAME_BYTES];
extern uint8_t pal_psram_tf_readahead[PAL_PSRAM_TF_READAHEAD_BYTES];
extern uint8_t pal_psram_effect[PAL_PSRAM_EFFECT_BYTES];
extern uint8_t pal_psram_fbp_background[PAL_PSRAM_FBP_BACKGROUND_BYTES];
extern uint8_t pal_psram_sfx_bank[PAL_PSRAM_SFX_BANK_BYTES];
extern uint8_t pal_psram_text_misc[PAL_PSRAM_TEXT_MISC_BYTES];
extern uint8_t pal_psram_sprite_pin[PAL_PSRAM_SPRITE_PIN_BYTES];
extern uint8_t pal_psram_menu_background[PAL_PSRAM_MENU_BACKGROUND_BYTES];
extern uint8_t pal_psram_menu_image[PAL_PSRAM_MENU_IMAGE_BYTES];
extern uint8_t pal_psram_menu_box[PAL_PSRAM_MENU_BOX_BYTES];
extern uint8_t pal_psram_ending_fbp_a[PAL_PSRAM_ENDING_FBP_BYTES];
extern uint8_t pal_psram_ending_fbp_b[PAL_PSRAM_ENDING_FBP_BYTES];

#ifdef __cplusplus
}
#endif

#endif
