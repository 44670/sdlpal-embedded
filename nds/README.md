# Nintendo DS native port

This directory owns the classic Nintendo DS/DS Lite port. The target remains
an NTR-only DLDI homebrew `.nds`; it does not support TWL mode or a
flashcart-private storage API. At runtime it selects one of two storage routes:

- a `fat:/...` path in `argv[0]` selects the normal DLDI route: the game opens
  that ROM for NitroFS and stores ordinary saves on FAT;
- no FAT argument plus a Slot-1 boot source selects the emulator route:
  Calico opens the card ROM with `nitroromGetSelf()` and libnds accesses a
  1MiB type-3 SPI FLASH save device directly.

The supported DSpico route is:

1. The official DSpico bootloader switches the cartridge into unscrambled
   game mode, zeroes the DS card-command scrambler seeds, and starts
   `fat:/_picoboot.nds` through Pico Loader.
2. Pico Launcher selects `sdlpal.nds` and starts Pico Loader again.
3. Pico Loader recognizes the image as homebrew, supplies a
   `fat:/.../sdlpal.nds` argument, and patches the image's 16KiB DLDI slot with
   the active DSpico driver.
4. The game mounts that driver with `fatInitDefault()`, opens its own `.nds`
   path from `argv[0]`, and mounts the embedded NitroFS with
   `nitroromOpen()`/`nitroFSMount()`.
5. Resources are read from `nitro:/pal_full.pak`; saves are ordinary files at
   `fat:/sdlpal/1.rpg` through `fat:/sdlpal/5.rpg`.

The second route is intentionally limited to a direct Slot-1 launch. It does
not probe DLDI when the launcher supplied no FAT path, and the DLDI route does
not touch the card backup device.

## Build and measured gate

The default complete pack is
`esp32s3/TF_datapak/pal_full.pak`:

```sh
make -C nds
make -C nds check
```

Use another structurally compatible complete pack with:

```sh
make -C nds PAL_FULL_PACK=/path/to/pal_full.pak check
```

The build produces `nds/sdlpal.nds`. It uses unit code `0x00`, maker code
`00`, game code `####`, zero homebrew autoload hooks, a zeroed TWL header area,
and one 16KiB DLDI patch target. The image keeps the homebrew multiboot secure
layout with an explicit already-decrypted marker; it is neither encrypted nor
converted into a retail ROM. The marker and nonmatching retail secure checksum
keep Pico Loader out of its optional Blowfish-key path.

`make -C nds check` verifies those loader-facing properties, the DLDI section,
NTR-only layout, NitroFS contents, pack identity and bytes beyond 32MiB, both
storage backends, fixed memory owners, ARM9/DTCM margins, and the bounded RIX
workload. It also rejects the retired custom retail CARD/ARM7 storage shim.

## Install for Pico Launcher on DSpico

Prepare DSpico's official bootloader, Pico Launcher, and Pico Loader according
to their upstream instructions. The bootloader is part of the DSpico firmware
setup, not a file supplied by this project. The SD card must contain the normal
launcher/loader files:

```text
/_picoboot.nds
/_pico/aplist.bin
/_pico/patchlist.bin
/_pico/picoLoader7.bin
/_pico/picoLoader9.bin
/_pico/savelist.bin
```

Copy `sdlpal.nds` anywhere Pico Launcher can browse. Do not pre-patch this
file with a host DLDI tool: Pico Loader patches the live image with the DLDI
driver it received from Pico Launcher. No `.sav` sidecar belongs beside the
ROM.

DSpico's bootloader setup requires the upstream NTR Blowfish key material for
its embedded default boot image. That bootloader, not SDLPAL, sends the `FC`
mode-switch command and installs zero command-scrambler seeds before loading
`/_picoboot.nds`. The launcher/loader chain does not assert the physical
Slot-1 reset that makes the DSpico firmware leave this mode. The selected
`sdlpal.nds` is already-decrypted homebrew, so Pico Loader does not request
Blowfish keys for this ROM.

Existing standard PAL saves can be copied directly, for example:

```text
/sdlpal/1.rpg
/sdlpal/2.rpg
```

On this route, save writes overwrite the selected file through normal DLDI/FAT
I/O; no `.sav` sidecar or backup-chip protocol is involved.

## Direct Slot-1 emulator route

Start `sdlpal.nds` directly in Slot-1 without a FAT/argv launch path and
configure a 1MiB FLASH backup device. For DeSmuME this is save type 7. Calico's
`nitroromGetSelf()` reads the embedded NitroFS through `ntrcard`; libnds
`cardReadEeprom()`/`cardWriteEeprom()` and 64KiB sector erase operations own
the save sidecar.

