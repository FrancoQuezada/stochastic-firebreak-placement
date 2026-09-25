#!/usr/bin/env bash
set -euo pipefail

if [[ ! -f "Makefile" || ! -d "src" || ! -d "include" ]]; then
  echo "Run this script from cpp_cplex_firebreak/." >&2
  exit 1
fi

SMOKE_TEST="${SMOKE_TEST:-0}"
if [[ "$SMOKE_TEST" == "1" ]]; then
  INSTANCE_FILTER="${INSTANCE_FILTER:-new20x20}"
  TRAIN_COUNTS="${TRAIN_COUNTS:-3}"
  ALPHAS="${ALPHAS:-0.02}"
  NUM_CASES="${NUM_CASES:-1}"
  TEST_COUNT="${TEST_COUNT:-4}"
  TRAINING_POOL_MIN="${TRAINING_POOL_MIN:-1}"
  TRAINING_POOL_MAX="${TRAINING_POOL_MAX:-12}"
  TEST_POOL_MIN="${TEST_POOL_MIN:-13}"
  TEST_POOL_MAX="${TEST_POOL_MAX:-16}"
  TIME_LIMIT="${TIME_LIMIT:-30}"
  MIP_GAP="${MIP_GAP:-0.05}"
  METHOD_FILE="${METHOD_FILE:-config/fpp_new20x20_manuscript_smoke_methods.txt}"
  OUTPUT_DIR="${OUTPUT_DIR:-results/batch/fpp_new20x20_manuscript_smoke}"
  MAX_PARALLEL_JOBS="${MAX_PARALLEL_JOBS:-4}"
  DEFAULT_EXPERIMENT_ID="fpp_new20x20_manuscript_smoke"
else
  INSTANCE_FILTER="${INSTANCE_FILTER:-new20x20}"
  TRAIN_COUNTS="${TRAIN_COUNTS:-100,200,400,800}"
  ALPHAS="${ALPHAS:-0.01,0.02,0.03}"
  NUM_CASES="${NUM_CASES:-30}"
  TEST_COUNT="${TEST_COUNT:-1000}"
  TRAINING_POOL_MIN="${TRAINING_POOL_MIN:-1}"
  TRAINING_POOL_MAX="${TRAINING_POOL_MAX:-9000}"
  TEST_POOL_MIN="${TEST_POOL_MIN:-9001}"
  TEST_POOL_MAX="${TEST_POOL_MAX:-10000}"
  TIME_LIMIT="${TIME_LIMIT:-1800}"
  MIP_GAP="${MIP_GAP:-0.001}"
  METHOD_FILE="${METHOD_FILE:-config/fpp_new20x20_manuscript_methods.txt}"
  OUTPUT_DIR="${OUTPUT_DIR:-results/batch/fpp_new20x20_manuscript_campaign}"
  MAX_PARALLEL_JOBS="${MAX_PARALLEL_JOBS:-12}"
  DEFAULT_EXPERIMENT_ID="fpp_new20x20_manuscript_campaign"
fi

INSTANCES_ROOT="${INSTANCES_ROOT:-new_instances}"
INSTANCE_CONFIG="${INSTANCE_CONFIG:-config/fpp_new_instances_scaling_instances.csv}"
SEED_BASE="${SEED_BASE:-20260529}"
THREADS="${THREADS:-1}"
PROJECTED_LLBI_ROOT_ROUNDS="${PROJECTED_LLBI_ROOT_ROUNDS:-100}"
PROJECTED_LLBI_MAX_CUTS_PER_ROUND="${PROJECTED_LLBI_MAX_CUTS_PER_ROUND:-100}"
PROJECTED_LLBI_VIOLATION_TOLERANCE="${PROJECTED_LLBI_VIOLATION_TOLERANCE:-1e-3}"
PROJECTED_LLBI_CUT_DENSITY_LIMIT="${PROJECTED_LLBI_CUT_DENSITY_LIMIT:-0}"
PROJECTED_POLY_MAX_CUTS="${PROJECTED_POLY_MAX_CUTS:-100000}"
FIREBREAK_BIN="${FIREBREAK_BIN:-./build_gpp/firebreak_cpp}"
DRY_RUN="${DRY_RUN:-0}"
CONFIRM_LONG_RUN="${CONFIRM_LONG_RUN:-0}"
PARALLEL="${PARALLEL:-1}"
RERUN_EXISTING="${RERUN_EXISTING:-0}"
RETRY_FAILED="${RETRY_FAILED:-0}"
KEEP_FULL_JSON="${KEEP_FULL_JSON:-0}"
ALLOW_PARTIAL="${ALLOW_PARTIAL:-0}"
METHODS_PER_WORKER="${METHODS_PER_WORKER:-0}"
EXPERIMENT_ID="${EXPERIMENT_ID:-$DEFAULT_EXPERIMENT_ID}"

