"""Builds the README side-by-side GIFs (original Mega Drive output vs this project).

Inputs are two probe runs of the SAME replay script (tests/fixtures/*-cockpit20.input,
pure controller input, no RAM writes):
  tools/probe.py --game smgpN --tag media50/orig --aspect 4:3 --original --script ...
  tools/probe.py --game smgpN --tag media50/enh  --aspect 4:3            --script ...
Scope: a QA replay of a few seconds of the first track, not a played campaign.
"""
from pathlib import Path
import argparse, hashlib, json, subprocess, shutil, tempfile
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--src', default='build/validation/media50')
ap.add_argument('--out', default='docs/media/0.50')
ap.add_argument('--step', type=int, default=2, help='keep every Nth consecutive frame (2 = 30 fps)')
a = ap.parse_args()
SRC, OUT = ROOT / a.src, ROOT / a.out
OUT.mkdir(parents=True, exist_ok=True)
FFMPEG = ROOT / '.tools/ffmpeg/ffmpeg.exe'
FONT = ImageFont.truetype(str(ROOT / 'assets/launcher/BarlowCondensed-Bold.ttf'), 17)
SEGMENTS = [('drive', 75), ('left', 75), ('right', 150)]   # consecutive ticks in each segment
TITLES = {'smgp1': 'SUPER MONACO GP', 'smgp2': "AYRTON SENNA'S SUPER MONACO GP II"}
BAR = 26
report = {}

for game in ('smgp1', 'smgp2'):
    names = [f'{seg}{i:03d}.png' for seg, n in SEGMENTS for i in range(n)][::a.step]
    tmp = Path(tempfile.mkdtemp(prefix=f'gif-{game}-'))
    for k, name in enumerate(names):
        o = Image.open(SRC / 'orig' / game / name).convert('RGB')
        e = Image.open(SRC / 'enh' / game / name).convert('RGB')
        w, h = o.size
        canvas = Image.new('RGB', (w * 2 + 4, h + BAR), (16, 19, 21))
        canvas.paste(o, (0, BAR)); canvas.paste(e, (w + 4, BAR))
        d = ImageDraw.Draw(canvas)
        d.text((8, 3), 'ORIGINAL MEGA DRIVE', font=FONT, fill=(190, 196, 200))
        d.text((w + 12, 3), 'DUO-REMIX RECOMP 0.50', font=FONT, fill=(255, 75, 54))
        canvas.save(tmp / f'f{k:04d}.png')
    fps = 60 // a.step
    gif = OUT / f'{game}-comparison.gif'
    subprocess.run([str(FFMPEG), '-y', '-loglevel', 'error', '-framerate', str(fps), '-i', str(tmp / 'f%04d.png'),
                    '-vf', 'split[a][b];[a]palettegen=max_colors=128:stats_mode=full[p];[b][p]paletteuse=dither=none',
                    '-loop', '0', str(gif)], check=True)
    shutil.rmtree(tmp)
    report[game] = dict(file=gif.name, frames=len(names), fps=fps, bytes=gif.stat().st_size,
                        sha256=hashlib.sha256(gif.read_bytes()).hexdigest(),
                        replay=f'tests/fixtures/{game}-cockpit20.input', title=TITLES[game])
(OUT / 'comparison.json').write_text(json.dumps(dict(
    scope='QA replay of controller input on the first track; left = SMGP_ORIGINAL=1, right = default presentation; 4:3; not a played campaign',
    gifs=report), indent=2))
print(json.dumps(report, indent=2))
