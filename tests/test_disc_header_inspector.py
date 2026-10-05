"""Synthetic PBP metadata tests; no firmware or game data needed."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from inspect_disc_header import inspect, HEADER_SIZE


class DiscHeaderTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / 'fixture.pbp'
        self.data = bytearray(64 + HEADER_SIZE)
        self.data[:4] = b'\0PBP'
        struct.pack_into('<8I', self.data, 8, 40, 40, 40, 40, 40, 40, 40, 64)
        self.data[40:44] = b'\x7fELF'
        self.data[64:76] = b'PSISOIMG0000'
        self.data[64 + 0x400:64 + 0x40B] = b'_TEST_00001'

    def check_bad(self):
        self.path.write_bytes(self.data)
        with self.assertRaises(ValueError):
            inspect(self.path)

    def test_plain_header(self):
        self.path.write_bytes(self.data)
        report = inspect(self.path)
        self.assertEqual(report['header_bytes'], HEADER_SIZE)
        self.assertEqual(report['disc_id_ascii'], '_TEST_00001')
        self.assertEqual(report['provider_result_word'], 0x464C457F ^ 0x4A08B53F)

    def test_truncation(self):
        self.data = self.data[:-1]
        self.check_bad()

    def test_wrong_pbp(self):
        self.data[0] = 1
        self.check_bad()

    def test_wrong_disc_magic(self):
        self.data[64] = 0
        self.check_bad()

    def test_reversed_components(self):
        struct.pack_into('<I', self.data, 16, 80)
        self.check_bad()

    def test_empty_executable_component(self):
        struct.pack_into('<I', self.data, 32, 64)
        self.check_bad()


if __name__ == '__main__':
    unittest.main()
