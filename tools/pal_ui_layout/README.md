# SDLPAL small-display layout compiler

This package is the build-time source of truth for native UI geometry.  It is
not a target-side layout engine: ESP32 builds consume generated `static const`
tables and compact font bitmaps only.

The compiler accepts a display resolution, safe area, extracted font metrics,
asset bounds, and semantic screen descriptions.  It deterministically selects
one of a small number of screen variants (`full`, `compact`,
`single_column`, `paged`, or `text_only`), then emits:

- resolved rectangles, offsets, grids, page capacities, and focus order;
- map and battle camera policy/test vectors;
- a JSON decision manifest;
- a header-only `static const` C table;
- deterministic PPM previews.

`240x135` and `160x128` are certified profiles.  Other positive resolutions
may be inspected, but are not considered supported until all checks pass.
Safe insets can be supplied as `WIDTHxHEIGHT@LEFT,TOP,RIGHT,BOTTOM`.

The complete compiler is:

```sh
python3 -B tools/pal_layout_check.py \
  --data-dir /mnt/hgfs/deb13/PAL \
  --font10-archive /path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  --output-dir /tmp/pal-ui-layout \
  --target-header-dir esp32s3/main/generated
```

The Cardputer build exposes the same operation as one-click Make targets:

```sh
make -C esp32s3 \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  cardputer-extreme-ui-layout-generate

make -C esp32s3 \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  cardputer-extreme-ui-layout-check
```

The check target rewrites its expected files in a disposable output directory,
then runs the compiler with `--check` against the committed target headers. It
never rewrites `esp32s3/main/generated`. Use a new directory for visual review
so unrelated files from an earlier compiler version cannot be mistaken for
current output. The same `FONT10_ARCHIVE` is mandatory for
`cardputer-extreme-pack-build`, `cardputer-extreme-music-pack-build`, and the
chapter-cache build/check targets.  The normal profiles add the matching
corpus-subsetted FONT10 chunk to the generated NOR pack; the chapter profile
adds the identical chunk to immutable `pal_core.pak`.  Firmware startup
rejects a missing or identity-mismatched chunk in every extreme profile.

Add `--check` to regenerate in memory twice, prove byte determinism, and
compare the result with existing output. Every coordinate and coefficient in
the native/generated layout layer—camera ratio, scale, fixed-point Q shift,
nearest-centre step/phase, exact stage destination-to-source sample map,
portrait/item/battle source bound, page capacity, grid size, focus order, and
chapter-cache loading-screen placement—originates in this Python run and is
written to a generated header. Legacy 320x200 draw calls remain outside this
layer until migrated. The target is not allowed to derive replacement
coefficients or choose a layout.

The PAL world, script, save, collision, and battle-animation state retains its
legacy 320x200 coordinate semantics.  Layout and camera transforms are derived
at the presentation boundary and must never be written back into gameplay
state.

The target font is the 10px monospaced Traditional Chinese variant of Fusion
Pixel Font.  Host tooling verifies a pinned release archive and extracts only
the glyphs used by `WORD.DAT`, `M.MSG`, and native UI labels.  BDF/TTF parsing
and rasterization are never linked into the target.  The upstream OFL-1.1 text
is preserved in `third_party/fusion-pixel-font/OFL.txt`.

The compiler also audits decoded RLE dimensions directly from the real
`RGM.MKF`, `BALL.MKF`, `F.MKF`, `ABC.MKF`, `FIRE.MKF`, `DATA.MKF #9` UI
sprites, and `DATA.MKF #10` battle effects.  Original assets are unchanged.  A
fixed-storage streaming compositor is implemented and parity-tested; it can
consume those policies and sample read-only RLE data directly into an RGB565
DMA strip without a decoded sprite buffer.  Native menu/battle draw call sites
are not migrated to that compositor yet.

Generated item and magic rows are slot templates, not a frozen copy of the
preview fixtures. The target helpers expose bounded slot lookup and page-count
calculation for future binding of live object IDs, enabled/count state, and
cursor without target-side layout solving. The legacy production call sites
do not use those helpers yet.

## Integration status

Passing generator and smoke tests does not imply live drawing integration.
This table is the current boundary:

| Capability | Generated | Compiled on target | Live production caller | Present proof |
| --- | --- | --- | --- | --- |
| Whole 320x200 frame to native stage | stage rect and exact axis maps | yes | yes | scaler and production checks |
| Map player anchor/camera | policy and vectors | table accessors | no | Python vectors |
| Battle crop plus native HUD | HUD-free policy and vectors | fixed O(3) resolver | no | C/Python runtime smoke |
| FONT10 renderer | subset and identity | pack view/glyph lookup | identity check only; no glyph draw | font/pack smokes |
| Item/magic live rows | slot/page templates | bounded accessors | no | runtime smoke |
| Bounded RLE frame sampling | real extent catalog | strip compositor | no | downsample smoke |

The current live Cardputer presentation still uniformly scales the complete
canonical framebuffer. Before changing a row to “yes”, demonstrate a non-test
caller and retain a target-shaped screenshot or deterministic gameplay trace
as its acceptance proof.

## C/Python parity contract

`pal_ui_layout.parity` resolves semantic fixture screens and emits the exact
map/battle policies plus close-focus and fit-all vectors on the host.
`emit_c.py` freezes their rectangles, page/focus tables, exact rational
scales, Q16 sampler steps/phases, exact stage axis maps, projected subject
rectangles, and FONT10 identity into one generated header per display profile.
`embedded/pal_ui_layout_runtime.c` bounds-checks and consumes those tables.  It
also runs the deliberately bounded O(3) integer battle subject union/clamp
policy for live player/actor/target inputs; it does not enumerate UI
candidates, choose a screen variant, or derive replacement coefficients.  The
Cardputer indexed and compatibility ARGB presentation paths read the generated
axis maps and perform no pixel-loop division.

Run both certified-profile C checks with:

```sh
make -C embedded ui-layout-runtime-check
make -C embedded ui-layout-production-check
```

The smoke is compiled separately against generated 240x135 and 160x128
headers, validates every table span and focus target, feeds both close and far
live subject sets through the C evaluator, and compares its crop/projection
against Python-generated expected vectors.  It also verifies that projected
subjects stay inside the HUD-free content rectangle.  It is linked with the
no-heap traps and checked for heap symbols.  The production check also audits
every runtime accessor and scaler sample back to generated macros or
`static const` table fields, then compiles each committed profile into a
standalone object.  Generated table symbols are capped at 26KB Flash, the
complete layout runtime object at 35KB Flash, and writable `.data`/`.bss` at
zero bytes.  The current caps include the real DATA #9/#10 extent catalog and
the exact HUD-free battle fit maps.
