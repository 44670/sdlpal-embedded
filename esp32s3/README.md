# M5Stack CoreS3 SE Bring-Up

This ESP-IDF project targets the CoreS3 SE full-engine host by default. The older scene scaffold remains available as an explicit bring-up/check target, with no audio; the target does not link audio/SFX slices or enable the speaker amplifier.

The board init copies/adapts the local walkie-talkie CoreS3 SE reference at `/home/john/work/CardPuterADV/esp-walkie-talkie`: AW9523, AXP2101, FT6336 touch, and SPI LCD init use the same pins and command sequence.

## Cardputer ADV 8MB/no-PSRAM extreme profile

`CARDPUTER_EXTREME_NO_PSRAM=ON` selects a separate M5Stack Cardputer ADV
(K132-Adv) target.  It is not the original Cardputer and it does not replace
the default CoreS3 SE build.  The profile has:

- 8MB flash, a 1MB app partition, and a `0x6f0000` read-only `pal_nor`
  partition;
- no PSRAM and no engine/resource heap allocation;
- exactly two named 320x200x8-bit logical screens plus one 4KB LCD DMA strip;
- no audio in the default build; the explicit music build adds RIX/OPL2 only,
  while desktop codecs and SFX remain excluded;
- no runtime decompressor or splash sequence;
- the Cardputer ADV vendor timing profile (240MHz CPU and a 1ms FreeRTOS tick);
- ST7789 240x135 indexed presentation through SPI3 and TCA8418 keyboard input;
- TF on independent SPI2 at 20MHz, with only a 2KB pack TOC resident in SRAM.
- USB Serial/JTAG as the console, leaving the ES8311 word-select GPIO43 free
  from UART0 output.

Build the firmware and the normal sparse resource packs:

```sh
make -C esp32s3 cardputer-extreme-build
make -C esp32s3 \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  cardputer-extreme-pack-build
```

The normal and music pack targets now require the pinned Fusion Pixel Font
archive.  Firmware startup opens NOR `FONT` chunk 1 and compares its glyph
count, byte length, and payload CRC with the Python-generated 240x135 header;
a legacy pack without the matching FONT10 chunk fails closed.  Full
`cardputer-extreme-check` / `cardputer-extreme-music-check` runs therefore
also require `FONT10_ARCHIVE`.

`tools/pal_native_ui_layout.py` generates the certified 240x135 and 160x128
native-view contracts.  Gameplay retains its canonical 320x200 indexed frame,
original palette and original DATA.MKF UI assets, while the LCD copies a 1:1
player-focused viewport with no whole-frame scaling.  Map view follows the
party; battle view keeps the active player visible and relocates the intact
original action/HUD sprite groups into that crop; target and list selection
may pan to the selected subject.  Unchanged menus pan the viewport to the
current selection, including custom player selectors, while status opens on
its identity/primary-value region.  Dialogue uses the corpus-subsetted 10px
FONT10 glyphs with generated wrapping/page geometry; only an overlarge
portrait is fitted with deterministic aspect-preserving RLE sampling.  The
relevant runtime files are `embedded/pal_native_ui.[ch]` and
`main/cardputer_extreme_native_view.[ch]`.

Visual acceptance must use real deterministic gameplay captures for dialogue,
map, battle, and menus at both resolutions.  Store review artifacts under
`./tmp_ui/`; Python-drawn mockups are not gameplay evidence.

### TF-backed chapter cache experiment

`CARDPUTER_EXTREME_CHAPTER_CACHE=ON` selects a separate 8MB flash layout:
the 768KB app is followed by a `0x460000` immutable `pal_core` partition and
one `0x2d0000` replaceable `pal_cache` partition.  The host tool builds 15
conservative scene bundles (`b00.pak` through `b14.pak`) plus the active
`pal_tf.pak` and complete decoded/native `pal_full.pak` TF mirror:

```sh
make -C esp32s3 \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  cardputer-extreme-chapter-cache-check
make -C esp32s3 \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  TF_MOUNT=/media/$USER/PALTF \
  cardputer-extreme-chapter-prepare-tf
make -C esp32s3 \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  PORT=/dev/ttyACM0 \
  cardputer-extreme-chapter-cache-flash-core
make -C esp32s3 \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  PORT=/dev/ttyACM0 \
  cardputer-extreme-chapter-cache-flash
```

