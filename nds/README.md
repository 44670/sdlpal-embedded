# Nintendo DS native port

This directory owns the classic Nintendo DS/DS Lite port. The accepted target
is one self-contained, NTR-only retail-style `.nds`. ARM9 code and the complete
native `pal_full.pak` are in the ROM image. The game has no TWL mode, homebrew
boot path, DLDI target, launch-volume filesystem dependency, or flashcart-
specific protocol.

The ROM exposes Nintendo SDK-shaped CARD, IRQ, NitroFS, and backup-device
surfaces so an ordinary retail loader can discover and redirect it. It does
not know whether that loader is nds-bootstrap, a flashcart kernel, or another
retail-compatible implementation. TWiLight Menu++ v27.24.1 classifies the
image as retail and selects official nds-bootstrap v2.16.0, which boots the
ROM, redirects reads beyond 32MiB, and persists/reloads the 1MiB save sidecar.
`make -C nds check` is authoritative for the ROM surfaces, bundle save image,
audio equivalence, and 4MB memory bounds.

## Target contract

- Produce an NTR-only retail-style image: unit code `0x00`, no TWL header
  extensions or load lists, no DLDI patch target, and no homebrew loader ABI.
  NTR-only is an image property, not a TWiLight per-game mode override.
- Target the retail CARD/backup contract, not R4, DSpico, or another device's
  private API. Compatible loaders adapt themselves by applying their normal
  retail-ROM and save redirection.
- Build with devkitARM, libnds, `MEM_LEVEL2`, `PAL_NO_RUNTIME_HEAP`, and
  `PAL_NO_RUNTIME_DECOMPRESS`.
