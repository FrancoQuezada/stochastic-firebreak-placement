#include "core/CanonicalLandscape.hpp"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace firebreak::core {

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr char kReburnSuffix[] = "_reburn";

void fnv_append(std::uint64_t& hash, const std::string& text) {
    for (const unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kFnvPrime;
    }
    // Field separator that cannot appear in the appended tokens, so distinct field
    // decompositions can never alias to the same digest.
    hash ^= static_cast<std::uint64_t>('\x1f');
    hash *= kFnvPrime;
}

std::string fnv_to_hex(std::uint64_t hash) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

std::string hex_suffix(const std::string& fnv_hash) {
    const auto colon = fnv_hash.find(':');
    if (colon == std::string::npos || colon + 1 >= fnv_hash.size()) {
        return fnv_hash;
    }
    return fnv_hash.substr(colon + 1);
}

}  // namespace

bool is_reburn_instance(const std::string& instance_id) {
    const std::string suffix = kReburnSuffix;
    if (instance_id.size() <= suffix.size()) {
        return false;
    }
    return instance_id.compare(
               instance_id.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string landscape_family_from_instance(const std::string& instance_id) {
    if (instance_id.empty()) {
        throw std::runtime_error("Instance id cannot be empty.");
    }
    if (is_reburn_instance(instance_id)) {
        return instance_id.substr(0, instance_id.size() - std::string(kReburnSuffix).size());
    }
    return instance_id;
}

std::string landscape_universe_hash(const LandscapeCellUniverse& universe) {
    validate_landscape_cell_universe(universe);
    std::vector<WeightedLandscapeCell> cells = universe.cells;
    std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) {
        return a.original_cell_id < b.original_cell_id;
    });

    std::uint64_t hash = kFnvOffsetBasis;
    fnv_append(hash, std::to_string(universe.rows));
    fnv_append(hash, std::to_string(universe.cols));
    fnv_append(hash, std::to_string(cells.size()));
    for (const auto& cell : cells) {
        fnv_append(hash, std::to_string(cell.original_cell_id));
        fnv_append(hash, std::to_string(cell.row));
        fnv_append(hash, std::to_string(cell.column));
    }
    return fnv_to_hex(hash);
}

CanonicalLandscapeIdentity make_canonical_landscape_identity(
    const std::string& instance_id,
    const LandscapeCellUniverse& universe) {
    validate_landscape_cell_universe(universe);

    CanonicalLandscapeIdentity identity;
    identity.landscape_family = landscape_family_from_instance(instance_id);
    identity.base_landscape_id = identity.landscape_family;
    identity.grid_rows = universe.rows;
    identity.grid_cols = universe.cols;
    identity.cell_count = static_cast<int>(universe.cells.size());
    identity.universe_hash = landscape_universe_hash(universe);

    std::ostringstream id;
    id.imbue(std::locale::classic());
    id << identity.landscape_family << "__" << universe.rows << "x" << universe.cols
       << "__" << hex_suffix(identity.universe_hash);
    identity.canonical_landscape_id = id.str();

    identity.reduced_instance_id = identity.landscape_family;
    identity.reburn_instance_id = identity.landscape_family + kReburnSuffix;
    identity.paired_landscape_id =
        is_reburn_instance(instance_id) ? identity.reduced_instance_id
                                        : identity.reburn_instance_id;
    return identity;
}

std::uint64_t derive_weight_generation_seed(
    std::uint64_t global_weight_seed,
    const std::string& canonical_landscape_id,
    const std::string& weight_profile,
    int weight_replicate,
    int weight_generator_version) {
    if (weight_replicate < 0) {
        throw std::runtime_error("weight_replicate must be nonnegative.");
    }
    if (weight_generator_version <= 0) {
        throw std::runtime_error("weight_generator_version must be positive.");
    }
    std::uint64_t hash = kFnvOffsetBasis;
    fnv_append(hash, "weight-generation-seed-v1");
    fnv_append(hash, std::to_string(global_weight_seed));
    fnv_append(hash, canonical_landscape_id);
    fnv_append(hash, weight_profile);
    fnv_append(hash, std::to_string(weight_replicate));
    fnv_append(hash, std::to_string(weight_generator_version));
    return hash;
}

std::string weighted_run_identity(const RunIdentityInputs& inputs) {
    std::uint64_t train_hash = kFnvOffsetBasis;
    std::vector<int> sorted_train = inputs.train_ids;
    std::sort(sorted_train.begin(), sorted_train.end());
    for (const int id : sorted_train) {
        fnv_append(train_hash, std::to_string(id));
    }

    std::ostringstream alpha_stream;
    alpha_stream.imbue(std::locale::classic());
    alpha_stream << std::scientific << std::setprecision(17) << inputs.alpha;
    std::ostringstream beta_stream;
    beta_stream.imbue(std::locale::classic());
    beta_stream << std::scientific << std::setprecision(17) << inputs.cvar_beta;
    std::ostringstream lambda_stream;
    lambda_stream.imbue(std::locale::classic());
    lambda_stream << std::scientific << std::setprecision(17) << inputs.cvar_lambda;

    std::uint64_t hash = kFnvOffsetBasis;
    fnv_append(hash, "run-identity-v1");
    fnv_append(hash, inputs.canonical_landscape_id);
    fnv_append(hash, inputs.instance_id);
    fnv_append(hash, inputs.method);
    fnv_append(hash, inputs.objective);
    fnv_append(hash, beta_stream.str());
    fnv_append(hash, lambda_stream.str());
    fnv_append(hash, std::to_string(inputs.scenario_count));
    fnv_append(hash, alpha_stream.str());
    fnv_append(hash, std::to_string(inputs.budget));
    fnv_append(hash, fnv_to_hex(train_hash));
    fnv_append(hash, inputs.weight_profile);
    fnv_append(hash, std::to_string(inputs.weight_replicate));
    fnv_append(hash, inputs.weight_map_hash);
    return fnv_to_hex(hash);
}

}  // namespace firebreak::core
