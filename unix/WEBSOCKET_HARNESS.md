# Linux WebSocket gameplay harness

The normal Unix SDL build exposes an optional local control surface for fast,
repeatable gameplay review. It is disabled unless `PAL_WS_PORT` is set, binds
only to `127.0.0.1`, rejects browser `Origin` requests, serves one client at a
time, and executes commands on the game thread.

Rendering expectations live in
[`../embedded/RESPONSIVE_RENDERING.md`](../embedded/RESPONSIVE_RENDERING.md);
human review follows
[`../embedded/UI_REVIEW_SOP.md`](../embedded/UI_REVIEW_SOP.md).

Build and launch from the PAL data directory:

```sh
make -C unix USE_SDL3=0 GENERATED=
cd /path/to/PAL
PAL_WS_PORT=12345 /path/to/sdlpal-embedded/unix/sdlpal
```

Drive the process with the dependency-free Python client:

```sh
python3 -B tools/ws_cli.py status
python3 -B tools/ws_cli.py event 200
python3 -B tools/ws_cli.py screenshot ./tmp_ui/current.png
python3 -B tools/ws_cli.py record ./tmp_ui/sequence --count 40 --interval-ms 50
python3 -B tools/ws_cli.py record ./tmp_ui/battle --battle 0 --battlefield 2 --battle-auto
python3 -B tools/ws_cli.py input enter down enter
python3 -B tools/ws_cli.py scene 7 --x 1312 --y 288
python3 -B tools/ws_cli.py load 1
python3 -B tools/ws_cli.py script 7739 --event 113
python3 -B tools/ws_cli.py shop 0
python3 -B tools/ws_cli.py battle 0 --battlefield 3
python3 -B tools/ws_cli.py battle-items
python3 -B tools/ws_cli.py ui status
python3 -B tools/ws_cli.py ui sell
python3 -B tools/ws_cli.py review-fixture
python3 -B tools/ws_cli.py review-next
python3 -B tools/ws_cli.py sop-capture ./tmp_ui/review_160x128
```

`record` writes a bounded PNG sequence plus per-frame `state.jsonl`; `--battle`
starts the battle on that connection so short-lived states are not lost to
reconnect latency. `--battlefield` selects the real FBP battle background.
`input` sends ordinary PAL key transitions; a tap is a bounded down/up pair.
`scene` changes only an already-running field game and accepts
coordinates only as a pair. `battle` enters the real `PAL_StartBattle()` loop,
and the server remains responsive inside that loop for input, state queries,
and screenshots. `load` requests the engine's ordinary next-tick save reload
and is accepted only during idle field gameplay. `script` runs a real trigger
script entry, with an optional event-object argument, after replying to the
client so dialogue and battle scripts cannot deadlock the harness. `shop`
opens the selected real `PAL_BuyMenu()` after drawing the current field scene;
it does not invent shop data or bypass the ordinary menu/input loop, but it
does bypass the NPC trigger script and therefore does not show the
shopkeeper's preceding dialogue. Use `script` when that dialogue-to-shop
sequence is under review. `status` reports field, dialogue, battle UI, and the
current `--ui-test` review module so automation can wait for a real
interaction state without guessing frames.
`event` reads one live event-object record through the same public event-state
API used by the engine, so it also observes the Cardputer extreme pager rather
than a desktop-only resident array.

`ui` opens an ordinary blocking gameplay UI (`main`, `status`, `items`,
`magic`, `save`, `confirm`, or `sell`) on the game thread. The `sell` UI calls
the real `PAL_SellMenu()` over the current inventory. `review-fixture` makes the
current host session visually dense using three real player roles plus item
and magic IDs discovered from the loaded data tables. It does not write a save
or prove that normal story progression acquired that state. `sop-capture`
requires an idle field game, applies that fixture after the clean map capture,
then opens the real UIs plus a shop, dialogue, and battle in SOP order. It
writes native first-page and scrolled item/magic screenshots, a sell-menu
screenshot, and
`captures.json` beneath the requested `./tmp_ui/` directory. Its pinned-data
default uses enemy team 3 so the battle screenshot contains three enemies; an
explicit `--battle` still overrides it. It waits for the interactive battle UI
and then captures and leaves open its real usable-item selector for human
review. The standalone `battle-items` command performs the same menu
initialization during an active player move; like `battle`, it is an
integration shortcut rather than story-route evidence. `sop-capture` first advances any
dialogue active in the loaded save and waits for a stable field interval, so
menus are not nested inside a trigger dialogue and the map image does not
retain a stale dialogue overlay.

The Cardputer native Linux host can compile the same server with
`CARDPUTER_EXTREME_NATIVE_WS=1`. Select its logical framebuffer at launch with
`--ui-size 240x135` or `--ui-size 160x128`; coordinates and widget extents are
calculated in C from that actual size. Its screenshot response is the physical
1:1 framebuffer. This flag
changes only the host build recipe; the ESP-IDF firmware never contains socket
code or the screenshot buffer. A visible native host
(`PAL_CORES3SE_NATIVE_DISPLAY=1`) uses real host time for human interaction;
headless deterministic runs and explicit replay routes retain the fast virtual
clock used by automated checks.

For example, after generating the normal Cardputer packs, launch a visible
160x128 session directly from the command line:

```sh
make -C esp32s3 CARDPUTER_EXTREME_NATIVE_WS=1 \
  /tmp/sdlpal-cores3se-native/cardputer_extreme_engine_host
cd /mnt/hgfs/deb13/PALSteam/PAL_DOS
PAL_WS_PORT=12345 \
PAL_CORES3SE_NATIVE_NOR_PACK=/tmp/pal_cardputer_extreme_nor.pak \
PAL_CORES3SE_NATIVE_TF_PACK=/tmp/pal_cardputer_extreme_full.pak \
/tmp/sdlpal-cores3se-native/cardputer_extreme_engine_host \
  --ui-test --ui-size 160x128 --scale 4
```

`--ui-test` is explicit host-only interactive QA mode. After reaching stable
field gameplay, F1 applies the dense review fixture and traverses the complete
module list in `UI_REVIEW_SOP.md`: dialogue; live battle commands, submenus,
targets and results; item/equipment and magic flows; buy/sell; system,
confirmation, save and opening/load; chapter loading/end; and map. Non-classic
builds also include their all-target and battle-speed screens. `review-next`
advances the same sequence without synthesizing an F1 key.
The mode is unavailable unless the flag was present at process startup, and it
does not alter embedded firmware.

Scene, script, shop, and battle commands are integration shortcuts. Their screenshots prove
that the real game loop and renderer handle the requested state, but do not
prove that a natural story route reaches it. Keep deterministic route evidence
for that separate claim.

Run the client tests with:

```sh
make -C unix USE_SDL3=0 ws-check
```
