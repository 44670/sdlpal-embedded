# Real PAL Data Audit

Data path audited:

```text
/mnt/hgfs/deb13/PAL
```

Generated with:

```sh
python3 -B Consult/pal_data_audit.py /mnt/hgfs/deb13/PAL --top 10
```

This report is intended to complement `Consult/Q1.md`. It records measurements from the actual local data set, not only source-level estimates.

## Structured Data

- Scenes: 300
- Event objects: 5,369
- DOS object records: 589
- Script entries: 42,494
- Global allocated table bytes in the current code path: 532,066
- Static embedded global-data mutable bytes: 182,176 in `pal_psram_save_state`
- `DATA.MKF #9` UI sprite bytes: 25,532
- `DATA.MKF #10` battle effect sprite bytes: 17,478

## Worst Normal Scene Residency

This table estimates map tile table + current map `GOP.MKF` chunk + event-object `MGO.MKF` sprites. It excludes framebuffers, player sprites, text/font, audio, globals, allocator overhead, and temporary decompression buffers.

`current resident` matches the current loader behavior, which loads every event-object sprite reference separately. `dedup resident` assumes sprites are loaded once per unique sprite number for the scene.

| scene | map | events | sprite refs | unique sprites | all sprite bytes | dedup sprite bytes | savings | current resident | dedup resident |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 156 | 156 | 130 | 123 | 10 | 811,764 | 45,928 | 765,836 | 908,882 | 143,046 |
| 59 | 76 | 142 | 122 | 11 | 656,486 | 58,686 | 597,800 | 776,600 | 178,800 |
| 65 | 57 | 120 | 91 | 8 | 493,766 | 61,900 | 431,866 | 605,940 | 174,074 |
| 260 | 163 | 72 | 58 | 11 | 328,260 | 42,228 | 286,032 | 458,970 | 172,938 |
| 60 | 55 | 74 | 56 | 7 | 352,178 | 41,994 | 310,184 | 452,180 | 141,996 |
| 200 | 187 | 51 | 43 | 5 | 303,042 | 28,990 | 274,052 | 433,808 | 159,756 |
| 212 | 186 | 40 | 38 | 2 | 291,396 | 10,316 | 281,080 | 421,926 | 140,846 |
| 167 | 145 | 51 | 50 | 6 | 303,746 | 32,782 | 270,964 | 417,620 | 146,656 |
| 292 | 191 | 54 | 52 | 7 | 308,870 | 49,732 | 259,138 | 399,826 | 140,688 |
| 288 | 206 | 47 | 45 | 6 | 249,204 | 34,402 | 214,802 | 341,462 | 126,660 |

Main conclusion: scene-sprite deduplication is a first-order memory reduction. The current worst measured scene drops from about 909KB to about 143KB for this subset of resources if repeated event-object sprite numbers share one decoded sprite.

## Worst Battle Enemy Sprite Residency

This table only covers enemy `ABC.MKF` sprite residency. It excludes player sprites, battle background, battle scene buffers, effects, audio, and temporary decompression buffers.

| team | enemy refs | unique enemy sprites | all sprite bytes | dedup sprite bytes | savings | enemy ids |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 385 | 3 | 1 | 92,556 | 30,852 | 61,704 | `[68, 68, 68]` |
| 156 | 3 | 1 | 84,156 | 28,052 | 56,104 | `[21, 21, 21]` |
| 342 | 3 | 2 | 78,432 | 54,642 | 23,790 | `[120, 68, 120]` |
| 346 | 3 | 2 | 70,448 | 50,650 | 19,798 | `[140, 68, 140]` |
| 368 | 3 | 2 | 67,496 | 49,174 | 18,322 | `[94, 68, 94]` |
| 367 | 3 | 2 | 66,408 | 48,630 | 17,778 | `[92, 68, 92]` |
| 218 | 3 | 1 | 65,916 | 21,972 | 43,944 | `[113, 113, 113]` |
| 344 | 3 | 3 | 64,876 | 64,876 | 0 | `[140, 68, 139]` |
| 381 | 4 | 4 | 63,018 | 63,018 | 0 | `[138, 153, 94, 92]` |
| 347 | 2 | 1 | 61,704 | 30,852 | 30,852 | `[68, 68]` |

Main conclusion: battle enemy sprite deduplication is smaller than scene deduplication, but still worthwhile and simple.

