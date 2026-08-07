# Nintendo DS native port

This directory owns the classic Nintendo DS/DS Lite port. The target is one
self-contained `.nds`: ARM9 code and the complete native `pal_full.pak` are in
the cartridge image, and gameplay never mounts FAT or opens an external TF
resource file. Mutable saves use the Slot-1 backup chip and are not part of the
read-only ROM image.

The port now builds and reaches real gameplay with native video, keys,
RIX music on the DS sound channels, NitroFS resource streaming, and Slot-1 save code. `make -C nds
check` is the authoritative build, ROM-content, and 4MB main-memory gate. Full
acceptance is still open: it requires the DeSmuME harness described below,
repeatable natural gameplay captures, save/reload coverage, and final DS/DS
Lite hardware testing.

## Target contract

- Build with devkitARM, libnds, `MEM_LEVEL2`, `PAL_NO_RUNTIME_HEAP`, and
  `PAL_NO_RUNTIME_DECOMPRESS`.
- Run the engine on ARM9. Use libnds directly for video, keys, timing, direct
  Slot-1 ROM access, and the ARM7 audio service; SDL is only a compile-time API
  shim on the target.
- Keep the legacy 320x200 space as a gameplay/resource coordinate contract.
  Render maps and UI directly at native 256x192 geometry; never resize a
  completed frame or write presentation coordinates back into gameplay state.
- `PAL_EXTREME_TWO_SCREENS` means two mutable native indexed engine canvases,
  not two physical DS displays. The DOS title and ending keep their clean
  scrolling background in the auxiliary canvas and draw cinematic layers on
  the presented canvas; the touch display remains the libnds console. A
  pointer into the single transient chunk is never retained across a delay or
  audio pump.
- Keep Level2's fixed lifecycle owners and reset rules. Do not add a general
  allocator, an eviction cache, runtime decompression, or VRAM-backed heap.
- Treat checker output plus the ARM9 ELF/map as the memory proof. Emulator or
  host-build success alone does not prove that the image fits a 4MB DS.

## Build and measured gate

The default data source is the shared generated complete pack at
`esp32s3/TF_datapak/pal_full.pak`:

```sh
make -C nds
make -C nds check
```

Override it only with another complete, structurally compatible pack:

```sh
make -C nds PAL_FULL_PACK=/path/to/pal_full.pak check
```

The gate checks every named fixed owner, leaves a required ARM9 main-RAM
margin, verifies that NitroFS contains only `/pal_full.pak`, and compares that
ROM extent byte-for-byte with the selected source pack. Its current output is
the source of truth for sizes; do not copy measured byte counts into prose.

## ROM resources and save storage

The build stages the complete pack at `build/nitrofiles/pal_full.pak` and lets
`ndstool` append it as NitroFS. At runtime `nitroFSInit(NULL)` mounts direct
Slot-1 and the provider performs bounded `lseek`/`read` operations on
`nitro:/pal_full.pak`. The pack remains in ROM rather than becoming ARM9
`.rodata`; `pal_core.pak`, chapter bundles, `PALSET.BIN`, DLDI, and
`fatInitDefault()` are not part of this target.

At boot, the provider derives the fixed resident Level2 view from the complete
pack while retaining bounded streaming access to all other chunks. The main
RAM owners include the scene, player, battle, and two fight arenas, one
resident-pack owner, one TOC owner, one transient chunk owner, and two
256x192x8 logical screens. The physical main-engine BG pages remain display
storage, not general RAM. A selected RIX track streams directly from Slot-1
into its fixed current-track owner; it never uses the shared transient chunk.

PAL saves use a fixed 1MiB type-3 SPI FLASH profile with five independent
slots, per-slot CRCs, and a commit footer written last. For emulator setup,
`make_slot1_save.py` can wrap an existing standard `N.rpg` as a raw 1MiB
Slot-1 image:

```sh
python3 -B nds/make_slot1_save.py /path/to/1.rpg /path/to/sdlpal.sav
```

The shipped image is deliberately classic NTR-only: the build passes
`ndstool -h 0x200`, so optional Calico TWL program headers are not copied into
the ROM. The checker requires unit code `0x00`, a `0x200` header, and zero DSi
extension fields. The ROM header identifies the image as `SDLPAL`, maker `00`,
with ndstool's `####` homebrew game code. The NTR header has no field for a
Slot-1 backup-chip type or capacity: its device-capacity byte describes ROM
capacity. Select `FLASH 8Mbit` explicitly in an emulator; for DeSmuME CLI this
is `--save-type 7`.

A physical cartridge or flashcart must actually expose compatible writable
Slot-1 backup storage. Running the `.nds` from a flashcart filesystem through
DLDI may still be useful as a compatibility test, but it is not evidence for
the direct Slot-1, no-external-resource contract.