The chapter build requires the same pinned `FONT10_ARCHIVE` as the normal
extreme profile.  Its host builder puts the identical corpus-subsetted
`FONT` chunk 1 in `pal_core.pak` (and in the complete TF mirror), while target
startup validates that chunk against the generated 240x135 header before it
opens the chapter catalog.  Missing or stale FONT10 data therefore fails
closed in the chapter-cache profile too.

After a save is loaded, and whenever a scene crosses a bundle boundary, the
engine compares the exact cached SPI-NOR payload SHA-256 with the descriptor
in the immutable core catalog.  A matching bundle is mapped directly.  A
mismatch shows a native 240x135 `LOADING` progress screen, streams the matching
short-name TF file through a caller-owned static FatFS `FIL` into SPI NOR,
verifies both the TF-side and flash-readback SHA-256, validates the pack CRC/set
ID/ownership through the normal provider, and writes the commit record last.
Erasing the commit sector before the payload makes an interrupted update
rebuild deterministically on the next attempt.

The generated resource closure covers scenes 1 through 299, but this is not
yet a full-game runtime claim.  The no-PSRAM profile pages all 5,369 mutable
event records and all 300 scene records through the fixed `EVENT.STA` journal
on TF, with three 4KB event pages resident in SRAM.  `EVENT.DEF` supplies the
matching immutable defaults generated from `pal_full.pak`; tagged saves stream
the complete event/scene state through the same fixed page storage.

The default firmware and pack targets remain the established no-audio
profile.  The explicit music-only profile builds in a fixed-storage OPL2/RIX
backend and adds the complete original-numbered `MUS.MKF` archive to NOR:
all 88 source slots are retained, slots 0 and 29 are empty in the source data,
and the other 86 tracks are playable.  MIDI, VOC, and SFX remain excluded:

```sh
make -C esp32s3 cardputer-extreme-music-build
make -C esp32s3 \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  cardputer-extreme-music-check
```

The music firmware uses its own `build-cardputer-extreme-music` directory and
sdkconfig, leaving `build-cardputer-extreme` as the no-audio build.  The full
gate also writes `/tmp/pal_cardputer_extreme_music_nor.pak`,
`/tmp/pal_cardputer_extreme_music_tf.pak`, the complete decoded mirror
`/tmp/pal_cardputer_extreme_music_full.pak`, and a separate music manifest.
It checks the exact track-ID closure, all 88 source chunk slots, both required
zero-sized slots, every RIX payload, the 8MB NOR partition budget, the 2KB
active-TF TOC budget, and the shared three-image pack-set ID.  Firmware startup
also maps and asks the bounded RIX decoder to validate all 86 non-empty tracks
before starting the real-time audio task, so a wrong or damaged NOR music
profile fails visibly instead of becoming a later silent track.  On the linked
firmware the gate additionally checks the exact 54-source inventory,
music/no-SFX compile defines and symbols, OPL table placement in flash,
OPL/audio state in SRAM, stack reports, fixed music state-machine semantics,
and separate music SRAM/flash budgets.  It also renders every track through the
same fixed OPL2 core and requires audible PCM from all 86 tracks.
The extreme backend deliberately freezes the sequencer, OPL state, and
sample-counted fades while music is disabled or its volume is zero.  This is
deterministic but differs from the desktop RIX player's one-time wall-clock
catch-up when a newly requested fade-out has not emitted any samples yet.

The gate runs the complete 5,369-event save/reopen/fault-recovery test against
the music pack-set ID rather than testing event persistence only with the
no-audio bundle.  Moving between the same-data no-audio and RIX-music profiles
atomically rebinds the committed `EVENT.STA` page set when the `EVENT.DEF`
payload CRC is identical.  An explicit New Game still resets every event and
scene to template defaults; loading a compatible tagged save restores its full
event state.  A different template CRC rebases immediately to the new
defaults.  This compatibility rule is intended for these checked same-dataset
profiles, not as a promise that arbitrary packs with unrelated scripts or
object tables are save-compatible.

