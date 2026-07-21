#include "opt/WeightedDpvScoring.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace firebreak::opt {

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void fnv_append(std::uint64_t& hash, const std::string& text) {
    for (const unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kFnvPrime;
    }
}

std::string stable_double_string(double value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error("Weighted DPV cannot hash a nonfinite numeric value.");
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::scientific << std::setprecision(17) << value;
    return out.str();
}

std::string finish_hash(std::uint64_t hash) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

std::string hash_text(const std::string& text) {
    std::uint64_t hash = kFnvOffsetBasis;
    fnv_append(hash, text);
    return finish_hash(hash);
}

const DpvNodeIndexSet& node_set_for_index(
    const OptimizationScenario& scenario,
    int compact_index) {
    if (compact_index >= 0 && compact_index < static_cast<int>(scenario.dpv.node_sets.size())) {
        const auto& node_set = scenario.dpv.node_sets[static_cast<std::size_t>(compact_index)];
        if (node_set.node_index == compact_index) {
            return node_set;
        }
    }
    for (const auto& node_set : scenario.dpv.node_sets) {
        if (node_set.node_index == compact_index) {
            return node_set;
        }
    }
    throw std::runtime_error("Weighted DPV could not find a DPV node set for a candidate.");
}

std::vector<int> sorted_unique_candidates(
    std::vector<int> candidates,
    const OptimizationInstance& opt) {
    for (const int candidate : candidates) {
        if (candidate < 0 || candidate >= opt.node_mapper.size()) {
            throw std::runtime_error("Weighted DPV candidate compact index is out of range.");
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

void validate_common_input(
    const OptimizationInstance& opt,
    const std::vector<int>& candidates) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("Weighted DPV requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("Weighted DPV requires at least one scenario.");
    }
    if (candidates.empty()) {
        throw std::runtime_error("Weighted DPV requires at least one candidate.");
    }
    for (const auto& scenario : opt.scenarios) {
        if (!scenario.dpv.descendants_include_self || scenario.dpv.node_sets.empty()) {
            throw std::runtime_error(
                "Weighted DPV requires closed-reachability DPV node sets.");
        }
        if (scenario.ignition_index < 0 || scenario.ignition_index >= opt.node_mapper.size()) {
            throw std::runtime_error("Weighted DPV scenario ignition index is out of range.");
        }
        if (!std::isfinite(scenario.probability) || scenario.probability < 0.0) {
            throw std::runtime_error("Weighted DPV scenario probability must be finite and nonnegative.");
        }
    }
}

double probability_for_scenario(
    const WeightedDpvScenarioStructure& scenario,
    const WeightedDpvScoringOptions& options) {
    return options.use_scenario_probabilities ? scenario.probability : 1.0;
}

double variant_multiplier(
    const WeightedDpvCandidateStructure& candidate,
    WeightedDpvVariant variant) {
    switch (variant) {
        case WeightedDpvVariant::StaticClosedDescendants:
            return 1.0;
        case WeightedDpvVariant::StaticClosedDescendantsTimesOutDegree:
            return static_cast<double>(candidate.successor_count);
    }
    return 1.0;
}

std::string weight_hash_for_compact_weights(
    const OptimizationInstance& opt,
    const std::vector<double>& weights) {
    if (!opt.cell_weight_map.deterministic_hash.empty() &&
        opt.cell_weight_map.weight_by_original_cell_id.size() == weights.size() &&
        !opt.compact_cell_weights.empty() &&
        opt.compact_cell_weights.size() == weights.size()) {
        bool identical = true;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            if (weights[i] != opt.compact_cell_weights[i]) {
                identical = false;
                break;
            }
        }
        if (identical) {
            return opt.cell_weight_map.deterministic_hash;
        }
    }

    std::uint64_t hash = kFnvOffsetBasis;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        fnv_append(hash, std::to_string(opt.node_mapper.to_node(static_cast<int>(i))));
        fnv_append(hash, ",");
        fnv_append(hash, stable_double_string(weights[i]));
        fnv_append(hash, "\n");
    }
    return finish_hash(hash);
}

