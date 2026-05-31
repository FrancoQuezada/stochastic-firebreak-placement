#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "opt/OptimizationInstance.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::cuts {

struct DominatorCutOptions {
    bool enabled = false;
    int max_aggregate_dominator_cuts_per_scenario = 50;
    int max_individual_dominator_cuts_per_scenario = 100;
    bool verbose = false;
};

struct DominatorCutStats {
    int scenarios_processed = 0;
    int dag_scenarios = 0;
    int fallback_scenarios = 0;
    int aggregate_cuts_added = 0;
    int individual_cuts_added = 0;
    double preprocessing_time_sec = 0.0;
    std::vector<std::string> notes;

    int total_cuts_added() const;
};

struct DominatorSet {
    int dominator_node = -1;
    std::vector<int> dominated_nodes;
};

struct ScenarioDominatorInfo {
    int scenario_id = 0;
    int scenario_index = 0;
    bool used_dag_algorithm = true;
    std::vector<int> reachable_nodes;
    std::vector<DominatorSet> dominated_by_u;
};

struct DominatorPreprocessResult {
    std::vector<ScenarioDominatorInfo> scenarios;
    DominatorCutStats stats;
};

class DominatorPreprocessor {
public:
    explicit DominatorPreprocessor(const opt::OptimizationInstance& instance);

    DominatorPreprocessResult compute() const;

private:
    const opt::OptimizationInstance& instance_;
};

#ifdef FIREBREAK_WITH_CPLEX
struct DominatorVariableAccess {
    std::function<bool(std::size_t, int)> has_x;
    std::function<IloNumVar(std::size_t, int)> get_x;
    std::function<bool(int)> has_y;
    std::function<IloNumVar(int)> get_y;
    bool skip_self_individual_cuts = false;
};

DominatorCutStats add_dominator_cuts_to_model(
    IloEnv& env,
    IloModel& model,
    const opt::OptimizationInstance& instance,
    const DominatorCutOptions& options,
    const DominatorVariableAccess& vars);
#endif

}  // namespace firebreak::cuts
