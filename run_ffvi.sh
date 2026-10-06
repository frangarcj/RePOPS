#!/bin/sh
# Build and run reconstructed POPS C against the local FFVI PBP on macOS.
# Every execution preserves prior traces. No firmware or game is modified.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -gt 1 ]; then
    echo "usage: $0 [path/to/Final Fantasy VI.PBP]" >&2
    exit 64
fi
PYTHON=${REPOPS_PYTHON:-python3}
PBP=${1:-${REPOPS_GAME_PBP:-"$ROOT/input/games/final_fantasy_vi/EBOOT.PBP"}}
case "$PBP" in /*) ;; *) PBP=$(pwd)/$PBP ;; esac
cd "$ROOT"
if [ ! -f "$PBP" ] && [ "$#" -eq 0 ] && [ -z "${REPOPS_GAME_PBP:-}" ]; then
    PBP="$HOME/Downloads/Final Fantasy VI.PBP"
fi
if [ ! -f "$PBP" ]; then
    echo "FFVI PBP not found: $PBP" >&2
    echo "Pass its path as the first argument or set REPOPS_GAME_PBP." >&2
    exit 66
fi
if ! command -v pkg-config >/dev/null || ! pkg-config --exists libpng; then
    echo "libpng and pkg-config are required by the native image adapter." >&2
    exit 69
fi
IMAGE=${REPOPS_NATIVE_IMAGE:-"$ROOT/build/native_image"}
if [ ! -d "$IMAGE" ]; then
    echo "Preparing hash-checked POPS data with Ghidra (first run only)..."
    sh "$ROOT/scripts/prepare_native.sh" "$IMAGE"
fi
if [ ! -f "$IMAGE/manifest.json" ] || [ ! -f "$IMAGE/pops_image.bin" ]; then
    echo "Incomplete image directory: $IMAGE. Select a fresh directory with REPOPS_NATIVE_IMAGE." >&2
    exit 65
fi
RUN=$(mktemp -d "$ROOT/out/ffvi_run.XXXXXX")
echo "Building and running native C (the game is not fully implemented yet)."
echo "PBP: $PBP"
set --
if [ "${REPOPS_DIAGNOSTIC_SKIP_UI:-0}" = 1 ]; then
    echo 'DIAGNOSTIC: PSP startup UI is bypassed, not reconstructed.'
    set -- --diagnostic-skip-ui
fi
"$PYTHON" "$ROOT/scripts/run_native.py" --image "$IMAGE" --pbp "$PBP" --out "$RUN/result" --timeout "${REPOPS_TIMEOUT:-30}" "$@"
echo
echo "Execution milestones:"
"$PYTHON" - "$RUN/result/trace.jsonl" <<'PY'
import json,sys
from pathlib import Path
for line in Path(sys.argv[1]).read_text().splitlines():
    event=json.loads(line)
    if event['kind'] in ('milestone','blocker','diagnostic_bypass'):
        print(f"  {event['kind']}: {event['name']} @ 0x{event['address']:08X} value={event['value']}")
    elif event['kind']=='result':
        print('  game_executed:',event['game_executed'])
PY
echo "Trace: $RUN/result/trace.jsonl"
echo "Report: $RUN/result/run.json"
echo "Exit 0 means the diagnostic run was captured, not that FFVI booted."
