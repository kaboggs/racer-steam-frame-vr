"""Installer safety tests using invented temporary files, never a game installation."""
import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('setup', Path(__file__).resolve().parents[1] / 'scripts/setup.py')
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)

class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'game'; self.game.mkdir()
        (self.game / 'SWEP1RCR.EXE').write_text('invented test marker, not game code')
        self.home = self.root / 'home'; self.home.mkdir()
        self.payload = self.root / 'dist/payload'; self.payload.mkdir(parents=True)
        self.names = ['dinput.dll', 'openvr_api.dll', 'assets/racer_openvr_actions.json', 'assets/racer_frame_bindings.json', 'assets/shaders/test.vert']
        for name in self.names:
            p=self.payload/name; p.parent.mkdir(parents=True, exist_ok=True); p.write_text('invented mod '+name)
        self.manifest()
        self.addCleanup(patch.stopall)
        patch.object(s, 'ROOT', self.root).start()
        patch.object(s, 'PAYLOAD', self.payload).start()
        patch.object(s.Path, 'home', return_value=self.home).start()
        patch.object(s.subprocess, 'check_output', return_value='').start()
        self.output=io.StringIO()
        self.redirect = contextlib.redirect_stdout(self.output)
        self.redirect.__enter__()
        self.addCleanup(self.redirect.__exit__, None, None, None)

    def manifest(self):
        files={str(p.relative_to(self.payload)):s.digest(p) for p in self.payload.rglob('*') if p.is_file()}
        (self.root/'dist/manifest.json').write_text(json.dumps({'files':files}))

    def install(self, dry=False):
        s.install(SimpleNamespace(game_dir=str(self.game), dry_run=dry))

    def backup(self):
        return next((self.home/'.local/share/racer-frame-vr/backups').iterdir())

    def test_dry_run_writes_nothing(self):
        self.install(True)
        self.assertEqual(list(self.game.iterdir()), [self.game/'SWEP1RCR.EXE'])
        self.assertEqual(list(self.home.iterdir()), [])

    def test_round_trip_restores_existing_and_removes_new_files(self):
        (self.game/'dinput.dll').write_text('old mod')
        self.install()
        backup=self.backup()
        s.restore(SimpleNamespace(backup=str(backup),dry_run=False))
        self.assertEqual((self.game/'dinput.dll').read_text(),'old mod')
        for name in self.names[1:]: self.assertFalse((self.game/name).exists())
        self.assertEqual((self.game/'SWEP1RCR.EXE').read_text(),'invented test marker, not game code')

    def test_changed_file_stops_all_restore_writes(self):
        self.install(); backup=self.backup()
        (self.game/'openvr_api.dll').write_text('later user change')
        with self.assertRaisesRegex(RuntimeError,'changed'): s.restore(SimpleNamespace(backup=str(backup),dry_run=False))
        self.assertEqual((self.game/'dinput.dll').read_text(),'invented mod dinput.dll')

    def test_extra_game_file_in_payload_is_rejected(self):
        (self.payload/'SWEP1RCR.EXE').write_text('invented extra')
        self.manifest()
        with self.assertRaisesRegex(RuntimeError,'Non-mod'): self.install()
        self.assertEqual(list(self.game.iterdir()),[self.game/'SWEP1RCR.EXE'])

    def test_modified_payload_is_rejected(self):
        (self.payload/'dinput.dll').write_text('changed')
        with self.assertRaisesRegex(RuntimeError,'checksum'): self.install()

    def test_symlinked_payload_directory_is_rejected(self):
        (self.payload/'assets').rename(self.root/'outside')
        (self.payload/'assets').symlink_to(self.root/'outside',target_is_directory=True)
        with self.assertRaises(RuntimeError): self.install()

    def test_escaping_destination_is_rejected(self):
        (self.game/'assets').symlink_to(self.home, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError,'symlink|escaping'): self.install()

    def test_temporary_conflict_stops_before_any_write(self):
        (self.game/'openvr_api.dll.racer-frame-installing').write_text('existing')
        with self.assertRaisesRegex(RuntimeError,'Temporary'): self.install()
        self.assertFalse((self.game/'dinput.dll').exists())

    def test_path_traversal_not_allowed(self):
        self.assertFalse(s.allowed('assets/shaders/../../SWEP1RCR.EXE'))
        self.assertFalse(s.allowed('../dinput.dll'))

if __name__=='__main__': unittest.main()
  
