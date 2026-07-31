#include "pal_native_ui.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}

int main(void)
{
    PalNativeUiViewport viewport;
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

    PalNativeUi_SetWorldView();
    if (!PalNativeUi_GetViewport(&viewport) ||
        viewport.width != PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH ||
        viewport.height != PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT ||
        viewport.source_x != PAL_NATIVE_UI_GENERATED_WORLD_ORIGIN_X ||
        viewport.source_y != PAL_NATIVE_UI_GENERATED_WORLD_ORIGIN_Y) {
        return fail("bad generated world viewport");
    }

    PalNativeUi_FocusLogical(-100, -100, PAL_NATIVE_UI_VIEW_UI);
    if (!PalNativeUi_GetViewport(&viewport) ||
        viewport.source_x != 0u || viewport.source_y != 0u) {
        return fail("viewport did not clamp at top-left");
    }
    PalNativeUi_FocusLogical(1000, 1000, PAL_NATIVE_UI_VIEW_UI);
    if (!PalNativeUi_GetViewport(&viewport) ||
        viewport.source_x + viewport.width !=
            PAL_NATIVE_UI_GENERATED_LOGICAL_WIDTH ||
        viewport.source_y + viewport.height !=
            PAL_NATIVE_UI_GENERATED_LOGICAL_HEIGHT) {
        return fail("viewport did not clamp at bottom-right");
    }

    PalNativeUi_SetDialogView();
    if (!PalNativeUi_GetViewport(&viewport) ||
        !PalNativeUi_GetDialogLayout(false, true, &dialog) ||
        dialog.portrait.x >= dialog.text.x ||
        dialog.page_lines != 4u ||
        (uint32_t)dialog.text.x + dialog.text.width >
            viewport.source_x + viewport.width) {
        return fail("upper dialogue layout is invalid");
    }
    if (!PalNativeUi_GetDialogLayout(true, true, &dialog) ||
        dialog.text.x >= dialog.portrait.x ||
        dialog.page_lines != 4u) {
        return fail("lower dialogue layout is invalid");
    }
    if (!PalNativeUi_GetDialogLayout(false, false, &dialog) ||
        dialog.text.width != PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH - 8u) {
        return fail("portrait-free dialogue layout is invalid");
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
