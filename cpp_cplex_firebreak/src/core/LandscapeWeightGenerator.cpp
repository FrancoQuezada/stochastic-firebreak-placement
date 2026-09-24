#include "core/LandscapeWeightGenerator.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <queue>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace firebreak::core {

namespace {

constexpr int kGeneratorVersion = 1;
constexpr int kClusterMaxAttempts = 100;
constexpr std::uint64_t kClusterWeightSeedSalt = 0x9e3779b97f4a7c15ull;
constexpr double kMeanTolerance = 1.0e-10;

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string stable_double_string(double value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error("Cannot write nonfinite landscape weight metadata.");
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::scientific << std::setprecision(17) << value;
    return out.str();
}

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += ch;
        }
    }
    return out;
}

std::vector<WeightedLandscapeCell> sorted_cells(const LandscapeCellUniverse& universe) {
    std::vector<WeightedLandscapeCell> cells = universe.cells;
    std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) {
        return a.original_cell_id < b.original_cell_id;
    });
    return cells;
}

std::vector<int> sorted_cell_ids(const LandscapeCellUniverse& universe) {
    const auto cells = sorted_cells(universe);
    std::vector<int> ids;
    ids.reserve(cells.size());
    for (const auto& cell : cells) {
        ids.push_back(cell.original_cell_id);
    }
    return ids;
}

void validate_positive_finite(double value, const std::string& name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::runtime_error(name + " must be finite and strictly positive.");
    }
}

double draw_uniform(std::mt19937_64& rng, double min_value, double max_value) {
    if (min_value == max_value) {
        return min_value;
    }
    std::uniform_real_distribution<double> distribution(min_value, max_value);
    return distribution(rng);
}

int chebyshev_distance(const WeightedLandscapeCell& a, const WeightedLandscapeCell& b) {
    return std::max(std::abs(a.row - b.row), std::abs(a.column - b.column));
}

std::uint64_t attempt_seed(std::uint64_t seed, int attempt) {
    return seed + kClusterWeightSeedSalt * static_cast<std::uint64_t>(attempt + 1);
}

std::vector<int> deterministic_cluster_sizes(int clustered_cell_count, int cluster_count) {
    if (cluster_count <= 0) {
        throw std::runtime_error("weight_cluster_count must be positive.");
    }
    if (clustered_cell_count < cluster_count) {
        throw std::runtime_error(
            "Requested clustered-cell count must be at least weight_cluster_count.");
    }
    std::vector<int> sizes(static_cast<std::size_t>(cluster_count), clustered_cell_count / cluster_count);
    const int remainder = clustered_cell_count % cluster_count;
    for (int i = 0; i < remainder; ++i) {
        ++sizes[static_cast<std::size_t>(i)];
    }
    return sizes;
}

struct ClusterAssignment {
    std::unordered_map<int, int> cluster_by_cell_id;
    std::vector<int> seed_ids;
    std::vector<int> cluster_sizes;
};

std::vector<int> neighbor_ids(
    const WeightedLandscapeCell& cell,
    const std::unordered_map<long long, int>& id_by_coordinate) {
    std::vector<int> ids;
    ids.reserve(8);
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) {
                continue;
            }
            const long long key =
                (static_cast<long long>(cell.row + dr) << 32) ^
                static_cast<unsigned int>(cell.column + dc);
            const auto it = id_by_coordinate.find(key);
            if (it != id_by_coordinate.end()) {
                ids.push_back(it->second);
            }
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<WeightedLandscapeCell> select_cluster_seeds(
    const std::vector<WeightedLandscapeCell>& cells,
    int cluster_count,
    int min_separation,
    std::mt19937_64& rng) {
    std::vector<WeightedLandscapeCell> candidates = cells;
    std::shuffle(candidates.begin(), candidates.end(), rng);

    std::vector<WeightedLandscapeCell> seeds;
    seeds.reserve(static_cast<std::size_t>(cluster_count));
    for (const auto& candidate : candidates) {
        bool feasible = true;
        for (const auto& seed : seeds) {
            if (chebyshev_distance(candidate, seed) < min_separation) {
                feasible = false;
                break;
            }
        }
        if (!feasible) {
            continue;
        }
        seeds.push_back(candidate);
        if (static_cast<int>(seeds.size()) == cluster_count) {
            return seeds;
        }
    }

    throw std::runtime_error(
        "Could not select the requested number of cluster seeds with the requested separation.");
}

