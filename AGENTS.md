# AGENTS.md

## Communication

Do not send optional commentary.

```python
disable_send_optional_commentary = True
```

## Test and development efficiency

Prefer repeatable control surfaces over manual frame-number searches, debugger
state injection, or long hand-authored input routes. The normal Unix/Linux SDL
build has an opt-in, loopback-only WebSocket harness in
`unix/pal_ws_server.[ch]`; start it with `PAL_WS_PORT=12345` and drive it with
`python3 -B tools/ws_cli.py`. Keep improving this harness when a missing,
generally useful command blocks efficient real-gameplay testing. Commands must
execute on the game thread, retain ordinary gameplay semantics, and remain
disabled by default. The Cardputer native Linux host opts in at build time with
`CARDPUTER_EXTREME_NATIVE_WS=1`, while the ESP-IDF firmware never compiles the
server. Do not add target-side networking or pull this host-only harness into
an ESP32 embedded build.

Use the harness for screenshots, input, state inspection, and scene positioning
instead of repeating brittle GDB/frame-guess workflows. Host control shortcuts
are renderer/integration evidence, not proof that a natural story route reaches
the injected state; document that distinction in acceptance artifacts.
Run the narrow harness gate with `make -C unix USE_SDL3=0 ws-check`.

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
- Runtime owns exactly two named native 240x135x8-bit indexed screens and one
  4KB RGB565 DMA strip. The legacy 320x200 space remains a gameplay/resource
  coordinate contract, not a target framebuffer. Their declarations are in
  `esp32s3/main/cardputer_extreme_memory.h`.
- The default Cardputer ADV profile combines fixed-storage RIX/OPL2 music with
  TF-managed core/chapter caches. MIDI, VOC, and SFX remain out.
- A generated catalog is broad resource coverage, not an unconditional
  full-game/story-route proof.

The architecture overview, build/flash commands, and acceptance caveats are
in `esp32s3/README.md`. Treat checker output and the ELF/map from the current
build as authoritative for measured bytes; prose measurements can age. The
enforced default-profile limits live in
`esp32s3/check_cardputer_extreme_chapter_cache.py`.

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
- Existing PAL resources, scripts, saves, collision tests, and battle
  animations retain their legacy coordinate meanings. A 320x200 compatibility
  coordinate is not the physical display architecture; presentation transforms
  must never be written back into gameplay state.
- Verify placement from ELF/map artifacts with `size`, `objdump`, `nm`, and
  linker maps. Source inspection alone is not a memory proof.
- Preserve existing dirty worktree changes unless they are clearly part of
  the requested task. Stage explicit paths rather than `git add -A`.

## Local data and pinned font

Use the audited stock DOS PAL data set unless the user specifies another:

```text
/mnt/hgfs/deb13/PALSteam/PAL_DOS
```

Do not mix resources, scripts, text tables, or saves from the former
`/mnt/hgfs/deb13/PAL` data set with this one. In particular, the stock
`PAL_DOS` `SSS.MKF` restores event-object 113's Lin carpenter trigger script;
copying only an event pointer or only `SSS.MKF` across data sets is invalid.

Small-screen dialogue uses the 10px monospaced Traditional Chinese Fusion
Pixel Font release locked in `tools/pal_ui_layout/font.py`:

```text
fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip
SHA-256 2e695c27627bf09683df2afe69b086fa3cd3e52795bce39fded9cc509188b5fe
size 18,220,904 bytes
```

The pinned release archive is vendored at
`third_party/fusion-pixel-font/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip`;
`FONT10_ARCHIVE` may override that default. The OFL-1.1 license is kept at
`third_party/fusion-pixel-font/OFL.txt`. Host tooling must still verify the
locked size and SHA-256 before it subsets the actual
`WORD.DAT`/`M.MSG` corpus plus the target-authored strings explicitly listed
in `tools/pal_pack_build.py`; it must not add invented replacement-menu
labels. The finite-profile chapter-complete endpoint is one such real runtime
string. Missing glyphs or a wrong archive must fail the build.

## Resource and state architecture

### Legacy Cardputer extreme packs

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

### Default Cardputer ADV music/cache packs

`tools/pal_chapter_pack_build.py` produces:

