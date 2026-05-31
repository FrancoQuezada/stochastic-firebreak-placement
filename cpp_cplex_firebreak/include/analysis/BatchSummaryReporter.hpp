#pragma once

#include <filesystem>
#include <string>

namespace firebreak::analysis {

class BatchSummaryReporter {
public:
    void write_report(
        const std::filesystem::path& batch_results_csv,
        const std::filesystem::path& summary_by_method_csv,
        const std::filesystem::path& pairwise_csv,
        const std::filesystem::path& runtime_summary_csv,
        const std::filesystem::path& output_report,
        const std::string& experiment_name) const;
};

}  // namespace firebreak::analysis
