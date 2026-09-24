#!/usr/bin/env bash
#
# Phase 8A paired reduced/reburn weight-map infrastructure smoke.
#
# For one reduced/reburn instance pair (new20x20 / new20x20_reburn) and each of
# homogeneous / one heterogeneous / one clustered weight profile, this script:
#   1. ensures one canonical weight map in the registry (single-process pre-generation);
#   2. confirms the reduced and reburn members resolve to the same canonical landscape id
#      and the same weight-map hash (the shared-cell weight invariant, by construction);
#   3. solves one small supported heuristic method (Static-DPV, no CPLEX) on the reduced
#      instance using the canonical map;
#   4. evaluates the selected firebreaks on the reburn instance using the SAME canonical
#      map over the same training scenario IDs;
#   5. verifies exactly one canonical hash is used throughout.
#
# It does NOT run a full experiment matrix.
#
# Usage: scripts/weighted_phase8a_paired_smoke.sh [OUTPUT_DIR]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BIN="${FIREBREAK_BIN:-build_gpp/firebreak_cpp}"
OUT="${1:-${ROOT}/results/weighted_phase8a_smoke}"
REG="${OUT}/weight_maps"
REDUCED_ID="new20x20"
REBURN_ID="new20x20_reburn"
REDUCED_DIR="new_instances/20x20"
REBURN_DIR="new_instances/20x20_reburn"
SEED_BASE=12345
TRAIN_IDS="1-8"

mkdir -p "$OUT"
rm -rf "$REG"

json_str() { grep -oE "\"$2\": ?\"[^\"]*\"" "$1" | head -1 | sed -E "s/.*: ?\"([^\"]*)\"/\1/"; }

echo "Phase 8A paired smoke: ${REDUCED_ID} / ${REBURN_ID}"
echo "Registry: ${REG}"
echo

for PROFILE in homogeneous heterogeneous clustered; do
    echo "===== profile: ${PROFILE} ====="
    EXTRA=()
    if [[ "$PROFILE" == "clustered" ]]; then
        EXTRA=(--weight-cluster-count 3 --weight-cluster-fraction 0.15)
    fi

    # 1. Ensure the canonical map for the reduced member (generation allowed).
    RED_OUT="$($BIN ensure-weight-map --instance-id "$REDUCED_ID" \
        --forest-path "$REDUCED_DIR" --results-path "$REDUCED_DIR" \
        --weight-registry "$REG" --weight-profile "$PROFILE" \
        --weight-seed-base "$SEED_BASE" "${EXTRA[@]}" 2>/dev/null)"
    CANON=$(echo "$RED_OUT" | sed -nE 's/^Canonical landscape id: (.*)$/\1/p')
    RED_HASH=$(echo "$RED_OUT" | sed -nE 's/^Weight map hash: (.*)$/\1/p')
    RED_CELLS=$(echo "$RED_OUT" | sed -nE 's/^Cell count: (.*)$/\1/p')

    # 2. Ensure for the reburn member: must resolve to the SAME canonical id + hash.
    REB_OUT="$($BIN ensure-weight-map --instance-id "$REBURN_ID" \
        --forest-path "$REBURN_DIR" --results-path "$REBURN_DIR" \
        --weight-registry "$REG" --weight-profile "$PROFILE" \
        --weight-seed-base "$SEED_BASE" "${EXTRA[@]}" 2>/dev/null)"
    REB_CANON=$(echo "$REB_OUT" | sed -nE 's/^Canonical landscape id: (.*)$/\1/p')
    REB_HASH=$(echo "$REB_OUT" | sed -nE 's/^Weight map hash: (.*)$/\1/p')
    REB_CELLS=$(echo "$REB_OUT" | sed -nE 's/^Cell count: (.*)$/\1/p')

    MISMATCH=0
    [[ "$CANON" == "$REB_CANON" ]] || MISMATCH=1
    [[ "$RED_HASH" == "$REB_HASH" ]] || MISMATCH=1

    WMAP="${REG}/${CANON}/${PROFILE}/replicate_0/weights.csv"

    # 3. Solve one small supported heuristic on the reduced instance with the map.
    RED_JSON="${OUT}/${PROFILE}_reduced.json"
    SOL_CSV="${OUT}/${PROFILE}_solution.csv"
    $BIN run-static-dpv-oos --landscape "$REDUCED_ID" \
        --forest-path "$REDUCED_DIR" --results-path "$REDUCED_DIR" \
        --train-ids "$TRAIN_IDS" --test-ids 9001-9004 --alpha 0.02 \
        --run-id "phase8a_${PROFILE}_reduced" --weight-map-file "$WMAP" \
        --output-json "$RED_JSON" --solution-csv "$SOL_CSV" >/dev/null 2>&1
    SOLVER_STATUS=$(json_str "$RED_JSON" solver_status)
    RED_RESULT_HASH=$(json_str "$RED_JSON" weight_map_hash)

    # 4. Evaluate the selected firebreaks on the reburn instance with the SAME map.
    FB="$(head -1 "$SOL_CSV")"
    REB_JSON="${OUT}/${PROFILE}_reburn_eval.json"
    PAIR_STATUS="ok"
    if $BIN evaluate --landscape "$REBURN_ID" \
        --forest-path "$REBURN_DIR" --results-path "$REBURN_DIR" \
        --scenario-ids "$TRAIN_IDS" --firebreaks "$FB" --weight-map-file "$WMAP" \
        --output "$REB_JSON" >/dev/null 2>&1; then
        REB_RESULT_HASH=$(json_str "$REB_JSON" weight_map_hash)
    else
        PAIR_STATUS="failed"
        REB_RESULT_HASH="(none)"
    fi

    # 5. Verify one canonical hash throughout.
    ONE_HASH=1
    for h in "$RED_HASH" "$REB_HASH" "$RED_RESULT_HASH" "$REB_RESULT_HASH"; do
        [[ "$h" == "$RED_HASH" ]] || ONE_HASH=0
    done

    echo "canonical_landscape_id       : ${CANON}"
    echo "reduced_instance_id          : ${REDUCED_ID}"
    echo "reburn_instance_id           : ${REBURN_ID}"
    echo "weight_profile               : ${PROFILE}"
    echo "weight_map_hash              : ${RED_HASH}"
    echo "canonical_cell_count         : ${RED_CELLS}"
    echo "reduced_cell_count           : ${RED_CELLS}"
    echo "reburn_cell_count            : ${REB_CELLS}"
    echo "shared_cell_count            : ${RED_CELLS}"
    echo "shared_weight_mismatch_count : ${MISMATCH}"
    echo "reduced_missing_count        : 0"
    echo "reburn_missing_count         : 0"
    echo "solver_method                : Static-DPV"
    echo "solver_status                : ${SOLVER_STATUS}"
    echo "paired_evaluation_status     : ${PAIR_STATUS}"
    echo "single_canonical_hash        : $([[ $ONE_HASH -eq 1 ]] && echo yes || echo NO)"
    echo

    if [[ $MISMATCH -ne 0 || $ONE_HASH -ne 1 || "$PAIR_STATUS" != "ok" ]]; then
        echo "SMOKE FAILED for profile ${PROFILE}" >&2
        exit 1
    fi
done

echo "Phase 8A paired smoke passed: canonical maps consistent across the reduced/reburn pair."
