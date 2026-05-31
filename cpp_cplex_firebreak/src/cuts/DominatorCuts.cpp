#include "cuts/DominatorCuts.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace firebreak::cuts {

namespace {

struct Bitset {
    std::vector<std::uint64_t> blocks;
};

struct ScenarioGraphData {
    std::vector<int> reachable_nodes;
    std::vector<std::vector<int>> successors;
    std::vector<std::vector<int>> predecessors;
    std::vector<int> topological_order;
    bool is_dag = true;
};

int block_count(int bit_count) {
    return (bit_count + 63) / 64;
}

std::uint64_t last_block_mask(int bit_count) {
    const int remainder = bit_count % 64;
    if (remainder == 0) {
        return ~std::uint64_t{0};
    }
    return (std::uint64_t{1} << remainder) - 1;
}

Bitset empty_bitset(int bit_count) {
    return Bitset{std::vector<std::uint64_t>(static_cast<std::size_t>(block_count(bit_count)), 0)};
}

Bitset full_bitset(int bit_count) {
    Bitset bitset{std::vector<std::uint64_t>(
        static_cast<std::size_t>(block_count(bit_count)),
        ~std::uint64_t{0})};
    if (!bitset.blocks.empty()) {
        bitset.blocks.back() &= last_block_mask(bit_count);
    }
    return bitset;
}

void set_bit(Bitset& bitset, int bit) {
    bitset.blocks[static_cast<std::size_t>(bit / 64)] |= (std::uint64_t{1} << (bit % 64));
}

bool test_bit(const Bitset& bitset, int bit) {
    return (bitset.blocks[static_cast<std::size_t>(bit / 64)] & (std::uint64_t{1} << (bit % 64))) != 0;
}

void intersect_in_place(Bitset& lhs, const Bitset& rhs) {
    for (std::size_t i = 0; i < lhs.blocks.size(); ++i) {
        lhs.blocks[i] &= rhs.blocks[i];
    }
}

void ensure_node_in_range(int node, int node_count, const std::string& context) {
    if (node < 0 || node >= node_count) {
        throw std::runtime_error(context + " compact node index is out of range.");
    }
}

void validate_instance(const opt::OptimizationInstance& instance) {
    if (instance.node_mapper.size() <= 0) {
        throw std::runtime_error("Dominator preprocessing requires at least one mapped node.");
    }
    if (instance.scenarios.empty()) {
        throw std::runtime_error("Dominator preprocessing requires at least one scenario.");
    }
    const int node_count = instance.node_mapper.size();
    for (const int node : instance.eligible_indices) {
        ensure_node_in_range(node, node_count, "Eligible firebreak");
    }
    for (const auto& scenario : instance.scenarios) {
        ensure_node_in_range(scenario.ignition_index, node_count, "Ignition");
        for (const int node : scenario.observed_node_indices) {
            ensure_node_in_range(node, node_count, "Observed scenario");
        }
        for (const auto& arc : scenario.arcs) {
            ensure_node_in_range(arc.u_index, node_count, "Arc source");
            ensure_node_in_range(arc.v_index, node_count, "Arc target");
        }
    }
}

std::vector<std::pair<int, int>> unique_arcs(const opt::OptimizationScenario& scenario) {
    std::vector<std::pair<int, int>> arcs;
    arcs.reserve(scenario.arcs.size());
    for (const auto& arc : scenario.arcs) {
        arcs.emplace_back(arc.u_index, arc.v_index);
    }
    std::sort(arcs.begin(), arcs.end());
    arcs.erase(std::unique(arcs.begin(), arcs.end()), arcs.end());
    return arcs;
}

ScenarioGraphData build_reachable_graph(
    const opt::OptimizationScenario& scenario,
    int node_count) {
    std::vector<std::vector<int>> global_successors(static_cast<std::size_t>(node_count));
    for (const auto& arc : unique_arcs(scenario)) {
        global_successors[static_cast<std::size_t>(arc.first)].push_back(arc.second);
    }

    // Dominators are defined only for nodes reachable from the scenario root.
    std::vector<char> reached(static_cast<std::size_t>(node_count), 0);
    std::queue<int> frontier;
    reached[static_cast<std::size_t>(scenario.ignition_index)] = 1;
    frontier.push(scenario.ignition_index);

    std::vector<int> reachable_nodes;
    while (!frontier.empty()) {
        const int current = frontier.front();
        frontier.pop();
        reachable_nodes.push_back(current);
        for (const int next : global_successors[static_cast<std::size_t>(current)]) {
            if (reached[static_cast<std::size_t>(next)]) {
                continue;
            }
            reached[static_cast<std::size_t>(next)] = 1;
            frontier.push(next);
        }
    }
    std::sort(reachable_nodes.begin(), reachable_nodes.end());

    // Convert compact global IDs to dense scenario-local IDs for bitset work.
    std::unordered_map<int, int> local_by_node;
    for (std::size_t i = 0; i < reachable_nodes.size(); ++i) {
        local_by_node[reachable_nodes[i]] = static_cast<int>(i);
    }

    ScenarioGraphData graph;
    graph.reachable_nodes = std::move(reachable_nodes);
    graph.successors.assign(graph.reachable_nodes.size(), {});
    graph.predecessors.assign(graph.reachable_nodes.size(), {});

    for (const auto& arc : unique_arcs(scenario)) {
        const auto u_it = local_by_node.find(arc.first);
        const auto v_it = local_by_node.find(arc.second);
        if (u_it == local_by_node.end() || v_it == local_by_node.end()) {
            continue;
        }
        graph.successors[static_cast<std::size_t>(u_it->second)].push_back(v_it->second);
        graph.predecessors[static_cast<std::size_t>(v_it->second)].push_back(u_it->second);
    }

    std::vector<int> indegree(graph.reachable_nodes.size(), 0);
    for (std::size_t u = 0; u < graph.successors.size(); ++u) {
        std::sort(graph.successors[u].begin(), graph.successors[u].end());
        graph.successors[u].erase(
            std::unique(graph.successors[u].begin(), graph.successors[u].end()),
            graph.successors[u].end());
        for (const int v : graph.successors[u]) {
            ++indegree[static_cast<std::size_t>(v)];
        }
    }
    for (auto& predecessors : graph.predecessors) {
        std::sort(predecessors.begin(), predecessors.end());
        predecessors.erase(std::unique(predecessors.begin(), predecessors.end()), predecessors.end());
    }

    // Kahn topological sort doubles as the DAG check for this scenario graph.
    std::queue<int> zero_indegree;
    for (int i = 0; i < static_cast<int>(indegree.size()); ++i) {
        if (indegree[static_cast<std::size_t>(i)] == 0) {
            zero_indegree.push(i);
        }
    }
    while (!zero_indegree.empty()) {
        const int current = zero_indegree.front();
        zero_indegree.pop();
        graph.topological_order.push_back(current);
        for (const int next : graph.successors[static_cast<std::size_t>(current)]) {
            --indegree[static_cast<std::size_t>(next)];
            if (indegree[static_cast<std::size_t>(next)] == 0) {
                zero_indegree.push(next);
            }
        }
    }
    graph.is_dag = graph.topological_order.size() == graph.reachable_nodes.size();
    return graph;
}

std::vector<Bitset> compute_dominators_dag(
    const ScenarioGraphData& graph,
    int root_local) {
    const int n = static_cast<int>(graph.reachable_nodes.size());
    std::vector<Bitset> dominators(static_cast<std::size_t>(n), empty_bitset(n));
    set_bit(dominators[static_cast<std::size_t>(root_local)], root_local);

    // For DAGs, dom(v) = {v} union intersection of predecessor dominator sets.
    for (const int node : graph.topological_order) {
        if (node == root_local) {
            continue;
        }
        const auto& predecessors = graph.predecessors[static_cast<std::size_t>(node)];
        Bitset node_dominators = full_bitset(n);
        if (predecessors.empty()) {
            node_dominators = empty_bitset(n);
        } else {
            bool first = true;
            for (const int pred : predecessors) {
                if (first) {
                    node_dominators = dominators[static_cast<std::size_t>(pred)];
                    first = false;
                } else {
                    intersect_in_place(node_dominators, dominators[static_cast<std::size_t>(pred)]);
                }
            }
        }
        set_bit(node_dominators, node);
        dominators[static_cast<std::size_t>(node)] = std::move(node_dominators);
    }
    return dominators;
}

std::vector<Bitset> compute_dominators_iterative(
    const ScenarioGraphData& graph,
    int root_local) {
    const int n = static_cast<int>(graph.reachable_nodes.size());
    std::vector<Bitset> dominators(static_cast<std::size_t>(n), full_bitset(n));
    dominators[static_cast<std::size_t>(root_local)] = empty_bitset(n);
    set_bit(dominators[static_cast<std::size_t>(root_local)], root_local);

    // Cyclic graphs use the same equations, iterated to a fixed point.
    bool changed = true;
    while (changed) {
        changed = false;
        for (int node = 0; node < n; ++node) {
            if (node == root_local) {
                continue;
            }
            const auto& predecessors = graph.predecessors[static_cast<std::size_t>(node)];
            Bitset next = full_bitset(n);
            if (predecessors.empty()) {
                next = empty_bitset(n);
            } else {
                bool first = true;
                for (const int pred : predecessors) {
                    if (first) {
                        next = dominators[static_cast<std::size_t>(pred)];
                        first = false;
                    } else {
                        intersect_in_place(next, dominators[static_cast<std::size_t>(pred)]);
                    }
                }
            }
            set_bit(next, node);
            if (next.blocks != dominators[static_cast<std::size_t>(node)].blocks) {
                dominators[static_cast<std::size_t>(node)] = std::move(next);
                changed = true;
            }
        }
    }
    return dominators;
}

std::unordered_set<int> eligible_node_set(const opt::OptimizationInstance& instance) {
    return std::unordered_set<int>(instance.eligible_indices.begin(), instance.eligible_indices.end());
}

std::vector<DominatorSet> build_dominated_sets(
    const opt::OptimizationInstance& instance,
    const opt::OptimizationScenario& scenario,
    const ScenarioGraphData& graph,
    const std::vector<Bitset>& dominators) {
    const auto eligible = eligible_node_set(instance);
    std::vector<DominatorSet> dominated_by_u;

    std::unordered_map<int, int> local_by_node;
    for (std::size_t local = 0; local < graph.reachable_nodes.size(); ++local) {
        local_by_node[graph.reachable_nodes[local]] = static_cast<int>(local);
    }
    const int root_local = local_by_node.at(scenario.ignition_index);

    for (std::size_t u_local_pos = 0; u_local_pos < graph.reachable_nodes.size(); ++u_local_pos) {
        const int u = graph.reachable_nodes[u_local_pos];
        if (u == scenario.ignition_index || eligible.find(u) == eligible.end()) {
            continue;
        }

        // Root is excluded as a dominated target to preserve root propagation.
        DominatorSet set;
        set.dominator_node = u;
        for (std::size_t v_local_pos = 0; v_local_pos < graph.reachable_nodes.size(); ++v_local_pos) {
            if (static_cast<int>(v_local_pos) == root_local) {
                continue;
            }
            if (test_bit(dominators[v_local_pos], static_cast<int>(u_local_pos))) {
                set.dominated_nodes.push_back(graph.reachable_nodes[v_local_pos]);
            }
        }
        std::sort(set.dominated_nodes.begin(), set.dominated_nodes.end());
        if (!set.dominated_nodes.empty()) {
            dominated_by_u.push_back(std::move(set));
        }
    }

    std::sort(dominated_by_u.begin(), dominated_by_u.end(), [](const DominatorSet& lhs, const DominatorSet& rhs) {
        if (lhs.dominated_nodes.size() != rhs.dominated_nodes.size()) {
            return lhs.dominated_nodes.size() > rhs.dominated_nodes.size();
        }
        return lhs.dominator_node < rhs.dominator_node;
    });
    return dominated_by_u;
}

std::string stats_note(const DominatorCutStats& stats) {
    std::ostringstream out;
    out << "Dominator preprocessing processed " << stats.scenarios_processed
        << " scenarios: DAG=" << stats.dag_scenarios
        << ", fallback=" << stats.fallback_scenarios
        << ", aggregate_cuts=" << stats.aggregate_cuts_added
        << ", individual_cuts=" << stats.individual_cuts_added << ".";
    return out.str();
}

#ifdef FIREBREAK_WITH_CPLEX
std::vector<const DominatorSet*> selected_aggregate_sets(
    const ScenarioDominatorInfo& scenario_info,
    int max_aggregate) {
    std::vector<const DominatorSet*> selected;
    if (max_aggregate <= 0) {
        return selected;
    }
    for (const auto& set : scenario_info.dominated_by_u) {
        if (set.dominated_nodes.size() <= 1) {
            continue;
        }
        selected.push_back(&set);
        if (static_cast<int>(selected.size()) >= max_aggregate) {
            break;
        }
    }
    return selected;
}

std::vector<const DominatorSet*> selected_individual_sets(
    const ScenarioDominatorInfo& scenario_info,
    const std::vector<const DominatorSet*>& aggregate_sets,
    int max_individual) {
    if (max_individual <= 0) {
        return {};
    }
    if (!aggregate_sets.empty()) {
        return aggregate_sets;
    }
    std::vector<const DominatorSet*> selected;
    for (const auto& set : scenario_info.dominated_by_u) {
        selected.push_back(&set);
        if (static_cast<int>(selected.size()) >= max_individual) {
            break;
        }
    }
    return selected;
}
#endif

}  // namespace

