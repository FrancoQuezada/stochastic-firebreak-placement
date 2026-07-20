#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "opt/OptimizationInstance.hpp"

namespace firebreak::benders {

struct FppStrengtheningOptions {
    bool use_coverage_llbi = false;
    bool use_path_llbi = false;
    int path_llbi_max_paths_per_node = 8;
    bool use_projected_coverage_llbi_exp = false;
    bool use_projected_path_llbi_exp = false;
    bool use_projected_coverage_llbi_poly = false;
    bool use_projected_path_llbi_poly = false;
    int projected_llbi_root_rounds = 3;
    int projected_llbi_max_cuts_per_round = 100;
    double projected_llbi_violation_tolerance = 1.0e-6;
    int projected_llbi_cut_density_limit = 0;
    int projected_poly_max_cuts = 100000;
    bool use_global_dominance_preprocessing = false;
    bool use_conditional_zero_benefit_fixing = false;
    std::filesystem::path coverage_llbi_export_path;
    std::filesystem::path path_llbi_export_path;
    std::filesystem::path dominance_preprocessing_export_path;
    std::filesystem::path conditional_fixing_log_export_path;
    std::filesystem::path projected_llbi_export_cuts_path;
};

struct FppCoverageLlbiNodeRecord {
    int compact_node = -1;
    std::vector<int> covering_candidate_compact_nodes;
};

struct FppCoverageLlbiScenarioRecord {
    int scenario_index = -1;
    int scenario_id = 0;
    double empty_burned_area = 0.0;
    std::vector<FppCoverageLlbiNodeRecord> nodes;
};

struct FppCoverageLlbiData {
    bool enabled = false;
    int num_zeta_vars = 0;
    int num_constraints = 0;
    double precompute_time_sec = 0.0;
    std::vector<FppCoverageLlbiScenarioRecord> scenarios;
};

struct FppPathLlbiPathRecord {
    std::vector<int> blocking_candidate_compact_nodes;
};

struct FppPathLlbiNodeRecord {
    int compact_node = -1;
    std::vector<FppPathLlbiPathRecord> paths;
};

struct FppPathLlbiScenarioRecord {
    int scenario_index = -1;
    int scenario_id = 0;
    std::vector<FppPathLlbiNodeRecord> nodes;
};

struct FppPathLlbiData {
    bool enabled = false;
    int num_b_vars = 0;
    int num_path_constraints = 0;
    int num_paths_used = 0;
    double precompute_time_sec = 0.0;
    std::vector<FppPathLlbiScenarioRecord> scenarios;
    std::vector<std::string> notes;
};

struct FppDominancePreprocessingResult {
    bool enabled = false;
    bool structural_weight_safe = false;
    opt::OptimizationInstance reduced_instance;
    int original_candidate_count = 0;
    int candidates_removed = 0;
    int equivalence_classes = 0;
    int post_candidate_count = 0;
    int warm_start_replacements = 0;
    double precompute_time_sec = 0.0;
    std::vector<int> kept_candidate_compact_nodes;
    std::vector<int> removed_candidate_compact_nodes;
    std::vector<std::pair<int, int>> removed_by_dominator;
    std::vector<std::string> notes;
};

struct FppConditionalZeroBenefitResult {
    bool enabled = false;
    bool structural_weight_safe = false;
    int callback_calls = 0;
    int nodes_checked = 0;
    int candidates_checked = 0;
    int fixings_attempted = 0;
    int fixings_applied = 0;
    int variables_fixed_zero = 0;
    int scenarios_reachability_computed = 0;
    double time_sec = 0.0;
    std::vector<int> zero_benefit_candidate_compact_nodes;
    std::vector<std::string> notes;
};

namespace detail {

inline void ensure_node_in_range(int node, int node_count, const std::string& context) {
    if (node < 0 || node >= node_count) {
        throw std::runtime_error(context + " compact node index is out of range.");
    }
}

inline std::vector<std::vector<int>> scenario_successors(
    const opt::OptimizationScenario& scenario,
    int node_count) {
    std::vector<std::vector<int>> successors(static_cast<std::size_t>(node_count));
    ensure_node_in_range(scenario.ignition_index, node_count, "Ignition");
    for (const auto& arc : scenario.arcs) {
        ensure_node_in_range(arc.u_index, node_count, "Arc source");
        ensure_node_in_range(arc.v_index, node_count, "Arc target");
        successors[static_cast<std::size_t>(arc.u_index)].push_back(arc.v_index);
    }
    for (auto& items : successors) {
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
    }
    return successors;
}

inline std::vector<int> reachable_from(
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

inline std::vector<char> reachable_after_fixed_firebreaks(
    const std::vector<std::vector<int>>& successors,
    int root,
    const std::vector<char>& fixed_selected) {
    std::vector<char> reached(successors.size(), 0);
    std::queue<int> frontier;
    reached[static_cast<std::size_t>(root)] = 1;
    frontier.push(root);
    while (!frontier.empty()) {
        const int current = frontier.front();
        frontier.pop();
        for (const int next : successors[static_cast<std::size_t>(current)]) {
            const auto next_pos = static_cast<std::size_t>(next);
            if (reached[next_pos]) {
                continue;
            }
            reached[next_pos] = 1;
            if (next != root && fixed_selected[next_pos]) {
                continue;
            }
            frontier.push(next);
        }
    }
    return reached;
}

inline std::vector<char> eligible_mask(const opt::OptimizationInstance& opt) {
    std::vector<char> eligible(static_cast<std::size_t>(opt.node_mapper.size()), 0);
    for (const int compact_node : opt.eligible_indices) {
        ensure_node_in_range(compact_node, opt.node_mapper.size(), "Eligible firebreak");
        eligible[static_cast<std::size_t>(compact_node)] = 1;
    }
    return eligible;
}

inline std::vector<int> unique_nodes_from_scenario(
    const opt::OptimizationScenario& scenario,
    const std::vector<int>& root_reachable) {
    std::vector<int> nodes = root_reachable;
    nodes.push_back(scenario.ignition_index);
    for (const int node : scenario.observed_node_indices) {
        nodes.push_back(node);
    }
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    return nodes;
}

inline void enumerate_paths_dfs(
    int current,
    int target,
    const std::vector<std::vector<int>>& successors,
    const std::vector<char>& eligible,
    int root,
    int max_paths,
    std::vector<char>& on_path,
    std::vector<int>& current_blockers,
    std::vector<FppPathLlbiPathRecord>& out_paths) {
    if (static_cast<int>(out_paths.size()) >= max_paths) {
        return;
    }
    if (current == target) {
        out_paths.push_back(FppPathLlbiPathRecord{current_blockers});
        return;
    }
    on_path[static_cast<std::size_t>(current)] = 1;
    for (const int next : successors[static_cast<std::size_t>(current)]) {
        if (on_path[static_cast<std::size_t>(next)]) {
            continue;
        }
        const bool next_blocks = next != root && eligible[static_cast<std::size_t>(next)] != 0;
        if (next_blocks) {
            current_blockers.push_back(next);
        }
        enumerate_paths_dfs(
            next,
            target,
            successors,
            eligible,
            root,
            max_paths,
            on_path,
            current_blockers,
            out_paths);
        if (next_blocks) {
            current_blockers.pop_back();
        }
        if (static_cast<int>(out_paths.size()) >= max_paths) {
            break;
        }
    }
    on_path[static_cast<std::size_t>(current)] = 0;
}

inline std::vector<FppPathLlbiPathRecord> enumerate_capped_paths(
    const std::vector<std::vector<int>>& successors,
    const std::vector<char>& eligible,
    int root,
    int target,
    int max_paths) {
    std::vector<FppPathLlbiPathRecord> paths;
    if (max_paths <= 0) {
        return paths;
    }
    if (target == root) {
        paths.push_back(FppPathLlbiPathRecord{});
        return paths;
    }
    std::vector<char> on_path(successors.size(), 0);
    std::vector<int> current_blockers;
    enumerate_paths_dfs(
        root,
        target,
        successors,
        eligible,
        root,
        max_paths,
        on_path,
        current_blockers,
        paths);
    return paths;
}

inline std::map<int, std::set<int>> scenario_dominator_sets(
    const opt::OptimizationInstance& opt,
    const opt::OptimizationScenario& scenario) {
    const int node_count = opt.node_mapper.size();
    const auto successors = scenario_successors(scenario, node_count);
    const auto reachable = reachable_from(successors, scenario.ignition_index);
    std::unordered_map<int, int> local_by_node;
    for (std::size_t pos = 0; pos < reachable.size(); ++pos) {
        local_by_node[reachable[pos]] = static_cast<int>(pos);
    }
    std::vector<std::vector<int>> predecessors(reachable.size());
    for (std::size_t local = 0; local < reachable.size(); ++local) {
        const int u = reachable[local];
        for (const int v : successors[static_cast<std::size_t>(u)]) {
            const auto v_it = local_by_node.find(v);
            if (v_it != local_by_node.end()) {
                predecessors[static_cast<std::size_t>(v_it->second)].push_back(static_cast<int>(local));
            }
        }
    }

    std::set<int> all_local;
    for (int i = 0; i < static_cast<int>(reachable.size()); ++i) {
        all_local.insert(i);
    }

    const int root_local = local_by_node.at(scenario.ignition_index);
    std::vector<std::set<int>> dominators(reachable.size(), all_local);
    dominators[static_cast<std::size_t>(root_local)] = {root_local};
    bool changed = true;
    while (changed) {
        changed = false;
        for (int local = 0; local < static_cast<int>(reachable.size()); ++local) {
            if (local == root_local) {
                continue;
            }
            std::set<int> next;
            const auto& preds = predecessors[static_cast<std::size_t>(local)];
            if (!preds.empty()) {
                next = dominators[static_cast<std::size_t>(preds.front())];
                for (std::size_t p = 1; p < preds.size(); ++p) {
                    std::set<int> intersection;
                    std::set_intersection(
                        next.begin(),
                        next.end(),
                        dominators[static_cast<std::size_t>(preds[p])].begin(),
                        dominators[static_cast<std::size_t>(preds[p])].end(),
                        std::inserter(intersection, intersection.begin()));
                    next = std::move(intersection);
                }
            }
            next.insert(local);
            if (next != dominators[static_cast<std::size_t>(local)]) {
                dominators[static_cast<std::size_t>(local)] = std::move(next);
                changed = true;
            }
        }
    }

    const auto eligible = eligible_mask(opt);
    std::map<int, std::set<int>> dominated_nodes_by_candidate;
    for (const int candidate : opt.eligible_indices) {
        std::set<int> dominated;
        if (candidate != scenario.ignition_index) {
            const auto candidate_it = local_by_node.find(candidate);
            if (candidate_it != local_by_node.end()) {
                const int candidate_local = candidate_it->second;
                for (std::size_t local = 0; local < reachable.size(); ++local) {
                    if (static_cast<int>(local) == root_local) {
                        continue;
                    }
                    if (eligible[static_cast<std::size_t>(candidate)] &&
                        dominators[local].find(candidate_local) != dominators[local].end()) {
                        dominated.insert(reachable[local]);
                    }
                }
            }
        }
        dominated_nodes_by_candidate[candidate] = std::move(dominated);
    }
    return dominated_nodes_by_candidate;
}

inline bool subset_of(const std::set<int>& lhs, const std::set<int>& rhs) {
    return std::includes(rhs.begin(), rhs.end(), lhs.begin(), lhs.end());
}

inline int original_node_for_compact(
    const opt::OptimizationInstance& opt,
    int compact_node) {
    ensure_node_in_range(compact_node, opt.node_mapper.size(), "Dominance candidate");
    return opt.node_mapper.to_node(compact_node);
}

inline bool candidate_identity_less(
    const opt::OptimizationInstance& opt,
    int lhs,
    int rhs) {
    const int lhs_original = original_node_for_compact(opt, lhs);
    const int rhs_original = original_node_for_compact(opt, rhs);
    if (lhs_original != rhs_original) {
        return lhs_original < rhs_original;
    }
    return lhs < rhs;
}

inline std::string set_signature(const std::set<int>& values) {
    std::ostringstream out;
    bool first = true;
    for (const int value : values) {
        if (!first) {
            out << ";";
        }
        first = false;
        out << value;
    }
    return out.str();
}

}  // namespace detail

inline FppCoverageLlbiData build_fpp_coverage_llbi_data(
    const opt::OptimizationInstance& opt,
    bool enabled) {
    FppCoverageLlbiData data;
    data.enabled = enabled;
    if (!enabled) {
        return data;
    }
    const auto start = std::chrono::steady_clock::now();
    const int node_count = opt.node_mapper.size();
    const auto eligible = detail::eligible_mask(opt);
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const auto& scenario = opt.scenarios[s];
        const auto successors = detail::scenario_successors(scenario, node_count);
        const auto root_reachable = detail::reachable_from(successors, scenario.ignition_index);
        std::vector<std::vector<int>> covering_candidates(static_cast<std::size_t>(node_count));
        for (const int candidate : opt.eligible_indices) {
            if (candidate == scenario.ignition_index) {
                continue;
            }
            const auto covered = detail::reachable_from(successors, candidate);
            for (const int node : covered) {
                covering_candidates[static_cast<std::size_t>(node)].push_back(candidate);
            }
        }

        FppCoverageLlbiScenarioRecord scenario_record;
        scenario_record.scenario_index = static_cast<int>(s);
        scenario_record.scenario_id = scenario.scenario_id;
        scenario_record.empty_burned_area = static_cast<double>(root_reachable.size());
        const auto nodes = detail::unique_nodes_from_scenario(scenario, root_reachable);
        for (const int node : nodes) {
            auto candidates = covering_candidates[static_cast<std::size_t>(node)];
            candidates.erase(
                std::remove_if(
                    candidates.begin(),
                    candidates.end(),
                    [&eligible](int candidate) {
                        return candidate < 0 ||
                               candidate >= static_cast<int>(eligible.size()) ||
                               !eligible[static_cast<std::size_t>(candidate)];
                    }),
                candidates.end());
            std::sort(candidates.begin(), candidates.end());
            candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
            if (candidates.empty()) {
                continue;
            }
            scenario_record.nodes.push_back(FppCoverageLlbiNodeRecord{node, std::move(candidates)});
            ++data.num_zeta_vars;
            ++data.num_constraints;
        }
        if (!scenario_record.nodes.empty()) {
            ++data.num_constraints;
        }
        data.scenarios.push_back(std::move(scenario_record));
    }
    data.precompute_time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return data;
}

inline FppPathLlbiData build_fpp_path_llbi_data(
    const opt::OptimizationInstance& opt,
    bool enabled,
    int max_paths_per_node) {
    FppPathLlbiData data;
    data.enabled = enabled;
    if (!enabled) {
        return data;
    }
    if (max_paths_per_node <= 0) {
        throw std::runtime_error("path_llbi_max_paths_per_node must be positive when path LLBI is enabled.");
    }
    const auto start = std::chrono::steady_clock::now();
    const int node_count = opt.node_mapper.size();
    const auto eligible = detail::eligible_mask(opt);
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const auto& scenario = opt.scenarios[s];
        const auto successors = detail::scenario_successors(scenario, node_count);
        const auto root_reachable = detail::reachable_from(successors, scenario.ignition_index);
        FppPathLlbiScenarioRecord scenario_record;
        scenario_record.scenario_index = static_cast<int>(s);
        scenario_record.scenario_id = scenario.scenario_id;
        const auto nodes = detail::unique_nodes_from_scenario(scenario, root_reachable);
        for (const int node : nodes) {
            auto paths = detail::enumerate_capped_paths(
                successors,
                eligible,
                scenario.ignition_index,
                node,
                max_paths_per_node);
            if (paths.empty()) {
                continue;
            }
            data.num_paths_used += static_cast<int>(paths.size());
            data.num_path_constraints += static_cast<int>(paths.size());
            ++data.num_b_vars;
            scenario_record.nodes.push_back(FppPathLlbiNodeRecord{node, std::move(paths)});
        }
        data.scenarios.push_back(std::move(scenario_record));
    }
    data.precompute_time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (data.num_paths_used == 0) {
        data.notes.push_back("Path LLBI generated no paths for the loaded FPP scenarios.");
    }
    return data;
}

