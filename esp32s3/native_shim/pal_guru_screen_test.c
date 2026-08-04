#include "pal_guru_screen.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    TEST_WIDTH = 160,
    TEST_HEIGHT = 128,
    TEST_PIXELS = TEST_WIDTH * TEST_HEIGHT,
};

static uint16_t whole[TEST_PIXELS];
static uint16_t strips[TEST_PIXELS];

static int
fail(
    const char *message)
{
    fprintf(stderr, "pal_guru_screen_test: %s\n", message);
    return 1;
}

int
main(void)
{
    PalGuruScreenText text;
    size_t red = 0u;
    size_t white = 0u;
    uint16_t first_y;

    PalGuruScreen_BuildText(&text,
        "/workspace/source/res.c", 656u, "a803286-dirty",
        "sprite pointer invalid");
    if (strcmp(text.file, "FILE res.c") != 0 ||
        strcmp(text.line, "LINE 656") != 0 ||
        strcmp(text.revision, "GIT a803286-dirty") != 0 ||
        strcmp(text.reason, "sprite pointer invalid") != 0) {
        return fail("diagnostic text mismatch");
    }
    if (!PalGuruScreen_RenderRgb565Strip(&text, whole, TEST_PIXELS,
            TEST_WIDTH, TEST_HEIGHT, 0u, TEST_HEIGHT,
            0x0000u, 0xf800u, 0xffffu)) {
        return fail("whole-frame render failed");
    }
    for (first_y = 0u; first_y < TEST_HEIGHT; first_y += 11u) {
        uint16_t rows = (uint16_t)(TEST_HEIGHT - first_y);

        if (rows > 11u) {
            rows = 11u;
        }
        if (!PalGuruScreen_RenderRgb565Strip(&text,
                strips + (size_t)first_y * TEST_WIDTH,
                (size_t)rows * TEST_WIDTH,
                TEST_WIDTH, TEST_HEIGHT, first_y, rows,
                0x0000u, 0xf800u, 0xffffu)) {
            return fail("strip render failed");
        }
    }
    if (memcmp(whole, strips, sizeof(whole)) != 0) {
        return fail("strip and whole-frame output differ");
    }
    for (first_y = 0u; first_y < TEST_PIXELS; first_y++) {
        if (whole[first_y] == 0xf800u) {
            red++;
        } else if (whole[first_y] == 0xffffu) {
            white++;
        } else if (whole[first_y] != 0x0000u) {
            return fail("unexpected pixel color");
        }
    }
    if (red < 300u || white < 100u || whole[0] != 0x0000u) {
        return fail("panic screen is blank or lacks a black background");
    }
    puts("pal_guru_screen_test: OK");
    return 0;
}
