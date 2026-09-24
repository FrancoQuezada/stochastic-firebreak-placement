#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "core/CanonicalLandscape.hpp"
#include "core/LandscapeWeightGenerator.hpp"

namespace {

firebreak::core::LandscapeCellUniverse make_universe(int rows, int cols) {
    firebreak::core::LandscapeCellUniverse universe;
    universe.source = "test";
    universe.rows = rows;
    universe.cols = cols;
    for (int cell_id = 1; cell_id <= rows * cols; ++cell_id) {
        const int zero_based = cell_id - 1;
        universe.cells.push_back(firebreak::core::WeightedLandscapeCell{
            cell_id, zero_based / cols + 1, zero_based % cols + 1});
    }
    return universe;
}

void test_universe_hash_determinism_and_order_independence() {
    const auto a = make_universe(4, 5);
    auto b = a;
    std::reverse(b.cells.begin(), b.cells.end());
    const std::string hash_a = firebreak::core::landscape_universe_hash(a);
    const std::string hash_b = firebreak::core::landscape_universe_hash(b);
    assert(hash_a == hash_b);            // order independent
    assert(hash_a.rfind("fnv1a64:", 0) == 0);

    const auto different = make_universe(5, 4);  // same cell count, different geometry
    assert(firebreak::core::landscape_universe_hash(different) != hash_a);
}

void test_family_and_pairing() {
    assert(firebreak::core::is_reburn_instance("new20x20_reburn"));
    assert(!firebreak::core::is_reburn_instance("new20x20"));
    assert(firebreak::core::landscape_family_from_instance("new20x20_reburn") == "new20x20");
    assert(firebreak::core::landscape_family_from_instance("new20x20") == "new20x20");

    const auto universe = make_universe(20, 20);
    const auto reduced = firebreak::core::make_canonical_landscape_identity("new20x20", universe);
    const auto reburn =
        firebreak::core::make_canonical_landscape_identity("new20x20_reburn", universe);

    // Reduced and reburn members of a pair share the canonical identity and universe hash.
    assert(reduced.canonical_landscape_id == reburn.canonical_landscape_id);
    assert(reduced.universe_hash == reburn.universe_hash);
    assert(reduced.landscape_family == "new20x20");
    assert(reduced.reduced_instance_id == "new20x20");
    assert(reduced.reburn_instance_id == "new20x20_reburn");
    assert(reduced.paired_landscape_id == "new20x20_reburn");
    assert(reburn.paired_landscape_id == "new20x20");
    assert(reduced.cell_count == 400);
    assert(reduced.canonical_landscape_id.find("new20x20__20x20__") == 0);
}

void test_seed_derivation() {
    const std::string canonical = "new20x20__20x20__deadbeefdeadbeef";
    const auto s1 = firebreak::core::derive_weight_generation_seed(
        12345, canonical, "heterogeneous", 0, 1);
    const auto s2 = firebreak::core::derive_weight_generation_seed(
        12345, canonical, "heterogeneous", 0, 1);
    assert(s1 == s2);  // stable across calls (process independent)

    // Distinct on any logical input change.
    assert(s1 != firebreak::core::derive_weight_generation_seed(
                     12345, canonical, "heterogeneous", 1, 1));
    assert(s1 != firebreak::core::derive_weight_generation_seed(
                     12345, canonical, "clustered", 0, 1));
    assert(s1 != firebreak::core::derive_weight_generation_seed(
                     999, canonical, "heterogeneous", 0, 1));
    assert(s1 != firebreak::core::derive_weight_generation_seed(
                     12345, "other__1x1__0", "heterogeneous", 0, 1));
}

firebreak::core::RunIdentityInputs base_inputs() {
    firebreak::core::RunIdentityInputs inputs;
    inputs.canonical_landscape_id = "new20x20__20x20__abc";
    inputs.instance_id = "new20x20";
    inputs.method = "FPP-SAA";
    inputs.objective = "expected";
    inputs.scenario_count = 100;
    inputs.alpha = 0.02;
    inputs.budget = 8;
    inputs.train_ids = {3, 1, 2};
    inputs.weight_profile = "heterogeneous";
    inputs.weight_replicate = 0;
    inputs.weight_map_hash = "fnv1a64:1111111111111111";
    return inputs;
}

void test_run_identity() {
    const auto base = base_inputs();
    const std::string id = firebreak::core::weighted_run_identity(base);

    // Deterministic and order-independent in train_ids.
    auto reordered = base;
    reordered.train_ids = {1, 2, 3};
    assert(firebreak::core::weighted_run_identity(reordered) == id);

    // Any weight-config change gives a different id.
    auto diff_profile = base;
    diff_profile.weight_profile = "clustered";
    assert(firebreak::core::weighted_run_identity(diff_profile) != id);

    auto diff_replicate = base;
    diff_replicate.weight_replicate = 1;
    assert(firebreak::core::weighted_run_identity(diff_replicate) != id);

    auto diff_hash = base;
    diff_hash.weight_map_hash = "fnv1a64:2222222222222222";
    assert(firebreak::core::weighted_run_identity(diff_hash) != id);

    // Legacy homogeneous (empty weight fields) cannot collide with a weighted row.
    auto legacy = base;
    legacy.weight_profile.clear();
    legacy.weight_map_hash.clear();
    const std::string legacy_id = firebreak::core::weighted_run_identity(legacy);
    assert(legacy_id != id);

    // Non-weight changes also change the id.
    auto diff_alpha = base;
    diff_alpha.alpha = 0.03;
    assert(firebreak::core::weighted_run_identity(diff_alpha) != id);
}

}  // namespace

int main() {
    test_universe_hash_determinism_and_order_independence();
    test_family_and_pairing();
    test_seed_derivation();
    test_run_identity();
    std::cout << "All canonical landscape identity tests passed.\n";
    return 0;
}