ClusterAssignment try_generate_cluster_assignment(
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationConfig& config,
    const std::vector<int>& target_sizes,
    std::mt19937_64& rng) {
    const auto cells = sorted_cells(universe);
    std::unordered_map<int, WeightedLandscapeCell> cell_by_id;
    std::unordered_map<long long, int> id_by_coordinate;
    cell_by_id.reserve(cells.size());
    id_by_coordinate.reserve(cells.size());
    for (const auto& cell : cells) {
        cell_by_id[cell.original_cell_id] = cell;
        const long long key =
            (static_cast<long long>(cell.row) << 32) ^
            static_cast<unsigned int>(cell.column);
        id_by_coordinate[key] = cell.original_cell_id;
    }

    const auto seeds = select_cluster_seeds(
        cells,
        config.cluster_count,
        config.cluster_min_separation,
        rng);

    ClusterAssignment assignment;
    assignment.cluster_sizes = target_sizes;
    assignment.seed_ids.reserve(seeds.size());
    for (int cluster_index = 0; cluster_index < config.cluster_count; ++cluster_index) {
        const int cluster_id = cluster_index + 1;
        const int seed_id = seeds[static_cast<std::size_t>(cluster_index)].original_cell_id;
        assignment.seed_ids.push_back(seed_id);
        assignment.cluster_by_cell_id[seed_id] = cluster_id;
    }

    std::vector<std::vector<int>> cells_by_cluster(static_cast<std::size_t>(config.cluster_count));
    for (int cluster_index = 0; cluster_index < config.cluster_count; ++cluster_index) {
        cells_by_cluster[static_cast<std::size_t>(cluster_index)].push_back(
            assignment.seed_ids[static_cast<std::size_t>(cluster_index)]);
    }

    int assigned_count = config.cluster_count;
    const int target_total = std::accumulate(target_sizes.begin(), target_sizes.end(), 0);
    while (assigned_count < target_total) {
        bool progressed = false;
        for (int cluster_index = 0; cluster_index < config.cluster_count; ++cluster_index) {
            auto& cluster_cells = cells_by_cluster[static_cast<std::size_t>(cluster_index)];
            const int target_size = target_sizes[static_cast<std::size_t>(cluster_index)];
            if (static_cast<int>(cluster_cells.size()) >= target_size) {
                continue;
            }

            std::vector<int> candidates;
            for (const int cell_id : cluster_cells) {
                const auto cell_it = cell_by_id.find(cell_id);
                if (cell_it == cell_by_id.end()) {
                    throw std::runtime_error("Internal cluster assignment lost a landscape cell.");
                }
                for (const int neighbor_id : neighbor_ids(cell_it->second, id_by_coordinate)) {
                    if (assignment.cluster_by_cell_id.find(neighbor_id) ==
                        assignment.cluster_by_cell_id.end()) {
                        candidates.push_back(neighbor_id);
                    }
                }
            }
            std::sort(candidates.begin(), candidates.end());
            candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
            if (candidates.empty()) {
                continue;
            }
            std::shuffle(candidates.begin(), candidates.end(), rng);
            const int chosen_id = candidates.front();
            const int cluster_id = cluster_index + 1;
            assignment.cluster_by_cell_id[chosen_id] = cluster_id;
            cluster_cells.push_back(chosen_id);
            ++assigned_count;
            progressed = true;
        }
        if (!progressed) {
            throw std::runtime_error("Cluster growth became impossible before reaching exact target sizes.");
        }
    }

    return assignment;
}

ClusterAssignment generate_cluster_assignment(
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationConfig& config,
    const std::vector<int>& target_sizes) {
    std::string last_error;
    for (int attempt = 0; attempt < kClusterMaxAttempts; ++attempt) {
        try {
            std::mt19937_64 rng(attempt_seed(config.seed, attempt));
            return try_generate_cluster_assignment(universe, config, target_sizes, rng);
        } catch (const std::runtime_error& exc) {
            last_error = exc.what();
        }
    }
    throw std::runtime_error(
        "Could not generate exact connected clusters after " +
        std::to_string(kClusterMaxAttempts) + " deterministic attempts. Last error: " +
        last_error);
}

int rounded_clustered_cell_count(double fraction, int cell_count, int cluster_count) {
    const double requested = fraction * static_cast<double>(cell_count);
    int count = static_cast<int>(std::llround(requested));
    if (count == 0 && fraction > 0.0) {
        count = 1;
    }
    if (count < cluster_count) {
        throw std::runtime_error(
            "Requested cluster fraction produces fewer clustered cells than cluster count.");
    }
    if (count > cell_count) {
        throw std::runtime_error("Requested clustered-cell count exceeds the generation universe.");
    }
    return count;
}