## RIX music on DS sound hardware

ARM9 decodes the RIX command stream at 70Hz but no longer synthesizes a
22.05kHz OPL2 PCM stream. OPL register state drives nine fixed PCM8 wave loops;
rhythm mode reuses channels 6--8 for three fixed percussion loops. Calico's
standard ARM7 sound service owns the actual DS mixer updates. The build gate
also rejects the retired software-OPL state and PCM ring.

This is a bounded hardware approximation rather than bit-identical OPL2.
Pitch, key edges, operator multiplier/waveform, total level, rhythm triggers,
looping, volume, and track fades are retained; detailed OPL envelopes are
simplified. Final tone and speaker volume require a physical DS/DS Lite pass.

The reusable host profiler reports the actual RIX workload:

```sh
make -C embedded nds-rix-profile
embedded/build/pal_nds_rix_profile esp32s3/TF_datapak/pal_full.pak
embedded/build/pal_nds_rix_profile esp32s3/TF_datapak/pal_full.pak --track 1
```

## Primary emulator: DeSmuME SDL CLI

The reference source checkout is `/home/john/tools/desmume/desmume`. Its
`src/frontend/posix/cli/main.cpp` is a small SDL2 frontend: it polls SDL input,
runs one `NDS_exec()` step on the emulator thread, and presents the two native
GPU buffers through two SDL textures. This is the primary emulator and the
only host frontend to extend for automated NDS acceptance.

The audited checkout has a Meson `desmume-cli` target plus the loopback-only
WebSocket/headless harness used by this repository. The required host packages
on Debian are:

```sh
sudo apt install meson ninja-build libglib2.0-dev libsdl2-dev \
  libpcap-dev zlib1g-dev libx11-dev
```

Build only the SDL CLI frontend, without GTK:

```sh
cd /home/john/tools/desmume/desmume/src/frontend/posix
meson setup build-sdl --buildtype=release \
  -Dfrontend-gtk=false -Dfrontend-gtk2=false -Dfrontend-cli=true \
  -Dwifi=false
ninja -C build-sdl
```

The resulting executable is `build-sdl/cli/desmume-cli`. Meson currently
requires libpcap even with Wi-Fi disabled.

### WebSocket + SDL harness contract

The harness belongs in the DeSmuME SDL CLI frontend, never in the ARM9 ROM or
shared PAL engine. It must be disabled by default, bind only to `127.0.0.1`,
accept one client, reject browser `Origin` requests, and process every command
on the emulator thread at a frame boundary.

Its minimum control surface is deliberately small:

- pause/resume and advance an exact number of emulated frames;
- DS key down/up/tap, with tap duration measured in emulated frames;
- capture exact 256x192 main and touch GPU buffers after a completed frame;
- report frame number, run state, and held keys;
- flush cartridge backup storage, reset, and quit cleanly.

Input must update DeSmuME's ordinary keypad state, and capture must read
`GPU->GetDisplayInfo()` rather than an SDL window or X11 pixels. A visible SDL
window remains useful for human review; the harness mode must also run without
window focus or a window manager so CI does not depend on `xdotool`, wall-clock
sleeps, or guessed frame numbers. SDL remains the frontend's event, timing,
and audio integration layer.

The repository-side driver launches an isolated DeSmuME config/save directory,
uses exact emulated-frame actions, retains a bounded log, and writes captures
under `tmp_ui/nds/`:

```sh
python3 -B tools/nds_desmume_ws.py nds/sdlpal.nds \
  --session tmp_ui/nds/smoke \
  --action run:600 --action status --action capture:title.png
```

`--audio-capture title.raw` selects SDL's disk audio backend and reports the
captured and nonzero byte counts. Its raw format is 44.1kHz stereo signed
PCM16. Existing captures made through a different emulator path remain
historical bring-up artifacts, not final DeSmuME acceptance evidence.

## Acceptance boundary

Automated acceptance must exercise the real ROM and ordinary gameplay
semantics. Required capture groups are startup/title, natural map movement,
dialogue, system and inventory menus, a complete battle, an FBP/RNG cutscene,
and save/quit/relaunch/load. Emulator-side key and frame control is valid;
editing ARM9 memory, injecting PAL state, loading an emulator savestate as the
only route, or synthesizing screenshots is not natural-route proof.

For each changed screen, keep exact emulator framebuffer captures under
`tmp_ui/nds/` and perform the normal human review. DeSmuME proves emulator
integration; final sound timing, backup-chip behavior, controls, and visual
output still require a 4MB Nintendo DS or DS Lite.
