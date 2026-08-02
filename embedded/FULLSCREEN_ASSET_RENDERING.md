# Full-screen asset rendering

This is the narrow full-canvas exception to
[`RESPONSIVE_RENDERING.md`](RESPONSIVE_RENDERING.md). It applies only to game
assets whose format and engine semantics define a complete 320x200 canvas; it
is not a general small-screen transform.

The currently audited asset classes are:

- decoded `FBP` images, including battle backgrounds and the fixed
  opening/status/equipment/ending canvases;
- `RNG` movies, whose delta command cursor addresses one logical 320x200
  canvas across frames.

This list is an allowlist. Adding another asset class is a case-by-case design
decision; dimensions or byte size alone never opt an asset into this path. Maps,
tile layers, RLE sprites, portraits, item/equipment icons, fonts, text, menus,
cursors, HUD elements, and hit regions keep their existing responsive or
per-asset drawing rules. In particular, this rule must not change the map
viewport or cause a completed map/UI frame to be resized.

## Required transform

For a qualifying full-screen asset, map the 320x200 source directly into the
entire physical indexed framebuffer:

- map X and Y independently, so the panel's aspect ratio is respected and the
  source aspect ratio is deliberately not preserved;
- use deterministic nearest-centre sampling on both axes;
- fill the destination, with no crop, letterbox, viewport pan, or filtering;
- transform only at the drawing boundary and never write physical coordinates
  back into scripts, events, collision, battle state, or saves;
- never render a complete 320x200 composition and then scale that framebuffer.

`embedded/pal_fullscreen_stretch.h` is the common allocation-free source of
truth for the forward coordinate mapping, inverse destination ranges, complete
indexed blits, and indexed scanline blits.

## Integration boundaries

`PAL_FBPBlitToSurface` applies the common transform to a resident decoded FBP.
`PAL_FBPBlitChunkToSurface` applies the same transform while retaining only one
320-byte source row. A native logical screen must never be passed as the
64,000-byte FBP input buffer.

The RNG decoder retains the canonical 64,000-pixel command cursor, but each
changed canonical pixel is written through the common inverse mapping into the
native surface. Encoded frames are consumed through a bounded read-at window;
a frame may be larger than that window. The provider validates the frame table
once, then `PalEngineBridge_ReadNativeRngFrameRange` serves only checked ranges.
The returned frame descriptor is valid only while the active pack/overlay set
is unchanged.

The background transform does not implicitly transform overlays. A status
portrait, equipment icon, battle fighter, label, or cursor is still placed by
its existing responsive draw path and is resized only when that asset's own
layout rule requires it. Text and numbers are always rasterized directly at
their native physical size.

## Audit and regression checks

Before adding another class, document its format semantics and call sites here,
then add a focused regression test. Do not route a generic RLE, map, sprite, or
UI blitter through the helper.

Run:

```sh
make -C embedded fullscreen-stretch-check
make -C esp32s3 cardputer-extreme-rng-decoder-check
make -C esp32s3 cardputer-extreme-pack-smoke
```

The first check exhaustively compares the forward and inverse mappings across
small up/down/non-uniform dimensions. The RNG check compares resident and
one-to-three-byte-window decoding under ASan/UBSan at 240x135, 160x128,
320x200, and an enlarged surface. The real-pack smoke proves that an encoded
RNG frame larger than the Cardputer auxiliary screen is read in bounded ranges.