std::string weight_profile_for_compact_weights(
    const OptimizationInstance& opt,
    const std::vector<double>& weights) {
    if (!opt.compact_cell_weights.empty() && opt.compact_cell_weights.size() == weights.size()) {
        bool identical = true;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            if (weights[i] != opt.compact_cell_weights[i]) {
                identical = false;
                break;
            }
        }
        if (identical &&
            !opt.cell_weight_map.profile.empty() &&
            opt.cell_weight_map.weight_by_original_cell_id.size() == weights.size()) {
            return opt.cell_weight_map.profile;
        }
    }
    bool all_unit = true;
    for (const double weight : weights) {
        if (std::fabs(weight - 1.0) > 1.0e-12) {
            all_unit = false;
            break;
        }
    }
    return all_unit ? "homogeneous" : "provided_compact_weights";
}

void validate_structural_data_matches(
    const OptimizationInstance& opt,
    const WeightedDpvStructuralData& structural_data,
    const WeightedDpvScoringOptions& options) {
    if (structural_data.cache_key.variant != weighted_dpv_variant_name(options.variant)) {
        throw std::runtime_error("Weighted DPV structural cache variant mismatch.");
    }
    if (structural_data.cache_key.ignition_policy != weighted_dpv_ignition_policy_name(options.ignition_policy)) {
        throw std::runtime_error("Weighted DPV structural cache ignition-policy mismatch.");
    }
    if (structural_data.cache_key.scenario_graph_hash != hash_weighted_dpv_scenario_graphs(opt)) {
        throw std::runtime_error("Weighted DPV structural cache scenario-graph mismatch.");
    }
    if (structural_data.scenarios.size() != opt.scenarios.size()) {
        throw std::runtime_error("Weighted DPV structural cache scenario-count mismatch.");
    }
    for (std::size_t s = 0; s < structural_data.scenarios.size(); ++s) {
        const auto& structural_scenario = structural_data.scenarios[s];
        const auto& opt_scenario = opt.scenarios[s];
        if (structural_scenario.scenario_id != opt_scenario.scenario_id ||
            structural_scenario.scenario_position != static_cast<int>(s)) {
            throw std::runtime_error("Weighted DPV structural cache scenario-order mismatch.");
        }
        if (options.use_scenario_probabilities &&
            structural_scenario.probability != opt_scenario.probability) {
            throw std::runtime_error("Weighted DPV structural cache scenario-probability mismatch.");
        }
    }
    std::vector<int> candidates;
    if (!structural_data.scenarios.empty()) {
        candidates.reserve(structural_data.scenarios.front().candidates.size());
        for (const auto& candidate : structural_data.scenarios.front().candidates) {
            candidates.push_back(candidate.compact_index);
        }
    }
    if (structural_data.cache_key.candidate_universe_hash !=
        hash_weighted_dpv_candidate_universe(opt, candidates)) {
        throw std::runtime_error("Weighted DPV structural cache candidate-universe mismatch.");
    }
}

}  // namespace

std::string weighted_dpv_variant_name(WeightedDpvVariant variant) {
    switch (variant) {
        case WeightedDpvVariant::StaticClosedDescendants:
            return "static_closed_descendants";
        case WeightedDpvVariant::StaticClosedDescendantsTimesOutDegree:
            return "static_closed_descendants_times_out_degree";
    }
    return "unknown";
}

std::string weighted_dpv_ignition_policy_name(WeightedDpvIgnitionPolicy policy) {
    switch (policy) {
        case WeightedDpvIgnitionPolicy::LegacyIncludeReachable:
            return "legacy_include_reachable";
        case WeightedDpvIgnitionPolicy::FppIgnitionNoProtection:
            return "fpp_ignition_no_protection";
    }
    return "unknown";
}

