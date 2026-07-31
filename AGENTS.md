# AGENTS.md

## Communication

Do not send optional commentary.

```python
disable_send_optional_commentary = True
```

For small-screen UI work, keep generated review artifacts under `./tmp_ui/`.
Do not run `rm -rf` to clean them; leave cleanup to the user after the goal is
complete.  Acceptance images must be screenshots produced by the real gameplay
loop with real PAL data.  Sketches, synthetic screen mockups, and compiler-only
previews are not evidence of gameplay integration.

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

Small-screen dialogue uses the 10px monospaced Traditional Chinese Fusion
Pixel Font release locked in `tools/pal_ui_layout/font.py`:

```text
fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip
SHA-256 2e695c27627bf09683df2afe69b086fa3cd3e52795bce39fded9cc509188b5fe
size 18,220,904 bytes
```

The archive is supplied out of band through `FONT10_ARCHIVE`; do not vendor
the release zip. The OFL-1.1 license is kept at
`third_party/fusion-pixel-font/OFL.txt`. Host tooling subsets the actual
`WORD.DAT`/`M.MSG` corpus plus the target-authored strings explicitly listed
in `tools/pal_pack_build.py`; it must not add invented replacement-menu
labels. The finite-profile chapter-complete endpoint is one such real runtime
string. Missing glyphs or a wrong archive must fail the build.

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

## Faithful native small-screen presentation

The small-screen path is deliberately not a replacement UI.  PAL still draws
its canonical 320x200 indexed gameplay frame with the original palette,
DATA.MKF borders/cursors/icons, menu hierarchy, input order, and return values.
The physical display exposes a generated 1:1 viewport of that frame; there is
no whole-frame scaler and no alternative flat/modern chrome.

The source of truth is intentionally small:

- geometry/header generator: `tools/pal_native_ui_layout.py`; it defaults to
  the two certified profiles but accepts repeated `--profile WIDTHxHEIGHT`
  values from 160x128 through the canonical 320x200 bounds;
- generated 240x135 and 160x128 profiles:
  `esp32s3/main/generated/pal_native_ui_*.h`;
- only 240x135 is the Cardputer ADV panel contract; 160x128 and any additional
  generated sizes are engine/host contracts until a matching board presenter
  is provided;
- fixed native viewport, FONT10 drawing, and bounded portrait RLE fitting:
  `embedded/pal_native_ui.[ch]`;
- read-only FONT10 pack view: `embedded/pal_font10_cache.[ch]`;
- Cardputer indexed 1:1 strip copy:
  `esp32s3/main/cardputer_extreme_native_view.[ch]`.

Generated values cover viewport origins, dialogue rectangles, page/line
limits, loading-screen coordinates, original DATA #9 frame IDs/colors, and
the exact FONT10 identity.  They do not describe replacement menu screens or
invent content.

Presentation rules:

- map view centres the canonical party anchor;
- battle view follows the moving player during action animation and the
  current player during selection; the original four-icon diamond and the one
  context-player info sprite are moved as intact groups into the live viewport
  (multiple full info frames cannot coexist with the action diamond at 160
  pixels). The context is normally the active player, but single-ally target
  selection shows the selected ally's HP/MP. Target selection starts at the
  actor/subject midpoint, then shifts only enough to retain the active
  player's actual unscaled sprite; list selection may pan to its cursor;
- original menus remain on the 320x200 canvas and the native viewport pans to
  keep the active selection visible, including item, magic-target, and
  equipment player selectors; the status screen uses a stable upper-left crop
  containing role identity and primary values;
- dialogue uses the generated viewport, reflows actual message text at word
  boundaries when available and at glyph boundaries otherwise with the 10px
  font, and keeps the original control-code timing,
  color, pagination, and interaction behavior; in particular, `~nn` ends the
  current message exactly as `TEXT_DisplayText()` does, so suffix controls are
  not reinterpreted as another wrapped line. Speaker-title placement also
  retains PAL's face-dependent rule: portrait-free upper/lower dialogue uses
  the original x=12 inset instead of reserving a nonexistent portrait column.
  A center-window line that cannot fit uses the original style-1 DATA #9
  border as a multi-line popup rather than clipping or introducing new chrome;
- only bounded assets that genuinely do not fit, currently dialogue portraits,
  may be downsampled on-device with deterministic nearest-centre sampling,
  preserved aspect ratio, and preserved RLE transparency.

Do not reintroduce a whole-frame axis map, semantic screen solver, mock menu
model, or independently redesigned battle HUD.  Gameplay state remains
canonical 320x200 and must never receive presentation coordinates.

### Required gameplay review

Tests are necessary but not visual acceptance.  Capture dialogue, map, battle,
and menu frames from the deterministic full gameplay executable for both
profiles, write them under `./tmp_ui/`, and inspect every final PNG with the
image viewer.  A crop of an old screenshot can help diagnose geometry but is
not a final acceptance capture.  Never substitute a Python-drawn preview,
wireframe, or synthetic fixture for a real gameplay frame.

The host-only forced-battle probe remains automatic by default.  Set
`PAL_DETERMINISTIC_FORCE_BATTLE_AUTO=0` when a review capture must hold on the
real interactive action selector; this does not change target gameplay.

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

### Native UI/font

```sh
PYTHONPATH=tools python3 -B tools/test_pal_native_ui_layout.py
make -C embedded native-ui-check

make -C esp32s3 \
  FONT10_ARCHIVE="$FONT10_ARCHIVE" \
  cardputer-extreme-ui-layout-check

make -C esp32s3 cardputer-extreme-native-view-check
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
