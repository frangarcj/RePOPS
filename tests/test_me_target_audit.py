"""Static inventory tests, using only synthetic words and temporary files."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from audit_me_targets import checked_image, measure


class MeTargetAuditTests(unittest.TestCase):
    def test_word_count_and_return(self):
        data = struct.pack('<II', 0x03E00008, 0)
        report = measure(data, 0x100)
        self.assertEqual(report['bytes'], 8)
        self.assertEqual(report['decoded_words'], 2)
        self.assertEqual(report['unsupported_words'], 0)
        self.assertEqual(report['sha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(report['indirect_sites_including_returns'][0]['address'], '0x00000100')

    def test_invalid_window(self):
        for data, start in [(b'', 0), (b'123', 0), (b'1234', 1), (b'1234', -4)]:
            with self.subTest(data=data, start=start), self.assertRaises(ValueError):
                measure(data, start)

    def test_hash_rejected_before_elf_parse(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'not_firmware'
            path.write_bytes(b'fixture')
            with self.assertRaisesRegex(ValueError, 'Wrong firmware hash'):
                checked_image(path, '0' * 64)

    def test_existing_results_refused_before_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/audit_me_targets.py'),
                                     '--out', directory, '--pops', 'does-not-exist'],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Refusing existing output', result.stderr)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_manifest_includes_sample_producer_not_only_stubs(self):
        spec = json.loads((ROOT / 'data/me_reverse_targets.json').read_text())
        names = [row['name'] for row in spec['windows']]
        self.assertEqual(len(names), len(set(names)))
        self.assertIn('me_callback_candidate_window', names)
        self.assertIn('me_sample_and_ack_loop', names)
        self.assertNotEqual(spec['provider_sha256'], spec['ark_provider_sha256'])
        for row in spec['windows']:
            start, end = int(row['start'], 0), int(row['end_exclusive'], 0)
            self.assertGreater(end, start)
            self.assertEqual((start | end) & 3, 0)


if __name__ == '__main__':
    unittest.main()
