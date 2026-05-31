#!/usr/bin/env bash
set -euo pipefail

if [[ ! -f "src/main.cpp" || ! -d "include" ]]; then
  echo "Run this script from cpp_cplex_firebreak/." >&2
  exit 2
fi

RESULTS_DIR="results/diagnostics/sub20_alpha001_002_projected_llbi_lp_relaxation"
EXPERIMENT_ID="sub20_alpha001_002_projected_llbi_lp_relaxation"
LANDSCAPE="Sub20"
FOREST_PATH="../sample_test/data/CanadianFBP/Sub20"
RESULTS_PATH="../sample_test/Sub20"
SEED_BASE=20260601
THREADS=1
TRAIN_COUNT=100
PATH_LLBI_MAX_PATHS_PER_NODE=8
PROJECTED_LLBI_ROOT_ROUNDS=10
PROJECTED_LLBI_MAX_CUTS_PER_ROUND=100
PROJECTED_LLBI_VIOLATION_TOLERANCE=1e-6
PROJECTED_LLBI_CUT_DENSITY_LIMIT=0

ALPHAS=("0.01" "0.02")
VARIANTS=(
  "fpp_saa_lp_base"
  "master_lp_none"
  "master_lp_llbi"
  "master_lp_coverage_llbi"
  "master_lp_path_llbi"
  "master_lp_projected_coverage_poly"
  "master_lp_projected_path_poly"
  "master_lp_projected_coverage_exp"
  "master_lp_projected_path_exp"
)

RESUME=0
for arg in "$@"; do
  case "${arg}" in
    --resume)
      RESUME=1
      ;;
    --help|-h)
      echo "Usage: bash scripts/run_sub20_projected_llbi_lp_diagnostic.sh [--resume]"
      exit 0
      ;;
    *)
      echo "Unknown argument: ${arg}" >&2
      exit 2
      ;;
  esac
done

if [[ ! -x "build_gpp/firebreak_cpp" ]]; then
  if command -v make >/dev/null 2>&1; then
    make cplex -j "${BUILD_JOBS:-2}"
  else
    echo "Missing build_gpp/firebreak_cpp and make is unavailable." >&2
    exit 2
  fi
fi

mkdir -p "${RESULTS_DIR}/json" "${RESULTS_DIR}/logs" "${RESULTS_DIR}/splits"

SPLIT_SOURCE_ARGS=()
if [[ -d "results/diagnostics/sub20_alpha001_002_fpp_master_lp_relaxation/splits" ]]; then
  SPLIT_SOURCE_ARGS=(
    --previous-split-dir
    "results/diagnostics/sub20_alpha001_002_fpp_master_lp_relaxation/splits"
  )
elif [[ -d "results/batch/sub20_alpha001_002_fpp_exact_tl300/splits" ]]; then
  SPLIT_SOURCE_ARGS=(
    --previous-split-dir
    "results/batch/sub20_alpha001_002_fpp_exact_tl300/splits"
  )
elif [[ -d "results/batch/sub20_100train_900test_5cases_alpha002_003_tl300/splits" ]]; then
  SPLIT_SOURCE_ARGS=(
    --previous-split-dir
    "results/batch/sub20_100train_900test_5cases_alpha002_003_tl300/splits"
  )
fi

python3 scripts/prepare_sub20_alpha001_002_shared_splits.py \
  --results-dir "${RESULTS_DIR}" \
  --results-path "${RESULTS_PATH}" \
  "${SPLIT_SOURCE_ARGS[@]}"

OUTPUT_CSV="${RESULTS_DIR}/lp_relaxation_results.csv"
if [[ "${RESUME}" -eq 0 ]]; then
  rm -f "${OUTPUT_CSV}"
  rm -f "${RESULTS_DIR}/json"/*.json 2>/dev/null || true
fi

train_ids_for_case() {
  local case_id="$1"
  python3 - "$RESULTS_DIR" "$case_id" <<'PY'
import json
import re
import sys
from pathlib import Path

results_dir = Path(sys.argv[1])
case_id = sys.argv[2]
config = json.loads((results_dir / "splits" / "shared_split_config.json").read_text(encoding="utf-8"))
train_path = Path(config["cases"][case_id]["train_path"])
tokens = re.split(r"[\s,;]+", train_path.read_text(encoding="utf-8").strip())
print(",".join(token for token in tokens if token))
PY
}

for case_index in 0 1 2 3 4; do
  case_id="$(printf 'case%02d' "${case_index}")"
  seed=$((SEED_BASE + case_index))
  train_ids="$(train_ids_for_case "${case_id}")"
  for alpha in "${ALPHAS[@]}"; do
    alpha_slug="${alpha/./}"
    for variant in "${VARIANTS[@]}"; do
      run_id="${case_id}_alpha${alpha_slug}_${variant}"
      output_json="${RESULTS_DIR}/json/${run_id}.json"
      log_file="${RESULTS_DIR}/logs/${run_id}.log"
      if [[ "${RESUME}" -eq 1 && -f "${output_json}" ]]; then
        echo "SKIP ${run_id}"
        continue
      fi
      echo "RUN ${run_id}"
      if ! ./build_gpp/firebreak_cpp diagnose-fpp-master-lp \
        --experiment-id "${EXPERIMENT_ID}" \
        --case-id "${case_id}" \
        --seed-base "${SEED_BASE}" \
        --seed "${seed}" \
        --landscape "${LANDSCAPE}" \
        --forest-path "${FOREST_PATH}" \
        --results-path "${RESULTS_PATH}" \
        --train-ids "${train_ids}" \
        --alpha "${alpha}" \
        --variant "${variant}" \
        --threads "${THREADS}" \
        --path-llbi-max-paths-per-node "${PATH_LLBI_MAX_PATHS_PER_NODE}" \
        --projected-llbi-root-rounds "${PROJECTED_LLBI_ROOT_ROUNDS}" \
        --projected-llbi-max-cuts-per-round "${PROJECTED_LLBI_MAX_CUTS_PER_ROUND}" \
        --projected-llbi-violation-tolerance "${PROJECTED_LLBI_VIOLATION_TOLERANCE}" \
        --projected-llbi-cut-density-limit "${PROJECTED_LLBI_CUT_DENSITY_LIMIT}" \
        --output-json "${output_json}" \
        --output-csv "${OUTPUT_CSV}" \
        >"${log_file}" 2>&1; then
        echo "FAILED ${run_id}. Last log lines:" >&2
        tail -80 "${log_file}" >&2 || true
        exit 1
      fi
    done
  done
done

python3 scripts/validate_sub20_projected_llbi_lp_diagnostic.py \
  --results-dir "${RESULTS_DIR}" \
  --expected-rows 90 \
  --seed-base "${SEED_BASE}"

python3 scripts/analyze_sub20_projected_llbi_lp_diagnostic.py \
  --results-dir "${RESULTS_DIR}"

echo "Projected LLBI LP diagnostic complete. CSV: ${OUTPUT_CSV}"
