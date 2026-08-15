# Nintendo DS port TODO

Keep this file limited to remaining acceptance work; remove an item when its
evidence is complete.

## 1. Accept Pico Launcher on DSpico on physical NTR hardware

State: the ROM is now NTR-only homebrew with one standard 16KiB DLDI patch
slot. Pico Loader's homebrew classifier accepts its zero autoload hooks,
supplies the selected ROM as a `fat:/` argument, and patches the active DLDI
driver. The runtime mounts DLDI FAT, opens that exact ROM, mounts its NitroFS,
and stores ordinary PAL saves under `fat:/sdlpal/`.

The build/check gate passes. Official DSpico bootloader commit `29671d041fe2`
sends the required `FC` command, zeroes the card-command scrambler seeds, and
boots `fat:/_picoboot.nds`; the following software chainloads do not assert the
physical Slot-1 reset that would leave this mode. The official DSpico v1.0.1
2KiB DLDI driver patches the image successfully in a host dry run. Its
read/write command stream matches the corresponding DSpico firmware handlers
exactly, including sector numbering, 512-byte payloads, sequential-write flags,
and ready polling. An end-to-end run with the official Pico Launcher v1.3.0
and official Pico Loader v1.7.1 DSpico release selects the ROM through the
launcher and reaches its natural main menu. The same chain loads stock save
count 4, writes count 5 through DLDI FAT, then a fresh emulator/DLDI-image run
displays and loads count 5. No `biosnds7.rom` or SDLPAL `.sav` sidecar was
present. The resulting ordinary `.rpg`, exact hashes, captures, and
emulator-versus-hardware boundary are under
`tmp_ui/nds/pico-launcher-v1.3.0-dspico-loader-v1.7.1-20260813/`.

Remaining:

- Put those same release files and the unpatched `sdlpal.nds` on a DSpico SD
  card and launch it on a DS/DS Lite.
- Verify physical DSpico reads beyond 32MiB reach the title and New Game path.
- Save to `fat:/sdlpal/N.rpg`, power-cycle, and reload the same slot.
- Retain the exact release versions and physical observations as acceptance
  evidence. Do not add DSpico-private I/O or route this physical DLDI test
  through the emulator-only Slot-1/SPI fallback.

## 2. Accept threaded RIX music

State: the software and emulator gates pass for melodic RIX tracks. The game
thread queues 70Hz OPL writes and a peer-priority ARM9 worker renders OPL2 at
16.384kHz, copying every synthesized sample twice into the 32.768kHz PCM
stream. The 16.384kHz synthesis output is checked bit-for-bit against the
unmodified DBOPL backend. Rhythm-mode tracks now retain their six melodic
channels instead of being rejected wholesale. Their five OPL2 percussion
voices remain deliberately omitted because the stock DBOPL percussion path
misses the PCM deadline and starves the UI even at 16.384kHz. The pack-wide
gate replays all 26 rhythm tracks and proves each retains melodic notes without
keying the three percussion-reserved OPL channels; the worst 251-write tick is
also covered by a forced Track 3 emulator performance run.

Remaining:

- Listen on DS/DS Lite and adjust only the final channel level if necessary.
  Emulator waveform/timing checks do not replace physical speaker or
  headphone acceptance.

## 3. Accept the touch-screen full map

State: when a MAP chunk becomes active, ARM9 converts its nonblocking
isometric tile records into an orthogonal walkable topology (including valid
bottom-tile index zero terrain), then keeps only
the four-neighbor closure containing the player. Active automatic transition
zones are treated as impassable because entering one necessarily runs its
scene-transfer script; their yellow boundary markers remain visible.
Stationary active blockers with no trigger also participate in the closure,
matching real movement collision and preventing Yangzhou scene 82 from
spilling into the unused outer MAP lattice. Moving characters and interactive
barriers are still ignored, so their motion does not regenerate the map.
Ordinary movement does not recompute it. The selected result is cropped
into a fixed 1024x1024 tiled address space at exactly four pixels per logical
grid cell. Adjacent walkable cells share uninterrupted blue fill; one-pixel
white lines appear only at region boundaries. One VBlank DMA uploads the
16-bit tile map and its deduplicated tile patterns. The sub 2D affine engine
centers small maps and scrolls larger maps without rotation, while a small red
point shows the current position. There is no exploration state. Ordinary
movement now commits the live affine reference and red-marker scroll together
inside a verified VBlank; the hidden main-screen bitmap DMA happens before the
wait. This targets the reported one-frame, physical-hardware-only displacement
without adding an unnecessary second tile-map buffer.

Remaining:

- Inspect the lower screen through real gameplay and confirm that native MAP
  topology produces useful connected floor plans on representative indoor and
  outdoor scenes, including a same-MAP floor transition and a large town map.
- Confirm the orthogonal four-pixel pitch, merged blue walkable regions, white
  boundaries, gray non-character blockers, yellow stairs/exits, green event
  points, omitted walking characters, and the small red point on a physical
  DS/DS Lite LCD.
- Repeatedly walk while a large map follows the player and confirm that neither
  the floor plan nor red point shows the previously reported transient
  one-frame displacement. Emulator frame captures cannot close this
  hardware-timing acceptance item.
