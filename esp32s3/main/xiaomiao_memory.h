#ifndef XIAOMIAO_MEMORY_H
#define XIAOMIAO_MEMORY_H

#include <stdint.h>
#include "pal_memory_profile.h"

#ifndef PAL_NATIVE_UI_GENERATED_HEADER
#define PAL_NATIVE_UI_GENERATED_HEADER "generated/pal_native_ui_160x128.h"
#endif
#include PAL_NATIVE_UI_GENERATED_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_EXTREME_SCREEN_WIDTH PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH
#define PAL_EXTREME_SCREEN_HEIGHT PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT
#define PAL_EXTREME_SCREEN_BYTES \
    (PAL_EXTREME_SCREEN_WIDTH * PAL_EXTREME_SCREEN_HEIGHT)
#define PAL_EXTREME_DISPLAY_DMA_BYTES (4u * 1024u)
#define PAL_EXTREME_FBP_SCANLINE_BYTES 320u

#define XIAOMIAO_MAPPED_PSRAM_BYTES ( \
    2u * PAL_EXTREME_SCREEN_BYTES + \
    PAL_MEM_LEVEL2_RESOURCE_BYTES + PAL_MEM_LEVEL2_SD_STORAGE_BYTES)

#if XIAOMIAO_MAPPED_PSRAM_BYTES > 0x003c0000u
#error "Xiaomiao named PSRAM owners exceed the conservative mapped window"
#endif

/* Compatibility names; both logical screens live in mapped PSRAM here. */
extern uint8_t pal_sram_framebuffer[PAL_EXTREME_SCREEN_BYTES];
extern uint8_t pal_sram_aux_framebuffer[PAL_EXTREME_SCREEN_BYTES];
extern uint8_t pal_sram_display_dma[PAL_EXTREME_DISPLAY_DMA_BYTES];
extern uint8_t pal_sram_fbp_scanline[PAL_EXTREME_FBP_SCANLINE_BYTES];

void Xiaomiao_TouchReservedBuffers(void);

#ifdef __cplusplus
}
#endif

#endif
