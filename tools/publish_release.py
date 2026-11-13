"""Build the ROM-free installer ZIP + SHA-256 manifest that tools/update_release.py expects.

Local only: nothing is uploaded. Upload the ZIP to a GitHub release tagged v<version>;
GitHub then reports the asset digest that the updater compares with the downloaded bytes.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import shutil
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('updater', ROOT/'tools/update_release.py')
updater = importlib.util.module_from_spec(spec); spec.loader.exec_module(updater)

TOP = ('CMakeLists.txt', 'README.md', 'CREDITS.md', 'CONTRIBUTING.md', 'THIRD_PARTY_NOTICES.md', 'Jogar.cmd', 'SDL2.dll')
DOCS = ('INSTALLATION.md', 'CONTROLS.md', 'BUILDING.md', 'VIDEO.md', 'RELEASE-0.50.md', 'ROADMAP.md')
UNUSED_ASSETS = ('assets/audio17/',)   # superseded sound pack, not read by the runtime
TOOLS = ('prepare_inputs.py', 'patch_runtime.py', 'install_roms.py', 'update_release.py', 'validate_installed.py', 'publish_release.py')
SCRIPTS = ('build.ps1', 'install.ps1')
TOOL_DIRS = ('python', 'cmake', 'ninja', 'engine', 'librashader')
SKIP_SUFFIX = {'.pyc', '.pdb', '.exp', '.partial', '.zip'}
REQUIRED = ('SuperMonacoDuoRecomp.exe', 'scripts/install.ps1', 'scripts/build.ps1', 'tools/update_release.py',
            'tools/install_roms.py', 'tools/prepare_inputs.py', 'CMakeLists.txt', 'docs/INSTALLATION.md')
# Extra files a real public release must carry (the synthetic tests build smaller trees).
PUBLIC_REQUIRED = ('README.md', 'CREDITS.md', 'CONTRIBUTING.md', 'THIRD_PARTY_NOTICES.md', 'games/supported-roms.json',
                   'assets/launcher/masthead.png', 'assets/audio18/arcade-sound.sb17', 'assets/audio18/gp2-victory.wav',
                   'assets/audio18/PROVENANCE.json', 'src/presentation/formation_art20.h', 'docs/licenses/SDL2.txt')

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()

def release_name(label):
    return f'SuperMonacoDuoRecomp-{label}-Windows-x64'

def gather(root, launcher):
    """Return {package path: source path}; user ROMs and generated code are never selected."""
    root = Path(root); files = {}
    def add(rel, src):
        src = Path(src)
        # ZIPs are skipped (user ROM archives) except inside .tools, where Python's own python313.zip
        # (the standard library) and the engine's SDL2 archive are required by the toolchain.
        skipped = SKIP_SUFFIX - {'.zip'} if rel.startswith('.tools/') else SKIP_SUFFIX
        if src.is_file() and src.suffix.lower() not in skipped and '__pycache__' not in src.parts:
            files[rel] = src
    add('SuperMonacoDuoRecomp.exe', launcher)
    for name in TOP: add(name, root/name)
    for name in SCRIPTS: add('scripts/'+name, root/'scripts'/name)
    for name in TOOLS: add('tools/'+name, root/'tools'/name)
    for name in DOCS: add('docs/'+name, root/'docs'/name)
    for p in sorted((root/'docs/licenses').rglob('*')): add(p.relative_to(root).as_posix(), p)
    add('games/supported-roms.json', root/'games/supported-roms.json')
    for folder in ('src', 'assets'):
        for p in sorted((root/folder).rglob('*')):
            rel = p.relative_to(root).as_posix()
            if not rel.startswith(UNUSED_ASSETS): add(rel, p)
    for folder in TOOL_DIRS:
        for p in sorted((root/'.tools'/folder).rglob('*')): add(p.relative_to(root).as_posix(), p)
    return files

def audit(files):
    """Refuse private inputs: supported ROM hashes, ROM names, local data, generated code."""
    roms = set(updater_hashes())
    for rel, src in files.items():
        first = rel.split('/')[0]
        if first in {'.local', 'build', 'dist'} or Path(rel).name.lower() in {'game.bin', 'smgp.zip'}:
            raise ValueError('Private/generated file selected: ' + rel)
        if src.stat().st_size <= 8*1024*1024 and sha(src) in roms:
            raise ValueError('A qualified ROM would be redistributed: ' + rel)
    for rel in REQUIRED:
        if rel not in files:
            raise ValueError('Release payload is incomplete: ' + rel)

def updater_hashes():
    spec = importlib.util.spec_from_file_location('prepare_inputs', ROOT/'tools/prepare_inputs.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module.SUPPORTED

def build(root, label, output, launcher, public=False):
    updater.version(label)
    if label.startswith('v'): raise ValueError('Use the version without the leading v')
    files = gather(root, launcher); audit(files)
    if public:
        missing = [rel for rel in PUBLIC_REQUIRED if rel not in files]
        if missing: raise ValueError('Public release payload is incomplete: ' + ', '.join(missing))
    name = release_name(label); stage = Path(output)/name
    if stage.exists(): shutil.rmtree(stage)
    if len(files) + 1 > updater.MAX_ENTRIES: raise ValueError('Too many files for the updater limit')
    for rel, src in files.items():
        dst = stage/rel; dst.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(src, dst)
    manifest = dict(version=label, kind='installer', roms_included=False, files=[
        dict(path=rel, bytes=(stage/rel).stat().st_size, sha256=sha(stage/rel)) for rel in sorted(files, key=str.casefold)])
    (stage/'PACKAGE-MANIFEST.json').write_text(json.dumps(manifest, indent=2)+'\n')
    archive = Path(output)/(name+'.zip'); temporary = archive.with_suffix('.zip.partial')
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for item in manifest['files']: z.write(stage/item['path'], f"{name}/{item['path']}")
        z.write(stage/'PACKAGE-MANIFEST.json', f'{name}/PACKAGE-MANIFEST.json')
    with zipfile.ZipFile(temporary) as z:
        if z.testzip() is not None: raise ValueError('The release ZIP failed verification')
    temporary.replace(archive)
    size = archive.stat().st_size; digest = sha(archive)
    if not 0 < size < 2*1024**3: raise ValueError('Release asset must be smaller than 2 GB (updater limit)')
    asset = dict(version=label, name=archive.name, size=size, digest='sha256:'+digest, state='uploaded',
                 browser_download_url=f'https://github.com/{updater.REPO}/releases/download/v{label}/{archive.name}')
    updater.validate_asset(asset)
    (Path(output)/'SHA256SUMS.txt').write_text(f'{digest}  {archive.name}\n')
    (Path(output)/'release-asset.json').write_text(json.dumps(asset, indent=2)+'\n')
    return archive, asset

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--version', default=updater.CURRENT)
    p.add_argument('--output', type=Path, default=ROOT/'dist/release')
    p.add_argument('--launcher', type=Path, default=ROOT/'build/native20/bin/SuperMonacoDuoRecomp.exe')
    a = p.parse_args()
    try:
        archive, asset = build(ROOT, a.version, a.output, a.launcher, public=True)
    except (ValueError, OSError) as error:
        sys.exit(str(error))
    print(f"{archive} {asset['size']} bytes {asset['digest']}")
