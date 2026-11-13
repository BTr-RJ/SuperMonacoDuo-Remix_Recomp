"""Converts a RetroAchievements 'patch' response into assets/achievements/<game>.json.

The patch (achievement titles, descriptions, points and the official MemAddr trigger logic) is
published by RetroAchievements.org. Fetching it needs the maintainer's own RA login, which is
never stored here: pass the downloaded JSON file with --patch. The output keeps only core
(official) achievements and contains no account data.

  python tools/import_ra_patch.py --game smgp1 --patch patch111.json
"""
from pathlib import Path
import argparse
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
GAMES = {'smgp1': 111, 'smgp2': 103}      # RetroAchievements game ids (Mega Drive)
WARNING_ID = 101000001                    # "Warning: Unknown Emulator" placeholder, not a real achievement


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--game', choices=sorted(GAMES), required=True)
    p.add_argument('--patch', type=Path, required=True)
    a = p.parse_args()
    data = json.loads(a.patch.read_text(encoding='utf-8'))
    if not data.get('Success'):
        sys.exit(f"The patch response was not successful: {data.get('Error')}")
    patch = data['PatchData']
    if patch['ID'] != GAMES[a.game]:
        sys.exit(f"Patch is for RA game {patch['ID']}, expected {GAMES[a.game]} for {a.game}")
    items = []
    for ach in patch['Achievements']:
        if ach['ID'] == WARNING_ID or ach.get('Flags') != 3 or not ach.get('MemAddr'):
            continue
        items.append({'id': ach['ID'], 'title': ach['Title'], 'description': ach['Description'],
                      'points': ach['Points'], 'badge': ach.get('BadgeName', ''), 'memaddr': ach['MemAddr'],
                      'type': ach.get('Type') or ''})
    out = {'game': a.game, 'ra_game_id': patch['ID'], 'ra_title': patch['Title'],
           'source': 'RetroAchievements.org core set (official achievement logic)',
           'total_points': sum(i['points'] for i in items), 'achievements': items}
    target = ROOT / 'assets/achievements' / f'{a.game}.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(out, indent=1, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f"{a.game}: {len(items)} achievements, {out['total_points']} points -> {target.relative_to(ROOT)}")


if __name__ == '__main__':
    main()
