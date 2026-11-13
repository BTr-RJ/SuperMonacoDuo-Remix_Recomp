# Version 0.60.1 — achievement overlay fix

[Download the Windows x64 public test](https://github.com/BTr-RJ/SuperMonacoDuo-Remix_Recomp/releases/tag/v0.60.1).

## Fixed

- **Achievement toast and F6 panel during races.** In 0.60 they were drawn before the GPU
  composition of the race scene, so the road, HUD and car sprites covered them (the unlock toast
  was partly hidden and the panel looked broken in races). They are now drawn after the
  composition, over the whole picture. Menus and other screens were not affected.
- The launcher's "What's new" panel listed the 0.50 items; it now lists the 0.60 features.

## Unchanged

Everything else is as in [0.60](RELEASE-0.60.md): offline RetroAchievements sets
(Super Monaco GP 29 achievements, GP II 53), the Game Over/Retire hand-off and RecompLauncher support.

## Checks that were run

- Race captures at 16:9 with the panel open and with a scripted unlock (the 16-bit "Above Maximum
  Speed" trigger) show the panel and the toast on top of the race. Scripted tests, not a played campaign.