WeightedDpvIgnitionPolicy parse_weighted_dpv_ignition_policy(const std::string& value) {
    std::string key;
    key.reserve(value.size());
    for (const char ch : value) {
        if (ch == '-' || ch == '_' || std::isspace(static_cast<unsigned char>(ch))) {
            continue;
        }
        key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (key == "fppsafe" || key == "fppignitionnoprotection") {
        return WeightedDpvIgnitionPolicy::FppIgnitionNoProtection;
    }
    if (key == "legacy" || key == "legacyincludereachable") {
        return WeightedDpvIgnitionPolicy::LegacyIncludeReachable;
    }
    throw std::runtime_error(
        "Invalid DPV ignition policy '" + value + "'. Supported values: fpp-safe, legacy.");
}

std::string weighted_dpv_structural_definition(const WeightedDpvScoringOptions& options) {
    std::string definition = "closed directed descendants from DpvIndexBuilder";
    if (options.variant == WeightedDpvVariant::StaticClosedDescendantsTimesOutDegree) {
        definition += "; scenario score multiplies descendant value by candidate out-degree";
    }
    if (options.ignition_policy == WeightedDpvIgnitionPolicy::FppIgnitionNoProtection) {
        definition += "; ignition candidate valued set is empty under FPP no-protection convention";
    } else {
        definition += "; ignition candidate uses the legacy closed-descendant set";
    }
    return definition;
}

std::vector<double> canonical_compact_dpv_weights_or_unit(const OptimizationInstance& opt) {
    const int node_count = opt.node_mapper.size();
    if (node_count <= 0) {
        throw std::runtime_error("Weighted DPV cannot build weights without mapped nodes.");
    }
    std::vector<double> weights;
    if (opt.compact_cell_weights.empty()) {
        weights.assign(static_cast<std::size_t>(node_count), 1.0);
    } else if (opt.compact_cell_weights.size() == static_cast<std::size_t>(node_count)) {
        weights = opt.compact_cell_weights;
    } else {
        throw std::runtime_error("Weighted DPV compact weight vector size does not match the mapped node count.");
    }

    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (!std::isfinite(weights[i]) || weights[i] <= 0.0) {
            throw std::runtime_error("Weighted DPV requires finite strictly positive compact cell weights.");
        }
    }
    return weights;
}

std::string weighted_dpv_weight_profile(
    const OptimizationInstance& opt,
    const std::vector<double>& compact_weights) {
    return weight_profile_for_compact_weights(opt, compact_weights);
}

std::string weighted_dpv_weight_map_hash(
    const OptimizationInstance& opt,
    const std::vector<double>& compact_weights) {
    return weight_hash_for_compact_weights(opt, compact_weights);
}

