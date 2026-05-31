#!/usr/bin/env bash
set -euo pipefail

if [[ ! -f "Makefile" || ! -d "src" || ! -d "include" ]]; then
  echo "Run this script from cpp_cplex_firebreak/." >&2
  exit 1
fi

BIN="${FIREBREAK_BIN:-./build_gpp/firebreak_cpp}"
if [[ ! -x "$BIN" ]]; then
  echo "Missing CPLEX binary: $BIN" >&2
  echo "Build it with:" >&2
  echo "  CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 ./build_gpp.sh --with-cplex" >&2
  exit 1
fi

LANDSCAPE="${LANDSCAPE:-Sub20}"
TRAIN_COUNTS="${TRAIN_COUNTS:-100,200,400,800}"
ALPHAS="${ALPHAS:-0.01,0.02,0.03}"
TEST_COUNT="${TEST_COUNT:-200}"
NUM_CASES="${NUM_CASES:-5}"
SEED_BASE="${SEED_BASE:-20260529}"
TIME_LIMIT="${TIME_LIMIT:-1800}"
MIP_GAP="${MIP_GAP:-0.001}"
THREADS="${THREADS:-1}"
OUTPUT_DIR="${OUTPUT_DIR:-results/batch/fpp_projected_llbi_scaling}"
PARALLEL="${PARALLEL:-1}"
MAX_PARALLEL_JOBS="${MAX_PARALLEL_JOBS:-12}"
RERUN_EXISTING="${RERUN_EXISTING:-0}"
PROJECTED_LLBI_ROOT_ROUNDS="${PROJECTED_LLBI_ROOT_ROUNDS:-10}"
PROJECTED_LLBI_MAX_CUTS_PER_ROUND="${PROJECTED_LLBI_MAX_CUTS_PER_ROUND:-100}"
PROJECTED_LLBI_VIOLATION_TOLERANCE="${PROJECTED_LLBI_VIOLATION_TOLERANCE:-1e-6}"
PROJECTED_LLBI_CUT_DENSITY_LIMIT="${PROJECTED_LLBI_CUT_DENSITY_LIMIT:-0}"
PROJECTED_POLY_MAX_CUTS="${PROJECTED_POLY_MAX_CUTS:-100000}"

FOREST_PATH="${FOREST_PATH:-../sample_test/data/CanadianFBP/${LANDSCAPE}}"
RESULTS_PATH="${RESULTS_PATH:-../sample_test/${LANDSCAPE}}"
METHOD_FILE="${METHOD_FILE:-config/fpp_projected_llbi_scaling_methods.txt}"

if [[ ! -f "$METHOD_FILE" ]]; then
  echo "Missing method file: $METHOD_FILE" >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR/logs"

manifest_args=(
  --output-dir "$OUTPUT_DIR"
  --landscape "$LANDSCAPE"
  --forest-path "$FOREST_PATH"
  --results-path "$RESULTS_PATH"
  --train-counts "$TRAIN_COUNTS"
  --alphas "$ALPHAS"
  --test-count "$TEST_COUNT"
  --num-cases "$NUM_CASES"
  --seed-base "$SEED_BASE"
  --time-limit "$TIME_LIMIT"
  --mip-gap "$MIP_GAP"
  --threads "$THREADS"
  --method-file "$METHOD_FILE"
  --projected-llbi-root-rounds "$PROJECTED_LLBI_ROOT_ROUNDS"
  --projected-llbi-max-cuts-per-round "$PROJECTED_LLBI_MAX_CUTS_PER_ROUND"
  --projected-llbi-violation-tolerance "$PROJECTED_LLBI_VIOLATION_TOLERANCE"
  --projected-llbi-cut-density-limit "$PROJECTED_LLBI_CUT_DENSITY_LIMIT"
  --projected-poly-max-cuts "$PROJECTED_POLY_MAX_CUTS"
)

python3 scripts/generate_fpp_projected_llbi_scaling_manifests.py "${manifest_args[@]}"
python3 scripts/generate_fpp_projected_llbi_scaling_manifests.py "${manifest_args[@]}" --verify-only

mapfile -t MANIFESTS < <(find "$OUTPUT_DIR/manifests" -maxdepth 1 -name 'worker_*_manifest.csv' | sort)
if [[ "${#MANIFESTS[@]}" -eq 0 ]]; then
  echo "No worker manifests found under $OUTPUT_DIR/manifests" >&2
  exit 1
fi

echo "Projected LLBI scaling experiment:"
echo "  instance-block workers: ${#MANIFESTS[@]}"
echo "  parallel jobs: $([[ "$PARALLEL" == "1" ]] && echo "$MAX_PARALLEL_JOBS" || echo "1")"
echo "  CPLEX threads per solve: $THREADS"
echo "  output: $OUTPUT_DIR"

worker_args=()
if [[ "$RERUN_EXISTING" == "1" ]]; then
  worker_args+=(--rerun-existing)
fi

run_worker() {
  local manifest="$1"
  local worker_id
  worker_id="$(basename "$manifest" _manifest.csv)"
  mkdir -p "$OUTPUT_DIR/workers/$worker_id/logs" "$OUTPUT_DIR/workers/$worker_id/json" "$OUTPUT_DIR/workers/$worker_id/solutions"
  python3 scripts/run_fpp_projected_llbi_scaling_manifest_worker.py \
    --worker-id "$worker_id" \
    --manifest "$manifest" \
    --binary "$BIN" \
    "${worker_args[@]}" \
    > "$OUTPUT_DIR/logs/${worker_id}.stdout.log" 2>&1
}

if [[ "$PARALLEL" == "1" ]]; then
  active_jobs=0
  failed=0
  for manifest in "${MANIFESTS[@]}"; do
    worker_id="$(basename "$manifest" _manifest.csv)"
    echo "Launching $worker_id"
    run_worker "$manifest" &
    active_jobs=$((active_jobs + 1))
    if [[ "$active_jobs" -ge "$MAX_PARALLEL_JOBS" ]]; then
      if ! wait -n; then
        failed=1
      fi
      active_jobs=$((active_jobs - 1))
    fi
  done
  while [[ "$active_jobs" -gt 0 ]]; do
    if ! wait -n; then
      failed=1
    fi
    active_jobs=$((active_jobs - 1))
  done
  if [[ "$failed" -ne 0 ]]; then
    echo "At least one worker failed. Check $OUTPUT_DIR/logs/worker_*.stdout.log." >&2
    exit 1
  fi
else
  for manifest in "${MANIFESTS[@]}"; do
    worker_id="$(basename "$manifest" _manifest.csv)"
    echo "Running $worker_id"
    run_worker "$manifest"
  done
fi

python3 scripts/merge_fpp_projected_llbi_scaling_experiment.py \
  --results-dir "$OUTPUT_DIR"

python3 scripts/analyze_fpp_projected_llbi_scaling_experiment.py \
  --results-dir "$OUTPUT_DIR"

echo "Projected LLBI scaling experiment complete: $OUTPUT_DIR"
