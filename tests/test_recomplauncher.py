"""recomplauncher.json must stay valid, shipped, and consistent with the prepare script."""
from pathlib import Path
import importlib.util
import json
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, rel):
    spec = importlib.util.spec_from_file_location(name, ROOT/rel)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


class RecompLauncherTests(unittest.TestCase):
    def setUp(self):
        self.doc = json.loads((ROOT/'recomplauncher.json').read_text(encoding='utf-8'))

    def test_manifest_uses_only_documented_level_1_and_3_keys(self):
        self.assertEqual(self.doc['format'], 1)
        self.assertLessEqual(set(self.doc), {'$schema', 'format', 'name', 'game', 'start', 'prepare'})
        self.assertIn(self.doc['game']['kind'], {'iso', 'folder', 'iso_or_folder'})
        self.assertTrue(self.doc['start']['program'] and self.doc['prepare']['program'])
        self.assertGreaterEqual(self.doc['prepare']['timeout'], 1)

    def test_programs_and_script_are_shipped_in_every_release(self):
        publish = load('publish', 'tools/publish_release.py'); updater = publish.updater
        self.assertIn('recomplauncher.json', publish.TOP)
        self.assertIn('recomplauncher.json', updater.TOP)
        self.assertIn('recomplauncher_prepare.py', publish.TOOLS)
        self.assertIn('tools/recomplauncher_prepare.py', publish.PUBLIC_REQUIRED)
        self.assertEqual(self.doc['start']['program'][0], 'SuperMonacoDuoRecomp.exe')
        self.assertEqual(self.doc['prepare']['program'][0], '.tools/python/python.exe')
        script = self.doc['prepare']['args'][0]
        self.assertEqual(script, '{install}/tools/recomplauncher_prepare.py')
        self.assertTrue((ROOT/script.replace('{install}/', '')).is_file())

    def test_prepare_passes_the_player_folder_and_install_folder(self):
        args = self.doc['prepare']['args']
        self.assertEqual(args[args.index('--game')+1], '{game}')
        self.assertEqual(args[args.index('--install')+1], '{install}')

    def test_no_game_data_in_the_manifest(self):
        text = json.dumps(self.doc).lower()
        for forbidden in ('crc32', 'sha256', '.bin', '.md"'):
            self.assertNotIn(forbidden, text)


if __name__ == '__main__':
    unittest.main()