#pragma once

#include <filesystem>

namespace firebreak::analysis {

struct AggregationSummary {
    int pairwise_cases = 0;
    int dpv_wins = 0;
    int fpp_wins = 0;
    int ties = 0;
    double average_relative_difference = 0.0;
};

class ExperimentAggregator {
public:
    AggregationSummary aggregate(
        const std::filesystem::path& input_csv,
        const std::filesystem::path& output_dir) const;
};

}  // namespace firebreak::analysis
