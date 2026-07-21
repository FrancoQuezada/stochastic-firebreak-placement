#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/LandscapeWeightMap.hpp"
#include "opt/IndexMapper.hpp"

namespace firebreak::experiments {

// Result of mapping a canonical (original-id keyed) weight map onto one instance's
// compact universe.
struct InstanceWeightMapping {
    std::vector<double> compact_weights;   // indexed by compact index
    int mapped_count = 0;
    int missing_count = 0;
    std::vector<int> missing_original_ids;
    std::string mapping_method = "original_cell_id";
    std::string mapping_hash;              // fnv1a64 over (compact_index, original_id, weight)
};

// Map a canonical weight map onto an instance by original Cell2Fire ID. A compact node
// whose original ID is absent from the map is a hard error (Phase 8A requires complete
// coverage of the instance universe).
InstanceWeightMapping map_weight_map_to_instance(
    const core::LandscapeWeightMap& map,
    const opt::IndexMapper& index_mapper);

// Explicit coordinate fallback, used only when stable original IDs are unavailable.
// `coordinate_of_original_id` gives the exact (row, column) of each cell in the map;
// `instance_cells` lists the instance's cells as (compact_index, row, column) with
// compact indices 0..N-1. Rejects duplicate coordinates, ambiguous matches, and missing
// coordinates. No fuzzy nearest-neighbor matching.
InstanceWeightMapping map_weight_map_to_instance_by_coordinate(
    const core::LandscapeWeightMap& map,
    const std::vector<std::pair<int, std::pair<int, int>>>& coordinate_of_original_id,
    const std::vector<std::pair<int, std::pair<int, int>>>& instance_cells);

// Report comparing the canonical map across a reduced/reburn pair.
struct ReducedReburnMappingReport {
    int canonical_cell_count = 0;
    int reduced_cell_count = 0;
    int reburn_cell_count = 0;
    int shared_cell_count = 0;
    int reduced_mapped_count = 0;
    int reburn_mapped_count = 0;
    int reduced_missing_count = 0;
    int reburn_missing_count = 0;
    int duplicate_original_id_count = 0;
    int shared_weight_mismatch_count = 0;
    std::string mapping_method = "original_cell_id";
    std::string mapping_hash;
};

// Compare the canonical map over the reduced and reburn instances' original-ID universes.
// The mandatory invariant is shared_weight_mismatch_count == 0. Duplicate original IDs in
// either member are detected and reported (and rejected via a thrown error).
ReducedReburnMappingReport compare_reduced_reburn(
    const core::LandscapeWeightMap& map,
    const std::vector<int>& reduced_original_ids,
    const std::vector<int>& reburn_original_ids);

}  // namespace firebreak::experiments
