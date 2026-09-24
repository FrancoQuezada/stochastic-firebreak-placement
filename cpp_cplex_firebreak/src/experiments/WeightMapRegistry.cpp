#include "experiments/WeightMapRegistry.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace firebreak::experiments {

namespace {

constexpr int kGeneratorVersion = 1;

std::string stable_double_string(double value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error("Cannot serialize a nonfinite registry value.");
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
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += ch;
        }
    }
    return out;
}

// Extract the direct (depth-1) scalar members of the top-level JSON object. String
// values are returned unquoted; numbers/booleans are returned verbatim. Nested
// objects/arrays are skipped. Sufficient for reading a registry metadata record whose
// validation-relevant fields are all top-level scalars.
std::unordered_map<std::string, std::string> parse_top_level_scalars(const std::string& text) {
    std::unordered_map<std::string, std::string> out;
    std::size_t i = 0;
    const std::size_t n = text.size();
    int depth = 0;
    while (i < n) {
        const char ch = text[i];
        if (ch == '{' || ch == '[') {
            ++depth;
            ++i;
            continue;
        }
        if (ch == '}' || ch == ']') {
            --depth;
            ++i;
            continue;
        }
        if (ch == '"' && depth == 1) {
            // Parse a key.
            std::string key;
            ++i;
            while (i < n && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < n) {
                    ++i;
                }
                key += text[i];
                ++i;
            }
            ++i;  // closing quote
            while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            if (i >= n || text[i] != ':') {
                continue;
            }
            ++i;  // colon
            while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            if (i >= n) {
                break;
            }
            if (text[i] == '{' || text[i] == '[') {
                // Nested value: skip it without recording (depth handling continues).
                continue;
            }
            std::string value;
            if (text[i] == '"') {
                ++i;
                while (i < n && text[i] != '"') {
                    if (text[i] == '\\' && i + 1 < n) {
                        ++i;
                    }
                    value += text[i];
                    ++i;
                }
                ++i;  // closing quote
            } else {
                while (i < n && text[i] != ',' && text[i] != '}' && text[i] != ']' &&
                       !std::isspace(static_cast<unsigned char>(text[i]))) {
                    value += text[i];
                    ++i;
                }
            }
            out[key] = value;
            continue;
        }
        ++i;
    }
    return out;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Could not open registry file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void atomic_write(const std::filesystem::path& path, const std::string& contents) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Could not open registry temp file: " + tmp.string());
        }
        out << contents;
        out.flush();
        if (!out) {
            throw std::runtime_error("Failed writing registry temp file: " + tmp.string());
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp);
        throw std::runtime_error("Failed atomic rename to " + path.string() + ": " + ec.message());
    }
}

std::string require(const std::unordered_map<std::string, std::string>& fields,
                    const std::string& key,
                    const std::filesystem::path& metadata_path) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        throw std::runtime_error("Registry metadata " + metadata_path.string() +
                                 " is missing required field '" + key + "'.");
    }
    return it->second;
}

int cluster_count_of(const core::LandscapeWeightMap& map) {
    int max_cluster = 0;
    for (const auto& [cell_id, cluster_id] : map.cluster_id_by_original_cell_id) {
        (void)cell_id;
        if (cluster_id > max_cluster) {
            max_cluster = cluster_id;
        }
    }
    return max_cluster;
}

core::LandscapeWeightGenerationConfig resolve_config(const WeightMapRegistryRequest& request,
                                                     std::uint64_t seed) {
    core::LandscapeWeightGenerationConfig config = request.config;
    config.profile = request.weight_profile;
    config.seed = seed;
    return core::validate_landscape_weight_generation_config(config);
}

