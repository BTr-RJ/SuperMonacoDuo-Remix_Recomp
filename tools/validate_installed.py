"""Verify an isolated local installation through the production launcher path."""
from pathlib import Path
import argparse
import json
import os
import subprocess

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--installation', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--frames', type=int, default=1800)
    args = p.parse_args(); root = args.installation.resolve(); out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    marker = json.loads((root/'.local/installation.json').read_text(encoding='utf-8-sig'))
    assert marker.get('product') == 'SuperMonacoDuoRecomp' and marker.get('games'), 'Incomplete installation'
    results = []; prefs = root/'.local/launcher.ini'; before = prefs.read_bytes() if prefs.exists() else None
    for game in marker['games']:
        folder = out/game; folder.mkdir(exist_ok=True); script = folder/'drive.input'
        script.write_text('WAIT 900\nPRESS START 12\nWAIT 240\nPRESS START 12\nWAIT 600\nHOLD B\nWAIT 300\nEXIT\n')
        report = root/'.local'/f'qa-launch-smgp{game[-1]}.json'; report.unlink(missing_ok=True)
        env = os.environ.copy()
        env.update(SMGP_QA_SCRIPT=str(script), SMGP_QA_FRAMES=str(args.frames), SMGP_QA_INPUT_ONLY='1', SMGP_ANALOG='0',
                   RR_TEST_HIDDEN='1', SDL_AUDIODRIVER='dummy', SDL_RENDER_VSYNC='0')
        r = subprocess.run([str(root/'SuperMonacoDuoRecomp.exe'), '--qa-launch', game[-1]], env=env, cwd=root,
                           creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0), timeout=300)
        assert r.returncode == 0, (game, r.returncode)
        data = json.loads(report.read_text())
        assert data['exit_code'] == 0 and data['launched_by_native_ui'], (game, data)
        results.append(dict(game=game, exit=r.returncode, launcher=data))
        print(f'{game}: isolated launcher replay passed', flush=True)
    assert (prefs.read_bytes() if prefs.exists() else None) == before, 'QA changed launcher preferences'
    (out/'result.json').write_text(json.dumps(dict(version=marker.get('version'), games=results, preferences_unchanged=True), indent=2)+'\n')

if __name__ == '__main__':
    main()
