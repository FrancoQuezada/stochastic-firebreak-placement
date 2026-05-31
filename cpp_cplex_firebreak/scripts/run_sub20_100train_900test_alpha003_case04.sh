#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${SCRIPT_DIR}/run_sub20_100train_900test_master_parallel10.sh" --worker "0.03" "alpha003_case04" "20260605" "$@"
