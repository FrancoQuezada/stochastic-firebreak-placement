#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_DIR}"

exec ./scripts/run_manifest.sh config/phase12_sub20_small_test_calibration.txt "$@"
