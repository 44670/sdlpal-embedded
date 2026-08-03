# Responsive rendering

The embedded engine renders directly into each device's native indexed
framebuffer. Legacy 320x200 coordinates remain part of PAL's resource, script,
event, collision, battle, and save contracts; they are converted only at draw
boundaries and are never replaced with physical coordinates in gameplay state.

Keep these coordinate spaces separate:

1. legacy gameplay/resource coordinates;
2. coordinates within an individual source asset;
3. final physical framebuffer coordinates.

The LCD presenter performs only indexed-to-RGB565 conversion. It must not
resize a completed frame.

## Maps

Maps use a 1:1 viewport, not scaling. Tiles, cover layers, sprites, effects,
and animations are clipped directly into the native framebuffer. The legacy
world camera remains `gpGlobals->viewport`; any physical centering offset is a
draw-only translation and must not become a second camera or enter game state.

The draw-time offset is calculated in C from the framebuffer dimensions so the
canonical party anchor `(160,112)` lands at the physical screen center. Do not
scale map tiles, map sprites, or a completed map frame.

## Full-canvas game assets

Only explicitly audited game assets that semantically define a complete
320x200 canvas may fill the physical framebuffer by independent-axis sampling.
The current allowlist and common implementation are documented in
[`FULLSCREEN_ASSET_RENDERING.md`](FULLSCREEN_ASSET_RENDERING.md).

That rule does not apply to maps or to the UI layered over a background.

## Text and UI elements

Text and numbers are rasterized directly at their final physical coordinates.
Small-screen text uses native 10x10 FONT10 cells with a 10-pixel advance and
must never be downscaled. Pre-authored message line, page, and control-code
boundaries are preserved; do not reflow them.

Sprites, portraits, icons, boxes, menus, HUD elements, cursors, and hit regions
are handled case by case. Start from PAL's original draw functions and assets,
then make only the position or per-material size adjustment required by the
physical canvas. There is no generic UI scaler, semantic layout solver,
replacement menu, or second scrolling-screen mode. If a list is taller than
the screen, scroll the list items rather than the rendered screen.

Each UI draw function calculates its rectangles directly in C from the current
framebuffer, FONT10 cell size, content count, and actual source material. Keep
these calculations next to the draw code. Do not use Python to calculate
coordinates, generated layout headers, resolution-specific coordinate tables,
or a parallel layout description. The required native canvases are 240x135 and
160x128, but formulas use the current width and height rather than a board name.

Per-material downsampling must be bounded and deterministic and preserve RLE
transparency. The current dialogue-portrait fitter preserves aspect ratio;
other material fitting remains case by case. No asset rule implies that
neighboring text, icons, or controls inherit the same transform.

## Acceptance

Follow [`UI_REVIEW_SOP.md`](UI_REVIEW_SOP.md). Final evidence must come from
the real gameplay loop with real PAL data; store captures under `./tmp_ui/` and
inspect every changed screen. Use the host control surface described in
[`../unix/WEBSOCKET_HARNESS.md`](../unix/WEBSOCKET_HARNESS.md) to reach states
repeatably.
