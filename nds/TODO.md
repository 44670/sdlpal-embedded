# Nintendo DS port TODO

Near-term work items for this target. Each entry lists the current state and
what "done" means. Keep this file short; delete entries as they land.

## 1. Validate the retail-loader contract on physical NTR hardware

State: the software route is complete. The NTR-only image has no TWL, DLDI,
self-ROM lookup, launch-volume FAT path, or device-specific flashcart code. Its
minimal ARM9 entry veneer matches the retail classifier in official TWiLight
Menu++ v27.24.1, which records `HOMEBREW_BOOTSTRAP = 0` and selects the retail
nds-bootstrap v2.16.0 binary. The generated bundle supplies the required
erased 1MiB sidecar because an NTR header does not encode backup capacity and
TWiLight's unknown-game fallback is only 512KiB. Official nds-bootstrap boots
the ROM, reads the embedded pack, loads save count 4, writes count 5 to that
redirected sidecar with valid CRCs, then relaunches and loads count 5 through
natural menus.

Remaining:

- Run a retail-compatible loader on a DS/DS Lite, then verify the same read
  beyond 32MiB and save/reload after a power cycle. Compatibility belongs to
  the loader; do not add its private storage protocol to this ROM.

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
