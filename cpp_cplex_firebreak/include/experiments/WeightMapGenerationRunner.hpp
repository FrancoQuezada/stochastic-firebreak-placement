#pragma once

#include <filesystem>
#include <string>

#include "core/LandscapeWeightGenerator.hpp"

namespace firebreak::experiments {

struct WeightMapGenerationOptions {
    std::string landscape = "Sub20";
    std::filesystem::path forest_path;
    std::filesystem::path results_path;
    core::LandscapeWeightGenerationConfig config;
    std::filesystem::path output_csv_path;
    std::filesystem::path output_json_path;
};

class WeightMapGenerationRunner {
public:
    int run(const WeightMapGenerationOptions& options) const;
};

}  // namespace firebreak::experiments
