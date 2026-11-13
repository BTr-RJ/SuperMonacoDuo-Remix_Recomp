"""Installer boundaries using synthetic ROMs only (no real cartridge data)."""
from pathlib import Path
import hashlib
import importlib.util
import json
import tempfile
import unittest
import unittest.mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/'tools'/f'{name}.py')
    result = importlib.util.module_from_spec(spec); spec.loader.exec_module(result); return result
roms = module('install_roms'); publish = module('publish_release'); prepare = module('prepare_inputs')

class InstallationTests(unittest.TestCase):
    def fixture(self):
        a = b'SYNTHETIC WORLD'*100; b = b'SYNTHETIC SENNA'*100
        table = {hashlib.sha256(a).hexdigest(): 'smgp1', hashlib.sha256(b).hexdigest(): 'smgp2'}
        return a, b, unittest.mock.patch.object(roms, 'qualified', return_value=table)

    def test_qualified_hashes_come_from_prepare_inputs_and_exclude_unqualified_usa(self):
        table = roms.qualified()
        self.assertEqual(sorted(table.values()), ['smgp1', 'smgp2'])
        self.assertEqual(table['9046c9d67f15ab0f68d9e73b0cc516b6fd1637a8db0da4a03dcff8933c12dfa1'], 'smgp1')
        self.assertEqual(table['c02206602e00dd8e9b6c38943b8a3be41e37d2d21c1768cf342900bce11ab4f0'], 'smgp2')
        self.assertEqual(prepare.SUPPORTED['d3c60d34571c5684b0cff5196c7c6db7364bee7d8b91048f0f3ae19d8f1a1db6'], 'smgp1-us')
    def test_zip_contents_are_hashed_and_entry_paths_are_not_extracted(self):
        a, b, patch = self.fixture()
        with tempfile.TemporaryDirectory() as tmp, patch:
            root = Path(tmp)/'install'; user = Path(tmp)/'user'; user.mkdir()
            with zipfile.ZipFile(user/'collection.zip', 'w') as z:
                z.writestr('../../escaped.bin', a); z.writestr('renamed-senna.txt.bin', b); z.writestr('x/other.md', b'unknown'*100)
            before = (user/'collection.zip').read_bytes()
            self.assertEqual(roms.stage(root, [user]), ['smgp1', 'smgp2'])
            self.assertEqual((user/'collection.zip').read_bytes(), before)
            self.assertFalse((root/'escaped.bin').exists()); self.assertFalse((Path(tmp)/'escaped.bin').exists())
            with zipfile.ZipFile(root/'.local/install-roms/smgp1.zip') as z: self.assertEqual(z.read('smgp1.bin'), a)
    def test_a_single_game_or_unknown_revision_leaves_no_private_inputs(self):
        a, b, patch = self.fixture()
        with tempfile.TemporaryDirectory() as tmp, patch:
            root = Path(tmp)/'install'; user = Path(tmp)/'user'; user.mkdir()
            (user/'world.gen').write_bytes(a); (user/'other.bin').write_bytes(b'unknown'*100)
            with self.assertRaisesRegex(ValueError, 'Qualified ROMs not found'): roms.stage(root, [user])
            self.assertFalse((root/'.local').exists())
    def test_duplicate_folders_stage_each_game_once(self):
        a, b, patch = self.fixture()
        with tempfile.TemporaryDirectory() as tmp, patch:
            root = Path(tmp)/'install'; user = Path(tmp)/'user'; user.mkdir()
            (user/'a.GEN').write_bytes(a); (user/'b.bin').write_bytes(b)
            self.assertEqual(roms.stage(root, [user, user]), ['smgp1', 'smgp2'])
    def test_release_selects_no_private_files_and_audits_rom_hashes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)/'project'
            names = ['CMakeLists.txt', 'README.md', 'SDL2.dll', 'scripts/install.ps1', 'scripts/build.ps1', 'docs/INSTALLATION.md',
                     'tools/update_release.py', 'tools/install_roms.py', 'tools/prepare_inputs.py', 'tools/check_world.py',
                     'src/main.cpp', 'assets/a.png', '.tools/python/python.exe', '.tools/ffmpeg/ffmpeg.exe', '.local/build-inputs/smgp1/game.bin',
                     'build/native20/bin/x.exe', 'smgp.zip', 'src/__pycache__/x.pyc',
                     '.tools/python/python313.zip', 'Super Monaco GP (World) (En,Ja).zip', 'assets/audio17/old.sb17']
            for n in names:
                p = root/n; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(n)
            launcher = root/'launcher.exe'; launcher.write_text('launcher')
            files = publish.gather(root, launcher)
            self.assertEqual(set(files), {'SuperMonacoDuoRecomp.exe', 'CMakeLists.txt', 'README.md', 'SDL2.dll', 'scripts/install.ps1', 'scripts/build.ps1',
                'docs/INSTALLATION.md', 'tools/update_release.py', 'tools/install_roms.py', 'tools/prepare_inputs.py', 'src/main.cpp', 'assets/a.png', '.tools/python/python.exe', '.tools/python/python313.zip'})
            publish.audit(files)
            rom = root/'assets/dump.bin'; rom.write_bytes(b'ROM'); files['assets/dump.bin'] = rom
            with unittest.mock.patch.object(publish, 'updater_hashes', return_value={hashlib.sha256(b'ROM').hexdigest(): 'smgp1'}):
                with self.assertRaisesRegex(ValueError, 'ROM would be redistributed'): publish.audit(files)
            files['.local/x'] = launcher
            with self.assertRaisesRegex(ValueError, 'Private/generated'): publish.audit({k: v for k, v in files.items() if k != 'assets/dump.bin'})
    def test_published_zip_matches_the_manifest_the_updater_verifies(self):
        updater = publish.updater
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)/'project'
            for n in ['CMakeLists.txt', 'scripts/install.ps1', 'scripts/build.ps1', 'docs/INSTALLATION.md', 'tools/update_release.py', 'tools/install_roms.py', 'tools/prepare_inputs.py']:
                p = root/n; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(n)
            launcher = root/'l.exe'; launcher.write_text('launcher'); out = Path(tmp)/'out'; out.mkdir()
            archive, asset = publish.build(root, '0.21', out, launcher)
            self.assertEqual(archive.name, 'SuperMonacoDuoRecomp-0.21-Windows-x64.zip')
            self.assertEqual(asset['digest'], 'sha256:'+hashlib.sha256(archive.read_bytes()).hexdigest())
            self.assertEqual(updater.select_release([dict(tag_name='v0.21', assets=[asset])], '0.20')['name'], archive.name)
            with tempfile.TemporaryDirectory() as work, unittest.mock.patch.object(updater, 'WORK_BASE', Path(work)):
                inst = Path(tmp)/'installed'; inst.mkdir()
                (inst/'.local').mkdir()
                (inst/'.local/installation.json').write_text(json.dumps(dict(product='SuperMonacoDuoRecomp', games=['smgp1', 'smgp2'])))
                for g in ('smgp1', 'smgp2'):
                    for rel in (f'.local/build-inputs/{g}/game.bin', f'{updater.BUILD}/bin/{g}/{g}.exe'):
                        (inst/rel).parent.mkdir(parents=True, exist_ok=True); (inst/rel).write_text('x')
                job = updater.new_job(inst)
                package = updater.extract(archive, asset, job)
                self.assertTrue((package/'scripts/install.ps1').is_file())

if __name__ == '__main__': unittest.main()
