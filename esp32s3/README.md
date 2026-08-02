# ESP32-S3 ports

The primary configuration is the M5Stack Cardputer ADV music profile below.
The CoreS3 SE port and its older bring-up scaffold remain available later in
this document.

## Cardputer ADV 8MB/no-PSRAM extreme profile

The default Cardputer workflow selects the M5Stack Cardputer ADV (K132-Adv),
not the original Cardputer. It has:

- 8MB flash with a 768KB app partition, a `0x460000` core cache, and a
  `0x2d0000` chapter cache;
- no PSRAM and no engine/resource heap allocation;
- exactly two named 240x135x8-bit native screens plus one 4KB LCD DMA strip;
- RIX/OPL2 music enabled by default; MIDI, VOC, and SFX remain excluded;
- no runtime decompressor or splash sequence;
- the Cardputer ADV vendor timing profile (240MHz CPU and a 1ms FreeRTOS tick);
- ST7789 240x135 indexed presentation through SPI3 and TCA8418 keyboard input;
- TF on independent SPI2 at 20MHz, with only a 2KB pack TOC resident in SRAM.
- USB Serial/JTAG as the console, leaving the ES8311 word-select GPIO43 free
  from UART0 output.

### Quick start

The pinned Fusion Pixel Font archive is vendored, so no font argument is
normally needed. Turn a PAL data directory into a complete, verified TF data
directory in one command:

```sh
make -C esp32s3 \
  PAL_DATA_DIR=/path/to/PAL \
  cardputer-adv-music-tf
```

This writes the ready-to-copy card contents directly to
`esp32s3/TF_datapak/`: `PALSET.BIN`, `pal_core.pak`, `pal_tf.pak`,
`pal_full.pak`, `EVENT.DEF`, `b00.pak` through `b14.pak`, and the audit
manifest. Copy the contents of that directory to the TF card. The default
audited source directory is `/mnt/hgfs/deb13/PALSteam/PAL_DOS`, so
`PAL_DATA_DIR` may be omitted for that data set. `TF_datapak/` is generated
output and is ignored by Git.

Install the cache partition layout once on a new board, or again only when the
partition layout changes:

```sh
make -C esp32s3 PORT=/dev/ttyACM0 cardputer-adv-music-provision
```

After that, normal firmware updates write only the application partition:

```sh
make -C esp32s3 PORT=/dev/ttyACM0 cardputer-adv-music-flash-app
```

Discover the actual serial port instead of assuming `/dev/ttyACM0`. Prepare
the TF card before first boot. `cardputer-adv-music-provision` necessarily
writes the bootloader and partition table as well as the app; it does not
write either resource partition. The regular gate is:

```sh
make -C esp32s3 cardputer-adv-music-check
```

The application build itself does not require a PAL directory and contains no
pack-set ID, core hash, chapter hash, or corpus-specific FONT10 identity. A
single compatible app can therefore boot different TF pack sets produced by
the builder. The file formats, fixed 10x10 font geometry, and engine ABI still
have to match; this is not compatibility with arbitrary PAL editions.

The runtime core contains only the corpus-subsetted FONT10 payload. `FONT`
chunk 0, the converted original 16px font, is absent from the core and remains
only in `pal_full.pak`. The firmware validates the full FONT10 structure and
fixed 10x10 geometry rather than comparing it with a hash compiled into the
app.

The default source set is the stock
`/mnt/hgfs/deb13/PALSteam/PAL_DOS` directory. Resources from the former
`/mnt/hgfs/deb13/PAL` directory are not save/script compatible and must not be
mixed into this pack set. The real-data regression gate pins event object 113
to trigger entry 7739 and verifies the opening Lin carpenter dialogue in the
matching `M.MSG` table.

The first responsive-display acceptance target is the Cardputer's physical
240x135 panel. The engine draws directly into a 240x135 indexed framebuffer;
`main/cardputer_extreme_native_view.[ch]` only converts those native pixels to
RGB565 strips. It never consumes or resizes a completed 320x200 frame.

320x200 survives only as the compatibility coordinate space used by existing
scripts, events, collision rules, saves, and battle animation state. The map
renderer requests a 240x135 world region directly and translates visible
sprites into that native view. Battle applies the same rule to its background,
fighters, effects, markers, and HUD: resources are composed on the physical
canvas, not transformed after composition. Presentation offsets are never
written back into gameplay state.

Map and battle retain the original layers, sprites, animations, target marks,
action diamond, player information assets, and input behavior. Their camera
and element positions are adjusted only where the physical canvas requires it.
Decoded full-screen FBP materials may live in NOR or TF. TF-owned backgrounds
are streamed through one named 320-byte scanline and sampled directly into the
native framebuffer; they never allocate or stage a 64KB completed frame.
The 160x128 generated profile is a later engine/host check, not a second
Cardputer panel mode.

Dialogue and menu layout are follow-up slices. They must continue from PAL's
original draw functions and DATA.MKF assets, change only positions and sizes
needed by the physical canvas, and draw the pinned 10px FONT10 at physical
native pixel size. Do not revive scrolling replacement UI, a second display
mode, or a semantic layout framework. Bounded portrait fitting in
`embedded/pal_native_ui.[ch]` remains the approved per-asset downsampling path.
The system-menu box starts at `(0, 0)` and only its item list scrolls when the
LCD cannot show every entry.

