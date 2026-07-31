# AGENTS.md

## Communication

Do not send optional commentary.

```python
disable_send_optional_commentary = True
```

## Active development lines

This repository has two distinct ESP32-S3 ports. Do not mix their hardware,
memory, storage, or bus assumptions.

### Primary: Cardputer ADV extreme

- Board: M5Stack Cardputer ADV K132-Adv, not the original Cardputer.
- Development line: the `extreme` branch. Always check the current branch and
  dirty worktree before editing; do not overwrite unrelated user changes.
- Flash: 8MB.
- PSRAM: none.
- LCD: ST7789, physical/native resolution 240x135.
- TF: independent SPI2 bus at 20MHz; LCD uses SPI3. There is no CoreS3-style
  LCD-D/C versus TF-MISO pin handoff on this board.
- Runtime owns exactly two named 320x200x8-bit logical screens and one 4KB
  RGB565 DMA strip. Their declarations are in
  `esp32s3/main/cardputer_extreme_memory.h`.
- Default profile has no audio. `CARDPUTER_EXTREME_MUSIC=ON` is a separate,
  fixed-storage RIX/OPL2 music-only profile; MIDI, VOC, and SFX remain out.
- `CARDPUTER_EXTREME_CHAPTER_CACHE=ON` is a separate TF-backed chapter-cache
  experiment, not an unconditional full-game proof.

The architecture overview, build/flash commands, and acceptance caveats are
in `esp32s3/README.md`. Treat checker output and the ELF/map from the current
build as authoritative for measured bytes; prose measurements can age. The
enforced limits live in `esp32s3/check_cardputer_extreme.py`.

### Secondary: CoreS3 SE

- Board: M5Stack CoreS3 SE, not CoreS3.
- Flash: 16MB.
- PSRAM: 8MB.
- The established contract treats roughly 300KB of fast SRAM as scarce and
  uses named PSRAM buffers for larger mutable working sets.
- CoreS3 SE LCD and TF share SPI signals; GPIO35 is both TF MISO and LCD D/C.
  Follow the pin-direction handoff in the existing board code. Do not apply
  that rule to Cardputer ADV.
- Audio/SFX are still excluded from the CoreS3 SE target app.

Useful hardware references:

- `/home/john/work/CardPuterADV/esp-walkie-talkie`
- `/home/john/work/CoreS3SE`
- `/home/john/esp-idf/examples/storage/sd_card/sdspi`

## Non-negotiable embedded contract

- No project calls to `malloc`, `calloc`, `realloc`, `free`, C++ new/delete,
  or hidden allocator-backed containers on target paths.
- No runtime YJ1/YJ2/LZ4 or other asset decompression. Decode and convert on
  the host when building packs.
- Use normal, named, fixed-lifetime `uint8_t` SRAM/PSRAM buffers and typed
  `const` views for mapped read-only data. Do not add a memory pool, tier
  allocator, or generic cache framework.
- Keep FatFS LFN heap support and dynamic FatFS buffers disabled. Runtime
  filenames must stay short (`0:/pal_tf.pak`, `0:/EVENT.STA`, `0:/b00.pak`,
  and similar).
- The PAL world, collision, scripts, saves, and battle animation retain
  canonical 320x200 semantics. Display layout and cameras are presentation
  transforms and must never be written back into gameplay state.
- Verify placement from ELF/map artifacts with `size`, `objdump`, `nm`, and
  linker maps. Source inspection alone is not a memory proof.
- Preserve existing dirty worktree changes unless they are clearly part of
  the requested task. Stage explicit paths rather than `git add -A`.

## Local data and pinned font

Use the audited PAL data set unless the user specifies another:

```text
/mnt/hgfs/deb13/PAL
```

The small-screen UI uses the 10px monospaced Traditional Chinese Fusion Pixel
Font release locked in `tools/pal_ui_layout/font.py`:

```text
fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip
SHA-256 2e695c27627bf09683df2afe69b086fa3cd3e52795bce39fded9cc509188b5fe
size 18,220,904 bytes
```

