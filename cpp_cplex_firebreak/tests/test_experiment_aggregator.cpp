#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "analysis/ExperimentAggregator.hpp"

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return content;
}

void write_fake_batch_csv(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out
        << "experiment_id,case_id,run_id,timestamp,landscape,method,alpha,budget,"
        << "train_scenario_count,test_scenario_count,objective_metric,train_ids,test_ids,"
        << "solver_status,objective_in_sample,best_bound,mip_gap,runtime_seconds,"
        << "solver_status_code,num_variables,num_constraints,selected_firebreaks,"
        << "warm_start_used,warm_start_source,warm_start_valid_nodes,warm_start_ignored_nodes,warm_start_notes,"
        << "train_expected_burned_area,train_worst_10pct_burned_area,"
        << "test_expected_burned_area,test_worst_10pct_burned_area,graph_type_note,notes\n";
    out
        << "exp,0,fpp,2026,Sub20,FPP-SAA,0.01000000,4,2,3,expected,1;2,3;4;5,"
        << "Optimal,10,10,0,1,0,10,5,154;174,false,,,,,"
        << "40,60,100,130,directed,note\n";
    out
        << "exp,0,dpv,2026,Sub20,DPV-SAA,0.01000000,4,2,3,dpv,1;2,3;4;5,"
        << "Optimal,20,20,0,2,0,20,10,154;174,false,,,,,"
        << "42,62,90,120,directed,note\n";
    out
        << "exp,1,fpp2,2026,Sub20,FPP-SAA,0.01000000,4,2,3,expected,6;7,8;9;10,"
        << "Optimal,10,10,0,1,0,10,5,154;174,false,,,,,"
        << "50,70,80,100,directed,note\n";
    out
        << "exp,1,dpv2,2026,Sub20,DPV-SAA,0.01000000,4,2,3,dpv,6;7,8;9;10,"
        << "Optimal,20,20,0,2,0,20,10,154;174,false,,,,,"
        << "50,70,80,100,directed,note\n";
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / "firebreak_aggregator_test";
    std::filesystem::remove_all(root);
    const auto input = root / "batch_results.csv";
    const auto output = root / "summary";
    write_fake_batch_csv(input);

    firebreak::analysis::ExperimentAggregator aggregator;
    const auto summary = aggregator.aggregate(input, output);

    assert(summary.pairwise_cases == 2);
    assert(summary.dpv_wins == 1);
    assert(summary.fpp_wins == 0);
    assert(summary.ties == 1);
    assert(std::filesystem::exists(output / "summary_by_method.csv"));
    assert(std::filesystem::exists(output / "pairwise_comparison_fpp_vs_dpv.csv"));

    const auto pairwise = read_file(output / "pairwise_comparison_fpp_vs_dpv.csv");
    assert(pairwise.find("DPV-SAA") != std::string::npos);
    assert(pairwise.find("Tie") != std::string::npos);

    std::filesystem::remove_all(root);
    std::cout << "All experiment aggregator tests passed.\n";
    return 0;
}
