#include "opt/OptimizationInstance.hpp"

namespace firebreak::opt {

std::size_t DpvScenarioIndexData::num_pairs() const {
    return product_pairs.size();
}

std::size_t OptimizationScenario::num_arcs() const {
    return arcs.size();
}

std::size_t OptimizationInstance::num_scenarios() const {
    return scenarios.size();
}

std::size_t OptimizationInstance::num_eligible_nodes() const {
    return eligible_indices.size();
}

std::size_t OptimizationInstance::num_mapped_nodes() const {
    return static_cast<std::size_t>(node_mapper.size());
}

}  // namespace firebreak::opt

