#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${PROJECT_DIR}/.." && pwd)"

LANDSCAPE="Sub20"
FOREST_PATH="../sample_test/data/CanadianFBP/Sub20"
RESULTS_PATH="../sample_test/Sub20"
OUTPUT_ROOT="results/batch/sub20_100train_900test_5cases_alpha002_003_tl300"
TRAIN_COUNT=100
TEST_COUNT=900
NUM_CASES_PER_WORKER=1
TIME_LIMIT_SECONDS=300
MIP_GAP=0.001
THREADS=1
CVAR_BETA=0.9
CVAR_LAMBDA=0.5
EXPECTED_METHOD_COUNT=30
EXPECTED_CASE_COUNT=5
EXPECTED_ALPHA_COUNT=2
EXPECTED_ROWS=$((EXPECTED_METHOD_COUNT * EXPECTED_CASE_COUNT * EXPECTED_ALPHA_COUNT))

METHOD_LABELS=(
  "FPP-SAA"
  "FPP-SAA-CVaR"
  "FPP-SAA-MeanCVaR"
  "FPP-Benders"
  "FPP-Benders-CVaR"
  "FPP-Benders-MeanCVaR"
  "FPP-Branch-Benders"
  "FPP-Branch-Benders-CVaR"
  "FPP-Branch-Benders-MeanCVaR"
  "FPP-Branch-Benders-LLBI"
  "FPP-Branch-Benders-RootCuts"
  "FPP-Branch-Benders-LLBI-RootCuts"
  "FPP-Branch-Benders-CVaR-LLBI"
  "FPP-Branch-Benders-CVaR-RootCuts"
  "FPP-Branch-Benders-CVaR-LLBI-RootCuts"
  "FPP-Restricted-Branch-Benders"
  "FPP-Restricted-Branch-Benders-CVaR"
  "FPP-Restricted-Branch-Benders-MeanCVaR"
  "DPV-SAA"
  "DPV-Benders"
  "Static-DPV"
  "Static-DPV-MIP"
  "Greedy-DPV3"
  "Greedy-DPV2"
  "Greedy-Betweenness"
  "Greedy-Closeness"
  "DPV-Branch-Benders"
  "DPV-Branch-Benders-LLBI"
  "DPV-Branch-Benders-RootCuts"
  "DPV-Branch-Benders-LLBI-RootCuts"
)

WORKER_SCRIPTS=(
  "scripts/run_sub20_100train_900test_alpha002_case00.sh"
  "scripts/run_sub20_100train_900test_alpha002_case01.sh"
  "scripts/run_sub20_100train_900test_alpha002_case02.sh"
  "scripts/run_sub20_100train_900test_alpha002_case03.sh"
  "scripts/run_sub20_100train_900test_alpha002_case04.sh"
  "scripts/run_sub20_100train_900test_alpha003_case00.sh"
  "scripts/run_sub20_100train_900test_alpha003_case01.sh"
  "scripts/run_sub20_100train_900test_alpha003_case02.sh"
  "scripts/run_sub20_100train_900test_alpha003_case03.sh"
  "scripts/run_sub20_100train_900test_alpha003_case04.sh"
)

WORKER_NAMES=(
  "alpha002_case00"
  "alpha002_case01"
  "alpha002_case02"
  "alpha002_case03"
  "alpha002_case04"
  "alpha003_case00"
  "alpha003_case01"
  "alpha003_case02"
  "alpha003_case03"
  "alpha003_case04"
)

join_by_comma() {
  local IFS=,
  echo "$*"
}

shell_quote_command() {
  printf "%q " "$@"
  printf "\n"
}

method_list() {
  join_by_comma "${METHOD_LABELS[@]}"
}

print_usage() {
  cat <<'USAGE'
Usage:
  bash scripts/run_sub20_100train_900test_master_parallel10.sh
  bash scripts/run_sub20_100train_900test_master_parallel10.sh --dry-run

Internal worker mode:
  bash scripts/run_sub20_100train_900test_master_parallel10.sh --worker ALPHA WORKER_NAME CASE_SEED [--dry-run]
USAGE
}

require_project_invocation() {
  local current_dir
  current_dir="$(pwd -P)"
  if [[ "${current_dir}" != "${PROJECT_DIR}" && "${current_dir}" != "${REPO_ROOT}" ]]; then
    echo "Run this script from either:" >&2
    echo "  ${PROJECT_DIR}" >&2
    echo "or:" >&2
    echo "  ${REPO_ROOT}" >&2
    exit 2
  fi
  cd "${PROJECT_DIR}"
}

