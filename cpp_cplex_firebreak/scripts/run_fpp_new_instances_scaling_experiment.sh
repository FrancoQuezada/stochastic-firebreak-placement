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
  echo "  CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 make cplex" >&2
  exit 1
fi

INSTANCES_ROOT="${INSTANCES_ROOT:-new_instances}"
INSTANCE_CONFIG="${INSTANCE_CONFIG:-config/fpp_new_instances_scaling_instances.csv}"
INSTANCE_FILTER="${INSTANCE_FILTER:-new40x40}"
TRAIN_COUNTS="${TRAIN_COUNTS:-100,200}"
ALPHAS="${ALPHAS:-0.01,0.02,0.03}"
TEST_COUNT="${TEST_COUNT:-1000}"
TRAINING_POOL_MIN="${TRAINING_POOL_MIN:-1}"
TRAINING_POOL_MAX="${TRAINING_POOL_MAX:-9000}"
TEST_POOL_MIN="${TEST_POOL_MIN:-9001}"
TEST_POOL_MAX="${TEST_POOL_MAX:-10000}"
NUM_CASES="${NUM_CASES:-5}"
SEED_BASE="${SEED_BASE:-20260529}"
TIME_LIMIT="${TIME_LIMIT:-1800}"
MIP_GAP="${MIP_GAP:-0.001}"
THREADS="${THREADS:-1}"
OUTPUT_DIR="${OUTPUT_DIR:-results/batch/fpp_new_instances_scaling_${INSTANCE_FILTER}}"
PARALLEL="${PARALLEL:-1}"
MAX_PARALLEL_JOBS="${MAX_PARALLEL_JOBS:-12}"
RERUN_EXISTING="${RERUN_EXISTING:-0}"
DRY_RUN="${DRY_RUN:-0}"
STRICT_INSTANCE_VALIDATION="${STRICT_INSTANCE_VALIDATION:-1}"
ALLOW_METADATA_WARNINGS="${ALLOW_METADATA_WARNINGS:-0}"
SKIP_PREFLIGHT="${SKIP_PREFLIGHT:-1}"
METHOD_FILE="${METHOD_FILE:-config/fpp_new_instances_scaling_methods.txt}"
SMOKE_METHODS="${SMOKE_METHODS:-0}"
PROJECTED_LLBI_ROOT_ROUNDS="${PROJECTED_LLBI_ROOT_ROUNDS:-100}"
PROJECTED_LLBI_MAX_CUTS_PER_ROUND="${PROJECTED_LLBI_MAX_CUTS_PER_ROUND:-100}"
PROJECTED_LLBI_VIOLATION_TOLERANCE="${PROJECTED_LLBI_VIOLATION_TOLERANCE:-1e-3}"
PROJECTED_LLBI_CUT_DENSITY_LIMIT="${PROJECTED_LLBI_CUT_DENSITY_LIMIT:-0}"
PROJECTED_POLY_MAX_CUTS="${PROJECTED_POLY_MAX_CUTS:-100000}"
# Phase 8B: canonical weight-map registry options. WEIGHT_REGISTRY unset (the default)
# preserves legacy homogeneous behavior with no weight flags passed to the generator.
WEIGHT_PROFILES="${WEIGHT_PROFILES:-homogeneous}"
WEIGHT_REPLICATES="${WEIGHT_REPLICATES:-0}"
WEIGHT_SEED_BASE="${WEIGHT_SEED_BASE:-12345}"
WEIGHT_REGISTRY="${WEIGHT_REGISTRY:-}"
GENERATE_MISSING_WEIGHT_MAPS="${GENERATE_MISSING_WEIGHT_MAPS:-0}"
PAIRED_REBURN_EVALUATION="${PAIRED_REBURN_EVALUATION:-0}"
RETRY_FAILED="${RETRY_FAILED:-0}"

if [[ "$RERUN_EXISTING" == "1" && "$RETRY_FAILED" == "1" ]]; then
  echo "RERUN_EXISTING=1 and RETRY_FAILED=1 are contradictory; set only one." >&2
  exit 1
fi

if [[ ! -f "$INSTANCE_CONFIG" ]]; then
  echo "Missing instance config: $INSTANCE_CONFIG" >&2
  exit 1
fi
if [[ ! -f "$METHOD_FILE" ]]; then
  echo "Missing method file: $METHOD_FILE" >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR/logs" "$OUTPUT_DIR/manifests"

if [[ "$SMOKE_METHODS" == "1" ]]; then
  METHOD_FILE="$OUTPUT_DIR/manifests/smoke_methods.txt"
  cat > "$METHOD_FILE" <<'EOF'
FPP-SAA
FPP-SAA-CVaR
FPP-Branch-Benders-CVaR-RootCuts
FPP-Branch-Benders-Combinatorial
FPP-Branch-Benders-Combinatorial-EtaDesc
FPP-Branch-Benders-Combinatorial-CVaR
FPP-Branch-Benders-Combinatorial-CVaR-EtaDesc
FPP-Branch-Benders-Combinatorial-MeanCVaR
FPP-Branch-Benders-Combinatorial-MeanCVaR-EtaDesc
EOF
  echo "SMOKE_METHODS=1: using temporary method file $METHOD_FILE"
