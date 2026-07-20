#include "benders/FppProjectedLlbi.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "benders/FppStrengthening.hpp"

namespace firebreak::benders {

namespace {

constexpr double kZeroTolerance = 1.0e-12;

std::vector<double> compact_y_values(
    const opt::OptimizationInstance& opt,
    const std::vector<double>& y_values_by_eligible_position) {
    if (y_values_by_eligible_position.size() != opt.eligible_indices.size()) {
        throw std::runtime_error(
            "Projected LLBI separation received the wrong number of y values.");
    }
    std::vector<double> values(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        values[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            y_values_by_eligible_position[pos];
    }
    return values;
}

void add_negative_coeff(
    std::unordered_map<int, double>& coefficients,
    int compact_node,
    double amount) {
    if (std::fabs(amount) <= kZeroTolerance) {
        return;
    }
    coefficients[compact_node] -= amount;
}

std::vector<std::pair<int, double>> sorted_coefficients(
    const std::unordered_map<int, double>& coefficients) {
    std::vector<std::pair<int, double>> out;
    out.reserve(coefficients.size());
    for (const auto& [node, coefficient] : coefficients) {
        if (std::fabs(coefficient) <= kZeroTolerance) {
            continue;
        }
        out.push_back({node, coefficient});
    }
    std::sort(out.begin(), out.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    return out;
}

bool within_density_limit(const BendersCut& cut, const FppProjectedLlbiOptions& options) {
    return options.cut_density_limit <= 0 ||
           static_cast<int>(cut.coefficients_by_compact_index.size()) <=
               options.cut_density_limit;
}

void update_violation_stats(
    FppProjectedLlbiSeparationResult& result,
    double violation) {
    if (std::isnan(result.min_violation)) {
        result.min_violation = violation;
    } else {
        result.min_violation = std::min(result.min_violation, violation);
    }
    result.max_violation = std::max(result.max_violation, violation);
    result.sum_violation += violation;
}

void populate_projected_coverage_stats_from_data(
    FppProjectedLlbiStats& stats,
    const FppCoverageLlbiData& data,
    const std::string& mode,
    const std::string& validity_mode) {
    stats.projected_coverage_llbi_weighted = data.weighted;
    stats.projected_coverage_llbi_mode = mode;
    stats.projected_coverage_llbi_weight_map_hash = data.weight_map_hash;
    stats.projected_coverage_llbi_scenarios_precomputed = data.scenarios_precomputed;
    stats.projected_coverage_llbi_baseline_cells = data.baseline_cells;
    stats.projected_coverage_llbi_nonempty_coverage_sets =
        data.nonempty_coverage_sets;
    stats.projected_coverage_llbi_total_incidence_terms =
        data.total_incidence_terms;
    stats.projected_coverage_llbi_precompute_time_sec = data.precompute_time_sec;
    stats.projected_coverage_llbi_validity_mode = validity_mode;
}

void populate_projected_coverage_result_from_data(
    FppProjectedLlbiSeparationResult& result,
    const FppCoverageLlbiData& data,
    const std::string& mode,
    const std::string& validity_mode) {
    result.projected_coverage_llbi_weighted = data.weighted;
    result.projected_coverage_llbi_mode = mode;
    result.projected_coverage_llbi_weight_map_hash = data.weight_map_hash;
    result.projected_coverage_llbi_scenarios_precomputed =
        data.scenarios_precomputed;
    result.projected_coverage_llbi_baseline_cells = data.baseline_cells;
    result.projected_coverage_llbi_nonempty_coverage_sets =
        data.nonempty_coverage_sets;
    result.projected_coverage_llbi_total_incidence_terms =
        data.total_incidence_terms;
    result.projected_coverage_llbi_precompute_time_sec =
        data.precompute_time_sec;
    result.projected_coverage_llbi_validity_mode = validity_mode;
}

void populate_projected_path_stats_from_data(
    FppProjectedLlbiStats& stats,
    const FppPathLlbiData& data,
    const std::string& mode,
    const std::string& validity_mode) {
    stats.projected_path_llbi_weighted = data.weighted;
    stats.projected_path_llbi_mode = mode;
    stats.projected_path_llbi_weight_map_hash = data.weight_map_hash;
    stats.projected_path_llbi_scenarios_precomputed = data.scenarios_precomputed;
    stats.projected_path_llbi_destination_nodes = data.baseline_nodes;
    stats.projected_path_llbi_total_paths = data.total_paths;
    stats.projected_path_llbi_total_incidence_terms =
        data.total_candidate_incidence_terms;
    stats.projected_path_llbi_nodes_without_paths = data.nodes_without_paths;
    stats.projected_path_llbi_enumeration_complete = data.path_enumeration_complete;
    stats.projected_path_llbi_paths_truncated = data.paths_truncated;
    stats.projected_path_llbi_precompute_time_sec = data.precompute_time_sec;
    stats.projected_path_llbi_validity_mode = validity_mode;
}

void populate_projected_path_result_from_data(
    FppProjectedLlbiSeparationResult& result,
    const FppPathLlbiData& data,
    const std::string& mode,
    const std::string& validity_mode) {
    result.projected_path_llbi_weighted = data.weighted;
    result.projected_path_llbi_mode = mode;
    result.projected_path_llbi_weight_map_hash = data.weight_map_hash;
    result.projected_path_llbi_scenarios_precomputed = data.scenarios_precomputed;
    result.projected_path_llbi_destination_nodes = data.baseline_nodes;
    result.projected_path_llbi_total_paths = data.total_paths;
    result.projected_path_llbi_total_incidence_terms =
        data.total_candidate_incidence_terms;
    result.projected_path_llbi_nodes_without_paths = data.nodes_without_paths;
    result.projected_path_llbi_enumeration_complete = data.path_enumeration_complete;
    result.projected_path_llbi_paths_truncated = data.paths_truncated;
    result.projected_path_llbi_precompute_time_sec = data.precompute_time_sec;
    result.projected_path_llbi_validity_mode = validity_mode;
}

BendersCut build_projected_coverage_cut_from_support_zero(
    const FppCoverageLlbiScenarioRecord& scenario_record) {
    std::unordered_map<int, double> coefficients;
    for (const auto& node_record : scenario_record.nodes) {
        for (const int candidate : node_record.covering_candidate_compact_nodes) {
            add_negative_coeff(coefficients, candidate, node_record.cell_weight);
        }
    }

    BendersCut cut;
    cut.scenario_id = scenario_record.scenario_id;
    cut.rhs_constant = scenario_record.empty_burned_area;
    cut.coefficients_by_compact_index = sorted_coefficients(coefficients);
    cut.subproblem_objective = scenario_record.empty_burned_area;
    cut.notes.push_back("ProjectedCoverageLLBI-poly all-unsaturated support cut.");
    return cut;
}

BendersCut build_projected_path_static_cut(
    const FppPathLlbiScenarioRecord& scenario_record) {
    std::unordered_map<int, double> coefficients;
    double rhs = 0.0;
    for (const auto& node_record : scenario_record.nodes) {
        if (node_record.paths.empty()) {
            continue;
        }
        rhs += node_record.cell_weight;
        for (const int candidate : node_record.paths.front().blocking_candidate_compact_nodes) {
            add_negative_coeff(coefficients, candidate, node_record.cell_weight);
        }
    }

    BendersCut cut;
    cut.scenario_id = scenario_record.scenario_id;
    cut.rhs_constant = rhs;
    cut.coefficients_by_compact_index = sorted_coefficients(coefficients);
    cut.subproblem_objective = rhs;
    cut.notes.push_back("ProjectedPathLLBI-poly deterministic first-path-system cut.");
    return cut;
}

FppProjectedLlbiSeparatedCut separate_coverage_scenario(
    const FppCoverageLlbiScenarioRecord& scenario_record,
    const std::vector<double>& y_values_by_compact_node,
    double eta_value,
    const FppProjectedLlbiOptions& options) {
    std::unordered_map<int, double> coefficients;
    double saturated_constant = 0.0;
    double rhs_at_ybar = scenario_record.empty_burned_area;

    for (const auto& node_record : scenario_record.nodes) {
        double cover_sum = 0.0;
        for (const int candidate : node_record.covering_candidate_compact_nodes) {
            if (candidate < 0 ||
                candidate >= static_cast<int>(y_values_by_compact_node.size())) {
                throw std::runtime_error(
                    "Projected CoverageLLBI references an out-of-range candidate.");
            }
            cover_sum += y_values_by_compact_node[static_cast<std::size_t>(candidate)];
        }
        if (cover_sum >= 1.0 - options.violation_tolerance) {
            saturated_constant += node_record.cell_weight;
            rhs_at_ybar -= node_record.cell_weight;
        } else {
            rhs_at_ybar -= node_record.cell_weight * cover_sum;
            for (const int candidate : node_record.covering_candidate_compact_nodes) {
                add_negative_coeff(coefficients, candidate, node_record.cell_weight);
            }
        }
    }

    BendersCut cut;
    cut.scenario_id = scenario_record.scenario_id;
    cut.rhs_constant = scenario_record.empty_burned_area - saturated_constant;
    cut.coefficients_by_compact_index = sorted_coefficients(coefficients);
    cut.subproblem_objective = rhs_at_ybar;
    cut.notes.push_back("ProjectedCoverageLLBI-exp separated root cut.");

    FppProjectedLlbiSeparatedCut separated;
    separated.cut = std::move(cut);
    separated.scenario_index = scenario_record.scenario_index;
    separated.violation = rhs_at_ybar - eta_value;
    separated.nonzeros =
        static_cast<int>(separated.cut.coefficients_by_compact_index.size());
    return separated;
}

FppProjectedLlbiSeparatedCut separate_path_scenario(
    const FppPathLlbiScenarioRecord& scenario_record,
    const std::vector<double>& y_values_by_compact_node,
    double eta_value,
    const FppProjectedLlbiOptions& options) {
    std::unordered_map<int, double> coefficients;
    double rhs = 0.0;
    double rhs_at_ybar = 0.0;
    for (const auto& node_record : scenario_record.nodes) {
        if (node_record.paths.empty()) {
            continue;
        }
        double best_path_cost = std::numeric_limits<double>::infinity();
        const FppPathLlbiPathRecord* best_path = nullptr;
        for (const auto& path : node_record.paths) {
            double path_cost = 0.0;
            for (const int candidate : path.blocking_candidate_compact_nodes) {
                if (candidate < 0 ||
                    candidate >= static_cast<int>(y_values_by_compact_node.size())) {
                    throw std::runtime_error(
                        "Projected PathLLBI references an out-of-range candidate.");
                }
                path_cost += y_values_by_compact_node[static_cast<std::size_t>(candidate)];
            }
            if (!std::isfinite(path_cost)) {
                throw std::runtime_error("Projected PathLLBI path cost is not finite.");
            }
            if (!best_path ||
                path_cost < best_path_cost - kZeroTolerance) {
                best_path_cost = path_cost;
                best_path = &path;
            }
        }
        if (!best_path ||
            best_path_cost >= 1.0 - options.violation_tolerance) {
            continue;
        }
        rhs += node_record.cell_weight;
        rhs_at_ybar += node_record.cell_weight * (1.0 - best_path_cost);
        for (const int candidate : best_path->blocking_candidate_compact_nodes) {
            add_negative_coeff(coefficients, candidate, node_record.cell_weight);
        }
    }

    BendersCut cut;
    cut.scenario_id = scenario_record.scenario_id;
    cut.rhs_constant = rhs;
    cut.coefficients_by_compact_index = sorted_coefficients(coefficients);
    cut.subproblem_objective = rhs_at_ybar;
    cut.notes.push_back("ProjectedPathLLBI-exp separated stored minimum-path root cut.");

    FppProjectedLlbiSeparatedCut separated;
    separated.cut = std::move(cut);
    separated.scenario_index = scenario_record.scenario_index;
    separated.violation = rhs_at_ybar - eta_value;
    separated.nonzeros =
        static_cast<int>(separated.cut.coefficients_by_compact_index.size());
    return separated;
}

}  // namespace

FppProjectedLlbiMode active_projected_llbi_mode(const FppProjectedLlbiOptions& options) {
    if (options.use_projected_coverage_llbi_poly ||
        options.use_projected_path_llbi_poly) {
        return FppProjectedLlbiMode::Poly;
    }
    if (options.use_projected_coverage_llbi_exp ||
        options.use_projected_path_llbi_exp) {
        return FppProjectedLlbiMode::Exp;
    }
    return FppProjectedLlbiMode::None;
}

FppProjectedLlbiFamily active_projected_llbi_family(const FppProjectedLlbiOptions& options) {
    if (options.use_projected_coverage_llbi_exp ||
        options.use_projected_coverage_llbi_poly) {
        return FppProjectedLlbiFamily::Coverage;
    }
    if (options.use_projected_path_llbi_exp ||
        options.use_projected_path_llbi_poly) {
        return FppProjectedLlbiFamily::Path;
    }
    throw std::runtime_error("No projected LLBI family is enabled.");
}

std::string to_string(FppProjectedLlbiMode mode) {
    switch (mode) {
        case FppProjectedLlbiMode::None: return "";
        case FppProjectedLlbiMode::Exp: return "exp";
        case FppProjectedLlbiMode::Poly: return "poly";
    }
    return "";
}

std::string to_string(FppProjectedLlbiFamily family) {
    switch (family) {
        case FppProjectedLlbiFamily::Coverage: return "coverage";
        case FppProjectedLlbiFamily::Path: return "path";
    }
    return "";
}

void validate_fpp_projected_llbi_options(const FppProjectedLlbiOptions& options) {
    int enabled = 0;
    enabled += options.use_projected_coverage_llbi_exp ? 1 : 0;
    enabled += options.use_projected_path_llbi_exp ? 1 : 0;
    enabled += options.use_projected_coverage_llbi_poly ? 1 : 0;
    enabled += options.use_projected_path_llbi_poly ? 1 : 0;
    if (enabled > 1) {
        throw std::runtime_error(
            "Only one projected LLBI variant may be enabled at a time.");
    }
    if (options.root_rounds <= 0) {
        throw std::runtime_error("projected_llbi_root_rounds must be positive.");
    }
    if (options.max_cuts_per_round <= 0) {
        throw std::runtime_error("projected_llbi_max_cuts_per_round must be positive.");
    }
    if (options.violation_tolerance < 0.0) {
        throw std::runtime_error("projected_llbi_violation_tolerance must be nonnegative.");
    }
    if (options.cut_density_limit < 0) {
        throw std::runtime_error("projected_llbi_cut_density_limit must be nonnegative.");
    }
    if (options.poly_max_cuts <= 0) {
        throw std::runtime_error("projected_poly_max_cuts must be positive.");
    }
    if ((options.use_projected_path_llbi_exp ||
         options.use_projected_path_llbi_poly) &&
        options.path_max_paths_per_node <= 0) {
        throw std::runtime_error("projected PathLLBI path_max_paths_per_node must be positive.");
    }
}

std::vector<BendersCut> build_fpp_projected_llbi_poly_cuts(
    const opt::OptimizationInstance& opt,
    const FppProjectedLlbiOptions& options,
    FppProjectedLlbiStats* stats) {
    validate_fpp_projected_llbi_options(options);
    std::vector<BendersCut> cuts;
    if (active_projected_llbi_mode(options) != FppProjectedLlbiMode::Poly) {
        return cuts;
    }
    if (stats) {
        stats->projected_poly_enumeration_limit = options.poly_max_cuts;
    }

    const auto family = active_projected_llbi_family(options);
    if (family == FppProjectedLlbiFamily::Coverage) {
        const auto data = build_fpp_coverage_llbi_data(opt, true);
        if (stats) {
            populate_projected_coverage_stats_from_data(
                *stats,
                data,
                "poly-all-unsaturated-support",
                "weighted-subset-of-exact-per-cell-capped-coverage-projection");
        }
        for (const auto& scenario_record : data.scenarios) {
            if (static_cast<int>(cuts.size()) >= options.poly_max_cuts) {
                if (stats) {
                    stats->projected_poly_enumeration_truncated = true;
                }
                break;
            }
            auto cut = build_projected_coverage_cut_from_support_zero(scenario_record);
            if (!within_density_limit(cut, options)) {
                continue;
            }
            cuts.push_back(std::move(cut));
        }
    } else {
        const auto data = build_fpp_path_llbi_data(
            opt,
            true,
            options.use_projected_path_llbi_poly ? 1 : options.path_max_paths_per_node);
        if (stats) {
            populate_projected_path_stats_from_data(
                *stats,
                data,
                "poly-first-stored-path",
                "weighted-fixed-subset-of-directed-path-projection");
        }
        for (const auto& scenario_record : data.scenarios) {
            if (static_cast<int>(cuts.size()) >= options.poly_max_cuts) {
                if (stats) {
                    stats->projected_poly_enumeration_truncated = true;
                }
                break;
            }
            auto cut = build_projected_path_static_cut(scenario_record);
            if (!within_density_limit(cut, options)) {
                continue;
            }
            cuts.push_back(std::move(cut));
        }
    }

    if (stats) {
        stats->projected_poly_candidate_cuts_generated = static_cast<int>(cuts.size());
        stats->projected_poly_candidate_cuts_added = static_cast<int>(cuts.size());
        if (family == FppProjectedLlbiFamily::Coverage) {
            stats->projected_coverage_llbi_cuts_generated =
                static_cast<int>(cuts.size());
            stats->projected_coverage_llbi_cuts_added =
                static_cast<int>(cuts.size());
        }
        if (family == FppProjectedLlbiFamily::Path) {
            stats->projected_path_llbi_cuts_generated =
                static_cast<int>(cuts.size());
            stats->projected_path_llbi_cuts_added =
                static_cast<int>(cuts.size());
        }
    }
    return cuts;
}

FppProjectedLlbiSeparationResult separate_fpp_projected_llbi_cuts(
    const opt::OptimizationInstance& opt,
    const FppProjectedLlbiOptions& options,
    const std::vector<double>& y_values_by_eligible_position,
    const std::vector<double>& eta_values) {
    validate_fpp_projected_llbi_options(options);
    if (active_projected_llbi_mode(options) != FppProjectedLlbiMode::Exp) {
        return {};
    }
    if (eta_values.size() != opt.scenarios.size()) {
        throw std::runtime_error(
            "Projected LLBI separation received the wrong number of eta values.");
    }
    const auto start = std::chrono::steady_clock::now();
    const auto compact_y = compact_y_values(opt, y_values_by_eligible_position);
    FppProjectedLlbiSeparationResult result;

    const auto family = active_projected_llbi_family(options);
    if (family == FppProjectedLlbiFamily::Coverage) {
        const auto data = build_fpp_coverage_llbi_data(opt, true);
        populate_projected_coverage_result_from_data(
            result,
            data,
            "exp-exact-separated",
            "weighted-exact-per-cell-capped-coverage-projection");
        for (const auto& scenario_record : data.scenarios) {
            ++result.scenarios_checked;
            auto separated = separate_coverage_scenario(
                scenario_record,
                compact_y,
                eta_values[static_cast<std::size_t>(scenario_record.scenario_index)],
                options);
            const double violation = separated.violation;
            update_violation_stats(result, violation);
            if (violation <= options.violation_tolerance ||
                !within_density_limit(separated.cut, options)) {
                continue;
            }
            ++result.violated_cuts_found;
            result.cuts.push_back(std::move(separated));
            if (static_cast<int>(result.cuts.size()) >= options.max_cuts_per_round) {
                break;
            }
        }
    } else {
        const auto data = build_fpp_path_llbi_data(
            opt,
            true,
            options.path_max_paths_per_node);
        populate_projected_path_result_from_data(
            result,
            data,
            "exp-exact-stored-path-separation",
            data.path_enumeration_complete
                ? "exact-directed-path-projection"
                : "truncated-stored-directed-path-projection");
        for (const auto& scenario_record : data.scenarios) {
            ++result.scenarios_checked;
            auto separated = separate_path_scenario(
                scenario_record,
                compact_y,
                eta_values[static_cast<std::size_t>(scenario_record.scenario_index)],
                options);
            const double violation = separated.violation;
            update_violation_stats(result, violation);
            if (violation <= options.violation_tolerance ||
                !within_density_limit(separated.cut, options)) {
                continue;
            }
            ++result.violated_cuts_found;
            result.cuts.push_back(std::move(separated));
            if (static_cast<int>(result.cuts.size()) >= options.max_cuts_per_round) {
                break;
            }
        }
    }

    result.separation_time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}

void export_fpp_projected_llbi_cuts(
    const std::filesystem::path& output_path,
    const std::vector<FppProjectedLlbiSeparatedCut>& cuts) {
    if (output_path.empty()) {
        return;
    }
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error(
            "Could not open projected LLBI cut export path: " + output_path.string());
    }
    out << "scenario_id,scenario_index,violation,rhs_constant,nonzeros,coefficients\n";
    for (const auto& separated : cuts) {
        out << separated.cut.scenario_id << ","
            << separated.scenario_index << ","
            << separated.violation << ","
            << separated.cut.rhs_constant << ","
            << separated.nonzeros << ",\"";
        bool first = true;
        for (const auto& [node, coefficient] : separated.cut.coefficients_by_compact_index) {
            if (!first) {
                out << ";";
            }
            first = false;
            out << node << ":" << coefficient;
        }
        out << "\"\n";
    }
}

}  // namespace firebreak::benders
