#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_DIR}"

FIREBREAK_BIN="${PROJECT_DIR}/build_gpp/firebreak_cpp"
if [[ ! -x "${FIREBREAK_BIN}" ]]; then
  echo "Missing ${FIREBREAK_BIN}. Build with CPLEX before running this benchmark:" >&2
  echo "  CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 ./build_gpp.sh --with-cplex" >&2
  exit 2
fi

MODES="fpp_base,fpp_base_greedy,fpp_base_dominator,fpp_base_separator,fpp_base_dominator_separator,fpp_base_dominator_separator_greedy,fpp_cut,fpp_cut_greedy,fpp_cut_dominator,fpp_cut_separator,fpp_cut_dominator_separator,fpp_cut_dominator_separator_greedy"
LOG_DIR="results/batch/fpp_12mode_benchmark_logs"
mkdir -p "${LOG_DIR}"

run_alpha() {
  local alpha="$1"
  local suffix="$2"
  local output_dir="results/batch/fpp_12mode_benchmark_${suffix}"
  local output_csv="${output_dir}/batch_results.csv"
  local log_file="${LOG_DIR}/${suffix}.log"

  "${FIREBREAK_BIN}" run-batch-oos \
    --landscape Sub20 \
    --forest-path ../sample_test/data/CanadianFBP/Sub20 \
    --results-path ../sample_test/Sub20 \
    --alphas "${alpha}" \
    --train-counts 100 \
    --test-count 100 \
    --num-cases 5 \
    --seed-base 20260520 \
    --methods FPP-SAA \
    --fpp-modes "${MODES}" \
    --time-limit 600 \
    --mip-gap 0.001 \
    --threads 1 \
    --output-dir "${output_dir}" \
    --output-csv "${output_csv}" \
    --rerun-existing \
    > "${log_file}" 2>&1

  "${FIREBREAK_BIN}" aggregate-batch \
    --input-csv "${output_csv}" \
    --output-dir "${output_dir}/summary" \
    >> "${log_file}" 2>&1
}

if [[ "${PARALLEL:-0}" == "1" ]]; then
  run_alpha 0.01 alpha001 &
  pid1=$!
  run_alpha 0.02 alpha002 &
  pid2=$!
  run_alpha 0.03 alpha003 &
  pid3=$!
  wait "${pid1}"
  wait "${pid2}"
  wait "${pid3}"
else
  run_alpha 0.01 alpha001
  run_alpha 0.02 alpha002
  run_alpha 0.03 alpha003
fi

python3 scripts/analyze_fpp_12mode_benchmark.py
