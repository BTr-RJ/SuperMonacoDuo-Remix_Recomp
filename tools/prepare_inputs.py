"""Identify local cartridges and stage hash-bound native build inputs."""
from pathlib import Path
import hashlib, json, struct, zipfile, zlib

ROOT = Path(__file__).resolve().parents[1]
SUPPORTED={
 '9046c9d67f15ab0f68d9e73b0cc516b6fd1637a8db0da4a03dcff8933c12dfa1':'smgp1',
 'd3c60d34571c5684b0cff5196c7c6db7364bee7d8b91048f0f3ae19d8f1a1db6':'smgp1-us',
 'c02206602e00dd8e9b6c38943b8a3be41e37d2d21c1768cf342900bce11ab4f0':'smgp2',
}
def write_changed(path,data):
    content=data.encode('utf-8') if isinstance(data,str) else data
    if not path.exists() or path.read_bytes()!=content:path.write_bytes(content)
def main():
    found = {}
    # .local/install-roms holds hash-verified copies staged by tools/install_roms.py.
    for archive in sorted([*ROOT.glob('*.zip'),*(ROOT/'.local/install-roms').glob('*.zip')]):
        with zipfile.ZipFile(archive) as z:
            for name in z.namelist():
                if not name.lower().endswith(('.bin', '.md', '.gen')):
                    continue
                data = z.read(name)
                title = data[0x150:0x180].decode('ascii', errors='replace').strip()
                digest=hashlib.sha256(data).hexdigest()
                game=SUPPORTED.get(digest)
                if game is None:raise ValueError(f'Unsupported SHA256: {archive.name}/{name}: {digest}')
                # USA is retained as a supplied reference, but this release
                # qualifies only the first game's World revision.
                if game=='smgp1-us':continue
                record = dict(game=game, archive=archive.name, entry=name, title=title,
                              size=len(data), sha256=hashlib.sha256(data).hexdigest(),
                              crc32=f'{zlib.crc32(data):08X}',
                              vectors=[f'{x:08X}' for x in struct.unpack('>64I', data[:256])])
                print(game,record['sha256'],record['title'],flush=True)
                # Prefer the World cartridge when both first-game revisions exist.
                if game not in found or 'World' in archive.name:
                    found[game] = (data, record)
    records=[]
    for game, (data, record) in found.items():
        stage = ROOT / '.local/build-inputs' / game
        stage.mkdir(parents=True, exist_ok=True)
        write_changed(stage/'game.bin',data)
        vectors = struct.unpack('>64I', data[:256])
        entries=sorted({x for x in vectors[1:] if 0x200<=x<len(data) and x%2==0})
        config = ('[game]\noutput_prefix = "'+game+'"\ndiscovery_files = ["discovery.toml"]\n'
                  '[ram_layout]\ninitial_ssp = '+hex(vectors[0])+'\n'
                  'vbla_stack = '+hex(vectors[0])+'\nintr_stack = '+hex(vectors[0])+'\n'
                  '[functions]\nextra = ['+', '.join(hex(x) for x in entries)+']\nblacklist = []\n')
        if game=='smgp1':
            # Observers see complete object projections before screen culling.
            # No register, flag, branch or guest-memory changes are permitted.
            # $61CA is the separately qualified proportional steering adapter.
            for pc in [0xd8e,0xe00,0xe6c,0xe86,0xed4,0xee8,0x61ca,0x7b76,0x761dc]:
                config+=f'\n[[widescreen_site]]\naddr = {hex(pc)}\nkind = "game_hook"\n'
        else:
            # Senna USA has distinct RAM pools, mapping format and builders.
            # 13AEC is the separately qualified GP II proportional adapter;
            # it changes only D7 before the original wheel clamp/conversion.
            for pc in [0xd3a4,0xd3e2,0xd49c,0xd588,0xd434,0xd55c,0xd4e4,0xd5d0,0x13196,0x13aec,0xc5be,0xc5e0,0xc5e6,0xc616,0xc656,0xc66c,0x11c5e]:
                config+=f'\n[[widescreen_site]]\naddr = {hex(pc)}\nkind = "game_hook"\n'
        write_changed(stage/'game.toml',config)
        # Observed code is a lead, not an automatically qualified translation.
        # Bulk promotion exposed a codegen regression; baseline roots stay pinned.
        write_changed(stage/'discovery.toml','[functions]\nextra = []\n')
        header=ROOT/'src/game'/f'{game}_identity.h'
        write_changed(header,f'#define GAME_NAME "{record["title"]}"\n'
                          f'#define GAME_SHORT "{game}"\n#define GAME_CRC 0x{record["crc32"]}u\n'
                          f'#define GAME_SIZE {len(data)}u\n#define GAME_ENTRY 0x{vectors[1]:X}u\n'
                          f'#define GAME_VINT 0x{vectors[30]:X}u\n#define GAME_HINT 0x{vectors[28]:X}u\n')
        records.append(record)
    write_changed(ROOT/'.local/roms.json',json.dumps(records, indent=2))
    if len(found)!=2:
        raise ValueError('Both games are required')
if __name__=='__main__': main()
