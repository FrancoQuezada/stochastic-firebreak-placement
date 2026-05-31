#include "experiments/ManifestRunner.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "analysis/BatchSummaryReporter.hpp"
#include "analysis/ExperimentAggregator.hpp"
#include "analysis/RuntimeProfiler.hpp"
#include "experiments/BatchExperimentRunner.hpp"
#include "experiments/ExperimentManifest.hpp"
#include "io/ExperimentResultWriter.hpp"
#include "io/PathUtils.hpp"
#include "solver/CplexEnvironment.hpp"

namespace firebreak::experiments {

namespace {

std::string trim_trailing_newlines(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::string shell_read_first_line(const std::string& command) {
    std::array<char, 256> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "unknown";
    }
    if (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output = buffer.data();
    }
    const int status = pclose(pipe);
    if (status != 0 || output.empty()) {
        return "unknown";
    }
    return trim_trailing_newlines(output);
}

std::string git_commit_hash() {
    const auto root = firebreak::io::project_root();
    return shell_read_first_line("git -C \"" + root.string() + "\" rev-parse HEAD 2>/dev/null");
}

void write_run_metadata(
    const std::filesystem::path& output_dir,
    const ManifestRunOptions& options) {
    const auto output_path = output_dir / "run_metadata.txt";
    firebreak::io::ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open run metadata file: " + output_path.string());
    }
    out << "timestamp=" << firebreak::io::current_timestamp_utc() << "\n";
    out << "command=" << options.executable_command << "\n";
    out << "git_commit=" << git_commit_hash() << "\n";
    out << "cplex_enabled=" << (firebreak::solver::cplex_support_enabled() ? "yes" : "no") << "\n";
    out << "cplex_version=unknown\n";
    out << "build_mode=" << firebreak::solver::cplex_build_mode_message() << "\n";
}

}  // namespace

int ManifestRunner::run(const ManifestRunOptions& options) const {
    if (options.manifest_path.empty()) {
        throw std::runtime_error("--manifest is required.");
    }

    const auto manifest_path = firebreak::io::resolve_input_path(options.manifest_path.string());
    auto manifest = load_experiment_manifest(manifest_path);
    if (options.rerun_existing) {
        manifest.config.resume_existing = false;
    }
    std::cout << describe_manifest_config(manifest) << std::flush;

    const auto output_dir = firebreak::io::resolve_output_path(manifest.config.output_dir.string());
    std::filesystem::create_directories(output_dir);
    copy_manifest_file(manifest_path, output_dir);
    write_run_metadata(output_dir, options);

    BatchExperimentRunner batch_runner;
    const int batch_status = batch_runner.run(manifest.config);
    if (batch_status != 0) {
        return batch_status;
    }

    const auto batch_csv = firebreak::io::resolve_output_path(manifest.config.output_csv.string());
    const auto summary_dir = output_dir / "summary";
    analysis::ExperimentAggregator aggregator;
    (void)aggregator.aggregate(batch_csv, summary_dir);
    analysis::RuntimeProfiler profiler;
    profiler.write_runtime_summary(batch_csv, summary_dir / "runtime_summary.csv");

    analysis::BatchSummaryReporter reporter;
    reporter.write_report(
        batch_csv,
        summary_dir / "summary_by_method.csv",
        summary_dir / "pairwise_comparison_fpp_vs_dpv.csv",
        summary_dir / "runtime_summary.csv",
        summary_dir / "summary_report.txt",
        manifest.config.experiment_name);

    std::cout << "Summary report: "
              << firebreak::io::path_to_string(summary_dir / "summary_report.txt")
              << "\n";
    return 0;
}

}  // namespace firebreak::experiments
