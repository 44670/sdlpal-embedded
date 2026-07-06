# AGENTS.md

# DO NOT send optional commentary!

Never send optional commentary.

```python
disable_send_optional_commentary = True
```

## Current Target: M5Stack CoreS3 SE port

- CoreS3 SE (not CoreS3)

- 16MB Flash(custom partition table needed)
- 8MB PSRAM
- No audio, for now. The CoreS3 SE target app should not link `pal_audio_static.c` / `pal_sfx_cache.c` or reserve the SFX bank until audio hardware playback is in scope; native/embedded contract harnesses may still exercise those slices.

References:
~/work/CardPuterADV/esp-walkie-talkie (CoreS3 SE port, copy/adapt its lcd/touch init code when useful)
/home/john/work/CoreS3SE (more refs.)

## Current Porting Goal

This repository is being evaluated for an embedded/resource-limited SDLPAL port. When making code, tooling, or documentation changes, keep memory placement and asset loading in scope.

The target is the full SDLPAL game/script loop on M5Stack CoreS3 SE, not CoreS3 and not a long-term fixed scene demo. Scene-only CoreS3 SE code is acceptable only as an intermediate bring-up step. TF resource-pack and save I/O should use ESP-IDF/FatFS paths on the target. The current NOR/TF pack split is acceptable, and runtime storage should remain normal named static buffers, not a pool.

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
- `tools/pal_pack_build.py` builds decoded/native `pal_nor.pak` and `pal_tf.pak` images plus a JSON manifest with source file SHA-256 hashes and decoded chunk sizes. YJ1 decode and VOC-to-PCM conversion are allowed there because they are host-side pack generation, not runtime. The default runtime packs contain the generated PCM SFX archive, not raw `VOC.MKF` chunks. `make -C embedded pack-build` regenerates the default packs from `PAL_DATA_DIR`.
- `tools/pal_pack_check.c` is a host-side mmap checker for generated packs using the same `embedded/pal_pack.c` reader.
- The audited data path has no loose `.ogg`, `.opus`, `.mp3`, `.wav`, `.mid`, or `.avi` files. Audio is in `MIDI.MKF`, `MUS.MKF`, and `VOC.MKF`.
- Scene/event sprite deduplication is high value: worst measured scene resources drop from about 909KB to about 143KB when repeated event-object sprite numbers share one decoded sprite.
- Text/font conversion should use the actual corpus. `WORD.DAT` + `M.MSG` decode cleanly as `cp950` and use 2,631 unique characters. The current `WOR16.ASC`/`WOR16.FON` data yields 2,600 unique 32-byte CJK glyphs in an 88,432-byte generated FONT chunk; ASCII remains a separate font path.

## Runtime Allocation Rules