inline FppDominancePreprocessingResult apply_fpp_global_dominance_preprocessing(
    const opt::OptimizationInstance& opt,
    bool enabled) {
    FppDominancePreprocessingResult result;
    result.enabled = enabled;
    result.structural_weight_safe = enabled;
    result.reduced_instance = opt;
    result.original_candidate_count = static_cast<int>(opt.eligible_indices.size());
    result.post_candidate_count = result.original_candidate_count;
    if (!enabled) {
        result.kept_candidate_compact_nodes = opt.eligible_indices;
        return result;
    }
    const auto start = std::chrono::steady_clock::now();

    std::vector<int> unique_candidates;
    unique_candidates.reserve(opt.eligible_indices.size());
    std::map<int, int> first_position_by_candidate;
    std::vector<char> duplicate_position_removed(opt.eligible_indices.size(), 0);
    std::set<int> duplicate_classes;
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        const int candidate = opt.eligible_indices[pos];
        const auto [it, inserted] =
            first_position_by_candidate.emplace(candidate, static_cast<int>(pos));
        if (inserted) {
            unique_candidates.push_back(candidate);
        } else {
            duplicate_position_removed[pos] = 1;
            duplicate_classes.insert(candidate);
        }
    }
    std::sort(
        unique_candidates.begin(),
        unique_candidates.end(),
        [&opt](int lhs, int rhs) {
            return detail::candidate_identity_less(opt, lhs, rhs);
        });
    result.equivalence_classes += static_cast<int>(duplicate_classes.size());

    std::map<int, std::vector<std::set<int>>> dominated_by_candidate;
    for (const int candidate : unique_candidates) {
        dominated_by_candidate[candidate].resize(opt.scenarios.size());
    }
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        const auto scenario_sets = detail::scenario_dominator_sets(opt, opt.scenarios[s]);
        for (const int candidate : unique_candidates) {
            const auto it = scenario_sets.find(candidate);
            if (it != scenario_sets.end()) {
                dominated_by_candidate[candidate][s] = it->second;
            }
        }
    }

    auto dominates = [&](int i, int j) {
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            if (!detail::subset_of(
                    dominated_by_candidate[j][s],
                    dominated_by_candidate[i][s])) {
                return false;
            }
        }
        return true;
    };

    std::map<std::string, std::vector<int>> classes_by_signature;
    for (const int candidate : unique_candidates) {
        std::ostringstream signature;
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            if (s > 0) {
                signature << "|";
            }
            signature << detail::set_signature(dominated_by_candidate[candidate][s]);
        }
        classes_by_signature[signature.str()].push_back(candidate);
    }

    std::set<int> removed_compact_nodes;
    std::map<int, int> removed_by;
    for (auto& [_, cls] : classes_by_signature) {
        std::sort(
            cls.begin(),
            cls.end(),
            [&opt](int lhs, int rhs) {
                return detail::candidate_identity_less(opt, lhs, rhs);
            });
        if (cls.size() <= 1) {
            continue;
        }
        ++result.equivalence_classes;
        const int representative = cls.front();
        for (std::size_t idx = 1; idx < cls.size(); ++idx) {
            removed_compact_nodes.insert(cls[idx]);
            removed_by[cls[idx]] = representative;
        }
    }

    for (const int j : unique_candidates) {
        if (removed_compact_nodes.find(j) != removed_compact_nodes.end()) {
            continue;
        }
        int best_dominator = -1;
        for (const int i : unique_candidates) {
            if (i == j || removed_compact_nodes.find(i) != removed_compact_nodes.end()) {
                continue;
            }
            if (dominates(i, j) && !dominates(j, i)) {
                if (best_dominator < 0 ||
                    detail::candidate_identity_less(opt, i, best_dominator)) {
                    best_dominator = i;
                }
            }
        }
        if (best_dominator >= 0) {
            removed_compact_nodes.insert(j);
            removed_by[j] = best_dominator;
        }
    }

    std::vector<int> kept_indices;
    std::vector<int> kept_original_nodes;
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        const int candidate = opt.eligible_indices[pos];
        if (duplicate_position_removed[pos] ||
            removed_compact_nodes.find(candidate) != removed_compact_nodes.end()) {
            result.removed_candidate_compact_nodes.push_back(candidate);
            const auto duplicate_rep_it = first_position_by_candidate.find(candidate);
            const int duplicate_representative =
                duplicate_rep_it == first_position_by_candidate.end()
                    ? candidate
                    : opt.eligible_indices[static_cast<std::size_t>(duplicate_rep_it->second)];
            const auto dominator_it = removed_by.find(candidate);
            result.removed_by_dominator.push_back(
                {candidate, dominator_it == removed_by.end() ? duplicate_representative : dominator_it->second});
            continue;
        }
        kept_indices.push_back(candidate);
        kept_original_nodes.push_back(opt.eligible_original_nodes[pos]);
    }
    if (static_cast<int>(kept_indices.size()) < opt.budget) {
        result.reduced_instance = opt;
        result.kept_candidate_compact_nodes = opt.eligible_indices;
        result.removed_candidate_compact_nodes.clear();
        result.removed_by_dominator.clear();
        result.candidates_removed = 0;
        result.equivalence_classes = 0;
        result.post_candidate_count = result.original_candidate_count;
        result.precompute_time_sec =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        result.notes.push_back(
            "FPP global dominance preprocessing was skipped because removing all dominated candidates would leave fewer eligible candidates than the exact firebreak budget.");
        return result;
    }
    result.reduced_instance.eligible_indices = kept_indices;
    result.reduced_instance.eligible_original_nodes = kept_original_nodes;
    result.kept_candidate_compact_nodes = kept_indices;
    result.candidates_removed = static_cast<int>(result.removed_candidate_compact_nodes.size());
    result.post_candidate_count = static_cast<int>(result.kept_candidate_compact_nodes.size());
    result.precompute_time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    result.notes.push_back(
        "FPP global dominance preprocessing removed " +
        std::to_string(result.candidates_removed) +
        " dominated candidates and kept " +
        std::to_string(result.kept_candidate_compact_nodes.size()) + ".");
    return result;
}

