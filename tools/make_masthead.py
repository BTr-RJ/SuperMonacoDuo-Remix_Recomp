"""Builds assets/launcher/masthead.png from the 0.50 comparison captures (enhanced output only)."""
from pathlib import Path
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]
E = ROOT / 'build/validation/media50/enh'
picks = [('smgp1', 'left040'), ('smgp2', 'right100'), ('smgp1', 'right030'), ('smgp2', 'right140'),
         ('smgp1', 'drive030'), ('smgp2', 'left040'), ('smgp1', 'right100')]
W, H = 2172, 280
BASE = (16, 19, 21)
cw = int(256 * H / 158)
strip = Image.new('RGB', (W, H), BASE)
for i, (g, n) in enumerate(picks):
    im = Image.open(E / g / f'{n}.png').convert('RGB').crop((0, 66, 256, 224))   # race view, no mirror strip
    strip.paste(im.resize((cw, H), Image.BICUBIC), (i * cw, 0))
# darker on the left (title area), fading into the page colour at the bottom
dark = Image.new('L', (W, 1))
dark.putdata([int(255 * (0.62 + 0.38 * max(0.0, 1 - (x / (W - 1)) / 0.55))) for x in range(W)])
dark = dark.resize((W, H))
bottom = Image.new('L', (1, H))
bottom.putdata([int(255 * 0.8 * min(1.0, max(0.0, (y / (H - 1) - 0.7) / 0.3))) for y in range(H)])
bottom = bottom.resize((W, H))
plain = Image.new('RGB', (W, H), BASE)
strip = Image.composite(plain, strip, dark)
strip = Image.composite(plain, strip, bottom)
strip.save(ROOT / 'assets/launcher/masthead.png', optimize=True)
print('written', (ROOT / 'assets/launcher/masthead.png').stat().st_size)