- Target runtime code should not use `malloc`, `calloc`, `realloc`, or `free`.
- Use explicit static storage: `uint8_t` buffers for SRAM/PSRAM and `const uint8_t` or typed `const` views for read-only NOR/pack data.
- Do not add a memory pool or tier allocator. Prefer normal file-scope/static `uint8_t` arrays with clear names, fixed sizes, owners, and lifetimes.
- CoreS3 SE FatFS use is limited to short runtime filenames such as `0:/pal_tf.pak` and `0:/1.rpg`; keep FatFS LFN heap support and dynamic FatFS buffers disabled in `esp32s3/sdkconfig.defaults`.
- The native SDL build should remain a verification harness using the same fixed-memory API. SDL itself may allocate internally, but project engine/resource code should be checked for forbidden C/C++ heap calls.
- Verify resource usage from build artifacts with `size`, `objdump -h`, `objdump -t`, `objdump -d`, `nm -S --size-sort`, and linker map files. Checks should cover `.text`, `.rodata`, `.data`, `.bss`, named SRAM/PSRAM buffer symbols, absence of runtime decoder symbols, absence of `PAL_LARGE` scratch buffers in active contract sources, `uint8_t` declarations for named SRAM/PSRAM buffers, no active loose original PAL data filename references, and absence of call sites to heap/decompress trap targets.
- Use `python3 -B tools/embedded_contract_check.py --root .` as the repeatable source/binary/pack contract audit. Add `--binary unix/sdlpal` after producing a native SDL build. Use `--max-symbol-prefix pal_sram_=307200 --max-symbol-prefix pal_psram_=8388608` to budget normal static buffers by scanning ELF symbols, and `--pack`/`--max-pack-size` to reject flagged/YJ1 payloads and NOR overflow.
- `embedded/pal_pack.c` is the first native runtime slice following the contract: no heap, no decompressor, `const uint8_t` pack reads, TF-pack TOC copy into fixed PSRAM through image or read-at APIs, fixed `uint8_t` copy destination, and objdump/nm/size verification through `embedded/Makefile`.
- `embedded/pal_noheap.c` provides link-time `--wrap` traps for `malloc`, `calloc`, `realloc`, and `free` in embedded smoke artifacts. SDL shared-library internals may allocate, but project object calls are redirected to the trap.
- `embedded/pal_memory.c` intentionally declares normal named static-storage buffers such as `pal_sram_framebuffer` and `pal_psram_map_tiles`; there is no memory pool API.
- `embedded/pal_native_sdl_smoke.c` is the native SDL fixed-memory smoke test. It wraps `pal_sram_framebuffer` with an SDL surface and reads from a `const uint8_t` pack; run it through `make -C embedded check` with `SDL_VIDEODRIVER=dummy`.
- `embedded/pal_realdata_sdl_smoke.c` is the native SDL real-data smoke test. It maps generated NOR/TF packs read-only, copies the TF pack TOC into `pal_psram_tf_toc`, also verifies file-backed read-at TF payload reads, copies TF chunks into named SRAM/PSRAM arrays, reads real save files into static PSRAM storage, and is run with `make -C embedded realdata-check`, which rebuilds the packs first.
- `make -C embedded contract-check` runs the embedded source/binary/pack budget gate with heap/decoder/`PAL_LARGE` scratch/typed SRAM-PSRAM storage/loose-resource scans, generated-pack checks, manifest source-hash/decoded-size checks, 16MB NOR pack budget, `.rodata`/`.bss` section budgets, and named `pal_sram_`, `pal_psram_`, `pal_scene_`, `pal_battle_`, `pal_sfx_`, `pal_audio_`, `pal_global_`, `pal_save_`, `pal_video_`, `pal_ui_`, `pal_music_`, `pal_menu_`, `pal_ending_`, `pal_palette_`, and `pal_dialog_` symbol totals.
- `make -C unix EMBEDDED_CONTRACT=1 contract-check` builds and objdump/nm-checks the reduced native SDL2 full-engine profile named `unix/sdlpal-embedded-contract`. It excludes MP3/OGG/OPUS/AVI/Timidity/TSF/GLSL/native-MIDI launcher paths plus desktop sound/RIX/resampler/font/text/codepage paths and also excludes `yj1.c`, so runtime decompressor code is not compiled into the contract profile. It links the same `--wrap=malloc/calloc/realloc/free` traps used by embedded smoke artifacts, and defines `PAL_NO_RUNTIME_HEAP` / `PAL_NO_RUNTIME_DECOMPRESS` so linked heap/decompress symbols are replaced by unavailable traps. The Unix contract replacements for desktop text/font/music/SFX map generated NOR/TF packs read-only, use `const uint8_t` TEXT/FONT/MUS views, synthesize MUS/RIX through fixed-storage OPL2 without the desktop resampler, convert returned UTF-16LE strings through an eight-slot `pal_sram_contract_text_slots` ring, copy each requested SFX PCM chunk into the 212KB `pal_psram_contract_sfx` buffer, and mix from that PSRAM buffer without heap allocation. The contract checker also disassembles the ELF and fails if a project object still calls heap/decompress trap targets or C++ `operator new`/`operator delete`, rebuilds/verifies the generated NOR/TF packs, and checks the manifest/source hashes. The Unix contract source scan runs with `--fail-on-source` over the exact `$(CFILES) $(CPPFILES)` linked by the contract profile, strips simple inactive preprocessor blocks for contract-only defines, and excludes nonlinked native-MIDI sources, so active reduced-profile source heap/new/delete/decompress/`PAL_LARGE` scratch/typed SRAM-PSRAM storage/loose-resource hits must stay at zero.
- Under the Unix contract replacement music path, `unix/contract_music.cpp` maps MUS tracks as `const uint8_t *` NOR views, feeds them to a heap-disabled `CrixPlayer::load_buffer()` path, and uses a small named SRAM tick buffer (`pal_sram_contract_rix_tick`) with a direct-rate DOSBox OPL2 core from `unix/contract_opl2.cpp`. The target profile does not link the desktop RIX file loader, surround/OPL3 wrappers, high-quality resampler, or C++ allocation operators.
- Under the same contract defines, SDL2 native verification still uses SDL surface wrappers, but their pixels are normal named static buffers: `pal_sram_video_screen` for the 320x200 indexed framebuffer, `pal_psram_video_screen_bak` for the backup screen, and `pal_psram_video_screen_real` for the SDL2 32-bit presentation surface. SDL may allocate small wrapper objects internally; project pixel storage is statically declared and visible to `nm`.
- Under the same contract defines, `palcommon.c` exposes generated-pack archive handles through `PAL_MKFOpenPackArchive()`, `PAL_MKFReadChunk()`, `PAL_MKFGetChunkSize()`, `PAL_MKFGetChunkCount()`, and `PAL_MKFMapChunk()`. Native contract verification maps the generated NOR/TF packs read-only, and existing full-engine call sites now read native FBP/MGO/BALL/DATA/F/FIRE/RGM/SSS/MAP/GOP/ABC/RNG chunks from those packs instead of opening the original MKF files.
- Under the same contract defines, `audio.c` uses a named 4KB SRAM mix buffer (`pal_sram_audio_mix_static`) instead of allocating `gAudioDevice.pSoundBuffer`; larger SDL callback requests are mixed in fixed-size chunks.
- Under the Unix contract replacement sound path, SFX chunks are validated as host-converted PCM16 and copied from the generated TF pack into `pal_psram_contract_sfx` before mixing. The current real data's largest SFX payload is 211,152 bytes, so the 212KB static PSRAM buffer covers single-effect playback without reading directly from TF during the audio callback.
- Under the same contract defines, `global.c` uses named `uint8_t` PSRAM buffers for mutable event objects, mutable magic data, and save/load structs. Read-only script entries, stores, enemies, enemy teams, battlefields, and level-up magic tables are mapped from the native NOR pack. It assumes the DOS/YJ1 data set and avoids heap codepage/version probes.
- Under the same contract defines, `audio.c`, `global.c`, and `util.c` no longer keep active loose original data filename references such as `mus.mkf`, `word.dat`, or the legacy resource-file checklist. Runtime resource access goes through generated packs, except save files.
- Under the same contract defines, `palcfg.c` uses default/static config strings without parsing config files into heap strings, and `util.c` uses case-sensitive no-heap path lookup instead of `strdup()`/`scandir()`.
- Under the same contract defines, `PAL_SetConfigItem()` copies string config values into fixed static `uint8_t` config buffers instead of using `free()`/`strdup()`. The audited data set has no `DESC.DAT`, so `ui.c` object-description load/free intentionally stays a no-heap `NULL` path; future description-bearing data should use generated read-only text/object data.
- `embedded/pal_scene_cache.c` is the static scene-loading slice. It copies decoded MAP/GOP chunks into named PSRAM arrays from mapped-pack or file-backed TF read-at access, deduplicates event-object MGO sprites as `const uint8_t *` references into the NOR pack, and can pin one scene's unique MGO sprites into `pal_psram_sprite_pin` for TF-backed sprite placement.
- The CoreS3 SE bring-up enables current-scene MGO pinning: unique event sprites are copied from the read-only NOR pack into the normal 1MB static `pal_psram_sprite_pin` buffer when a scene loads, reducing per-frame NOR traffic while keeping the no-heap/no-runtime-decompress contract.
- Under `PAL_NO_RUNTIME_HEAP` / `PAL_NO_RUNTIME_DECOMPRESS`, the full-engine `map.c` loader uses file-scope static `uint8_t` PSRAM buffers (`pal_psram_map_instance`, `pal_psram_map_gop_static`) and only accepts already-native 64KB map chunks. It does not allocate a `PALMAP`, allocate GOP storage, or call the runtime decoder.
- Under the same contract defines, `main.c` splash loading uses `pal_psram_splash_fbp` for FBP staging and maps both title/crane sprites as read-only `const uint8_t` NOR views. Title reveal uses a clipped RLE blit instead of mutating the sprite header. It only accepts already-native splash FBP/MGO chunks.
- Under the same contract defines, `uigame.c` FBP menu backgrounds and menu box scratch use shared static `uint8_t` PSRAM buffers (`pal_psram_uigame_background`, `pal_psram_uigame_box`). Its saved menu boxes use named per-call-site PSRAM buffers for cash/system/selection boxes. RGM/BALL menu images are mapped as read-only `const uint8_t` NOR views, and FBP/RGM/BALL chunks must already be native.
- Under the same contract defines, `battle.c` maps F/ABC battle sprites and `DATA.MKF #10` effect sprites as `const uint8_t` views into the native NOR pack. Battle backgrounds still use the fixed PSRAM buffer `pal_psram_battle_background_static`, with F/ABC/DATA/FBP chunks required to already be native.
- Under the same contract defines, `fight.c` maps temporary FIRE effect sprites and F.MKF summon sprites as `const uint8_t` views into the native NOR pack and requires native F/FIRE chunks.
- Under the same contract defines, `ending.c` uses `pal_psram_ending_fbp_static` for FBP screen staging and maps MGO ending/effect sprites as read-only `const uint8_t` NOR views. FBP/MGO chunks must already be native.
- Under the same contract defines, `video.c` omits the YJ1-compressed touch-overlay BMP path; target overlay artwork must be preconverted rather than decoded during `VIDEO_Startup()`.
- Under the same contract defines, `global.c` skips heap-loaded object descriptions. This matches `/mnt/hgfs/deb13/PAL`, which has no `DESC.DAT`; descriptions in future packs need to come from generated read-only text/object data.
- Under the same contract defines, `res.c` uses static `uint8_t` PSRAM buffers for the resource manager and event-sprite pointer table. Event and player MGO sprites are `const uint8_t` views into the native NOR pack, and duplicate event sprite references share the same view. It requires native MGO chunks.
- Under the same contract defines, `rngplay.c` reads host-predecoded native RNG frame records into `pal_psram_rng_frame_static` and blits them directly without heap or `Decompress()`.
- Under the same contract defines, `ui.c` maps `DATA.MKF #9` UI sprite data as a read-only `const uint8_t` NOR pack view. Saved UI boxes use caller-owned named `uint8_t` PSRAM buffers declared at the specific menu call sites in `uigame.c` rather than a box pool or duplicate-surface allocation. `PAL_InitUI()` and `PAL_CreateBoxInternal()` do not allocate project heap.
- Under the same contract defines, `palette.c` maps `PAT.MKF` palette chunks from the generated NOR pack and uses three named SRAM `uint8_t` buffers for loaded/current/work palettes instead of opening the original `pat.mkf` or using `PAL_LARGE` palette arrays on the stack.
- `embedded/pal_battle_cache.c` is the static battle-loading slice. It copies decoded FBP backgrounds into PSRAM from mapped-pack or file-backed TF read-at access, keeps F/ABC/FIRE sprites plus `DATA.MKF #10` battle effects as `const uint8_t *` views into the NOR pack, can stage one FIRE effect in `pal_psram_effect`, and deduplicates enemy sprite references.
- `embedded/pal_rng_cache.c` is the static RNG-frame slice. It reads predecoded RNG frame records from the TF pack, supports both mapped-pack and file-backed read-at access, stages TF movie frame tables in `pal_psram_tf_readahead`, and copies selected frames into `pal_psram_rng_frame_a` / `pal_psram_rng_frame_b`.
- `embedded/pal_sfx_cache.c` is the static sound-effect bank slice. It copies selected preconverted SFX PCM16 chunks from mapped-pack or file-backed TF read-at access into `pal_psram_sfx_bank` and tracks spans with a small fixed `pal_sfx_` metadata table.
- `embedded/pal_audio_static.c` is the static audio mix slice. It validates host-converted 22050Hz mono PCM16 SFX payloads and mixes them into `pal_sram_audio` without runtime resampling or heap allocation.
- `embedded/pal_global_cache.c` is the static global-data slice. It copies mutable default event/scene/object/player-role data into the 256KB `pal_psram_save_state` buffer and maps read-only scripts/DATA tables as `const uint8_t *` pack views.
- `embedded/pal_script_static.c` is the static script-view slice. It maps script entries through `PalGlobalCache` as read-only `const uint8_t *` data and provides bounded entry reads/traces only; it does not execute scripts yet.
- The CoreS3 SE bring-up now runs a small bounded auto-script subset directly against mutable event-object records each animation tick. Supported operations are basic stop/jump/wait plus visual/event-pointer updates used by the scene scaffold; unsupported script operations deliberately leave the script pointer unchanged until the full interpreter is ported.
- `embedded/pal_text_cache.c` is the static text slice. The pack builder converts `WORD.DAT` and `M.MSG` to UTF-16LE in the NOR pack; runtime maps it read-only with no text heap or codepage conversion.
- `embedded/pal_font_cache.c` is the static font slice. The pack builder converts `WOR16.ASC`/`WOR16.FON` into a read-only NOR glyph table with sorted UTF-16 codepoints and 32-byte glyph payloads; runtime maps it as `const uint8_t *` data with no `unicode_font` allocation.
- `embedded/pal_save_cache.c` is the static save-file slice. It reads real `.rpg` files into the 256KB `pal_psram_save_state` buffer and exposes fixed header fields without allocating a `SAVEDGAME_DOS`/`SAVEDGAME_WIN` object; `/mnt/hgfs/deb13/PAL` save files are currently under 189KB.
- The CoreS3 SE bring-up reads startup saves from FatFS into `pal_psram_save_state` and writes the same fixed buffer back to the loaded short-name slot on scene changes. This is only a save I/O proof for the scene scaffold, not the final menu-driven save system.
- `embedded/pal_video_static.c` is the static indexed-video slice. It uses `pal_sram_framebuffer`, `pal_sram_big_buffer`, `pal_psram_screen_bak`, and `pal_sram_display_dma` for clear/full-screen save/battle-scene save/rectangular save/restore/scanline RGB565 conversion without `gpScreenReal`, `VIDEO_DuplicateSurface`, or texture-sized project buffers.
- `embedded/pal_ui_cache.c` is the static UI asset slice. It maps DATA UI sprites/effects, BALL item bitmaps, and RGM face bitmaps as read-only `const uint8_t *` pack views, and copies PAT palettes into `pal_sram_misc`.
- `embedded/pal_music_cache.c` is the static music-data slice. It maps MIDI and MUS/RIX tracks from the NOR pack as read-only `const uint8_t *` views and does not allocate player state.
- `embedded/pal_menu_static.c` is the static menu/status scratch slice. It copies decoded FBP menu backgrounds from mapped-pack or file-backed TF read-at access into `pal_psram_menu_background`, copies legacy mutable image scratch into `pal_psram_menu_image`, uses `pal_psram_menu_box` for fixed menu boxes, and maps item images as `const uint8_t *` when copying is unnecessary.
- `embedded/pal_ending_static.c` is the static ending/splash slice. It copies FBP screens from mapped-pack or file-backed TF read-at access into `pal_psram_ending_fbp_a` / `pal_psram_ending_fbp_b` and maps MGO ending/effect sprites as `const uint8_t *` NOR views.
- `embedded/pal_palette_static.c` is the static palette/fade slice. It uses `pal_sram_palette_current` and `pal_sram_palette_work` for RGB palette copies and fade steps instead of local `SDL_Color[256]` scratch arrays.
- `embedded/pal_dialog_static.c` is the static dialog-asset slice. It maps `DATA.MKF #12` dialog icons and `RGM.MKF` faces as `const uint8_t *` NOR views, replacing the embedded shape of `TEXTLIB.bufDialogIcons[282]` and `PAL_RLEBUFSIZE` face scratch reads.

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
