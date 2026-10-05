"""Firmware-free checks of ME worker test vectors and refusal guards."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from verify_me_worker import checked_code, vectors


class MeWorkerHarnessTests(unittest.TestCase):
    def test_vector_counts_and_boundaries(self):
        cases = vectors()
        self.assertEqual(len(cases), 856)
        self.assertEqual(len(vectors(count=0)), 600)
        self.assertIn((-32768, 32767, 0, 0xFFFFFFFF), cases)
        self.assertIn((-1, 1, 2, 4), cases)

    def test_determinism_and_sample_domain(self):
        self.assertEqual(vectors(660), vectors(660))
        self.assertNotEqual(vectors(660)[600:], vectors(661)[600:])
        self.assertTrue(all(-32768 <= lo <= 32767 and -32768 <= hi <= 32767
                            for lo, hi, _, _ in vectors()))

    def test_wrong_source_rejected_before_export_read(self):
        with patch("verify_me_worker.Image", return_value=SimpleNamespace(data=b"wrong firmware")):
            with self.assertRaisesRegex(ValueError, "Wrong corpus POPSMAN hash"):
                checked_code(Path("missing.prx"), Path("missing-export"))

    def test_existing_result_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "result"
            out.mkdir()
            marker = out / "retain.txt"
            marker.write_text("retain")
            run = subprocess.run([sys.executable, str(ROOT / "scripts/verify_me_worker.py"),
                                  "--relocated", "missing-export", "--out", str(out)],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 2)
            self.assertIn("Refusing existing output", run.stderr)
            self.assertEqual(marker.read_text(), "retain")


if __name__ == "__main__":
    unittest.main()