fi

PREFLIGHT_CSV="$OUTPUT_DIR/preflight_new_instances_smoke.csv"
if [[ "$SKIP_PREFLIGHT" == "1" ]]; then
  echo "SKIP_PREFLIGHT=1: skipping preflight diagnostic."
else
  echo "Running new_instances preflight diagnostic..."
  preflight_args=(
    smoke-new-instances
    --instances-root "$INSTANCES_ROOT"
    --output "$PREFLIGHT_CSV"
  )
  strict_preflight_arg=0
  if [[ "$STRICT_INSTANCE_VALIDATION" == "1" && "$ALLOW_METADATA_WARNINGS" != "1" && -z "$INSTANCE_FILTER" ]]; then
    preflight_args+=(--strict-metadata)
    strict_preflight_arg=1
    echo "Using smoke-new-instances --strict-metadata for unfiltered preflight."
  fi
  set +e
  "$BIN" "${preflight_args[@]}"
  preflight_rc=$?
  set -e
  if [[ "$preflight_rc" -ne 0 && "$strict_preflight_arg" != "1" ]]; then
    echo "Preflight diagnostic failed with exit code $preflight_rc." >&2
    exit "$preflight_rc"
  fi
  if [[ ! -f "$PREFLIGHT_CSV" ]]; then
    echo "Preflight diagnostic did not write expected CSV: $PREFLIGHT_CSV" >&2
    exit 1
  fi
  if [[ "$preflight_rc" -ne 0 ]]; then
    echo "Strict preflight returned exit code $preflight_rc; validating selected rows from $PREFLIGHT_CSV." >&2
  fi
fi

if [[ "$SKIP_PREFLIGHT" != "1" ]]; then
  STRICT_INSTANCE_VALIDATION="$STRICT_INSTANCE_VALIDATION" \
  ALLOW_METADATA_WARNINGS="$ALLOW_METADATA_WARNINGS" \
  python3 - "$INSTANCE_CONFIG" "$INSTANCE_FILTER" "$PREFLIGHT_CSV" <<'PY'
import csv
import os
import sys

config_path, instance_filter, preflight_path = sys.argv[1:4]
strict = os.environ.get("STRICT_INSTANCE_VALIDATION", "1") not in {"0", "false", "False", "no", "NO"}
allow = os.environ.get("ALLOW_METADATA_WARNINGS", "0") in {"1", "true", "True", "yes", "YES"}

def truthy(value):
    return str(value or "").strip().lower() in {"1", "true", "yes", "on"}

with open(config_path, newline="", encoding="utf-8") as inp:
    config_rows = list(csv.DictReader(inp))

enabled = [row for row in config_rows if truthy(row.get("enabled", "true"))]
filters = [value.strip() for value in instance_filter.split(",") if value.strip()]
if filters:
    known = {row["instance_id"] for row in enabled}
    unknown = sorted(set(filters) - known)
    if unknown:
        print(f"Unknown enabled instance IDs in INSTANCE_FILTER: {unknown}", file=sys.stderr)
        sys.exit(1)
    enabled = [row for row in enabled if row["instance_id"] in set(filters)]
if not enabled:
    print("No enabled instances selected.", file=sys.stderr)
    sys.exit(1)

preflight = {}
if preflight_path:
    with open(preflight_path, newline="", encoding="utf-8") as inp:
        preflight = {row.get("folder", ""): row for row in csv.DictReader(inp)}

problems = []
warnings = []
for row in enabled:
    folder = row["folder_name"]
    instance_id = row["instance_id"]
    declared = str(row.get("declared_cells", "")).strip()
    pf = preflight.get(folder)
    if pf is None:
        problems.append((instance_id, folder, "missing preflight row"))
        continue
    status = pf.get("status", "")
    inferred = str(pf.get("inferred_n_cells", "")).strip()
    consistent = str(pf.get("metadata_consistent", "")).strip().lower()
    reasons = []
    if status != "ok":
        reasons.append(f"status={status}")
    if consistent not in {"true", "1", "yes"}:
        reasons.append(f"metadata_consistent={pf.get('metadata_consistent', '')}")
    if declared and inferred and declared != inferred:
        reasons.append(f"declared_cells={declared} inferred_cells={inferred}")
    if reasons:
        item = (instance_id, folder, "; ".join(reasons))
        if truthy(row.get("requires_strict_metadata", "true")):
            problems.append(item)
        else:
            warnings.append(item)

print("Selected new_instances:")
for row in enabled:
    pf = preflight.get(row["folder_name"], {})
    print(
        f"  {row['instance_id']} ({row['folder_name']}): "
        f"declared={row.get('declared_cells', '')} inferred={pf.get('inferred_n_cells', '')} "
        f"status={pf.get('status', 'missing')}"
    )

