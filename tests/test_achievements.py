"""Offline achievement sets: structure, addresses and absence of account data."""
from pathlib import Path
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {'smgp1': (111, 29, 320), 'smgp2': (103, 53, 585)}


class AchievementAssets(unittest.TestCase):
    def load(self, game):
        return json.loads((ROOT / f'assets/achievements/{game}.json').read_text(encoding='utf-8'))

    def test_counts_and_points_match_the_retroachievements_sets(self):
        for game, (ra_id, count, points) in EXPECTED.items():
            data = self.load(game)
            self.assertEqual(data['ra_game_id'], ra_id)
            self.assertEqual(len(data['achievements']), count)
            self.assertEqual(sum(a['points'] for a in data['achievements']), points)
            self.assertEqual(data['total_points'], points)

    def test_entries_are_well_formed(self):
        for game in EXPECTED:
            ids = set()
            for a in self.load(game)['achievements']:
                self.assertNotIn(a['id'], ids)
                ids.add(a['id'])
                self.assertTrue(a['title'] and a['description'] and a['memaddr'])
                self.assertNotEqual(a['id'], 101000001, 'the unknown-emulator warning is not an achievement')

    def test_every_address_is_inside_the_64k_work_ram_view(self):
        # Mega Drive sets read 68000 work RAM through offsets 0..0xFFFF (word-swizzled by the host).
        for game in EXPECTED:
            for a in self.load(game)['achievements']:
                for token in re.findall(r'0x[A-Za-z ]?([0-9a-fA-F]{6})', a['memaddr']):
                    self.assertLess(int(token, 16), 0x10000, (game, a['id'], token))

    def test_no_account_data_is_shipped(self):
        for game in EXPECTED:
            text = (ROOT / f'assets/achievements/{game}.json').read_text(encoding='utf-8').lower()
            for needle in ('"token"', '"username"', 'apikey', 'api_key', 'password'):
                self.assertNotIn(needle, text)


if __name__ == '__main__':
    unittest.main()
