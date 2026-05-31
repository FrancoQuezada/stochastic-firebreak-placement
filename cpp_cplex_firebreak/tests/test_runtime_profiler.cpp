#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "analysis/RuntimeProfiler.hpp"

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / "firebreak_runtime_profiler_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto input = root / "batch_results.csv";
    const auto output = root / "runtime_summary.csv";
    {
        std::ofstream out(input);
        out << "experiment_id,case_id,run_id,timestamp,landscape,method,alpha,budget,train_scenario_count,test_scenario_count,"
            << "objective_metric,train_ids,test_ids,solver_status,objective_in_sample,best_bound,mip_gap,runtime_seconds,"
            << "solver_status_code,num_variables,num_constraints,selected_firebreaks,warm_start_used,warm_start_source,"
            << "warm_start_valid_nodes,warm_start_ignored_nodes,warm_start_notes,train_expected_burned_area,"
            << "train_worst_10pct_burned_area,test_expected_burned_area,test_worst_10pct_burned_area,graph_type_note,notes\n";
        out << "exp,0,r1,now,Sub20,FPP-SAA,0.01000000,4,5,30,expected,1,2,Optimal,1,1,0.01,2,0,10,20,1,false,,,,,1,1,1,1,directed,note\n";
        out << "exp,1,r2,now,Sub20,FPP-SAA,0.01000000,4,5,30,expected,1,2,TimeLimit,1,1,0.03,4,0,20,40,1,false,,,,,1,1,1,1,directed,note\n";
        out << "exp,0,r3,now,Sub20,DPV-SAA,0.01000000,4,5,30,dpv,1,2,Optimal,1,1,0.02,10,0,30,60,1,false,,,,,1,1,1,1,directed,note\n";
    }

    firebreak::analysis::RuntimeProfiler profiler;
    profiler.write_runtime_summary(input, output);

    assert(std::filesystem::exists(output));
    const auto text = read_file(output);
    assert(text.find("FPP-SAA,0.01000000,5,2,3.00000000,3.00000000,4.00000000,0.02000000,1,1,15.00000000,30.00000000") != std::string::npos);
    assert(text.find("DPV-SAA,0.01000000,5,1,10.00000000,10.00000000,10.00000000,0.02000000,1,0,30.00000000,60.00000000") != std::string::npos);

    std::filesystem::remove_all(root);
    std::cout << "All runtime profiler tests passed.\n";
    return 0;
}
