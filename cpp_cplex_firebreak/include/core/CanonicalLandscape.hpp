#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/LandscapeWeightGenerator.hpp"

namespace firebreak::core {

// Deterministic physical landscape identity. Independent of method, risk objective,
// scenario count, train/test split, compact node indices, and result directory.
struct CanonicalLandscapeIdentity {
    std::string landscape_family;
    std::string base_landscape_id;
    int grid_rows = 0;
    int grid_cols = 0;
    int cell_count = 0;
    std::string universe_hash;          // fnv1a64:... over the sorted physical universe
    std::string canonical_landscape_id; // family__RxC__<universe_hash_suffix>
    std::string reduced_instance_id;
    std::string reburn_instance_id;
    std::string paired_landscape_id;
};

// True when the instance id ends with the "_reburn" suffix.
bool is_reburn_instance(const std::string& instance_id);

// The instance id with a single trailing "_reburn" removed. Reduced and reburn members
// of a pair share the same family.
std::string landscape_family_from_instance(const std::string& instance_id);

// FNV-1a-64 digest ("fnv1a64:xxxxxxxxxxxxxxxx") over the full physical universe: the
// grid dimensions followed by every cell's (original_cell_id, row, column). Order
// independent (cells are sorted by original id first).
std::string landscape_universe_hash(const LandscapeCellUniverse& universe);

// Build the canonical identity for an instance from its physical universe. Validates the
// universe first (rejects duplicate original IDs / bad coordinates).
CanonicalLandscapeIdentity make_canonical_landscape_identity(
    const std::string& instance_id,
    const LandscapeCellUniverse& universe);

// Stable, process-independent generation seed derived from all logical inputs via
// FNV-1a-64. Never uses std::hash / std::random_device / wall-clock.
std::uint64_t derive_weight_generation_seed(
    std::uint64_t global_weight_seed,
    const std::string& canonical_landscape_id,
    const std::string& weight_profile,
    int weight_replicate,
    int weight_generator_version);

// Inputs to the deterministic experiment run identity.
struct RunIdentityInputs {
    std::string canonical_landscape_id;
    std::string instance_id;
    std::string method;
    std::string objective;   // e.g. "expected", "cvar", "mean_cvar"
    double cvar_beta = 0.9;
    double cvar_lambda = 1.0;
    int scenario_count = 0;
    double alpha = 0.0;
    int budget = 0;
    std::vector<int> train_ids;
    std::string weight_profile;   // empty => legacy homogeneous
    int weight_replicate = 0;
    std::string weight_map_hash;  // empty => legacy homogeneous
};

// Deterministic FNV-1a-64 run key. Identical inputs -> identical key; any weight-config
// change -> different key; legacy rows (empty weight fields) cannot collide with weighted
// rows.
std::string weighted_run_identity(const RunIdentityInputs& inputs);

}  // namespace firebreak::core