Run `cardputer-extreme-music-check` for current application, section, stack,
NOR/TF, TOC, and reserve measurements. The limits are enforced by
`check_cardputer_extreme.py` and `check_cardputer_extreme_music_pack.py`;
copied measurements in prose become stale. The pinned FONT10 chunk is
mandatory and is already included in every supported music pack—there is no
supported “legacy-font” pack. The separate complete mirror contains every
host-decoded/preconverted resource, including MIDI/MUS tracks and
host-converted PCM SFX, while deliberately omitting raw `VOC.MKF` in favor of
the generated `SFX` archive.

Install the matching firmware and pack bundle with:

```sh
make -C esp32s3 TF_MOUNT=/media/$USER/PALTF \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  cardputer-extreme-music-prepare-tf
make -C esp32s3 PORT=/dev/ttyACM0 \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  cardputer-extreme-music-flash-all
```

The TF preparation target syncs and byte-compares all three installed files.
Both `cardputer-extreme-music-flash` and
`cardputer-extreme-music-flash-all` write bootloader, partition table,
application, and the matching music NOR image in one esptool session, so the
board is not deliberately rebooted between mismatched firmware/resource
images.

For a real-board acceptance run, leave the headphone jack unplugged so the
on-board speaker amplifier is enabled.  The USB Serial/JTAG log must report
the ES8311/I2S path and fixed RIX player ready.  After music starts, its
periodic telemetry must show `nonzero > 0`, `peak > 0`,
`deadline_miss=0`, `write_err=0`, `send_q_ovf=0`, and `source_fault=0`.
`tick_gap_max_us` and `tick_gap_excess_max_us` expose scheduling stalls that
render-time-only measurements cannot see; `send_q_ovf` is the driver signal
that a completed DMA-buffer notification was overwritten.  Periodic formatting
and USB logging run from the cooperative engine/input path rather than the
real-time audio task; inspect the cumulative values together with the reported
stack, internal-heap, and DMA-heap low-water marks.

The active music TF payload size is currently unchanged, but its pack-set ID
differs; it must not be mixed with the default no-audio NOR image.  The prepare
target installs both `pal_tf.pak` and `pal_full.pak`.

The stronger repeatable gate is:

```sh
make -C esp32s3 \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  cardputer-extreme-check
```

It builds the Xtensa firmware, validates the ELF/partition/sdkconfig/pack
budgets, checks source hashes and the declared candidate resource boundaries,
streams an FBP chunk and one RNG frame from TF, then runs 3000 deterministic
frames through the exact extreme engine profile.  The host run reaches the
normal walking loop and injects team 0, a reachable two-enemy battle; it
requires both battle entry and exit plus a nontrivial logical-screen PNG.  Its
two compared traces are independent repeatability runs of the same extreme
binary and packs, not parity against the unrestricted desktop engine.  The
gate also injects a reproducible scene-20 stress state through GDB into the
same binary and requires exactly 358 live draw entries and a 358-entry
high-water mark; this is the measured counterexample to the old 256/320
capacities.  The
same gate also exercises the tagged save format, sparse event 5334
persistence, corruption rejection, backup recovery, successful overwrite, and
atomic replacement failure paths.  This is host behavioral verification, not
a substitute for real-board heap/stack/LCD/TF testing or a story-route proof.

The current chapter policy is `tools/pal_pack_layout_cardputer_extreme.json`.
It is deliberately labelled a candidate rather than a completed story-route
closure.  It preserves chunk numbering with zero-sized holes and retains
scenes 1..20 plus 22, their 423 contiguous event objects, one 32-byte sparse
global-state overlay for event 5334, the selected MAP/GOP/MGO set, and a
conservative script-derived battle set.  The SZC2 tagged save persists that
sparse state.  Static traversal still finds an unresolved transition from
scene 22 to scene 21 in the original script.  The extreme interpreter replaces
that exact transition with a visible `CHAPTER COMPLETE - SUZHOU NEXT` endpoint,
and the deterministic gate executes entry 10600 and requires the engine to
remain in scene 22.  This supplies a finite pre-Suzhou boundary, but the build
is still not a route-proven “up to Suzhou” release: it lacks
closure roots for every item/magic/poison/death script, intended-route
coverage for all 20 statically selected battle teams, and a proof that the
512-entry scene draw list covers every selected scene state.  The original
256-entry extreme list and a 320-entry variant both have measured scene-20
counterexamples; 512 is the SRAM-budgeted engineering setting, not a formal
route bound.  The profile reduces the five formatting/path scratch strings
from 1024 to 256 bytes each; target runtime paths are deliberately short.

