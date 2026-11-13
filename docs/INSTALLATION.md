# Installation and updates

Windows 10/11 x64, a folder with write permission and your own ROMs. Extract the
complete `SuperMonacoDuoRecomp-<version>-Windows-x64.zip` before opening
`SuperMonacoDuoRecomp.exe`; keep the executable, `SDL2.dll`, `assets`, `tools`,
`scripts`, `src` and `.tools` together.

1. In the launcher, choose **Install games**.
2. Choose the folder(s) with your ROMs. Both games are required: **Super Monaco GP
   (World)** and **Ayrton Senna's Super Monaco GP II (USA)**.
3. Choose an empty folder or a recognised installation. Avoid Program Files and disk roots.
4. The installer verifies SHA-256, makes private copies, generates the game code
   locally and compiles it into `build/native20`. Then open the launcher and play.

BIN, MD, GEN and ZIP are accepted. The file name authenticates nothing; only these dumps are supported:

| Game | SHA-256 |
|---|---|
| Super Monaco GP (World) | `9046c9d67f15ab0f68d9e73b0cc516b6fd1637a8db0da4a03dcff8933c12dfa1` |
| Ayrton Senna's Super Monaco GP II (USA) | `c02206602e00dd8e9b6c38943b8a3be41e37d2d21c1768cf342900bce11ab4f0` |

The USA dump of the first game is not supported. No ROM is downloaded, uploaded or
included in the package, and your original folders are never modified. Private copies
live in `.local/install-roms` and `.local/build-inputs`.

You can also run the installer from PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install.ps1 -Destination "D:\Games\SuperMonacoDuo" -RomFolder1 "D:\ROMs"
```

## RecompLauncher

Releases from 0.60 on carry `recomplauncher.json` at the top of the ZIP (next to `SuperMonacoDuoRecomp.exe`). RecompLauncher asks for the folder that holds your ROMs and runs `tools/recomplauncher_prepare.py`, which verifies both ROMs by SHA-256 and builds the games into `build/native20` with the same `scripts/install.ps1` as the launcher. It never opens a window or installs anything, so the Microsoft C++ Build Tools must already be present; if not, the step fails with a message. Then **Play** starts the project launcher. Do not mix the project's own updater and RecompLauncher's updates on the same folder.

## Tools

The release package already carries `.tools` (Python, CMake, Ninja, the patched
SDK and librashader). Every file is listed with SHA-256 in `PACKAGE-MANIFEST.json`
and verified before it is copied. Only the Microsoft C++ Build Tools
(Visual Studio 2022) are missing from a clean machine; the installer offers the
official signed Microsoft installer with administrator consent. An installed
`.tools` is not replaced by updates; they build with the `.tools` of the new package.

## Automatic updates

The installed launcher asks GitHub (`BTr-RJ/SuperMonacoDuo-Remix_Recomp`) at most once a
day; **Updates > Check** asks again. Checking downloads nothing. Downloads use https from
`github.com` / `*.githubusercontent.com` only, with size and SHA-256 verified and the
package manifest validated. Without a network you can play normally.

When you choose **Download and install**, the launcher closes and a separate process
builds the new version in a working folder, reusing your private ROM copies, and runs
the validation through the launcher. Only then it replaces the managed files, with a
backup and a recovery journal in `.local/updates/`. Cancelling keeps the old version;
an interrupted update is recovered the next time the launcher opens. Keep 4 GB free and
close the games first.

Preserved: `.local/controls.ini`, `.local/launcher.ini`, `.local/build-inputs`, and the
settings/saves of each game in `build/native20/bin/smgp1` and `smgp2` (copies are also
kept in the update record). `build/native20` stays fixed between versions for that reason.

## Maintainers: publishing

`tools/publish_release.py --version 0.60.1` builds `SuperMonacoDuoRecomp-0.60.1-Windows-x64.zip`,
`PACKAGE-MANIFEST.json`, `SHA256SUMS.txt` and `release-asset.json`, refusing ROMs,
`.local`, `build` and `smgp.zip`. Attach the ZIP to a `v0.60.1` release on GitHub.
The package contains `assets` derived from the arcade game; see
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

## Common problems

- **ROMs not found:** check the SHA-256 values and choose the cartridge folder, not the whole disk.
- **Compile failure:** keep `.local/install-0.60.1.log` and check free space and the C++ Build Tools.
- **Tool checksum failure:** extract the ZIP again.
- When reporting problems send the version, Windows, GPU and a log excerpt, never ROMs, generated code or saves.
