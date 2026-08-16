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

The native SDL host accepts `--ui-size 240x135` and `--ui-size 160x128`.
`tools/ws_cli.py sop-capture ./tmp_ui/<session>` is the standard automated
preparation capture; it uses real gameplay UIs and leaves final acceptance to
the human reviewer.

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

This repository has three distinct ESP32-family ports. Do not mix their
hardware, memory, storage, or bus assumptions.

### Primary: Cardputer ADV extreme

- Board: M5Stack Cardputer ADV K132-Adv, not the original Cardputer.
- Development line: the `extreme` branch. Always check the current branch and
  dirty worktree before editing; do not overwrite unrelated user changes.
- Flash: 8MB.
- PSRAM: none.
- Resource ownership uses `MEM_LEVEL1`; do not emulate PSRAM with TF or add a
  target allocator.
- LCD: ST7789, physical/native resolution 240x135.
- TF: independent SPI2 bus at 20MHz; LCD uses SPI3. There is no CoreS3-style
  LCD-D/C versus TF-MISO pin handoff on this board.
- Runtime owns exactly two named native 240x135x8-bit indexed screens and one
  4KB RGB565 DMA strip. The legacy 320x200 space remains a gameplay/resource
  coordinate contract, not a target framebuffer. Their declarations are in
  `esp32s3/main/cardputer_extreme_memory.h`.
- The default Cardputer ADV profile combines fixed-storage RIX/OPL2 music,
  one host-converted SFX voice, and TF-managed core/chapter caches. Music is
  16.384kHz PCM16; SFX is 8.192kHz signed PCM8 duplicated twice into that
  stream. Raw MIDI/VOC decoding remains out.
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
- Resource ownership uses `MEM_LEVEL2`; board wiring and the existing NOR/TF
  split remain separate choices.

### Secondary: Xueersi Xiaomiao

- Board: Xueersi Xiaomiao with classic ESP32-WROVER-B, not ESP32-S3.
- The profile deliberately uses a conservative 4MB-compatible flash geometry
  (WROVER-B itself has 4/8/16MB ordering variants); the partition table
  contains no PAL resource or chapter-cache partition.
- Physical PSRAM is 8MB, but this profile keeps all named resident buffers
  inside the classic ESP32's 4MB directly mapped window and does not use
  himem bank switching or a general allocator. Resource ownership uses
  `MEM_LEVEL2`.
- LCD: ST7735, physical/native landscape resolution 160x128.
- LCD and SD share VSPI. GPIO19 is used for the one-time LCD reset before the
  bus is initialized, then becomes SD MISO; do not apply the CoreS3 SE GPIO35
  D/C/MISO handoff or the Cardputer ADV independent-bus assumption.
- All decoded/native game data comes from the shared `0:/pal_full.pak`.
  At boot the target derives its selected resident view directly from that
  file into its fixed mapped-PSRAM owner; no second TF payload pack exists.
  The resident view keeps all RIX tracks stable for the audio task, while
  other gameplay chunks remain streamed from the complete pack.
- TF is the only PAL-data source. The app contains no data-set digest and the
  SD-only provider must not hash or scan the `pal_full.pak` payload at boot;
  it accepts any structurally compatible complete pack.
- Standard save writes use a fixed 4KB internal-DMA staging workaround only
  under classic `CONFIG_IDF_TARGET_ESP32` plus `MEM_LEVEL2`; ESP32-S3 and
  Level1 builds retain the direct FatFS write path.
- Source of truth: `esp32s3/main/xiaomiao_*`,
  `tools/pal_pack_layout_xiaomiao.json`, and `esp32s3/check_xiaomiao.py`.
- Build/data gate: `make -C esp32s3 xiaomiao-check`; shared card generation:
  `make -C esp32s3 tf-datapack`. The gate drives the SD-only provider through
  real 160x128 map and forced-battle gameplay captures under `tmp_ui/`; the
  forced battle proves integration, not natural story-route reachability.
- RIX/OPL2 music renders at a logical 16.384kHz as mono PCM16. One 40,960-byte internal-SRAM
  owner holds up to five seconds of host-converted 8.192kHz signed PCM8 SFX;
  every source sample is duplicated twice and saturating-mixed with music. An
  11-bit LEDC PWM channel drives the built-in passive buzzer at GPIO14 while
  GPTimer updates its duty once per sample from a fixed four-block ring. Their
  physical periods share one quantized APB-clock rate so they do not drift. Raw
  MIDI/VOC decoding remains out. Buzzer sound quality and timing require
  physical acceptance.

The classic Nintendo DS port is the sole non-PSRAM `MEM_LEVEL2` exception; `nds/README.md` owns its self-contained NitroFS ROM, native 256x192 libnds path, and DeSmuME SDL/WebSocket acceptance harness.

