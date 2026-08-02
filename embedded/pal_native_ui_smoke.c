#include "pal_native_ui.h"

#include <stdio.h>
#include <string.h>

static uint8_t mapped_pixels[
    PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH *
    PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT];

static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}

int main(void)
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

    if (!PalNativeUi_GetDialogLayout(false, true, &dialog) ||
        dialog.portrait.x >= dialog.text.x ||
        dialog.page_lines != 4u ||
        (uint32_t)dialog.text.x + dialog.text.width >
            PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH) {
        return fail("upper dialogue layout is invalid");
    }
    if (!PalNativeUi_GetDialogLayout(true, true, &dialog) ||
        dialog.text.x >= dialog.portrait.x ||
        dialog.page_lines != 4u ||
        dialog.title_x !=
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TITLE_X) {
        return fail("lower dialogue layout is invalid");
    }
    if (!PalNativeUi_GetDialogLayout(false, false, &dialog) ||
        dialog.text.width != PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH - 8u ||
        dialog.title_x !=
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TITLE_NO_PORTRAIT_X) {
        return fail("portrait-free dialogue layout is invalid");
    }
    if (!PalNativeUi_GetDialogLayout(true, false, &dialog) ||
        dialog.title_x !=
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TITLE_NO_PORTRAIT_X) {
        return fail("portrait-free lower dialogue title is invalid");
    }
    if (!PalNativeUi_GetCenterDialogLayout(&dialog) ||
        dialog.text.height !=
            PAL_NATIVE_UI_GENERATED_DIALOG_LINE_HEIGHT *
            PAL_NATIVE_UI_GENERATED_DIALOG_PAGE_LINES) {
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
            PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH,
            PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH,
            PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT,
            0, 0, &drawn) ||
        drawn.x != 0 || drawn.y != 0 ||
        drawn.width != (uint16_t)PalNativeUi_MapVirtualX(4) ||
        drawn.height != (uint16_t)PalNativeUi_MapVirtualY(4) ||
        mapped_pixels[0] == 0u ||
        mapped_pixels[(size_t)(drawn.height - 1u) *
            PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH + drawn.width - 1u] != 16u) {
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
