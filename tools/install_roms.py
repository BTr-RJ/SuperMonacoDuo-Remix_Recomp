"""Find the user's own qualified cartridge images and stage private, hash-bound copies.

Only the SHA-256 values listed in tools/prepare_inputs.py are accepted; names are never
trusted. Verified bytes go to .local/install-roms/<game>.zip, which prepare_inputs reads.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import io
import os
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MAX_ROM = 4*1024*1024
TITLES = {'smgp1': 'Super Monaco GP (World)', 'smgp2': "Ayrton Senna's Super Monaco GP II (USA)"}

def qualified():
    spec = importlib.util.spec_from_file_location('prepare_inputs', ROOT/'tools/prepare_inputs.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return {h: g for h, g in module.SUPPORTED.items() if g in TITLES}

def candidates(paths):
    seen = set()
    for supplied in paths:
        supplied = Path(supplied)
        if not supplied.exists():
            raise ValueError(f'ROM folder or file not found: {supplied}')
        entries = [supplied] if supplied.is_file() else supplied.rglob('*')
        scanned = 0
        for path in entries:
            if not path.is_file() or path.is_symlink() or path in seen:
                continue
            seen.add(path)
            suffix = path.suffix.lower()
            if suffix not in ('.bin', '.md', '.gen', '.zip'):
                continue
            scanned += 1
            if scanned > 4096:
                raise ValueError('Choose a smaller ROM folder (over 4096 candidates)')
            if suffix == '.zip':
                if path.stat().st_size > 128*1024*1024:
                    continue
                try:
                    with zipfile.ZipFile(path) as archive:
                        for item in archive.infolist():
                            if Path(item.filename).suffix.lower() in ('.bin', '.md', '.gen') and 512 <= item.file_size <= MAX_ROM:
                                yield path, archive.read(item)
                except (zipfile.BadZipFile, RuntimeError, OSError):
                    continue
            elif 512 <= path.stat().st_size <= MAX_ROM:
                yield path, path.read_bytes()

def stage(root, paths):
    root = Path(root)
    by_hash = qualified()
    found = {}
    for path, data in candidates(paths):
        digest = hashlib.sha256(data).hexdigest()
        if digest in by_hash:
            found.setdefault(by_hash[digest], (path, data, digest))
    missing = [TITLES[g] for g in TITLES if g not in found]
    if missing:
        raise ValueError('Qualified ROMs not found: ' + '; '.join(missing) +
                         '. Both exact dumps are required (see docs/INSTALLATION.md); the USA Super Monaco GP dump is not qualified.')
    target = root/'.local/install-roms'
    target.mkdir(parents=True, exist_ok=True)
    for game, (source, data, digest) in sorted(found.items()):
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, 'w', zipfile.ZIP_STORED) as z:
            z.writestr(game+'.bin', data)
        final = target/(game+'.zip'); temporary = target/('.rom-'+uuid.uuid4().hex)
        temporary.write_bytes(buffer.getvalue()); os.replace(temporary, final)
        with zipfile.ZipFile(final) as z:
            if hashlib.sha256(z.read(game+'.bin')).hexdigest() != digest:
                raise ValueError(f'Failed to verify private ROM copy: {game}')
        print(f'[ROM] {TITLES[game]}: verified and staged ({digest})', flush=True)
    return sorted(found)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom-dir', action='append', required=True)
    args = parser.parse_args()
    try:
        stage(ROOT, args.rom_dir)
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        raise SystemExit(str(error))