WeightMapRegistryEntry entry_from_map(const WeightMapRegistryRequest& request,
                                      std::uint64_t seed,
                                      const std::string& relative_path,
                                      const core::LandscapeWeightMap& map,
                                      const core::LandscapeWeightGenerationConfig& config) {
    WeightMapRegistryEntry entry;
    entry.canonical_landscape_id = request.identity.canonical_landscape_id;
    entry.weight_profile = request.weight_profile;
    entry.weight_replicate = request.weight_replicate;
    entry.weight_generation_seed = seed;
    entry.weight_generator_version = kGeneratorVersion;
    entry.weight_map_path = relative_path;
    entry.weight_map_hash = map.deterministic_hash;
    entry.source_universe_hash = request.identity.universe_hash;
    entry.cell_count = static_cast<int>(map.weight_by_original_cell_id.size());
    entry.normalization_mode = map.normalized ? "mean_one" : "raw";
    entry.mean_weight = map.normalized_mean;
    entry.minimum_weight = map.minimum_weight;
    entry.maximum_weight = map.maximum_weight;
    entry.cluster_count = cluster_count_of(map);
    entry.cluster_fraction = config.cluster_fraction;
    entry.cluster_multiplier = config.cluster_max;
    entry.background_multiplier = config.background_max;
    return entry;
}

std::string metadata_json(const WeightMapRegistryEntry& entry,
                          const WeightMapRegistryRequest& request,
                          const core::LandscapeWeightGenerationConfig& config) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\n";
    out << "  \"canonical_landscape_id\": \"" << json_escape(entry.canonical_landscape_id) << "\",\n";
    out << "  \"landscape_family\": \"" << json_escape(request.identity.landscape_family) << "\",\n";
    out << "  \"base_landscape_id\": \"" << json_escape(request.identity.base_landscape_id) << "\",\n";
    out << "  \"paired_landscape_id\": \"" << json_escape(request.identity.paired_landscape_id) << "\",\n";
    out << "  \"grid_rows\": " << request.identity.grid_rows << ",\n";
    out << "  \"grid_cols\": " << request.identity.grid_cols << ",\n";
    out << "  \"weight_profile\": \"" << json_escape(entry.weight_profile) << "\",\n";
    out << "  \"weight_replicate\": " << entry.weight_replicate << ",\n";
    out << "  \"weight_generation_seed\": " << entry.weight_generation_seed << ",\n";
    out << "  \"weight_generator_version\": " << entry.weight_generator_version << ",\n";
    out << "  \"weight_map_path\": \"" << json_escape(entry.weight_map_path) << "\",\n";
    out << "  \"weight_map_hash\": \"" << json_escape(entry.weight_map_hash) << "\",\n";
    out << "  \"source_universe_hash\": \"" << json_escape(entry.source_universe_hash) << "\",\n";
    out << "  \"cell_count\": " << entry.cell_count << ",\n";
    out << "  \"normalization_mode\": \"" << json_escape(entry.normalization_mode) << "\",\n";
    out << "  \"mean_weight\": " << stable_double_string(entry.mean_weight) << ",\n";
    out << "  \"minimum_weight\": " << stable_double_string(entry.minimum_weight) << ",\n";
    out << "  \"maximum_weight\": " << stable_double_string(entry.maximum_weight) << ",\n";
    out << "  \"cluster_count\": " << entry.cluster_count << ",\n";
    out << "  \"cluster_fraction\": " << stable_double_string(entry.cluster_fraction) << ",\n";
    out << "  \"cluster_multiplier\": " << stable_double_string(entry.cluster_multiplier) << ",\n";
    out << "  \"background_multiplier\": " << stable_double_string(entry.background_multiplier) << ",\n";
    out << "  \"generation_parameters\": {\n";
    out << "    \"profile\": \"" << json_escape(config.profile) << "\",\n";
    out << "    \"seed\": " << config.seed << ",\n";
    out << "    \"normalize\": " << (config.normalize ? "true" : "false") << ",\n";
    out << "    \"heterogeneous_min\": " << stable_double_string(config.heterogeneous_min) << ",\n";
    out << "    \"heterogeneous_max\": " << stable_double_string(config.heterogeneous_max) << ",\n";
    out << "    \"cluster_count\": " << config.cluster_count << ",\n";
    out << "    \"cluster_fraction\": " << stable_double_string(config.cluster_fraction) << ",\n";
    out << "    \"background_min\": " << stable_double_string(config.background_min) << ",\n";
    out << "    \"background_max\": " << stable_double_string(config.background_max) << ",\n";
    out << "    \"cluster_min\": " << stable_double_string(config.cluster_min) << ",\n";
    out << "    \"cluster_max\": " << stable_double_string(config.cluster_max) << ",\n";
    out << "    \"cluster_min_separation\": " << config.cluster_min_separation << "\n";
    out << "  },\n";
    out << "  \"universe_source\": \"" << json_escape(request.universe.source) << "\"\n";
    out << "}\n";
    return out.str();
}

}  // namespace