WeightedDpvStructuralData build_weighted_dpv_structural_data(
    const OptimizationInstance& opt,
    const std::vector<int>& candidate_compact_indices,
    const WeightedDpvScoringOptions& options) {
    const auto start = std::chrono::steady_clock::now();
    const auto candidates = sorted_unique_candidates(candidate_compact_indices, opt);
    validate_common_input(opt, candidates);

    WeightedDpvStructuralData result;
    result.scenarios.reserve(opt.scenarios.size());
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const auto& scenario = opt.scenarios[s];
        WeightedDpvScenarioStructure scenario_data;
        scenario_data.scenario_id = scenario.scenario_id;
        scenario_data.scenario_position = static_cast<int>(s);
        scenario_data.ignition_index = scenario.ignition_index;
        scenario_data.ignition_original_node = scenario.ignition_original_node;
        scenario_data.probability = scenario.probability;
        scenario_data.candidates.reserve(candidates.size());

        for (const int candidate_index : candidates) {
            const auto& node_set = node_set_for_index(scenario, candidate_index);
            WeightedDpvCandidateStructure candidate_data;
            candidate_data.compact_index = candidate_index;
            candidate_data.original_node = opt.node_mapper.to_node(candidate_index);
            candidate_data.successor_count = static_cast<int>(node_set.successor_indices.size());
            if (options.ignition_policy == WeightedDpvIgnitionPolicy::FppIgnitionNoProtection &&
                candidate_index == scenario.ignition_index) {
                candidate_data.valued_node_indices.clear();
            } else {
                candidate_data.valued_node_indices = node_set.descendant_indices;
                std::sort(candidate_data.valued_node_indices.begin(), candidate_data.valued_node_indices.end());
                candidate_data.valued_node_indices.erase(
                    std::unique(candidate_data.valued_node_indices.begin(), candidate_data.valued_node_indices.end()),
                    candidate_data.valued_node_indices.end());
            }
            for (const int valued_index : candidate_data.valued_node_indices) {
                if (valued_index < 0 || valued_index >= opt.node_mapper.size()) {
                    throw std::runtime_error("Weighted DPV valued descendant index is out of range.");
                }
            }
            result.total_valued_incidence += candidate_data.valued_node_indices.size();
            scenario_data.candidates.push_back(std::move(candidate_data));
        }
        result.scenarios.push_back(std::move(scenario_data));
    }

    result.cache_key.variant = weighted_dpv_variant_name(options.variant);
    result.cache_key.ignition_policy = weighted_dpv_ignition_policy_name(options.ignition_policy);
    result.cache_key.structural_definition = weighted_dpv_structural_definition(options);
    result.cache_key.candidate_universe_hash = hash_weighted_dpv_candidate_universe(opt, candidates);
    result.cache_key.scenario_graph_hash = hash_weighted_dpv_scenario_graphs(opt);
    result.cache_key.implementation_version = options.implementation_version;
    result.cache_key.digest = hash_text(
        result.cache_key.variant + "|" +
        result.cache_key.ignition_policy + "|" +
        result.cache_key.structural_definition + "|" +
        result.cache_key.candidate_universe_hash + "|" +
        result.cache_key.scenario_graph_hash + "|" +
        result.cache_key.implementation_version);

    const auto end = std::chrono::steady_clock::now();
    result.precompute_time_sec = std::chrono::duration<double>(end - start).count();
    return result;
}

