#!/usr/bin/env bash
set -euo pipefail

INSTANCE_FILTER="${INSTANCE_FILTER:-new20x20}" \
TRAIN_COUNTS="${TRAIN_COUNTS:-5}" \
ALPHAS="${ALPHAS:-0.01}" \
TEST_COUNT="${TEST_COUNT:-10}" \
TRAINING_POOL_MIN="${TRAINING_POOL_MIN:-1}" \
TRAINING_POOL_MAX="${TRAINING_POOL_MAX:-100}" \
TEST_POOL_MIN="${TEST_POOL_MIN:-101}" \
TEST_POOL_MAX="${TEST_POOL_MAX:-110}" \
NUM_CASES="${NUM_CASES:-1}" \
TIME_LIMIT="${TIME_LIMIT:-60}" \
MAX_PARALLEL_JOBS="${MAX_PARALLEL_JOBS:-1}" \
OUTPUT_DIR="${OUTPUT_DIR:-results/batch/fpp_new_instances_scaling_smoke}" \
SMOKE_METHODS="${SMOKE_METHODS:-1}" \
scripts/run_fpp_new_instances_scaling_experiment.sh