preflight() {
  require_project_invocation

  if [[ ! -x "./build_gpp/firebreak_cpp" ]]; then
    echo "Please build first with CPLEX support, e.g. make cplex or ./build_gpp.sh --with-cplex" >&2
    exit 2
  fi
  if [[ ! -d "${FOREST_PATH}" ]]; then
    echo "Missing Sub20 forest path: ${FOREST_PATH}" >&2
    exit 2
  fi
  if [[ ! -d "${RESULTS_PATH}" ]]; then
    echo "Missing Sub20 results path: ${RESULTS_PATH}" >&2
    exit 2
  fi
  if [[ "${#METHOD_LABELS[@]}" -ne "${EXPECTED_METHOD_COUNT}" ]]; then
    echo "Method list has ${#METHOD_LABELS[@]} labels; expected ${EXPECTED_METHOD_COUNT}." >&2
    exit 2
  fi
  if [[ "${EXPECTED_ROWS}" -ne 300 ]]; then
    echo "Expected row count computed as ${EXPECTED_ROWS}; expected 300." >&2
    exit 2
  fi
  if ! grep -q '"Static-DPV-MIP"' src/experiments/BatchExperimentConfig.cpp; then
    echo "Static-DPV-MIP is not registered in src/experiments/BatchExperimentConfig.cpp." >&2
    echo "Implement/register Static-DPV-MIP before launching this experiment." >&2
    exit 2
  fi
  if ! grep -a -q "Static-DPV-MIP" ./build_gpp/firebreak_cpp; then
    echo "The current ./build_gpp/firebreak_cpp binary does not appear to contain Static-DPV-MIP." >&2
    echo "Rebuild before launching, e.g. make cplex." >&2
    exit 2
  fi
  if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 is required to generate minimal_report.csv." >&2
    exit 2
  fi
}

build_worker_command() {
  local alpha="$1"
  local worker_name="$2"
  local case_seed="$3"
  local worker_output_dir="${OUTPUT_ROOT}/${worker_name}"
  local methods
  methods="$(method_list)"

  WORKER_COMMAND=(
    "./build_gpp/firebreak_cpp" "run-batch-oos"
    "--landscape" "${LANDSCAPE}"
    "--forest-path" "${FOREST_PATH}"
    "--results-path" "${RESULTS_PATH}"
    "--alphas" "${alpha}"
    "--train-counts" "${TRAIN_COUNT}"
    "--test-count" "${TEST_COUNT}"
    "--num-cases" "${NUM_CASES_PER_WORKER}"
    "--seed-base" "${case_seed}"
    "--methods" "${methods}"
    "--time-limit" "${TIME_LIMIT_SECONDS}"
    "--mip-gap" "${MIP_GAP}"
    "--threads" "${THREADS}"
    "--cvar-beta" "${CVAR_BETA}"
    "--cvar-lambda" "${CVAR_LAMBDA}"
    "--output-dir" "${worker_output_dir}"
    "--output-csv" "${worker_output_dir}/batch_results.csv"
  )
}

run_worker() {
  local alpha="$1"
  local worker_name="$2"
  local case_seed="$3"
  local dry_run="$4"
  local worker_output_dir="${OUTPUT_ROOT}/${worker_name}"
  local status_file="${worker_output_dir}/worker_status.txt"
  local log_file="${worker_output_dir}/worker.log"

  require_project_invocation
  build_worker_command "${alpha}" "${worker_name}" "${case_seed}"

  if [[ "${dry_run}" == "true" ]]; then
    echo "# ${worker_name}"
    shell_quote_command "${WORKER_COMMAND[@]}"
    return 0
  fi

  mkdir -p "${worker_output_dir}"
  echo "Worker log: ${worker_output_dir}/worker.log"
  exec >"${log_file}" 2>&1

  trap 'code=$?; echo "FAILED" > "'"${status_file}"'"; echo "[$(date -Is)] FAILED '"${worker_name}"' with exit code ${code}"; exit "${code}"' ERR

  echo "[$(date -Is)] START ${worker_name}"
  echo "alpha=${alpha}"
  echo "case_seed=${case_seed}"
  echo "method_count=${#METHOD_LABELS[@]}"
  shell_quote_command "${WORKER_COMMAND[@]}"
  echo "RUNNING" > "${status_file}"
  "${WORKER_COMMAND[@]}"
  echo "SUCCESS" > "${status_file}"
  echo "[$(date -Is)] END ${worker_name}"
}

combine_worker_csvs() {
  local combined_csv="${OUTPUT_ROOT}/batch_results_all.csv"
  local first="true"

  rm -f "${combined_csv}"
  for worker_name in "${WORKER_NAMES[@]}"; do
    local csv_path="${OUTPUT_ROOT}/${worker_name}/batch_results.csv"
    if [[ ! -s "${csv_path}" ]]; then
      echo "Missing worker CSV: ${csv_path}" >&2
      exit 1
    fi
    if [[ "${first}" == "true" ]]; then
      head -n 1 "${csv_path}" > "${combined_csv}"
      first="false"
    fi
    tail -n +2 "${csv_path}" >> "${combined_csv}"
  done

  local line_count data_rows
  line_count="$(wc -l < "${combined_csv}" | tr -d ' ')"
  data_rows=$((line_count - 1))
  if [[ "${data_rows}" -ne "${EXPECTED_ROWS}" ]]; then
    echo "Combined CSV has ${data_rows} data rows; expected ${EXPECTED_ROWS}." >&2
    exit 1
  fi
  echo "Combined CSV: ${combined_csv}"
  echo "Data rows: ${data_rows}"
}

