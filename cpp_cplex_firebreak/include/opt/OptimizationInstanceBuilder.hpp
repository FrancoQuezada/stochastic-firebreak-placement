#pragma once

#include "core/Instance.hpp"
#include "opt/OptimizationInstance.hpp"

namespace firebreak::opt {

class OptimizationInstanceBuilder {
public:
    OptimizationInstance build(
        const core::Instance& instance,
        double alpha,
        bool build_dpv_indices = true) const;
};

}  // namespace firebreak::opt

