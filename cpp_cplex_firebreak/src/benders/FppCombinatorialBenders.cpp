#include "benders/FppCombinatorialBenders.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <functional>
#include <limits>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace firebreak::benders {

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

std::string normalized_key(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        if (ch == '-' || ch == '_' || ch == ' ') {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

void ensure_node_in_range(int node, int node_count, const std::string& context) {
    if (node < 0 || node >= node_count) {
        throw std::runtime_error(context + " compact node index is out of range.");
    }
}

std::vector<double> expand_y_to_compact_values(
    const opt::OptimizationInstance& opt,
    const std::vector<double>& y_values_by_eligible_position) {
    if (y_values_by_eligible_position.size() != opt.eligible_indices.size()) {
        throw std::runtime_error(
            "FPP combinatorial Benders y vector has the wrong eligible-node size.");
    }
    std::vector<double> compact_values(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < y_values_by_eligible_position.size(); ++pos) {
        compact_values[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            y_values_by_eligible_position[pos];
    }
    return compact_values;
}

std::vector<double> binary_to_double(const std::vector<int>& values) {
    std::vector<double> as_double;
    as_double.reserve(values.size());
    for (const int value : values) {
        as_double.push_back(value != 0 ? 1.0 : 0.0);
    }
    return as_double;
}

int sampled_cut_limit(double ratio, std::size_t scenario_count) {
    if (scenario_count == 0) {
        return 0;
    }
    if (ratio >= 1.0) {
        return static_cast<int>(scenario_count);
    }
    return std::max(1, static_cast<int>(std::ceil(ratio * scenario_count)));
}

}  // namespace

double FppCombinatorialBendersStats::average_paths_per_cut() const {
    return cuts_for_averages > 0
        ? total_paths_per_cut / static_cast<double>(cuts_for_averages)
        : 0.0;
}

double FppCombinatorialBendersStats::average_cut_nonzeros() const {
    return cuts_for_averages > 0
        ? total_nonzeros_per_cut / static_cast<double>(cuts_for_averages)
        : 0.0;
}

std::string to_string(FppCombinatorialBendersLiftMode mode) {
    switch (mode) {
        case FppCombinatorialBendersLiftMode::None:
            return "none";
        case FppCombinatorialBendersLiftMode::Posterior:
            return "posterior";
        case FppCombinatorialBendersLiftMode::Heuristic:
            return "heuristic";
    }
    return "heuristic";
}

FppCombinatorialBendersLiftMode parse_fpp_combinatorial_benders_lift_mode(
    const std::string& value) {
    const std::string key = normalized_key(value);
    if (key == "none" || key == "nolift") {
        return FppCombinatorialBendersLiftMode::None;
    }
    if (key == "posterior" || key == "p") {
        return FppCombinatorialBendersLiftMode::Posterior;
    }
    if (key == "heuristic" || key == "h") {
        return FppCombinatorialBendersLiftMode::Heuristic;
    }
    throw std::runtime_error(
        "Unsupported FPP combinatorial Benders lift mode: " + value +
        ". Supported values are none, posterior, heuristic.");
}

std::string to_string(FppCombinatorialBendersScenarioOrder order) {
    switch (order) {
        case FppCombinatorialBendersScenarioOrder::EtaAscending:
            return "eta-asc";
        case FppCombinatorialBendersScenarioOrder::EtaDescending:
            return "eta-desc";
    }
    return "eta-asc";
}

FppCombinatorialBendersScenarioOrder parse_fpp_combinatorial_benders_scenario_order(
    const std::string& value) {
    const std::string key = normalized_key(value);
    if (key == "etaasc" || key == "ascendingeta" || key == "asc") {
        return FppCombinatorialBendersScenarioOrder::EtaAscending;
    }
    if (key == "etadesc" || key == "descendingeta" || key == "desc") {
        return FppCombinatorialBendersScenarioOrder::EtaDescending;
    }
    throw std::runtime_error(
        "Unsupported FPP combinatorial Benders scenario order: " + value +
        ". Supported values are eta-asc, eta-desc.");
}

std::vector<int> order_fpp_combinatorial_scenarios_by_eta(
    const std::vector<double>& eta_values_by_scenario,
    FppCombinatorialBendersScenarioOrder order) {
    std::vector<int> ordered;
    ordered.reserve(eta_values_by_scenario.size());
    for (std::size_t s = 0; s < eta_values_by_scenario.size(); ++s) {
        ordered.push_back(static_cast<int>(s));
    }
    std::sort(
        ordered.begin(),
        ordered.end(),
        [&eta_values_by_scenario, order](int lhs, int rhs) {
            const double lhs_eta = eta_values_by_scenario[static_cast<std::size_t>(lhs)];
            const double rhs_eta = eta_values_by_scenario[static_cast<std::size_t>(rhs)];
            if (std::fabs(lhs_eta - rhs_eta) > 1.0e-12) {
                if (order == FppCombinatorialBendersScenarioOrder::EtaDescending) {
                    return lhs_eta > rhs_eta;
                }
                return lhs_eta < rhs_eta;
            }
            return lhs < rhs;
        });
    return ordered;
}

void validate_fpp_combinatorial_benders_options(
    const FppCombinatorialBendersOptions& options) {
    if (options.cut_sampling_ratio <= 0.0 || options.cut_sampling_ratio > 1.0) {
        throw std::runtime_error(
            "FPP combinatorial Benders cut sampling ratio must be in (0, 1].");
    }
}

FppCombinatorialBendersSeparator::FppCombinatorialBendersSeparator(
    const opt::OptimizationInstance& opt)
    : opt_(opt),
      node_count_(opt.node_mapper.size()) {
    if (node_count_ <= 0) {
        throw std::runtime_error(
            "FPP combinatorial Benders separator requires at least one mapped node.");
    }
    if (opt_.scenarios.empty()) {
        throw std::runtime_error(
            "FPP combinatorial Benders separator requires at least one scenario.");
    }
    if (opt_.eligible_indices.empty()) {
        throw std::runtime_error(
            "FPP combinatorial Benders separator requires at least one eligible candidate.");
    }

    eligible_.assign(static_cast<std::size_t>(node_count_), 0);
    y_position_by_node_.assign(static_cast<std::size_t>(node_count_), -1);
    for (std::size_t pos = 0; pos < opt_.eligible_indices.size(); ++pos) {
        const int node = opt_.eligible_indices[pos];
        ensure_node_in_range(node, node_count_, "Eligible firebreak");
        eligible_[static_cast<std::size_t>(node)] = 1;
        y_position_by_node_[static_cast<std::size_t>(node)] = static_cast<int>(pos);
    }

    successors_by_scenario_.reserve(opt_.scenarios.size());
    for (const auto& scenario : opt_.scenarios) {
        ensure_node_in_range(scenario.ignition_index, node_count_, "Ignition");
        std::vector<std::vector<int>> successors(static_cast<std::size_t>(node_count_));
        for (const auto& arc : scenario.arcs) {
            ensure_node_in_range(arc.u_index, node_count_, "Arc source");
            ensure_node_in_range(arc.v_index, node_count_, "Arc target");
            successors[static_cast<std::size_t>(arc.u_index)].push_back(arc.v_index);
        }
        for (auto& items : successors) {
            std::sort(items.begin(), items.end());
            items.erase(std::unique(items.begin(), items.end()), items.end());
        }
        successors_by_scenario_.push_back(std::move(successors));
    }
}

FppCombinatorialCut FppCombinatorialBendersSeparator::separateScenario(
    int scenario_position,
    const std::vector<double>& y_values_by_eligible_position,
    double eta_value,
    bool fractional,
    FppCombinatorialBendersLiftMode lift_mode,
    double tolerance) const {
    if (scenario_position < 0 ||
        scenario_position >= static_cast<int>(opt_.scenarios.size())) {
        throw std::runtime_error(
            "FPP combinatorial Benders scenario position is out of range.");
    }
    if (y_values_by_eligible_position.size() != opt_.eligible_indices.size()) {
        throw std::runtime_error(
            "FPP combinatorial Benders separator received a y vector with the wrong size.");
    }

    FppCombinatorialCut separated;
    separated.fractional = fractional;
    if (fractional && lift_mode != FppCombinatorialBendersLiftMode::None) {
        lift_mode = FppCombinatorialBendersLiftMode::None;
        separated.lift_mode_fallback = true;
    }

    const auto& scenario = opt_.scenarios[static_cast<std::size_t>(scenario_position)];
    const auto& successors = successors_by_scenario_[static_cast<std::size_t>(scenario_position)];
    const int root = scenario.ignition_index;

    std::vector<double> y_compact = expand_y_to_compact_values(opt_, y_values_by_eligible_position);
    std::vector<double> dist(static_cast<std::size_t>(node_count_), kInfinity);
    std::vector<int> parent(static_cast<std::size_t>(node_count_), -1);
    using QueueItem = std::pair<double, int>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;
    dist[static_cast<std::size_t>(root)] = 0.0;
    queue.push({0.0, root});

    while (!queue.empty()) {
        const auto [current_dist, current] = queue.top();
        queue.pop();
        if (current_dist > dist[static_cast<std::size_t>(current)] + 1.0e-12) {
            continue;
        }
        if (current_dist >= 1.0 - 1.0e-12) {
            break;
        }
        for (const int next : successors[static_cast<std::size_t>(current)]) {
            if (next == root) {
                continue;
            }
            const bool next_blocks =
                eligible_[static_cast<std::size_t>(next)] != 0;
            const double step_cost = next_blocks
                ? std::max(0.0, std::min(1.0, y_compact[static_cast<std::size_t>(next)]))
                : 0.0;
            const double candidate_dist = current_dist + step_cost;
            auto& next_dist = dist[static_cast<std::size_t>(next)];
            const bool better = candidate_dist < next_dist - 1.0e-12;
            if (better) {
                next_dist = candidate_dist;
                parent[static_cast<std::size_t>(next)] = current;
                queue.push({candidate_dist, next});
            }
        }
    }

    std::unordered_map<int, double> coefficient_counts;
    int active_nodes = 0;
    int paths = 0;
    for (int node = 0; node < node_count_; ++node) {
        if (!(dist[static_cast<std::size_t>(node)] < 1.0 - tolerance)) {
            continue;
        }
        ++active_nodes;
        ++paths;
        std::set<int> blockers_on_path;
        int current = node;
        int guard = 0;
        while (current != root && current >= 0 && guard <= node_count_) {
            if (eligible_[static_cast<std::size_t>(current)] != 0) {
                if (lift_mode == FppCombinatorialBendersLiftMode::None) {
                    coefficient_counts[current] += 1.0;
                } else {
                    blockers_on_path.insert(current);
                }
            }
            current = parent[static_cast<std::size_t>(current)];
            ++guard;
        }
        if (lift_mode != FppCombinatorialBendersLiftMode::None) {
            for (const int blocker : blockers_on_path) {
                coefficient_counts[blocker] += 1.0;
            }
        }
    }

    BendersCut cut;
    cut.scenario_id = scenario.scenario_id;
    cut.rhs_constant = static_cast<double>(active_nodes);
    cut.subproblem_objective = static_cast<double>(active_nodes);
    cut.ybar_compact_values.reserve(opt_.eligible_indices.size());
    for (std::size_t pos = 0; pos < opt_.eligible_indices.size(); ++pos) {
        cut.ybar_compact_values.push_back({
            opt_.eligible_indices[pos],
            y_values_by_eligible_position[pos],
        });
    }
    cut.coefficients_by_compact_index.reserve(coefficient_counts.size());
    for (const auto& [compact_node, count] : coefficient_counts) {
        if (std::fabs(count) <= 1.0e-12) {
            continue;
        }
        cut.coefficients_by_compact_index.push_back({compact_node, -count});
    }
    std::sort(
        cut.coefficients_by_compact_index.begin(),
        cut.coefficients_by_compact_index.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    separated.cut = std::move(cut);
    separated.rhs_at_ybar = separated.cut.evaluateAt(y_compact);
    separated.violation = separated.rhs_at_ybar - eta_value;
    separated.active_nodes = active_nodes;
    separated.activation_paths = paths;
    separated.nonzeros =
        static_cast<int>(separated.cut.coefficients_by_compact_index.size());
    if (separated.lift_mode_fallback) {
        separated.cut.notes.push_back(
            "Fractional combinatorial separation used non-lifted path cuts because heuristic lifting is not asserted as a stronger fractional dual-feasible cut.");
    }
    return separated;
}

FppCombinatorialSeparationSummary
FppCombinatorialBendersSeparator::separateViolatedCuts(
    const std::vector<double>& y_values_by_eligible_position,
    const std::vector<double>& eta_values_by_scenario,
    bool fractional,
    FppCombinatorialBendersLiftMode lift_mode,
    FppCombinatorialBendersScenarioOrder scenario_order,
    double cut_sampling_ratio,
    double tolerance) const {
    if (eta_values_by_scenario.size() != opt_.scenarios.size()) {
        throw std::runtime_error(
            "FPP combinatorial Benders separator received an eta vector with the wrong size.");
    }
    FppCombinatorialSeparationSummary summary;
    const auto start = std::chrono::steady_clock::now();
    const std::vector<int> order =
        order_fpp_combinatorial_scenarios_by_eta(eta_values_by_scenario, scenario_order);

    const int max_violated = sampled_cut_limit(cut_sampling_ratio, opt_.scenarios.size());
    for (const int scenario_position : order) {
        if (summary.violated_cuts >= max_violated) {
            break;
        }
        ++summary.scenarios_checked;
        auto cut = separateScenario(
            scenario_position,
            y_values_by_eligible_position,
            eta_values_by_scenario[static_cast<std::size_t>(scenario_position)],
            fractional,
            lift_mode,
            tolerance);
        summary.max_violation = std::max(summary.max_violation, cut.violation);
        if (cut.lift_mode_fallback) {
            ++summary.lift_fallback_count;
        }
        if (cut.violation > tolerance) {
            ++summary.violated_cuts;
            summary.total_paths += cut.activation_paths;
            summary.total_nonzeros += cut.nonzeros;
            summary.cuts.push_back(std::move(cut));
        } else {
            ++summary.nonviolated_cuts;
        }
    }
    summary.scenarios_skipped =
        static_cast<int>(opt_.scenarios.size()) - summary.scenarios_checked;
    summary.separation_time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return summary;
}

std::vector<FppCombinatorialCut>
FppCombinatorialBendersSeparator::initialCutsFromSolution(
    const std::vector<int>& y_values_by_eligible_position,
    FppCombinatorialBendersLiftMode lift_mode) const {
    const auto y_double = binary_to_double(y_values_by_eligible_position);
    std::vector<FppCombinatorialCut> cuts;
    cuts.reserve(opt_.scenarios.size());
    for (std::size_t s = 0; s < opt_.scenarios.size(); ++s) {
        cuts.push_back(separateScenario(
            static_cast<int>(s),
            y_double,
            -kInfinity,
            false,
            lift_mode,
            0.0));
    }
    return cuts;
}

std::vector<double> FppCombinatorialBendersSeparator::evaluateScenarioLosses(
    const std::vector<int>& y_values_by_eligible_position) const {
    if (y_values_by_eligible_position.size() != opt_.eligible_indices.size()) {
        throw std::runtime_error(
            "FPP combinatorial burned-area evaluation received a y vector with the wrong size.");
    }
    std::vector<char> selected(static_cast<std::size_t>(node_count_), 0);
    for (std::size_t pos = 0; pos < y_values_by_eligible_position.size(); ++pos) {
        if (y_values_by_eligible_position[pos] != 0) {
            selected[static_cast<std::size_t>(opt_.eligible_indices[pos])] = 1;
        }
    }

    std::vector<double> losses;
    losses.reserve(opt_.scenarios.size());
    for (std::size_t s = 0; s < opt_.scenarios.size(); ++s) {
        const auto& scenario = opt_.scenarios[s];
        const auto& successors = successors_by_scenario_[s];
        const int root = scenario.ignition_index;
        std::vector<char> reached(static_cast<std::size_t>(node_count_), 0);
        std::queue<int> frontier;
        reached[static_cast<std::size_t>(root)] = 1;
        frontier.push(root);
        double burned_count = 1.0;
        while (!frontier.empty()) {
            const int current = frontier.front();
            frontier.pop();
            for (const int next : successors[static_cast<std::size_t>(current)]) {
                const auto next_pos = static_cast<std::size_t>(next);
                if (reached[next_pos]) {
                    continue;
                }
                reached[next_pos] = 1;
                if (next != root && selected[next_pos]) {
                    continue;
                }
                burned_count += 1.0;
                frontier.push(next);
            }
        }
        losses.push_back(burned_count);
    }
    return losses;
}

std::vector<int> FppCombinatorialBendersSeparator::greedyInitialSolution() const {
    std::vector<int> selected_by_position(opt_.eligible_indices.size(), 0);
    const int picks = std::min(opt_.budget, static_cast<int>(opt_.eligible_indices.size()));
    if (picks <= 0) {
        return selected_by_position;
    }
    auto current_losses = evaluateScenarioLosses(selected_by_position);
    for (int pick = 0; pick < picks; ++pick) {
        int best_pos = -1;
        double best_reduction = -std::numeric_limits<double>::infinity();
        for (std::size_t pos = 0; pos < opt_.eligible_indices.size(); ++pos) {
            if (selected_by_position[pos] != 0) {
                continue;
            }
            auto trial = selected_by_position;
            trial[pos] = 1;
            const auto trial_losses = evaluateScenarioLosses(trial);
            double reduction = 0.0;
            for (std::size_t s = 0; s < opt_.scenarios.size(); ++s) {
                reduction += opt_.scenarios[s].probability *
                    (current_losses[s] - trial_losses[s]);
            }
            if (reduction > best_reduction + 1.0e-12 ||
                (std::fabs(reduction - best_reduction) <= 1.0e-12 &&
                 (best_pos < 0 || static_cast<int>(pos) < best_pos))) {
                best_reduction = reduction;
                best_pos = static_cast<int>(pos);
            }
        }
        if (best_pos < 0) {
            break;
        }
        selected_by_position[static_cast<std::size_t>(best_pos)] = 1;
        current_losses = evaluateScenarioLosses(selected_by_position);
    }
    return selected_by_position;
}

}  // namespace firebreak::benders
