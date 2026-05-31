#!/usr/bin/env bash
set -euo pipefail

if [[ ! -f "Makefile" || ! -d "src" || ! -d "include" ]]; then
  echo "Run this script from cpp_cplex_firebreak/." >&2
  exit 1
fi

export LANDSCAPE="${LANDSCAPE:-Sub20}"
export TRAIN_COUNTS="${TRAIN_COUNTS:-2}"
export ALPHAS="${ALPHAS:-0.01}"
export TEST_COUNT="${TEST_COUNT:-3}"
export NUM_CASES="${NUM_CASES:-1}"
export SEED_BASE="${SEED_BASE:-20260529}"
export TIME_LIMIT="${TIME_LIMIT:-60}"
export MIP_GAP="${MIP_GAP:-0.001}"
export THREADS="${THREADS:-1}"
export OUTPUT_DIR="${OUTPUT_DIR:-results/batch/fpp_projected_llbi_scaling_smoke}"
export PARALLEL="${PARALLEL:-1}"
export MAX_PARALLEL_JOBS="${MAX_PARALLEL_JOBS:-2}"
export RERUN_EXISTING="${RERUN_EXISTING:-0}"

bash scripts/run_fpp_projected_llbi_scaling_experiment.sh

python3 scripts/analyze_fpp_projected_llbi_scaling_experiment.py \
  --results-dir "$OUTPUT_DIR"

echo "Smoke experiment completed: $OUTPUT_DIR"
