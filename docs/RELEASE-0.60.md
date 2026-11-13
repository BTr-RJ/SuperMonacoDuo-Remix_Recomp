# Version 0.60 — achievements, Game Over/Retire and RecompLauncher

[Download the Windows x64 public test](https://github.com/BTr-RJ/SuperMonacoDuo-Remix_Recomp/releases/tag/v0.60).
This is still a **Public Test**; see the known limitations in the README.

## New

- **Offline achievements (both games).** The core RetroAchievements sets (Super Monaco GP:
  game 111, 29 achievements, 320 points; Super Monaco GP II: game 103, 53 achievements, 585
  points) are bundled with their original trigger logic and run on the game's RAM through
  rcheevos (`src/achievements`). Unlock toasts appear at the top of the screen and **F6**
  opens the list. Progress is stored in `.local/achievements/<game>.dat`; nothing is sent
  anywhere. The two "unknown emulator" warning entries of the sets are not part of the list.
  Not included: leaderboards, rich presence, hardcore mode and an online RetroAchievements link.
- **Game Over (Super Monaco GP) and Retire (GP II).** The cartridge animates the letters
  (they scatter and grow into place). The continuous zoom used to start again at about a
  fifth of the letter size, so the word shrank before growing. It now starts at the
  cartridge's final letter size and position and grows from there; GP II's Game Over and
  Retire use the same hand-off.
- **RecompLauncher.** `recomplauncher.json` and `tools/recomplauncher_prepare.py` are part
  of the package (they were only in the repository after 0.50).

## Checks that were run

- Trigger plumbing: scripted RAM writes unlocked the "Above Maximum Speed" achievement with
  its official 16-bit condition; progress was saved and reloaded in a second run without
  unlocking again. Both sets activated all their triggers (29 and 53) without rejection.
- Game Over / Retire: replay fixtures (World ending, GP II failure and explicit retirement)
  confirm the first zoom frame matches the native letter size and position, growth is
  continuous, the background stays frozen and the game state, RAM and audio are identical
  to the run without the overlay. These are QA fixtures, not played campaigns.

## Notes

- Achievement logic was taken as published by RetroAchievements.org. It was not
  re-authored, and not every achievement was played through.
- The zoom hand-off was verified on the World ending and on GP II fixtures; the exact
  in-game Retire sequence of GP II was not recorded.
