# Contributing

Read [the build guide](docs/BUILDING.md) and [video guide](docs/VIDEO.md) first.
Keep changes focused and explain the concrete problem and the resulting behaviour.
Run the relevant native probes and `python -m unittest discover -s tests -p "test_*.py"`.

Never commit ROMs, generated game C, `.local`, `.tools`, build outputs, saves, account
credentials, private screenshots, arcade ROM data or the arcade-derived art/sound packs.
Use synthetic fixtures for source tests. Rendering regressions need the game and revision,
a reproducible input or scene, an original/enhanced comparison and, when possible, the time
in a recording.

Presentation hooks must stay read-only with respect to guest RAM, VRAM and the sprite
table; the native VDP stays authoritative for status, collision and overflow. RAM, position,
team or traffic preparation is for QA fixtures only and must not enter the product. When the
GPU or composition fails, the native presentation must be preserved.

Do not edit generated C. Engine changes belong in the project integration code or
`tools/patch_runtime.py`. Do not update dependency versions as a side effect of another
change, and keep component licence notices and media provenance. Network play, complete static
coverage and arcade equivalence are not current release claims.
