#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_DIR}"

if [[ $# -ne 2 ]]; then
  echo "Usage: scripts/aggregate_batch.sh <batch-results-csv> <output-summary-dir>" >&2
  echo "Example: scripts/aggregate_batch.sh results/batch/phase11_sub20_debug/batch_results.csv results/batch/phase11_sub20_debug/summary" >&2
  exit 2
fi

INPUT_CSV="$1"
OUTPUT_DIR="$2"

if [[ ! -x "build_gpp/firebreak_cpp" ]]; then
  echo "Missing build_gpp/firebreak_cpp. Run 'make build' or 'make cplex' first." >&2
  exit 2
fi

exec ./build_gpp/firebreak_cpp aggregate-batch \
  --input-csv "${INPUT_CSV}" \
  --output-dir "${OUTPUT_DIR}"
