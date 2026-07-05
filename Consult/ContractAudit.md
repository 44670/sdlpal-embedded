# Embedded Runtime Contract Audit

This records the current state against the target contract:

- no project-side runtime heap allocation,
- explicit fixed `uint8_t` SRAM/PSRAM buffers,
- `const uint8_t` or typed `const` read-only resource data,
- native SDL verification build,
- resource usage checked through build artifacts,
- no runtime decompression code.

## Tool

Added:

```text
tools/embedded_contract_check.py
tools/pal_pack_build.py
tools/pal_pack_check.c
embedded/pal_pack.c
embedded/pal_pack.h
embedded/pal_pack_smoke.c
embedded/pal_noheap.c
embedded/pal_memory.c
embedded/pal_memory.h
embedded/pal_memory_smoke.c
embedded/pal_native_sdl_smoke.c
embedded/pal_realdata_sdl_smoke.c
embedded/pal_scene_cache.c
embedded/pal_scene_cache.h
embedded/pal_battle_cache.c
embedded/pal_battle_cache.h
embedded/pal_rng_cache.c
embedded/pal_rng_cache.h
embedded/pal_sfx_cache.c
embedded/pal_sfx_cache.h
embedded/pal_audio_static.c
embedded/pal_audio_static.h
embedded/pal_global_cache.c
embedded/pal_global_cache.h
embedded/pal_text_cache.c
embedded/pal_text_cache.h
embedded/pal_font_cache.c
embedded/pal_font_cache.h
embedded/pal_save_cache.c
embedded/pal_save_cache.h
embedded/pal_video_static.c
embedded/pal_video_static.h
embedded/pal_ui_cache.c
embedded/pal_ui_cache.h
embedded/pal_music_cache.c
embedded/pal_music_cache.h
embedded/pal_menu_static.c
embedded/pal_menu_static.h
embedded/pal_ending_static.c
embedded/pal_ending_static.h
embedded/pal_palette_static.c
embedded/pal_palette_static.h
embedded/pal_dialog_static.c
embedded/pal_dialog_static.h
```

Source scan:

```sh
python3 -B tools/embedded_contract_check.py --root . --fail-on-source
```

Binary/artifact scan:

```sh
python3 -B tools/embedded_contract_check.py \
  --root . \
  --binary unix/sdlpal \
  --max text=2000000 \
  --max data=524288 \
  --max bss=8388608
```

The tool scans selected project sources for C/C++ heap use, decompression use, active `PAL_LARGE` scratch buffers, typed `pal_sram_`/`pal_psram_` storage declarations, and loose original PAL data filename references. When given a binary, it runs `size`, `objdump -h`, `objdump -t`, `objdump -d`, `nm -C`, and `nm -S --size-sort` to report sections, forbidden symbols/call targets, and named static-buffer totals.
It can also sum normal static-storage buffers from ELF symbols with `nm -S --size-sort`, for example:

```sh
python3 -B tools/embedded_contract_check.py \
  --root embedded \
  --binary embedded/build/pal_memory_smoke \
  --max-symbol-prefix pal_sram_=307200 \
  --max-symbol-prefix pal_psram_=8388608
```

It can also parse generated pack files through `--pack`, fail if any chunk has runtime flags, fail if a payload still starts with `YJ_1`, enforce pack-size budgets with `--max-pack-size PATH=BYTES`, and check that a linker map exists with `--link-map`.

## Resource Pack Builder

The host-side pack builder decodes YJ1 chunks before writing runtime packs:

```sh
python3 -B tools/pal_pack_build.py \
  /mnt/hgfs/deb13/PAL \
  --out-nor /tmp/pal_nor_default.pak \
  --out-tf /tmp/pal_tf_default.pak \
  --manifest /tmp/pal_pack_default_manifest.json
```

The embedded makefile wraps that default command as:

```sh
make -C embedded pack-build
```

Current default pack sizes from the audited data:

```text
/tmp/pal_nor_default.pak: 10,446,724 bytes
/tmp/pal_tf_default.pak: 47,309,294 bytes
```

Default NOR archives:

```text
ABC,BALL,DATA,F,FIRE,MGO,MIDI,MUS,PAT,RGM,SSS,TEXT,FONT
```

Default TF archives:

```text
FBP,GOP,MAP,RNG,SFX
```

`MAP`, `FBP`, `MGO`, `ABC`, `F`, `FIRE`, and RNG frames are decoded by the host tool. `WORD.DAT` and `M.MSG` are converted to UTF-16LE by the host tool. `WOR16.ASC` and `WOR16.FON` are converted to a sorted read-only glyph table by the host tool. `VOC.MKF` is converted to 22050Hz mono PCM16 chunks in the generated SFX archive, and raw `VOC.MKF` chunks are omitted from the default runtime packs. `GOP` is already raw/native and is copied as raw chunks.

The pack builder also writes a JSON manifest. It records the `PAL_DATA_DIR`, SHA-256 and byte size for every source file used by the generated packs, and the decoded/native chunk sizes and formats for each generated archive. `make -C embedded contract-check` passes that manifest back into `tools/embedded_contract_check.py`, which rehashes the source files and compares the manifest's decoded-size summary against the actual pack files.