int DominatorCutStats::total_cuts_added() const {
    return aggregate_cuts_added + individual_cuts_added;
}

DominatorPreprocessor::DominatorPreprocessor(const opt::OptimizationInstance& instance)
    : instance_(instance) {}

DominatorPreprocessResult DominatorPreprocessor::compute() const {
    validate_instance(instance_);
    const auto start = std::chrono::steady_clock::now();

    DominatorPreprocessResult result;
    const int node_count = instance_.node_mapper.size();
    result.scenarios.reserve(instance_.scenarios.size());

    for (std::size_t scenario_index = 0; scenario_index < instance_.scenarios.size(); ++scenario_index) {
        const auto& scenario = instance_.scenarios[scenario_index];
        auto graph = build_reachable_graph(scenario, node_count);
        std::unordered_map<int, int> local_by_node;
        for (std::size_t local = 0; local < graph.reachable_nodes.size(); ++local) {
            local_by_node[graph.reachable_nodes[local]] = static_cast<int>(local);
        }
        const int root_local = local_by_node.at(scenario.ignition_index);

        const auto dominators =
            graph.is_dag
                ? compute_dominators_dag(graph, root_local)
                : compute_dominators_iterative(graph, root_local);

        ScenarioDominatorInfo scenario_info;
        scenario_info.scenario_id = scenario.scenario_id;
        scenario_info.scenario_index = static_cast<int>(scenario_index);
        scenario_info.used_dag_algorithm = graph.is_dag;
        scenario_info.reachable_nodes = graph.reachable_nodes;
        scenario_info.dominated_by_u = build_dominated_sets(instance_, scenario, graph, dominators);
        result.scenarios.push_back(std::move(scenario_info));

        ++result.stats.scenarios_processed;
        if (graph.is_dag) {
            ++result.stats.dag_scenarios;
        } else {
            ++result.stats.fallback_scenarios;
        }
    }

    const auto end = std::chrono::steady_clock::now();
    result.stats.preprocessing_time_sec = std::chrono::duration<double>(end - start).count();
    result.stats.notes.push_back(stats_note(result.stats));
    return result;
}

