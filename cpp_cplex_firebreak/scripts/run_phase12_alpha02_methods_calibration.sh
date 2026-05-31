#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_DIR}"

if [[ "${CONFIRM_LONG_RUN:-0}" != "1" ]]; then
  echo "This calibration can take a long time." >&2
  echo "Re-run with CONFIRM_LONG_RUN=1 to launch it:" >&2
  echo "  CONFIRM_LONG_RUN=1 scripts/run_phase12_alpha02_methods_calibration.sh" >&2
  exit 2
fi

exec ./scripts/run_manifest.sh config/phase12_sub20_alpha02_methods_calibration.txt "$@"
