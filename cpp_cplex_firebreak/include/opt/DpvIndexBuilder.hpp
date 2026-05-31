#pragma once

#include "opt/IndexMapper.hpp"
#include "opt/OptimizationInstance.hpp"

namespace firebreak::opt {

class DpvIndexBuilder {
public:
    DpvScenarioIndexData build_for_scenario(
        const OptimizationScenario& scenario,
        const IndexMapper& node_mapper) const;
};

}  // namespace firebreak::opt

