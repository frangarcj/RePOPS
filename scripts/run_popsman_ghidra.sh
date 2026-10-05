#!/bin/sh
# Fresh provider analysis. Leaves existing POPS databases/exports untouched.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
INPUT=${1:-"$ROOT/build/f0-kd-popsman.prx"}
OUTPUT=${2:-"$ROOT/out/decompiled/popsman_new_pass"}
GHIDRA_HOME=${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}
HEADLESS="$GHIDRA_HOME/support/analyzeHeadless"
[ -x "$HEADLESS" ] || { echo "Set GHIDRA_HOME to a Ghidra installation" >&2; exit 1; }
[ ! -e "$OUTPUT" ] || { echo "Refusing existing output: $OUTPUT" >&2; exit 1; }
INPUT=$(CDPATH= cd -- "$(dirname -- "$INPUT")" && pwd)/$(basename -- "$INPUT")
python3 - "$INPUT" <<'PY'
import hashlib, sys
from pathlib import Path
sha = hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest()
if sha != '83ed5373388ba2af57f26eba421eacdee66f56bd3e72432af38365ae8bf4d855':
    raise SystemExit('Wrong POPSMAN input hash; refusing version-specific symbol map')
PY
mkdir -p "$(dirname -- "$OUTPUT")"
OUTPUT=$(CDPATH= cd -- "$(dirname -- "$OUTPUT")" && pwd)/$(basename -- "$OUTPUT")
PROJECT=$(mktemp -d /tmp/repops-popsman.XXXXXX)
"$HEADLESS" "$PROJECT" popsman \
  -import "$INPUT" -analysisTimeoutPerFile 120 -max-cpu 2 \
  -scriptPath "$ROOT/ghidra" \
  -preScript ImportSymbolCsv.java "$ROOT/data/popsman_660_known_symbols.csv" \
  -postScript ExportAllDecomp.java "$OUTPUT"
# Check the result: some headless-script errors do not produce a failing exit.
python3 - "$OUTPUT/index.json" <<'PY'
import json, sys
from pathlib import Path
r = json.loads(Path(sys.argv[1]).read_text())
if r['input_sha256'] != '83ed5373388ba2af57f26eba421eacdee66f56bd3e72432af38365ae8bf4d855':
    raise SystemExit('Unexpected export provenance')
if r['language'] != 'Allegrex:LE:32:default':
    raise SystemExit('Missing Allegrex language/loader')
print('Exported', r['function_count'], 'entries;', r['decompile_completed'], 'decompiled')
PY
printf '%s\n' "$PROJECT" > "$OUTPUT/ghidra_project_path.txt"