if warnings:
    print("Non-blocking metadata warnings:")
    for instance_id, folder, reason in warnings:
        print(f"  {instance_id} ({folder}): {reason}")

if problems and strict and not allow:
    print("Blocking metadata inconsistencies detected; no solver workers will be launched.", file=sys.stderr)
    for instance_id, folder, reason in problems:
        print(f"  {instance_id} ({folder}): {reason}", file=sys.stderr)
    print(
        "Use INSTANCE_FILTER to select validated folders, or set ALLOW_METADATA_WARNINGS=1 "
        "only for debugging invalid metadata.",
        file=sys.stderr,
    )
    sys.exit(1)

if problems and (allow or not strict):
    print("WARNING: metadata inconsistencies are being overridden.", file=sys.stderr)
    print("WARNING: resulting outputs are not valid for scientific reporting.", file=sys.stderr)
    for instance_id, folder, reason in problems:
        print(f"  {instance_id} ({folder}): {reason}", file=sys.stderr)
PY
fi

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
  --projected-llbi-root-rounds "$PROJECTED_LLBI_ROOT_ROUNDS"
  --projected-llbi-max-cuts-per-round "$PROJECTED_LLBI_MAX_CUTS_PER_ROUND"
  --projected-llbi-violation-tolerance "$PROJECTED_LLBI_VIOLATION_TOLERANCE"
  --projected-llbi-cut-density-limit "$PROJECTED_LLBI_CUT_DENSITY_LIMIT"
  --projected-poly-max-cuts "$PROJECTED_POLY_MAX_CUTS"
)

if [[ -n "$WEIGHT_REGISTRY" ]]; then
  manifest_args+=(
    --weight-profiles "$WEIGHT_PROFILES"
    --weight-replicates "$WEIGHT_REPLICATES"
    --weight-seed-base "$WEIGHT_SEED_BASE"
    --weight-registry "$WEIGHT_REGISTRY"
    --binary "$BIN"
  )
  if [[ "$GENERATE_MISSING_WEIGHT_MAPS" == "1" ]]; then
    manifest_args+=(--generate-missing-weight-maps)
  fi
  if [[ "$PAIRED_REBURN_EVALUATION" == "1" ]]; then
    manifest_args+=(--paired-reburn-evaluation)
  fi
fi

python3 scripts/generate_fpp_new_instances_scaling_manifests.py "${manifest_args[@]}"
python3 scripts/generate_fpp_new_instances_scaling_manifests.py "${manifest_args[@]}" --verify-only

if [[ "$DRY_RUN" == "1" ]]; then
  echo "DRY_RUN=1: manifests and deterministic split files were verified; no workers launched."
  echo "Output: $OUTPUT_DIR"
  exit 0
fi

mapfile -t MANIFESTS < <(find "$OUTPUT_DIR/manifests" -maxdepth 1 -name 'worker_*_manifest.csv' | sort)
if [[ "${#MANIFESTS[@]}" -eq 0 ]]; then
  echo "No worker manifests found under $OUTPUT_DIR/manifests" >&2
  exit 1
fi

if [[ -n "$WEIGHT_REGISTRY" ]]; then
  weight_summary="$WEIGHT_PROFILES (registry: $WEIGHT_REGISTRY)"
else
  weight_summary="legacy homogeneous"
fi
if [[ "$RERUN_EXISTING" == "1" ]]; then
  resume_summary="rerun-existing (discard all)"
elif [[ "$RETRY_FAILED" == "1" ]]; then
  resume_summary="retry-failed"
else
  resume_summary="default (skip complete, rerun invalid, skip failed)"
fi

echo "FPP new_instances scaling experiment:"
echo "  worker manifests: ${#MANIFESTS[@]}"
echo "  parallel jobs: $([[ "$PARALLEL" == "1" ]] && echo "$MAX_PARALLEL_JOBS" || echo "1")"
echo "  CPLEX threads per solve: $THREADS"
echo "  output: $OUTPUT_DIR"
echo "  weight profiles: $weight_summary"
echo "  resume mode: $resume_summary"

worker_args=()
if [[ "$RERUN_EXISTING" == "1" ]]; then
  worker_args+=(--rerun-existing)
fi
if [[ "$RETRY_FAILED" == "1" ]]; then
  worker_args+=(--retry-failed)
fi

run_worker() {
  local manifest="$1"
  local worker_id
  worker_id="$(basename "$manifest" _manifest.csv)"
  mkdir -p "$OUTPUT_DIR/workers/$worker_id/logs" "$OUTPUT_DIR/workers/$worker_id/json" "$OUTPUT_DIR/workers/$worker_id/solutions"
  python3 scripts/run_fpp_new_instances_scaling_manifest_worker.py \
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

python3 scripts/merge_fpp_new_instances_scaling_experiment.py \
  --results-dir "$OUTPUT_DIR"

python3 scripts/analyze_fpp_new_instances_scaling_experiment.py \
  --results-dir "$OUTPUT_DIR"

echo "FPP new_instances scaling experiment complete: $OUTPUT_DIR"
