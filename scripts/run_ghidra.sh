#!/bin/sh
set -eu

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <decrypted-pops-prx>" >&2
    exit 2
fi

INPUT=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)/$(basename -- "$1")
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
GHIDRA_HOME=${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}
HEADLESS="$GHIDRA_HOME/support/analyzeHeadless"

if [ ! -x "$HEADLESS" ]; then
    echo "analyzeHeadless not found at $HEADLESS; set GHIDRA_HOME" >&2
    exit 1
fi

mkdir -p "$ROOT/out"

# Ghidra rejects project-location path components that begin with a dot. The
# Codexify workspace lives under ~/.codexify, so use a disposable project in
# /tmp while keeping every exported artifact inside the repository.
PROJECT_DIR=${REPOPS_GHIDRA_PROJECT_DIR:-$(mktemp -d /tmp/repops-ghidra.XXXXXX)}
mkdir -p "$PROJECT_DIR"
if [ -e "$PROJECT_DIR/repops.gpr" ]; then
    echo "Refusing to overwrite an existing Ghidra project: $PROJECT_DIR" >&2
    echo "Use scripts/export_c.sh for read-only export." >&2
    exit 1
fi

"$HEADLESS" "$PROJECT_DIR" repops \
    -import "$INPUT" \
    -analysisTimeoutPerFile 180 \
    -scriptPath "$ROOT/ghidra" \
    -preScript LimitPopsCode.java \
    -preScript ImportSymbolCsv.java "$ROOT/data/pops_660_known_symbols.csv" \
    -postScript ApplyBootstrapContracts.java \
    -postScript ExportFunctions.java "$ROOT/out/ghidra_functions.csv" \
    -postScript ExportDecomp.java "$ROOT/out/ghidra_seed_decomp.c" \
        0x00016000 0x00001A00 0x00007F00 0x000083E8 0x0000D1B0

printf '%s\n' "$PROJECT_DIR" > "$ROOT/out/ghidra_project_path.txt"
printf '%s\n' "$(basename -- "$INPUT")" > "$ROOT/out/ghidra_program_name.txt"
