"""Headless setup step for RecompLauncher (see recomplauncher.json, "prepare").

RecompLauncher passes the folder that holds the player's ROMs and the install folder.
This verifies both ROMs by SHA-256 (tools/install_roms.py) and builds the two games
locally with the same scripts/install.ps1 the project's own launcher uses, writing to
the same place (build/native20). No window is opened and nothing is downloaded: if the
Microsoft C++ Build Tools are missing the step fails with a clear message instead of
asking for administrator rights.

Output lines follow RecompLauncher's format: "progress <0..1> <text>". Exit 0 = success.
"""
from pathlib import Path
import argparse
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
STAGES = 7          # [STAGE n/7] lines printed by scripts/install.ps1


def progress(value, text):
    print(f'progress {min(max(value, 0.0), 1.0):.3f} {text}', flush=True)


def fail(message, code=1):
    print(f'error {message}', flush=True)
    sys.exit(code)


def stream(command, on_line, cwd=None):
    process = subprocess.Popen([str(c) for c in command], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    for raw in iter(process.stdout.readline, b''):
        on_line(raw.decode('utf-8', errors='replace').rstrip())
    return process.wait()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game', '--rom-dir', dest='roms', type=Path, required=True,
                   help='folder with your Super Monaco GP (World) and Super Monaco GP II (USA) ROMs')
    p.add_argument('--install', type=Path, default=ROOT, help='install folder (the release folder, default)')
    a = p.parse_args()
    install = a.install.resolve()
    if install != ROOT:
        fail('The install folder must be the folder this script belongs to.')
    roms = a.roms.resolve()
    if not roms.is_dir():
        fail(f'Choose the folder that contains your ROM files ({roms} is not a folder).')

    progress(0.02, 'Checking your ROM files')
    rc = stream([sys.executable, ROOT / 'tools/install_roms.py', '--rom-dir', roms], lambda line: print(line, flush=True))
    if rc:
        fail('Both Super Monaco GP (World) and Ayrton Senna\'s Super Monaco GP II (USA) are needed. '
             'See docs/INSTALLATION.md for the exact supported dumps.', 2)

    powershell = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    ninja = re.compile(r'^\[(\d+)/(\d+)\]')
    stage = re.compile(r'^\[STAGE (\d+)/(\d+)\]\s*(.*)')
    state = {'value': 0.05}

    def on_line(line):
        m = stage.match(line)
        if m:
            n = int(m[1])
            state['value'] = 0.05 + 0.9 * (n - 1) / STAGES
            progress(state['value'], m[3] or f'Step {n} of {STAGES}')
            return
        m = ninja.match(line)
        if m and int(m[2]):
            # Stage 6 (compiling) is by far the longest: spread it over its share of the bar.
            lo, hi = 0.05 + 0.9 * 5 / STAGES, 0.05 + 0.9 * 6 / STAGES
            progress(lo + (hi - lo) * int(m[1]) / int(m[2]), f'Compiling the games ({m[1]}/{m[2]})')
            return
        if line and 'warning C' not in line and 'warning:' not in line:
            print(line, flush=True)       # keep errors visible in RecompLauncher's log

    rc = stream([powershell, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ROOT / 'scripts/install.ps1',
                 '-Destination', install, '-RomFolder1', roms, '-NoToolInstall'], on_line)
    if rc:
        fail('The local build failed. Microsoft C++ Build Tools (Visual Studio 2022) must be installed. '
             'Details: the newest .local/install-*.log.', 3)
    for game in ('smgp1', 'smgp2'):
        if not (install / f'build/native20/bin/{game}/{game}.exe').is_file():
            fail(f'{game}.exe was not produced.', 4)
    progress(1.0, 'Both games are ready')


if __name__ == '__main__':
    main()
