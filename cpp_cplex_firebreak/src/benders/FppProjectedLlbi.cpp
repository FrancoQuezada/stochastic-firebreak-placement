#include "benders/FppProjectedLlbi.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "benders/FppStrengthening.hpp"

namespace firebreak::benders {

namespace {

constexpr double kZeroTolerance = 1.0e-12;

std::vector<int> y_positions_by_node(const opt::OptimizationInstance& opt) {
    std::vector<int> positions(static_cast<std::size_t>(opt.node_mapper.size()), -1);
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        positions[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            static_cast<int>(pos);
    }
    return positions;
}

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

std::vector<std::vector<int>> scenario_successors(
    const opt::OptimizationScenario& scenario,
    int node_count) {
    std::vector<std::vector<int>> successors(static_cast<std::size_t>(node_count));
    for (const auto& arc : scenario.arcs) {
        if (arc.u_index < 0 || arc.u_index >= node_count ||
            arc.v_index < 0 || arc.v_index >= node_count) {
            throw std::runtime_error("Projected LLBI scenario arc references an out-of-range node.");
        }
        successors[static_cast<std::size_t>(arc.u_index)].push_back(arc.v_index);
    }
    for (auto& items : successors) {
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
    }
    return successors;
}

std::vector<int> reachable_from(
    const std::vector<std::vector<int>>& successors,
    int root) {
    std::vector<char> seen(successors.size(), 0);
    std::queue<int> frontier;
    seen[static_cast<std::size_t>(root)] = 1;
    frontier.push(root);
    std::vector<int> reached;
    while (!frontier.empty()) {
        const int current = frontier.front();
        frontier.pop();
        reached.push_back(current);
        for (const int next : successors[static_cast<std::size_t>(current)]) {
            if (seen[static_cast<std::size_t>(next)]) {
                continue;
            }
            seen[static_cast<std::size_t>(next)] = 1;
            frontier.push(next);
        }
    }
    std::sort(reached.begin(), reached.end());
    return reached;
}

BendersCut build_projected_coverage_cut_from_support_zero(
    const FppCoverageLlbiScenarioRecord& scenario_record) {
    std::unordered_map<int, double> coefficients;
    for (const auto& node_record : scenario_record.nodes) {
        for (const int candidate : node_record.covering_candidate_compact_nodes) {
            add_negative_coeff(coefficients, candidate, 1.0);
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
        rhs += 1.0;
        for (const int candidate : node_record.paths.front().blocking_candidate_compact_nodes) {
            add_negative_coeff(coefficients, candidate, 1.0);
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
            saturated_constant += 1.0;
            rhs_at_ybar -= 1.0;
        } else {
            rhs_at_ybar -= cover_sum;
            for (const int candidate : node_record.covering_candidate_compact_nodes) {
                add_negative_coeff(coefficients, candidate, 1.0);
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
    const opt::OptimizationInstance& opt,
    std::size_t scenario_index,
    const std::vector<double>& y_values_by_compact_node,
    double eta_value,
    const FppProjectedLlbiOptions& options) {
    const auto& scenario = opt.scenarios[scenario_index];
    const int node_count = opt.node_mapper.size();
    const auto y_pos = y_positions_by_node(opt);
    const auto successors = scenario_successors(scenario, node_count);
    const auto relevant_nodes = reachable_from(successors, scenario.ignition_index);

    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(static_cast<std::size_t>(node_count), inf);
    std::vector<int> pred(static_cast<std::size_t>(node_count), -1);
    using QueueItem = std::pair<double, int>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;
    dist[static_cast<std::size_t>(scenario.ignition_index)] = 0.0;
    queue.push({0.0, scenario.ignition_index});
    while (!queue.empty()) {
        const auto [current_dist, current] = queue.top();
        queue.pop();
        if (current_dist > dist[static_cast<std::size_t>(current)] + kZeroTolerance) {
            continue;
        }
        for (const int next : successors[static_cast<std::size_t>(current)]) {
            double node_cost = 0.0;
            if (next != scenario.ignition_index &&
                next >= 0 &&
                next < static_cast<int>(y_pos.size()) &&
                y_pos[static_cast<std::size_t>(next)] >= 0) {
                node_cost = y_values_by_compact_node[static_cast<std::size_t>(next)];
            }
            const double candidate_dist = current_dist + node_cost;
            const auto next_pos = static_cast<std::size_t>(next);
            if (candidate_dist + kZeroTolerance < dist[next_pos]) {
                dist[next_pos] = candidate_dist;
                pred[next_pos] = current;
                queue.push({candidate_dist, next});
            }
        }
    }

    std::unordered_map<int, double> coefficients;
    double rhs = 0.0;
    double rhs_at_ybar = 0.0;
    for (const int node : relevant_nodes) {
        const double distance = dist[static_cast<std::size_t>(node)];
        if (!std::isfinite(distance) || distance >= 1.0 - options.violation_tolerance) {
            continue;
        }
        rhs += 1.0;
        rhs_at_ybar += 1.0 - distance;
        int current = node;
        while (current != -1 && current != scenario.ignition_index) {
            if (current >= 0 &&
                current < static_cast<int>(y_pos.size()) &&
                y_pos[static_cast<std::size_t>(current)] >= 0) {
                add_negative_coeff(coefficients, current, 1.0);
            }
            current = pred[static_cast<std::size_t>(current)];
        }
    }

    BendersCut cut;
    cut.scenario_id = scenario.scenario_id;
    cut.rhs_constant = rhs;
    cut.coefficients_by_compact_index = sorted_coefficients(coefficients);
    cut.subproblem_objective = rhs_at_ybar;
    cut.notes.push_back("ProjectedPathLLBI-exp separated shortest-path root cut.");

    FppProjectedLlbiSeparatedCut separated;
    separated.cut = std::move(cut);
    separated.scenario_index = static_cast<int>(scenario_index);
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
        const auto data = build_fpp_path_llbi_data(opt, true, 1);
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
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            ++result.scenarios_checked;
            auto separated = separate_path_scenario(
                opt,
                s,
                compact_y,
                eta_values[s],
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
