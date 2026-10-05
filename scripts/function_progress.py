#!/usr/bin/env python3
"""Report reviewed function status, never infer completion from decompilation."""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STATES = ('complete', 'partial', 'adapter', 'pending')


def load_register(path, root=ROOT):
    with Path(path).open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    keys = set()
    for row in rows:
        entry = row['entry']
        identity = int(entry, 0) if entry.startswith('0x') else entry
        key = (row['profile'], identity)
        if key in keys:
            raise ValueError(f'Duplicate original function: {key}')
        keys.add(key)
        if row['status'] not in STATES:
            raise ValueError('Invalid function status')
        if row['status'] != 'pending' and not (row['source'] and (root / row['source']).is_file()):
            raise ValueError(f'Missing implementation source for {key}')
        if row['status'] == 'pending' and row['integration'] != 'not_integrated':
            raise ValueError('Pending function cannot be marked integrated')
    return rows


def summarize(rows):
    profiles = defaultdict(Counter)
    for row in rows:
        profiles[row['profile']][row['status']] += 1
    return {profile: {status: counts[status] for status in STATES}
            for profile, counts in sorted(profiles.items())}


def observed_trace(path):
    # Repeated calls count once. Executing a prefix never upgrades its status.
    events = [json.loads(line) for line in Path(path).read_text().splitlines() if line]
    return {event['address'] for event in events if event.get('kind') == 'native_c_function'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--register', type=Path, default=ROOT / 'data/function_progress.csv')
    parser.add_argument('--trace', type=Path)
    args = parser.parse_args()
    rows = load_register(args.register)
    print('Original function bodies/models (not full-emulator completion):')
    for profile, counts in summarize(rows).items():
        print(profile + ': ' + ', '.join(f'{status}={counts[status]}' for status in STATES))
    if args.trace:
        observed = observed_trace(args.trace)
        counts = Counter(row['status'] for row in rows
                         if row['profile'] == 'pops_660' and row['entry'].startswith('0x')
                         and int(row['entry'], 0) in observed)
        print(f'Trace: {len(observed)} unique recorded entries; ' + ', '.join(f'{s}={counts[s]}' for s in STATES))
    print('Complete body does not imply implemented dependencies or formal equivalence.')
    print('Registry is scoped audited work, not a complete firmware census; no percentage inferred.')


if __name__ == '__main__':
    main()