The archive is supplied out of band through `FONT10_ARCHIVE`; do not vendor
the release zip. The OFL-1.1 license is kept at
`third_party/fusion-pixel-font/OFL.txt`. Host tooling subsets the actual
`WORD.DAT`, `M.MSG`, and native-label corpus. Missing glyphs or a wrong archive
must fail the build.

## Resource and state architecture

### Normal Cardputer extreme packs

- `tools/pal_pack_build.py` is the host conversion/pack builder.
- `tools/pal_pack_layout_cardputer_extreme.json` is the active sparse
  chapter-candidate policy.
- `/tmp/pal_cardputer_extreme_nor.pak` is the active NOR image.
- `/tmp/pal_cardputer_extreme_tf.pak` is the small runtime-active TF image.
- `/tmp/pal_cardputer_extreme_full.pak` is a complete decoded/native TF mirror.
  It intentionally overlaps NOR and active TF resources, but current firmware
  does not index it because its TOC exceeds the 2KB active-index budget.
- Pack-set ID, whole-image CRC, manifest hashes, archive/chunk format, and
  source hashes are part of the fail-closed contract.

Do not describe `pal_full.pak` as swap or as a runtime fallback. It is an
offline-complete source for future pack generation until a bounded streaming
index is deliberately implemented.

### Chapter cache

`tools/pal_chapter_pack_build.py` produces:

- immutable `pal_core.pak`;
- active `pal_tf.pak`;
- complete `pal_full.pak`;
- 15 conservative bundles `b00.pak` through `b14.pak`;
- `chapter_manifest.json`;
- `EVENT.DEF`.

The target compares the required bundle SHA-256 with the replaceable NOR cache
at save load and bundle transitions. A mismatch enters a native 240x135
`LOADING` screen, copies sequentially from TF, verifies TF and NOR readback,
then writes the commit record last. The implementation is in:

- `esp32s3/engine_bridge/pal_engine_chapter_cache.[ch]`
- `esp32s3/check_cardputer_extreme_chapter_cache.py`
- `esp32s3/partitions_cardputer_extreme_cache.csv`

The catalog currently covers generated scene resources broadly, but this is
still labelled a candidate. Do not claim a complete story route without
deterministic route evidence. The established finite pre-Suzhou profile
selects scenes 1..20 plus 22 and replaces the unresolved scene-22 to scene-21
transition with a visible chapter-complete endpoint; see
`esp32s3/README.md` and the closure fields in
`tools/pal_pack_layout_cardputer_extreme.json`.

### TF-backed event/scene state

Event state is not fully resident in SRAM and is not a block-device swap:

- all 5,369 32-byte event records occupy 42 logical 4KB pages;
- exactly three 4KB event pages are resident;
- current-scene pages are pinned and other pages use bounded recency-based
  replacement;
- all 300 scene records share the same durable journal transaction model;
- `EVENT.DEF` is the immutable template;
- `EVENT.STA` is the live journal, with `EVENT.TMP`/`EVENT.BAD` used for
  atomic replacement and recovery.

Source of truth:

- `embedded/pal_event_pager.[ch]`
- `embedded/pal_event_journal.[ch]`
- `esp32s3/engine_bridge/pal_engine_event_state.[ch]`
- `esp32s3/engine_bridge/pal_engine_extreme_save.inc`

Writes emit structured `PAL_TFIO v=1` records containing reason, dirty bytes,
storage/API bytes, calls, syncs, elapsed time, hit/miss counts, and software
write amplification. These counters do not expose the card controller's
internal NAND writes or wear. Keep the instrumentation when changing
persistence behavior.

## Small-screen UI layout compiler

The build-time UI compiler is the source of truth for native display geometry:

