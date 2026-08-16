# Fusion Pixel Font inputs

`fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip` is the pinned upstream
release described by `tools/pal_ui_layout/fusion_pixel_font.lock.json`.
`OFL.txt` is its SIL Open Font License 1.1.

`docs/sdlpal-font10-pal-dos-v2026.07.20.zip` contains the precise 41,392-byte
FONT10 payload generated from the pinned Traditional Chinese BDF and the
audited stock PAL DOS text corpus plus the explicitly declared target UI
strings. It lives beside the browser page so a published `docs/` tree is
self-contained. The stored subset has SHA-256:

```text
1e97c92de941b2276a2362e34d1f645a5d14b1842b8cfb8b9d7037b2d53481c3
```

The single-member ZIP is 29,746 bytes with SHA-256
`fd1463d62710a88af9eb037052eae32937fdea8b8bfb2b56f116bbfb27c8b453`.
The browser pack builder fetches this compressed native subset instead of
parsing the 18MB upstream font archive in JavaScript. It validates the ZIP,
the exact native digest, binary structure, payload CRC, and coverage of the
selected PAL text. It then copies only those requested fixed glyph records into
the resource pack and recomputes the FONT10 count and CRC.
