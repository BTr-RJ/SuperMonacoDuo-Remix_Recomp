# Third-party notices and provenance

This independent fan project is not an official Sega product. Super Monaco GP and
Ayrton Senna's Super Monaco GP II, their images and audio belong to Sega and their rights
holders. No ROM is included; you provide your own dumps.

## Components

| Component | Licence | Notes |
|---|---|---|
| GenesisRecomp runtime (`mstan/segagenesisrecomp` @ `252535d12476f5a697cf87082ea3e827f6430a1b`) | PolyForm Noncommercial 1.0.0 | [docs/licenses/GenesisRecomp-PolyForm-Noncommercial.md](docs/licenses/GenesisRecomp-PolyForm-Noncommercial.md); recompiler: [GenesisRecomp-recompiler.txt](docs/licenses/GenesisRecomp-recompiler.txt); runtime dependencies: [GenesisRecomp-third-party.md](docs/licenses/GenesisRecomp-third-party.md) |
| SDL2 | zlib | [docs/licenses/SDL2.txt](docs/licenses/SDL2.txt) |
| ymfm (YM2151 / OPM) | BSD 3-Clause | [docs/licenses/ymfm.txt](docs/licenses/ymfm.txt), commit `17decfae857b92ab55fbb30ade2287ace095a381` |
| superzazu/z80 | MIT | [docs/licenses/superzazu-z80.txt](docs/licenses/superzazu-z80.txt) |
| clowncommon | ISC | [docs/licenses/clowncommon.txt](docs/licenses/clowncommon.txt) |
| rcheevos | MIT | [docs/licenses/rcheevos.txt](docs/licenses/rcheevos.txt) (SDK copy) |
| librashader 0.12.0 | MPL 2.0 | [docs/licenses/librashader.md](docs/licenses/librashader.md) |
| koko-aio shaders (`kokoko3k/koko-aio-slang` @ `64f3d8338ccaf3bc4024ad01f0e6da4a10da418b`) | per-file notices | preserved in `assets/shaders/koko-aio` |
| Microsoft D3DX libraries | Microsoft SDK terms | [docs/licenses/Microsoft-D3DX.txt](docs/licenses/Microsoft-D3DX.txt) |
| Barlow / Barlow Condensed | SIL OFL 1.1 | `assets/launcher/BARLOW-LICENSE.txt` |
| Python, CMake, Ninja, Pillow | their own licences | notices ship inside `.tools` in the release package |

The runtime integration is reproducible through `tools/patch_runtime.py`.
Public visibility does not imply commercial-use rights.

## Arcade-derived data (release package only)

The release ZIP, never this git repository, contains data derived from the Sega arcade game
(Super Monaco GP, X Board) and from a fan recording. Rights remain with Sega and the
respective holders. They are included at the owner's decision, for noncommercial fan use, and may be removed on
request of a rights holder.

| Path in the package | Content and origin |
|---|---|
| `assets/arcade-smgp` | Sprites, palettes and descriptors extracted from the arcade ROM set `smgp.zip` (cockpit, wheels, hands, rival rear views, tyre stacks, chevron signs, trees, effects, Game Over and Retire glyphs). Each folder has a `PROVENANCE.json` with the source file hashes; `props21` and `sweep21` record the arcade/cartridge object matches. Extraction tools: `tools/extract_arcade*.py`. |
| `assets/audio18/arcade-sound.sb17` | Arcade sound program `epr-12436.17` and PCM banks `mpr-12437.11`, `mpr-12438.12`, `mpr-12439.13` (sha256 in `assets/audio18/PROVENANCE.json`). The host runs them with the SDK's Z80 core, ymfm and its own PCM bus. MAME was only an external QA oracle; no MAME code is linked or copied. |
| `assets/audio18/gp2-victory.wav` | *Tema da Vitória (Sega Genesis remake)* by **Edu Moonwalker** (<https://www.youtube.com/watch?v=Ho3owgC7jMI>). Only the first 7 seconds of silence were removed; no gain, normalisation, fade or edit. The recording and composition belong to their holders. |
| `src/presentation/formation_art20.h` | Indexed texels of the overhead car thumbnail, from the GP II cartridge (tiles AB..AD of the native formation screen). |

No AI-generated artwork or external emulator code is part of the arcade data. If you
hold rights to any of this material and want it removed, open an issue.

## Research tools (not part of the product)

MAME 0.289 and Genesis Plus GX / libretro were used as external QA references for arcade
descriptors and Mega Drive behaviour; Capstone was used locally to read the ROM code; FFmpeg
was used to build comparison media. None is linked into, copied into or distributed with the game.
