"""Refusal guards for the PSP hybrid builder; no firmware is needed."""
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import build_psp_hybrid as builder


class HybridBuilderGuards(unittest.TestCase):
    def test_wrong_firmware_rejected_before_audit(self):
        with patch.object(builder, 'Image') as image:
            image.return_value.data = b'not the pinned firmware'
            with self.assertRaisesRegex(ValueError, 'Wrong original POPS hash'):
                builder.check_input(Path('unused'), Path('missing-audit'))

    def test_existing_output_is_preserved(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)
            sentinel = path / 'keep.txt'
            sentinel.write_text('keep')
            with patch.object(sys, 'argv', ['build_psp_hybrid.py', '--audit', 'unused', '--out', root]):
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                    builder.main()
            self.assertEqual(error.exception.code, 2)
            self.assertEqual(sentinel.read_text(), 'keep')
            self.assertEqual(sorted(p.name for p in path.iterdir()), ['keep.txt'])


if __name__ == '__main__':
    unittest.main()
