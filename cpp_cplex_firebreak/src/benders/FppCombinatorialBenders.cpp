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

bool has_nonunit_weights(const std::vector<double>& weights) {
    for (const double weight : weights) {
        if (std::fabs(weight - 1.0) > 1.0e-9) {
            return true;
        }
    }
    return false;
}

std::vector<double> compact_weights_or_unit(const opt::OptimizationInstance& opt) {
    if (opt.compact_cell_weights.empty()) {
        return std::vector<double>(static_cast<std::size_t>(opt.node_mapper.size()), 1.0);
    }
    if (opt.compact_cell_weights.size() !=
        static_cast<std::size_t>(opt.node_mapper.size())) {
        throw std::runtime_error(
            "FPP combinatorial Benders compact weight vector does not cover the optimization node universe.");
    }
    for (const double weight : opt.compact_cell_weights) {
        if (!std::isfinite(weight) || weight <= 0.0) {
            throw std::runtime_error(
                "FPP combinatorial Benders compact weights must be finite and strictly positive.");
        }
    }
    return opt.compact_cell_weights;
}

bool is_phase6c2a_supported_lift_mode(FppCombinatorialBendersLiftMode mode) {
    return mode == FppCombinatorialBendersLiftMode::None ||
           mode == FppCombinatorialBendersLiftMode::Heuristic ||
           mode == FppCombinatorialBendersLiftMode::Posterior;
}

