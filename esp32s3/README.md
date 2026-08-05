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
```

This command generates data only; it does not compile C code and does not need
ESP-IDF. The output includes `PALSET.BIN`,
`chapter_manifest.json`, `pal_core.pak`, the complete portable
`pal_full.pak`, and `b00.pak` through `b14.pak`. Obsolete `pal_tf.pak` and
`pal_l2.pak` files are removed from the output directory.

The audited default input is `/mnt/hgfs/deb13/PALSteam/PAL_DOS`. Do not mix its
scripts, text, or saves with the former `/mnt/hgfs/deb13/PAL` data set. The
following is therefore the local default equivalent of `/path/to/PAL`:

```text
/mnt/hgfs/deb13/PALSteam/PAL_DOS
```

`make -C esp32s3 tf-datapack` remains a generation-plus-verification shortcut;
unlike the Python command above, its verification dependencies compile
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
`pal_full.pak` is the only TF payload pack and the complete portable
host-decoded/native resource source. `pal_core.pak` and `bNN.pak` are
Cardputer TF-to-NOR cache sources with the same pack-set ID; they may duplicate
full-pack chunks but never define data completeness. The pack-set ID is
derived only from the complete data, so changing NOR placement does not
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

Generate the complete ready-to-copy directory with the Python command at
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
The chapter-cache layer is the sole NOR-cache payload integrity owner: it retains
the TF-to-NOR SHA-256 checks and NOR readback checks for core and bundles. Once
those pass, the pack provider performs only structural, bounds, ownership, and
pack-set checks; it does not repeat a whole-pack CRC over core, an overlay, or
`pal_full.pak`. The CRC fields remain part of the pack and are checked by the
host generation/provisioning gates.
The Cardputer maps the bounded `pal_full.pak` TOC stored as CACHE#1 inside the
SHA-verified `pal_core.pak` in SPI NOR. At boot it reads only the 32-byte
`pal_full.pak` header and requires it to match the cached TOC header; it neither
copies the full TOC into SRAM nor scans the 55MB payload. NOR core/overlay
chunks take precedence over their duplicates, while FBP/RNG and any uncached
chunk stream directly from the complete pack. This is not swap.

### Fixed chapter-bundle assignment

The default cache uses a fixed, route-aware, non-contiguous scene assignment.
Numeric scene IDs are not story order, so contiguous numeric intervals are not
an acceptable substitute for this table. The executable policy lives in
`SCENE_BUNDLE_RANGES` in `tools/pal_chapter_pack_build.py`; generation writes
the exact ranges to `chapter_manifest.json`, and the chapter-cache checker
compares both the manifest and binary scene table against that policy.

| Bundle | Exact scene IDs | Route grouping |
| --- | --- | --- |
| `b00` | `001-020` | Shengyu, Ten-Mile Slope, Fairy Island |
| `b01` | `021-038` | Suzhou and Lin Family Fort |
| `b02` | `039-047,101-104,106-107,109-113` | Hidden Dragon Cave, Toad Mountain, Jiangnan water route |
| `b03` | `048-058,077-078,080,121` | White River, Jade Buddha Temple, one-way state scenes |
| `b04` | `059-076` | General's Tomb and Ghost Mountain |
| `b05` | `079,081-099,105` | Yangzhou, including scene 105 east outskirts |
| `b06` | `100,108,114-120,122-137` | Capital and Minister's Residence, including local room states |
| `b07` | `138-143,215-226` | Butterfly story and Trial Cave |
| `b08` | `150-151,155,157-164,172-174,176-177,193-199` | Shushan, tower perimeter, post-tower story |
| `b09` | `144-149,152-154,156,165-171` | Complete Locking Demon Tower interior |
| `b10` | `175,178-192` | Mount Ling, Divine Wood Forest, Peach Blossom region |
| `b11` | `200,202-214,259-260,263-273` | Dali physical maps before and after invasion |
| `b12` | `201,227-246` | Dream return and ten-years-earlier Nanzhao |
| `b13` | `247-258,261-262,274-276` | Ten-years-earlier Shengyu and Rain sequences |
| `b14` | `277-299` | Final Nanzhao and catalog sentinel/padding rows |

For the pinned DOS data, scene 294 is the source sentinel and scenes 295-299
are normalized catalog padding; all are deterministically assigned to `b14`.
They add no gameplay payload. Scene 121 is deliberately kept as a one-shot
state outside `b06` to retain useful soft-cap headroom; local capital room
states 125, 126, and 133 remain in `b06` so entering and leaving those rooms
does not install another bundle.

The conservative state-unioned script graph still has nine reversible
cross-bundle boundaries. They are retained region or story boundaries, not
high-frequency room edges:

| Bundle boundary | Scene pairs |
| --- | --- |
| `b02` / `b05` | `102<->105`, `113<->105` |
| `b03` / `b04` | `054<->061`, `055<->070` |
| `b03` / `b05` | `080<->083` |
| `b06` / `b07` | `117<->139` |
| `b08` / `b10` | `176<->186` |
| `b10` / `b11` | `179<->202` |
| `b11` / `b07` | `214<->215` |

The policy intentionally keeps high-frequency local movement and the complete
tower interior together; it does not claim that every graph edge stays inside
one bundle. Changing this cache assignment does not change the portable
pack-set ID. A new `PALSET.BIN` core hash and catalog CRC invalidate stale NOR
core and overlay commits even when the complete-data identity is unchanged.

Only standard `N.rpg` slots are durable game state. The LEVEL1 runtime keeps
three event pages over session-only `EVENT.WRK`; startup ignores stale work,
New Game or Load Game overwrites the logical image, and dirty pages reach it
only through normal LRU eviction. Save streams live records directly into
`N.rpg`, and shutdown does not flush the work file. See
[`../embedded/EVENT_STATE.md`](../embedded/EVENT_STATE.md).

Run the primary gate with:

```sh
make -C esp32s3 cardputer-adv-music-check
```

The older `cardputer-extreme-*` targets are regression profiles, not the
default installation workflow. The bundle assignment above is fixed and
checker-enforced, but generated resource closure remains a broad-coverage
candidate; only a deterministic natural route can prove story completeness.
Physical acceptance must still cover LCD, keyboard, TF, save/reload, music,
and the intended route.

## Xueersi Xiaomiao: SD-only MEM_LEVEL2 profile

This target is the classic ESP32-WROVER-B Xiaomiao, not ESP32-S3:

- conservative 4MB-compatible flash partitioning with no PAL data partition;
- 8MB physical PSRAM, while named buffers stay inside the 4MB mapped window;
- `MEM_LEVEL2` plus `PAL_STORAGE_SD_ONLY`;
- ST7735 native landscape 160x128 LCD;
- LCD and SD share VSPI; GPIO19 is LCD reset during initialization and then SD
  MISO, while GPIO34/35 remain input-only keys;
- fixed RIX/OPL2 music produces 22.05kHz mono PCM16; a saturating 3x output
  gain compensates for the quiet passive buzzer, and an 11-bit LEDC PWM
  channel drives it on GPIO14 while GPTimer updates its duty
  once per sample from a fixed four-tick ring. MIDI, VOC, and SFX remain
  excluded.

Xiaomiao uses the same `esp32s3/TF_datapak/` directory as the Cardputer target.
It indexes `pal_full.pak` directly and reconstructs its selected long-lived
resident view from that file into a fixed 2MB PSRAM owner at boot. No second
payload pack is stored on TF. The resident view includes the complete MUS
archive so the audio task never reads SD; other gameplay resources remain
bounded SD reads into their lifecycle owners. No PAL data is stored in
internal flash.

The Xiaomiao application is data-set agnostic: TF is its only PAL-data source,
and no generated data-set digest is compiled into or compared by the firmware.
Boot validates the pack structure, bounded TOC, and 10px-font geometry; it
does not hash or scan the `pal_full.pak` payload. A different structurally
compatible complete pack can therefore replace it without rebuilding the
application. Host-side generation and the `xiaomiao-prepare-tf` copy/compare
step own full-file integrity verification.

Its complete mutable event table is resident in fixed PSRAM. It never opens
`EVENT.WRK`; New Game and Load Game populate the array directly, and standard
`N.rpg` remains portable between compatible LEVEL1 and LEVEL2 builds. Save
writes use a narrowly guarded classic-ESP32-plus-`MEM_LEVEL2` workaround: one
fixed 4KB internal-DMA staging buffer because that SPI DMA engine cannot
transmit the Level2 PSRAM save image directly. ESP32-S3 and Level1 builds keep
the normal direct FatFS path.

Shared two-screen engine code selects framebuffer storage through
`pal_target_memory.h`; compile-time guards require that storage geometry match
the selected LCD. On Xiaomiao, controlled fatal errors stop audio and the game
task, then draw a black Guru Meditation screen directly through the fixed LCD
DMA strip. Its const 5x7 font shows the source file/line and application Git
revision without depending on TF, FONT10, the game framebuffer, or an
allocator. Uncontrolled CPU exceptions retain ESP-IDF's serial panic report
and halt instead of rebooting.

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

`xiaomiao-check` builds the ESP32 artifact, captures real host map/battle
frames at 160x128, and audits the compiled LEDC/GPTimer path. It does not prove
physical LCD orientation, shared-bus stability, keys, save/reload, passive-
buzzer sound quality, or a natural full-story route; those remain hardware
acceptance work.

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
