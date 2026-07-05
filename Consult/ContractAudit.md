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
embedded/pal_global_cache.c
embedded/pal_global_cache.h
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

## Resource Pack Builder

The host-side pack builder decodes YJ1 chunks before writing runtime packs:

```sh
python3 -B tools/pal_pack_build.py \
  /mnt/hgfs/deb13/PAL \
  --out-nor /tmp/pal_nor_default.pak \
  --out-tf /tmp/pal_tf_default.pak
```

Current default pack sizes from the audited data:

```text
/tmp/pal_nor_default.pak: 10,103,472 bytes
/tmp/pal_tf_default.pak: 40,069,156 bytes
```

Default NOR archives:

```text
ABC,BALL,DATA,F,FIRE,MGO,MIDI,MUS,PAT,RGM,SSS
```

Default TF archives:

```text
FBP,GOP,MAP,RNG,VOC
```

`MAP`, `FBP`, `MGO`, `ABC`, `F`, `FIRE`, and RNG frames are decoded by the host tool. `GOP` and most audio/data chunks are already raw/native and are copied as raw chunks.

The generated packs can also be checked with the C runtime reader through the host-side mmap checker:

```sh
cc -std=c99 -Wall -Wextra -Werror -O2 \
  -Iembedded embedded/pal_pack.c tools/pal_pack_check.c \
  -o /tmp/pal_pack_check
/tmp/pal_pack_check /tmp/pal_nor_default.pak /tmp/pal_tf_default.pak
```

Current C-reader summary:

```text
/tmp/pal_nor_default.pak: size=10103472
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
  archives=11 payload=10079448
/tmp/pal_tf_default.pak: size=40069156
  FBP  chunks=   72 payload=4608000
  GOP  chunks=  226 payload=11529414
  MAP  chunks=  226 payload=14614528
  RNG  chunks=   12 payload=7307725
  VOC  chunks=  276 payload=1995936
  archives=5 payload=40055603
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
embedded/pal_global_cache.c
embedded/pal_global_cache.h
```

Properties:

- maps pack payloads as `const uint8_t`,
- copies TF-style raw chunks into caller-supplied `uint8_t` buffers,
- uses no `malloc`, `calloc`, `realloc`, or `free`,
- contains no decompression path,
- rejects chunks flagged as compressed.

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

It maps a `const uint8_t` native pack chunk, fills `pal_sram_framebuffer`, and wraps that exact buffer with `SDL_CreateRGBSurfaceFrom()`. SDL may allocate internally; the project-side smoke code does not use heap allocation and contains no decoder path.

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
  --max-symbol-prefix pal_psram_=8388608
```

Current result:

```text
source heap hits: 0
source decompress hits: 0
text=4155 data=624 bss=182792
pal_sram_ total=182784 limit=307200
pal_psram_ total=0 limit=8388608
PASS
```

`pal_psram_` is zero in this binary because the SDL smoke only exercises the framebuffer path; `pal_memory_smoke` is the artifact that forces all declared PSRAM buffers into the ELF for symbol-budget verification.

## Native SDL Real-Data Smoke

The embedded makefile also builds a native SDL2 smoke binary that opens the generated pack files from the audited data set:

```text
embedded/build/pal_realdata_sdl_smoke
```

It maps both generated packs read-only, checks real archive counts, maps representative NOR chunks as `const uint8_t`, copies real TF chunks into `pal_sram_framebuffer`, `pal_psram_map_tiles`, `pal_psram_gop_copy`, and `pal_psram_sfx_bank`, then wraps `pal_sram_framebuffer` with SDL. There is no project-side heap allocation and no decoder path in this binary.

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

The smoke also exercises `embedded/pal_sfx_cache.c` on representative large VOC chunks from the TF pack:

| VOC chunk | Bytes |
| ---: | ---: |
| 1 | 1,748 |
| 62 | 28,406 |
| 192 | 33,768 |
| 213 | 52,006 |
| 214 | 37,702 |
| 255 | 38,334 |
| 272 | 50,954 |

The bank copies these chunks into `pal_psram_sfx_bank` with 4-byte alignment and uses 242,926 bytes total for this checked set.

The smoke also exercises `embedded/pal_global_cache.c` against real SSS/DATA tables. It copies mutable default state into `pal_psram_save_state`:

| Data | Records | Bytes |
| --- | ---: | ---: |
| Event objects | 5,369 | 171,808 |
| Scenes | 300 | 2,400 |
| DOS object table | 589 | 7,068 |
| Player roles | 1 | 900 |

With 4-byte alignment this uses 182,176 bytes of the 640KB save-state PSRAM buffer. Scripts, stores, enemies, enemy teams, magic, battlefields, level-up magic, battle-effect indexes, enemy positions, and level-up EXP remain `const uint8_t *` views into the NOR pack.

Build packs and run the real-data smoke:

```sh
python3 -B tools/pal_pack_build.py \
  /mnt/hgfs/deb13/PAL \
  --out-nor /tmp/pal_nor_default.pak \
  --out-tf /tmp/pal_tf_default.pak
make -C embedded realdata-check
```

Verify the artifact:

```sh
python3 -B tools/embedded_contract_check.py \
  --root embedded \
  --fail-on-source \
  --binary embedded/build/pal_realdata_sdl_smoke \
  --max text=65536 \
  --max data=4096 \
  --max bss=7200000 \
  --max-symbol-prefix pal_sram_=307200 \
  --max-symbol-prefix pal_psram_=8388608 \
  --max-symbol-prefix pal_scene_=8192 \
  --max-symbol-prefix pal_battle_=2048 \
  --max-symbol-prefix pal_sfx_=4096 \
  --max-symbol-prefix pal_global_=4096
```

Current result:

```text
source heap hits: 0
source decompress hits: 0
text=11826 data=704 bss=7196744
pal_sram_ total=182784 limit=307200
pal_psram_ total=7008208 limit=8388608
pal_scene_ total=4736 limit=8192
pal_battle_ total=334 limit=2048
pal_sfx_ total=384 limit=4096
pal_global_ total=232 limit=4096
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
4. Add host-side pack generation for decoded MAP/FBP/MGO/ABC/F/FIRE/RNG payloads.
5. Replace heap-backed scene, battle, text, font, save/load, audio, and temporary buffers with normal named static SRAM/PSRAM arrays.
6. Keep running `tools/embedded_contract_check.py` after each cut until source and binary checks pass.
