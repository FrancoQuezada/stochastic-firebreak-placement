#!/usr/bin/env bash
#
# Unified release-validation script for the weighted-landscape system
# (Phase 10). Runs a graded set of checks and produces a machine-readable
# report (weighted_release_validation.json).
#
# Usage:
#   scripts/validate_weighted_landscapes_release.sh [--quick|--full] \
#       [--with-cplex] [--skip-smoke] [--output-dir DIR]
#
# --quick (default): syntax checks, focused Python unit tests, non-CPLEX
#   build, a schema sanity check, and git diff --check.
# --full: everything in --quick, plus `make test`, a small end-to-end
#   paired smoke workflow (merge + analysis on top), and a reproducibility
#   check of the manifest/merge/analysis layers. Pass --with-cplex to also
#   run `make cplex` and let the smoke workflow actually solve (otherwise
#   the smoke stage is skipped with a clear reason, since it requires a
#   CPLEX-enabled binary).
#
# Never launches a large production experiment. Never modifies
# results/weighted_phase8b_smoke or any other tracked source fixture --
# all smoke/reproducibility work happens in a fresh output directory.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MODE="quick"
WITH_CPLEX=0
SKIP_SMOKE=0
OUTPUT_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick) MODE="quick"; shift ;;
        --full) MODE="full"; shift ;;
        --with-cplex) WITH_CPLEX=1; shift ;;
        --skip-smoke) SKIP_SMOKE=1; shift ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ -z "$OUTPUT_DIR" ]]; then
    OUTPUT_DIR="${ROOT}/results/validation_runs/run_$$"
fi
mkdir -p "$OUTPUT_DIR"
REPORT_JSON="${OUTPUT_DIR}/weighted_release_validation.json"

STAGE_LOG="${OUTPUT_DIR}/stages.log"
: > "$STAGE_LOG"

TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0
FAILED_STAGES=()
SKIPPED_STAGES=()
START_TIME="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

log_stage() {
    echo "==> $1" | tee -a "$STAGE_LOG"
}

skip_stage() {
    echo "SKIP: $1" | tee -a "$STAGE_LOG"
    SKIPPED_STAGES+=("$1")
    TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
}

fail_stage() {
    echo "FAIL: $1" | tee -a "$STAGE_LOG"
    FAILED_STAGES+=("$1")
    TESTS_FAILED=$((TESTS_FAILED + 1))
}

pass_stage() {
    echo "PASS: $1" | tee -a "$STAGE_LOG"
    TESTS_PASSED=$((TESTS_PASSED + 1))
}

PY_FILES=$(find scripts -maxdepth 1 \( \
    -name "weighted_*.py" -o -name "test_weighted_*.py" \
    -o -name "merge_weighted_experiment_results.py" \
    -o -name "analyze_weighted_experiment_results.py" \
    \) | sort -u)

# --- Stage: syntax checks -------------------------------------------------
log_stage "Syntax checks (py_compile)"
TESTS_RUN=$((TESTS_RUN + 1))
if python3 -m py_compile $PY_FILES; then
    pass_stage "py_compile"
else
    fail_stage "py_compile"
fi

# --- Stage: focused Python unit tests --------------------------------------
# test_weighted_manifest_worker.py is the one exception: it invokes the
# real compiled binary end-to-end and requires a CPLEX-enabled build. It is
# run separately, only in --full --with-cplex mode, after `make cplex`.
log_stage "Focused Python unit tests (pure, no CPLEX binary required)"
TEST_SCRIPTS=$(find scripts -maxdepth 1 -name "test_weighted_*.py" ! -name "test_weighted_manifest_worker.py" | sort)
PY_TEST_TOTAL_PASS=0
PY_TEST_TOTAL_FAIL=0
for test_script in $TEST_SCRIPTS; do
    TESTS_RUN=$((TESTS_RUN + 1))
    name="$(basename "$test_script")"
    if output=$(python3 "$test_script" 2>&1); then
        pass_stage "$name"
        line=$(echo "$output" | tail -1)
        n_pass=$(echo "$line" | grep -oP '\d+(?= passed)' || echo 0)
        n_fail=$(echo "$line" | grep -oP '\d+(?= failed)' || echo 0)
        PY_TEST_TOTAL_PASS=$((PY_TEST_TOTAL_PASS + n_pass))
        PY_TEST_TOTAL_FAIL=$((PY_TEST_TOTAL_FAIL + n_fail))
    else
        fail_stage "$name"
        echo "$output" >> "$STAGE_LOG"
    fi
