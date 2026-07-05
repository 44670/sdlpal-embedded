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
embedded/pal_pack.c
embedded/pal_pack.h
embedded/pal_pack_smoke.c
embedded/pal_memory.c
embedded/pal_memory.h
embedded/pal_memory_smoke.c
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
/tmp/pal_nor_default.pak: 10,103,468 bytes
/tmp/pal_tf_default.pak: 40,069,152 bytes
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

## Contract Runtime Slice

The first runtime slices are a pack reader and a plain static-buffer declaration unit:

```text
embedded/pal_pack.c
embedded/pal_pack.h
embedded/pal_memory.c
embedded/pal_memory.h
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
text=2211 data=576 bss=8
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
