# ESP32-family ports

This file owns board, storage, build, and flash instructions. Shared embedded
rules and focused rendering/test documents are indexed in
[`../embedded/README.md`](../embedded/README.md).

The default PAL data set is `/mnt/hgfs/deb13/PALSteam/PAL_DOS`. Do not mix its
scripts, text, or saves with the former `/mnt/hgfs/deb13/PAL` data set. Current
checker output, generated manifests, and ELF/map files are authoritative for
sizes; prose measurements are intentionally omitted.

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

Generate a complete ready-to-copy TF directory:

```sh
make -C esp32s3 \
  PAL_DATA_DIR=/path/to/PAL \
  cardputer-adv-music-tf
```

`PAL_DATA_DIR` may be omitted for the default audited data set. Output is
written to `esp32s3/TF_datapak/` and contains `PALSET.BIN`, `pal_core.pak`,
`pal_tf.pak`, `pal_full.pak`, `EVENT.DEF`, `b00.pak` through `b14.pak`, and the
audit manifest. Copy one complete generated set to the TF card.

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
`pal_full.pak` is an offline-complete mirror, not swap or a runtime fallback.

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

The generated `esp32s3/TF_xiaomiao/` directory contains `pal_core.pak`,
`pal_sd.pak`, `EVENT.DEF`, and `MANIFEST.JSON`. The core is copied once into a
fixed PSRAM owner; gameplay resources remain bounded SD reads into their
lifecycle owners.

```sh
make -C esp32s3 PAL_DATA_DIR=/path/to/PAL xiaomiao-tf
make -C esp32s3 xiaomiao-check
make -C esp32s3 TF_MOUNT=/media/$USER/PALTF xiaomiao-prepare-tf
make -C esp32s3 PORT=/dev/ttyUSB0 xiaomiao-flash
make -C esp32s3 PORT=/dev/ttyUSB0 xiaomiao-flash-app
```

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