Const/random-access assets remain in NOR.  Five decoded FBP screens and
decoded RNG movie 1 live on TF; the runtime reads FBP sequentially into screen
B and reads only one RNG frame at a time.  NOR and TF may therefore contain
disjoint chunks of the same archive, and the provider resolves ownership per
chunk.  Both packs carry one deterministic pack-set ID and a whole-image
CRC32; startup rejects a mismatched card, stale NOR/TF pair, or corrupted
image before exposing resources to the engine.

The same TF card also receives `pal_full.pak`, an independent complete mirror
that may overlap every active NOR/TF chunk.  The current no-PSRAM firmware does
not open or index this file: its TOC exceeds the 2KB active index budget.
Keeping the verified sparse `pal_tf.pak` as the runtime image avoids changing
chunk precedence or the current read path.  The mirror is a future-expansion
source for generating a larger sparse active pack or for a later bounded
streaming-index implementation.  The build manifest records the mirror's
whole-pack SHA-256/CRC32 plus every archive/chunk format, size, and SHA-256.
The checker rebuilds all host conversions from the source files, compares
every chunk, rejects residual YJ1 payloads, and verifies all three pack-set
IDs.

Prepare and flash:

```sh
make -C esp32s3 TF_MOUNT=/media/$USER/PALTF \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  cardputer-extreme-prepare-tf
make -C esp32s3 PORT=/dev/ttyACM0 \
  FONT10_ARCHIVE=/path/to/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip \
  cardputer-extreme-flash-nor
make -C esp32s3 PORT=/dev/ttyACM0 cardputer-extreme-flash
```

`cardputer-extreme-prepare-tf` installs the generated active TF image as
`pal_tf.pak` and the complete decoded mirror as `pal_full.pak`.  Only
`pal_tf.pak` is runtime-active.  The firmware logs both general internal-RAM and DMA-capable
internal-RAM free/minimum/largest-block memory, plus the main-task stack
high-water mark, at app entry, after board/pack startup, after resource loads,
and around battle.  Real hardware validation must still capture those logs
and verify LCD offsets/color order, keyboard matrix behavior, TF stability,
save/reload, and an uninterrupted playthrough of the selected story range.

Build the full original-engine CoreS3 SE host:

```sh
source /home/john/esp-idf/export.sh
idf.py -C esp32s3 -B build-cores3se-engine-host -DPAL_CORES3SE_ENGINE_HOST=1 set-target esp32s3 build
```

Equivalent make target:

```sh
make -C esp32s3 build
```

Build the scene scaffold only when checking the intermediate LCD/touch/TF/NOR bring-up:

```sh
make -C esp32s3 scaffold-build
```

The scaffold uses `build-cores3se-scaffold` by default so its `PAL_CORES3SE_ENGINE_HOST=0` CMake cache does not poison the full-engine host build directory.

Repeatable target-side contract check:

```sh
make -C esp32s3 check
```

That builds the ESP-IDF artifact, rebuilds the default resource packs from `/mnt/hgfs/deb13/PAL`, verifies the NOR pack fits the `pal_nor` partition and stays under the 97% soft utilization gate, rejects compressed/YJ1 payloads in both NOR and TF packs, validates the generated pack manifest source hashes, decoded-size summaries, and pack-layout hash/archive placement, checks the protected-engine-file divergence manifest, scans the target-side sources and linked project objects for heap/decompress calls, checks that every target-linked source is covered by the source scan, checks the shared SPI/SDSPI invariants for the CoreS3 SE TF/LCD bus, checks the scaffold-freeze guard against target-side script/dialog/battle/save mutation, checks key ELF sections, verifies the named SRAM/PSRAM buffer registry, checks generated `-fstack-usage` files, rejects disabled target sources plus target audio/SFX symbols, and reports `pal_sram_` / `pal_psram_` symbol totals from the ESP32-S3 ELF.

