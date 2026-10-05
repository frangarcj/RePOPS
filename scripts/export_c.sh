#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <new-output-directory>" >&2
    exit 2
fi
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
GHIDRA_HOME=${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}
HEADLESS="$GHIDRA_HOME/support/analyzeHeadless"

if [ -n "${REPOPS_GHIDRA_PROJECT_DIR:-}" ]; then
    PROJECT_DIR=$REPOPS_GHIDRA_PROJECT_DIR
elif [ -f "$ROOT/out/ghidra_project_path.txt" ]; then
    PROJECT_DIR=$(cat "$ROOT/out/ghidra_project_path.txt")
else
    echo "No saved Ghidra project path. Run scripts/run_ghidra.sh first." >&2
    exit 1
fi
PROGRAM=${REPOPS_GHIDRA_PROGRAM:-pops_660.prx.dec}
if [ -z "${REPOPS_GHIDRA_PROGRAM:-}" ] && [ -f "$ROOT/out/ghidra_program_name.txt" ]; then
    PROGRAM=$(cat "$ROOT/out/ghidra_program_name.txt")
fi
case "$1" in
    /*) OUTPUT=$1 ;;
    *) OUTPUT=$(pwd)/$1 ;;
esac
if [ -d "$OUTPUT" ] && [ -n "$(ls -A "$OUTPUT")" ]; then
    echo "Refusing to overwrite nonempty export directory: $OUTPUT" >&2
    exit 1
fi
if [ ! -x "$HEADLESS" ] || [ ! -f "$PROJECT_DIR/repops.gpr" ]; then
    echo "Ghidra executable or saved project is missing." >&2
    exit 1
fi

exec "$HEADLESS" "$PROJECT_DIR" repops \
    -process "$PROGRAM" -readOnly -noanalysis \
    -scriptPath "$ROOT/ghidra" \
    -postScript ApplyBootstrapContracts.java \
    -postScript ExportAllDecomp.java "$OUTPUT"