Useful hardware references:

- `/home/john/work/CardPuterADV/esp-walkie-talkie`
- `/home/john/work/CoreS3SE`
- `/home/john/work/xueersi-xiaomiao`
- `/home/john/esp-idf/examples/storage/sd_card/sdspi`

## Non-negotiable embedded contract

- The target-side PAL/game/resource code must not call `malloc`, `calloc`,
  `realloc`, `free`, C++ new/delete, or use wrappers and containers that hide
  equivalent game-owned allocation. Game memory must instead have named fixed
  owners or audited lifecycle arenas so its capacity and lifetime are
  verifiable from the source plus ELF/map and fragmentation is impossible.
  This is a game-memory ownership rule, not a ban on platform implementations:
  allocations performed internally by the toolchain runtime, SDK, filesystem,
  or other third-party platform libraries do not count, must not be rejected by
  allocator call-graph inspection, and are not a reason to reimplement those
  libraries. Project code must still not use such libraries as an indirect
  general allocator for game or resource objects.
- No runtime YJ1/YJ2/LZ4 or other asset decompression. Decode and convert on
  the host when building packs.
- Follow `embedded/RESPONSIVE_RENDERING.md`. The only common full-canvas
  transform currently allowed is the audited FBP/RNG path in
  `embedded/FULLSCREEN_ASSET_RENDERING.md`; do not route maps or UI through it.
- Every no-heap engine target defines exactly one resource-memory profile.
  `MEM_LEVEL1` keeps persistent resource views mapped from NOR/core/overlay and
  is the no-PSRAM Cardputer profile. `MEM_LEVEL2` is for targets with at least
  4MB of directly addressable PSRAM; it reads mutable-lifetime resources into
  the fixed scene (256KB), player (128KB), and battle (512KB) linear arenas in
  `embedded/pal_memory_profile.[ch]`. Those arenas only bump forward and reset
  as a whole at their matching engine lifecycle boundary. The two 64KB fight
  buffers are fixed owners because effect and summon data can coexist.
- Do not add individual arena frees, freelists, compaction, fallback
  allocation, a general memory pool, or a generic cache framework. Arena reset
  changes only the used offset; it does not clear PSRAM. Overflow is a
  fail-closed profile/configuration error, and the real-data checker must prove
  the shipped pack fits before the size is changed.
- Memory profile, board wiring, display geometry, and storage topology are
  orthogonal. In particular, `PAL_STORAGE_SD_ONLY` selects the Xiaomiao core
  copy/streaming provider; it is not a board name and does not define a memory
  profile. `PAL_CARDPUTER_EXTREME` is retired: use `MEM_LEVEL1`/`MEM_LEVEL2`
  for resource ownership and the existing target, storage, or presentation
  capability for those independent choices. Use normal named buffers and typed
  `const` views outside the three Level2 arenas.
