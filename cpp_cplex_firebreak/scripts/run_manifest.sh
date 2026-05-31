#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_DIR}"

if [[ $# -lt 1 ]]; then
  echo "Usage: scripts/run_manifest.sh <manifest-path> [extra firebreak_cpp args]" >&2
  echo "Example: scripts/run_manifest.sh config/phase11_sub20_debug.txt" >&2
  exit 2
fi

MANIFEST="$1"
shift

if [[ ! -x "build_gpp/firebreak_cpp" ]]; then
  echo "Missing build_gpp/firebreak_cpp. Run 'make build' or 'make cplex' first." >&2
  exit 2
fi

exec ./build_gpp/firebreak_cpp run-manifest --manifest "${MANIFEST}" "$@"
