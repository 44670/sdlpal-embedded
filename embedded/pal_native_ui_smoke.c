#include "pal_native_ui.h"

#include <stdio.h>
#include <string.h>

static uint8_t mapped_pixels[240u * 135u];

static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}

static int check_geometry(uint16_t width, uint16_t height)
{
    PalNativeUiDialogLayout dialog;
    PalNativeUiRect box = { 0, 0, 2, 2 };
    PalNativeUiRect drawn;
    uint8_t pixels[16];
    uint8_t glyph[13];
    const uint8_t rle[] = {
        4, 0, 4, 0,
        16,
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
        13, 14, 15, 16,
    };

    if (!PalNativeUi_GetDialogLayout(
            width, height, false, true, &dialog) ||
        dialog.portrait.x >= dialog.text.x ||
        dialog.page_lines != 4u ||
        (uint32_t)dialog.text.x + dialog.text.width > width) {
        return fail("upper dialogue layout is invalid");
    }
    if (!PalNativeUi_GetDialogLayout(
            width, height, true, true, &dialog) ||
        dialog.text.x >= dialog.portrait.x ||
        dialog.page_lines != 4u ||
        dialog.title_y >= dialog.text.y) {
        return fail("lower dialogue layout is invalid");
    }
    if (!PalNativeUi_GetDialogLayout(
            width, height, false, false, &dialog) ||
        dialog.text.width != width - 8u || dialog.title_x != 12) {
        return fail("portrait-free dialogue layout is invalid");
    }
    if (!PalNativeUi_GetDialogLayout(
            width, height, true, false, &dialog) ||
        dialog.title_x != 12) {
        return fail("portrait-free lower dialogue title is invalid");
    }
    if (!PalNativeUi_GetCenterDialogLayout(width, height, &dialog) ||
        dialog.text.height != PAL_NATIVE_UI_FONT_HEIGHT * 4u) {
        return fail("center dialogue layout is invalid");
    }

    memset(pixels, 0, sizeof(pixels));
    if (!PalNativeUi_BlitRleFitIndexed(
            rle, sizeof(rle), pixels, 4, 4, 4, box, &drawn) ||
        drawn.width != 2u || drawn.height != 2u ||
        pixels[0] != 6u || pixels[1] != 8u ||
        pixels[4] != 14u || pixels[5] != 16u) {
        return fail("nearest-centre RLE fit mismatch");
    }

    memset(mapped_pixels, 0, sizeof(mapped_pixels));
    if (!PalNativeUi_BlitRleMappedIndexed(
            rle, sizeof(rle), mapped_pixels,
            width, width, height,
            0, 0, &drawn) ||
        drawn.x != 0 || drawn.y != 0 ||
        drawn.width != (uint16_t)PalNativeUi_MapVirtualX(4, width) ||
        drawn.height != (uint16_t)PalNativeUi_MapVirtualY(4, height) ||
        mapped_pixels[0] == 0u ||
        mapped_pixels[(size_t)(drawn.height - 1u) *
            width + drawn.width - 1u] != 16u) {
        return fail("mapped RLE material mismatch");
    }

    memset(pixels, 0, sizeof(pixels));
    memset(glyph, 0, sizeof(glyph));
    glyph[0] = 0x80u;
    glyph[12] = 0x10u;
    if (!PalNativeUi_DrawFont10Glyph(
            glyph, pixels, 4, 4, 4, 0, 0, 7u) ||
        pixels[0] != 7u) {
        return fail("FONT10 draw mismatch");
    }

    return 0;
}

int main(void)
{
    if (check_geometry(240u, 135u) != 0) {
        return 1;
    }
    return check_geometry(160u, 128u);
}
