#ifndef CARDPUTER_EXTREME_BOARD_H
#define CARDPUTER_EXTREME_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M5Stack Cardputer ADV (K132-Adv), not the original Cardputer.
 *
 * The generated 240x135 profile owns the LCD and stage geometry.  The board
 * presents its indexed source through that stage without an RGB565
 * framebuffer.
 */
#include "generated/pal_ui_layout_240x135.h"

#if !defined(PAL_UI_GENERATED_COEFFICIENTS_ONLY) || \
    PAL_UI_GENERATED_COEFFICIENTS_ONLY != 1u
#error "Cardputer extreme requires Python-generated UI coefficients"
#endif

#define CARDPUTER_EXTREME_LCD_WIDTH PAL_UI_GENERATED_DISPLAY_WIDTH
#define CARDPUTER_EXTREME_LCD_HEIGHT PAL_UI_GENERATED_DISPLAY_HEIGHT
#define CARDPUTER_EXTREME_PAL_VIEW_WIDTH PAL_UI_GENERATED_STAGE_WIDTH
#define CARDPUTER_EXTREME_PAL_VIEW_HEIGHT PAL_UI_GENERATED_STAGE_HEIGHT
#define CARDPUTER_EXTREME_PAL_VIEW_X PAL_UI_GENERATED_STAGE_X
#define CARDPUTER_EXTREME_PAL_VIEW_Y PAL_UI_GENERATED_STAGE_Y
#define CARDPUTER_EXTREME_TF_MOUNT_POINT "/sdcard"

/*
 * Logical actions produced by CardputerExtreme_PollKeyboard().
 *
 * Directions use the Cardputer's printed Fn layer:
 *   Fn+;  up, Fn+, left, Fn+. down, Fn+/ right.
 * Enter, Space, left Ctrl, and BtnA are Search.  Fn+`/Alt/Backspace are
 * Menu.  The remaining bits match SDLPAL's desktop battle shortcuts.
 */
enum CardputerExtremeAction {
    CARDPUTER_EXTREME_ACTION_UP = 1u << 0,
    CARDPUTER_EXTREME_ACTION_DOWN = 1u << 1,
    CARDPUTER_EXTREME_ACTION_LEFT = 1u << 2,
    CARDPUTER_EXTREME_ACTION_RIGHT = 1u << 3,
    CARDPUTER_EXTREME_ACTION_MENU = 1u << 4,
    CARDPUTER_EXTREME_ACTION_SEARCH = 1u << 5,
    CARDPUTER_EXTREME_ACTION_PAGE_UP = 1u << 6,
    CARDPUTER_EXTREME_ACTION_PAGE_DOWN = 1u << 7,
    CARDPUTER_EXTREME_ACTION_REPEAT = 1u << 8,
    CARDPUTER_EXTREME_ACTION_AUTO = 1u << 9,
    CARDPUTER_EXTREME_ACTION_DEFEND = 1u << 10,
    CARDPUTER_EXTREME_ACTION_USE_ITEM = 1u << 11,
    CARDPUTER_EXTREME_ACTION_THROW_ITEM = 1u << 12,
    CARDPUTER_EXTREME_ACTION_FLEE = 1u << 13,
    CARDPUTER_EXTREME_ACTION_FORCE = 1u << 14,
    CARDPUTER_EXTREME_ACTION_STATUS = 1u << 15,
};

bool CardputerExtreme_Begin(void);
bool CardputerExtreme_SetBacklight(bool enabled);

bool CardputerExtreme_MountTf(void);
bool CardputerExtreme_TfMounted(void);
/*
 * Kept as a target-board hook even though Cardputer ADV gives TF (SPI2) and
 * LCD (SPI3) independent buses.  Unlike CoreS3 SE, no pin-direction handoff
 * is required before FatFS access.
 */
void CardputerExtreme_PrepareTfAccess(void);
/*
 * Same-volume save transaction primitives.  Unlink optionally treats a
 * missing short-name file as success; rename never replaces an existing
 * destination.  The implementation is FatFS on target and an isolated
 * save-directory adapter in the deterministic host.
 */
bool CardputerExtreme_SaveUnlink(const char *path, bool missing_ok);
bool CardputerExtreme_SaveRename(const char *old_path, const char *new_path);

bool CardputerExtreme_KeyboardReady(void);
bool CardputerExtreme_PollKeyboard(void);
/*
 * Return one edge-triggered logical key event.  Directions are reported as
 * i/j/k/l for the engine bridge, Search as CR/space, Menu as backspace, and
 * the printed [/] keys as page-up/page-down.  Releases always use the key
 * chosen on the corresponding press, even if Fn or Shift has already been
 * released.  At most one edge is exposed between empty polls so the engine's
 * event-drain loop samples a key-down state before a queued key-up edge.
 */
bool CardputerExtreme_PollKey(uint8_t *ascii, bool *pressed);
uint32_t CardputerExtreme_ActionMask(void);
uint32_t CardputerExtreme_ActionPressedMask(void);
uint32_t CardputerExtreme_ActionReleasedMask(void);

/*
 * Raw physical state after TCA8418-to-Cardputer remapping.  Bit
 * (row * 14 + column) represents one cell of the logical 4x14 keyboard.
 * This lets an engine bridge choose a different mapping without another
 * keyboard driver.
 */
uint64_t CardputerExtreme_PhysicalKeyMask(void);

/*
 * palette_rgba is the SDL_Color byte layout used by the fixed SDL shim:
 * 256 consecutive { r, g, b, a } entries.  Conversion and generated-stage
 * scaling are done a strip at a time in the 4KB DMA buffer.
 */
bool CardputerExtreme_FlushIndexedFramebuffer(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba);
bool CardputerExtreme_FlushArgb8888Texture(
    const void *pixels,
    uint16_t width,
    uint16_t height,
    uint16_t pitch);
#if defined(PAL_EXTREME_CHAPTER_CACHE)
/* Draw the cache-rebuild screen directly at the generated display resolution. */
void CardputerExtreme_ShowLoading(uint8_t percent);
#endif
void CardputerExtreme_ShowError(const char *line1, const char *line2);

#ifdef __cplusplus
}
#endif

#endif