The 240x135 buy screen keeps the original item frame, current-count and cash
boxes, item names, prices, and confirmation menu. It uses a six-row list beside
the information column; only that item list scrolls for longer stores. Scripts
that begin directly with dialogue text, without a preceding dialog-position
opcode, use the original default upper/no-portrait layout rather than stale or
uninitialized native geometry.

Visual acceptance uses real gameplay captures under `./tmp_ui/`, never
Python-drawn mockups. The current vertical slice requires inspected 240x135 map
and battle frames; dialogue, menu, and 160x128 captures become required when
those slices are implemented.

For efficient interactive capture, build the native Linux host with
`CARDPUTER_EXTREME_NATIVE_WS=1` and use `tools/ws_cli.py`; see
[`unix/WEBSOCKET_HARNESS.md`](../unix/WEBSOCKET_HARNESS.md). The server is
host-only and is never linked into ESP-IDF firmware.

### TF-managed resource caches

`PALSET.BIN` is the small target-readable data-set record. It contains the
pack-set ID, exact `pal_core.pak` size and SHA-256, and all 15 chapter bundle
descriptors. The human-readable `chapter_manifest.json` is copied for audit
but is not parsed by the firmware, preserving short filenames and fixed FatFS
storage.

At every boot, before the core is mapped, the target reads `PALSET.BIN` and
hashes the core SPI-NOR partition. A mismatch displays the native 240x135
`LOADING` screen, verifies `0:/pal_core.pak`, erases and copies it sequentially,
then verifies the NOR readback. A reset during this process leaves a bad hash,
so the next boot repeats the copy before using any core data.

At game load and whenever a scene needs another bundle, the target selects
`0:/bNN.pak` from the external catalog. It verifies TF, writes the replaceable
chapter partition with progress, verifies NOR readback and pack ownership, and
writes the commit record last. The process-global `current_bundle` records the
already verified and mapped bundle, so scenes in the same bundle do not hash it
again. A bundle transition clears that state and performs the check before the
new scene resources are exposed.

Both caches are implementation details: resource source-of-truth stays on TF,
and neither pack is flashed by the normal host workflow. `pal_full.pak` remains
an offline-complete decoded/native mirror; firmware does not index it because
its TOC exceeds the fixed active-index budget.

The generated resource closure covers scenes 1 through 299, but this is not
yet a full-game runtime claim. The stock PALSteam/PAL_DOS data supplies 5,332
mutable event records and 294 scene rows. `EVENT.DEF` appends 37 unreachable
zero event records and six zero-content sentinel scene rows so the established
42-event-page plus one-scene-page `EVENT.STA` journal geometry remains stable.
Exactly three 4KB event pages are resident in SRAM. Tagged saves stream the
complete normalized event/scene state through the same fixed page storage.

The default fixed-storage OPL2/RIX backend retains all 88 original MUS slots;
source slots 0 and 29 are empty and the other 86 are playable. The profile gate
checks the music/no-SFX compile graph, static SRAM and stack budgets, fixed
music state-machine behavior, and audible output from every non-empty track.
The sequencer, OPL state, and sample-counted fades freeze while music is
disabled or its volume is zero.

`EVENT.DEF` and `EVENT.STA` are keyed by the data template rather than by the
application. New Game resets event and scene state to the installed template;
a compatible tagged save restores it, while a different template CRC rebases
to the new defaults.

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

The gate validates the exact partition table, app/section/stack/SRAM limits,
music source inventory, absence of target heap/decompression/SFX calls, all TF
pack hashes, the external set record, and recovery-oriented cache formats.
Generated scene coverage remains a candidate rather than a proof of a complete
story playthrough. Real-board acceptance must still verify LCD, keyboard, TF,
save/reload, audio telemetry, and the intended story route. The firmware logs
internal-RAM/DMA low-water marks and main-task stack high-water marks around
resource startup and battle.

The older `cardputer-extreme-*` targets remain for explicit no-audio or
non-cache regression work. They are not the default installation path and may
still require host-flashed resource images.

## CoreS3 SE

The CoreS3 SE full-engine host is a separate 16MB-flash/8MB-PSRAM target. Its
board init adapts the local walkie-talkie reference at
`/home/john/work/CardPuterADV/esp-walkie-talkie`: AW9523, AXP2101, FT6336
touch, and SPI LCD init use the same pins and command sequence.

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

That builds the ESP-IDF artifact, rebuilds the default resource packs from `/mnt/hgfs/deb13/PALSteam/PAL_DOS`, verifies the NOR pack fits the `pal_nor` partition and stays under the 97% soft utilization gate, rejects compressed/YJ1 payloads in both NOR and TF packs, validates the generated pack manifest source hashes, decoded-size summaries, and pack-layout hash/archive placement, checks the protected-engine-file divergence manifest, scans the target-side sources and linked project objects for heap/decompress calls, checks that every target-linked source is covered by the source scan, checks the shared SPI/SDSPI invariants for the CoreS3 SE TF/LCD bus, checks the scaffold-freeze guard against target-side script/dialog/battle/save mutation, checks key ELF sections, verifies the named SRAM/PSRAM buffer registry, checks generated `-fstack-usage` files, rejects disabled target sources plus target audio/SFX symbols, and reports `pal_sram_` / `pal_psram_` symbol totals from the ESP32-S3 ELF.

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
