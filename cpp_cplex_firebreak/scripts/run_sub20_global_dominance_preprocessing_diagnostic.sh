#!/usr/bin/env bash
set -euo pipefail

if [[ ! -f "Makefile" || ! -d "src" || ! -d "include" ]]; then
  echo "Run this script from cpp_cplex_firebreak/." >&2
  exit 1
fi

BIN="./build_gpp/firebreak_cpp"
if [[ ! -x "$BIN" ]]; then
  make cplex -j2
fi

SPLIT_DIR="results/batch/sub20_alpha001_002_fpp_exact_tl300/splits"
if [[ ! -d "$SPLIT_DIR" ]]; then
  SPLIT_DIR="results/batch/sub20_100train_900test_5cases_alpha002_003_tl300/splits"
fi
if [[ ! -d "$SPLIT_DIR" ]]; then
  echo "No compatible split directory found. Expected previous Sub20 experiment splits." >&2
  exit 1
fi

OUT_DIR="results/diagnostics/sub20_alpha001_002_global_dominance_preprocessing"
JSON_DIR="$OUT_DIR/json"
LOG_DIR="$OUT_DIR/logs"
CSV="$OUT_DIR/global_dominance_preprocessing_results.csv"
mkdir -p "$JSON_DIR" "$LOG_DIR"
rm -f "$CSV"

LANDSCAPE="Sub20"
FOREST_PATH="../sample_test/data/CanadianFBP/Sub20"
RESULTS_PATH="../sample_test/Sub20"
SEED_BASE=20260601
ALPHAS=("0.01" "0.02")

for case_idx in 0 1 2 3 4; do
  seed=$((SEED_BASE + case_idx))
  case_id=$(printf "case%02d" "$case_idx")
  train_file="$SPLIT_DIR/Sub20_seed${seed}_train100_test900_case${case_idx}_train.csv"
  if [[ ! -f "$train_file" ]]; then
    echo "Missing train split: $train_file" >&2
    exit 1
  fi
  train_count=$(wc -l < "$train_file" | tr -d ' ')
  if [[ "$train_count" != "100" ]]; then
    echo "Invalid train split count in $train_file: $train_count" >&2
    exit 1
  fi
  train_ids=$(paste -sd, "$train_file")

  for alpha in "${ALPHAS[@]}"; do
    alpha_slug=${alpha/./}
    run_id="${case_id}_alpha${alpha_slug}"
    echo "RUN global dominance preprocessing ${run_id}"
    "$BIN" diagnose-fpp-dominance-preprocessing \
      --experiment-id sub20_alpha001_002_global_dominance_preprocessing \
      --case-id "$case_id" \
      --seed-base "$SEED_BASE" \
      --seed "$seed" \
      --landscape "$LANDSCAPE" \
      --forest-path "$FOREST_PATH" \
      --results-path "$RESULTS_PATH" \
      --train-ids "$train_ids" \
      --alpha "$alpha" \
      --output-json "$JSON_DIR/${run_id}.json" \
      --output-csv "$CSV" \
      > "$LOG_DIR/${run_id}.log" 2>&1
  done
done

python3 scripts/analyze_sub20_global_dominance_preprocessing.py \
  --results-dir "$OUT_DIR"

echo "Done: $CSV"
