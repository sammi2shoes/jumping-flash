"""Verify analysis bundle keeps useful code, original bytes and transfer bounds."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('prepare_ax12_apk', Path(__file__).resolve().parents[1] / 'scripts/prepare_ax12_apk.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class BundleTests(unittest.TestCase):
    def test_selection_keeps_native_app_dex_scripts_not_qt_or_unsafe_paths(self):
        names = ['AndroidManifest.xml', 'resources.arsc', 'classes.dex', 'classes2.dex',
                 'lib/arm64-v8a/libRadioMasterAX_arm64-v8a.so', 'lib/arm64-v8a/libusb.so',
                 'lib/arm64-v8a/libQt6Core.so', 'lib/arm64-v8a/libc++_shared.so',
                 'lib/arm64-v8a/libplugins_platforms_qt.so', 'lib/armeabi-v7a/libRadioMasterAX.so',
                 'assets/test.lua', 'assets/map.png', 'assets/../bad.lua', '/assets/abs.lua']
        selected, abi = tool.selected_names(names)
        self.assertEqual(abi, 'arm64-v8a')
        self.assertEqual(selected, sorted(names[:6] + ['assets/test.lua']))

    def test_original_unchanged_and_bundle_exact_and_split_rejoins(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            apk = root / 'radio.apk'
            with zipfile.ZipFile(apk, 'w') as z:
                z.writestr('classes.dex', b'FAKE DEX')
                z.writestr('assets/test.lua', b'return {}')
            original = apk.read_bytes()
            upload = tool.prepare(apk, root / 'normal')
            self.assertEqual(apk.read_bytes(), original)
            with zipfile.ZipFile(upload[0]) as z:
                self.assertEqual(z.read('classes.dex'), b'FAKE DEX')
                self.assertIn('AX12-ANALYSIS-INDEX.json', z.namelist())
            old_limit = tool.LIMIT
            try:
                tool.LIMIT = 100
                parts = tool.prepare(apk, root / 'split')
                self.assertGreater(len(parts), 1)
                self.assertTrue(all(p.stat().st_size <= tool.LIMIT for p in parts))
                self.assertEqual(b''.join(p.read_bytes() for p in parts), (root / 'split/AX12-analysis.zip').read_bytes())
            finally:
                tool.LIMIT = old_limit


if __name__ == '__main__':
    unittest.main()
