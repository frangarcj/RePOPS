#!/bin/sh
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: $0 <new-native-image-directory>" >&2
    exit 2
fi
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
GHIDRA_HOME=${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}
INPUT=${REPOPS_POPS_ELF:-"$ROOT/build/firmware_660/F0/kd/pops_01g.prx"}
case "$1" in /*) OUTPUT=$1 ;; *) OUTPUT=$(pwd)/$1 ;; esac
if [ -e "$OUTPUT" ]; then
    echo "Refusing existing output: $OUTPUT" >&2
    exit 1
fi
python3 - "$INPUT" <<'PY'
from pathlib import Path
import hashlib,sys
expected='6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0'
if hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest()!=expected:
    raise SystemExit('Wrong POPS reference hash')
PY
PROJECT=$(mktemp -d /tmp/repops-native-image.XXXXXX)
mkdir -p "$(dirname "$OUTPUT")"
"$GHIDRA_HOME/support/analyzeHeadless" "$PROJECT" native_image \
    -import "$INPUT" -noanalysis -scriptPath "$ROOT/ghidra" \
    -postScript ExportNativeImage.java "$OUTPUT" \
    > "$PROJECT/export.log" 2>&1
# Exporters can report script failure while analyzeHeadless itself exits zero.
test -f "$OUTPUT/pops_image.bin"
test -f "$OUTPUT/manifest.json"
printf '%s\n' "$PROJECT" > "$OUTPUT/ghidra_project_path.txt"
printf 'Native C data image: %s\nGhidra log: %s/export.log\n' "$OUTPUT" "$PROJECT"
