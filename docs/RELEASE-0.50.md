# Version 0.50 — initial public test

[Download the Windows x64 public test](https://github.com/BTr-RJ/SuperMonacoDuo-Remix_Recomp/releases/tag/v0.50).
The release follows the ROM-free local-build model: supply your compatible original ROMs
and the installer builds both games on your computer.

## Included

- English native launcher, installer and updater, with the classic covers.
- Both games: Super Monaco GP (World) and Ayrton Senna's Super Monaco GP II (USA).
- Continuous zoom for cars, signs, tyres, trees, arches and tunnel structures, with
  the arcade sound program for engines, tyre skids, impacts and sparks.
- Arcade-version art: cockpit, hands and wheels, rival rear views, tyre stacks,
  chevron signs, trees, flagman, Game Over and Retire; collisions with the new
  trackside objects and trees.
- Team colours: GP II formation and rivals, World standings icons, 16 Championship liveries.
- Analog steering in both games; optional pulse pedals (R2 / L2 / right stick).
- GP II victory theme (*Tema da Vitória (Sega Genesis remake)*, Edu Moonwalker, first 7 s removed).
- Widescreen (16:9, 21:9, 32:9), resolutions to 4K, CRT presets through librashader / koko-aio.
- Approach, curve, tunnel entrance/exit, arch and kerb refinements in both games,
  including the GP II lateral camera and the kerb wheel pose.
- Automatic update check against GitHub releases, with SHA-256 verified downloads,
  staged builds, rollback protection and preserved controls/settings/saves.

## Scope and limits

- Hybrid recompilation: translated 68000 code with an interpreter fallback and an
  interpreted Z80. Not complete static recompilation, not the arcade game, no constant
  60 FPS claim. Network play is not implemented.
- Validation was done with QA replays, fixtures and sweeps (controller-only replays and
  prepared scenarios), not with complete campaigns played on every track. Final visual,
  audio and control approval rests with the author and with testers.
- Known issues: residual curve jitter and lateral camera steps in GP II (native steps,
  smoothed with ~50 ms delay); blue signs over just-passed cars; distant cars that may
  look like they float; tunnel lights alternating in GP II; tunnel and facade growth
  at the cartridge's 20 Hz step; zoom not calibrated to the arcade's object sizes.
- Arcade equivalents cover the sign and tyre objects seen on the first track of each
  game; other tracks may still show the cartridge art.
- Pulse pedals and right-stick pedals have been verified with QA inputs only.
- The update flow was tested with local release fixtures; the first real GitHub update
  check becomes possible after this release exists.

## Package contents

The ZIP carries the launcher, scripts, tools, source, shaders, the toolchain in `.tools`,
and `assets/arcade-smgp` and `assets/audio18` (arcade art, arcade sound bank and the
victory theme). It carries no ROMs, no generated game code and no saves. Those packs are
derived from Sega's arcade game and a fan recording; see
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). Verify downloads with `SHA256SUMS.txt`.