run_aggregate() {
  ./build_gpp/firebreak_cpp aggregate-batch \
    --input-csv "${OUTPUT_ROOT}/batch_results_all.csv" \
    --output-dir "${OUTPUT_ROOT}/aggregate"
}

generate_minimal_report() {
  local input_csv="${OUTPUT_ROOT}/batch_results_all.csv"
  local output_csv="${OUTPUT_ROOT}/minimal_report.csv"

  python3 - "${input_csv}" "${output_csv}" <<'PY'
import csv
import sys

input_csv, output_csv = sys.argv[1], sys.argv[2]

candidate_groups = [
    ["experiment_id"],
    ["case_id"],
    ["alpha"],
    ["train_scenario_count"],
    ["test_scenario_count"],
    ["method"],
    ["objective_in_sample"],
    ["objective_metric"],
    ["solver_status"],
    ["best_bound"],
    ["mip_gap"],
    ["runtime_seconds"],
    ["selected_firebreaks"],
    ["train_expected_burned_area"],
    ["test_expected_burned_area"],
    ["test_empirical_var_90pct_burned_area", "test_var_burned_area", "test_empirical_var_burned_area"],
    ["test_cvar_burned_area", "test_empirical_cvar_90pct_burned_area", "test_empirical_cvar_burned_area"],
    ["test_worst_10pct_burned_area"],
    ["notes"],
]

with open(input_csv, newline="") as f:
    reader = csv.DictReader(f)
    if reader.fieldnames is None:
        raise SystemExit(f"Missing header in {input_csv}")
    field_set = set(reader.fieldnames)
    selected = []
    missing_required = []
    for group in candidate_groups:
        found = next((name for name in group if name in field_set), None)
        optional = group[0].startswith("test_empirical_var")
        if found is None:
            if not optional:
                missing_required.append(group[0])
            continue
        selected.append(found)

    if missing_required:
        raise SystemExit(
            "Missing required columns for minimal report: " + ", ".join(missing_required)
        )

    rows = [{name: row.get(name, "") for name in selected} for row in reader]

with open(output_csv, "w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=selected)
    writer.writeheader()
    writer.writerows(rows)

print(f"Wrote {output_csv} with {len(rows)} rows and {len(selected)} columns.")
PY
}

run_master() {
  local dry_run="$1"
  preflight

  echo "Method count: ${#METHOD_LABELS[@]}"
  echo "Expected data rows: ${EXPECTED_ROWS}"
  echo "Output root: ${OUTPUT_ROOT}"

  if [[ "${dry_run}" == "true" ]]; then
    echo "Dry run: worker launch commands"
    for worker_script in "${WORKER_SCRIPTS[@]}"; do
      shell_quote_command "bash" "${worker_script}" "--dry-run"
    done
    return 0
  fi

  mkdir -p "${OUTPUT_ROOT}"
  echo "[$(date -Is)] Launching ${#WORKER_SCRIPTS[@]} workers in parallel."

  local pids=()
  for worker_script in "${WORKER_SCRIPTS[@]}"; do
    echo "Launching ${worker_script}"
    bash "${worker_script}" &
    pids+=("$!")
  done

  local failed=0
  for pid in "${pids[@]}"; do
    if ! wait "${pid}"; then
      failed=1
    fi
  done

  for worker_name in "${WORKER_NAMES[@]}"; do
    local status_file="${OUTPUT_ROOT}/${worker_name}/worker_status.txt"
    local status="MISSING"
    if [[ -f "${status_file}" ]]; then
      status="$(cat "${status_file}")"
    fi
    if [[ "${status}" != "SUCCESS" ]]; then
      echo "Worker ${worker_name} status: ${status}" >&2
      failed=1
    fi
  done

  if [[ "${failed}" -ne 0 ]]; then
    echo "At least one worker failed. Inspect ${OUTPUT_ROOT}/*/worker.log." >&2
    exit 1
  fi

  combine_worker_csvs
  run_aggregate
  generate_minimal_report
  echo "[$(date -Is)] Experiment post-processing complete."
}

main() {
  if [[ "${1:-}" == "--worker" ]]; then
    if [[ "$#" -lt 4 ]]; then
      print_usage >&2
      exit 2
    fi
    local alpha="$2"
    local worker_name="$3"
    local case_seed="$4"
    shift 4
    local dry_run="false"
    while [[ "$#" -gt 0 ]]; do
      case "$1" in
        --dry-run)
          dry_run="true"
          ;;
        --help|-h)
          print_usage
          return 0
          ;;
        *)
          echo "Unknown worker argument: $1" >&2
          exit 2
          ;;
      esac
      shift
    done
    run_worker "${alpha}" "${worker_name}" "${case_seed}" "${dry_run}"
    return 0
  fi

  local dry_run="false"
  while [[ "$#" -gt 0 ]]; do
    case "$1" in
      --dry-run)
        dry_run="true"
        ;;
      --help|-h)
        print_usage
        return 0
        ;;
      *)
        echo "Unknown argument: $1" >&2
        exit 2
        ;;
    esac
    shift
  done

  run_master "${dry_run}"
}

main "$@"