std::filesystem::path WeightMapRegistry::entry_relative_dir(
    const std::string& canonical_landscape_id,
    const std::string& weight_profile,
    int weight_replicate) {
    if (canonical_landscape_id.empty()) {
        throw std::runtime_error("canonical_landscape_id cannot be empty.");
    }
    if (weight_profile.empty()) {
        throw std::runtime_error("weight_profile cannot be empty.");
    }
    if (weight_replicate < 0) {
        throw std::runtime_error("weight_replicate must be nonnegative.");
    }
    return std::filesystem::path(canonical_landscape_id) / weight_profile /
           ("replicate_" + std::to_string(weight_replicate));
}

WeightMapRegistryEntry WeightMapRegistry::ensure(const WeightMapRegistryRequest& request) const {
    if (request.registry_root.empty()) {
        throw std::runtime_error("Registry root cannot be empty.");
    }
    const std::string profile =
        core::normalize_landscape_weight_profile_name(request.weight_profile);
    WeightMapRegistryRequest normalized = request;
    normalized.weight_profile = profile;

    const std::uint64_t seed = core::derive_weight_generation_seed(
        request.global_weight_seed,
        request.identity.canonical_landscape_id,
        profile,
        request.weight_replicate,
        kGeneratorVersion);

    const auto rel_dir = entry_relative_dir(
        request.identity.canonical_landscape_id, profile, request.weight_replicate);
    const auto abs_dir = request.registry_root / rel_dir;
    const auto csv_path = abs_dir / "weights.csv";
    const auto metadata_path = abs_dir / "metadata.json";
    const std::string rel_csv = (rel_dir / "weights.csv").generic_string();

    const bool csv_exists = std::filesystem::exists(csv_path);
    const bool meta_exists = std::filesystem::exists(metadata_path);

    if (csv_exists != meta_exists) {
        throw std::runtime_error(
            "Incomplete weight-map registry entry at " + abs_dir.string() +
            " (exactly one of weights.csv / metadata.json is present). Remove it and "
            "regenerate.");
    }

    const auto config = resolve_config(normalized, seed);

    if (csv_exists && meta_exists) {
        // Validate: the existing content must match a deterministic regeneration of the
        // requested parameters. Any mismatch is a hard error (no silent overwrite).
        const auto loaded = core::load_landscape_weight_map_csv(
            csv_path, {}, profile);
        core::LandscapeWeightMap regenerated =
            core::generate_landscape_weight_map(request.universe, config);
        if (regenerated.deterministic_hash != loaded.deterministic_hash) {
            throw std::runtime_error(
                "Weight-map registry entry " + abs_dir.string() +
                " does not match the requested generation parameters (stored hash " +
                loaded.deterministic_hash + " != requested hash " +
                regenerated.deterministic_hash + "). Refusing to overwrite.");
        }
        const auto fields = parse_top_level_scalars(read_file(metadata_path));
        const auto stored_hash = require(fields, "weight_map_hash", metadata_path);
        if (stored_hash != loaded.deterministic_hash) {
            throw std::runtime_error(
                "Weight-map registry metadata hash (" + stored_hash +
                ") disagrees with the stored CSV hash (" + loaded.deterministic_hash +
                ") at " + abs_dir.string() + ".");
        }
        const auto stored_universe = require(fields, "source_universe_hash", metadata_path);
        if (stored_universe != request.identity.universe_hash) {
            throw std::runtime_error(
                "Weight-map registry entry " + abs_dir.string() +
                " was generated over a different physical universe (" + stored_universe +
                " != " + request.identity.universe_hash + ").");
        }
        return entry_from_map(normalized, seed, rel_csv, loaded, config);
    }

    if (!request.allow_generate) {
        throw std::runtime_error(
            "Weight map is missing at " + abs_dir.string() +
            " and generation is not allowed (worker contract). Pre-generate it first.");
    }

    core::LandscapeWeightMap map =
        core::generate_landscape_weight_map(request.universe, config);
    const auto entry = entry_from_map(normalized, seed, rel_csv, map, config);
    core::write_landscape_weight_map_csv(map, csv_path);
    atomic_write(metadata_path, metadata_json(entry, normalized, config));
    return entry;
}