BendersCut make_combinatorial_cut_from_counts(
    int scenario_id,
    double active_loss,
    const std::unordered_map<int, double>& coefficient_counts,
    const opt::OptimizationInstance& opt,
    const std::vector<double>& y_values_by_eligible_position) {
    BendersCut cut;
    cut.scenario_id = scenario_id;
    cut.rhs_constant = active_loss;
    cut.subproblem_objective = active_loss;
    cut.ybar_compact_values.reserve(opt.eligible_indices.size());
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        cut.ybar_compact_values.push_back({
            opt.eligible_indices[pos],
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
    return cut;
}

double coefficient_for_node(const BendersCut& cut, int compact_node) {
    const auto it = std::lower_bound(
        cut.coefficients_by_compact_index.begin(),
        cut.coefficients_by_compact_index.end(),
        std::pair<int, double>{compact_node, -std::numeric_limits<double>::infinity()},
        [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });
    if (it == cut.coefficients_by_compact_index.end() || it->first != compact_node) {
        return 0.0;
    }
    return it->second;
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

std::string fpp_phase6c2a_combinatorial_mode(
    FppCombinatorialBendersLiftMode mode) {
    if (mode == FppCombinatorialBendersLiftMode::None) {
        return "baseline-integer-exact-no-lifting";
    }
    if (mode == FppCombinatorialBendersLiftMode::Posterior) {
        return "baseline-integer-exact-posterior-path-dedup-lifting";
    }
    return "baseline-integer-exact-heuristic-path-dedup-lifting";
}

std::string fpp_phase6c2a_lifting_validity_mode(
    FppCombinatorialBendersLiftMode mode,
    bool weighted) {
    if (mode == FppCombinatorialBendersLiftMode::None) {
        return "none";
    }
    if (mode == FppCombinatorialBendersLiftMode::Posterior) {
        return weighted
            ? "exact-posterior-weighted-path-dedup-lifting"
            : "exact-posterior-unit-path-dedup-lifting";
    }
    return weighted
        ? "heuristic-mode-exact-weighted-path-dedup-lifting"
        : "heuristic-mode-exact-unit-path-dedup-lifting";
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
    std::vector<int> scenario_ids;
    scenario_ids.reserve(eta_values_by_scenario.size());
    for (std::size_t s = 0; s < eta_values_by_scenario.size(); ++s) {
        scenario_ids.push_back(static_cast<int>(s));
    }
    return order_fpp_combinatorial_scenarios_by_eta(
        eta_values_by_scenario,
        scenario_ids,
        order);
}

std::vector<int> order_fpp_combinatorial_scenarios_by_eta(
    const std::vector<double>& eta_values_by_scenario,
    const std::vector<int>& scenario_ids_by_position,
    FppCombinatorialBendersScenarioOrder order) {
    if (scenario_ids_by_position.size() != eta_values_by_scenario.size()) {
        throw std::runtime_error(
            "FPP combinatorial Benders scenario ordering received mismatched eta and scenario-id vectors.");
    }
    std::vector<int> ordered;
    ordered.reserve(eta_values_by_scenario.size());
    for (std::size_t s = 0; s < eta_values_by_scenario.size(); ++s) {
        ordered.push_back(static_cast<int>(s));
    }
    std::sort(
        ordered.begin(),
        ordered.end(),
        [&eta_values_by_scenario, &scenario_ids_by_position, order](int lhs, int rhs) {
            const double lhs_eta = eta_values_by_scenario[static_cast<std::size_t>(lhs)];
            const double rhs_eta = eta_values_by_scenario[static_cast<std::size_t>(rhs)];
            if (std::fabs(lhs_eta - rhs_eta) > 1.0e-12) {
                if (order == FppCombinatorialBendersScenarioOrder::EtaDescending) {
                    return lhs_eta > rhs_eta;
                }
                return lhs_eta < rhs_eta;
            }
            const int lhs_id = scenario_ids_by_position[static_cast<std::size_t>(lhs)];
            const int rhs_id = scenario_ids_by_position[static_cast<std::size_t>(rhs)];
            if (lhs_id != rhs_id) {
                return lhs_id < rhs_id;
            }
            return lhs < rhs;
        });
    return ordered;
}

int fpp_combinatorial_realized_sample_size(
    std::size_t scenario_count,
    double cut_sampling_ratio) {
    if (cut_sampling_ratio <= 0.0 || cut_sampling_ratio > 1.0) {
        throw std::runtime_error(
            "FPP combinatorial Benders cut sampling ratio must be in (0, 1].");
    }
    if (scenario_count == 0) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::ceil(
                           cut_sampling_ratio * static_cast<double>(scenario_count))));
}

void validate_fpp_combinatorial_benders_options(
    const FppCombinatorialBendersOptions& options) {
    (void)fpp_combinatorial_realized_sample_size(1, options.cut_sampling_ratio);
}

bool is_fpp_phase6c1_weighted_combinatorial_baseline(
    const FppCombinatorialBendersOptions& options) {
    return options.enabled &&
           options.lift_mode == FppCombinatorialBendersLiftMode::None &&
           options.scenario_order == FppCombinatorialBendersScenarioOrder::EtaAscending &&
           std::fabs(options.cut_sampling_ratio - 1.0) <= 1.0e-12 &&
           !options.separate_fractional &&
           !options.initial_cuts;
}

bool is_fpp_phase6c2a_weighted_combinatorial_integer_mode(
    const FppCombinatorialBendersOptions& options) {
    return options.enabled &&
           is_phase6c2a_supported_lift_mode(options.lift_mode) &&
           options.scenario_order == FppCombinatorialBendersScenarioOrder::EtaAscending &&
           std::fabs(options.cut_sampling_ratio - 1.0) <= 1.0e-12 &&
           !options.separate_fractional &&
           !options.initial_cuts;
}

bool is_fpp_phase6c2b_weighted_combinatorial_mode(
    const FppCombinatorialBendersOptions& options) {
    return options.enabled &&
           is_phase6c2a_supported_lift_mode(options.lift_mode) &&
           options.scenario_order == FppCombinatorialBendersScenarioOrder::EtaAscending &&
           std::fabs(options.cut_sampling_ratio - 1.0) <= 1.0e-12;
}

bool is_fpp_phase6c2c_weighted_combinatorial_mode(
    const FppCombinatorialBendersOptions& options) {
    return options.enabled &&
           is_phase6c2a_supported_lift_mode(options.lift_mode) &&
           options.cut_sampling_ratio > 0.0 &&
           options.cut_sampling_ratio <= 1.0;
}

void validate_fpp_phase6c1_weighted_combinatorial_baseline(
    const FppCombinatorialBendersOptions& options,
    bool use_root_user_cuts,
    bool use_lifted_lower_bounds,
    const FppStrengtheningOptions& strengthening_options) {
    if (!options.enabled) {
        return;
    }
    if (!is_fpp_phase6c1_weighted_combinatorial_baseline(options)) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C1 supports only baseline integer incumbent cuts with lift_mode=none, scenario_order=eta-asc, cut_sampling_ratio=1, separate_fractional=false, and initial_cuts=false.");
    }
    if (use_root_user_cuts) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C1 does not combine with root user cuts.");
    }
    if (use_lifted_lower_bounds ||
        strengthening_options.use_coverage_llbi ||
        strengthening_options.use_path_llbi ||
        strengthening_options.use_projected_coverage_llbi_exp ||
        strengthening_options.use_projected_path_llbi_exp ||
        strengthening_options.use_projected_coverage_llbi_poly ||
        strengthening_options.use_projected_path_llbi_poly) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C1 does not combine with LLBI or projected LLBI families.");
    }
    if (strengthening_options.use_global_dominance_preprocessing) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C1 keeps global dominance disabled until the combinatorial separator remapping is separately validated.");
    }
    if (strengthening_options.use_conditional_zero_benefit_fixing) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C1 does not combine with conditional zero-benefit fixing.");
    }
}

