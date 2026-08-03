# Event state and saves

The only durable mutable game state is the ordinary DOS-compatible `N.rpg`
save slot. There is no event journal, template file, checkpoint, recovery
transaction, or private save wrapper.

## MEM_LEVEL1

The no-PSRAM profile keeps three 4KB event pages in SRAM. At startup it derives
the event-record count from the native `SSS` chunk 0 byte length. Records are
32 bytes; the current fixed capacity is 5,500 records, but no supported data
set is required to contain exactly that many or match the audited stock count.
`0:/EVENT.WRK` contains the resulting logical table, rounded up to a whole
page; only the unused tail of its last page is zero padding.

The live event state is the union of the resident SRAM pages and the
non-resident pages in `EVENT.WRK`. A dirty bit means that one 32-byte resident
record is newer than its copy in `EVENT.WRK`; it does **not** mean that the
record is absent from the latest `N.rpg`. Each page has a 128-bit dirty-record
bitmap. An unchanged write does not set its bit.

- Engine startup initializes only pager metadata and ignores any old work
  file. It does not delete or truncate it.
- New Game opens or creates the work file and overwrites the complete logical
  image from stock `SSS` event data plus final-page padding.
- Load Game overwrites that same image from the exact event payload in
  `N.rpg` plus final-page padding.
- Current-scene pages are pinned; all other replacement is bounded LRU.
- A dirty page is written to the work file only when normal LRU replacement
  evicts it. The complete 4KB page is written even if only one record changed.
  A failed write retains the resident page and its dirty bits, and the
  requested replacement fails.
- Scene changes, save requests, and shutdown never explicitly flush pages.
  There is no checkpoint operation.
- Shutdown closes the work file and discards resident dirty pages. A later
  launch starts from New Game defaults or a selected `N.rpg`, never from
  `EVENT.WRK`.

### LEVEL1 Save Game

Save Game creates a complete standard PAL snapshot; it does not copy
`EVENT.WRK` and does not store dirty metadata:

1. Open `N.rpg` with `wb` and write the ordinary fixed game state, including
   party, inventory, objects, and all 300 resident scene records.
2. Read event IDs 1 through the runtime-derived record count through the live
   pager and stage up to 4KB at a time into the existing fixed save buffer.
3. Append exactly `record_count * 32` event bytes. Resident dirty records therefore come
   directly from SRAM; non-resident records come from `EVENT.WRK`.

The file has no private header, checksum, dirty bitmap, journal, temporary
companion, or backup slot. Its total length follows the selected PAL data
set's ordinary save geometry.

The sequential snapshot reads can cause ordinary cache misses and LRU
replacement. Consequently, Save Game does not call a work-file flush, but it
can indirectly write `EVENT.WRK` if its normal traversal evicts a dirty page.
A pinned dirty page is read directly into `N.rpg`, remains resident, and stays
dirty relative to `EVENT.WRK`. Saving must not clear such a dirty bit without
first updating the work file, because a later eviction and reload in the same
session would otherwise lose the change.

`N.rpg` is overwritten directly. A power loss or I/O failure during Save Game
may therefore leave that slot incomplete; this is the deliberate consequence
of having no atomic replacement or recovery layer.

All 300 scene records remain in the normal resident global state and in the
fixed part of the standard save; they are not separately paged.

Source of truth:

- `embedded/pal_event_pager.[ch]`
- `esp32s3/engine_bridge/pal_engine_event_state.[ch]`
- `esp32s3/engine_bridge/pal_engine_paged_save.inc`

## MEM_LEVEL2

The PSRAM profile fills its complete fixed event array directly from stock
data or `N.rpg`. It neither links the LEVEL1 pager backend nor accesses
`EVENT.WRK`. Saves use the same standard `N.rpg` format, so save files remain
portable between the two memory profiles when their PAL data is compatible.