void fill_metadata(
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationMetadata& generated,
    LandscapeWeightGenerationMetadata* out) {
    if (out == nullptr) {
        return;
    }
    *out = generated;
    out->generation_universe_source = universe.source;
    out->cell_count = static_cast<int>(universe.cells.size());
    out->generator_version = kGeneratorVersion;
}

}  // namespace

std::string normalize_landscape_weight_profile_name(const std::string& profile) {
    const std::string normalized = lower_copy(profile);
    if (normalized == "homogeneous" ||
        normalized == "heterogeneous" ||
        normalized == "clustered") {
        return normalized;
    }
    throw std::runtime_error(
        "Unknown landscape weight profile '" + profile +
        "'. Expected homogeneous, heterogeneous, or clustered.");
}

void validate_landscape_cell_universe(const LandscapeCellUniverse& universe) {
    if (universe.cells.empty()) {
        throw std::runtime_error("Landscape weight generation universe cannot be empty.");
    }
    std::vector<int> ids;
    ids.reserve(universe.cells.size());
    for (const auto& cell : universe.cells) {
        if (cell.original_cell_id <= 0) {
            throw std::runtime_error("Landscape cell universe contains a nonpositive original cell ID.");
        }
        if (cell.row <= 0 || cell.column <= 0) {
            throw std::runtime_error("Landscape cell universe coordinates must be one-based positive row/column values.");
        }
        if (universe.rows > 0 && cell.row > universe.rows) {
            throw std::runtime_error("Landscape cell universe row exceeds declared row count.");
        }
        if (universe.cols > 0 && cell.column > universe.cols) {
            throw std::runtime_error("Landscape cell universe column exceeds declared column count.");
        }
        ids.push_back(cell.original_cell_id);
    }
    std::sort(ids.begin(), ids.end());
    for (std::size_t i = 1; i < ids.size(); ++i) {
        if (ids[i] == ids[i - 1]) {
            throw std::runtime_error(
                "Landscape cell universe contains duplicate original cell ID " +
                std::to_string(ids[i]) + ".");
        }
    }
}

LandscapeWeightGenerationConfig validate_landscape_weight_generation_config(
    const LandscapeWeightGenerationConfig& config) {
    LandscapeWeightGenerationConfig validated = config;
    validated.profile = normalize_landscape_weight_profile_name(config.profile);
    if (validated.profile == "heterogeneous") {
        validate_positive_finite(validated.heterogeneous_min, "weight_min");
        validate_positive_finite(validated.heterogeneous_max, "weight_max");
        if (validated.heterogeneous_min > validated.heterogeneous_max) {
            throw std::runtime_error("weight_min must be less than or equal to weight_max.");
        }
    }
    if (validated.profile == "clustered") {
        if (validated.cluster_count <= 0) {
            throw std::runtime_error("weight_cluster_count must be positive.");
        }
        if (!std::isfinite(validated.cluster_fraction) ||
            validated.cluster_fraction <= 0.0 ||
            validated.cluster_fraction > 1.0) {
            throw std::runtime_error("weight_cluster_fraction must be finite and in (0, 1].");
        }
        if (validated.cluster_min_separation < 0) {
            throw std::runtime_error("weight_cluster_min_separation must be nonnegative.");
        }
        validate_positive_finite(validated.background_min, "weight_background_min");
        validate_positive_finite(validated.background_max, "weight_background_max");
        validate_positive_finite(validated.cluster_min, "weight_cluster_min");
        validate_positive_finite(validated.cluster_max, "weight_cluster_max");
        if (!(validated.background_min <= validated.background_max &&
              validated.background_max < validated.cluster_min &&
              validated.cluster_min <= validated.cluster_max)) {
            throw std::runtime_error(
                "Clustered weight ranges must satisfy 0 < background_min <= background_max < "
                "cluster_min <= cluster_max.");
        }
    }
    return validated;
}

