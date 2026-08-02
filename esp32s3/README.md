# ESP32-family ports

This file owns board, storage, build, and flash instructions. Shared embedded
rules and focused rendering/test documents are indexed in
[`../embedded/README.md`](../embedded/README.md).

## Build the shared TF data pack

Every ESP32-family port uses the same complete `esp32s3/TF_datapak/`
directory. This includes Cardputer ADV, CoreS3 SE, and Xueersi Xiaomiao.
Board-specific files in that directory are optional caches; they do not make
the TF data set board-specific, and a board may simply ignore caches it does
not use. Copy the whole directory contents to any target's TF card.

Data-pack generation requires Python 3.10 or newer. It uses only the Python
standard library: there are no PyPI dependencies and no `pip install` command
is required. In particular, Pillow, NumPy, fontTools, and compression packages
must not be installed merely to build the pack. The pinned Fusion Pixel Font
archive is already vendored in the repository.

From the repository root, build the complete shared data set directly with
Python:

```sh
python3 -B tools/pal_chapter_pack_build.py /path/to/PAL \
  --out-dir esp32s3/TF_datapak \
  --manifest esp32s3/TF_datapak/chapter_manifest.json \
  --font10-archive \
    third_party/fusion-pixel-font/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip

python3 -B tools/pal_event_template_build.py \
  --full-pack esp32s3/TF_datapak/pal_full.pak \
  --out esp32s3/TF_datapak/EVENT.DEF
```

These commands generate data only; they do not compile C code and do not need
ESP-IDF. The output includes `PALSET.BIN`, `EVENT.DEF`,
`chapter_manifest.json`, `pal_core.pak`, `pal_tf.pak`, the complete portable
`pal_full.pak`, `pal_l2.pak`, and `b00.pak` through `b14.pak`.

The audited default input is `/mnt/hgfs/deb13/PALSteam/PAL_DOS`. Do not mix its
scripts, text, or saves with the former `/mnt/hgfs/deb13/PAL` data set. The
following is therefore the local default equivalent of `/path/to/PAL`:

```text
/mnt/hgfs/deb13/PALSteam/PAL_DOS
```

`make -C esp32s3 tf-datapack` remains a generation-plus-verification shortcut;
unlike the two Python commands above, its verification dependencies compile
and run small host-side C tests. Current checker output, generated manifests,
and ELF/map files are authoritative for sizes; prose measurements are
intentionally omitted.

## Common architecture

Memory ownership, board wiring, display geometry, and storage topology are
independent compile-time choices:

| Profile | Intended memory | Resource ownership |
| --- | --- | --- |
| `MEM_LEVEL1` | about 500KB SRAM, no PSRAM | Persistent validated NOR views plus bounded TF streaming; no arena. |
| `MEM_LEVEL2` | at least 4MB directly mapped PSRAM | Fixed lifecycle-scoped scene, player, and battle arenas; no general allocator. |

All target profiles prohibit project heap allocation and runtime asset
decompression. `PAL_STORAGE_SD_ONLY` selects an SD-backed provider; it is not a
board or memory-profile name. The retired `PAL_CARDPUTER_EXTREME` resource
policy macro must not be restored.

Every port uses the shared `esp32s3/TF_datapak/` directory generated above.
`pal_full.pak` is the complete portable host-decoded/native resource file.
Other packs in the directory are optional, target-specific cache images with
the same pack-set ID; they may duplicate full-pack chunks but may not become a
second source of data completeness. Small-memory targets can index those
bounded caches while still shipping the complete file on TF. The pack-set ID
is derived only from the complete data, so changing a cache layout does not
change `pal_full.pak`.

The `cardputer-adv-music-tf` and `xiaomiao-tf` make targets remain compatibility
aliases for the generation-plus-verification shortcut.

Small-screen rendering is defined by:

- [`../embedded/RESPONSIVE_RENDERING.md`](../embedded/RESPONSIVE_RENDERING.md)
  for maps, text, and case-by-case UI/material layout;
- [`../embedded/FULLSCREEN_ASSET_RENDERING.md`](../embedded/FULLSCREEN_ASSET_RENDERING.md)
  for the FBP/RNG-only full-canvas transform;
- [`../embedded/UI_REVIEW_SOP.md`](../embedded/UI_REVIEW_SOP.md) for visual
  acceptance.

## Cardputer ADV: default 8MB/no-PSRAM profile

The primary target is M5Stack Cardputer ADV K132-Adv, not the original
Cardputer:

- ESP32-S3, 8MB flash, no PSRAM, `MEM_LEVEL1`;
- ST7789 native 240x135 LCD on SPI3;
- TF on independent SPI2 at 20MHz;
- two fixed 240x135 indexed screens and one 4KB RGB565 DMA strip;
- RIX/OPL2 music enabled by default; MIDI, VOC, and SFX excluded.

### Build, data, and install

Generate the complete ready-to-copy directory with the two Python commands at
the start of this document. Copy one complete generated set to the TF card;
do not assemble files from different generation runs.

Provision the partition table once, then use app-only updates:

```sh
make -C esp32s3 PORT=/dev/ttyACM0 cardputer-adv-music-provision
make -C esp32s3 PORT=/dev/ttyACM0 cardputer-adv-music-flash-app
```

Discover the actual serial port. Provisioning/flashing and replacing card
contents are external mutations and require explicit user authorization.

The application contains no data-set hash. At boot it reads `PALSET.BIN`,
validates the core NOR cache, and displays a native `LOADING` progress screen
while copying `pal_core.pak` when needed. Scene transitions similarly validate
and install the required `bNN.pak`; the commit record is written last.
The chapter-cache layer is the sole full-payload integrity owner: it retains
the TF-to-NOR SHA-256 checks and NOR readback checks for core and bundles. Once
those pass, the pack provider performs only structural, bounds, ownership, and
pack-set checks; it does not repeat a whole-pack CRC over core, an overlay, or
`pal_tf.pak`. The CRC fields remain part of the pack and are checked by the
host generation/provisioning gates.
The Cardputer profile does not index the large `pal_full.pak` TOC; it uses the
bounded derived packs. The complete file remains on TF for portability and is
not swap.

Event and scene state use the bounded TF journal described in `AGENTS.md`;
three event pages are resident, and `PAL_TFIO` records retain write-pressure
telemetry.

Run the primary gate with:

```sh
make -C esp32s3 cardputer-adv-music-check
```

The older `cardputer-extreme-*` targets are regression profiles, not the
default installation workflow. Generated chapter coverage is a candidate;
only a deterministic natural route can prove story completeness. Physical
acceptance must still cover LCD, keyboard, TF, save/reload, music, and the
intended route.

## Xueersi Xiaomiao: SD-only MEM_LEVEL2 profile

This target is the classic ESP32-WROVER-B Xiaomiao, not ESP32-S3:

- conservative 4MB-compatible flash partitioning with no PAL data partition;
- 8MB physical PSRAM, while named buffers stay inside the 4MB mapped window;
- `MEM_LEVEL2` plus `PAL_STORAGE_SD_ONLY`;
- ST7735 native landscape 160x128 LCD;
- LCD and SD share VSPI; GPIO19 is LCD reset during initialization and then SD
  MISO, while GPIO34/35 remain input-only keys;
- audio disabled in the initial profile.

Xiaomiao uses the same `esp32s3/TF_datapak/` directory as the Cardputer target.
It indexes `pal_full.pak` directly and copies the
optional overlapping `pal_l2.pak` resident-view cache once into its fixed
PSRAM owner. Gameplay resources remain bounded SD reads into their lifecycle
owners; no PAL data is stored in internal flash.

The Xiaomiao application is data-set agnostic: TF is its only PAL-data source,
and no generated data-set digest is compiled into or compared by the firmware.
Boot validates pack version, size, TOC bounds, 10px-font geometry, and agreement
between the two files' data-provided pack-set IDs; it does not hash or scan the
`pal_full.pak` payload. A different compatible pack set generated together can
therefore replace both TF files without rebuilding the application. Host-side
generation and the `xiaomiao-prepare-tf` copy/compare step own full-file
integrity verification.

```sh
make -C esp32s3 xiaomiao-check
make -C esp32s3 TF_MOUNT=/media/$USER/PALTF xiaomiao-prepare-tf
make -C esp32s3 PORT=/dev/ttyUSB0 xiaomiao-flash
make -C esp32s3 PORT=/dev/ttyUSB0 xiaomiao-flash-app
```

Use the `xtensa-esp-elf` toolchain selected by the active ESP-IDF checkout;
install its pinned version with `python $IDF_PATH/tools/idf_tools.py install
xtensa-esp-elf`. The gate also verifies that cache-off flash initialization
cannot call flash-resident `memcpy` or `memset`.

`xiaomiao-check` builds the ESP32 artifact and captures real host map/battle
frames at 160x128. It does not prove physical LCD orientation, shared-bus
stability, keys, save/reload, or a natural full-story route; those remain
hardware acceptance work.

## CoreS3 SE

CoreS3 SE is a separate ESP32-S3 target with 16MB flash, 8MB PSRAM, and
`MEM_LEVEL2`. It is not CoreS3. Its LCD and TF share SPI signals: GPIO35 is
both TF MISO and LCD D/C, so the existing direction handoff is mandatory.
Audio/SFX remain excluded.

Build and verify the full original-engine host:

```sh
make -C esp32s3 engine-host-build
make -C esp32s3 engine-port-check
```

The port gate covers native provider/engine parity, save parity, Xtensa link,
ESP-IDF build, pack layout, fixed-memory placement, and the no-heap/no-runtime-
decompression contracts. Prepare TF data and flash only when requested:

```sh
make -C esp32s3 TF_MOUNT=/media/$USER/PALTF prepare-tf
make -C esp32s3 PORT=/dev/ttyACM0 flash-app
make -C esp32s3 PORT=/dev/ttyACM0 TF_MOUNT=/media/$USER/PALTF \
  engine-hardware-smoke
```

`scaffold-build`, `check`, and `native-smoke` retain the older scene-only
hardware bring-up path. They are useful board-glue regressions but are not
evidence for full-engine gameplay.