done
echo "Python test assertions: ${PY_TEST_TOTAL_PASS} passed, ${PY_TEST_TOTAL_FAIL} failed" | tee -a "$STAGE_LOG"

# --- Stage: non-CPLEX build -------------------------------------------------
log_stage "make build (non-CPLEX)"
TESTS_RUN=$((TESTS_RUN + 1))
if make build >> "$STAGE_LOG" 2>&1; then
    pass_stage "make build"
else
    fail_stage "make build"
fi

# --- Stage: schema sanity check ---------------------------------------------
log_stage "Schema sanity check"
TESTS_RUN=$((TESTS_RUN + 1))
if python3 -c "
import sys
sys.path.insert(0, 'scripts')
import weighted_result_schema as schema
assert schema.RESULT_SCHEMA_VERSION
assert len(schema.CANONICAL_FIELDS) > 50
names = [f.name for f in schema.CANONICAL_FIELDS]
assert len(names) == len(set(names)), 'duplicate canonical field names'
print('schema OK:', schema.RESULT_SCHEMA_VERSION, len(schema.CANONICAL_FIELDS), 'fields')
" >> "$STAGE_LOG" 2>&1; then
    pass_stage "schema sanity check"
else
    fail_stage "schema sanity check"
fi

# --- Stage: diff checks ------------------------------------------------------
log_stage "git diff --check"
TESTS_RUN=$((TESTS_RUN + 1))
if git diff --check >> "$STAGE_LOG" 2>&1; then
    pass_stage "git diff --check"
else
    fail_stage "git diff --check"
fi

TESTS_RUN=$((TESTS_RUN + 1))
if git diff --cached --check >> "$STAGE_LOG" 2>&1; then
    pass_stage "git diff --cached --check"
else
    fail_stage "git diff --cached --check"
fi

CPLEX_ENABLED=0
PAIRED_WORKFLOW_STATUS="skipped"
MERGE_STATUS="skipped"
ANALYSIS_STATUS="skipped"
REPRODUCIBILITY_STATUS="skipped"

