import json
from pathlib import Path
import runpy
import tempfile
import unittest

locate = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'bin/find-steam-game'))['locate']

class SteamLibraryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.prefix = self.base / 'prefix'
        self.steam = self.prefix / 'drive_c/Program Files (x86)/Steam'
        (self.steam / 'steamapps').mkdir(parents=True)
        (self.prefix / 'dosdevices').mkdir()
        (self.prefix / 'dosdevices/c:').symlink_to(self.prefix / 'drive_c')

    def install(self, library, complete=True):
        apps = library / 'steamapps'; apps.mkdir(parents=True, exist_ok=True)
        (apps / 'appmanifest_1281220.acf').write_text('"AppState" { "appid" "1281220" "installdir" "Task Force Admiral" "buildid" "42" }')
        game = apps / 'common/Task Force Admiral'
        if complete:
            (game / 'bin/master').mkdir(parents=True)
            (game / 'bin/master/TFA.exe').touch()
        return game.resolve()

    def register(self, drive, windows_path):
        target = self.base / ('drive-' + drive); target.mkdir()
        (self.prefix / 'dosdevices' / (drive.lower() + ':')).symlink_to(target)
        (self.steam / 'steamapps/libraryfolders.vdf').write_text('"libraryfolders" { "1" { "path" ' + json.dumps(windows_path) + ' } }')
        return target / windows_path[3:].replace('\\', '/')

    def test_default_c_without_additional_library(self):
        game = self.install(self.steam)
        self.assertEqual(locate(self.prefix, '1281220', 'bin/master/TFA.exe')['game_directory'], str(game))

    def test_registered_s_library(self):
        game = self.install(self.register('S', 'S:\\SteamLibrary'))
        self.assertEqual(locate(self.prefix, '1281220', 'bin/master/TFA.exe')['game_directory'], str(game))

    def test_custom_drive_with_spaces(self):
        game = self.install(self.register('D', 'D:\\My Games\\Steam Library'))
        self.assertEqual(locate(self.prefix, '1281220', 'bin/master/TFA.exe')['game_directory'], str(game))

    def test_incomplete_download_rejected(self):
        self.install(self.steam, complete=False)
        with self.assertRaisesRegex(ValueError, 'not installed'):
            locate(self.prefix, '1281220', 'bin/master/TFA.exe')

    def test_duplicate_installations_rejected(self):
        self.install(self.steam)
        self.install(self.register('S', 'S:\\SteamLibrary'))
        with self.assertRaisesRegex(ValueError, 'Multiple installations'):
            locate(self.prefix, '1281220', 'bin/master/TFA.exe')

if __name__ == '__main__':
    unittest.main()
