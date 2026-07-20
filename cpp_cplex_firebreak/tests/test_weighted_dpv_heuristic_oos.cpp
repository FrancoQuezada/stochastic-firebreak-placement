#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "experiments/GreedyOutOfSampleRunner.hpp"
#include "experiments/StaticDpvOutOfSampleRunner.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/PathUtils.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"

namespace {

namespace fs = std::filesystem;

fs::path temp_file(const std::string& name) {
    return fs::temp_directory_path() / name;
}

void write_weight_map_for_sub20(const fs::path& path) {
    const auto forest_path = firebreak::io::repo_root() / "sample_test" / "data" / "CanadianFBP" / "Sub20";
    const auto results_path = firebreak::io::repo_root() / "sample_test" / "Sub20";
    std::vector<std::string> warnings;
    firebreak::io::Cell2FireReader reader;
    auto instance = reader.load_instance("Sub20", forest_path, results_path, {836}, warnings);
    firebreak::opt::OptimizationInstanceBuilder builder;
    const auto opt = builder.build(instance, 0.01, true);

    std::ofstream out(path);
    assert(out);
    out << "cell_id,raw_weight,normalized_weight,cluster_id\n";
    for (const int cell_id : opt.node_mapper.original_nodes()) {
        const double weight = 1.0 + static_cast<double>(cell_id % 5) * 0.25;
        out << cell_id << "," << weight << "," << weight << "," << (cell_id % 3) << "\n";
    }
}

}  // namespace

int main() {
    const auto weight_map = temp_file("firebreak_weighted_dpv_heuristic_oos_weights.csv");
    write_weight_map_for_sub20(weight_map);

    firebreak::experiments::StaticDpvOutOfSampleOptions static_options;
    static_options.landscape = "Sub20";
    static_options.train_ids = {836};
    static_options.test_ids = {531};
    static_options.alpha = 0.01;
    static_options.run_id = "test_weighted_static_dpv_oos";
    static_options.weight_map_file = weight_map;
    static_options.output_json_path = temp_file("firebreak_weighted_static_dpv_oos.json");
    static_options.output_csv_path = temp_file("firebreak_weighted_static_dpv_oos.csv");
    static_options.solution_json_path = temp_file("firebreak_weighted_static_dpv_oos_solution.json");
    static_options.solution_csv_path = temp_file("firebreak_weighted_static_dpv_oos_solution.csv");
    fs::remove(static_options.output_json_path);
    fs::remove(static_options.output_csv_path);
    fs::remove(static_options.solution_json_path);
    fs::remove(static_options.solution_csv_path);
    assert(firebreak::experiments::StaticDpvOutOfSampleRunner().run(static_options) == 0);

    firebreak::experiments::GreedyOutOfSampleOptions greedy_options;
    greedy_options.landscape = "Sub20";
    greedy_options.train_ids = {836};
    greedy_options.test_ids = {531};
    greedy_options.alpha = 0.01;
    greedy_options.metric = "DPV3";
    greedy_options.run_id = "test_weighted_greedy_dpv_oos";
    greedy_options.weight_map_file = weight_map;
    greedy_options.output_json_path = temp_file("firebreak_weighted_greedy_dpv_oos.json");
    greedy_options.output_csv_path = temp_file("firebreak_weighted_greedy_dpv_oos.csv");
    greedy_options.solution_json_path = temp_file("firebreak_weighted_greedy_dpv_oos_solution.json");
    greedy_options.solution_csv_path = temp_file("firebreak_weighted_greedy_dpv_oos_solution.csv");
    fs::remove(greedy_options.output_json_path);
    fs::remove(greedy_options.output_csv_path);
    fs::remove(greedy_options.solution_json_path);
    fs::remove(greedy_options.solution_csv_path);
    assert(firebreak::experiments::GreedyOutOfSampleRunner().run(greedy_options) == 0);

    assert(fs::exists(static_options.output_json_path));
    assert(fs::exists(greedy_options.output_json_path));
    fs::remove(weight_map);
    fs::remove(static_options.output_json_path);
    fs::remove(static_options.output_csv_path);
    fs::remove(static_options.solution_json_path);
    fs::remove(static_options.solution_csv_path);
    fs::remove(greedy_options.output_json_path);
    fs::remove(greedy_options.output_csv_path);
    fs::remove(greedy_options.solution_json_path);
    fs::remove(greedy_options.solution_csv_path);

    std::cout << "Weighted DPV heuristic OOS test passed.\n";
    return 0;
}