LandscapeWeightMap normalize_landscape_weight_records(
    const std::string& profile,
    std::uint64_t seed,
    bool normalize,
    std::vector<LandscapeWeightRecord> records,
    double* normalization_factor) {
    if (records.empty()) {
        throw std::runtime_error("Cannot normalize an empty landscape weight map.");
    }
    double raw_total = 0.0;
    for (const auto& record : records) {
        if (!std::isfinite(record.raw_weight) || record.raw_weight <= 0.0) {
            throw std::runtime_error("Raw landscape weights must be finite and strictly positive.");
        }
        raw_total += record.raw_weight;
    }
    const double raw_mean = raw_total / static_cast<double>(records.size());
    if (!std::isfinite(raw_mean) || raw_mean <= kMeanTolerance) {
        throw std::runtime_error("Raw landscape weight mean is invalid or numerically zero.");
    }

    const double factor = normalize ? raw_mean : 1.0;
    for (auto& record : records) {
        record.weight = record.raw_weight / factor;
        if (!std::isfinite(record.weight) || record.weight <= 0.0) {
            throw std::runtime_error("Final landscape weights must be finite and strictly positive.");
        }
    }

    auto map = make_landscape_weight_map(profile, seed, normalize, records);
    map.normalization_factor = factor;
    recompute_landscape_weight_map_statistics_and_hash(map);
    validate_landscape_weight_map(map);
    if (normalize && std::fabs(map.normalized_mean - 1.0) > 1.0e-12) {
        throw std::runtime_error("Normalized landscape weights do not have arithmetic mean one.");
    }
    if (normalization_factor != nullptr) {
        *normalization_factor = factor;
    }
    return map;
}

LandscapeWeightMap generate_landscape_weight_map(
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationConfig& config,
    LandscapeWeightGenerationMetadata* metadata) {
    const auto validated = validate_landscape_weight_generation_config(config);
    if (validated.profile == "homogeneous") {
        return generate_homogeneous_weight_map(universe, validated, metadata);
    }
    if (validated.profile == "heterogeneous") {
        return generate_heterogeneous_weight_map(universe, validated, metadata);
    }
    if (validated.profile == "clustered") {
        return generate_clustered_weight_map(universe, validated, metadata);
    }
    throw std::runtime_error("Unsupported landscape weight profile.");
}

LandscapeWeightMap generate_homogeneous_weight_map(
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationConfig& config,
    LandscapeWeightGenerationMetadata* metadata) {
    const auto validated = validate_landscape_weight_generation_config(config);
    if (validated.profile != "homogeneous") {
        throw std::runtime_error("generate_homogeneous_weight_map requires profile homogeneous.");
    }
    validate_landscape_cell_universe(universe);
    auto map = make_homogeneous_weight_map(sorted_cell_ids(universe));
    map.seed = validated.seed;
    map.normalized = validated.normalize;
    map.normalization_factor = 1.0;
    recompute_landscape_weight_map_statistics_and_hash(map);

    LandscapeWeightGenerationMetadata generated;
    generated.clustered_cell_count = 0;
    generated.normalization_factor = 1.0;
    fill_metadata(universe, generated, metadata);
    return map;
}

LandscapeWeightMap generate_heterogeneous_weight_map(
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationConfig& config,
    LandscapeWeightGenerationMetadata* metadata) {
    const auto validated = validate_landscape_weight_generation_config(config);
    if (validated.profile != "heterogeneous") {
        throw std::runtime_error("generate_heterogeneous_weight_map requires profile heterogeneous.");
    }
    validate_landscape_cell_universe(universe);

    std::mt19937_64 rng(validated.seed);
    std::vector<LandscapeWeightRecord> records;
    const auto cells = sorted_cells(universe);
    records.reserve(cells.size());
    for (const auto& cell : cells) {
        const double raw = draw_uniform(rng, validated.heterogeneous_min, validated.heterogeneous_max);
        records.push_back(LandscapeWeightRecord{cell.original_cell_id, raw, raw, 0});
    }

    double factor = 1.0;
    auto map = normalize_landscape_weight_records(
        "heterogeneous",
        validated.seed,
        validated.normalize,
        std::move(records),
        &factor);

    LandscapeWeightGenerationMetadata generated;
    generated.clustered_cell_count = 0;
    generated.normalization_factor = factor;
    fill_metadata(universe, generated, metadata);
    return map;
}