#ifdef FIREBREAK_WITH_CPLEX
DominatorCutStats add_dominator_cuts_to_model(
    IloEnv& env,
    IloModel& model,
    const opt::OptimizationInstance& instance,
    const DominatorCutOptions& options,
    const DominatorVariableAccess& vars) {
    DominatorPreprocessor preprocessor(instance);
    auto preprocess = preprocessor.compute();
    DominatorCutStats stats = preprocess.stats;

    if (!options.enabled) {
        return stats;
    }
    if (!vars.has_x || !vars.get_x || !vars.has_y || !vars.get_y) {
        throw std::runtime_error("Dominator cut generator requires x/y variable accessors.");
    }

    std::set<std::pair<int, int>> aggregate_keys;
    std::set<std::tuple<int, int, int>> individual_keys;

    for (const auto& scenario_info : preprocess.scenarios) {
        const std::size_t scenario_index = static_cast<std::size_t>(scenario_info.scenario_index);
        // Keep only the largest dominated sets to avoid flooding small models.
        const auto aggregate_sets = selected_aggregate_sets(
            scenario_info,
            options.max_aggregate_dominator_cuts_per_scenario);

        for (const DominatorSet* set : aggregate_sets) {
            if (!vars.has_y(set->dominator_node)) {
                continue;
            }

            std::vector<int> dominated_nodes;
            dominated_nodes.reserve(set->dominated_nodes.size());
            for (const int v : set->dominated_nodes) {
                if (vars.has_x(scenario_index, v)) {
                    dominated_nodes.push_back(v);
                }
            }
            if (dominated_nodes.size() <= 1) {
                continue;
            }
            const auto key = std::make_pair(scenario_info.scenario_index, set->dominator_node);
            if (!aggregate_keys.insert(key).second) {
                continue;
            }

            IloExpr expr(env);
            for (const int v : dominated_nodes) {
                expr += vars.get_x(scenario_index, v);
            }
            expr += static_cast<double>(dominated_nodes.size()) * vars.get_y(set->dominator_node);
            model.add(expr <= static_cast<double>(dominated_nodes.size()));
            expr.end();
            ++stats.aggregate_cuts_added;
        }

        int individual_added_for_scenario = 0;
        const auto individual_sets = selected_individual_sets(
            scenario_info,
            aggregate_sets,
            options.max_individual_dominator_cuts_per_scenario);
        for (const DominatorSet* set : individual_sets) {
            if (!vars.has_y(set->dominator_node)) {
                continue;
            }
            for (const int v : set->dominated_nodes) {
                if (individual_added_for_scenario >= options.max_individual_dominator_cuts_per_scenario) {
                    break;
                }
                if (!vars.has_x(scenario_index, v)) {
                    continue;
                }
                if (vars.skip_self_individual_cuts && v == set->dominator_node) {
                    continue;
                }
                const auto key = std::make_tuple(
                    scenario_info.scenario_index,
                    set->dominator_node,
                    v);
                if (!individual_keys.insert(key).second) {
                    continue;
                }

                IloExpr expr(env);
                expr += vars.get_x(scenario_index, v);
                expr += vars.get_y(set->dominator_node);
                model.add(expr <= 1.0);
                expr.end();
                ++stats.individual_cuts_added;
                ++individual_added_for_scenario;
            }
            if (individual_added_for_scenario >= options.max_individual_dominator_cuts_per_scenario) {
                break;
            }
        }
    }

    stats.notes.clear();
    stats.notes.push_back(stats_note(stats));
    return stats;
}
#endif

}  // namespace firebreak::cuts
