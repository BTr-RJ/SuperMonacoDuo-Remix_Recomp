# Building from source

The public repository holds the integration source, installer tools, tests and
documentation. It does not hold ROMs, generated game code, the arcade-derived
packs, the CRT shaders, `src/presentation/formation_art20.h` (cartridge-derived
texels) or the build toolchain. The simplest way to build is to use a release
package, which carries the toolchain in `.tools` and those files in place.

## Requirements

- Windows 10/11 x64 with Microsoft C++ Build Tools (Visual Studio 2022, x64).
- A release package extracted next to (or over) a checkout of this repository, so
  `.tools` (Python, CMake, Ninja, the patched GenesisRecomp SDK, librashader) and `assets`
  are present.
- Your own ROMs matching [games/supported-roms.json](../games/supported-roms.json).

## Build

```powershell
.tools/python/python.exe tools/install_roms.py --rom-dir "D:\ROMs"   # stages verified copies in .local/install-roms
.tools/python/python.exe tools/prepare_inputs.py          # verifies SHA-256, generates the game code locally
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -BuildFolder build/native20 -NoRootLauncherCopy
```

`prepare_inputs.py` identifies the ROM by SHA-256 only and refuses anything else.
`tools/patch_runtime.py` applies the idempotent project integration to the pinned
SDK (`mstan/segagenesisrecomp`, commit `252535d12476f5a697cf87082ea3e827f6430a1b`).
Outputs land in `build/native20/bin/{smgp1,smgp2}` and `SuperMonacoDuoRecomp.exe`.

## Tests

```powershell
.tools/python/python.exe -m unittest discover -s tests -p "test_*.py"   # installer and updater, synthetic
.tools/python/python.exe tools/probe.py --build build/native20 --game smgp1 --tag demo --frames 1200 --aspect 4:3
```

`tools/probe.py` runs a deterministic hidden replay and writes screenshots under
`build/validation/<tag>`. `--original` selects the unmodified Mega Drive rendering.
`tests/fixtures/*-cockpit20.input` are controller-only replays used for the README
comparison (`tools/make_comparison_gifs.py`). Fixtures that write RAM are QA-only.

## Layout

| Directory | Purpose |
|---|---|
| `src/launcher` | Win32 launcher, controls editor, installer and updater |
| `src/game` | Controls and per-game adapters |
| `src/presentation` | Scene capture, zoom, tunnels, liveries, GPU/CRT output |
| `src/audio` | Arcade sound board and effects |
| `tools` | ROM preparation, installer, updater, packaging, QA probes |
| `scripts` | `build.ps1`, `install.ps1` |

Presentation hooks are read-only with respect to guest RAM, VRAM and the sprite table;
the native VDP stays authoritative for status, collision and overflow. The documented
exceptions are the analog steering adapter and the optional pedal pulses.
