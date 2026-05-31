#!/usr/bin/env bash
set -euo pipefail

if [[ ! -f "src/main.cpp" || ! -d "include" ]]; then
  echo "Run this launcher from cpp_cplex_firebreak/." >&2
  exit 2
fi

RESULTS_DIR="results/batch/sub20_projected_llbi_integral_alpha001_002_tl300"
PREVIOUS_RESULTS_DIR="results/batch/sub20_alpha001_002_fpp_exact_tl300"
BINARY="${FIREBREAK_BIN:-build_gpp/firebreak_cpp}"

if [[ ! -x "$BINARY" ]]; then
  echo "CPLEX binary not found at $BINARY; running make cplex -j2." >&2
  make cplex -j2
fi

python3 scripts/generate_sub20_projected_llbi_integral_alpha001_002_tl300_manifests.py \
  --results-dir "$RESULTS_DIR" \
  --verify-splits-only

python3 scripts/generate_sub20_projected_llbi_integral_alpha001_002_tl300_manifests.py \
  --results-dir "$RESULTS_DIR"

python3 scripts/generate_sub20_projected_llbi_integral_alpha001_002_tl300_manifests.py \
  --results-dir "$RESULTS_DIR" \
  --verify-only

declare -a pids=()
declare -a workers=()

for idx in $(seq 0 9); do
  worker="$(printf 'worker_%02d' "$idx")"
  workers+=("$worker")
  mkdir -p "$RESULTS_DIR/$worker/logs" "$RESULTS_DIR/$worker/json" "$RESULTS_DIR/$worker/solutions"
  script="scripts/sub20_projected_llbi_integral_tl300_workers/run_${worker}.sh"
  echo "Launching $worker"
  FIREBREAK_BIN="$BINARY" bash "$script" \
    > "$RESULTS_DIR/$worker/logs/stdout.log" 2>&1 &
  pids+=("$!")
done

failed=0
for idx in "${!pids[@]}"; do
  if ! wait "${pids[$idx]}"; then
    echo "${workers[$idx]} failed; see $RESULTS_DIR/${workers[$idx]}/logs/stdout.log" >&2
    failed=1
  fi
done

if [[ "$failed" -ne 0 ]]; then
  echo "At least one worker failed; not merging results." >&2
  exit 1
fi

python3 scripts/merge_sub20_projected_llbi_integral_alpha001_002_tl300.py \
  --results-dir "$RESULTS_DIR" \
  --expected-workers 10 \
  --expected-rows-per-worker 24

python3 scripts/validate_sub20_projected_llbi_integral_alpha001_002_tl300.py \
  --results-dir "$RESULTS_DIR" \
  --previous-results-dir "$PREVIOUS_RESULTS_DIR" \
  --expected-rows 240 \
  --expected-workers 10 \
  --expected-rows-per-worker 24 \
  --seed-base 20260601

python3 scripts/analyze_sub20_projected_llbi_integral_vs_previous.py \
  --previous-results "$PREVIOUS_RESULTS_DIR/combined/batch_results_all.csv" \
  --projected-results "$RESULTS_DIR/combined/batch_results_projected_integral_all.csv" \
  --output-dir "$RESULTS_DIR/combined/comparison_with_previous"

echo "Projected LLBI integral experiment complete."
