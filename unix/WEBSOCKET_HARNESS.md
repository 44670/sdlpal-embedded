# Linux WebSocket gameplay harness

The normal Unix SDL build exposes an optional local control surface for fast,
repeatable gameplay review. It is disabled unless `PAL_WS_PORT` is set, binds
only to `127.0.0.1`, rejects browser `Origin` requests, serves one client at a
time, and executes commands on the game thread.

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
python3 -B tools/ws_cli.py script 1435
python3 -B tools/ws_cli.py shop 0
python3 -B tools/ws_cli.py battle 0 --battlefield 2
```

`record` writes a bounded PNG sequence plus per-frame `state.jsonl`; `--battle`
starts the battle on that same WebSocket connection so short-lived states are
not lost to reconnect latency. `--battlefield` selects the real FBP battle
background resource instead of inheriting an unrelated field-state value. This is
useful for transient animation/message/result states. `input` sends ordinary PAL key transitions; a tap is an explicit bounded
down/up pair. `scene` changes only an already-running field game and accepts
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
sequence is the behavior under review. `status`
reports field and battle UI state so automation can
wait for a specific real interaction state without guessing frame numbers.
`event` reads one live event-object record through the same public event-state
API used by the engine, so it also observes the Cardputer extreme pager rather
than a desktop-only resident array.

The Cardputer native Linux host can compile the same server with
`CARDPUTER_EXTREME_NATIVE_WS=1`. Its screenshot response is the generated
physical 1:1 viewport profile (for example 160x128 or 240x135). This flag
changes only the host build recipe; the ESP-IDF firmware never contains socket
code or the screenshot buffer. A visible native host
(`PAL_CORES3SE_NATIVE_DISPLAY=1`) uses real host time for human interaction;
headless deterministic runs and explicit replay routes retain the fast virtual
clock used by automated checks.

Scene, script, shop, and battle commands are integration shortcuts. Their screenshots prove
that the real game loop and renderer handle the requested state, but do not
prove that a natural story route reaches it. Keep deterministic route evidence
for that separate claim.

Run the client tests with:

```sh
make -C unix USE_SDL3=0 ws-check
```
