#include "cuts/SeparatorContextCallback.hpp"

#ifdef FIREBREAK_WITH_CPLEX

#include <algorithm>
#include <cmath>
#include <utility>

namespace firebreak::cuts {
namespace {

int max_compact_node_in_instance(const opt::OptimizationInstance& instance) {
    int max_node = -1;
    for (const int node : instance.eligible_indices) {
        max_node = std::max(max_node, node);
    }
    for (const auto& scenario : instance.scenarios) {
        max_node = std::max(max_node, scenario.ignition_index);
        for (const int node : scenario.observed_node_indices) {
            max_node = std::max(max_node, node);
        }
        for (const auto& arc : scenario.arcs) {
            max_node = std::max(max_node, std::max(arc.u_index, arc.v_index));
        }
    }
    return max_node;
}

double clamped_unit_value(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::max(0.0, std::min(1.0, value));
}

}  // namespace

SeparatorContextCallback::SeparatorContextCallback(
    const opt::OptimizationInstance& instance,
    SeparatorCutOptions options,
    SeparatorVariableAccess access)
    : instance_(instance),
      options_(std::move(options)),
      access_(std::move(access)),
      separator_core_(instance, options_) {
    const int max_node = max_compact_node_in_instance(instance_);
    if (max_node >= 0) {
        eligible_by_node_.assign(static_cast<std::size_t>(max_node + 1), 0);
    }
    for (const int node : instance_.eligible_indices) {
        if (node >= 0) {
            if (node >= static_cast<int>(eligible_by_node_.size())) {
                eligible_by_node_.resize(static_cast<std::size_t>(node + 1), 0);
            }
            eligible_by_node_[static_cast<std::size_t>(node)] = 1;
        }
    }
}

void SeparatorContextCallback::invoke(const IloCplex::Callback::Context& context) {
    if (!context.inRelaxation()) {
        return;
    }

    ++stats_.callback_invocations;
    if (!shouldSeparate(context)) {
        return;
    }

    separateRelaxation(context);
}

const SeparatorCutStats& SeparatorContextCallback::stats() const {
    return stats_;
}

bool SeparatorContextCallback::shouldSeparate(const IloCplex::Callback::Context& context) {
    CPXLONG depth = 0;
    try {
        depth = context.getLongInfo(IloCplex::Callback::Context::Info::NodeDepth);
    } catch (...) {
        depth = stats_.callback_invocations == 1 ? 0 : 1;
    }

    if (depth == 0) {
        return options_.sep_at_root;
    }

    const int frequency = std::max(1, options_.sep_frequency_nodes);
    return stats_.callback_invocations % frequency == 0;
}

void SeparatorContextCallback::separateRelaxation(const IloCplex::Callback::Context& context) {
    const auto ybar = extractYbar(context);
    const auto xbar = extractXbar(context);
    // Keep screening, min-cut separation, violation checks, and duplicate
    // filtering in a CPLEX-independent core so tests can force fractional
    // xbar/ybar values without relying on CPLEX callback scheduling.
    const auto candidate_cuts =
        separator_core_.separate(ybar, xbar, options_.sep_max_cuts_per_call);

    for (const auto& candidate : candidate_cuts) {
        IloEnv env = context.getEnv();
        IloExpr lhs(env);
        lhs += getX(static_cast<std::size_t>(candidate.scenario_index), candidate.target_compact_node);
        for (const int compact_node : candidate.separator_compact_nodes) {
            lhs += getY(compact_node);
        }

        // Generic relaxation-context user cut: globally valid, not lazy.
        IloRange cut(lhs <= static_cast<IloNum>(candidate.separator_compact_nodes.size()));
        context.addUserCut(cut, IloCplex::UseCutPurge, IloFalse);
        cut.end();
        lhs.end();
    }
    refreshStatsFromSeparatorCore();
}

std::vector<double> SeparatorContextCallback::extractYbar(
    const IloCplex::Callback::Context& context) const {
    std::vector<double> ybar(eligible_by_node_.size(), 0.0);
    for (int compact_node = 0; compact_node < static_cast<int>(access_.y_position_by_node.size()); ++compact_node) {
        const int pos = access_.y_position_by_node[static_cast<std::size_t>(compact_node)];
        if (pos < 0) {
            continue;
        }
        if (compact_node >= static_cast<int>(ybar.size())) {
            ybar.resize(static_cast<std::size_t>(compact_node + 1), 0.0);
        }
        ybar[static_cast<std::size_t>(compact_node)] =
            clamped_unit_value(context.getRelaxationPoint(access_.y_vars[static_cast<IloInt>(pos)]));
    }
    return ybar;
}

std::vector<std::vector<double>> SeparatorContextCallback::extractXbar(
    const IloCplex::Callback::Context& context) const {
    const int node_count = static_cast<int>(eligible_by_node_.size());
    std::vector<std::vector<double>> xbar(
        instance_.scenarios.size(),
        std::vector<double>(static_cast<std::size_t>(node_count), 0.0));

    for (std::size_t s = 0; s < instance_.scenarios.size(); ++s) {
        const auto& scenario = instance_.scenarios[s];
        for (const int compact_node : scenario.observed_node_indices) {
            if (!hasX(s, compact_node)) {
                continue;
            }
            if (compact_node >= static_cast<int>(xbar[s].size())) {
                xbar[s].resize(static_cast<std::size_t>(compact_node + 1), 0.0);
            }
            xbar[s][static_cast<std::size_t>(compact_node)] =
                clamped_unit_value(context.getRelaxationPoint(getX(s, compact_node)));
        }
    }
    return xbar;
}

bool SeparatorContextCallback::hasY(int compact_node) const {
    return compact_node >= 0 &&
           compact_node < static_cast<int>(access_.y_position_by_node.size()) &&
           access_.y_position_by_node[static_cast<std::size_t>(compact_node)] >= 0;
}

bool SeparatorContextCallback::hasX(std::size_t scenario_index, int compact_node) const {
    return scenario_index < access_.x_position_by_scenario.size() &&
           scenario_index < access_.x_vars_by_scenario.size() &&
           compact_node >= 0 &&
           compact_node < static_cast<int>(access_.x_position_by_scenario[scenario_index].size()) &&
           access_.x_position_by_scenario[scenario_index][static_cast<std::size_t>(compact_node)] >= 0;
}

IloNumVar SeparatorContextCallback::getY(int compact_node) const {
    return access_.y_vars[static_cast<IloInt>(access_.y_position_by_node[static_cast<std::size_t>(compact_node)])];
}

IloNumVar SeparatorContextCallback::getX(std::size_t scenario_index, int compact_node) const {
    const int pos = access_.x_position_by_scenario[scenario_index][static_cast<std::size_t>(compact_node)];
    return access_.x_vars_by_scenario[scenario_index][static_cast<IloInt>(pos)];
}

void SeparatorContextCallback::refreshStatsFromSeparatorCore() {
    const int callback_invocations = stats_.callback_invocations;
    stats_ = separator_core_.stats();
    stats_.callback_invocations = callback_invocations;
}

}  // namespace firebreak::cuts

#else

namespace firebreak::cuts {
// Non-CPLEX builds expose SeparatorCutOptions and SeparatorCutStats only.
}

#endif