if [[ "$MODE" == "full" ]]; then
    log_stage "make test (non-solver C++ regression suite)"
    TESTS_RUN=$((TESTS_RUN + 1))
    if make test >> "$STAGE_LOG" 2>&1; then
        pass_stage "make test"
    else
        fail_stage "make test"
    fi

    if [[ "$WITH_CPLEX" == "1" ]]; then
        log_stage "make cplex"
        TESTS_RUN=$((TESTS_RUN + 1))
        if make cplex >> "$STAGE_LOG" 2>&1; then
            pass_stage "make cplex"
            CPLEX_ENABLED=1
        else
            fail_stage "make cplex"
        fi

        if [[ "$CPLEX_ENABLED" == "1" ]]; then
            log_stage "test_weighted_manifest_worker.py (requires CPLEX binary)"
            TESTS_RUN=$((TESTS_RUN + 1))
            if python3 scripts/test_weighted_manifest_worker.py >> "$STAGE_LOG" 2>&1; then
                pass_stage "test_weighted_manifest_worker.py"
            else
                fail_stage "test_weighted_manifest_worker.py"
            fi
        fi
    else
        skip_stage "make cplex (pass --with-cplex to enable)"
        skip_stage "test_weighted_manifest_worker.py (requires --with-cplex)"
    fi

    if [[ "$SKIP_SMOKE" == "1" ]]; then
        skip_stage "end-to-end paired smoke workflow (--skip-smoke)"
    elif [[ "$CPLEX_ENABLED" != "1" ]]; then
        skip_stage "end-to-end paired smoke workflow (requires --with-cplex)"
    else
        log_stage "End-to-end paired smoke workflow"
        SMOKE_DIR="${OUTPUT_DIR}/smoke"
        TESTS_RUN=$((TESTS_RUN + 1))
        if bash scripts/weighted_phase8b_batch_smoke.sh "$SMOKE_DIR" >> "$STAGE_LOG" 2>&1; then
            pass_stage "paired smoke workflow"
            PAIRED_WORKFLOW_STATUS="passed"
        else
            fail_stage "paired smoke workflow"
            PAIRED_WORKFLOW_STATUS="failed"
        fi

        log_stage "Merge + analysis smoke"
        MERGED_DIR="${OUTPUT_DIR}/merged"
        ANALYSIS_DIR="${OUTPUT_DIR}/analysis"
        TESTS_RUN=$((TESTS_RUN + 1))
        if python3 scripts/merge_weighted_experiment_results.py \
            --input-root "$SMOKE_DIR" --output-dir "$MERGED_DIR" --strict >> "$STAGE_LOG" 2>&1; then
            pass_stage "merge smoke"
            MERGE_STATUS="passed"
        else
            fail_stage "merge smoke"
            MERGE_STATUS="failed"
        fi

        TESTS_RUN=$((TESTS_RUN + 1))
        if [[ "$MERGE_STATUS" == "passed" ]] && python3 scripts/analyze_weighted_experiment_results.py \
            --merged-current-valid "${MERGED_DIR}/merged_current_valid.csv" \
            --merged-all-attempts "${MERGED_DIR}/merged_all_attempts.csv" \
            --output-dir "$ANALYSIS_DIR" >> "$STAGE_LOG" 2>&1; then
            pass_stage "analysis smoke"
            ANALYSIS_STATUS="passed"
        else
            fail_stage "analysis smoke"
            ANALYSIS_STATUS="failed"
        fi

        log_stage "Reproducibility check (manifest + merge + analysis determinism)"
        TESTS_RUN=$((TESTS_RUN + 1))
        REPRO_DIR="${OUTPUT_DIR}/repro"
        mkdir -p "$REPRO_DIR"
        if [[ "$MERGE_STATUS" == "passed" && "$ANALYSIS_STATUS" == "passed" ]]; then
            MERGED_DIR2="${REPRO_DIR}/merged"
            ANALYSIS_DIR2="${REPRO_DIR}/analysis"
            python3 scripts/merge_weighted_experiment_results.py \
                --input-root "$SMOKE_DIR" --output-dir "$MERGED_DIR2" --strict >> "$STAGE_LOG" 2>&1
            python3 scripts/analyze_weighted_experiment_results.py \
                --merged-current-valid "${MERGED_DIR2}/merged_current_valid.csv" \
                --merged-all-attempts "${MERGED_DIR2}/merged_all_attempts.csv" \
                --output-dir "$ANALYSIS_DIR2" >> "$STAGE_LOG" 2>&1
            REPRO_OK=1
            for f in merged_current_valid.csv merged_all_attempts.csv; do
                if ! diff -q "${MERGED_DIR}/${f}" "${MERGED_DIR2}/${f}" > /dev/null 2>&1; then
                    REPRO_OK=0
                    echo "Reproducibility mismatch: $f" >> "$STAGE_LOG"
                fi
            done
            for f in gaps/row_level_gaps.csv summaries/method_summary.csv tables/table_best_known.csv; do
                if ! diff -q "${ANALYSIS_DIR}/${f}" "${ANALYSIS_DIR2}/${f}" > /dev/null 2>&1; then
                    REPRO_OK=0
                    echo "Reproducibility mismatch: $f" >> "$STAGE_LOG"
                fi
            done
            if [[ "$REPRO_OK" == "1" ]]; then
                pass_stage "reproducibility check"
                REPRODUCIBILITY_STATUS="passed"
            else
                fail_stage "reproducibility check"
                REPRODUCIBILITY_STATUS="failed"
            fi
        else
            skip_stage "reproducibility check (prerequisite merge/analysis smoke did not pass)"
            REPRODUCIBILITY_STATUS="skipped"
        fi
    fi