if [[ "$RERUN_EXISTING" == "1" && "$RETRY_FAILED" == "1" ]]; then
  echo "RERUN_EXISTING=1 and RETRY_FAILED=1 are contradictory." >&2
  exit 1
fi
if [[ -n "${WEIGHT_REGISTRY:-}" ]]; then
  echo "This campaign is homogeneous/unit-weight and does not use WEIGHT_REGISTRY." >&2
  exit 1
fi
if [[ ! -f "$INSTANCE_CONFIG" || ! -f "$METHOD_FILE" ]]; then
  echo "Missing instance or method configuration: $INSTANCE_CONFIG / $METHOD_FILE" >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR/logs" "$OUTPUT_DIR/manifests"

manifest_args=(
  --output-dir "$OUTPUT_DIR"
  --instances-root "$INSTANCES_ROOT"
  --instance-config "$INSTANCE_CONFIG"
  --instance-filter "$INSTANCE_FILTER"
  --train-counts "$TRAIN_COUNTS"
  --alphas "$ALPHAS"
  --test-count "$TEST_COUNT"
  --training-pool-min "$TRAINING_POOL_MIN"
  --training-pool-max "$TRAINING_POOL_MAX"
  --test-pool-min "$TEST_POOL_MIN"
  --test-pool-max "$TEST_POOL_MAX"
  --num-cases "$NUM_CASES"
  --seed-base "$SEED_BASE"
  --time-limit "$TIME_LIMIT"
  --mip-gap "$MIP_GAP"
  --threads "$THREADS"
  --method-file "$METHOD_FILE"
  --cvar-beta 0.9
  --mean-cvar-lambda 0.5
  --projected-llbi-root-rounds "$PROJECTED_LLBI_ROOT_ROUNDS"
  --projected-llbi-max-cuts-per-round "$PROJECTED_LLBI_MAX_CUTS_PER_ROUND"
  --projected-llbi-violation-tolerance "$PROJECTED_LLBI_VIOLATION_TOLERANCE"
  --projected-llbi-cut-density-limit "$PROJECTED_LLBI_CUT_DENSITY_LIMIT"
  --projected-poly-max-cuts "$PROJECTED_POLY_MAX_CUTS"
  --methods-per-worker "$METHODS_PER_WORKER"
  --weight-profiles homogeneous
  --weight-replicates 0
  --paired-reburn-evaluation
)

python3 scripts/generate_fpp_new_instances_scaling_manifests.py "${manifest_args[@]}"
python3 scripts/generate_fpp_new_instances_scaling_manifests.py \
  --output-dir "$OUTPUT_DIR" --verify-only

process_args=(
  --results-dir "$OUTPUT_DIR"
  --experiment-id "$EXPERIMENT_ID"
)
if [[ "$SMOKE_TEST" == "1" ]]; then
  process_args+=(--allow-nonstandard-design)
fi
if [[ "$KEEP_FULL_JSON" == "1" ]]; then
  process_args+=(--keep-full-json)
fi
python3 scripts/process_fpp_new20x20_manuscript_campaign.py \
  "${process_args[@]}" --manifest-only

method_count="$(grep -Ev '^[[:space:]]*(#|$)' "$METHOD_FILE" | wc -l | tr -d ' ')"
objective_count="$(python3 - "$METHOD_FILE" <<'PY'
import sys
methods = [line.split('#', 1)[0].strip() for line in open(sys.argv[1], encoding='utf-8')]
objectives = {'MeanCVaR' if 'MeanCVaR' in m else ('CVaR' if 'CVaR' in m else 'Expected') for m in methods if m}
print(len(objectives))
PY
)"
expected_runs="$(python3 - "$OUTPUT_DIR/manifests/full_task_manifest.csv" <<'PY'
import csv, sys
with open(sys.argv[1], newline='', encoding='utf-8') as handle:
    print(sum(1 for _ in csv.DictReader(handle)))
