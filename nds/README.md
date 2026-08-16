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
  transition is removed before that closure: the engine transfers the party
  automatically inside the zone, so it is not stable walkable floor.
  Stationary, active blockers with no trigger are also removed. They are part
  of the engine's real movement collision and can seal an otherwise open path
  from a street into the unused outer MAP lattice. Moving characters and
  interactive blockers remain traversable while forming the complete floor
  plan, so NPC motion does not regenerate it and opening a gate does not reveal
  a new fragment. This uses movement semantics rather than GOP pixels.
  The renderer reverses PAL's isometric tile projection, crops the selected
  component, and keeps a fixed four-pixel pitch for every logical grid cell.
  Adjacent walkable cells merge into one blue region without internal lines;
  one-pixel white lines outline only its boundaries. Event overlays are a
  separate dynamic layer: PAL blockers are gray, active event points are
  green, and transition/relocation triggers (including stairs) are yellow.
  Gray blockers and green event points use their event object's original world
  coordinate; they are not snapped to, clipped by, or required to be adjacent
  to the selected closure. This keeps invisible counter and wall search
  triggers at their authored positions. A yellow automatic touch transition
  represents a trigger zone rather than a point: if its authored center lies
  beyond the floor-plan crop, its marker uses an impassable trigger-zone cell
  adjacent to the selected component. Search-activated yellow transitions
  retain their authored coordinate. Ordinary three-frame walking characters
  without an interaction trigger are omitted to avoid clutter. A
  script-operated barrier disappears as soon as its event state stops
  blocking. Small maps are centered in full; larger maps scroll only after the
  player reaches a viewport edge. A small red point marks the player's current
  position. Fatal errors switch back to the preserved console.
- The floor plan is generated when the MAP number changes or a same-MAP scene
  transition places the player in another disconnected component. At scene
  entry, fixed arrays snapshot automatic exits and stationary structural
  seals for topology construction. Later event position, state, and color
  changes never invalidate that topology. Retaining an old structural seal is
  deliberately fail-closed if an event unexpectedly moves; it cannot reopen a
  route into unused MAP storage. Ordinary movement inside the selected
  component never recomputes the closure. The fixed 128x128 tile-map owner is
  temporarily reused only as the bounded flood-fill queue, while the pattern
  dictionary temporarily owns the closure-blocked bitset; they then become
  the 16-bit GPU tile map and deduplicated pattern index respectively. Each
  8x8 tile
  encodes a two-by-two group of four-pixel cells plus its boundary neighbors;
  equivalent patterns share one of 1,024 fixed tile slots. The sub 2D affine
  engine displays the map without rotation and applies the clamped viewport in
  hardware. Movement within the selected component causes no rerasterization
  or tile-map upload. The affine identity matrix is installed once; at each
  display commit, the live BG3 reference point and BG1 red-marker scroll are
  written together during a verified VBlank. The 48KiB main-screen copy goes
  to its hidden bitmap page before that wait, so it cannot delay the lower
  screen's register commit into active scanout. A second lower-screen tile-map
  buffer is neither used nor needed for movement because those VRAM contents
  are static. Gray blockers use the sub engine's 8x8
  hardware OBJs; palette banks distinguish gray blockers, yellow scene exits,
  and green event points without duplicating tile graphics. A fixed 160-entry
  descriptor array treats every displayed event as dynamic and updates its
  projection without changing the MAP tile layer. Green and gray points remain
  at their live authored coordinates; yellow touch zones use the static
  selected component only to choose their visible impassable boundary cell.
  Trigger-script classification is repeated only when that event's script
  entry changes. The console remains intact in the same VRAM bank. While a
  scene transition is
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

## Threaded RIX/OPL2 music and one SFX voice

The game thread decodes RIX at 70Hz and queues ordered OPL2 register writes. A
Calico ARM9 worker one priority level above the main thread renders and outputs
mono PCM16 directly at the logical 16.384kHz rate in fixed 256-frame blocks.
Calico does not time-slice equal-priority threads, so this single priority step
is required for the sleeping audio worker to preempt long scene/resource
loads. The fractional 70Hz interval alternates between 234 and 235 samples as
required; there is no second 32.768kHz output stage. The NDS sound timer is an
integer divider, so its physical rate is about 16,380.25Hz.