- Run the engine on ARM9. Use libnds directly for video, keys, timing,
  NTR Slot-1 card access, and the Calico ARM7 sound service; SDL is only a
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
make -C nds twilight-bundle
```

The last command writes the two files that must be copied together:

```text
nds/build/twilight/roms/nds/sdlpal.nds
nds/build/twilight/roms/nds/saves/sdlpal.sav
```

Override it only with another complete, structurally compatible pack:

```sh
make -C nds PAL_FULL_PACK=/path/to/pal_full.pak check
```

The gate checks every named fixed owner, leaves a required ARM9 main-RAM
margin, verifies that NitroFS contains only `/pal_full.pak`, and compares that
ROM extent byte-for-byte with the selected source pack. It also requires unit
code `0x00`, a zeroed TWL header area, the retail classifier and ARM7/ARM9 SDK
patch surfaces, valid header and secure-area CRCs, a trimmed NTR image, bounded
ARM7 relocation/WRAM ranges, and no DLDI or retired homebrew-storage symbols.
Its current output is the source of truth for sizes.

## ROM resources and save storage

The build stages the complete pack at `build/nitrofiles/pal_full.pak` and lets
`ndstool` append it as NitroFS. A loader must expose the selected image through
ordinary retail CARD reads. Runtime code parses that ROM's FNT/FAT directly;
it never discovers the ROM through `argv[0]`, opens the `.nds` as a FAT file,
calls `fatInitDefault()`, or uses DLDI. The pack remains in ROM rather than
becoming ARM9 `.rodata`;
`pal_core.pak`, chapter bundles, and `PALSET.BIN` are not part of this target.

At boot, the provider derives the fixed resident Level2 view from the complete
pack while retaining bounded streaming access to all other chunks. The main
RAM owners include the scene, player, battle, and two fight arenas, one
resident-pack owner, one TOC owner, one transient chunk owner, and two
256x192x8 logical screens. The physical main-engine BG pages remain display
storage, not general RAM. A selected RIX track streams directly from Slot-1
into its fixed current-track owner; it never uses the shared transient chunk.

Saves use the retail backup-device path. The loader redirects the game's
ordinary backup accesses to one 1MiB sidecar `.sav`; the game does not create
files or mount a FAT volume. The five fixed 192KiB PAL slots fit in that image
and retain their per-slot CRC and commit footer.
`make_slot1_save.py` defines the pre-provisioned image layout. Retail software
knows its backup protocol: this title always uses type-3, three-byte-address
flash and does not probe a chip type or size at runtime. Each write erases its
three 64KiB sectors, writes the payload, and writes the footer last. The load
path validates the footer and complete payload CRC.

The NTR header has no standard field for backup capacity. TWiLight's retail
save-size table is keyed by known game codes, and its fallback for this unique
`ZPLE` code is 512KiB. The ROM therefore does not impersonate another title to
obtain a larger automatic save. A normal build instead creates an erased,
exactly 1MiB `build/sdlpal.sav`, and `twilight-bundle` places it in TWiLight's
standard sibling `saves/` directory. TWiLight preserves that existing size and
passes the path to nds-bootstrap. Copying only the `.nds` is incomplete
installation for this title.

Provision a known PAL save into the required raw sidecar before first launch:

```sh
python3 -B nds/make_slot1_save.py /path/to/0.RPG /path/to/sdlpal.sav
```

Use the sidecar name and location required by the selected loader.

The final ROM header must identify an NTR title with a unique game code and a
retail classifier signature, while retaining the normal `0x4000` NTR header
area and valid header/secure-area CRCs. A 16-byte entry veneer occupies unused
zero fill at ROM offset `0x47e0`: its four words match TWiLight's Nintendo SDK
3 classifier and its first instruction branches to the untouched Calico
`MOD9` entry at `0x4800`. No SDK runtime or loader ABI is added. The image must
not contain TWL metadata or a DLDI patch target. This is a retail-loader
contract, not a claim that the image is an officially signed Nintendo release.

## Retail-loader route

The ROM contains no R4, DSpico, or other flashcart-specific implementation.
Those products can support it only by recognizing the ROM as a retail NTR title
and providing their normal CARD and backup-device redirection. A DLDI/homebrew
launch is not a supported fallback.

Official TWiLight Menu++ v27.24.1 reads the four ARM9 entry words and records
`HOMEBREW_BOOTSTRAP = 0` for this image. Its generated
`fat:/_nds/nds-bootstrap.ini` names `sdlpal.nds`, the existing 1MiB
`saves/sdlpal.sav`, and `DSI_MODE = 0`; it chooses
`nds-bootstrap-release.nds`, not the `hb-` binary. No per-game override or
loader-private file is shipped by this target.

Official nds-bootstrap v2.16.0 is the verified retail backend. It finds the
SDK-style ARM9 CARD/IRQ surface and ARM7 universal-backup surface, installs its
card engines, maps the selected ROM as Slot-1, and redirects the 1MiB save. The
Calico ARM7 build uses a small NTR retail startup because its normal homebrew
startup clears `0x02FFD000`, which is loader-owned memory. The replacement
loads only the declared sections and joins the loader-patched IRQ tables to
Calico's live dispatchers.

The test happens to run the official menu and backend through DeSmuME's
emulated R4 device. That device is only the environment running the loaders;
it is not a target API and no R4 code is linked into the game. DeSmuME does not
complete TWiLight's hardware-style reboot/chainload in the same emulator
process, so acceptance is split at the card boundary: the official menu writes
and dumps its classifier result and bootstrap INI, then the exact official
retail bootstrap binary is launched against that dumped configuration and
sidecar. This proves both halves of the handoff but does not turn the R4 test
fixture into a supported target. Physical loader, controls, and audio
acceptance remain separate hardware work.

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

### Local emulator fixes (required)

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

The audited checkout also fixes AUXSPI transaction boundaries in `src/MMU.cpp`
and `src/mc.cpp`. Chip select is held only while backup SPI mode is enabled,
and a pending command reset is applied before the first byte of the next
transaction. Without both changes, DeSmuME consumes the next retail-save
command as data from the preceding command: reads begin at the wrong address,
and `WREN` swallows the page program. This is an emulator fix; the ROM uses the
SDK-shaped retail backup calls with a direct libnds cartridge fallback.

DeSmuME still reports the three standard `D8` sector-erase commands as
unhandled, but its byte-backed page program overwrites the affected data. The
acceptance run therefore verifies the resulting payload/footer CRCs and a
relaunch load; actual erase behavior remains part of physical-cart acceptance.

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
the returned words as hexadecimal strings. `regs`, `flush-save`, and `reset`
expose the corresponding emulator-thread commands. Resolve diagnostic-owner
addresses from the current ELF with `arm-none-eabi-nm`; addresses are
intentionally not hard-coded in the driver.

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
integration and direct Slot-1 ROM reads. Running the official nds-bootstrap
binary additionally proves that a retail loader finds these patch surfaces and
redirects the ROM/save in this environment. Final sound timing, controls, and
loader behavior still require a 4MB Nintendo DS or DS Lite.

The NTR retail/save acceptance run is under
`tmp_ui/nds/ntr-retail-slot1-save-final4-20260809/`. It starts from a 1MiB raw
sidecar containing stock `0.RPG`, discovers save count 4, loads it through
AUXSPI, saves count 5, validates the persisted payload and footer CRCs, then
relaunches and loads count 5. These are natural menu/input operations through
the real gameplay loop, not injected ARM9 state.

The nds-bootstrap v2.16.0 acceptance is under
`tmp_ui/nds/bootstrap-v2.16.0-retail-20260809/`. The final save run is
`bootstrap-save-final4/`: it loads count 4, saves count 5, and its extracted
1MiB sidecar has valid generation-2 payload/footer CRCs. After copying that
sidecar back into the emulated card, `bootstrap-reload-final/` relaunches the
loader, shows count 5, and loads the map through natural menu input.

The automatic TWiLight classification rerun uses official TWiLight Menu++
v27.24.1 and official nds-bootstrap v2.16.0. The selected-title capture is
under `tmp_ui/nds/twilight-v27.24.1-dsimenu-nav2-20260810/`; the dumped settings
and generated INI under
`tmp_ui/nds/twilight-v27.24.1-retail-configure-20260810/` record
`HOMEBREW_BOOTSTRAP = 0` and name the 1MiB sidecar. The retail-backend save and
reload captures are under
`tmp_ui/nds/twilight-v27.24.1-save-route-20260810/` and
`tmp_ui/nds/twilight-v27.24.1-reload-route-20260810/`. They load count 4, save
count 5, validate the resulting 1MiB generation-2 payload/footer CRCs, then
relaunch and load count 5 through natural menu input.