void validate_fpp_phase6c2a_weighted_combinatorial_integer_mode(
    const FppCombinatorialBendersOptions& options,
    bool use_root_user_cuts,
    bool use_lifted_lower_bounds,
    const FppStrengtheningOptions& strengthening_options) {
    if (!options.enabled) {
        return;
    }
    if (!is_fpp_phase6c2a_weighted_combinatorial_integer_mode(options)) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2A supports only integer incumbent cuts with lift_mode=none|heuristic|posterior, scenario_order=eta-asc, cut_sampling_ratio=1, separate_fractional=false, and initial_cuts=false.");
    }
    if (use_root_user_cuts) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2A does not combine with root user cuts.");
    }
    if (use_lifted_lower_bounds ||
        strengthening_options.use_coverage_llbi ||
        strengthening_options.use_path_llbi ||
        strengthening_options.use_projected_coverage_llbi_exp ||
        strengthening_options.use_projected_path_llbi_exp ||
        strengthening_options.use_projected_coverage_llbi_poly ||
        strengthening_options.use_projected_path_llbi_poly) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2A does not combine with LLBI or projected LLBI families.");
    }
    if (strengthening_options.use_global_dominance_preprocessing) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2A keeps global dominance disabled until the combinatorial separator remapping is separately validated.");
    }
    if (strengthening_options.use_conditional_zero_benefit_fixing) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2A does not combine with conditional zero-benefit fixing.");
    }
}

void validate_fpp_phase6c2b_weighted_combinatorial_mode(
    const FppCombinatorialBendersOptions& options,
    bool use_root_user_cuts,
    bool use_lifted_lower_bounds,
    const FppStrengtheningOptions& strengthening_options) {
    if (!options.enabled) {
        return;
    }
    if (!is_fpp_phase6c2b_weighted_combinatorial_mode(options)) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2B supports only lift_mode=none|heuristic|posterior, scenario_order=eta-asc, and cut_sampling_ratio=1. Initial binary cuts and fractional path user cuts may be enabled, but scenario sampling and eta-desc remain disabled.");
    }
    if (use_root_user_cuts) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2B does not combine with LP-dual root user cuts; the repository has no separate combinatorial root-only cut mechanism in this phase.");
    }
    if (use_lifted_lower_bounds ||
        strengthening_options.use_coverage_llbi ||
        strengthening_options.use_path_llbi ||
        strengthening_options.use_projected_coverage_llbi_exp ||
        strengthening_options.use_projected_path_llbi_exp ||
        strengthening_options.use_projected_coverage_llbi_poly ||
        strengthening_options.use_projected_path_llbi_poly) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2B does not combine with LLBI or projected LLBI families.");
    }
    if (strengthening_options.use_global_dominance_preprocessing) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2B keeps global dominance disabled with combinatorial separation.");
    }
    if (strengthening_options.use_conditional_zero_benefit_fixing) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2B does not combine with conditional zero-benefit fixing.");
    }
}

