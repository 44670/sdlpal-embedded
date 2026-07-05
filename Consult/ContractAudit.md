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

The tool scans selected project sources for heap/decompression use and, when given a binary, runs `size`, `objdump -h`, and `nm -C` to report sections and forbidden symbols.
It can also sum normal static-storage buffers from ELF symbols with `nm -S --size-sort`, for example:

```sh
python3 -B tools/embedded_contract_check.py \
  --root embedded \
  --binary embedded/build/pal_memory_smoke \
  --max-symbol-prefix pal_sram_=307200 \
  --max-symbol-prefix pal_psram_=8388608
```

It can also parse generated pack files through `--pack`, fail if any chunk has runtime flags, fail if a payload still starts with `YJ_1`, and enforce pack-size budgets with `--max-pack-size PATH=BYTES`.

## Resource Pack Builder

The host-side pack builder decodes YJ1 chunks before writing runtime packs:

```sh
python3 -B tools/pal_pack_build.py \
  /mnt/hgfs/deb13/PAL \
  --out-nor /tmp/pal_nor_default.pak \
  --out-tf /tmp/pal_tf_default.pak
```

The embedded makefile wraps that default command as:

```sh
make -C embedded pack-build
```

Current default pack sizes from the audited data:

```text
/tmp/pal_nor_default.pak: 10,446,724 bytes
/tmp/pal_tf_default.pak: 49,309,874 bytes
```

Default NOR archives:

```text
ABC,BALL,DATA,F,FIRE,MGO,MIDI,MUS,PAT,RGM,SSS,TEXT,FONT
```

Default TF archives:

```text
FBP,GOP,MAP,RNG,VOC,SFX
```

`MAP`, `FBP`, `MGO`, `ABC`, `F`, `FIRE`, and RNG frames are decoded by the host tool. `WORD.DAT` and `M.MSG` are converted to UTF-16LE by the host tool. `WOR16.ASC` and `WOR16.FON` are converted to a sorted read-only glyph table by the host tool. `VOC.MKF` is converted to 22050Hz mono PCM16 chunks in the generated SFX archive. `GOP` and most remaining audio/data chunks are already raw/native and are copied as raw chunks.

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
/tmp/pal_tf_default.pak: size=49309874
  FBP  chunks=   72 payload=4608000
  GOP  chunks=  226 payload=11529414
  MAP  chunks=  226 payload=14614528
  RNG  chunks=   12 payload=7307725
  VOC  chunks=  276 payload=1995936
  SFX  chunks=  276 payload=9236076
  archives=6 payload=49291679
```

## Contract Runtime Slice

The first runtime slices are a pack reader and a plain static-buffer declaration unit:

```text
embedded/pal_pack.c
embedded/pal_pack.h
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
```

Properties:

- maps pack payloads as `const uint8_t`,
- copies TF-style raw chunks into caller-supplied `uint8_t` buffers,
- uses no `malloc`, `calloc`, `realloc`, or `free`,
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
text=2395 data=576 bss=8
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

pal_psram_save_state[655360]
pal_psram_map_tiles[65536]
pal_psram_gop_copy[65536]
pal_psram_screen_bak[64000]
pal_psram_resource_staging[262144]
pal_psram_rng_frame_a[65000]
pal_psram_rng_frame_b[65000]
pal_psram_tf_readahead[131072]
pal_psram_effect[65536]
pal_psram_fbp_background[64000]
pal_psram_sfx_bank[4194304]
pal_psram_text_misc[262144]
pal_psram_sprite_pin[1048576]
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
  --max bss=7200000 \
  --max-symbol-prefix pal_sram_=307200 \
  --max-symbol-prefix pal_psram_=8388608
```

Current result:

```text
source heap hits: 0
source decompress hits: 0
text=1461 data=520 bss=7191000
pal_sram_ total=182784 limit=307200
pal_psram_ total=7008208 limit=8388608
PASS
```

## Native SDL Fixed-Memory Smoke