The generated packs can also be checked with the C runtime reader through the host-side mmap checker:

```sh
cc -std=c99 -Wall -Wextra -Werror -O2 \
  -Iembedded embedded/pal_pack.c tools/pal_pack_check.c \
  -o /tmp/pal_pack_check
/tmp/pal_pack_check /tmp/pal_nor_default.pak /tmp/pal_tf_default.pak
```

Current C-reader summary:

```text
/tmp/pal_nor_default.pak: size=10446724
  ABC  chunks=  160 payload=2154538
  BALL chunks=  231 payload=133776
  DATA chunks=   15 payload=70784
  F    chunks=   19 payload=329624
  FIRE chunks=   55 payload=1909992
  MGO  chunks=  637 payload=3363230
  MIDI chunks=   88 payload=762086
  MUS  chunks=   88 payload=330928
  PAT  chunks=    9 payload=8448
  RGM  chunks=   92 payload=452830
  SSS  chunks=    5 payload=563212
  TEXT chunks=    1 payload=254762
  FONT chunks=    1 payload=88432
  archives=13 payload=10422642
/tmp/pal_tf_default.pak: size=47309294
  FBP  chunks=   72 payload=4608000
  GOP  chunks=  226 payload=11529414
  MAP  chunks=  226 payload=14614528
  RNG  chunks=   12 payload=7307725
  SFX  chunks=  276 payload=9236076
  archives=5 payload=47295743
```

The real-data SDL smoke also sweeps every generated NOR payload through `PalPack_MapConst()` as read-only `const uint8_t` views: 1,401 chunks, 10,422,642 payload bytes, and per-archive count/format/maximum-size checks matching the generated pack summary.

## Contract Runtime Slice

The first runtime slices are a pack reader and a plain static-buffer declaration unit:

```text
embedded/pal_pack.c
embedded/pal_pack.h
embedded/pal_noheap.c
embedded/pal_memory.c
embedded/pal_memory.h
embedded/pal_native_sdl_smoke.c
embedded/pal_realdata_sdl_smoke.c
embedded/pal_scene_cache.c
embedded/pal_scene_cache.h
embedded/pal_battle_cache.c
embedded/pal_battle_cache.h
embedded/pal_rng_cache.c
embedded/pal_rng_cache.h
embedded/pal_sfx_cache.c
embedded/pal_sfx_cache.h
embedded/pal_audio_static.c
embedded/pal_audio_static.h
embedded/pal_global_cache.c
embedded/pal_global_cache.h
embedded/pal_text_cache.c
embedded/pal_text_cache.h
embedded/pal_font_cache.c
embedded/pal_font_cache.h
embedded/pal_save_cache.c
embedded/pal_save_cache.h
embedded/pal_video_static.c
embedded/pal_video_static.h
embedded/pal_ui_cache.c
embedded/pal_ui_cache.h
embedded/pal_music_cache.c
embedded/pal_music_cache.h
embedded/pal_menu_static.c
embedded/pal_menu_static.h
embedded/pal_ending_static.c
embedded/pal_ending_static.h
embedded/pal_palette_static.c
embedded/pal_palette_static.h
embedded/pal_dialog_static.c
embedded/pal_dialog_static.h
```

Properties:

- maps pack payloads as `const uint8_t`,
- copies TF-style raw chunks into caller-supplied `uint8_t` buffers,
- uses no `malloc`, `calloc`, `realloc`, or `free`,
- links embedded smoke artifacts with `--wrap=malloc/calloc/realloc/free`, with wrappers that trap if project objects call the heap,
- contains no decompression path,
- rejects chunks flagged as compressed,
- keeps indexed video work in named SRAM/PSRAM buffers.

Build and verify:

```sh
make -C embedded clean check
python3 -B tools/embedded_contract_check.py \
  --root embedded \
  --binary embedded/build/pal_pack_smoke \
  --max text=65536 \
  --max data=4096 \
  --max bss=4096
```

Current result:

```text
source heap hits: 0
source decompress hits: 0
source scratch hits: 0
source storage hits: 0
source loose-resource hits: 0
text=4790 data=576 bss=104
PASS
```

The memory slice deliberately does not implement a pool or allocator. It declares named `uint8_t` buffers at static storage duration and lets the ELF symbol table be the source of truth:

```text
pal_sram_framebuffer[64000]
pal_sram_big_buffer[65536]
pal_sram_audio[16384]
pal_sram_display_dma[4096]
pal_sram_hot_globals[24576]
pal_sram_misc[8192]
pal_sram_palette_current[768]
pal_sram_palette_work[768]

pal_psram_save_state[655360]
pal_psram_map_tiles[65536]
pal_psram_gop_copy[65536]
pal_psram_screen_bak[64000]
pal_psram_resource_staging[262144]
pal_psram_tf_toc[32768]
pal_psram_rng_frame_a[65000]
pal_psram_rng_frame_b[65000]
pal_psram_tf_readahead[131072]
pal_psram_effect[65536]
pal_psram_fbp_background[64000]
pal_psram_sfx_bank[4194304]
pal_psram_text_misc[262144]
pal_psram_sprite_pin[1048576]
pal_psram_menu_background[64000]
pal_psram_menu_image[64000]
pal_psram_menu_box[5184]
pal_psram_ending_fbp_a[64000]
pal_psram_ending_fbp_b[64000]
```