void validate_fpp_phase6c2c_weighted_combinatorial_mode(
    const FppCombinatorialBendersOptions& options,
    bool use_root_user_cuts,
    bool use_lifted_lower_bounds,
    const FppStrengtheningOptions& strengthening_options) {
    if (!options.enabled) {
        return;
    }
    if (!is_fpp_phase6c2c_weighted_combinatorial_mode(options)) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2C supports lift_mode=none|heuristic|posterior, scenario_order=eta-asc|eta-desc, and cut_sampling_ratio in (0,1] with exact sampling-first fallback.");
    }
    validate_fpp_combinatorial_benders_options(options);
    if (use_root_user_cuts) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2C does not combine with LP-dual root user cuts; the repository has no separate combinatorial root-only cut mechanism in this phase.");
    }
    if (use_lifted_lower_bounds ||
        strengthening_options.use_coverage_llbi ||
        strengthening_options.use_path_llbi ||
        strengthening_options.use_projected_coverage_llbi_exp ||
        strengthening_options.use_projected_path_llbi_exp ||
        strengthening_options.use_projected_coverage_llbi_poly ||
        strengthening_options.use_projected_path_llbi_poly) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2C does not combine with LLBI or projected LLBI families.");
    }
    if (strengthening_options.use_global_dominance_preprocessing) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2C keeps global dominance disabled with combinatorial separation.");
    }
    if (strengthening_options.use_conditional_zero_benefit_fixing) {
        throw std::runtime_error(
            "Non-homogeneous weighted FPP combinatorial Benders Phase 6C2C does not combine with conditional zero-benefit fixing.");
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
    compact_weights_ = compact_weights_or_unit(opt_);
    weighted_ = has_nonunit_weights(compact_weights_);
    weight_map_hash_ = opt_.cell_weight_map.deterministic_hash;
    validity_mode_ = weighted_
        ? "weighted-baseline-integer-path-activation-cut"
        : "unit-baseline-integer-path-activation-cut";

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
    const auto propagation_start = std::chrono::steady_clock::now();
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

    separated.propagation_time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - propagation_start).count();

    const auto cut_build_start = std::chrono::steady_clock::now();
    std::unordered_map<int, double> baseline_coefficient_counts;
    std::unordered_map<int, double> lifted_coefficient_counts;
    int active_nodes = 0;
    int paths = 0;
    double active_loss = 0.0;
    for (int node = 0; node < node_count_; ++node) {
        if (!(dist[static_cast<std::size_t>(node)] < 1.0 - tolerance)) {
            continue;
        }
        ++active_nodes;
        ++paths;
        const double destination_weight = compact_weights_[static_cast<std::size_t>(node)];
        if (!std::isfinite(destination_weight) || destination_weight <= 0.0) {
            throw std::runtime_error(
                "FPP combinatorial Benders encountered an invalid compact destination weight.");
        }
        active_loss += destination_weight;
        std::set<int> blockers_on_path;
        int current = node;
        int guard = 0;
        while (current != root && current >= 0 && guard <= node_count_) {
            if (eligible_[static_cast<std::size_t>(current)] != 0) {
                baseline_coefficient_counts[current] += destination_weight;
                blockers_on_path.insert(current);
            }
            current = parent[static_cast<std::size_t>(current)];
            ++guard;
        }
        if (lift_mode != FppCombinatorialBendersLiftMode::None) {
            for (const int blocker : blockers_on_path) {
                lifted_coefficient_counts[blocker] += destination_weight;
            }
        }
    }

    const auto baseline_cut = make_combinatorial_cut_from_counts(
        scenario.scenario_id,
        active_loss,
        baseline_coefficient_counts,
        opt_,
        y_values_by_eligible_position);
    auto lifted_cut = lift_mode == FppCombinatorialBendersLiftMode::None
        ? baseline_cut
        : make_combinatorial_cut_from_counts(
              scenario.scenario_id,
              active_loss,
              lifted_coefficient_counts,
              opt_,
              y_values_by_eligible_position);
    const auto lifting_start = std::chrono::steady_clock::now();
    separated.baseline_nonzeros =
        static_cast<int>(baseline_cut.coefficients_by_compact_index.size());
    separated.lifted_nonzeros =
        static_cast<int>(lifted_cut.coefficients_by_compact_index.size());
    separated.lifting_attempted =
        lift_mode != FppCombinatorialBendersLiftMode::None && !fractional;
    separated.lifting_success = separated.lifting_attempted;
    separated.candidates_considered_for_lifting = separated.lifting_attempted
        ? separated.baseline_nonzeros
        : 0;
    if (separated.lifting_attempted) {
        for (const auto& [compact_node, baseline_coefficient] :
             baseline_cut.coefficients_by_compact_index) {
            const double lifted_coefficient =
                coefficient_for_node(lifted_cut, compact_node);
            const double change = lifted_coefficient - baseline_coefficient;
            if (change < -1.0e-9) {
                separated.lifted_dominates_baseline = false;
                separated.lifting_failure = true;
                separated.lifting_success = false;
            }
            if (std::fabs(change) > 1.0e-9) {
                ++separated.coefficients_changed;
                separated.max_coefficient_change =
                    std::max(separated.max_coefficient_change, std::fabs(change));
            }
        }
        for (const auto& [compact_node, lifted_coefficient] :
             lifted_cut.coefficients_by_compact_index) {
            const double baseline_coefficient =
                coefficient_for_node(baseline_cut, compact_node);
            if (lifted_coefficient < baseline_coefficient - 1.0e-9) {
                separated.lifted_dominates_baseline = false;
                separated.lifting_failure = true;
                separated.lifting_success = false;
            }
        }
    }
    separated.lifting_time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - lifting_start).count();
    separated.cut_build_time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - cut_build_start).count();
    separated.baseline_cut = baseline_cut;
    separated.cut = std::move(lifted_cut);
    separated.baseline_rhs_at_ybar = separated.baseline_cut.evaluateAt(y_compact);
    separated.lifted_rhs_at_ybar = separated.cut.evaluateAt(y_compact);
    separated.rhs_at_ybar = separated.cut.evaluateAt(y_compact);
    separated.incumbent_weighted_loss = active_loss;
    separated.incumbent_eta = eta_value;
    separated.violation = separated.rhs_at_ybar - eta_value;
    separated.baseline_tightness_error =
        std::fabs(separated.baseline_rhs_at_ybar - active_loss);
    separated.lifted_tightness_error =
        std::fabs(separated.lifted_rhs_at_ybar - active_loss);
    separated.tightness_error = std::fabs(separated.rhs_at_ybar - active_loss);
    separated.active_nodes = active_nodes;
    separated.activation_paths = paths;
    separated.nonzeros =
        static_cast<int>(separated.cut.coefficients_by_compact_index.size());
    if (separated.lifting_failure) {
        throw std::runtime_error(
            "FPP combinatorial Benders Phase 6C2A lifting produced a coefficient weaker than the baseline dominance check allows.");
    }
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
    const auto ordering_start = std::chrono::steady_clock::now();
    std::vector<int> scenario_ids;
    scenario_ids.reserve(opt_.scenarios.size());
    for (const auto& scenario : opt_.scenarios) {
        scenario_ids.push_back(scenario.scenario_id);
    }
    const std::vector<int> order =
        order_fpp_combinatorial_scenarios_by_eta(
            eta_values_by_scenario,
            scenario_ids,
            scenario_order);
    summary.ordering_time_sec =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - ordering_start).count();

    const auto sampling_start = std::chrono::steady_clock::now();
    const int sample_size = fpp_combinatorial_realized_sample_size(
        opt_.scenarios.size(),
        cut_sampling_ratio);
    summary.realized_sample_size = sample_size;
    summary.sampling_exact_fallback = cut_sampling_ratio < 1.0 - 1.0e-12;
    summary.scenario_policy_exact = true;
    summary.scenario_policy_heuristic = false;
    summary.full_verification_before_acceptance = true;
    summary.sampling_time_sec =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - sampling_start).count();

    auto evaluate_one = [&](int scenario_position, bool fallback) {
        ++summary.scenarios_checked;
        if (fallback) {
            ++summary.fallback_scenarios_evaluated;
        } else {
            ++summary.initial_sample_scenarios_evaluated;
        }
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
        summary.propagation_time_sec += cut.propagation_time_sec;
        summary.cut_build_time_sec += cut.cut_build_time_sec;
        ++summary.weighted_recourse_evaluations;
        summary.max_tightness_error =
            std::max(summary.max_tightness_error, cut.tightness_error);
        summary.max_baseline_tightness_error =
            std::max(summary.max_baseline_tightness_error, cut.baseline_tightness_error);
        summary.max_lifted_tightness_error =
            std::max(summary.max_lifted_tightness_error, cut.lifted_tightness_error);
        summary.baseline_cut_nonzeros += cut.baseline_nonzeros;
        summary.lifted_cut_nonzeros += cut.lifted_nonzeros;
        summary.lifting_time_sec += cut.lifting_time_sec;
        summary.candidates_considered_for_lifting +=
            cut.candidates_considered_for_lifting;
        summary.coefficients_changed_by_lifting += cut.coefficients_changed;
        summary.propagation_evaluations_for_lifting +=
            cut.propagation_evaluations_for_lifting;
        summary.max_coefficient_change =
            std::max(summary.max_coefficient_change, cut.max_coefficient_change);
        if (cut.lifting_attempted) {
            ++summary.lifting_attempts;
        }
        if (cut.lifting_success) {
            ++summary.lifting_successes;
        }
        if (cut.lifting_failure) {
            ++summary.lifting_failures;
        }
        if (cut.lifted_dominates_baseline) {
            ++summary.lifted_cuts_dominating_baseline;
        }
        if (cut.tightness_error <= std::max(1.0, std::fabs(cut.incumbent_weighted_loss)) * 1.0e-8) {
            ++summary.tight_cuts;
        }
        if (cut.violation > tolerance) {
            ++summary.violated_cuts;
            if (fallback) {
                ++summary.fallback_violations;
            } else {
                ++summary.sampled_violations;
            }
            summary.total_paths += cut.activation_paths;
            summary.total_nonzeros += cut.nonzeros;
            summary.cuts.push_back(std::move(cut));
        } else {
            ++summary.nonviolated_cuts;
        }
    };

    const int ordered_count = static_cast<int>(order.size());
    const int initial_count = std::min(sample_size, ordered_count);
    for (int pos = 0; pos < initial_count; ++pos) {
        evaluate_one(order[static_cast<std::size_t>(pos)], false);
    }
    if (summary.sampled_violations > 0) {
        summary.candidates_rejected_in_initial_sample = 1;
        summary.scenarios_skipped_after_candidate_rejection =
            ordered_count - summary.scenarios_checked;
    } else {
        for (int pos = initial_count; pos < ordered_count; ++pos) {
            evaluate_one(order[static_cast<std::size_t>(pos)], true);
        }
        if (summary.fallback_violations > 0) {
            summary.candidates_rejected_in_fallback = 1;
        } else {
            summary.candidates_fully_verified = 1;
        }
    }
    if (summary.scenarios_checked == ordered_count) {
        summary.candidate_full_sweeps = 1;
    }
    summary.scenarios_skipped =
        ordered_count - summary.scenarios_checked;
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
        double burned_loss = compact_weights_[static_cast<std::size_t>(root)];
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
                const double weight = compact_weights_[next_pos];
                if (!std::isfinite(weight) || weight <= 0.0) {
                    throw std::runtime_error(
                        "FPP combinatorial burned-loss evaluation encountered an invalid compact weight.");
                }
                burned_loss += weight;
                frontier.push(next);
            }
        }
        losses.push_back(burned_loss);
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