Native host smoke for the CoreS3 SE target app:

```sh
make -C esp32s3 native-smoke
```

This compiles `esp32s3/main/app_main.c` against local ESP-IDF/FatFS/LCD shims, maps the generated NOR/TF packs from `/tmp`, runs the target scaffold, writes `/tmp/sdlpal-cores3se-native/cores3se_scene.png`, and verifies the PNG shape/content. It is a target-glue smoke test only; full original game-loop verification still belongs to the Unix deterministic contract profile, which can emit `/tmp/sdlpal-contract-deterministic/newgame.png` with:

```sh
make -C unix deterministic-newgame-png-check
```

The CoreS3 SE target directory also exposes that full-engine check with LCD framing:

```sh
make -C esp32s3 native-engine-smoke
```

That command builds the original engine in the native no-audio deterministic contract profile, replays the new-game route against generated packs/static buffers, emits a 320x240 CoreS3 SE-framed PNG at `/tmp/sdlpal-cores3se-native/cores3se_engine.png`, and verifies it with the same PNG gate. This is the native full-engine target bridge; it excludes desktop audio/MIDI/RIX/OPL/SFX linkage and does not add game logic to the CoreS3 SE scaffold.

The same no-audio full-engine profile can also be built without linking desktop SDL, using a fixed native SDL2 shim that models the target display/input seam:

```sh
make -C esp32s3 native-engine-shim-smoke
```

That emits `/tmp/sdlpal-cores3se-native/cores3se_engine_shim.png` and verifies the 320x240 PNG. The stronger shim parity gate is:

```sh
make -C esp32s3 native-engine-shim-parity-check
```

That compiles the no-audio contract against `esp32s3/native_engine_shim/`, replays the walking/gameplay route, and compares it directly against stock desktop SDLPAL.

A stricter native bridge also drops the Unix platform file and uses the CoreS3 SE `pal_config.h` plus the fixed SDL shim:

```sh
make -C esp32s3 native-engine-bridge-smoke
make -C esp32s3 native-engine-bridge-parity-check
```

The smoke emits `/tmp/sdlpal-cores3se-native/cores3se_engine_bridge.png`; the parity gate replays the same walking/gameplay route and compares the bridge directly against stock desktop SDLPAL.

The closest native compile shape to the ESP-IDF full-engine host is:

```sh
make -C esp32s3 native-engine-host-smoke
make -C esp32s3 native-engine-host-parity-check
```

The smoke runs `app_main()` against native ESP-IDF/FatFS/LCD shims, opens the generated NOR/TF packs through the same target pack path, presents through the CoreS3 SE LCD flush seam, and verifies `/tmp/sdlpal-cores3se-native/cores3se_engine_host.png`. The parity gate enables deterministic replay on that same host path, compares 3000 walking/gameplay frames plus script/event traces against stock desktop SDLPAL, and verifies the target-flushed PNG `/tmp/sdlpal-cores3se-native/cores3se_engine_host_deterministic.png`.

The current full-engine port gate combines the native bridge and host PNG/parity paths with the Xtensa link check and ESP-IDF engine-host check:

```sh
make -C esp32s3 engine-port-check
```

The original no-audio engine source set can also be compiled and link-checked with the ESP32-S3 toolchain against the CoreS3 SE config, fixed SDL shim, and bridge pack provider:

```sh
make -C esp32s3 engine-bridge-idf-compile
make -C esp32s3 engine-bridge-idf-link
```

