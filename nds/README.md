# Nintendo DS native port

This directory owns the classic Nintendo DS/DS Lite port. The target is one
self-contained `.nds`: ARM9 code and the complete native `pal_full.pak` are in
the cartridge image, and gameplay never opens an external TF resource file.
Mutable saves are ordinary files on the loader's DLDI device (SD) and are not
part of the read-only ROM image.

The port now builds and reaches real gameplay with native video, keys,
NitroFS resource streaming, DLDI FAT save code, and a threaded full-rate
software RIX/OPL2 music backend. `make -C nds check` is the authoritative
build, ROM-content, audio-equivalence, and 4MB main-memory gate. Full
acceptance is still open: it requires the DeSmuME harness described below,
repeatable natural gameplay captures, save/reload coverage, and final DS/DS
Lite hardware testing.

## Target contract

- Build with devkitARM, libnds, `MEM_LEVEL2`, `PAL_NO_RUNTIME_HEAP`, and
  `PAL_NO_RUNTIME_DECOMPRESS`.
- Run the engine on ARM9. Use libnds directly for video, keys, timing, direct
  Slot-1 ROM access, and the Calico ARM7 sound service; SDL is only a
  compile-time API shim on the target.
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
`.rodata`; `pal_core.pak`, chapter bundles, and `PALSET.BIN` are not part of
this target. The DLDI interface (`fatInitDefault()`) is used only for the
save files described below, never for resources.

At boot, the provider derives the fixed resident Level2 view from the complete
pack while retaining bounded streaming access to all other chunks. The main
RAM owners include the scene, player, battle, and two fight arenas, one
resident-pack owner, one TOC owner, one transient chunk owner, and two
256x192x8 logical screens. The physical main-engine BG pages remain display
storage, not general RAM. A selected RIX track streams directly from Slot-1
into its fixed current-track owner; it never uses the shared transient chunk.

PAL saves are files on the DLDI device: five fixed slots at
`fat:/sdlpal/N.sav`, each a 192KiB image with a per-slot CRC payload and a
commit footer at the fixed tail offset. A write goes to `N.tmp`, is read back
and verified, and is renamed over the live slot last, so a power loss
mid-write keeps the previous committed save. This backend is deliberate: a
homebrew launched from SD has no Slot-1 backup chip behind it, and
nds-bootstrap only redirects `cardEeprom*` for retail ROMs it recognizes by
Nintendo SDK signatures — never for homebrew — so the retired Slot-1 EEPROM
backend could not persist anything on the accepted TWiLight Menu boot path.
`make_slot1_save.py` is retired with that backend.

The shipped image keeps Calico's default TWL-aware ARM9/ARM7 ELFs and the
full `0x4000` ROM header with the TWL loadlist (unit code `0x02`). The
NTR-only image (`ndstool -h 0x200`, unit code `0x00`) white-screens on the
accepted real-hardware boot path (TWiLight Menu / nds-bootstrap); this was
verified by byte-comparing against a known-good on-device binary. The
checker requires unit code `0x02`, a `0x4000` header, and zero DSiWare
public/private save fields. The ROM header identifies the image as `SDLPAL`,
maker `00`, with ndstool's `####` homebrew game code.

## Forced DS (NTR) mode under TWiLight Menu

TWiLight Menu classifies homebrew by the ARM9 boot-code signature, unit code,
and ARM7 address — never by game code — and direct-boots "modern" homebrew in
DSi mode, bypassing nds-bootstrap. To run this port in forced DS (NTR) mode
without losing the TWL header that the direct boot path requires, the build
emits `sdlpal.nds.ini` next to the ROM. Copy it to the TWiLight per-game
settings directory with a name matching the ROM file:

```sh
cp nds/sdlpal.nds.ini <sd>/_nds/TWiLightMenu/gamesettings/sdlpal.nds.ini
```

`DSI_MODE = 0` routes the launch through nds-bootstrap with DS mode forced
(67MHz, 4MB view); the touch-screen boot log then prints `mode: NTR (DS)`.
Without the ini the same ROM boots in DSi mode and prints `mode: TWL (DSi)`.
Emulators and flashcart kernels that load the `.nds` directly ignore the ini.

## Threaded RIX/OPL2 music

The game thread decodes RIX at 70Hz and queues the original ordered OPL2
register writes. It does not render PCM. A peer-priority Calico ARM9 worker
owns DOSBox's integer OPL2 core. It sleeps to the physical PCM cadence, renders
one short block when it wakes, and can run while the game thread is in its
normal `threadWaitForVBlank()` idle interval. No DBOPL2 work runs synchronously
on the game thread.

The hot OPL2 code is linked into ARM9 ITCM. Its active state, render scratch,
exact-envelope work buffers, and compact OPL2 waveform table are fixed owners
in DTCM, including the small multiply table. The standard DOSBox multiply-table
backend directly renders every nominal 32.768kHz mono PCM frame. There is no
reduced internal sample rate, interpolation, or resampling. The target applies
only a bounded 2x final gain.

This specialization removes unused OPL3 channels and modes, compacts the four
OPL2 waveforms from 8KiB to 4KiB without changing their samples, precomputes
the two exact envelope streams once per LFO span, and renders the melodic
operators from localized state. When both operators of an FM channel have a
provably constant envelope for the complete span, a separate hot loop reuses
their two fixed attenuation values and skips both envelope-buffer passes. The
compiler is given explicit non-aliasing contracts for the render buffers, which
keeps channel state out of the ARM946E-S loop spills. Scratch conversion, final
gain, and clipping share one output pass instead of scanning every PCM block a
second time. RIX rhythm mode uses DBOPL's six-operator percussion path and is
not supported on this CPU budget: `music_load_track` checks the RIX rhythm byte
and rejects such a track before decoder initialization, leaving gameplay
running with silence for that track. The host differential test instantiates
the optimized and unmodified DBOPL multiply-table cores side by side and
requires bit-identical PCM and envelope state over deterministic randomized
melodic OPL2 traffic. It currently compares 4,690,350 samples on every
`make -C nds check`.