Build and verify:

```sh
make -C embedded clean check artifact-check
python3 -B tools/embedded_contract_check.py \
  --root embedded \
  --fail-on-source \
  --binary embedded/build/pal_memory_smoke \
  --max text=65536 \
  --max data=4096 \
  --max bss=7500000 \
  --max-symbol-prefix pal_sram_=307200 \
  --max-symbol-prefix pal_psram_=8388608
```

Current result:

```text
source heap hits: 0
source decompress hits: 0
source scratch hits: 0
source storage hits: 0
source loose-resource hits: 0
text=1573 data=520 bss=7486488
pal_sram_ total=184320 limit=307200
pal_psram_ total=7302160 limit=8388608
PASS
```

## Native SDL Fixed-Memory Smoke

The embedded makefile now builds a native SDL2 smoke binary that uses the same static SRAM declarations and pack reader:

```text
embedded/build/pal_native_sdl_smoke
```

It maps a `const uint8_t` native pack chunk, can copy pack TOCs into fixed PSRAM from either a mapped image or a read-at callback, fills `pal_sram_framebuffer`, exercises `embedded/pal_video_static.c` clear/save/restore/scanline RGB565 conversion, and wraps that exact 8-bit buffer with `SDL_CreateRGBSurfaceFrom()`. SDL may allocate internally; the project-side smoke code does not use heap allocation and contains no decoder path.

Build and verify:

```sh
make -C embedded clean check artifact-check
python3 -B tools/embedded_contract_check.py \
  --root embedded \
  --fail-on-source \
  --binary embedded/build/pal_native_sdl_smoke \
  --max text=65536 \
  --max data=4096 \
  --max bss=7500000 \
  --max-symbol-prefix pal_sram_=307200 \
  --max-symbol-prefix pal_psram_=8388608 \
  --max-symbol-prefix pal_video_=4096
```

Current result:

```text
source heap hits: 0
source decompress hits: 0
source scratch hits: 0
source storage hits: 0
source loose-resource hits: 0
text=6468 data=640 bss=7487008
pal_sram_ total=184320 limit=307200
pal_psram_ total=7302160 limit=8388608
pal_video_ total=512 limit=4096
PASS
```

The native SDL smoke now touches `pal_psram_screen_bak`, so the full named PSRAM buffer section is visible in this artifact as well as in `pal_memory_smoke`.

## Native SDL Real-Data Smoke

The embedded makefile also builds a native SDL2 smoke binary that opens the generated pack files from the audited data set:

```text
embedded/build/pal_realdata_sdl_smoke
```

It maps both generated packs read-only, checks real archive counts, maps representative NOR chunks as `const uint8_t`, copies the TF pack TOC into `pal_psram_tf_toc`, verifies the same TOC/payload path through a file-backed read-at callback, copies real TF chunks into `pal_sram_framebuffer`, `pal_psram_map_tiles`, `pal_psram_gop_copy`, and `pal_psram_sfx_bank`, exercises the static indexed-video path, then wraps `pal_sram_framebuffer` with SDL. There is no project-side heap allocation and no decoder path in this binary.

The current TF pack TOC is 13,084 bytes for 812 chunks. The smoke copies it into the 32KB `pal_psram_tf_toc` array and verifies chunk counts, SFX chunk #255 metadata, RNG/MAP metadata, and FBP/MAP payload copies through the copied TOC. The file-backed read-at path proves the target shape: the runtime can keep only the TF index in PSRAM and read payload ranges from TF without mapping the whole pack into target memory. It now sweeps every TF native/PCM payload that fits the fixed staging buffers through read-at: 72 FBP chunks totaling 4,608,000 bytes, 226 GOP chunks totaling 11,529,414 bytes, 226 MAP chunks totaling 14,614,528 bytes, and 276 SFX chunks totaling 9,236,076 bytes. RNG is swept separately by frame.

The same smoke also exercises `embedded/pal_scene_cache.c` on high-pressure real scenes:

| Scene | Events | Sprite refs | Unique sprites |
| ---: | ---: | ---: | ---: |
| 59 | 142 | 122 | 11 |
| 65 | 120 | 91 | 8 |
| 156 | 130 | 123 | 10 |
| 260 | 72 | 58 | 11 |

For each scene it reads the scene/event tables from `SSS`, copies decoded `MAP` and raw `GOP` chunks from the TF pack into named PSRAM buffers, and keeps event-object MGO sprites as deduplicated `const uint8_t *` views into the NOR pack. The same scene set is checked through both mapped-pack reads and file-backed TF read-at reads, so the scene path does not require mapping the whole TF pack.

The smoke also checks the PSRAM sprite-pin fallback for a high-sprite scene:

| Scene | Sprite refs | Unique sprites | Unique bytes | Pinned bytes |
| ---: | ---: | ---: | ---: | ---: |
| 153 | 14 | 7 | 65,150 | 65,156 |

This copies the scene's unique `MGO` sprites into `pal_psram_sprite_pin`, so the same fixed-buffer path can be used if a build demotes MGO sprites from NOR to a TF-backed pack. The pinned scene is also checked while MAP/GOP are loaded through the TF read-at path.