- TF source `pal_core.pak`, copied to the core SPI-NOR cache on demand;
- active `pal_tf.pak`;
- complete `pal_full.pak`;
- 15 conservative bundles `b00.pak` through `b14.pak`;
- `PALSET.BIN`, containing the external pack-set ID, core SHA-256, and chapter
  catalog;
- `chapter_manifest.json`;
- `EVENT.DEF`.

At boot, the target validates the core cache against `PALSET.BIN`; a mismatch
enters a native 240x135 `LOADING` screen and verifies/copies/verifies
`pal_core.pak` from TF before mapping it. At game load and bundle transitions,
the same pattern applies to `bNN.pak`; the commit record is written last. The
global current-bundle state skips repeated hashes while scenes remain in the
same verified bundle. No data-set hash is compiled into the default app. The
implementation is in:

- `esp32s3/engine_bridge/pal_engine_chapter_cache.[ch]`
- `esp32s3/check_cardputer_extreme_chapter_cache.py`
- `esp32s3/partitions_cardputer_extreme_cache.csv`

The catalog currently covers generated scene resources broadly, but this is
still labelled a candidate. Do not claim a complete story route without
deterministic route evidence. The legacy sparse layout remains separately
documented by the closure fields in
`tools/pal_pack_layout_cardputer_extreme.json`.

Default workflow commands are:

```sh
make -C esp32s3 PAL_DATA_DIR=/path/to/PAL cardputer-adv-music-tf
make -C esp32s3 PORT=/dev/ttyACM0 cardputer-adv-music-provision
make -C esp32s3 PORT=/dev/ttyACM0 cardputer-adv-music-flash-app
make -C esp32s3 cardputer-adv-music-check
```

The first command writes the complete ready-to-copy card contents under
`esp32s3/TF_datapak/`; it does not require a mounted card. Provisioning writes
the bootloader/partition table/app once; ordinary updates are app-only. Neither
flashing command host-flashes a data pack.

### TF-backed event/scene state

Event state is not fully resident in SRAM and is not a block-device swap:

- the stock data supplies 5,332 32-byte event records; the established
  42-page journal keeps 37 zero, unreachable compatibility records at its
  tail so existing on-card journal geometry does not change;
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

## Responsive small-screen presentation

The small-screen path is deliberately not a replacement UI. The engine draws
directly into the native 240x135 indexed framebuffer. Legacy 320x200 positions
remain compatibility coordinates for resources, scripts, events, collision,
and battle logic; convert them only at the draw boundary and never write
physical coordinates back into gameplay or save state. The LCD presenter is a
1:1 indexed-to-RGB565 strip conversion and must not resize a completed frame.

Keep three coordinate spaces explicit; never use one as an implicit substitute
for another:

- legacy gameplay/resource coordinates, normally 320x200;
- the pixel coordinates inside an individual source material;
- final physical coordinates in the native framebuffer.

Every Cardputer draw path must make its conversion at the draw boundary. A map
viewport translation, a full-screen material mapping, and a native UI position
are different operations. In particular, never put the map viewport offset in
a generic FBP/RLE blitter and never infer overlay coordinates from the current
framebuffer dimensions.

Map frames use a viewport, not scaling. Conceptually compose the original map,
cover tiles, sprites, effects, and animations and take the 1:1 source viewport
`(40,45,240,135)`, which maps PAL's canonical party anchor `(160,112)` to
`(120,67)`. The implementation may clip those draw operations directly into
the native framebuffer; it must not allocate or produce an intermediate
320x200 frame. Legacy `gpGlobals->viewport` remains the only world camera and
continues to handle movement and scripted camera motion. The physical viewport
offset is draw-only and must not be added to gameplay state. Do not scale map
tiles, map sprites, or a completed map frame.

A 320x200 to 240x135 nearest-centre mapping is permitted only while blitting a
specific full-screen art resource, such as a battle background or a fixed
full-screen item/status/equipment FBP. It scales that source material directly
into the native framebuffer; it never scales the framebuffer or a completed
composition. Use it only when cropping the resource would discard required
content. A screen backed by semantic full-screen artwork must be migrated as
one composition: background, portraits, equipment/item images, labels,
numbers, cursors, and hit positions must all use the same legacy-to-physical
base mapping. Graphical overlays are mapped as individual materials directly
into the native framebuffer; do not scale only the background while leaving
its dependent overlays at legacy coordinates. Other elements continue through
their original draw functions with only necessary native positions or bounded
per-asset fitting.