- entry point: `tools/pal_layout_check.py`
- package and design notes: `tools/pal_ui_layout/README.md`
- geometry: `tools/pal_ui_layout/geometry.py`
- semantic screens: `tools/pal_ui_layout/screens.py`
- deterministic candidate solver: `tools/pal_ui_layout/solver.py`
- font/corpus packing: `tools/pal_ui_layout/font.py`
- map/battle presentation cameras: `tools/pal_ui_layout/camera.py`
- C header emitter: `tools/pal_ui_layout/emit_c.py`
- real-asset previews: `tools/pal_ui_layout/preview.py`
- full compiler/check: `tools/pal_ui_layout/check.py`
- generated target headers: `esp32s3/main/generated/`
- target table consumer: `embedded/pal_ui_layout_runtime.[ch]`
- target FONT10 view: `embedded/pal_font10_cache.[ch]`
- fixed-storage RLE strip compositor: `embedded/pal_ui_rle_downsample.[ch]`
- Cardputer whole-stage scaler:
  `esp32s3/main/cardputer_extreme_scaler.[ch]`

Certified profiles are 240x135 and 160x128. Other positive resolutions and
safe insets may be explored but are unsupported until every check and visual
review passes.

The solver enumerates a small fixed set of semantic layouts (`full`,
`compact`, `single_column`, `paged`, `text_only`). It may remove decoration,
reduce columns, split status/equipment into pages, or move details, but it
must not delete a functional action or change its semantic return value.
Keep the 10px font; do not solve tight space by silently shrinking text.

Generated output owns:

- all rectangles, offsets, grids, rows/columns, page capacities, and focus
  order;
- copies of the stable public screen/element/camera IDs defined in
  `embedded/pal_ui_layout_runtime.h`, with compile-time equality checks;
- loading-screen coordinates;
- exact rational scales, Q16 coefficients, and nearest-centre axis maps;
- map player anchors and battle HUD-free camera policy;
- real RLE extent catalogs for RGM, BALL, F, ABC, FIRE, DATA #9 UI sprites,
  and DATA #10 battle effects;
- FONT10 identity.

The target may perform fixed O(3) live battle subject union/clamp against the
generated policy, but it must not enumerate layouts or derive replacement
coefficients. Item/magic layouts are live slot templates: bind current object
IDs, enabled/count state, and cursor at runtime; do not treat preview words or
fixture return values as production content.

Map/battle presentation should remain player-focused. Battle must reserve the
generated HUD exclusion. For the first production integration, render the
complete canonical 320x200 battle frame, apply one camera crop/uniform scale
into the HUD-free content rectangle, then draw the native HUD. This preserves
one global sampling phase for backgrounds, sprites, color shifts, mono/shadow
effects, and full-screen effects. Independently scaling each RLE sprite is a
different visual semantic unless camera origin/global phase is carried into
the compositor.

### Current integration boundary

Do not infer production integration merely because generators and smokes pass.
At the time of this update:

- Cardputer production presentation consumes the generated 240x135 stage
  rectangle and exact axis maps.
- Semantic menu/battle tables, FONT10, live battle camera evaluation, and the
  RLE compositor are compiled and contract-tested.
- Generated map player anchors/camera vectors have no production consumer yet;
  the current Cardputer path uniformly scales the full canonical framebuffer.
- Legacy game menu/dialog/status/equipment/battle draw call sites still render
  into the canonical 320x200 framebuffer; native small-screen draw migration
  remains work.

Before claiming this has changed, search for real non-test callers of
`PalUiLayout_GetScreen`, `PalUiLayout_GetFocusElement`,
`PalUiLayout_GetListTemplate`, `PalUiLayout_GetListSlot`,
`PalUiLayout_GetListPageCount`, `PalUiLayout_ResolveBattleCamera`,
`PalFont10_FindGlyph`, and `PalUiRle_ComposeRgb565Strip`, then prove them with
target-shaped screenshots and deterministic gameplay traces. Startup
validation or FONT10 identity checks alone are not live drawing integration.

### Required host visual review

UI work is not complete after unit tests. Generate the actual-corpus,
actual-asset previews locally, create contact sheets if needed, and inspect
every screen/page and both camera modes at original pixel detail. Check text
legibility, focus visibility, bounds, page indicators, portrait/item/battle
scales, HUD occlusion, and black side gutters.

