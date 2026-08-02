# Retired consultation notes

The former consultation reports in this directory described an earlier
CoreS3-oriented architecture, the obsolete `/mnt/hgfs/deb13/PAL` data set,
and intermediate static/scaffold implementations. They are retired because
their buffer, pack, rendering, and verification claims no longer describe the
active targets. Git history preserves the full reports.

Use the current documentation instead:

- [`../embedded/README.md`](../embedded/README.md): documentation index;
- [`../esp32s3/README.md`](../esp32s3/README.md): boards, profiles, packs, and
  commands;
- [`../embedded/RESPONSIVE_RENDERING.md`](../embedded/RESPONSIVE_RENDERING.md):
  maps and responsive UI;
- [`../embedded/FULLSCREEN_ASSET_RENDERING.md`](../embedded/FULLSCREEN_ASSET_RENDERING.md):
  the FBP/RNG full-canvas exception.

For current measurements, run the relevant checker and inspect its generated
manifest plus ELF/map output.
