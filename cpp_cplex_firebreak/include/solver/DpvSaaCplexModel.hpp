#pragma once

#include <cstddef>
#include <vector>

#include "opt/OptimizationInstance.hpp"
#include "solver/FppSaaCplexModel.hpp"
#include "solver/ModelResult.hpp"
#include "solver/WarmStart.hpp"

namespace firebreak::solver {

struct DpvObjectiveTermDescriptor {
    int scenario_id = 0;
    int source_index = -1;
    int successor_index = -1;
    int descendant_index = -1;
};

struct DpvSaaModelStructure {
    std::size_t x_variable_count = 0;
    std::size_t y_variable_count = 0;
    std::size_t z_variable_count = 0;
    std::size_t total_variable_count = 0;
    std::size_t budget_constraint_count = 0;
    std::size_t ignition_constraint_count = 0;
    std::size_t propagation_constraint_count = 0;
    std::size_t linearization_constraint_count = 0;
    std::size_t total_constraint_count = 0;
    std::vector<int> y_indices;
    std::vector<PropagationConstraintDescriptor> propagation_constraints;
    std::vector<DpvObjectiveTermDescriptor> objective_terms;

    bool has_y_for_node_index(int node_index) const;
};

DpvSaaModelStructure analyze_dpv_saa_model_structure(const opt::OptimizationInstance& opt);

class DpvSaaCplexModel {
public:
    ModelResult solve(
        const opt::OptimizationInstance& opt,
        double time_limit_seconds,
        double mip_gap,
        int threads,
        bool verbose,
        const WarmStart* warm_start = nullptr) const;
};

}  // namespace firebreak::solver
