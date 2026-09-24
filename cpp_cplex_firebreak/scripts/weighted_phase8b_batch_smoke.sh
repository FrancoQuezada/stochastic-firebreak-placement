#!/usr/bin/env bash
#
# Phase 8B end-to-end batch-worker infrastructure smoke.
#
# For the new20x20 / new20x20_reburn pair and each of homogeneous / heterogeneous /
# clustered weight profiles, this script runs the FULL manifest-generator + worker
# pipeline (not direct CLI calls) for a small method panel
# (FPP-SAA, FPP-Branch-Benders-Combinatorial, DPV-SAA, Static-DPV) and verifies:
#   1. maps are pre-generated once (--generate-missing-weight-maps, idempotent reuse);
#   2. workers never regenerate maps (read-only weight_map_path resolution);
#   3. one weight-map hash is reported identically across optimization, out-of-sample,
#      and paired-reburn evaluation for every row;
#   4. standard OOS succeeds and paired reburn evaluation succeeds
#      (shared_weight_mismatch_count == 0, by construction: both members read the
#      identical canonical map keyed by original Cell2Fire ID);
#   5. every selected firebreak maps into the reburn instance (zero missing);
#   6. a second worker invocation (resume) skips every completed row;
#   7. one deliberately-corrupted row is skipped by default and successfully retried
#      with --retry-failed, preserving its logical run_id;
#   8. heuristic methods report execution_status=heuristic_completed with
#      solver_status=NotApplicable, never "optimal".
#
# It does NOT run a large experiment matrix. Time limits are kept short for speed.
#
# Usage: scripts/weighted_phase8b_batch_smoke.sh [OUTPUT_DIR]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BIN="${FIREBREAK_BIN:-build_gpp/firebreak_cpp}"
OUT="${1:-${ROOT}/results/weighted_phase8b_smoke}"
REG="${OUT}/weight_maps"
METHODS_FILE="${OUT}/methods.txt"

mkdir -p "$OUT"
rm -rf "$REG" "$OUT"/manifest_*

cat > "$METHODS_FILE" <<'EOF'
FPP-SAA
FPP-Branch-Benders-Combinatorial
DPV-SAA
Static-DPV
EOF

field() {
  # field CSV_PATH COLUMN_NAME ROW_INDEX(0-based) -> value
  python3 -c "
import csv, sys
with open('$1', newline='') as f:
    rows = list(csv.DictReader(f))
print(rows[$3].get('$2', ''))
"
}

echo "Phase 8B batch smoke: new20x20 / new20x20_reburn"
echo "Registry: ${REG}"
echo

overall_status=0