- Keep FatFS LFN heap support and dynamic FatFS buffers disabled. Runtime
  filenames must stay short (`0:/pal_full.pak`, `0:/EVENT.WRK`, `0:/b00.pak`,
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

Resource cardinalities belong to the selected PAL data set. Runtime code must
derive counts from validated native chunk lengths or generated metadata; it
must not require audited sample counts such as 5,332 event objects. Fixed
arrays, pagers, and arenas define explicit capacities only: validate nonzero
size, record alignment, cross-table bounds, and capacity before use, then keep
the derived count for gameplay and save I/O. Tests may assert exact counts only
when they explicitly identify a pinned fixture data set.

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

### Shared TF data set

`esp32s3/TF_datapak/` is the single generated TF directory for every port.
`pal_full.pak` is the complete host-decoded, runtime-native resource source;
it is portable across target profiles and must not omit data merely because a
particular board does not use it. Cardputer-specific files beside it are NOR
cache sources with the same pack-set ID: `pal_core.pak` and `bNN.pak` are
copied into replaceable NOR contents and may overlap the complete pack. They
never define data completeness. Xiaomiao derives its bounded resident PSRAM
view from `pal_full.pak` at boot. This is neither block-device swap nor
permission to add a generic cache framework. The pack-set ID is derived only
from the complete native data; changing cache placement must not change
`pal_full.pak` or invalidate otherwise compatible state.

### Default Cardputer ADV music/cache packs

`tools/pal_chapter_pack_build.py` produces:

- TF source `pal_core.pak`, copied to the core SPI-NOR cache on demand;
- the sole complete TF payload `pal_full.pak`;
- 15 fixed route-aware, non-contiguous bundles `b00.pak` through `b14.pak`;
- `PALSET.BIN`, containing the external pack-set ID, core SHA-256, and chapter
  catalog;
- `chapter_manifest.json`.

The Cardputer maps the bounded `pal_full.pak` TOC from `pal_core.pak` CACHE#1
in SPI NOR; only the 32-byte TF header is read and compared at boot, and no
full TOC copy lives in SRAM. Validated NOR core/overlay chunks take precedence
over duplicate TF chunks. Xiaomiao indexes that same complete file from PSRAM
and derives its fixed resident view from it. `pal_full.pak` is not swap.

At boot, the target validates the core cache against `PALSET.BIN`; a mismatch
enters a native 240x135 `LOADING` screen and verifies/copies/verifies
`pal_core.pak` from TF before mapping it. At game load and bundle transitions,
the same pattern applies to `bNN.pak`; the commit record is written last. The
global current-bundle state skips repeated hashes while scenes remain in the
same verified bundle. The chapter-cache SHA layer is the only target-side
NOR-cache payload integrity owner: the provider must not repeat whole-pack CRC
scans of core, overlays, or `pal_full.pak`. It still validates pack structure,
bounds, ownership, and pack-set identity; host generation/provisioning retains
the pack CRC checks. No data-set hash is compiled into the default app. The
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

### Event state and saves

The only durable mutable state is a standard `N.rpg` save. `MEM_LEVEL1` uses
three resident pages over session-only `0:/EVENT.WRK`; New Game or Load Game
overwrites its logical image, normal dirty LRU eviction is its only write-back
path, and startup/shutdown never recover or flush it. `MEM_LEVEL2` keeps the
complete event table in fixed PSRAM and never accesses the work file. Scene
records remain resident in both profiles. See `embedded/EVENT_STATE.md`.

## Responsive small-screen presentation

The normative rendering rules are split by responsibility:

- `embedded/RESPONSIVE_RENDERING.md`: coordinate spaces, map viewport, native
  FONT10, and case-by-case UI/material layout;
- `embedded/FULLSCREEN_ASSET_RENDERING.md`: the FBP/RNG-only full-canvas
  allowlist, transform, streaming boundary, and regression checks;
- `embedded/UI_REVIEW_SOP.md`: human visual-acceptance order;
- `unix/WEBSOCKET_HARNESS.md`: repeatable host control and capture.

Hard boundaries: draw into the native indexed framebuffer, never resize a
completed frame, never write presentation coordinates into gameplay state,
and never infer that a transform for one asset also applies to its overlays.
Maps retain their responsive 1:1 viewport. Text and numbers remain native
10px pixels with pre-authored line/page boundaries. All non-allowlisted assets
and UI elements are handled case by case.

Only 240x135 is accepted on Cardputer ADV. Xiaomiao's 160x128 profile has host
map/battle coverage but still requires physical-board acceptance.

### Required gameplay review

Tests are not visual acceptance. Capture every changed screen from the real
gameplay loop under `./tmp_ui/`, inspect it, then follow
`embedded/UI_REVIEW_SOP.md` and pause for the human reviewer. A forced state
proves rendering/integration, not natural story-route reachability.

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
make -C embedded native-ui-check
make -C esp32s3 cardputer-extreme-native-view-check
```

Responsive coordinates and widget extents are calculated directly in C from
the current framebuffer size. Do not add Python layout solvers, coordinate
generators, generated layout headers, or per-resolution constant tables.

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
make -C embedded fullscreen-stretch-check
make -C esp32s3 cardputer-extreme-rng-decoder-check
make -C esp32s3 FONT10_ARCHIVE="$FONT10_ARCHIVE" \
  cardputer-extreme-native-smoke
make -C esp32s3 FONT10_ARCHIVE="$FONT10_ARCHIVE" \
  cardputer-extreme-save-check
```

TF preparation and flashing are external mutations. Use the exact targets in
`esp32s3/README.md`; keep every installed TF file from one generated manifest,
and require an explicit user request before flashing or replacing card
contents. Default hardware port is normally `/dev/ttyACM0`, but discover it
rather than assuming. Flashing may operate only on an existing `/dev/tty*`
device. If no matching TTY device is present, stop and tell the user; never
reset, rebind, detach, or otherwise manipulate a USB bus or USB device in an
attempt to make a flashing port appear.

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

Use `embedded/README.md` as the current documentation index.

Keep this file compact and navigational. Detailed measured results belong in
the checker output or focused README, not in an ever-growing historical list
of every embedded slice. When behavior changes:

1. update the implementation and repeatable checker;
2. update the focused README that owns the feature;
3. update this file only if the target, invariant, canonical path, integration
   boundary, or primary command changed;
4. remove superseded claims instead of appending contradictory history.

Do not clean ignored `__pycache__` directories merely to make status output
look tidy; update ignore rules when generated files are repeatedly noisy.
