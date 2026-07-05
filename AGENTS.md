# AGENTS.md

## Current Porting Goal

This repository is being evaluated for an embedded/resource-limited SDLPAL port. When making code, tooling, or documentation changes, keep memory placement and asset loading in scope.

Primary consultation notes are in:

- `Consult/Q1.md`
- `Consult/RealData.md`
- `Consult/ContractAudit.md`
- `Consult/mkf_audit.py`
- `Consult/pal_data_audit.py`
- `tools/pal_pack_build.py`
- `tools/pal_pack_check.c`
- `tools/embedded_contract_check.py`

## Target Memory and Storage Constraints

- Fast SRAM: 300KB total. Treat this as scarce low-latency memory.
- PSRAM: 8MB total. Usable for larger mutable working sets, but much slower than SRAM.
- NOR flash: 16MB total. Read-only at runtime, random accessible.
- TF card: effectively unlimited capacity, but slow.

Prefer designs that explicitly choose where data lives: SRAM, PSRAM, NOR, or TF. Avoid adding hidden always-resident RAM use.

## PAL Data Path

The current local PAL data set is:

```text
/mnt/hgfs/deb13/PAL
```

Use this path for dataset audits and memory estimates unless the user gives a different path.

## Asset Strategy Notes

- Pre-decompression/offline asset conversion is mandatory for target runtime resources.
- Do not add runtime decompression to the embedded path. YJ1/YJ2/LZ4/etc. decode belongs in host-side pack-building tools only.
- Full pre-decompression of all assets into 16MB NOR is not feasible for the current data set, so large decoded/native resources should live in TF-backed resource packs.
- TF card can hold the original files and generated cache files, but runtime TF random access should be minimized. Prefer sequential reads of already-decoded/native chunks.
- Favor reproducible tools for dataset inspection and conversion.
- `tools/pal_pack_build.py` builds decoded/native `pal_nor.pak` and `pal_tf.pak` images. YJ1 decode is allowed there because it is host-side pack generation, not runtime.
- `tools/pal_pack_check.c` is a host-side mmap checker for generated packs using the same `embedded/pal_pack.c` reader.
- The audited data path has no loose `.ogg`, `.opus`, `.mp3`, `.wav`, `.mid`, or `.avi` files. Audio is in `MIDI.MKF`, `MUS.MKF`, and `VOC.MKF`.
- Scene/event sprite deduplication is high value: worst measured scene resources drop from about 909KB to about 143KB when repeated event-object sprite numbers share one decoded sprite.
- Text/font conversion should use the actual corpus. `WORD.DAT` + `M.MSG` decode cleanly as `cp950` and use 2,631 unique characters. The current `WOR16.ASC`/`WOR16.FON` data yields 2,600 unique 32-byte CJK glyphs in an 88,432-byte generated FONT chunk; ASCII remains a separate font path.

## Runtime Allocation Rules

