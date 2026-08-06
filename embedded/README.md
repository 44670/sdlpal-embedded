# Embedded port documentation

Use these documents as the current source of truth:

- [`../esp32s3/README.md`](../esp32s3/README.md): boards, memory/storage
  profiles, data-pack generation, build, and flash commands.
- [`../nds/README.md`](../nds/README.md): the self-contained Nintendo DS port
  and its DeSmuME SDL/WebSocket acceptance path.
- [`RESPONSIVE_RENDERING.md`](RESPONSIVE_RENDERING.md): native-framebuffer
  rendering, map viewports, text, and per-element UI rules.
- [`FULLSCREEN_ASSET_RENDERING.md`](FULLSCREEN_ASSET_RENDERING.md): the narrow
  FBP/RNG full-canvas transform and its memory-safe streaming paths.
- [`EVENT_STATE.md`](EVENT_STATE.md): standard saves plus the LEVEL1 session
  pager and LEVEL2 resident event-state contracts.
- [`UI_REVIEW_SOP.md`](UI_REVIEW_SOP.md): human review order and visual
  acceptance rules.
- [`../unix/WEBSOCKET_HARNESS.md`](../unix/WEBSOCKET_HARNESS.md): repeatable
  host-side gameplay control and capture.

`AGENTS.md` contains only repository-wide invariants and navigation. Measured
bytes belong in the current checker, ELF/map output, or generated manifest;
do not copy them into several Markdown files.

The reports under `Consult/` are retired historical consultations. Git history
preserves their former contents, but they are not implementation guidance.

## Main gates

```sh
make -C embedded contract-check
make -C unix EMBEDDED_CONTRACT=1 contract-check
make -C esp32s3 cardputer-adv-music-check
make -C esp32s3 xiaomiao-check
make -C nds check
```

Use the narrower commands linked from the focused document while iterating.
