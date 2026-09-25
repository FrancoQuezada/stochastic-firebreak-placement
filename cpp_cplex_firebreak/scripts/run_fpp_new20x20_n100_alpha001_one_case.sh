#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Small end-to-end check of the complete 24-method manuscript panel.  Each
# method gets its own one-task worker so MAX_PARALLEL_JOBS is the actual solve
# concurrency.  Scientific OOS/reburn evaluation is kept intact.
OUTPUT_DIR="${OUTPUT_DIR:-results/batch/fpp_new20x20_n100_alpha001_one_case}"
TIME_LIMIT="${TIME_LIMIT:-60}"
MAX_PARALLEL_JOBS="${MAX_PARALLEL_JOBS:-12}"
MIP_GAP="${MIP_GAP:-0.001}"
THREADS="${THREADS:-1}"
FIREBREAK_BIN="${FIREBREAK_BIN:-./build_gpp/firebreak_cpp}"
REQUESTED_DRY_RUN="${DRY_RUN:-0}"
CONFIRM_LONG_RUN="${CONFIRM_LONG_RUN:-0}"
RERUN_EXISTING="${RERUN_EXISTING:-0}"
RETRY_FAILED="${RETRY_FAILED:-0}"
KEEP_FULL_JSON="${KEEP_FULL_JSON:-0}"

if [[ "$REQUESTED_DRY_RUN" != "0" && "$REQUESTED_DRY_RUN" != "1" ]]; then
  echo "DRY_RUN must be 0 or 1." >&2
  exit 1
fi
if [[ "$MAX_PARALLEL_JOBS" -lt 1 ]]; then
  echo "MAX_PARALLEL_JOBS must be at least 1." >&2
  exit 1
fi

run_campaign() {
  local dry_run="$1"
  env \
    SMOKE_TEST=1 \
    INSTANCE_FILTER=new20x20 \
    TRAIN_COUNTS=100 \
    ALPHAS=0.01 \
    NUM_CASES=1 \
    TEST_COUNT=1000 \
    TRAINING_POOL_MIN=1 \
    TRAINING_POOL_MAX=9000 \
    TEST_POOL_MIN=9001 \
    TEST_POOL_MAX=10000 \
    TIME_LIMIT="$TIME_LIMIT" \
    MIP_GAP="$MIP_GAP" \
    THREADS="$THREADS" \
    METHOD_FILE=config/fpp_new20x20_manuscript_methods.txt \
    METHODS_PER_WORKER=1 \
    MAX_PARALLEL_JOBS="$MAX_PARALLEL_JOBS" \
    OUTPUT_DIR="$OUTPUT_DIR" \
    EXPERIMENT_ID=fpp_new20x20_n100_alpha001_one_case \
    FIREBREAK_BIN="$FIREBREAK_BIN" \
    PARALLEL=1 \
    ALLOW_PARTIAL=0 \
    DRY_RUN="$dry_run" \
    CONFIRM_LONG_RUN="$CONFIRM_LONG_RUN" \
    RERUN_EXISTING="$RERUN_EXISTING" \
    RETRY_FAILED="$RETRY_FAILED" \
    KEEP_FULL_JSON="$KEEP_FULL_JSON" \
    scripts/run_fpp_new20x20_manuscript_campaign.sh
}

verify_manifest() {
  python3 - "$OUTPUT_DIR" <<'PY'
import csv
import sys
from collections import Counter
from pathlib import Path

out = Path(sys.argv[1])
manifest_path = out / "manifests" / "full_task_manifest.csv"
with manifest_path.open(newline="", encoding="utf-8") as handle:
    rows = list(csv.DictReader(handle))

errors = []
check = lambda condition, message: errors.append(message) if not condition else None
check(len(rows) == 24, f"expected 24 manifest rows, found {len(rows)}")
check(len({row.get('run_id') for row in rows}) == 24, "run_id values are not unique")
check(len({row.get('method') for row in rows}) == 24, "expected 24 unique methods")
check({row.get('worker_id') for row in rows} and len({row.get('worker_id') for row in rows}) == 24,
      "expected one worker per method")
worker_counts = Counter(row.get("worker_id") for row in rows)
check(all(count == 1 for count in worker_counts.values()), "a worker contains more than one task")
check({row.get('case_id') for row in rows} == {'case00'}, "case must be exactly case00")
check({row.get('train_count') for row in rows} == {'100'}, "N must be exactly 100")
check(all(abs(float(row.get('alpha', 'nan')) - 0.01) < 1e-12 for row in rows),
      "alpha must be exactly 0.01")
check({row.get('instance_id') for row in rows} == {'new20x20'},
      "only new20x20 may be optimized")
check(all(row.get('paired_reburn_instance_id') == 'new20x20_reburn' for row in rows),
      "paired reburn instance is missing")
check(all(row.get('paired_evaluation_enabled') == 'true' for row in rows),
      "paired reburn evaluation must be enabled")
check(all(row.get('test_count') == '1000' for row in rows), "test_count must be 1000")
check(all(row.get('test_pool_min') == '9001' and row.get('test_pool_max') == '10000'
          for row in rows), "fixed OOS pool must be 9001..10000")
check(len({row.get('train_split_path') for row in rows}) == 1,
      "all methods must reuse one training split")
check(len({row.get('test_split_path') for row in rows}) == 1,
      "all methods must reuse one OOS split")

worker_files = sorted((out / "manifests").glob("worker_*_manifest.csv"))
check(len(worker_files) == 24, f"expected 24 worker manifests, found {len(worker_files)}")
if rows:
    test_ids = [int(value.strip()) for value in Path(rows[0]["test_split_path"])
                .read_text(encoding="utf-8").splitlines() if value.strip()]
    check(test_ids == list(range(9001, 10001)), "OOS IDs are not exactly 9001..10000")

if errors:
    raise SystemExit("Manifest verification FAILED:\n- " + "\n- ".join(errors))
print("Manifest verification PASS: 24 methods, 24 one-task workers, N=100, alpha=0.01, case00.")
PY
}

