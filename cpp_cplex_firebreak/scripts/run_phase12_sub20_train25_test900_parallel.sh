#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_DIR}"

if [[ "${CONFIRM_LONG_RUN:-0}" != "1" ]]; then
  echo "This launches three parallel alpha runs for train_counts=25, test_count=900, num_cases=10." >&2
  echo "Re-run with CONFIRM_LONG_RUN=1 to launch it:" >&2
  echo "  CONFIRM_LONG_RUN=1 scripts/run_phase12_sub20_train25_test900_parallel.sh" >&2
  exit 2
fi

if [[ ! -x "build_gpp/firebreak_cpp" ]]; then
  echo "Missing build_gpp/firebreak_cpp. Run './build_gpp.sh --with-cplex' first." >&2
  exit 2
fi

LOG_DIR="logs/phase12_sub20_train25_test900"
mkdir -p "${LOG_DIR}"

MANIFESTS=(
  "config/phase12_sub20_train25_test900_alpha001.txt"
  "config/phase12_sub20_train25_test900_alpha002.txt"
  "config/phase12_sub20_train25_test900_alpha003.txt"
)

LABELS=(
  "alpha001"
  "alpha002"
  "alpha003"
)

PIDS=()

for i in "${!MANIFESTS[@]}"; do
  manifest="${MANIFESTS[$i]}"
  label="${LABELS[$i]}"
  log_path="${LOG_DIR}/${label}.log"
  echo "Launching ${label}: ${manifest}"
  echo "  log: ${log_path}"
  ./build_gpp/firebreak_cpp run-manifest --manifest "${manifest}" > "${log_path}" 2>&1 &
  PIDS+=("$!")
done

status=0
for i in "${!PIDS[@]}"; do
  label="${LABELS[$i]}"
  pid="${PIDS[$i]}"
  if wait "${pid}"; then
    echo "Completed ${label}"
  else
    echo "Failed ${label}; see ${LOG_DIR}/${label}.log" >&2
    status=1
  fi
done

exit "${status}"
