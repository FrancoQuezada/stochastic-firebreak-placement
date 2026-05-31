#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_DIR}"

if [[ ! -x build_gpp/firebreak_cpp ]]; then
  echo "Missing build_gpp/firebreak_cpp. Build with CPLEX first:" >&2
  echo "  CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 ./build_gpp.sh --with-cplex" >&2
  exit 1
fi

OUT_DIR="results/batch/fpp_lp_vi_diagnostic"
mkdir -p "${OUT_DIR}"

./build_gpp/firebreak_cpp run-fpp-lp-diagnostic \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --alphas 0.01,0.02,0.03 \
  --train-count 100 \
  --test-count 100 \
  --num-cases 5 \
  --seed-base 20260520 \
  --time-limit 300 \
  --threads 1 \
  --offline-sep-max-rounds 10 \
  --offline-sep-max-cuts-per-round 500 \
  --offline-sep-min-violation 1e-6 \
  --offline-sep-max-scenarios-per-round 0 \
  --offline-sep-max-nodes-per-scenario 50 \
  --offline-sep-max-cut-cardinality 50 \
  --output-dir "${OUT_DIR}" \
  2>&1 | tee "${OUT_DIR}/run.log"

python3 scripts/analyze_fpp_lp_vi_diagnostic.py