```sh
python3 -B tools/pal_layout_check.py \
  --data-dir /mnt/hgfs/deb13/PAL \
  --font10-archive "$FONT10_ARCHIVE" \
  --output-dir /tmp/pal-ui-review-new
```

Use a new output directory for every review; this command must not rewrite
production headers. PPM files are the compiler's deterministic preview output.
Any PNG/contact sheets are reviewer-created derivatives. These are host
mockups using real data, not live game screenshots, and must be described as
such. After any semantic/camera/font/scaler change, regenerate rather than
reusing an older review directory.

## Input mapping

The Cardputer mapping is defined in
`esp32s3/engine_bridge/pal_engine_target_input.c` and the keyboard matrix in
`esp32s3/main/cardputer_extreme_board.c`:

- backtick maps to Escape;
- semicolon maps to Up;
- comma maps to Left;
- period maps to Down;
- slash maps to Right;
- Enter/Space map to Search/confirm;
- the printed Fn arrow layer remains an alias.

Keep press/release pairing stable when Fn or Shift changes between edges.

## Verification commands

Use the narrowest relevant checks while iterating, then the profile gate.

### UI/layout/font

```sh
PYTHONPATH=tools python3 -B -m unittest discover \
  -s tools -p 'test_pal_ui_layout_*.py'

make -C embedded ui-layout-runtime-check
make -C embedded ui-layout-production-check ui-rle-downsample-check

make -C esp32s3 \
  FONT10_ARCHIVE="$FONT10_ARCHIVE" \
  cardputer-extreme-ui-layout-check

make -C esp32s3 cardputer-extreme-scaler-check
```

Generation is an explicit mutation:

```sh
make -C esp32s3 \
  FONT10_ARCHIVE="$FONT10_ARCHIVE" \
  cardputer-extreme-ui-layout-generate
```

The check target must compare against committed generated headers and must not
silently rewrite them.

### Cardputer extreme

```sh
make -C esp32s3 \
  FONT10_ARCHIVE="$FONT10_ARCHIVE" \
  cardputer-extreme-check

make -C esp32s3 \
  FONT10_ARCHIVE="$FONT10_ARCHIVE" \
  cardputer-extreme-music-check

make -C esp32s3 \
  FONT10_ARCHIVE="$FONT10_ARCHIVE" \
  cardputer-extreme-chapter-cache-check
```

Useful narrow checks:

```sh
make -C esp32s3 cardputer-extreme-chapter-cache-logic-check
make -C esp32s3 cardputer-extreme-event-pager-check
make -C esp32s3 cardputer-extreme-event-journal-check
make -C esp32s3 FONT10_ARCHIVE="$FONT10_ARCHIVE" \
  cardputer-extreme-native-smoke
make -C esp32s3 FONT10_ARCHIVE="$FONT10_ARCHIVE" \
  cardputer-extreme-save-check
```

TF preparation and flashing are external mutations. Use the exact targets in
`esp32s3/README.md`, keep firmware/NOR/TF from one manifest together, and
require an explicit user request before flashing or replacing card contents.
Default hardware port is normally `/dev/ttyACM0`, but discover it rather than
assuming.

### CoreS3 SE and general contract

```sh
make -C esp32s3 check
make -C esp32s3 native-smoke
make -C embedded contract-check
make -C unix EMBEDDED_CONTRACT=1 contract-check
python3 -B tools/embedded_contract_check.py --root .
```

The native harness may use SDL internally; project engine/resource paths must
still pass no-heap/no-runtime-decompression checks.

## Source-of-truth rule

Keep this file compact and navigational. Detailed measured results belong in
the checker output or focused README, not in an ever-growing historical list
of every embedded slice. When behavior changes:

1. update the implementation and repeatable checker;
2. update the focused README that owns the feature;
3. update this file only if the target, invariant, canonical path, integration
   boundary, or primary command changed;
4. remove superseded claims instead of appending contradictory history.