verify_results() {
  python3 - "$OUTPUT_DIR" "$KEEP_FULL_JSON" <<'PY'
import csv
import sys
from pathlib import Path

out = Path(sys.argv[1])
keep_full_json = sys.argv[2] == "1"

def read_csv(name):
    with (out / name).open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))

results = read_csv("manuscript_results.csv")
solutions = read_csv("solutions.csv")
summary = read_csv("manuscript_summary.csv")
errors = []
check = lambda condition, message: errors.append(message) if not condition else None

check(len(results) == 24, f"expected 24 result rows, found {len(results)}")
check(len({row.get('run_id') for row in results}) == 24, "result run_id values are not unique")
check(len({row.get('method') for row in results}) == 24, "expected 24 result methods")
for row in results:
    label = row.get("method", "<unknown>")
    check(row.get("execution_status") == "completed", f"{label}: execution_status is not completed")
    check(row.get("has_incumbent") == "true", f"{label}: no incumbent")
    check(row.get("objective_validation_passed") == "true",
          f"{label}: objective validation did not pass")
    for field in ("train_eval_objective_value", "oos_eval_objective_value",
                  "reburn_eval_objective_value"):
        check(row.get(field, "") != "", f"{label}: missing {field}")
    try:
        selected = int(row.get("selected_firebreak_count", ""))
        check(0 <= selected <= 4, f"{label}: selected count {selected} is outside budget 0..4")
    except ValueError:
        errors.append(f"{label}: invalid selected_firebreak_count")

result_ids = {row.get("run_id") for row in results}
solution_ids = {row.get("run_id") for row in solutions}
check(len(solutions) == 24, f"expected 24 solution rows, found {len(solutions)}")
check(solution_ids == result_ids, "solution run IDs do not match result run IDs")
check(len(summary) == 24, f"expected 24 summary rows, found {len(summary)}")
for row in summary:
    label = row.get("method", "<unknown>")
    check(row.get("expected_runs") == "1", f"{label}: expected_runs != 1")
    check(row.get("observed_runs") == "1", f"{label}: observed_runs != 1")
    check(row.get("runs_with_incumbent") == "1", f"{label}: runs_with_incumbent != 1")
    check(row.get("failed_no_incumbent_runs") == "0", f"{label}: failed count != 0")

report = (out / "manuscript_validation_report.txt").read_text(encoding="utf-8")
check("validation_status=PASS" in report, "validation report is not PASS")
check("campaign_complete=true" in report, "campaign is not marked complete")

if not keep_full_json:
    manifest = read_csv("manifests/full_task_manifest.csv")
    leftover_solver_json = [Path(row["output_json"]) for row in manifest
                            if Path(row["output_json"]).exists()]
    leftover_paired_json = [Path(row["output_json"]).with_name(
        Path(row["output_json"]).stem + "_paired_reburn_eval.json") for row in manifest]
    leftover_paired_json = [path for path in leftover_paired_json if path.exists()]
    row_csv = list(out.glob("workers/*/logs/row_csv/*.csv"))
    check(not leftover_solver_json, f"{len(leftover_solver_json)} compactable solver JSON files remain")
    check(not leftover_paired_json, f"{len(leftover_paired_json)} paired JSON files remain")
    check(not row_csv, f"{len(row_csv)} redundant solver row CSV files remain")

if errors:
    raise SystemExit("Result verification FAILED:\n- " + "\n- ".join(errors))

print("\nMethod status:")
print(f"{'method':72} {'status':16} {'reason':14} {'sec':>9} {'gap':>10} {'y':>3}")
for row in results:
    print(f"{row['method'][:72]:72} {row['solver_status'][:16]:16} "
          f"{row['termination_reason'][:14]:14} {row['runtime_sec']:>9} "
          f"{row['mip_gap']:>10} {row['selected_firebreak_count']:>3}")
print("\nResult verification PASS: 24/24 completed with incumbent, valid objective, OOS and reburn evaluation.")
PY
}

# Generate first, then verify the exact small-test design before any CPLEX job is
# allowed to start.
run_campaign 1
verify_manifest

if [[ "$REQUESTED_DRY_RUN" == "1" ]]; then
  echo "DRY_RUN=1: verification finished; no solver was launched."
  exit 0
fi

run_campaign 0
verify_results
echo "PASS: small parallel N=100, alpha=0.01 test completed successfully in $OUTPUT_DIR"