The compile target renames the protected `main.c` entry to `PAL_EngineMain` for the later ESP-IDF `app_main` host. It also compiles `esp32s3/engine_bridge/pal_engine_target_packs.c`, which opens the `pal_nor` partition with `esp_partition_mmap()` and opens `0:/pal_tf.pak` through FatFS read-at. The link target builds a standalone Xtensa ELF from the hostless subset, verifies no unresolved symbols remain, and rejects accidental linkage of the protected `PAL_MKF*` pack functions so resource access goes through `esp32s3/engine_bridge/pal_engine_pack_provider.c`. The native bridge parity check exercises the same provider through POSIX host shims.

The ESP-IDF full-engine host build is:

```sh
make -C esp32s3 engine-host-build
make -C esp32s3 engine-host-check
```

That target links `app_main()` to `PAL_EngineMain`, initializes CoreS3 SE board/video, maps the generated NOR pack from the `pal_nor` partition, opens `0:/pal_tf.pak` through FatFS read-at, presents the shim SDL texture through the LCD flush path, maps FT6336 touch points to SDL keyboard events, routes SDL timing to `esp_timer_get_time()` / FreeRTOS delays, and routes engine `0:/` save/config stdio calls through fixed FatFS file slots without touching original input/game/save logic. Large bridge/shim/engine buffers are placed in PSRAM through `pal_engine_psram.lf`; the host build forces the SDL shim's target hooks to be strong so the LCD present, touch-event, and timing bridges cannot be garbage-collected. The host check verifies the protected-engine divergence manifest, protected engine object coverage, required support object coverage, required pack/FatFS/video/input/timing symbols, required bridge relocations, FatFS wrapper objects, stack-usage reports, no original `PAL_MKF*` runtime exports, no project-object heap/decompress/C++ allocation undefined calls, no runtime decompressor symbols, no target audio/SFX buffer symbols, 16MB flash/partition/pack layout consistency, CoreS3 SE shared SPI/SDSPI invariants, dynamic NOR/TF archive placement in the pack provider, short-name/static-buffer FatFS sdkconfig values, unsupported target stdio calls, and `.dram0.bss` / `.ext_ram.bss` budgets.

The no-audio bridge can be checked against the audio-enabled contract loop with:

```sh
make -C unix deterministic-newgame-noaudio-parity-check
```

That compares screen, state, script, and event traces for the deterministic new-game route.
A longer input-coverage variant also requires distinct framebuffer, state, and direction values:

```sh
make -C unix deterministic-newgame-extended-noaudio-parity-check
```

The stronger target bridge check compares the no-audio contract directly against stock desktop SDLPAL on a route that reaches the original walking/gameplay loop:

```sh
make -C esp32s3 native-engine-parity-check
```

Flash the full-engine host app:

```sh
make -C esp32s3 PORT=/dev/ttyACM0 flash-app
```

Equivalent explicit target:

```sh
make -C esp32s3 PORT=/dev/ttyACM0 flash-engine-host
```

Run the full native/IDF port gate, flash the generated NOR pack, then flash the full-engine host app:

```sh
make -C esp32s3 PORT=/dev/ttyACM0 TF_MOUNT=/media/$USER/PALTF flash-engine-port
```

Run the same gate, flash NOR, flash the full-engine host, then monitor the boot until the target proves both pack startup and the first LCD-present from the original engine:

```sh
make -C esp32s3 PORT=/dev/ttyACM0 TF_MOUNT=/media/$USER/PALTF engine-hardware-smoke
```

That target runs the full native/IDF port gate, stages `pal_tf.pak` to the mounted TF card, flashes the generated NOR pack, flashes the full-engine host, and then requires the serial log to contain `engine packs ready`, `engine first present`, and `engine present stats: frames=60`; it writes the captured monitor output to `/tmp/sdlpal-cores3se-native/cores3se_engine_hardware.log` by default.

The custom partition table assumes 16MB flash and reserves an 11MB read-only data partition named `pal_nor` at `0x310000`. Default NOR/TF archive placement is driven by `tools/pal_pack_layout_default.json`; move archives such as FIRE from NOR to TF there if the NOR budget tightens. The contract check also applies a 97% soft utilization gate to catch NOR growth before the partition is exhausted. The current generated NOR pack is about 10.45MB, so it fits there.