`VOC.MKF` is converted on the host to raw signed mono PCM8 at exactly 8.192kHz.
Only one effect can play at a time. Its source sample is copied to two adjacent
16.384kHz output frames and saturating-mixed with music. The single fixed
40,960-byte owner holds five seconds and lives in ARM9 main SRAM even though
the port uses the `MEM_LEVEL2` resource profile. Loading a new effect stops the
old voice. If its sound ID matches the slot's `last_loaded_sound_id`, playback
restarts from the existing PCM8 data without another ROM/TF read. A different
effect invalidates and overwrites that same slot with one synchronous pack
read, so storage access never races the audio worker and does not require a
second slot. Stopping playback retains the loaded ID for later reuse; closing
the audio device resets it to `-1`.

The hot OPL2 code and fixed tables/state use audited ITCM/DTCM owners. The
supported melodic path remains bit-identical to the unmodified DBOPL
multiply-table backend in the host differential test. RIX rhythm-mode tracks
are accepted, but the target masks OPL2 percussion register `0xBD` bit 5. Their
six melodic channels therefore remain audible while bass drum, snare, tom,
cymbal, and hi-hat are omitted. The stock DBOPL percussion loop cannot meet
the ARM9 PCM deadline even at the reduced synthesis rate, so enabling it is
not part of this profile.

The ARM7 looping PCM channel uses a fixed four-block ring. At 256 frames per
block, the ring adds at most about 62.5ms after an effect has been loaded,
rather than the half second a 32-block ring would add at the current rate.
Synchronous pack read time is separate. The 32-entry RIX command queue remains
unchanged so scene/resource loads retain their producer lookahead.

A forced Track 18 test on the preceding 128-synthesis-sample transport measured
the stock percussion loop at 9.85ms average and 16.05ms peak against a 7.81ms
deadline; it accumulated deadline misses and starved the UI. Keeping
percussion masked measured 1.13ms average and 1.45ms peak, with nonzero audio
and zero deadline misses, queue underruns, or overruns. The current direct
256-frame transport was then measured through a direct SPI-save load, scene
construction, and scripted sound 78. With the current four-block ring, 1,728
render blocks averaged 2.27ms and peaked at 3.60ms against the physical
15.63ms block deadline, with zero PCM deadline misses and zero OPL queue
underruns or overruns. A preceding same-renderer profile measured full SFX
blocks at 3.02ms versus 2.75ms for adjacent music-only blocks, so the one-voice
mix added about 0.27ms in that run. The positioned save is an integration
shortcut: ordinary input ran scene 4 event 108 and its real script, but this is
not evidence of a natural story route to that event. Current four-block-ring
artifacts are under `tmp_ui/nds/audio-priority-sfx-ring4-20260816/`; the
preceding mix-cost profile is under
`tmp_ui/nds/audio-priority-sfx-profile-20260816/`. Physical listening remains
acceptance work. The older forced-track artifacts are under
`tmp_ui/nds/rhythm-enable-16384-track18-20260815-smoke/` and
`tmp_ui/nds/rhythm-load-melodic-only-track18-20260815/`.

The pack-wide decoder replay covers all 26 nonempty rhythm tracks. Every one
has melodic key-on events on channels 0 through 5 (at least 76 per track), no
key-on events on percussion-reserved channels 6 through 8, and no more than
six simultaneously held channels. Track 3 is also the worst register burst in
the complete pack at 251 OPL writes in one RIX tick against a capacity of 256.
A forced Track 3 run over 900 DeSmuME frames repeatedly exercised its complete
3.17-second stream: rendering measured 1.19ms average and 1.61ms peak, with
zero deadline misses, underruns, or overruns and 160 target presents. Its
capture is under
`tmp_ui/nds/rhythm-melodic-only-track3-worstwrites-20260815/`.

Run the workload profiler with:

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
