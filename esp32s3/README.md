# M5Stack CoreS3 SE Bring-Up

This ESP-IDF project is the first target-side CoreS3 SE port slice. It is intentionally a video/input/NOR-partition bring-up, with no audio.

The board init follows the local walkie-talkie CoreS3 SE reference at `/home/john/work/CardPuterADV/esp-walkie-talkie`: AW9523, AXP2101, FT6336 touch, and SPI LCD init use the same pins and command sequence.

Build:

```sh
source /home/john/esp-idf/export.sh
idf.py -C esp32s3 -B build-cores3se set-target esp32s3 build
```

Repeatable target-side contract check:

```sh
make -C esp32s3 check
```

That builds the ESP-IDF artifact, rebuilds the default resource packs from `/mnt/hgfs/deb13/PAL`, verifies the NOR pack fits the `pal_nor` partition, rejects compressed/YJ1 payloads in the NOR pack, scans the target-side sources for heap/decompress calls, and reports `pal_sram_` / `pal_psram_` symbol totals from the ESP32-S3 ELF.

Flash app:

```sh
idf.py -C esp32s3 -B build-cores3se -p PORT flash monitor
```

The custom partition table assumes 16MB flash and reserves an 11MB read-only data partition named `pal_nor` at `0x310000`. The current generated NOR pack is about 10.45MB, so it fits there.

Flash the generated NOR pack:

```sh
python -m esptool --chip esp32s3 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x310000 /tmp/pal_nor_default.pak
```

The firmware also tries to open a generated TF pack at:

```text
/sdcard/pal_tf.pak
```

When that file is mounted and present, the app reads its pack table of contents into `pal_psram_tf_toc`, then reads `FBP #0` through the file-backed `PalPackToc_CopyRawReadAt()` path into `pal_sram_big_buffer` and uses it as the demo background. Missing TF storage is non-fatal; the app falls back to the synthetic background.

The current firmware draws a 320x200 indexed framebuffer through `embedded/pal_video_static.c`, centered on the 320x240 LCD with 20-pixel black bars. Touch input is polled through FT6336 and marks the framebuffer. This proves the CoreS3 SE LCD/touch path without adding audio or runtime decompression.