`make -C esp32s3 pack-build` also accepts `PACK_LAYOUT`, `PACK_NOR_ARCHIVES`, and `PACK_TF_ARCHIVES` for explicit layout experiments. The engine bridge no longer hard-codes which archive IDs are TF-backed; it resolves each chunk from the generated packs, accepts disjoint sparse halves of one archive, and rejects ambiguous non-empty duplicate chunks.

Flash the generated NOR pack:

```sh
make -C esp32s3 PORT=/dev/ttyACM0 flash-nor
```

Stage the generated TF pack onto a mounted microSD card before hardware smoke:

```sh
make -C esp32s3 TF_MOUNT=/media/$USER/PALTF prepare-tf
```

The firmware also tries to open a generated TF pack through FatFS at:

```text
0:/pal_tf.pak
```

`prepare-tf` copies `/tmp/pal_tf_default.pak` to the card as `pal_tf.pak` and syncs it. The CoreS3 SE TF slot shares SPI with the LCD: SCLK `GPIO36`, MOSI `GPIO37`, MISO/LCD D/C `GPIO35`, and TF CS `GPIO4`. The firmware mounts the card with ESP-IDF FatFS, uses FatFS paths for TF resource-pack reads and save-file reads, keeps the shared D/C-MISO pin in TF input mode by default, switches it to LCD D/C output only around LCD transfers, and reads the pack table of contents into `pal_psram_tf_toc`. It maps generated TEXT/FONT/UI/dialog/music/ending chunks from the NOR pack as read-only caches, loads menu FBP/image/box assets, battle background/effect scratch, RNG frame data, and ending FBP screens into named PSRAM buffers, then loads default mutable global records into the 256KB `pal_psram_save_state` buffer and loads scenes through the shared static scene cache: decoded `MAP` and `GOP` chunks are copied to `pal_psram_map_tiles` / `pal_psram_gop_copy`, current-scene event and party sprites are copied once from read-only NOR views into the 1.25MB `pal_psram_sprite_pin` buffer, and scene draw metadata stays in normal static storage. The target app intentionally does not link the script-trace verification slice; real script behavior belongs to the original engine path. The target also reserves an explicit SRAM tile slot and dedicated PSRAM screen/battle buffers for the later full-engine host path rather than overloading menu/background staging. Missing TF storage is non-fatal; the app falls back to the synthetic background.

The target FatFS config keeps only short filename support and disables FatFS dynamic buffers / LFN heap buffers. Runtime TF paths are therefore short names such as `0:/pal_tf.pak` and `0:/1.rpg`, with FAT file objects and sector buffers living in normal static/component storage instead of per-operation LFN heap allocations. PSRAM is configured as mapped external memory for named static `.bss` placement, not as general `malloc()` or `heap_caps_malloc()` storage. The main task stack is explicitly budgeted at 32KB, and the CoreS3 SE component is built with `-fstack-usage` so later full-engine hosting work can audit stack growth.

If any `0:/1.rpg` through `0:/5.rpg` save is present, the firmware reads only each short header's saved-times counter and logs the best slot. It does not parse full save state or use save-derived scene, party, player-role, scene-table, or event-object data; the smoke scaffold always loads the default mutable global cache.

The current firmware draws a 320x200 indexed framebuffer through `embedded/pal_video_static.c`, centered on the 320x240 LCD with 20-pixel black bars. Touch input is polled through FT6336 only for board/display smoke behavior: tapping the top or bottom black bar loads the previous or next scene from the TF pack, and touching inside the viewport draws a transient marker. The scaffold does not move the party, run collision checks, execute scripts/dialog/battles, or write saves. Preview animation state is kept in scaffold-only static metadata instead of mutating loaded save/event-object records. This proves the CoreS3 SE LCD/touch/TF/NOR scene path without adding audio, runtime decompression, or target-side script/gameplay execution.

The scaffold also logs non-gameplay performance counters: scene-load time, TF read bytes/calls/throughput with the LCD/TF pin switching path, max draw/flush/frame time over short windows, estimated PSRAM/NOR source bytes touched by the scene draw path, framebuffer write bytes, tile/blit counts, and main task stack high-water. Those logs are intended to guide later placement decisions before the real engine is hosted on ESP-IDF.
