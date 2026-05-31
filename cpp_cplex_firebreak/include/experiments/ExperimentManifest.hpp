#pragma once

#include <filesystem>
#include <string>

#include "experiments/BatchExperimentConfig.hpp"

namespace firebreak::experiments {

struct ExperimentManifest {
    std::filesystem::path source_path;
    BatchExperimentConfig config;
};

ExperimentManifest load_experiment_manifest(const std::filesystem::path& manifest_path);
void copy_manifest_file(
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& output_dir);
std::string describe_manifest_config(const ExperimentManifest& manifest);

}  // namespace firebreak::experiments
