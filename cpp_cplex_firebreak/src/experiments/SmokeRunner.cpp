#include "experiments/SmokeRunner.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

#include "io/Cell2FireReader.hpp"
#include "io/PathUtils.hpp"
#include "io/ResultWriter.hpp"
#include "io/ScenarioFileUtils.hpp"

namespace firebreak::experiments {

namespace {

std::filesystem::path default_forest_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / "data" / "CanadianFBP" / landscape;
}

std::filesystem::path default_results_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / landscape;
}

}  // namespace

int SmokeRunner::run(const SmokeOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.scenario_ids.empty()) {
        throw std::runtime_error("--scenario-ids is required.");
    }

    const auto forest_path = options.forest_path.empty()
        ? default_forest_path(options.landscape)
        : firebreak::io::resolve_input_path(options.forest_path.string());
    const auto results_path = options.results_path.empty()
        ? default_results_path(options.landscape)
        : firebreak::io::resolve_input_path(options.results_path.string());
    const auto output_path = options.output_path.empty()
        ? firebreak::io::resolve_output_path("results/smoke_summary.json")
        : firebreak::io::resolve_output_path(options.output_path.string());

    const auto inventory = firebreak::io::detect_message_files(results_path);
    firebreak::io::validate_scenario_ids(inventory, options.scenario_ids);

    std::vector<std::string> warnings;
    firebreak::io::Cell2FireReader reader;
    auto instance = reader.load_instance(
        options.landscape,
        forest_path,
        results_path,
        options.scenario_ids,
        warnings);

    firebreak::io::print_smoke_summary(std::cout, instance, inventory, options.scenario_ids, warnings);
    firebreak::io::write_smoke_summary_json(output_path, instance, inventory, options.scenario_ids, warnings);
    std::cout << "Wrote summary: " << firebreak::io::path_to_string(output_path) << "\n";
    return 0;
}

}  // namespace firebreak::experiments