The SPI image is split into five 192KiB slot extents. A 64-byte footer records
the payload length and CRC because PAL save files are variable-length; it is
written after the payload so an interrupted write is not accepted as a valid
slot. Only the first 960KiB of the 1MiB device is used. This route does not
change the ROM header into a commercial encrypted image and does not restore
the retired nds-bootstrap SDK-patch or custom ARM7 implementation.

Relevant upstream projects:

- [DSpico bootloader](https://github.com/LNH-team/dspico-bootloader)
- [Pico Launcher](https://github.com/LNH-team/pico-launcher)
- [Pico Loader](https://github.com/LNH-team/pico-loader)
- [DSpico DLDI](https://github.com/LNH-team/dspico-dldi)
- [DSpico firmware](https://github.com/LNH-team/dspico-firmware)

## Runtime architecture

- ARM9 runs the engine with devkitARM, libnds, `MEM_LEVEL2`,
  `PAL_NO_RUNTIME_HEAP`, and `PAL_NO_RUNTIME_DECOMPRESS`.
- The default Calico ARM7 service owns audio. There is no custom retail ARM7
  startup or backup protocol; the emulator fallback uses libnds's ARM9 SPI
  functions directly.
- The legacy 320x200 space remains a gameplay/resource coordinate contract.
  Maps and UI render at native 256x192 geometry without resizing a completed
  frame or writing presentation coordinates into gameplay state.
- `PAL_EXTREME_TWO_SCREENS` means two mutable native indexed engine canvases,
  not two physical DS displays.
- During boot the touch display is the libnds diagnostic console. Once a map
  is active it switches to a floor plan generated directly from the current
  native MAP chunk; there is no exploration or visited-cell state. MAP bottom
  tile index zero remains valid terrain (it is commonly ordinary grass), and
  only MAP bit `0x2000` removes a cell. Starting at the player's logical cell,
  a four-neighbor closure keeps only that connected walkable component.
  Every active touch-trigger zone whose current script performs a scene
  transition is included as reachable floor but is not expanded through: the
  engine runs that script before it can accept the player's following step.
  This closes doorways into otherwise connected zero-filled MAP space without
  inventing collision from GOP pixels. Dynamic event blockers are deliberately
  ignored while forming the closure, so opening a gate does not reveal a new
  fragment; permanent MAP collision and disconnected rooms or floors remain
  excluded.
  The renderer reverses PAL's isometric tile projection, crops the selected
  component, and keeps a fixed four-pixel pitch for every logical grid cell.
  Adjacent walkable cells merge into one blue region without internal lines;
  one-pixel white lines outline only its boundaries. Current event objects
  whose state has
  PAL's blocker semantics are overlaid as four-pixel gray cells. Active event
  points are green, while transition/relocation triggers (including stairs)
  are yellow. Ordinary three-frame walking characters without such a trigger
  are omitted, so moving NPCs neither clutter nor continuously change the
  floor plan. A script-operated barrier disappears as soon as its event state
  stops blocking. Small maps are centered in full; larger maps scroll only
  after the player reaches a viewport edge. A small red point marks the
  player's current position. Fatal errors switch back to the preserved
  console.
- The floor plan is generated when the MAP number changes or a same-MAP scene
  transition places the player in another disconnected component. A change to
  the active transition triggers also invalidates it. Ordinary movement inside
  the selected component never recomputes the closure. The fixed 128x128
  tile-map owner is temporarily reused for the transition-event list and the
  bounded flood-fill queue, while the pattern dictionary temporarily owns the
  terminal bitset; they then become the 16-bit GPU tile map and deduplicated
  pattern index respectively. Each 8x8 tile
  encodes a two-by-two group of four-pixel cells plus its boundary neighbors;
  equivalent patterns share one of 1,024 fixed tile slots. The sub 2D affine
  engine displays the map without rotation and applies the clamped viewport in
  hardware. Movement within the selected component causes no rerasterization
  or upload; it only changes affine reference registers. Gray blockers use the
  sub engine's 8x8
  hardware OBJs; palette banks distinguish gray blockers, yellow scene exits,
  and green event points without duplicating tile graphics. They update from
  the current scene event table without changing the MAP tile layer. The
  console remains intact in the same VRAM bank. While a scene transition is
  fading out or in, the lower map is hidden; the new component appears only
  after the new scene has completed its fade, avoiding mixed old-map/new-scene
  frames.
- The fixed Level2 scene, player, battle, fight, resident-pack, TOC, transient,
  screen, music, and audio owners retain their audited lifecycle rules. Do not
  add a general allocator, cache framework, or runtime decompression.

The pack stays in NitroFS instead of ARM9 `.rodata`. At boot, the provider
derives the fixed resident Level2 view while retaining bounded streaming
access to other chunks. DLDI launches read the selected `.nds` as a FAT file;
direct emulator launches read the same NitroFS extent from Slot-1.

## Threaded RIX/OPL2 music

The game thread decodes RIX at 70Hz and queues ordered OPL2 register writes. A
peer-priority Calico ARM9 worker renders OPL2 at 16.384kHz. Each synthesized
sample is copied directly to two consecutive samples in the fixed 32.768kHz
mono PCM output; there is no interpolation or filter. The game thread normally
waits for VBlank while the worker fills 256-frame output blocks.

The hot OPL2 code and fixed tables/state use audited ITCM/DTCM owners. The
supported melodic path remains bit-identical to the unmodified DBOPL
multiply-table backend in the host differential test. Rhythm-mode tracks are
rejected before decoder initialization because their percussion renderer is
outside the ARM9 budget; gameplay continues with that track silent.

The complete pinned pack peaks at 251 OPL writes in one RIX tick against a
capacity of 256. Run the workload profiler with:

```sh
make -C embedded nds-rix-profile
embedded/build/pal_nds_rix_profile esp32s3/TF_datapak/pal_full.pak
```

## Verification boundary

The current software checks cover distinct boundaries:

- `make -C nds check` proves the built image and fixed-memory contract.
- A DLDI patch dry run with the official DSpico v1.0.1 driver proves that the
  driver's 2KiB declared footprint fits and patches the ROM's 16KiB slot. Its
  header advertises read, write, Slot-1, and ARM7 capability, matching Calico's
  ARM7-resident DLDI target at `0x0380b000`.
- A source-level protocol audit matches that driver to DSpico firmware commit
  `472c9d8e9957`: the driver's `E3` request, `E4` poll, `E5` data, and
  `F6E10D98` write commands, 512-byte transfers, first/last flags, and final
  ready polls are the commands and state transitions implemented by the
  firmware. This proves an exact interface match, but not physical timing.
- DSpico bootloader commit `29671d041fe2` performs the required precondition:
  it sends the scrambled `FC` command, zeroes `REG_MCSCR0` through
  `REG_MCSCR2`, hands Slot-1 to Pico Loader, and boots
  `fat:/_picoboot.nds`. Pico Loader clears the active ROM transfer but does not
  assert a Slot-1 reset; the firmware resets its card mode only on the physical
  `PIN_RST` edge. Thus the standard DSpico DLDI commands remain available
  through both software chainloads. This is upstream launch-chain behavior;
  SDLPAL contains no DSpico-specific initialization.
- An end-to-end melonDS run uses the unmodified official Pico Launcher v1.3.0
  release and official Pico Loader v1.7.1 DSpico release. Ordinary launcher
  input selects this ROM; the loader chainloads it, supplies the DLDI
  `argv[0]`, and reaches both the title and natural main menu after the game
  mounts its self-ROM NitroFS and initializes DLDI saves. A standard count-4
  PAL save is then loaded, overwritten as count 5, followed by a fresh
  launcher/loader/emulator run that displays and loads count 5. Captures, the
  resulting ordinary `.rpg`, and exact release hashes are under
  `tmp_ui/nds/pico-launcher-v1.3.0-dspico-loader-v1.7.1-20260813/`.
  That card image contained neither `biosnds7.rom` nor an SDLPAL `.sav`
  sidecar: the decrypted marker bypassed the key path, and Pico Loader skipped
  its card-save arranger after classifying the image as homebrew. The final
  dual-backend ROM was then launched through the same Pico path and reached
  its ready screen reporting `DLDI self-ROM NitroFS` and `DLDI FAT`.
- A direct DeSmuME Slot-1 run with `--save-type 7` and no FAT device or launch
  argument reaches the ready screen through `nitroromGetSelf()`, loads a PAL
  SPI slot at count 6, saves it as count 7, restarts the emulator, displays
  count 7, and loads it back into gameplay. The final 1MiB DSV payload and its
  PAL footer both pass independent CRC checks. Captures, hashes, and the exact
  boundary of this emulator test are under
  `tmp_ui/nds/direct-slot1-spi-20260814/acceptance.md`.

The end-to-end emulator uses melonDS's live DLDI driver; it does not emulate
DSpico card-command timing. The separate official DSpico driver test proves
the final ROM's DLDI patch surface, not physical SD I/O. These are therefore
not a substitute for the remaining physical DSpico + DS/DS Lite
save/power-cycle test in `TODO.md`.

DeSmuME models the type-3 page reads and writes used by the direct route, but
the tested build logs its `D8` sector-erase command as unhandled. The
write/readback/restart result proves the emulator backend and sidecar layout;
it is not evidence for physical FLASH erase timing. The direct route is not a
physical-card target.

For visual gameplay work, the DeSmuME SDL/WebSocket harness remains the
repeatable NDS control surface. Captures must come from the real gameplay loop
and live under `tmp_ui/nds/`; emulator evidence does not replace physical
audio, controls, or storage acceptance.