## RNG Movie Frame Stats

RNG movie chunks are raw outer streams containing compressed frames. The current `PAL_RNGPlay()` two-buffer model is consistent with the data: decompressed frames peak around 64KB.

| movie | outer bytes | frames | max frame comp | max frame decomp | total frame comp | total frame decomp |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 9 | 1,268,712 | 257 | 40,118 | 63,854 | 1,267,680 | 2,111,022 |
| 5 | 357,088 | 83 | 34,344 | 64,104 | 356,752 | 591,128 |
| 4 | 192,390 | 41 | 27,992 | 64,288 | 192,222 | 335,777 |
| 11 | 134,920 | 181 | 25,274 | 60,317 | 134,192 | 275,583 |
| 0 | 92,718 | 65 | 22,272 | 58,577 | 92,454 | 165,494 |
| 1 | 681,518 | 411 | 22,104 | 38,729 | 679,870 | 974,037 |
| 2 | 114,798 | 94 | 15,236 | 29,350 | 114,418 | 134,068 |
| 7 | 654,096 | 71 | 13,874 | 29,363 | 653,808 | 1,096,727 |
| 3 | 764,646 | 141 | 13,412 | 21,780 | 764,078 | 1,141,303 |
| 10 | 29,988 | 43 | 11,850 | 22,832 | 29,812 | 44,399 |
| 8 | 56,678 | 34 | 7,802 | 17,231 | 56,538 | 81,370 |
| 6 | 198,470 | 55 | 6,162 | 11,019 | 198,246 | 350,817 |

Total predecoded RNG frame payload across all movies is about 7.3MB. That is too large to treat as an automatic NOR resident asset, but may be acceptable as a TF-side generated cache or as selective hot content.

## Text and Glyphs

- Best decoding among tested encodings: `cp950`
- Replacement character counts: `cp950=0`, `big5=1`, `gbk=41294`
- `WORD.DAT` entries: 589
- Non-empty `WORD.DAT` entries: 587
- Message entries: 10,495
- Unique characters used by `WORD.DAT` + `M.MSG`: 2,631
- Unique non-space characters: 2,630
- Estimated 1bpp 16x16 glyph payload at 32 bytes per glyph: 84,192 bytes
- Generated TEXT archive payload: 254,762 bytes, with 210,386 bytes of UTF-16LE text and offset tables for 589 words plus 10,495 messages
- Most frequent characters: `．`, `，`, `的`, `我`, `！`, `不`, `是`, `了`, `你`, `？`, `這`, `一`

Main conclusion: the current 2MB `unicode_font[65536][32]` table is unnecessary for this data set if the port can build a compact CP950/codepoint glyph subset.

## Largest Single Runtime Chunks

- `MGO.MKF #571`: compressed 33,944 bytes, runtime 59,516 bytes, YJ1
- `ABC.MKF #159`: compressed 33,944 bytes, runtime 59,516 bytes, YJ1
- `F.MKF #13`: compressed 34,060 bytes, runtime 58,116 bytes, YJ1
- `FIRE.MKF #37`: compressed 36,734 bytes, runtime 65,502 bytes, YJ1

These sizes are relevant for a shared decompression scratch buffer and for deciding which assets can be directly decoded into their final PSRAM/NOR-cache destination.

## Audio and Video Files

- Loose `.ogg`, `.opus`, `.mp3`, `.wav`, `.mid`, and `.avi` files found: none
- Present audio containers:
  - `MIDI.MKF`: 762,442 bytes
  - `MUS.MKF`: 331,284 bytes
  - `VOC.MKF`: 1,997,044 bytes
- Largest `VOC.MKF` chunks measured in the generated TF pack:
  - #213: 52,006 bytes
  - #272: 50,954 bytes
  - #255: 38,334 bytes
  - #214: 37,702 bytes
  - #192: 33,768 bytes
- The static SFX smoke banks VOC chunks `{1, 62, 192, 213, 214, 255, 272}` into `pal_psram_sfx_bank`; this checked subset uses 242,926 bytes after 4-byte alignment.

Main conclusion: for this data set, OGG/OPUS/MP3/loose-WAV/AVI support is not needed for base gameplay. The relevant audio formats are the PAL MKF containers, especially RIX/MUS-style music and VOC sound effects.
