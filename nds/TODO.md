# Nintendo DS port TODO

Near-term work items for this target. Each entry lists the current state and
what "done" means. Keep this file short; delete entries as they land.

## 1. Launch-volume FAT saves

State: implemented in `source/nds_save.c` (five fixed slots at
`<launch>:/sdlpal/N.sav`, tmp-write + read-back verify + rename commit). The
retired Slot-1 EEPROM backend could not persist on the accepted TWiLight
Menu boot path — nds-bootstrap only patches `cardEeprom*` for retail ROMs,
never for homebrew.

Remaining:

- Real-hardware acceptance: boot the TWL-header build without a forced-NTR
  per-game ini, confirm the console prints `mode: TWL (DSi)`,
  `storage: TWL SD ok`, and `save: launch fat ok`, save in-game, and check that
  `/sdlpal/1.sav` appears on the SD card and reloads after power-cycle.
- DeSmuME has no writable launch volume, so save/reload acceptance is hardware-only;
  the emulator path intentionally reports saves unavailable and continues.

## 2. Accept threaded full-rate RIX music

State: implemented in software and emulator-tested for melodic RIX tracks. The
game thread queues the original 70Hz RIX/OPL writes; a peer-priority Calico ARM9
worker directly renders every nominal 32.768kHz PCM frame with the standard
DBOPL multiply-table backend. There is no reduced internal rate, interpolation,
or resampling. RIX rhythm mode is outside this target's CPU budget, so a track
whose format byte 2 enables rhythm is rejected before it can queue OPL writes;
gameplay continues with that track silent. The OPL2-only core precomputes exact
envelopes, keeps its hot code and tables in ITCM/DTCM, bypasses envelope-buffer
work when both FM envelopes are provably constant, and emits fixed 256-frame
PCM blocks. A host differential gate compared 4,690,350 supported melodic
samples bit-for-bit with the unmodified DBOPL backend.

The 6,000-frame target profile under
`tmp_ui/nds/dbopl2-optimized-profile-6000-fused-20260808/` completed with zero queue
underruns, queue overruns, and audio deadline misses. Its 12,479 PCM blocks
averaged 1,232 Calico ticks (2.35ms) for synthesis, final gain, clipping, and
PCM output with a 1,871-tick (3.57ms) maximum inside a 7.81ms hardware block
interval. `make -C nds check` passes, including the differential audio test
and fixed DTCM user-stack margin. A forced rhythm-track rejection run under
`tmp_ui/nds/dbopl2-rhythm-reject-track18-2000-20260808/` completed 2,000 frames
with zero queue failures or audio deadline misses and without entering the
expensive percussion renderer.

Remaining:

- Listen on DS/DS Lite and adjust only the final channel level if necessary.
  Emulator timing and waveform checks do not replace physical speaker/headphone
  acceptance.