LandscapeWeightMap generate_clustered_weight_map(
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationConfig& config,
    LandscapeWeightGenerationMetadata* metadata) {
    const auto validated = validate_landscape_weight_generation_config(config);
    if (validated.profile != "clustered") {
        throw std::runtime_error("generate_clustered_weight_map requires profile clustered.");
    }
    validate_landscape_cell_universe(universe);
    if (universe.rows <= 0 || universe.cols <= 0) {
        throw std::runtime_error("Clustered weight generation requires known landscape rows and columns.");
    }

    const int cell_count = static_cast<int>(universe.cells.size());
    const int clustered_cell_count = rounded_clustered_cell_count(
        validated.cluster_fraction,
        cell_count,
        validated.cluster_count);
    const auto target_sizes = deterministic_cluster_sizes(
        clustered_cell_count,
        validated.cluster_count);
    const auto assignment = generate_cluster_assignment(universe, validated, target_sizes);

    std::mt19937_64 rng(validated.seed ^ kClusterWeightSeedSalt);
    std::vector<LandscapeWeightRecord> records;
    const auto cells = sorted_cells(universe);
    records.reserve(cells.size());
    for (const auto& cell : cells) {
        const auto cluster_it = assignment.cluster_by_cell_id.find(cell.original_cell_id);
        const int cluster_id = cluster_it == assignment.cluster_by_cell_id.end()
            ? 0
            : cluster_it->second;
        const double raw = cluster_id == 0
            ? draw_uniform(rng, validated.background_min, validated.background_max)
            : draw_uniform(rng, validated.cluster_min, validated.cluster_max);
        records.push_back(LandscapeWeightRecord{cell.original_cell_id, raw, raw, cluster_id});
    }

    double factor = 1.0;
    auto map = normalize_landscape_weight_records(
        "clustered",
        validated.seed,
        validated.normalize,
        std::move(records),
        &factor);

    LandscapeWeightGenerationMetadata generated;
    generated.clustered_cell_count = clustered_cell_count;
    generated.cluster_seed_ids = assignment.seed_ids;
    generated.cluster_sizes = target_sizes;
    generated.normalization_factor = factor;
    fill_metadata(universe, generated, metadata);
    return map;
}

void write_landscape_weight_generation_metadata_json(
    const std::filesystem::path& output_path,
    const std::string& landscape,
    const LandscapeCellUniverse& universe,
    const LandscapeWeightGenerationConfig& config,
    const LandscapeWeightMap& weight_map,
    const LandscapeWeightGenerationMetadata& metadata) {
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error(
            "Could not open landscape weight metadata JSON for writing: " +
            output_path.string());
    }
    out.imbue(std::locale::classic());
    out << "{\n";
    out << "  \"landscape\": \"" << json_escape(landscape) << "\",\n";
    out << "  \"profile\": \"" << json_escape(weight_map.profile) << "\",\n";
    out << "  \"seed\": " << weight_map.seed << ",\n";
    out << "  \"normalize\": " << (weight_map.normalized ? "true" : "false") << ",\n";
    out << "  \"generation_universe_source\": \""
        << json_escape(metadata.generation_universe_source) << "\",\n";
    out << "  \"cell_count\": " << metadata.cell_count << ",\n";
    out << "  \"rows\": " << universe.rows << ",\n";
    out << "  \"cols\": " << universe.cols << ",\n";
    out << "  \"heterogeneous_min\": " << stable_double_string(config.heterogeneous_min) << ",\n";
    out << "  \"heterogeneous_max\": " << stable_double_string(config.heterogeneous_max) << ",\n";
    out << "  \"cluster_count\": " << config.cluster_count << ",\n";
    out << "  \"cluster_fraction\": " << stable_double_string(config.cluster_fraction) << ",\n";
    out << "  \"clustered_cell_count\": " << metadata.clustered_cell_count << ",\n";
    out << "  \"cluster_seed_ids\": [";
    for (std::size_t i = 0; i < metadata.cluster_seed_ids.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << metadata.cluster_seed_ids[i];
    }
    out << "],\n";
    out << "  \"cluster_sizes\": [";
    for (std::size_t i = 0; i < metadata.cluster_sizes.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << metadata.cluster_sizes[i];
    }
    out << "],\n";
    out << "  \"cluster_min_separation\": " << config.cluster_min_separation << ",\n";
    out << "  \"background_min\": " << stable_double_string(config.background_min) << ",\n";
    out << "  \"background_max\": " << stable_double_string(config.background_max) << ",\n";
    out << "  \"cluster_min\": " << stable_double_string(config.cluster_min) << ",\n";
    out << "  \"cluster_max\": " << stable_double_string(config.cluster_max) << ",\n";
    out << "  \"raw_mean\": " << stable_double_string(weight_map.raw_mean) << ",\n";
    out << "  \"final_mean\": " << stable_double_string(weight_map.normalized_mean) << ",\n";
    out << "  \"minimum_weight\": " << stable_double_string(weight_map.minimum_weight) << ",\n";
    out << "  \"maximum_weight\": " << stable_double_string(weight_map.maximum_weight) << ",\n";
    out << "  \"total_weight\": " << stable_double_string(weight_map.total_weight) << ",\n";
    out << "  \"normalization_factor\": " << stable_double_string(metadata.normalization_factor) << ",\n";
    out << "  \"weight_map_hash\": \""
        << json_escape(weight_map.deterministic_hash) << "\",\n";
    out << "  \"generator_version\": " << metadata.generator_version << "\n";
    out << "}\n";
}

}  // namespace firebreak::core