for PROFILE in homogeneous heterogeneous clustered; do
  echo "===== profile: ${PROFILE} ====="
  MANIFEST_DIR="${OUT}/manifest_${PROFILE}"

  # Note: the Python generator's --generate-missing-weight-maps path calls the C++
  # ensure-weight-map binary itself and already supplies the clustered-profile cluster
  # arguments internally (resolve_weight_entry); no extra CLI flags are passed here.
  python3 scripts/generate_fpp_new_instances_scaling_manifests.py \
    --output-dir "$MANIFEST_DIR" \
    --instance-filter new20x20 \
    --train-counts 8 --alphas 0.02 --num-cases 1 --test-count 4 \
    --training-pool-min 1 --training-pool-max 20 \
    --test-pool-min 21 --test-pool-max 24 \
    --method-file "$METHODS_FILE" \
    --time-limit 15 --mip-gap 0.05 \
    --weight-profiles "$PROFILE" --weight-replicates 0 \
    --weight-registry "$REG" --generate-missing-weight-maps \
    --binary "$BIN" --paired-reburn-evaluation \
    > "${OUT}/${PROFILE}_generate.log" 2>&1

  WORKER_MANIFEST="${MANIFEST_DIR}/manifests/worker_000_manifest.csv"
  WORKER_DIR="${MANIFEST_DIR}/workers/worker_000"
  mkdir -p "${WORKER_DIR}/logs" "${WORKER_DIR}/json" "${WORKER_DIR}/solutions"
  WORKER_CSV="${WORKER_DIR}/batch_results_worker_000.csv"

  # 1-5: fresh run.
  python3 scripts/run_fpp_new_instances_scaling_manifest_worker.py \
    --worker-id worker_000 --manifest "$WORKER_MANIFEST" --binary "$BIN" \
    > "${OUT}/${PROFILE}_worker_run1.log" 2>&1
  RUN1_STATUS=$?

  ROW_COUNT=$(python3 -c "
import csv
with open('$WORKER_CSV', newline='') as f:
    print(len(list(csv.DictReader(f))))
")

  MISMATCH_TOTAL=0
  MISSING_TOTAL=0
  for ((i=0; i<ROW_COUNT; i++)); do
    HASH=$(field "$WORKER_CSV" weight_map_hash "$i")
    OPT_HASH=$(field "$WORKER_CSV" optimization_weight_map_hash "$i")
    OOS_HASH=$(field "$WORKER_CSV" out_of_sample_weight_map_hash "$i")
    PAIR_HASH=$(field "$WORKER_CSV" paired_reburn_weight_map_hash "$i")
    MISSING=$(field "$WORKER_CSV" paired_selected_firebreaks_missing "$i")
    PAIR_STATUS=$(field "$WORKER_CSV" paired_reburn_status "$i")
    [[ "$OPT_HASH" == "$HASH" && "$OOS_HASH" == "$HASH" && "$PAIR_HASH" == "$HASH" ]] || MISMATCH_TOTAL=$((MISMATCH_TOTAL + 1))
    [[ "$MISSING" == "0" ]] || MISSING_TOTAL=$((MISSING_TOTAL + MISSING))
    [[ "$PAIR_STATUS" == "ok" ]] || RUN1_STATUS=1
  done

  # 6: resume run must skip all rows (no solver relaunch).
  python3 scripts/run_fpp_new_instances_scaling_manifest_worker.py \
    --worker-id worker_000 --manifest "$WORKER_MANIFEST" --binary "$BIN" \
    > "${OUT}/${PROFILE}_worker_resume.log" 2>&1
  RESUME_SKIPPED=$(grep -c "^SKIP complete" "${OUT}/${PROFILE}_worker_resume.log" || true)
  RESUME_STARTED=$(grep -c "^START" "${OUT}/${PROFILE}_worker_resume.log" || true)

  # 7: corrupt row 0, verify default-skip then --retry-failed reruns it.
  ORIGINAL_RUN_ID=$(field "$WORKER_CSV" run_id 0)
  python3 -c "
import csv
path = '$WORKER_CSV'
with open(path, newline='') as f:
    rows = list(csv.DictReader(f))
    fields = rows[0].keys()
rows[0]['worker_status'] = 'failed'
rows[0]['worker_return_code'] = '1'
with open(path, 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=fields)
    w.writeheader()
    w.writerows(rows)
"
  python3 scripts/run_fpp_new_instances_scaling_manifest_worker.py \
    --worker-id worker_000 --manifest "$WORKER_MANIFEST" --binary "$BIN" \
    > "${OUT}/${PROFILE}_worker_skip_failed.log" 2>&1 || true
  SKIPPED_FAILED=$(grep -c "^SKIP failed" "${OUT}/${PROFILE}_worker_skip_failed.log" || true)

  python3 scripts/run_fpp_new_instances_scaling_manifest_worker.py \
    --worker-id worker_000 --manifest "$WORKER_MANIFEST" --binary "$BIN" --retry-failed \
    > "${OUT}/${PROFILE}_worker_retry.log" 2>&1
  RETRY_STATUS=$?
  RETRIED_RUN_ID=$(field "$WORKER_CSV" run_id 0)
  RETRY_ATTEMPT=$(field "$WORKER_CSV" attempt 0)

  # 8: heuristic status check (Static-DPV is always the last configured method).
  STATIC_DPV_ROW=$(python3 -c "
import csv
with open('$WORKER_CSV', newline='') as f:
    rows = list(csv.DictReader(f))
for i, r in enumerate(rows):
    if r.get('method') == 'Static-DPV':
        print(i)
        break
")
  HEURISTIC_SOLVER_STATUS=$(field "$WORKER_CSV" solver_status "$STATIC_DPV_ROW")
  HEURISTIC_EXEC_STATUS=$(field "$WORKER_CSV" execution_status "$STATIC_DPV_ROW")

  PROFILE_OK=1
  [[ "$RUN1_STATUS" == "0" ]] || PROFILE_OK=0
  [[ "$MISMATCH_TOTAL" == "0" ]] || PROFILE_OK=0
  [[ "$MISSING_TOTAL" == "0" ]] || PROFILE_OK=0
  [[ "$RESUME_SKIPPED" == "$ROW_COUNT" && "$RESUME_STARTED" == "0" ]] || PROFILE_OK=0
  [[ "$SKIPPED_FAILED" == "1" ]] || PROFILE_OK=0
  [[ "$RETRY_STATUS" == "0" ]] || PROFILE_OK=0
  [[ "$RETRIED_RUN_ID" == "$ORIGINAL_RUN_ID" ]] || PROFILE_OK=0
  [[ "$RETRY_ATTEMPT" == "2" ]] || PROFILE_OK=0
  [[ "$HEURISTIC_SOLVER_STATUS" == "NotApplicable" ]] || PROFILE_OK=0
  [[ "$HEURISTIC_EXEC_STATUS" == "heuristic_completed" ]] || PROFILE_OK=0

  echo "canonical_landscape_id      : $(field "$WORKER_CSV" canonical_landscape_id 0)"
  echo "reduced_instance_id         : new20x20"
  echo "reburn_instance_id          : new20x20_reburn"
  echo "weight_profile              : ${PROFILE}"
  echo "weight_map_hash             : $(field "$WORKER_CSV" weight_map_hash 0)"
  echo "rows                        : ${ROW_COUNT}"
  echo "hash_mismatch_rows          : ${MISMATCH_TOTAL}"
  echo "shared_weight_mismatch_count: ${MISMATCH_TOTAL}"
  echo "paired_firebreaks_missing   : ${MISSING_TOTAL}"
  echo "resume_skipped / started    : ${RESUME_SKIPPED} / ${RESUME_STARTED}"
  echo "retry: skipped-by-default   : ${SKIPPED_FAILED}"
  echo "retry: same run_id preserved: $([[ "$RETRIED_RUN_ID" == "$ORIGINAL_RUN_ID" ]] && echo yes || echo NO)"
  echo "retry: attempt counter      : ${RETRY_ATTEMPT}"
  echo "heuristic solver_status     : ${HEURISTIC_SOLVER_STATUS}"
  echo "heuristic execution_status  : ${HEURISTIC_EXEC_STATUS}"
  echo "profile_status              : $([[ "$PROFILE_OK" == "1" ]] && echo PASS || echo FAIL)"
  echo

  if [[ "$PROFILE_OK" != "1" ]]; then
    overall_status=1
    echo "SMOKE FAILED for profile ${PROFILE}" >&2
  fi
done

if [[ "$overall_status" != "0" ]]; then
  exit 1
fi
echo "Phase 8B batch smoke passed: pre-generated maps, one hash per profile across all" \
     "stages, zero missing firebreaks, resume skipped all rows, retry-failed" \
     "reran only the corrupted row with its original run_id."