PY
)"

echo "new20x20 manuscript campaign:"
echo "  optimization instance: $INSTANCE_FILTER"
echo "  paired evaluation instance: new20x20_reburn"
echo "  N values: $TRAIN_COUNTS"
echo "  alpha values: $ALPHAS"
echo "  cases: $NUM_CASES"
echo "  objectives: $objective_count"
echo "  methods: $method_count"
echo "  expected optimization runs: $expected_runs"
echo "  time limit: $TIME_LIMIT seconds"
echo "  threads per solve: $THREADS"
echo "  maximum parallel workers: $MAX_PARALLEL_JOBS"
echo "  results directory: $OUTPUT_DIR"

if [[ "$DRY_RUN" == "1" ]]; then
  echo "DRY_RUN=1: manifests, deterministic splits, and campaign configuration validated; no solver workers launched."
  exit 0
fi

if [[ "$CONFIRM_LONG_RUN" != "1" ]]; then
  echo "Safety guard: set CONFIRM_LONG_RUN=1 to start solver workers." >&2
  exit 2
fi
if [[ ! -x "$FIREBREAK_BIN" ]]; then
  echo "Missing CPLEX binary: $FIREBREAK_BIN (build with make cplex)." >&2
  exit 1
fi

mapfile -t manifests < <(find "$OUTPUT_DIR/manifests" -maxdepth 1 -name 'worker_*_manifest.csv' | sort)
if [[ "${#manifests[@]}" -eq 0 ]]; then
  echo "No worker manifests found under $OUTPUT_DIR/manifests." >&2
  exit 1
fi

worker_args=()
if [[ "$RERUN_EXISTING" == "1" ]]; then
  worker_args+=(--rerun-existing)
fi
if [[ "$RETRY_FAILED" == "1" ]]; then
  worker_args+=(--retry-failed)
fi
if [[ "$KEEP_FULL_JSON" != "1" ]]; then
  worker_args+=(--compact-success-output)
fi

run_worker() {
  local manifest="$1"
  local worker_id
  worker_id="$(basename "$manifest" _manifest.csv)"
  mkdir -p "$OUTPUT_DIR/workers/$worker_id/logs" \
    "$OUTPUT_DIR/workers/$worker_id/json" \
    "$OUTPUT_DIR/workers/$worker_id/solutions"
  python3 scripts/run_fpp_new_instances_scaling_manifest_worker.py \
    --worker-id "$worker_id" \
    --manifest "$manifest" \
    --binary "$FIREBREAK_BIN" \
    "${worker_args[@]}" \
    > "$OUTPUT_DIR/logs/${worker_id}.stdout.log" 2>&1
}

worker_failed=0
if [[ "$PARALLEL" == "1" ]]; then
  active_jobs=0
  for manifest in "${manifests[@]}"; do
    run_worker "$manifest" &
    active_jobs=$((active_jobs + 1))
    if [[ "$active_jobs" -ge "$MAX_PARALLEL_JOBS" ]]; then
      if ! wait -n; then worker_failed=1; fi
      active_jobs=$((active_jobs - 1))
    fi
  done
  while [[ "$active_jobs" -gt 0 ]]; do
    if ! wait -n; then worker_failed=1; fi
    active_jobs=$((active_jobs - 1))
  done
else
  for manifest in "${manifests[@]}"; do
    if ! run_worker "$manifest"; then worker_failed=1; fi
  done
fi

if [[ "$ALLOW_PARTIAL" == "1" ]]; then
  process_args+=(--allow-partial)
fi
postprocess_failed=0
if ! python3 scripts/process_fpp_new20x20_manuscript_campaign.py "${process_args[@]}"; then
  postprocess_failed=1
fi

if [[ "$worker_failed" != "0" || "$postprocess_failed" != "0" ]]; then
  echo "Campaign finished with failed/incomplete workers or validation errors; inspect logs and manuscript_validation_report.txt." >&2
  exit 1
fi

echo "FPP new20x20 manuscript campaign complete: $OUTPUT_DIR"
