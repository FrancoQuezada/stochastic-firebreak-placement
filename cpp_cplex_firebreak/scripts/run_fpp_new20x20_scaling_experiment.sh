#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [[ ! -f "Makefile" || ! -d "src" || ! -d "include" ]]; then
  echo "Could not locate cpp_cplex_firebreak project root from $SCRIPT_DIR." >&2
  exit 1
fi

export INSTANCE_FILTER="${INSTANCE_FILTER:-new20x20,new20x20_reburn}"
export OUTPUT_DIR="${OUTPUT_DIR:-results/batch/fpp_new20x20_scaling}"
export MAX_PARALLEL_JOBS="${MAX_PARALLEL_JOBS:-12}"
export PARALLEL="${PARALLEL:-1}"
export RERUN_EXISTING="${RERUN_EXISTING:-0}"

python3 - "$INSTANCE_FILTER" <<'PY'
import sys

allowed = {"new20x20", "new20x20_reburn"}
selected = [item.strip() for item in sys.argv[1].split(",") if item.strip()]
if not selected:
    print("INSTANCE_FILTER selected no instances.", file=sys.stderr)
    sys.exit(1)
invalid = sorted(set(selected) - allowed)
if invalid:
    print(
        "run_fpp_new20x20_scaling_experiment.sh only supports "
        f"{sorted(allowed)}; invalid INSTANCE_FILTER entries: {invalid}",
        file=sys.stderr,
    )
    sys.exit(1)
PY

scripts/run_fpp_new_instances_scaling_experiment.sh