WeightMapRegistryEntry WeightMapRegistry::load(
    const std::filesystem::path& registry_root,
    const std::string& canonical_landscape_id,
    const std::string& weight_profile,
    int weight_replicate,
    int weight_generator_version) const {
    const std::string profile =
        core::normalize_landscape_weight_profile_name(weight_profile);
    const auto rel_dir =
        entry_relative_dir(canonical_landscape_id, profile, weight_replicate);
    const auto abs_dir = registry_root / rel_dir;
    const auto csv_path = abs_dir / "weights.csv";
    const auto metadata_path = abs_dir / "metadata.json";

    if (!std::filesystem::exists(csv_path) || !std::filesystem::exists(metadata_path)) {
        throw std::runtime_error(
            "Weight-map registry entry not found at " + abs_dir.string() +
            " (workers never regenerate maps).");
    }

    const auto loaded = core::load_landscape_weight_map_csv(csv_path, {}, profile);
    const auto fields = parse_top_level_scalars(read_file(metadata_path));

    const auto stored_hash = require(fields, "weight_map_hash", metadata_path);
    if (stored_hash != loaded.deterministic_hash) {
        throw std::runtime_error(
            "Weight-map registry CSV hash (" + loaded.deterministic_hash +
            ") disagrees with the stored metadata hash (" + stored_hash + ") at " +
            abs_dir.string() + ".");
    }
    const int stored_version = std::stoi(require(fields, "weight_generator_version", metadata_path));
    if (stored_version != weight_generator_version) {
        throw std::runtime_error(
            "Weight-map registry generator version mismatch at " + abs_dir.string() + ".");
    }

    WeightMapRegistryEntry entry;
    entry.canonical_landscape_id = require(fields, "canonical_landscape_id", metadata_path);
    entry.weight_profile = require(fields, "weight_profile", metadata_path);
    entry.weight_replicate = std::stoi(require(fields, "weight_replicate", metadata_path));
    entry.weight_generation_seed = static_cast<std::uint64_t>(
        std::stoull(require(fields, "weight_generation_seed", metadata_path)));
    entry.weight_generator_version = stored_version;
    entry.weight_map_path = require(fields, "weight_map_path", metadata_path);
    entry.weight_map_hash = stored_hash;
    entry.source_universe_hash = require(fields, "source_universe_hash", metadata_path);
    entry.cell_count = std::stoi(require(fields, "cell_count", metadata_path));
    entry.normalization_mode = require(fields, "normalization_mode", metadata_path);
    entry.mean_weight = std::stod(require(fields, "mean_weight", metadata_path));
    entry.minimum_weight = std::stod(require(fields, "minimum_weight", metadata_path));
    entry.maximum_weight = std::stod(require(fields, "maximum_weight", metadata_path));
    entry.cluster_count = std::stoi(require(fields, "cluster_count", metadata_path));
    entry.cluster_fraction = std::stod(require(fields, "cluster_fraction", metadata_path));
    entry.cluster_multiplier = std::stod(require(fields, "cluster_multiplier", metadata_path));
    entry.background_multiplier = std::stod(require(fields, "background_multiplier", metadata_path));
    return entry;
}

core::LandscapeWeightMap WeightMapRegistry::load_weight_map(
    const std::filesystem::path& registry_root,
    const WeightMapRegistryEntry& entry) const {
    const auto csv_path = registry_root / entry.weight_map_path;
    auto map = core::load_landscape_weight_map_csv(csv_path, {}, entry.weight_profile);
    if (map.deterministic_hash != entry.weight_map_hash) {
        throw std::runtime_error(
            "Weight-map CSV hash mismatch for registry entry " + entry.weight_map_path +
            " (" + map.deterministic_hash + " != " + entry.weight_map_hash + ").");
    }
    return map;
}

}  // namespace firebreak::experiments
