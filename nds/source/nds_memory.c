#include "pal_target_memory.h"

#if defined(__GNUC__)
#define PAL_NDS_MAIN_RAM \
   __attribute__((section(".bss.pal_main"), aligned(4)))
#else
#define PAL_NDS_MAIN_RAM
#endif

uint8_t pal_sram_framebuffer[PAL_EXTREME_SCREEN_BYTES] PAL_NDS_MAIN_RAM;
uint8_t pal_sram_aux_framebuffer[PAL_EXTREME_SCREEN_BYTES] PAL_NDS_MAIN_RAM;
uint8_t pal_sram_fbp_scanline[PAL_EXTREME_FBP_SCANLINE_BYTES]
   PAL_NDS_MAIN_RAM;
uint8_t pal_nds_minimap_topology[PAL_NDS_MINIMAP_TOPOLOGY_BYTES]
   PAL_NDS_MAIN_RAM;
uint8_t pal_nds_minimap_tiles[PAL_NDS_MINIMAP_TILE_BYTES]
   PAL_NDS_MAIN_RAM;
uint16_t pal_nds_minimap_pattern_tiles[PAL_NDS_MINIMAP_PATTERN_COUNT]
   PAL_NDS_MAIN_RAM;
uint16_t pal_nds_minimap_tilemap[PAL_NDS_MINIMAP_TILEMAP_ENTRIES]
   PAL_NDS_MAIN_RAM;

void
NdsTarget_TouchReservedBuffers(
   void)
{
#define PAL_NDS_TOUCH(array) do { \
   ((uint8_t *)(array))[0] = 0u; \
   ((uint8_t *)(array))[sizeof(array) - 1u] = 0u; \
} while (0)
   PAL_NDS_TOUCH(pal_sram_framebuffer);
   PAL_NDS_TOUCH(pal_sram_aux_framebuffer);
   PAL_NDS_TOUCH(pal_sram_fbp_scanline);
   PAL_NDS_TOUCH(pal_nds_minimap_topology);
   PAL_NDS_TOUCH(pal_nds_minimap_tiles);
   PAL_NDS_TOUCH(pal_nds_minimap_pattern_tiles);
   PAL_NDS_TOUCH(pal_nds_minimap_tilemap);
#undef PAL_NDS_TOUCH
}
