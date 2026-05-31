#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_DIR}"

if [[ ! -x "build_gpp/firebreak_cpp" ]]; then
  echo "Missing build_gpp/firebreak_cpp. Run './build_gpp.sh --with-cplex' first." >&2
  exit 2
fi

OUTPUT_DIRS=(
  "results/batch/sub20_train25_test900_alpha001"
  "results/batch/sub20_train25_test900_alpha002"
  "results/batch/sub20_train25_test900_alpha003"
)

COMBINED_DIR="results/batch/sub20_train25_test900_all_alphas"
COMBINED_CSV="${COMBINED_DIR}/batch_results.csv"

missing=0
for output_dir in "${OUTPUT_DIRS[@]}"; do
  input_csv="${output_dir}/batch_results.csv"
  if [[ ! -f "${input_csv}" ]]; then
    echo "Missing ${input_csv}" >&2
    missing=1
    continue
  fi
  mkdir -p "${output_dir}/summary"
  ./build_gpp/firebreak_cpp aggregate-batch \
    --input-csv "${input_csv}" \
    --output-dir "${output_dir}/summary"
done

if [[ "${missing}" -ne 0 ]]; then
  echo "Aggregation stopped because one or more alpha batch_results.csv files are missing." >&2
  exit 2
fi

mkdir -p "${COMBINED_DIR}/summary"
head -n 1 "${OUTPUT_DIRS[0]}/batch_results.csv" > "${COMBINED_CSV}"
for output_dir in "${OUTPUT_DIRS[@]}"; do
  tail -n +2 "${output_dir}/batch_results.csv" >> "${COMBINED_CSV}"
done

./build_gpp/firebreak_cpp aggregate-batch \
  --input-csv "${COMBINED_CSV}" \
  --output-dir "${COMBINED_DIR}/summary"

echo "Combined CSV: ${COMBINED_CSV}"
echo "Combined summary: ${COMBINED_DIR}/summary"
