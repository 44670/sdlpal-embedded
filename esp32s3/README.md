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
- no audio, desktop codecs, runtime decompressor, splash sequence, or custom
  screen layouts;
- the Cardputer ADV vendor timing profile (240MHz CPU and a 1ms FreeRTOS tick);
- ST7789 240x135 indexed presentation through SPI3 and TCA8418 keyboard input;
- TF on independent SPI2 at 20MHz, with only a 2KB pack TOC resident in SRAM.

Build the firmware and the chapter resource packs:

```sh
make -C esp32s3 cardputer-extreme-build
make -C esp32s3 cardputer-extreme-pack-build
```

The stronger repeatable gate is:

```sh
make -C esp32s3 cardputer-extreme-check
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

Const/random-access assets remain in NOR.  Four decoded FBP screens and
decoded RNG movie 1 live on TF; the runtime reads FBP sequentially into screen
B and reads only one RNG frame at a time.  NOR and TF may therefore contain
disjoint chunks of the same archive, and the provider resolves ownership per
chunk.  Both packs carry one deterministic pack-set ID and a whole-image
CRC32; startup rejects a mismatched card, stale NOR/TF pair, or corrupted
image before exposing resources to the engine.

Prepare and flash:

```sh
make -C esp32s3 TF_MOUNT=/media/$USER/PALTF cardputer-extreme-prepare-tf
make -C esp32s3 PORT=/dev/ttyACM0 cardputer-extreme-flash-nor
make -C esp32s3 PORT=/dev/ttyACM0 cardputer-extreme-flash
```

`cardputer-extreme-prepare-tf` installs the generated TF image as
`pal_tf.pak`.  The firmware logs both general internal-RAM and DMA-capable
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