fi

END_TIME="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
GIT_COMMIT="$(git rev-parse HEAD 2>/dev/null || echo unknown)"
GIT_BRANCH="$(git branch --show-current 2>/dev/null || echo unknown)"
DIRTY="False"
if [[ -n "$(git status --porcelain 2>/dev/null)" ]]; then
    DIRTY="True"
fi
COMPILER="$(g++ --version 2>/dev/null | head -1 || echo unknown)"
PYTHON_VERSION="$(python3 --version 2>&1)"

json_array() {
    if [[ $# -eq 0 ]]; then
        echo "[]"
        return
    fi
    local out="["
    local first=1
    for item in "$@"; do
        if [[ "$first" == "1" ]]; then first=0; else out+=","; fi
        out+="\"$(printf '%s' "$item" | sed 's/"/\\"/g')\""
    done
    out+="]"
    echo "$out"
}
FAILED_STAGES_JSON="$(json_array "${FAILED_STAGES[@]+"${FAILED_STAGES[@]}"}")"
SKIPPED_STAGES_JSON="$(json_array "${SKIPPED_STAGES[@]+"${SKIPPED_STAGES[@]}"}")"

python3 - "$REPORT_JSON" <<PYEOF
import json, sys

report = {
    "git_commit": "${GIT_COMMIT}",
    "branch": "${GIT_BRANCH}",
    "dirty_worktree": ${DIRTY},
    "compiler": """${COMPILER}""".strip(),
    "cplex_enabled": bool(${CPLEX_ENABLED}),
    "python_version": """${PYTHON_VERSION}""".strip(),
    "tests_run": ${TESTS_RUN},
    "tests_passed": ${TESTS_PASSED},
    "tests_failed": ${TESTS_FAILED},
    "tests_skipped": ${TESTS_SKIPPED},
    "python_assertion_pass_count": ${PY_TEST_TOTAL_PASS},
    "python_assertion_fail_count": ${PY_TEST_TOTAL_FAIL},
    "quick_or_full": "${MODE}",
    "weight_profiles_tested": ["homogeneous", "heterogeneous", "clustered"] if "${PAIRED_WORKFLOW_STATUS}" == "passed" else [],
    "risk_measures_tested": ["expected"] if "${PAIRED_WORKFLOW_STATUS}" == "passed" else [],
    "methods_tested": ["FPP-SAA", "FPP-Branch-Benders-Combinatorial", "DPV-SAA", "Static-DPV"] if "${PAIRED_WORKFLOW_STATUS}" == "passed" else [],
    "paired_workflow_status": "${PAIRED_WORKFLOW_STATUS}",
    "merge_status": "${MERGE_STATUS}",
    "analysis_status": "${ANALYSIS_STATUS}",
    "reproducibility_status": "${REPRODUCIBILITY_STATUS}",
    "start_time": "${START_TIME}",
    "end_time": "${END_TIME}",
    "failed_stages": ${FAILED_STAGES_JSON},
    "skipped_stages": ${SKIPPED_STAGES_JSON},
}
with open(sys.argv[1], "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2, sort_keys=True)
    handle.write("\n")
print(json.dumps(report, indent=2, sort_keys=True))
PYEOF

echo ""
echo "================================================================"
echo "Validation summary (${MODE} mode): ${TESTS_PASSED} passed, ${TESTS_FAILED} failed, ${TESTS_SKIPPED} skipped"
echo "Report: ${REPORT_JSON}"
echo "Stage log: ${STAGE_LOG}"
echo "================================================================"

if [[ "$TESTS_FAILED" -gt 0 ]]; then
    exit 1
fi
exit 0