The PCM channel uses fixed 256-frame blocks. `soundTimerFromHz(32768)` selects
the closest DS timer (511), whose physical rate is about 32.793kHz; 32.768kHz
is the synthesis/API rate. A rational sample-phase accumulator places each RIX
update at a true physical 70Hz boundary, so the timer, sequencer, worker, and
looping PCM channel cannot drift. The ring retains 32 blocks, so its time span
is unchanged at about 250ms while one uninterrupted render is half as long.
Each consumed RIX tick requests one future tick from the game thread; a bounded
20-tick pump catches up across sparse presentation calls without blocking the
UI or overflowing the 32-tick queue. NitroFS reads are sliced at 1KiB pump
boundaries so a New Game resource load cannot starve that producer.

All storage is fixed: one 10,108-byte current-track owner, a 32-tick SPSC OPL
queue, two fixed DBOPL2 states, fixed lookup tables, one PCM ring, and one
worker stack. A full queue drops music work and records an overrun; it never
blocks the UI. In addition to the queue/deadline counters, the ELF exposes
DBOPL2 render call, total, minimum, and maximum target-tick counters.

The complete pinned pack peaks at 251 ordered OPL writes in one RIX tick; the
target capacity is 256. The repeatable profiler command below measures this
instead of relying on a hand-audited track. The timing-valid 900-frame DeSmuME
recording is under
`tmp_ui/nds/dbopl2-melodic-only-final-audio-900-20260808/`; it contains
nonzero PCM, peaks at -11.27dBFS, and does not clip. The final 6,000-frame
target profile under
`tmp_ui/nds/dbopl2-optimized-profile-6000-fused-20260808/` completed with zero queue
underruns, queue overruns, or audio deadline misses. Its 12,479 256-frame
blocks averaged 1,232 Calico ticks (2.35ms) for synthesis, final gain, clipping,
and PCM output with a 1,871-tick (3.57ms) maximum, against a 7.81ms hardware
block interval. The measured span can include peer-thread preemption; these are
target-side deadline-budget measurements, not hardware PMU cycle counts. The
current natural New Game run under
`tmp_ui/nds/dbopl2-melodic-only-final-natural2-20260808/` also completed with
all three audio failure counters at zero. A forced run of melodic Track 23,
whose profiler occupancy is nine held channels throughout the track, is under
`tmp_ui/nds/dbopl2-melodic-worst-track23-2000-20260808/`; it averaged 1,514
Calico ticks (2.89ms), peaked at 1,915 ticks (3.66ms), and kept all three
failure counters at zero. The forced Track 18 rhythm-rejection run under
`tmp_ui/nds/dbopl2-rhythm-reject-track18-2000-20260808/` completed 2,000 frames
with all three failure counters at zero; its synthesis callback averaged 68
Calico ticks (0.13ms) because the rejected track never entered percussion
synthesis. Physical DS/DS Lite listening is still required for final volume
and tone acceptance.

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

### Local CPU interpreter fix (required)

The audited checkout carries a local fix in `src/arm_instructions.cpp`: the
`LDM{IB,IA,DA,DB}2(_W)` handlers (exception-return `ldm{.., pc}^`) must align
the restored pc by SPSR.T — halfword (`& 0xFFFFFFFE`) when returning to Thumb,
word (`& 0xFFFFFFFC`) when returning to ARM. Upstream masks with
`0xFFFFFFFC | (BIT0(tmp)<<1)`, which truncates a Thumb resume address at
`pc % 4 == 2` down by two bytes; the CPU then decodes the second halfword of a
32-bit Thumb instruction and crashes randomly minutes into gameplay (observed
as wild jumps, IME being cleared by a stray `tickTaskStart` tail, or prefetch
aborts). Real hardware is unaffected.

The checkout also exposes two debug-only WebSocket commands, `regs` (ARM7/ARM9
pc/lr/sp/cpsr) and `mem` (bounded 32-bit reads), used only by the bring-up
harness.

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

`mem:ADDRESS:LENGTH` forwards the harness's bounded ARM9 debug read and prints
the returned words as hexadecimal strings. Resolve diagnostic-owner addresses
from the current ELF with `arm-none-eabi-nm`; addresses are intentionally not
hard-coded in the driver.

For timing-valid disk capture, the local CLI frontend must honor
`DESMUME_WS_REALTIME` when applying its headless defaults instead of forcing
the limiter off. The driver sets that variable and matches SDL's disk-audio
delay to DeSmuME's 2940-frame callback (67ms); otherwise callback-rate silence
swamps the emulated signal and the raw duration is not meaningful.

## Acceptance boundary

Automated acceptance must exercise the real ROM and ordinary gameplay
semantics. Required capture groups are startup/title, natural map movement,
dialogue, system and inventory menus, a complete battle, an FBP/RNG cutscene,
and save/quit/relaunch/load. Emulator-side key and frame control is valid;
editing ARM9 memory, injecting PAL state, loading an emulator savestate as the
only route, or synthesizing screenshots is not natural-route proof.

For each changed screen, keep exact emulator framebuffer captures under
`tmp_ui/nds/` and perform the normal human review. DeSmuME proves emulator
integration; it has no DLDI device, so save I/O reports unavailable there and
save/reload acceptance runs on hardware. Final sound timing, save-file
behavior, controls, and visual output still require a 4MB Nintendo DS or DS
Lite.
