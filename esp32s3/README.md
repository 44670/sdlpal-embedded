# M5Stack CoreS3 SE Bring-Up

This ESP-IDF project is the first target-side CoreS3 SE port slice. It is intentionally a video/input/NOR-partition bring-up, with no audio.

The board init copies/adapts the local walkie-talkie CoreS3 SE reference at `/home/john/work/CardPuterADV/esp-walkie-talkie`: AW9523, AXP2101, FT6336 touch, and SPI LCD init use the same pins and command sequence.

Build:

```sh
source /home/john/esp-idf/export.sh
idf.py -C esp32s3 -B build-cores3se set-target esp32s3 build
```

Repeatable target-side contract check:

```sh
make -C esp32s3 check
```

That builds the ESP-IDF artifact, rebuilds the default resource packs from `/mnt/hgfs/deb13/PAL`, verifies the NOR pack fits the `pal_nor` partition, rejects compressed/YJ1 payloads in both NOR and TF packs, scans the target-side sources and linked project objects for heap/decompress calls, scans the generated `SSS` scene-enter/auto/trigger roots for unsupported bounded branch-aware script opcodes, checks key ELF sections, and reports `pal_sram_` / `pal_psram_` symbol totals from the ESP32-S3 ELF.

Flash app:

```sh
make -C esp32s3 PORT=/dev/ttyACM0 flash-app
```

The custom partition table assumes 16MB flash and reserves an 11MB read-only data partition named `pal_nor` at `0x310000`. The current generated NOR pack is about 10.45MB, so it fits there.

Flash the generated NOR pack:

```sh
make -C esp32s3 PORT=/dev/ttyACM0 flash-nor
```

The firmware also tries to open a generated TF pack at:

```text
/sdcard/pal_tf.pak
```

Copy `/tmp/pal_tf_default.pak` to the card as `pal_tf.pak`. The CoreS3 SE TF slot shares SPI with the LCD: SCLK `GPIO36`, MOSI `GPIO37`, MISO/LCD D/C `GPIO35`, and TF CS `GPIO4`. The firmware mounts the card with ESP-IDF FatFS, uses FatFS paths for TF resource-pack reads and save-file reads/writes, switches the shared D/C-MISO pin to input for TF reads and back to output for LCD flushes, and reads the pack table of contents into `pal_psram_tf_toc`. It maps generated TEXT/FONT/UI/dialog/music/ending/script chunks from the NOR pack as read-only caches, loads menu FBP/image/box assets, battle background/effect scratch, RNG frame data, and ending FBP screens into named PSRAM buffers, then loads default mutable global records into the 256KB `pal_psram_save_state` buffer and loads scenes through the shared static scene cache: decoded `MAP` and `GOP` chunks are copied to `pal_psram_map_tiles` / `pal_psram_gop_copy`, current-scene event and party sprites are copied once from read-only NOR views into the 1MB `pal_psram_sprite_pin` buffer, and scene draw metadata stays in normal static storage. Missing TF storage is non-fatal; the app falls back to the synthetic background.

The target FatFS config keeps only short filename support and disables FatFS dynamic buffers / LFN heap buffers. Runtime TF paths are therefore short names such as `0:/pal_tf.pak` and `0:/1.rpg`, with FAT file objects and sector buffers living in normal static/component storage instead of per-operation LFN heap allocations. When a startup save is loaded, scene changes write the updated scene, viewport, party direction, party/trail positions, and saved-times counter back to the same slot through `PalSaveFatFs_WriteFile()`.

If any `/sdcard/1.rpg` through `/sdcard/5.rpg` save is present, the firmware chooses the slot with the highest saved-times counter, reads it into the existing 256KB static save buffer, and uses its saved scene, viewport, party roles/trail, party leader direction, player-role data, scene table, and event-object table as the startup view. If no save is usable, it falls back to the default mutable global cache.

The current firmware draws a 320x200 indexed framebuffer through `embedded/pal_video_static.c`, centered on the 320x240 LCD with 20-pixel black bars. Touch input is polled through FT6336; holding inside a viewport quadrant moves the party leader by the original 16x8 isometric walking step, with map-tile and blocking event-object collision checks, tapping near the party triggers the facing event through a bounded script subset, and tapping the top or bottom black bar loads the previous or next scene from the TF pack. The bounded scene-enter/auto/trigger-script subset runs on mutable scene, event-object, party, player-role, cash, and inventory records for stop/jump/wait/random branch, nested trigger calls, scene/map transition, palette day/night/palette-number/fade switches, music/battlefield/screen-wave state, party placement/composition/followers/trail sync, basic NPC/party movement, viewport movement, visual/event-pointer/state updates, script dialog overlays, player HP/MP/stat edits, cash changes, equipment removal, learned-magic edits, and item add/remove/count/equipped/full-HP branches. Battle-start scripts load the requested enemy team/battlefield through the static battle cache and render a short indexed-framebuffer preview using TF FBP background data plus NOR F/ABC sprite views; this is not the final turn-based battle loop yet. RNG scripts stream host-predecoded TF RNG frame records through the fixed 65KB PSRAM frame buffers and blit them into the indexed framebuffer without runtime decompression. Script dialog text renders as an indexed-framebuffer overlay using the preconverted UTF-16 text/font caches, and the next touch clears the dialog before movement or scene hot zones run. The audited PAL scene-enter/auto/trigger roots have no first-op unsupported scripts in the bounded scan; audio commands still advance as placeholders while that system is out of scope. This proves the CoreS3 SE LCD/touch/TF/NOR scene path without adding audio or runtime decompression.
