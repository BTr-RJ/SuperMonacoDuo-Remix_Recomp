# Video settings

Open **Video & CRT** in the launcher. Settings are stored in `.local/launcher.ini`.

| Setting | Options |
|---|---|
| Aspect | 4:3, 16:9, 21:9, 32:9. Wider choices add side scenery in races |
| Output size | 960×540 up to 3840×2160, windowed or fullscreen |
| Filter | Pixels, linear, koko-aio (Balanced, Ambilight, Classic CRT), light CRT, Trinitron, Composite |
| Continuous zoom + arcade art | On by default. Off shows the original Mega Drive rendering |
| Fullscreen | Bars preserve the geometry when the aspect differs from the screen |

- The GPU composites sprites and road with 3× coverage before the CRT filter. Without
  suitable GPU support the native sprites are kept.
- The front end rebuilds motion at about 59.94 Hz without speeding up the simulation.
  That is not 60 new draws of every animation, and constant 60 FPS is not promised.
- CRT presets use their authors' original parameters; nothing is added to hide defects.
- Wide aspects apply only to the race layout; menus, pit and hint screens keep the
  original composition.
- A built-in recorder writes 59.94 fps CFR video of the pre-shader frame when
  `.local/record.on` exists or `SMGP_RECORD` is set (for bug reports; off by default).
  It also writes a per-tick telemetry CSV. Delete the file to turn it off.
