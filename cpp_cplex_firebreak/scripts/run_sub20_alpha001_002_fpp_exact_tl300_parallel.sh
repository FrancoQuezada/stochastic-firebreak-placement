#!/usr/bin/env bash
set -euo pipefail

OUTPUT_ROOT="results/batch/sub20_alpha001_002_fpp_exact_tl300"
WORKER_DIR="scripts/sub20_alpha001_002_tl300_workers"
WORKERS=(
  worker_00
  worker_01
  worker_02
  worker_03
  worker_04
  worker_05
  worker_06
  worker_07
  worker_08
  worker_09
)

require_project_root() {
  if [[ ! -f "src/main.cpp" || ! -d "include" ]]; then
    echo "Run this script from cpp_cplex_firebreak/." >&2
    exit 2
  fi
}

ensure_binary() {
  if command -v make >/dev/null 2>&1 && [[ -f "Makefile" ]]; then
    echo "Building/verifying CPLEX binary with make cplex"
    make cplex -j "${BUILD_JOBS:-2}"
  elif [[ -x "./build_gpp.sh" ]]; then
    echo "Building/verifying CPLEX binary with ./build_gpp.sh --with-cplex"
    if [[ -z "${CPLEX_STUDIO_DIR:-}" && -d "/opt/ibm/ILOG/CPLEX_Studio2211" ]]; then
      CPLEX_STUDIO_DIR="/opt/ibm/ILOG/CPLEX_Studio2211" ./build_gpp.sh --with-cplex
    else
      ./build_gpp.sh --with-cplex
    fi
  fi
  if [[ ! -x "build_gpp/firebreak_cpp" ]]; then
    echo "Missing build_gpp/firebreak_cpp. Build with CPLEX support before launching." >&2
    exit 2
  fi
}

prepare_infrastructure() {
  python3 scripts/prepare_sub20_alpha001_002_shared_splits.py \
    --results-dir "${OUTPUT_ROOT}"
  python3 scripts/generate_sub20_alpha001_002_fpp_exact_tl300_manifests.py \
    --results-dir "${OUTPUT_ROOT}"
  python3 scripts/generate_sub20_alpha001_002_fpp_exact_tl300_manifests.py \
    --results-dir "${OUTPUT_ROOT}" \
    --verify-only
  for worker in "${WORKERS[@]}"; do
    mkdir -p "${OUTPUT_ROOT}/${worker}/logs" \
             "${OUTPUT_ROOT}/${worker}/json" \
             "${OUTPUT_ROOT}/${worker}/solutions"
  done
}

launch_workers() {
  local pids=()
  local names=()
  for worker in "${WORKERS[@]}"; do
    local script="${WORKER_DIR}/run_${worker}.sh"
    local stdout_log="${OUTPUT_ROOT}/${worker}/logs/stdout.log"
    if [[ ! -f "${script}" ]]; then
      echo "Missing worker script: ${script}" >&2
      exit 2
    fi
    echo "Launching ${worker}"
    bash "${script}" >"${stdout_log}" 2>&1 &
    pids+=("$!")
    names+=("${worker}")
  done

  local failed=0
  for index in "${!pids[@]}"; do
    if wait "${pids[$index]}"; then
      echo "${names[$index]} completed"
    else
      echo "${names[$index]} failed; see ${OUTPUT_ROOT}/${names[$index]}/logs/stdout.log" >&2
      failed=1
    fi
  done
  if [[ "${failed}" -ne 0 ]]; then
    exit 1
  fi
}

merge_validate_analyze() {
  python3 scripts/merge_sub20_alpha001_002_fpp_exact_tl300.py \
    --results-dir "${OUTPUT_ROOT}" \
    --expected-workers 10 \
    --expected-rows-per-worker 45
  python3 scripts/validate_sub20_alpha001_002_fpp_exact_tl300.py \
    --results-dir "${OUTPUT_ROOT}" \
    --expected-rows 450 \
    --expected-workers 10 \
    --expected-rows-per-worker 45 \
    --seed-base 20260601
  python3 scripts/analyze_sub20_alpha001_002_fpp_exact_tl300.py \
    --results-dir "${OUTPUT_ROOT}"
}

main() {
  require_project_root
  ensure_binary
  prepare_infrastructure
  launch_workers
  merge_validate_analyze
}

main "$@"
