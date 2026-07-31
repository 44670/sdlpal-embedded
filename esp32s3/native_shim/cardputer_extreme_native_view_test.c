#include "cardputer_extreme_native_view.h"

#include <stdio.h>
#include <string.h>

static uint8_t pixels[320u * 200u];
static uint8_t palette[256u * 4u];
static uint8_t output[PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH * 2u * 2u];

static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}

int main(void)
{
    PalNativeUiViewport viewport;
    uint16_t source;
    uint16_t x;
    uint16_t y;

    PalNativeUi_SetWorldView();
    if (!PalNativeUi_GetViewport(&viewport) ||
        !CardputerExtreme_NativeViewValidate()) {
        return fail("native view validation failed");
    }
    if (!CardputerExtreme_NativeViewSourceX(0u, &source) ||
        source != viewport.source_x ||
        !CardputerExtreme_NativeViewSourceX(
            viewport.width - 1u, &source) ||
        source != viewport.source_x + viewport.width - 1u) {
        return fail("native x mapping is not 1:1");
    }
    if (!CardputerExtreme_NativeViewSourceY(0u, &source) ||
        source != viewport.source_y ||
        !CardputerExtreme_NativeViewSourceY(
            viewport.height - 1u, &source) ||
        source != viewport.source_y + viewport.height - 1u) {
        return fail("native y mapping is not 1:1");
    }

    for (y = 0; y < 200u; y++) {
        for (x = 0; x < 320u; x++) {
            pixels[(size_t)y * 320u + x] = (uint8_t)(x + y);
        }
    }
    for (x = 0; x < 256u; x++) {
        palette[(size_t)x * 4u + 0u] = (uint8_t)x;
        palette[(size_t)x * 4u + 1u] = 0u;
        palette[(size_t)x * 4u + 2u] = 0u;
        palette[(size_t)x * 4u + 3u] = 0xffu;
    }
    memset(output, 0, sizeof(output));
    if (!CardputerExtreme_CopyIndexedNativeStrip(
            pixels, 320u, palette, 0u, 2u, output,
            viewport.width * 2u, sizeof(output))) {
        return fail("native strip conversion failed");
    }
    if (output[0] != (uint8_t)(
            (pixels[(size_t)viewport.source_y * 320u +
                    viewport.source_x] & 0xf8u)) ||
        output[1] != 0u) {
        return fail("native strip sampled the wrong source pixel");
    }

    PalNativeUi_FocusLogical(0, 0, PAL_NATIVE_UI_VIEW_UI);
    if (!CardputerExtreme_NativeViewSourceX(0u, &source) || source != 0u ||
        !CardputerExtreme_NativeViewSourceY(0u, &source) || source != 0u) {
        return fail("native view did not follow a UI focus");
    }
    return 0;
}
