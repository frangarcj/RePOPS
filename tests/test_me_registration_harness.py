"""Harness guards and vectors; no firmware or Unicorn installation required."""
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import verify_me_registration as harness


class RegistrationHarnessTests(unittest.TestCase):
    def test_vector_counts(self):
        self.assertEqual(len(harness.make_vectors(660, 0)), 320 + 32)
        self.assertEqual(len(harness.make_vectors(660, 1024)), 1376)

    def test_reproducible_random_vectors(self):
        self.assertEqual(harness.make_vectors(660, 12), harness.make_vectors(660, 12))
        self.assertNotEqual(harness.make_vectors(660, 12), harness.make_vectors(661, 12))

    def test_pointer_mask_boundary_is_present(self):
        vectors = harness.make_vectors(660, 0)[:320]
        triples = {(v["entry"], v["stack"], v["k1"]) for v in vectors}
        self.assertIn((0x80000000, 0, 0x100000), triples)
        self.assertIn((0, 0x80000000, 0x100000), triples)
        self.assertIn((0, 0, 0x100000), triples)

    def test_repeat_sequence_preserves_state(self):
        tail = harness.make_vectors(660, 10)[-32:]
        self.assertEqual([v["reset"] for v in tail], [True] + [False] * 31)
        self.assertEqual(tail[0]["stack"], 0x09FF8000)
        self.assertEqual(tail[1]["stack"], 0x00010001)

    def test_wrong_elf_hash_is_rejected_before_export_read(self):
        with patch.object(harness, "Image") as image:
            image.return_value.data = b"synthetic wrong firmware"
            with self.assertRaisesRegex(ValueError, "Wrong POPSMAN ELF hash"):
                harness.checked_export(Path("unused"), Path("not-a-directory"))

    def test_existing_output_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            marker = output / "keep.txt"
            marker.write_text("existing user work")
            argv = ["verify", "--out", str(output), "--relocated", "not-used"]
            with patch.object(sys, "argv", argv), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as caught:
                    harness.main()
            self.assertEqual(caught.exception.code, 2)
            self.assertEqual(marker.read_text(), "existing user work")


if __name__ == "__main__":
    unittest.main()