inline FppConditionalZeroBenefitResult detect_fpp_conditional_zero_benefit_candidates(
    const opt::OptimizationInstance& opt,
    const std::vector<int>& fixed_one_compact_nodes,
    bool enabled) {
    FppConditionalZeroBenefitResult result;
    result.enabled = enabled;
    result.structural_weight_safe = enabled;
    if (!enabled) {
        return result;
    }
    const auto start = std::chrono::steady_clock::now();
    const int node_count = opt.node_mapper.size();
    std::vector<char> fixed_selected(static_cast<std::size_t>(node_count), 0);
    for (const int node : fixed_one_compact_nodes) {
        detail::ensure_node_in_range(node, node_count, "Fixed firebreak");
        fixed_selected[static_cast<std::size_t>(node)] = 1;
    }
    result.nodes_checked = 1;

    for (const int candidate : opt.eligible_indices) {
        if (fixed_selected[static_cast<std::size_t>(candidate)]) {
            continue;
        }
        ++result.candidates_checked;
        bool zero_benefit_all_scenarios = true;
        for (const auto& scenario : opt.scenarios) {
            if (candidate == scenario.ignition_index) {
                continue;
            }
            const auto successors = detail::scenario_successors(scenario, node_count);
            const auto reached = detail::reachable_after_fixed_firebreaks(
                successors,
                scenario.ignition_index,
                fixed_selected);
            ++result.scenarios_reachability_computed;
            if (reached[static_cast<std::size_t>(candidate)]) {
                zero_benefit_all_scenarios = false;
                break;
            }
        }
        ++result.fixings_attempted;
        if (zero_benefit_all_scenarios) {
            result.zero_benefit_candidate_compact_nodes.push_back(candidate);
        }
    }
    result.variables_fixed_zero = result.fixings_applied;
    result.time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    result.notes.push_back(
        "Conditional zero-benefit detector identified " +
        std::to_string(result.zero_benefit_candidate_compact_nodes.size()) +
        " candidates under the supplied fixed-one set.");
    return result;
}

inline void export_fpp_strengthening_summary(
    const std::filesystem::path& output_path,
    const std::vector<std::pair<std::string, std::string>>& fields) {
    if (output_path.empty()) {
        return;
    }
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open FPP strengthening export path: " + output_path.string());
    }
    out << "field,value\n";
    for (const auto& [field, value] : fields) {
        out << field << "," << value << "\n";
    }
}

}  // namespace firebreak::benders
