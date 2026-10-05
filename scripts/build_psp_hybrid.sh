#!/bin/sh
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: $0 <new-output-directory>" >&2
    exit 2
fi
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
GHIDRA_HOME=${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}
PYTHON=${REPOPS_PYTHON:-"$ROOT/.tools/verify-env/bin/python"}
INPUT=${REPOPS_POPS_ELF:-"$ROOT/build/pops_660.prx.dec"}
case "$1" in /*) OUTPUT=$1 ;; *) OUTPUT=$(pwd)/$1 ;; esac
if [ -e "$OUTPUT" ]; then
    echo "Refusing an existing output directory: $OUTPUT" >&2
    exit 1
fi
if [ ! -x "$PYTHON" ] || [ ! -x "$GHIDRA_HOME/support/analyzeHeadless" ]; then
    echo "Set REPOPS_PYTHON and GHIDRA_HOME to the local analysis tools." >&2
    exit 1
fi
command -v psp-gcc >/dev/null
"$PYTHON" - "$INPUT" "$ROOT/scripts" <<'PY'
import hashlib, sys
from pathlib import Path
sys.path.insert(0, sys.argv[2])
from build_psp_hybrid import SOURCE_SHA256
import unicorn
if unicorn.__version__ != '2.1.4':
    raise SystemExit('Requires unicorn==2.1.4 for the bounded execution checks')
if hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest() != SOURCE_SHA256:
    raise SystemExit('Wrong original POPS hash')
PY
mkdir -p "$OUTPUT"
PROJECT=$(mktemp -d /tmp/repops-psp-hybrid.XXXXXX)
printf '%s\n' "$PROJECT" > "$OUTPUT/ghidra_project_path.txt"
"$GHIDRA_HOME/support/analyzeHeadless" "$PROJECT" hybrid_audit \
    -import "$INPUT" -noanalysis -scriptPath "$ROOT/ghidra" \
    -postScript ExportMemoryRange.java "$OUTPUT/audit" 0x44dc 40 \
    > "$OUTPUT/ghidra.log" 2>&1
exec "$PYTHON" "$ROOT/scripts/build_psp_hybrid.py" \
    --elf "$INPUT" --audit "$OUTPUT/audit" --out "$OUTPUT/build"