WeightedDpvScoreReport evaluate_weighted_dpv_scores(
    const OptimizationInstance& opt,
    const WeightedDpvStructuralData& structural_data,
    const std::vector<double>& compact_weights,
    const WeightedDpvScoringOptions& options) {
    const auto start = std::chrono::steady_clock::now();
    if (compact_weights.size() != static_cast<std::size_t>(opt.node_mapper.size())) {
        throw std::runtime_error("Weighted DPV compact weight vector size mismatch.");
    }
    for (const double weight : compact_weights) {
        if (!std::isfinite(weight) || weight <= 0.0) {
            throw std::runtime_error("Weighted DPV requires finite strictly positive compact cell weights.");
        }
    }
    validate_structural_data_matches(opt, structural_data, options);

    WeightedDpvScoreReport report;
    report.structural_data = structural_data;
    if (structural_data.scenarios.empty()) {
        throw std::runtime_error("Weighted DPV structural data has no scenarios.");
    }

    const auto& first_scenario = structural_data.scenarios.front();
    report.candidate_scores.reserve(first_scenario.candidates.size());
    for (const auto& candidate : first_scenario.candidates) {
        report.candidate_scores.push_back(WeightedDpvCandidateScore{
            candidate.compact_index,
            candidate.original_node,
            0.0,
            0.0,
            0,
        });
    }

    for (const auto& scenario : structural_data.scenarios) {
        if (scenario.candidates.size() != report.candidate_scores.size()) {
            throw std::runtime_error("Weighted DPV structural data has inconsistent candidate counts.");
        }
        const double probability = probability_for_scenario(scenario, options);
        for (std::size_t pos = 0; pos < scenario.candidates.size(); ++pos) {
            const auto& candidate = scenario.candidates[pos];
            auto& aggregate = report.candidate_scores[pos];
            if (candidate.compact_index != aggregate.compact_index) {
                throw std::runtime_error("Weighted DPV structural data has inconsistent candidate ordering.");
            }

            double unweighted_value = static_cast<double>(candidate.valued_node_indices.size());
            double weighted_value = 0.0;
            for (const int valued_index : candidate.valued_node_indices) {
                weighted_value += compact_weights[static_cast<std::size_t>(valued_index)];
            }
            const double multiplier = variant_multiplier(candidate, options.variant);
            aggregate.unweighted_score += probability * multiplier * unweighted_value;
            aggregate.weighted_score += probability * multiplier * weighted_value;
        }
    }

    report.candidate_scores = rank_weighted_dpv_scores(std::move(report.candidate_scores));
    const auto end = std::chrono::steady_clock::now();
    const double eval_time = std::chrono::duration<double>(end - start).count();

    report.numerical_cache_key.structural_digest = structural_data.cache_key.digest;
    report.numerical_cache_key.weight_profile = weight_profile_for_compact_weights(opt, compact_weights);
    report.numerical_cache_key.weight_map_hash = weight_hash_for_compact_weights(opt, compact_weights);
    report.numerical_cache_key.probability_hash = hash_weighted_dpv_probabilities(opt);
    report.numerical_cache_key.normalization_mode =
        options.use_scenario_probabilities ? "scenario_probability_weighted_sum" : "raw_scenario_sum";
    report.numerical_cache_key.implementation_version = options.implementation_version;
    report.numerical_cache_key.digest = hash_text(
        report.numerical_cache_key.structural_digest + "|" +
        report.numerical_cache_key.weight_profile + "|" +
        report.numerical_cache_key.weight_map_hash + "|" +
        report.numerical_cache_key.probability_hash + "|" +
        report.numerical_cache_key.normalization_mode + "|" +
        report.numerical_cache_key.implementation_version);

    report.diagnostics.dpv_variant = weighted_dpv_variant_name(options.variant);
    report.diagnostics.dpv_weighted = true;
    report.diagnostics.dpv_structural_definition = weighted_dpv_structural_definition(options);
    report.diagnostics.dpv_weight_profile = report.numerical_cache_key.weight_profile;
    report.diagnostics.dpv_weight_map_hash = report.numerical_cache_key.weight_map_hash;
    report.diagnostics.dpv_scenarios = static_cast<int>(structural_data.scenarios.size());
    report.diagnostics.dpv_candidates = static_cast<int>(report.candidate_scores.size());
    report.diagnostics.dpv_structural_sets_computed =
        structural_data.scenarios.size() * first_scenario.candidates.size();
    report.diagnostics.dpv_total_valued_incidence = structural_data.total_valued_incidence;
    report.diagnostics.dpv_precompute_time_sec = structural_data.precompute_time_sec;
    report.diagnostics.dpv_weighted_evaluation_time_sec = eval_time;

    if (!report.candidate_scores.empty()) {
        report.diagnostics.dpv_score_min = std::numeric_limits<double>::infinity();
        report.diagnostics.dpv_score_max = -std::numeric_limits<double>::infinity();
        double total = 0.0;
        for (const auto& score : report.candidate_scores) {
            report.diagnostics.dpv_score_min = std::min(report.diagnostics.dpv_score_min, score.weighted_score);
            report.diagnostics.dpv_score_max = std::max(report.diagnostics.dpv_score_max, score.weighted_score);
            total += score.weighted_score;
            if (std::fabs(score.weighted_score) <= 1.0e-12) {
                ++report.diagnostics.dpv_zero_score_candidates;
            }
        }
        report.diagnostics.dpv_score_mean = total / static_cast<double>(report.candidate_scores.size());
    }

    return report;
}

