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
- [ ] 7. Shops
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
