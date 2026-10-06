import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from run_native import last_trace_event


class NativeRunnerTraceTests(unittest.TestCase):
    def test_missing_and_empty_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            self.assertEqual(last_trace_event(path), {'status': 'no_result'})
            path.touch()
            self.assertEqual(last_trace_event(path), {'status': 'no_result'})

    def test_terminal_record_after_many_events(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            expected = {'kind': 'result', 'status': 'diagnostic_limit', 'game_executed': False}
            with path.open('w') as stream:
                for i in range(10000):
                    stream.write(json.dumps({'kind': 'step', 'value': i}) + '\n')
                stream.write(json.dumps(expected) + '\n')
            self.assertEqual(last_trace_event(path), expected)

    def test_truncated_terminal_record_is_not_success(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.jsonl'
            path.write_text('{"kind":"step"}\n{"kind":"result"')
            with self.assertRaises(json.JSONDecodeError):
                last_trace_event(path)


if __name__ == '__main__':
    unittest.main()
