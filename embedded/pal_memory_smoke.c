#include "pal_memory.h"

#include <stddef.h>

static int check_buffer(uint8_t *data, size_t size, uint8_t tag)
{
    if (data == 0 || size == 0) {
        return 1;
    }
    data[0] = tag;
    data[size - 1] = (uint8_t)(tag ^ 0x5a);
    return 0;
}

#define CHECK_BUFFER(name, tag) check_buffer((name), sizeof(name), (tag))

int main(void)
{
    if (PAL_SRAM_DECLARED_BYTES != 184832u) {
        return 1;
    }
    if (PAL_PSRAM_DECLARED_BYTES != 7302160u) {
        return 2;
    }

    if (CHECK_BUFFER(pal_sram_framebuffer, 1u) != 0) {
        return 3;
    }
    if (CHECK_BUFFER(pal_sram_big_buffer, 2u) != 0) {
        return 4;
    }
    if (CHECK_BUFFER(pal_sram_audio, 3u) != 0) {
        return 5;
    }
    if (CHECK_BUFFER(pal_sram_display_dma, 4u) != 0) {
        return 6;
    }
    if (CHECK_BUFFER(pal_sram_hot_globals, 5u) != 0) {
        return 7;
    }
    if (CHECK_BUFFER(pal_sram_misc, 6u) != 0) {
        return 8;
    }
    if (CHECK_BUFFER(pal_sram_palette_current, 7u) != 0) {
        return 9;
    }
    if (CHECK_BUFFER(pal_sram_palette_work, 8u) != 0) {
        return 10;
    }
    if (CHECK_BUFFER(pal_sram_palette_rgb565, 9u) != 0) {
        return 11;
    }

    if (CHECK_BUFFER(pal_psram_save_state, 10u) != 0) {
        return 12;
    }
    if (CHECK_BUFFER(pal_psram_map_tiles, 11u) != 0) {
        return 13;
    }
    if (CHECK_BUFFER(pal_psram_gop_copy, 12u) != 0) {
        return 14;
    }
    if (CHECK_BUFFER(pal_psram_screen_bak, 13u) != 0) {
        return 15;
    }
    if (CHECK_BUFFER(pal_psram_resource_staging, 14u) != 0) {
        return 16;
    }
    if (CHECK_BUFFER(pal_psram_tf_toc, 15u) != 0) {
        return 17;
    }
    if (CHECK_BUFFER(pal_psram_rng_frame_a, 16u) != 0) {
        return 18;
    }
    if (CHECK_BUFFER(pal_psram_rng_frame_b, 17u) != 0) {
        return 19;
    }
    if (CHECK_BUFFER(pal_psram_tf_readahead, 18u) != 0) {
        return 20;
    }
    if (CHECK_BUFFER(pal_psram_effect, 19u) != 0) {
        return 21;
    }
    if (CHECK_BUFFER(pal_psram_fbp_background, 20u) != 0) {
        return 22;
    }
    if (CHECK_BUFFER(pal_psram_sfx_bank, 21u) != 0) {
        return 23;
    }
    if (CHECK_BUFFER(pal_psram_text_misc, 22u) != 0) {
        return 24;
    }
    if (CHECK_BUFFER(pal_psram_sprite_pin, 23u) != 0) {
        return 25;
    }
    if (CHECK_BUFFER(pal_psram_menu_background, 24u) != 0) {
        return 26;
    }
    if (CHECK_BUFFER(pal_psram_menu_image, 25u) != 0) {
        return 27;
    }
    if (CHECK_BUFFER(pal_psram_menu_box, 26u) != 0) {
        return 28;
    }
    if (CHECK_BUFFER(pal_psram_ending_fbp_a, 27u) != 0) {
        return 29;
    }
    if (CHECK_BUFFER(pal_psram_ending_fbp_b, 28u) != 0) {
        return 30;
    }

    return 0;
}