- Target runtime code should not use `malloc`, `calloc`, `realloc`, or `free`.
- Use explicit static storage: `uint8_t` buffers for SRAM/PSRAM and `const uint8_t` or typed `const` views for read-only NOR/pack data.
- Do not add a memory pool or tier allocator. Prefer normal file-scope/static `uint8_t` arrays with clear names, fixed sizes, owners, and lifetimes.
- The native SDL build should remain a verification harness using the same fixed-memory API. SDL itself may allocate internally, but project engine/resource code should be checked for forbidden heap calls.
- Verify resource usage from build artifacts with `size`, `objdump -h`, `objdump -t`, `nm -S --size-sort`, and linker map files. Checks should cover `.text`, `.rodata`, `.data`, `.bss`, named SRAM/PSRAM buffer symbols, and absence of runtime decoder symbols.
- Use `python3 -B tools/embedded_contract_check.py --root .` as the repeatable source/binary contract audit. Add `--binary unix/sdlpal` after producing a native SDL build. Use `--max-symbol-prefix pal_sram_=307200 --max-symbol-prefix pal_psram_=8388608` to budget normal static buffers by scanning ELF symbols.
- `embedded/pal_pack.c` is the first native runtime slice following the contract: no heap, no decompressor, `const uint8_t` pack reads, fixed `uint8_t` copy destination, and objdump/nm/size verification through `embedded/Makefile`.
- `embedded/pal_memory.c` intentionally declares normal named static-storage buffers such as `pal_sram_framebuffer` and `pal_psram_map_tiles`; there is no memory pool API.
- `embedded/pal_native_sdl_smoke.c` is the native SDL fixed-memory smoke test. It wraps `pal_sram_framebuffer` with an SDL surface and reads from a `const uint8_t` pack; run it through `make -C embedded check` with `SDL_VIDEODRIVER=dummy`.
- `embedded/pal_realdata_sdl_smoke.c` is the native SDL real-data smoke test. It maps generated NOR/TF packs read-only, copies TF chunks into named SRAM/PSRAM arrays, reads real save files into static PSRAM storage, and is run with `make -C embedded realdata-check`.
- `embedded/pal_scene_cache.c` is the static scene-loading slice. It copies decoded MAP/GOP chunks into named PSRAM arrays and deduplicates event-object MGO sprites as `const uint8_t *` references into the NOR pack.
- `embedded/pal_battle_cache.c` is the static battle-loading slice. It copies decoded FBP backgrounds into PSRAM and keeps F/ABC/FIRE sprites as `const uint8_t *` views into the NOR pack, with enemy sprite deduplication.
- `embedded/pal_rng_cache.c` is the static RNG-frame slice. It reads predecoded RNG frame records from the TF pack and copies frames into `pal_psram_rng_frame_a` / `pal_psram_rng_frame_b`.
- `embedded/pal_sfx_cache.c` is the static sound-effect bank slice. It copies selected VOC chunks from the TF pack into `pal_psram_sfx_bank` and tracks spans with a small fixed `pal_sfx_` metadata table.
- `embedded/pal_global_cache.c` is the static global-data slice. It copies mutable default event/scene/object/player-role data into `pal_psram_save_state` and maps read-only scripts/DATA tables as `const uint8_t *` pack views.
- `embedded/pal_text_cache.c` is the static text slice. The pack builder converts `WORD.DAT` and `M.MSG` to UTF-16LE in the NOR pack; runtime maps it read-only with no text heap or codepage conversion.
- `embedded/pal_font_cache.c` is the static font slice. The pack builder converts `WOR16.ASC`/`WOR16.FON` into a read-only NOR glyph table with sorted UTF-16 codepoints and 32-byte glyph payloads; runtime maps it as `const uint8_t *` data with no `unicode_font` allocation.
- `embedded/pal_save_cache.c` is the static save-file slice. It reads real `.rpg` files into `pal_psram_save_state` and exposes fixed header fields without allocating a `SAVEDGAME_DOS`/`SAVEDGAME_WIN` object.
- `embedded/pal_video_static.c` is the static indexed-video slice. It uses `pal_sram_framebuffer`, `pal_psram_screen_bak`, and `pal_sram_display_dma` for clear/save/restore/scanline RGB565 conversion without `gpScreenReal` or texture-sized project buffers.

## Known Memory Pressure Points

- The full desktop path still has `fontglyph.h` with mutable `unicode_font[65536][32]`, about 2MB. The embedded font slice proves the replacement shape, but the full engine has not yet been wired to it.
- `resampler.c` has mutable float LUTs totaling about 147KB.
- Global game data currently allocates about 532KB from this data set.
- Desktop save/load currently allocates about 184-189KB per save operation for this data set; the embedded save slice uses the existing PSRAM save-state buffer instead.
- Worst measured normal scene resource residency is about 909KB before framebuffers, text/font, audio, and allocator overhead, but about 143KB for the same subset after event-sprite deduplication.
- Several `PAL_LARGE` local buffers are 64KB stack allocations on Unix-style builds.
- `PAL_MKFDecompressChunk()` allocates a compressed scratch buffer per decompression.
- The full desktop SDL video path still creates 32-bit surfaces/textures. The embedded video slice proves the replacement shape, but the full engine has not yet been wired to it.

These should not be assumed to fit in fast SRAM, and the target runtime should remove the heap/decompression paths rather than merely moving them to PSRAM.
