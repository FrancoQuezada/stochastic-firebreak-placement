#pragma once

#include <filesystem>

namespace firebreak::analysis {

class RuntimeProfiler {
public:
    void write_runtime_summary(
        const std::filesystem::path& batch_results_csv,
        const std::filesystem::path& output_csv) const;
};

}  // namespace firebreak::analysis
