# Small-screen gameplay UI review SOP

Apply the rules in [`RESPONSIVE_RENDERING.md`](RESPONSIVE_RENDERING.md).
Only the allowlisted FBP/RNG materials use the transform in
[`FULLSCREEN_ASSET_RENDERING.md`](FULLSCREEN_ASSET_RENDERING.md); maps and UI
elements retain their responsive or per-material paths.

The reviewer is the human currently operating the SDL game. For each major
screen, use the host WebSocket harness only to start the game and position it
at a real, interactive gameplay state. Then stop sending simulated input,
bring the SDL window to the foreground, tell the human what is ready to review,
and pause. The human explores that screen and its submenus freely, reports any
problem, and explicitly asks to continue. Do not mark a screen complete or move
to the next screen until the human reviewer does so.

A harness-positioned state proves rendering and interaction only; it does not
prove that the natural story route reaches that state. Automated screenshots
and input routes are preparation and diagnostic evidence; they do not replace
the human review pause.

Review the major screens in this order:

- [ ] 1. Map / exploration
- [ ] 2. Dialogue / story presentation
- [ ] 3. Battle
- [ ] 4. Main menu / character status
- [ ] 5. Items / equipment
- [ ] 6. Magic
- [ ] 7. Shops / buying / selling
- [ ] 8. System / save / load / title
- [ ] 9. Scene transition / chapter loading / terminal screens

For each major screen, check only these three general requirements before
moving on:

1. Important content is visible and uses the physical canvas sensibly.
2. Text and numbers are crisp native 10px pixels without post-scaling.
3. Normal input, cancellation, confirmation, and return to gameplay work.

Store real gameplay screenshots and session-specific notes under `./tmp_ui/`.
Use the repeatable controls in
[`../unix/WEBSOCKET_HARNESS.md`](../unix/WEBSOCKET_HARNESS.md) to reach the
review state, then stop automation while the human operates it.

Before human review, run the SDL host at both `240x135` and `160x128` and use
the command-line SOP capture command documented in the harness guide. It writes
one real-gameplay screenshot for every reachable major screen into a new
subdirectory of `./tmp_ui/`; it must never synthesize UI images or clean older
captures.

The automated fixture must exercise dense, real-data layouts rather than an
early-save minimum: three party members, many inventory items, many learned
magics, and a battle with multiple enemies. Capture both the first and a
scrolled page of the item and magic lists, plus the usable-item selector inside
the active battle and the sell selector with cash and price visible.
`sop-capture` applies the
session-only `review-fixture` and uses the three-enemy pinned-data battle team
by default; these state changes are visual integration fixtures, not story
progression evidence.

After automated capture, start a visible SDL session at the resolution under
review with the command-line `--ui-test` flag. This flag is host-only and is
the sole way to enable the F1 review control; ordinary game runs do not assign
F1 any test behavior. Reach stable field gameplay, then press F1 to cycle
through every review module below and back to the map:

- dialogue;
- a three-party/three-enemy battle: main commands, magic, use/throw item,
  miscellaneous and item submenus, and enemy/player targets;
- battle result, level-up, stat gain, learned magic, and game-over screens;
- main menu, status, dense item list, inventory choice, item target, equipment;
- magic party choice, dense magic list, and magic target;
- buying and selling;
- system, confirmation, save slots, and opening/load menu;
- chapter loading and chapter-end screens.

F1 closes the current blocking UI. It keeps one live fixture battle while
cycling its submenus and targets, then terminates that fixture before the
post-battle review screens. `tools/ws_cli.py review-next` performs the same
advance for repeatable harness testing; it is accepted only in `--ui-test`
mode. Non-classic builds additionally include their interactive all-target
states and battle-speed menu; DOS/classic builds do not invent those absent
screens.

Once each module is interactive, automation stops. The human navigates the
full lists, changes party selections and battle commands, cancels, and returns
to gameplay with ordinary SDL input. Keep the visible process at the requested
state and pause for the human's result before moving to the next SOP screen.
The WebSocket `review-fixture`, `ui`, and `battle` commands remain available
for direct positioning when the reviewer wants to revisit one module instead
of cycling.
