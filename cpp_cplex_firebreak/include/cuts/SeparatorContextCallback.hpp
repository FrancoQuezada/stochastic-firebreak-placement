#pragma once

#include <string>
#include <vector>

#include "cuts/SeparatorCutSeparator.hpp"
#include "opt/OptimizationInstance.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::cuts {

#ifdef FIREBREAK_WITH_CPLEX
struct SeparatorVariableAccess {
    IloNumVarArray y_vars;
    std::vector<int> y_position_by_node;
    std::vector<IloNumVarArray> x_vars_by_scenario;
    std::vector<std::vector<int>> x_position_by_scenario;
};

class SeparatorContextCallback : public IloCplex::Callback::Function {
public:
    SeparatorContextCallback(
        const opt::OptimizationInstance& instance,
        SeparatorCutOptions options,
        SeparatorVariableAccess access);

    void invoke(const IloCplex::Callback::Context& context) ILO_OVERRIDE;

    const SeparatorCutStats& stats() const;

private:
    bool shouldSeparate(const IloCplex::Callback::Context& context);
    void separateRelaxation(const IloCplex::Callback::Context& context);
    std::vector<double> extractYbar(const IloCplex::Callback::Context& context) const;
    std::vector<std::vector<double>> extractXbar(const IloCplex::Callback::Context& context) const;
    bool hasY(int compact_node) const;
    bool hasX(std::size_t scenario_index, int compact_node) const;
    IloNumVar getY(int compact_node) const;
    IloNumVar getX(std::size_t scenario_index, int compact_node) const;
    void refreshStatsFromSeparatorCore();

    const opt::OptimizationInstance& instance_;
    SeparatorCutOptions options_;
    SeparatorVariableAccess access_;
    SeparatorCutStats stats_;
    SeparatorCutSeparator separator_core_;
    std::vector<char> eligible_by_node_;
};
#endif

}  // namespace firebreak::cuts