Text and number pixels are never mapped, although their anchor positions may
be converted into physical coordinates. Small-screen text uses FONT10 and must
be rasterized directly into the native indexed framebuffer at its final
physical coordinates as ordinary 10x10 cells with a 10-pixel advance. Native
glyph/number extents do not shrink with their anchors: after converting
positions, check spacing and alignment against those final extents and make
the smallest screen-local adjustment needed to prevent overlap. Text must
never be drawn into a legacy-sized surface and then downsampled, filtered,
sample-dropped, or otherwise shrunk. Existing pre-authored message lines retain
their original line/page/control-code boundaries and must not be reflowed.

The current source of truth is intentionally small:

- 1:1 indexed strip conversion:
  `esp32s3/main/cardputer_extreme_native_view.[ch]`;
- geometry/header generator: `tools/pal_native_ui_layout.py`;
- generated 240x135 and 160x128 profiles:
  `esp32s3/main/generated/pal_native_ui_*.h`;
- FONT10 drawing, bounded portrait fitting, and dialogue-layout compatibility:
  `embedded/pal_native_ui.[ch]`;
- read-only FONT10 pack view: `embedded/pal_font10_cache.[ch]`.

Cardputer extreme NOR/core packs contain only `FONT` chunk 1, the corpus-
subsetted FONT10 payload. The converted original 16px font (`FONT` chunk 0)
is excluded from those flash-resident packs and is retained only in the
offline-complete TF mirror.

Only 240x135 is the current Cardputer ADV acceptance target. The 160x128
profile remains a later engine/host check until the 240x135 vertical slice is
accepted and a matching board presenter exists.

Map and battle keep the original scene layers, sprites, animations, target
markers, action diamond, player information boxes, input order, and return
values. Do not add focus-following presentation crops, special small-screen
battle HUDs, semantic layout solvers, replacement menus, independently
redesigned chrome, or a whole-frame scaler. UI follow-up work must continue
from the original draw functions and DATA.MKF assets, making only necessary
position or per-material size changes for the physical canvas. Existing
viewport compatibility code must not become a second world camera or control
gameplay state.

Per-material downsampling must be bounded and deterministic, use
nearest-centre sampling, and preserve RLE transparency. Dialogue portraits use
aspect-preserving fitting. Materials that belong to a mapped full-screen
composition, such as status portraits and equipment images, use that
composition's axis mapping so they remain aligned with its artwork.

### Required gameplay review

Tests are necessary but not visual acceptance. For the current slice, capture
map and battle frames at 240x135 from the full gameplay executable, write them
under `./tmp_ui/`, and inspect every final PNG with the image viewer. Dialogue,
menu, and 160x128 captures become required when those slices are implemented.
A crop of an old screenshot can help diagnose geometry but is not a final
acceptance capture. Never substitute a Python-drawn preview, wireframe, or
synthetic fixture for a real gameplay frame.

Any screen changed during a slice becomes a required acceptance screen even if
it appears later in the SOP. Before handing control to the reviewer: build the
exact binary, position the real game at every changed screen, capture it, and
personally inspect the final PNG. Do not claim the SDL session is ready while a
known reachable screen still mixes old and new coordinate transforms. Only
after this gate should the window be placed on the human's desktop and all
automated input stop.

Use `embedded/UI_REVIEW_SOP.md` as the persistent major-screen review order.
Keep session-specific screenshots and notes under `./tmp_ui/`; do not replace
the real-gameplay review with synthetic fixtures or direct renderer calls.

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
make -C esp32s3 cardputer-adv-music-check
```

Legacy profile gates remain available when specifically working on them:

```sh
make -C esp32s3 FONT10_ARCHIVE="$FONT10_ARCHIVE" \
  cardputer-extreme-check
make -C esp32s3 \
  FONT10_ARCHIVE="$FONT10_ARCHIVE" \
  cardputer-extreme-music-check
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
`esp32s3/README.md`; keep every installed TF file from one generated manifest,
and require an explicit user request before flashing or replacing card
contents. Default hardware port is normally `/dev/ttyACM0`, but discover it
rather than assuming.

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



# Update .gitignore, not rm every time

Do not clean __pycache__, it has been git-ignored.