WeightedDpvScoreReport build_weighted_dpv_score_report(
    const OptimizationInstance& opt,
    const std::vector<int>& candidate_compact_indices,
    const WeightedDpvScoringOptions& options) {
    const auto structural_data = build_weighted_dpv_structural_data(
        opt,
        candidate_compact_indices,
        options);
    return evaluate_weighted_dpv_scores(
        opt,
        structural_data,
        canonical_compact_dpv_weights_or_unit(opt),
        options);
}

std::vector<WeightedDpvCandidateScore> rank_weighted_dpv_scores(
    std::vector<WeightedDpvCandidateScore> scores,
    double tolerance) {
    if (tolerance < 0.0 || !std::isfinite(tolerance)) {
        throw std::runtime_error("Weighted DPV ranking tolerance must be finite and nonnegative.");
    }
    for (const auto& score : scores) {
        if (!std::isfinite(score.weighted_score) || !std::isfinite(score.unweighted_score)) {
            throw std::runtime_error("Weighted DPV ranking cannot order nonfinite scores.");
        }
    }
    std::sort(scores.begin(), scores.end(), [tolerance](
        const WeightedDpvCandidateScore& lhs,
        const WeightedDpvCandidateScore& rhs) {
        if (std::fabs(lhs.weighted_score - rhs.weighted_score) > tolerance) {
            return lhs.weighted_score > rhs.weighted_score;
        }
        if (lhs.original_node != rhs.original_node) {
            return lhs.original_node < rhs.original_node;
        }
        return lhs.compact_index < rhs.compact_index;
    });
    for (std::size_t pos = 0; pos < scores.size(); ++pos) {
        scores[pos].rank = static_cast<int>(pos + 1);
    }
    return scores;
}

std::string hash_weighted_dpv_candidate_universe(
    const OptimizationInstance& opt,
    const std::vector<int>& candidate_compact_indices) {
    const auto candidates = sorted_unique_candidates(candidate_compact_indices, opt);
    std::uint64_t hash = kFnvOffsetBasis;
    for (const int candidate : candidates) {
        fnv_append(hash, std::to_string(candidate));
        fnv_append(hash, ":");
        fnv_append(hash, std::to_string(opt.node_mapper.to_node(candidate)));
        fnv_append(hash, "\n");
    }
    return finish_hash(hash);
}

std::string hash_weighted_dpv_scenario_graphs(const OptimizationInstance& opt) {
    std::uint64_t hash = kFnvOffsetBasis;
    fnv_append(hash, std::to_string(opt.node_mapper.size()));
    fnv_append(hash, "\n");
    for (const auto& scenario : opt.scenarios) {
        fnv_append(hash, "scenario=");
        fnv_append(hash, std::to_string(scenario.scenario_id));
        fnv_append(hash, ";ignition=");
        fnv_append(hash, std::to_string(scenario.ignition_index));
        fnv_append(hash, ";arcs=");
        std::vector<std::pair<int, int>> arcs;
        arcs.reserve(scenario.arcs.size());
        for (const auto& arc : scenario.arcs) {
            arcs.push_back({arc.u_index, arc.v_index});
        }
        std::sort(arcs.begin(), arcs.end());
        for (const auto& arc : arcs) {
            fnv_append(hash, std::to_string(arc.first));
            fnv_append(hash, ">");
            fnv_append(hash, std::to_string(arc.second));
            fnv_append(hash, ",");
        }
        fnv_append(hash, "\n");
    }
    return finish_hash(hash);
}

std::string hash_weighted_dpv_probabilities(const OptimizationInstance& opt) {
    std::uint64_t hash = kFnvOffsetBasis;
    for (const auto& scenario : opt.scenarios) {
        if (!std::isfinite(scenario.probability) || scenario.probability < 0.0) {
            throw std::runtime_error("Weighted DPV cannot hash invalid scenario probabilities.");
        }
        fnv_append(hash, std::to_string(scenario.scenario_id));
        fnv_append(hash, ":");
        fnv_append(hash, stable_double_string(scenario.probability));
        fnv_append(hash, "\n");
    }
    return finish_hash(hash);
}

}  // namespace firebreak::opt