The embedded makefile now builds a native SDL2 smoke binary that uses the same static SRAM declarations and pack reader:

```text
embedded/build/pal_native_sdl_smoke
```

It maps a `const uint8_t` native pack chunk, fills `pal_sram_framebuffer`, exercises `embedded/pal_video_static.c` clear/save/restore/scanline RGB565 conversion, and wraps that exact 8-bit buffer with `SDL_CreateRGBSurfaceFrom()`. SDL may allocate internally; the project-side smoke code does not use heap allocation and contains no decoder path.

Build and verify:

```sh
make -C embedded clean check artifact-check
python3 -B tools/embedded_contract_check.py \
  --root embedded \
  --fail-on-source \
  --binary embedded/build/pal_native_sdl_smoke \
  --max text=65536 \
  --max data=4096 \
  --max bss=7200000 \
  --max-symbol-prefix pal_sram_=307200 \
  --max-symbol-prefix pal_psram_=8388608 \
  --max-symbol-prefix pal_video_=4096
```

Current result:

```text
source heap hits: 0
source decompress hits: 0
text=5228 data=640 bss=7191520
pal_sram_ total=182784 limit=307200
pal_psram_ total=7008208 limit=8388608
pal_video_ total=512 limit=4096
PASS
```

The native SDL smoke now touches `pal_psram_screen_bak`, so the full named PSRAM buffer section is visible in this artifact as well as in `pal_memory_smoke`.

## Native SDL Real-Data Smoke

The embedded makefile also builds a native SDL2 smoke binary that opens the generated pack files from the audited data set:

```text
embedded/build/pal_realdata_sdl_smoke
```

It maps both generated packs read-only, checks real archive counts, maps representative NOR chunks as `const uint8_t`, copies real TF chunks into `pal_sram_framebuffer`, `pal_psram_map_tiles`, `pal_psram_gop_copy`, and `pal_psram_sfx_bank`, exercises the static indexed-video path, then wraps `pal_sram_framebuffer` with SDL. There is no project-side heap allocation and no decoder path in this binary.

The same smoke also exercises `embedded/pal_scene_cache.c` on high-pressure real scenes:

| Scene | Events | Sprite refs | Unique sprites |
| ---: | ---: | ---: | ---: |
| 59 | 142 | 122 | 11 |
| 65 | 120 | 91 | 8 |
| 156 | 130 | 123 | 10 |
| 260 | 72 | 58 | 11 |

For each scene it reads the scene/event tables from `SSS`, copies decoded `MAP` and raw `GOP` chunks from the TF pack into named PSRAM buffers, and keeps event-object MGO sprites as deduplicated `const uint8_t *` views into the NOR pack.

The smoke also exercises `embedded/pal_battle_cache.c` on real high-pressure battle teams:

| Team | Enemy refs | Unique enemy sprites |
| ---: | ---: | ---: |
| 156 | 3 | 1 |
| 342 | 3 | 2 |
| 385 | 3 | 1 |

For each team it reads the enemy-team table from `DATA`, resolves enemy sprite ids through the `SSS` object table, copies a decoded `FBP` background from the TF pack into `pal_psram_fbp_background`, maps player sprites from `F`, maps enemy sprites from `ABC`, and maps one `FIRE` effect as read-only `const uint8_t` data.

The smoke also exercises `embedded/pal_rng_cache.c` on large real RNG frames:

| RNG movie | Frame | Frames in movie | Frame bytes | Destination |
| ---: | ---: | ---: | ---: | --- |
| 4 | 0 | 41 | 64,288 | `pal_psram_rng_frame_a` |
| 5 | 0 | 83 | 64,104 | `pal_psram_rng_frame_b` |
| 9 | 0 | 257 | 61,773 | `pal_psram_rng_frame_a` |

These are already decoded by `tools/pal_pack_build.py`; runtime only copies the selected frame into a named PSRAM buffer.