The smoke also exercises `embedded/pal_battle_cache.c` on real high-pressure battle teams:

| Team | Enemy refs | Unique enemy sprites |
| ---: | ---: | ---: |
| 156 | 3 | 1 |
| 342 | 3 | 2 |
| 385 | 3 | 1 |

For each team it reads the enemy-team table from `DATA`, resolves enemy sprite ids through the `SSS` object table, copies a decoded `FBP` background from the TF pack into `pal_psram_fbp_background`, maps player sprites from `F`, maps enemy sprites from `ABC`, maps one `FIRE` effect, and maps `DATA.MKF #10` battle effects as read-only `const uint8_t` data. The battle FBP background copy is checked through both mapped-pack and TF-style file-backed read-at paths. It also copies the largest observed `FIRE.MKF` effect chunk (#37, 65,502 bytes) into `pal_psram_effect` to prove the fixed PSRAM effect scratch path.

The smoke also exercises `embedded/pal_rng_cache.c` on large real RNG frames:

| RNG movie | Frame | Frames in movie | Frame bytes | Destination |
| ---: | ---: | ---: | ---: | --- |
| 4 | 0 | 41 | 64,288 | `pal_psram_rng_frame_a` |
| 5 | 0 | 83 | 64,104 | `pal_psram_rng_frame_b` |
| 9 | 0 | 257 | 61,773 | `pal_psram_rng_frame_a` |

These are already decoded by `tools/pal_pack_build.py`; runtime only copies the selected frame into a named PSRAM buffer. The smoke covers both the mapped-pack path and the TF-style read-at path: file-backed reads stage the selected movie's frame table in `pal_psram_tf_readahead`, then read the selected frame payload range directly into `pal_psram_rng_frame_a` or `pal_psram_rng_frame_b`. It also sweeps every RNG movie through the read-at path: 12 movies, 1,476 frames, 7,301,725 bytes of decoded frame payload, and a 64,288-byte maximum at movie #4 frame 0.

The smoke also exercises `embedded/pal_sfx_cache.c` and `embedded/pal_audio_static.c` on representative large SFX chunks from the TF pack. The host pack builder converts VOC data to 22050Hz mono PCM16 before writing the SFX archive; runtime only copies, validates, and mixes PCM16 samples. The same representative SFX bank is copied through both mapped-pack and TF-style file-backed read-at paths:

| SFX chunk | PCM16 pack bytes |
| ---: | ---: |
| 1 | 12,622 |
| 62 | 111,738 |
| 192 | 132,856 |
| 213 | 204,664 |
| 214 | 148,342 |
| 255 | 211,152 |
| 272 | 200,526 |

The bank copies these chunks into `pal_psram_sfx_bank` with 4-byte alignment and uses 1,021,906 bytes total for this checked set. `PalAudio_MixSfx()` mixes checked chunks into the fixed `pal_sram_audio` buffer; no runtime resampler is linked into this slice.

The smoke also exercises `embedded/pal_global_cache.c` against real SSS/DATA tables. It copies mutable default state into `pal_psram_save_state`:

| Data | Records | Bytes |
| --- | ---: | ---: |
| Event objects | 5,369 | 171,808 |
| Scenes | 300 | 2,400 |
| DOS object table | 589 | 7,068 |
| Player roles | 1 | 900 |

With 4-byte alignment this uses 182,176 bytes of the 640KB save-state PSRAM buffer. Scripts, stores, enemies, enemy teams, magic, battlefields, level-up magic, battle-effect indexes, enemy positions, and level-up EXP remain `const uint8_t *` views into the NOR pack.

The smoke also exercises `embedded/pal_text_cache.c` against the generated TEXT archive. The host pack builder converts `WORD.DAT` and `M.MSG` from CP950 to UTF-16LE and stores one read-only NOR payload:

| Text source | Entries | UTF-16LE bytes |
| --- | ---: | ---: |
| `WORD.DAT` | 589 | included in text payload |
| `M.MSG` | 10,495 | included in text payload |
| Combined text data | 11,084 | 210,386 |

The TEXT archive chunk is 254,762 bytes including header and offset tables. Runtime only validates the table and returns `const uint8_t *` UTF-16LE spans.

The smoke also exercises `embedded/pal_font_cache.c` against the generated FONT archive. The host pack builder trims the `WOR16.ASC` `0xff` terminator, decodes the valid 5,202-byte CP950 prefix, pairs it with 30-byte source glyphs from `WOR16.FON` at offset `0x682`, pads each glyph to 32 bytes, deduplicates by codepoint, and stores a sorted read-only table:

| Font source | Count | Bytes |
| --- | ---: | ---: |
| Decoded `WOR16.ASC` slots | 2,601 | 5,202 |
| Source `WOR16.FON` glyph slots after `0x682` | 2,602 | 78,060 |
| Unique generated glyphs | 2,600 | 83,200 |
| Generated FONT archive chunk | 1 | 88,432 |

Runtime only validates the header/table and binary-searches codepoints to return `const uint8_t *` glyph spans. The checked glyphs include U+7D93, U+9A57, and U+503C from the word "經驗值"; ASCII digit U+0030 is intentionally absent from this WOR16 CJK pack.

The smoke also exercises `embedded/pal_save_cache.c` against real save files in `/mnt/hgfs/deb13/PAL`. It reads each file into the existing `pal_psram_save_state` fixed buffer, checks that no heap-backed `SAVEDGAME_DOS`/`SAVEDGAME_WIN` object is needed, validates common header fields, then writes `2.rpg` back out through the same fixed buffer and reads it again for a byte-preserving checksum round trip:

| Save file | Bytes | Saved times | Scene | Cash |
| --- | ---: | ---: | ---: | ---: |
| `1.rpg` | 184,672 | 1 | 1 | 0 |
| `2.rpg` | 188,864 | 8 | 17 | 580 |
| `4.RPG` | 183,488 | 1 | 1 | 899,999 |

The smoke also exercises `embedded/pal_video_static.c`. It preserves a real 320x200 indexed framebuffer through `pal_psram_screen_bak`, saves/restores rectangular regions in the same static buffer, saves/restores a full scene through `pal_sram_big_buffer`, uses the 512-byte `pal_video_rgb565` LUT, and converts one line into `pal_sram_display_dma` for RGB565 scanout-style output. The rectangular save/restore path is the embedded replacement shape for `VIDEO_DuplicateSurface()` box-background saves; the SRAM big-buffer path is the replacement shape for the battle scene surface.

The smoke also exercises `embedded/pal_ui_cache.c` against real UI assets in the NOR pack:

| Asset | Pack source | Bytes | Runtime placement |
| --- | --- | ---: | --- |
| UI sprite | `DATA.MKF #9` | 25,532 | `const uint8_t *` NOR view |
| Battle effect sprite | `DATA.MKF #10` | 17,478 | `const uint8_t *` NOR view |
| Item bitmap | `BALL.MKF #95` | 1,876 | `const uint8_t *` NOR view |
| Face bitmap | `RGM.MKF #72` | 8,024 | `const uint8_t *` NOR view |
| Day/night palette | `PAT.MKF #0` | 768 per palette | copied into `pal_sram_misc` |

The smoke also exercises `embedded/pal_music_cache.c` against real music assets in the NOR pack:

| Track | MIDI bytes | MUS/RIX bytes | Runtime placement |
| ---: | ---: | ---: | --- |
| 31 | 6,162 | 3,220 | `const uint8_t *` NOR view |
| 37 | 24,548 | 3,956 | `const uint8_t *` NOR view |
| 77 | 5,180 | 3,898 | `const uint8_t *` NOR view |

The smoke also exercises `embedded/pal_menu_static.c` against real menu/status assets:

| Asset | Pack source | Bytes | Runtime placement |
| --- | --- | ---: | --- |
| Status background | `FBP.MKF #0` | 64,000 | `pal_psram_menu_background` |
| Equipment background | `FBP.MKF #1` | 64,000 | `pal_psram_menu_background` |
| Main menu background | `FBP.MKF #60` | 64,000 | `pal_psram_menu_background` |
| Face image scratch | `RGM.MKF #72` | 8,024 | `pal_psram_menu_image` |
| Item image | `BALL.MKF #95` | 1,876 | `const uint8_t *` NOR view |
| Menu box | 72x72 fill | 5,184 | `pal_psram_menu_box` |

The FBP menu background copies are checked through both mapped-pack and TF-style file-backed read-at paths.

The smoke also exercises `embedded/pal_ending_static.c` against real ending/splash assets:

| Asset | Pack source | Bytes | Runtime placement |
| --- | --- | ---: | --- |
| Ending still | `FBP.MKF #68` | 64,000 | `pal_psram_ending_fbp_a` |
| Ending upper screen | `FBP.MKF #61` | 64,000 | `pal_psram_ending_fbp_a` |
| Ending lower screen | `FBP.MKF #62` | 64,000 | `pal_psram_ending_fbp_b` |
| Ending beast sprite | `MGO.MKF #571` | 59,516 | `const uint8_t *` NOR view |
| Ending girl sprite | `MGO.MKF #572` | 5,736 | `const uint8_t *` NOR view |
| Ending effect sprite | `MGO.MKF #627` | 3,136 | `const uint8_t *` NOR view |

The FBP ending/splash screen copies are checked through both mapped-pack and TF-style file-backed read-at paths.

The smoke also exercises `embedded/pal_palette_static.c` against real palette/fade data:

| Asset | Pack source | Bytes | Runtime placement |
| --- | --- | ---: | --- |
| Day palette | `PAT.MKF #0` | 768 | `pal_sram_palette_current` |
| Night palette | `PAT.MKF #0` | 768 | `pal_sram_misc`, then fade source |
| Fade work palette | day/night blend, black scale, color fill | 768 | `pal_sram_palette_work` |

The smoke also exercises `embedded/pal_dialog_static.c` against real dialog assets:

| Asset | Pack source | Bytes | Runtime placement |
| --- | --- | ---: | --- |
| Dialog icons | `DATA.MKF #12` | 282 | `const uint8_t *` NOR view |
| Face bitmap | `RGM.MKF #72` | 8,024 | `const uint8_t *` NOR view |

Build packs and run the real-data smoke:

```sh
make -C embedded realdata-check
```

The data directory can be overridden with:

```sh
make -C embedded realdata-check PAL_DATA_DIR=/mnt/hgfs/deb13/PAL
```

Verify the artifact:

```sh
make -C embedded contract-check
```

The `contract-check` target is the same `tools/embedded_contract_check.py` source/binary/pack gate: no project-side heap hits, no decoder hits, no active `PAL_LARGE`, typed SRAM/PSRAM storage, or loose-resource hits, section budgets from `size`/`objdump` including `.rodata`, symbol-prefix budgets from `nm -S --size-sort`, no generated-pack runtime flags or YJ1 payloads, no raw VOC archive in runtime packs, and a 16MB NOR pack size limit.

Current result:

```text
source heap hits: 0
source decompress hits: 0
source scratch hits: 0
source storage hits: 0
source loose-resource hits: 0
objdump -t forbidden symbols: 0
text=40662 .rodata=448 data=744 bss=7493280
pal_sram_ total=184320 limit=307200
pal_psram_ total=7302160 limit=8388608
pal_scene_ total=4736 limit=8192
pal_battle_ total=334 limit=2048
pal_sfx_ total=384 limit=4096
pal_audio_ total=0 limit=4096
pal_global_ total=232 limit=4096
pal_save_ total=512 limit=4096
pal_video_ total=512 limit=4096
pal_ui_ total=0 limit=4096
pal_music_ total=0 limit=4096
pal_menu_ total=0 limit=4096
pal_ending_ total=0 limit=4096
pal_palette_ total=0 limit=4096
pal_dialog_ total=0 limit=4096

pack /tmp/pal_nor_default.pak:
size=10446724 chunks=1401 payload=10422642 max-size=16777216
formats NATIVE=1399 TEXT_UTF16=1 FONT_GLYPHS=1

pack /tmp/pal_tf_default.pak:
size=47309294 chunks=812 payload=47295743
formats NATIVE=524 RNG_FRAMES=12 SFX_PCM16=276

linker map build/pal_realdata_sdl_smoke.map:
size=620502
PASS
```

## Native SDL Build Attempt

The default Unix makefile currently assumes SDL3:

```sh
make -C unix -j2
```

On this host, that fails because `pkg-config sdl3` and `SDL3/SDL.h` are not installed.

The native SDL build can be produced with SDL2 on this host by overriding the make variables:

```sh
make -C unix -j2 \
  SDL_CONFIG='pkg-config sdl2' \
  GENERATED='' \
  EXTRA_CCFLAGS='-I. -I.. -I../liboggvorbis/include -I../liboggvorbis/src -I../libopusfile/include -I../libopusfile/src -I../libopusfile/celt -I../libopusfile/silk -I../libopusfile/silk/float -I../timidity -DPAL_HAS_PLATFORM_SPECIFIC_UTILS -DHAVE_CONFIG_H -UUSE_SDL3 -DUSE_SDL3=0'
```

This produces:

```text
unix/sdlpal
```

This is only a baseline desktop/native binary. It is not yet an embedded-contract binary, because it still includes heap allocation, YJ decompression, MP3/OGG/OPUS/AVI, Timidity/TSF paths, and large static tables.

## Reduced Native SDL Profile

The Unix makefile now also has a reduced full-engine native profile:

```sh
make -C unix -j2 EMBEDDED_CONTRACT=1
```

The reduced profile can be checked with the same binary scanner:

```sh
make -C unix EMBEDDED_CONTRACT=1 contract-check
```

On this host it selects SDL2 and produces:

```text
unix/sdlpal-embedded-contract
```

This profile builds with `-ffunction-sections`, `-fdata-sections`, `--gc-sections`, `--wrap=malloc/calloc/realloc/free`, `PAL_NO_RUNTIME_HEAP`, and `PAL_NO_RUNTIME_DECOMPRESS`. It excludes the MP3, OGG, OPUS, AVI, Timidity, TinySoundFont, GLSL, native MIDI, launcher UI, desktop sound, desktop RIX, high-quality resampler, desktop font, desktop text, codepage-table, and `yj1.c` decompressor objects from the full-engine build. The checker also rejects active C++ `new`/`delete` use and linked `operator new`/`operator delete` symbols. The desktop text/font/music/SFX replacement maps generated NOR/TF packs read-only, uses the existing `PalTextCache`, `PalFontCache`, and `PalMusicCache` readers, synthesizes MUS/RIX through a fixed-storage OPL2 path without the desktop resampler, and copies SFX PCM from the TF pack into a fixed PSRAM buffer before mixing:

```text
PAL_InitFont
PAL_FreeFont
PAL_DrawCharOnSurface
PAL_CharWidth
PAL_FontHeight
PAL_InitText
PAL_FreeText
PAL_GetWord
PAL_GetMsg
PAL_DrawText
RIX_Init
SOUND_Init
```

Unsupported desktop audio/video backends remain inert in this reduced profile:

```text
resampler_init
PAL_MultiByteToWideCharCP
PAL_DetectCodePageForString
MP3_Init
OGG_Init
OPUS_Init
TIMIDITY_Init
TSF_Init
PAL_AVIInit
PAL_AVIShutdown
AVI_FillAudioBuffer
AVI_GetPlayState
```

Current reduced-profile artifact size:

```text
text=211451 data=4330 bss=1672224
.text=170470 .rodata=8402 .data=226 .bss=1672224
pal_sram_ total=74638 limit=307200
pal_psram_ total=1498888 limit=8388608
```

The reduced profile now has no forbidden heap/new/delete/decompress symbols in `objdump -t` or `nm -C`, and no disassembly call sites to the heap/decompress trap targets or C++ allocation operators:

```text
source heap hits: 0
source decompress hits: 0
source scratch hits: 0
source storage hits: 0
source loose-resource hits: 0
objdump -t forbidden symbols: 0
forbidden call targets: 0
```

The Unix contract source scan runs with `--fail-on-source` over the exact `$(CFILES) $(CPPFILES)` linked by the contract profile, strips simple inactive preprocessor blocks for contract-only defines, and excludes nonlinked native-MIDI sources before counting heap/decompress/`PAL_LARGE` scratch/typed SRAM-PSRAM storage/loose-resource patterns. The same target rebuilds and verifies the generated NOR/TF packs and manifest, rejects raw `VOC` in runtime packs, and enforces the 16MB NOR pack budget.

This is still only a reduced native contract profile, not the finished embedded runtime. The macros make old heap/decompress call sites land on unavailable traps; the reduced profile now has no surviving calls to those traps. Remaining engineering work is to continue replacing desktop resource paths with the generated pack/static-buffer slices and target display/input backends.

The reduced profile's music path is no longer silent. `unix/contract_music.cpp` maps generated MUS/RIX tracks from the NOR pack as `const uint8_t *` spans, feeds them to a `PAL_NO_RUNTIME_HEAP` `CrixPlayer::load_buffer()` path, and renders one 70Hz tick at a time into `pal_sram_contract_rix_tick`. `unix/contract_opl2.cpp` links only the DOSBox OPL2 core used by this path. The OPL output is generated at the configured game audio rate, so the contract profile still excludes `resampler.c` and its large LUTs.

The contract SDL2 video startup now wraps named static pixel buffers with SDL surfaces instead of asking SDL to allocate the large pixel arrays. `pal_sram_video_screen` is the 320x200 indexed `gpScreen` pixel store, `pal_psram_video_screen_bak` is the backup screen pixel store, and `pal_psram_video_screen_real` is the host SDL2 32-bit presentation surface pixel store. SDL still creates small wrapper objects and the renderer/texture for host verification, but the large project video buffers are now visible in `nm -S --size-sort`.

Contract-mode `palcommon.c` now provides generated-pack archive handles through the legacy MKF read API. `PAL_MKFOpenPackArchive()` returns fixed sentinel handles for generated pack archives, `PAL_MKFGetChunkCount()`, `PAL_MKFGetChunkSize()`, and `PAL_MKFReadChunk()` service those handles from read-only mapped NOR/TF packs, and `PAL_MKFMapChunk()` exposes a `const uint8_t *` view for code that can parse a native chunk in place. `UTIL_CloseFile()` ignores these handles.

With that bridge in place, the contract `global.c` path opens FBP/MGO/BALL/DATA/F/FIRE/RGM/SSS from generated packs, `res.c` opens MAP/GOP from generated packs, `battle.c` opens ABC from generated packs, and `rngplay.c` opens RNG from generated packs. These paths no longer depend on the original MKF files in the native contract profile; they require host-built native chunks in `/tmp/pal_nor_default.pak` and `/tmp/pal_tf_default.pak` or the `PAL_CONTRACT_NOR_PACK` / `PAL_CONTRACT_TF_PACK` overrides.

The first full-engine loader cut is `map.c`: under `PAL_NO_RUNTIME_HEAP` / `PAL_NO_RUNTIME_DECOMPRESS`, it uses static `uint8_t` PSRAM buffers for the `PALMAP` object and GOP sprite data, and it accepts only already-native 64KB map chunks. The old compressed-MAP path remains only for non-contract desktop builds.

The full-engine splash path in `main.c` now uses static `uint8_t` PSRAM for FBP staging, maps title/crane sprites as `const uint8_t` NOR views, uses a clipped RLE blit for title reveal instead of mutating sprite data, and accepts only already-native splash FBP/MGO chunks. The old 128KB heap block and `Decompress()` calls remain only for non-contract desktop builds.

The full-engine menu paths in `uigame.c` now avoid runtime decompression in contract mode. Opening-menu, status, and equipment paths use shared static `uint8_t` PSRAM buffers for FBP background and box scratch; saved cash/system/selection boxes use named per-call-site PSRAM buffers. RGM/BALL menu images are mapped as read-only `const uint8_t` NOR views.

The full-engine battle path in `battle.c` now maps player/enemy F/ABC battle sprites and `DATA.MKF #10` effect sprites as `const uint8_t` views into the native NOR pack under contract mode. Battle backgrounds still use a fixed PSRAM buffer. The old F/ABC/FBP decompression and battle-effect allocation paths remain only for non-contract desktop builds.

The full-engine magic/summon paths in `fight.c` now map FIRE effect sprites and F.MKF summon sprites as `const uint8_t` views into the native NOR pack in contract mode. These paths require the host-built packs to provide native chunks and avoid `PAL_MKFDecompressChunk()` / `UTIL_malloc()` at runtime.

The full-engine ending paths in `ending.c` now use a static `uint8_t` PSRAM buffer for FBP screen staging and map MGO ending/effect sprites as `const uint8_t` NOR views in contract mode. FBP and MGO chunks must already be native, so ending screens and animations avoid heap allocation and runtime decompression.

The contract `VIDEO_Startup()` path now omits the YJ1-compressed touch-overlay BMP decode. Overlay art for target builds must be preconverted/offline-packed; the old `bmpData` decode remains only in non-contract desktop builds.

The contract `global.c` path skips the legacy heap-loaded object-description list. This matches the audited `/mnt/hgfs/deb13/PAL` data set, which has no `DESC.DAT`; future data sets that include descriptions should generate read-only object-description data instead of using `PAL_LoadObjectDesc()`.

The contract `res.c` path now uses static `uint8_t` PSRAM storage for the resource manager and event-sprite pointer table. Event and player MGO sprites are `const uint8_t` views into the native NOR pack, and duplicate event sprite references share the same view. MGO chunks must already be native.

The contract `rngplay.c` path now reads host-predecoded native RNG frame records into `pal_psram_rng_frame_static` and blits them directly without heap allocation or `Decompress()`.

The contract `audio.c` path now uses `pal_sram_audio_mix_static`, a named 4KB SRAM mix buffer, instead of allocating `gAudioDevice.pSoundBuffer`. If SDL requests a larger callback buffer, the sound-effect mixer processes it in fixed-size chunks.

The Unix contract SFX replacement copies the requested host-converted PCM16 SFX chunk from the generated TF pack into `pal_psram_contract_sfx`, a 212KB static PSRAM buffer, before handing it to the mixer. The largest generated SFX payload in the current data set is chunk #255 at 211,152 bytes, so single-effect playback fits without reading TF-backed data from the audio callback.

The contract `global.c` path now uses named `uint8_t` PSRAM storage for mutable event objects, mutable magic data, and save/load structs. Read-only script entries, stores, enemies, enemy teams, battlefields, and level-up magic tables are mapped from the native NOR pack. The contract profile assumes the DOS/YJ1 data set and avoids heap-based version/codepage probes.

The contract `audio.c`, `global.c`, and `util.c` paths no longer keep active loose original data filename references for `mus.mkf`, `word.dat`, or the legacy file-check list. Music/data access is through the generated packs in contract mode; save files remain normal user files.

The contract `palcfg.c` / `util.c` path avoids heap config strings and heap path lookup helpers. It compiles out config-file parsing in the reduced profile, uses default/static config strings, keeps fixed static `uint8_t` config buffers for string setters, and uses case-sensitive no-heap path lookup.

The contract `ui.c` path now maps `DATA.MKF #9` UI sprite data as a read-only `const uint8_t` NOR pack view. Saved UI boxes use caller-owned named `uint8_t` PSRAM metadata/pixel buffers declared for the concrete `uigame.c` menus, avoiding `calloc`, `free`, project-side duplicate-surface allocation, and a generic UI box pool.

The contract `ui.c` object-description load/free path is intentionally a no-heap `NULL` path for the current data set because no `DESC.DAT` exists. Description-bearing data should be handled by generated read-only text/object-description data rather than the legacy linked-list loader.

The contract `palette.c` path now reads `PAT` chunks through the generated pack bridge and stores loaded/current/work palette colors in named SRAM `uint8_t` buffers (`pal_sram_palette_base`, `pal_sram_palette_work`, `pal_sram_palette_next`). It no longer opens the original `pat.mkf` in contract mode, and palette fades no longer use `PAL_LARGE SDL_Color[256]` local arrays.

The contract `global.c`, `ui.c`, and `uigame.c` mutable storage now follows the same `uint8_t` buffer rule as the embedded slices: mutable global buffers and UI box metadata/pixels are named byte arrays that are cast at the use site, and the contract checker rejects active typed `pal_sram_`/`pal_psram_` declarations.

## Current Contract Status

`make -C unix EMBEDDED_CONTRACT=1 contract-check` passes for the reduced full-engine contract profile:

```text
source heap hits: 0
source decompress hits: 0
source scratch hits: 0
source storage hits: 0
source loose-resource hits: 0
objdump -t forbidden symbols: 0
forbidden call targets: 0
text=211451 data=4330 bss=1672224
.text=170470 .rodata=8402 .data=226 .bss=1672224
pal_sram_ total=74638 / 307200
pal_psram_ total=1498888 / 8388608
NOR pack=10446724 / 16777216
TF pack=47309294
```

The normal native SDL2 desktop build still passes as a regression check, but it is not the target profile and still includes desktop heap, codec, and large-table paths. Target work should continue against `unix/sdlpal-embedded-contract` and `embedded/pal_realdata_sdl_smoke`.

## Next Engineering Cuts

1. If a future data set includes `DESC.DAT`, generate read-only object-description data instead of enabling the heap linked-list loader.
2. Continue wiring static pack/resource slices into the full gameplay path while keeping source/binary contract checks at zero heap, zero runtime decompression, zero active `PAL_LARGE`, and zero loose-resource references.
3. Keep running `tools/embedded_contract_check.py` after each cut until source and binary checks pass.
