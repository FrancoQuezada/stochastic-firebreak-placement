#pragma once

#include "experiments/BatchExperimentConfig.hpp"

namespace firebreak::experiments {

class BatchExperimentRunner {
public:
    int run(const BatchExperimentConfig& config) const;
};

}  // namespace firebreak::experiments