The smoke also exercises `embedded/pal_sfx_cache.c` and `embedded/pal_audio_static.c` on representative large SFX chunks from the TF pack. The host pack builder converts VOC data to 22050Hz mono PCM16 before writing the SFX archive; runtime only copies, validates, and mixes PCM16 samples:

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

The smoke also exercises `embedded/pal_save_cache.c` against real save files in `/mnt/hgfs/deb13/PAL`. It reads each file into the existing `pal_psram_save_state` fixed buffer, checks that no heap-backed `SAVEDGAME_DOS`/`SAVEDGAME_WIN` object is needed, and validates common header fields:

| Save file | Bytes | Saved times | Scene | Cash |
| --- | ---: | ---: | ---: | ---: |
| `1.rpg` | 184,672 | 1 | 1 | 0 |
| `2.rpg` | 188,864 | 8 | 17 | 580 |
| `4.RPG` | 183,488 | 1 | 1 | 899,999 |

The smoke also exercises `embedded/pal_video_static.c`. It preserves a real 320x200 indexed framebuffer through `pal_psram_screen_bak`, uses the 512-byte `pal_video_rgb565` LUT, and converts one line into `pal_sram_display_dma` for RGB565 scanout-style output.

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

The `contract-check` target is the same `tools/embedded_contract_check.py` source/binary/pack gate: no project-side heap hits, no decoder hits, section budgets from `size`/`objdump`, symbol-prefix budgets from `nm -S --size-sort`, no generated-pack runtime flags or YJ1 payloads, and a 16MB NOR pack size limit.

Current result:

```text
source heap hits: 0
source decompress hits: 0
text=19198 data=720 bss=7197792
pal_sram_ total=182784 limit=307200
pal_psram_ total=7008208 limit=8388608
pal_scene_ total=4736 limit=8192
pal_battle_ total=334 limit=2048
pal_sfx_ total=384 limit=4096
pal_audio_ total=0 limit=4096
pal_global_ total=232 limit=4096
pal_save_ total=512 limit=4096
pal_video_ total=512 limit=4096
pal_ui_ total=0 limit=4096
pal_music_ total=0 limit=4096

pack /tmp/pal_nor_default.pak:
size=10446724 chunks=1401 payload=10422642 max-size=16777216
formats NATIVE=1399 TEXT_UTF16=1 FONT_GLYPHS=1

pack /tmp/pal_tf_default.pak:
size=49309874 chunks=1088 payload=49291679
formats NATIVE=800 RNG_FRAMES=12 SFX_PCM16=276
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

## Current Contract Failures

Source scan after stripping C comments:

- Heap hits: 300
- Decompression hits: 49

Binary scan of `unix/sdlpal`:

```text
text    3165745
data    2287010
bss     1179232
```

Important forbidden symbols still present:

```text
Decompress
PAL_MKFDecompressChunk
PAL_MKFGetDecompressedSize
UTIL_calloc
UTIL_malloc
YJ1_Decompress
YJ2_Decompress
calloc@GLIBC_2.2.5
free@GLIBC_2.2.5
malloc@GLIBC_2.2.5
realloc@GLIBC_2.2.5
```

The large `.data` footprint is expected from the current desktop build and is not target-acceptable. Major contributors include the existing full font tables and codec/audio support.

## Next Engineering Cuts

1. Add a dedicated native embedded-contract build profile instead of overloading the full desktop Unix build.
2. Exclude MP3/OGG/OPUS/AVI/TinySoundFont/Timidity/high-quality resampler from that profile.
3. Replace `PAL_MKFDecompressChunk`, `PAL_MKFGetDecompressedSize`, and `Decompress` use with a raw/native resource-pack API.
4. Wire the generated raw/native packs into the full engine resource path instead of only the embedded smoke slices.
5. Replace heap-backed scene, battle, save/load, audio, and temporary buffers with normal named static SRAM/PSRAM arrays.
6. Keep running `tools/embedded_contract_check.py` after each cut until source and binary checks pass.
