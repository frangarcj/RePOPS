import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from function_progress import load_register, observed_trace, summarize


class FunctionProgressTests(unittest.TestCase):
    def test_actual_register_has_no_duplicate_or_missing_sources(self):
        rows = load_register(ROOT / 'data/function_progress.csv')
        summary = summarize(rows)
        self.assertEqual(sum(sum(v.values()) for v in summary.values()), len(rows))
        self.assertIn('popsman_ark_660', summary)
        self.assertIn('popsman_corpus_660', summary)

    def test_partial_calls_do_not_become_completed(self):
        rows = [{'profile': 'fixture', 'status': 'partial'}] * 2
        self.assertEqual(summarize(rows)['fixture']['complete'], 0)
        self.assertEqual(summarize(rows)['fixture']['partial'], 2)

    def test_trace_counts_unique_entries_only(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            path.write_text('\n'.join(json.dumps({'kind': 'native_c_function', 'address': x})
                                      for x in [0x16000, 0x16000, 0x16080]))
            self.assertEqual(observed_trace(path), {0x16000, 0x16080})

    def test_duplicate_original_entry_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'duplicate.csv'
            path.write_text('profile,entry,status,source,integration\n'
                            'pops,0x0,pending,,not_integrated\n'
                            'pops,0x0000,pending,,not_integrated\n')
            with self.assertRaisesRegex(ValueError, 'Duplicate'):
                load_register(path)


if __name__ == '__main__':
    unittest.main()
