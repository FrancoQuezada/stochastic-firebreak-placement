#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "analysis/BatchSummaryReporter.hpp"

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / "firebreak_summary_reporter_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto batch_csv = root / "batch_results.csv";
    const auto summary_csv = root / "summary_by_method.csv";
    const auto pairwise_csv = root / "pairwise_comparison_fpp_vs_dpv.csv";
    const auto runtime_csv = root / "runtime_summary.csv";
    const auto report = root / "summary_report.txt";

    {
        std::ofstream out(batch_csv);
        out << "experiment_id,case_id,run_id,timestamp,landscape,method,alpha,budget,train_scenario_count,test_scenario_count,"
            << "objective_metric,train_ids,test_ids,solver_status,objective_in_sample,best_bound,mip_gap,runtime_seconds,"
            << "solver_status_code,num_variables,num_constraints,selected_firebreaks,warm_start_used,warm_start_source,"
            << "warm_start_valid_nodes,warm_start_ignored_nodes,warm_start_notes,train_expected_burned_area,"
            << "train_worst_10pct_burned_area,test_expected_burned_area,test_worst_10pct_burned_area,graph_type_note,notes\n";
        out << "exp,0,fpp,now,Sub20,FPP-SAA,0.01000000,4,2,3,expected,1;2,3;4;5,Optimal,1,1,0,1,0,1,1,10;20,false,,,,,10,20,100,150,directed,note\n";
        out << "exp,0,dpv,now,Sub20,DPV-SAA,0.01000000,4,2,3,dpv,1;2,3;4;5,Optimal,2,2,0,2,0,2,2,10;20,false,,,,,11,21,90,140,directed,note\n";
        out << "exp,1,fpp2,now,Sub20,FPP-SAA,0.01000000,4,2,3,expected,6;7,8;9;10,Optimal,1,1,0,1,0,1,1,10;20,false,,,,,10,20,80,100,directed,note\n";
        out << "exp,1,dpv2,now,Sub20,DPV-SAA,0.01000000,4,2,3,dpv,6;7,8;9;10,Optimal,2,2,0,2,0,2,2,10;20,false,,,,,11,21,90,110,directed,note\n";
    }
    {
        std::ofstream out(summary_csv);
        out << "landscape,alpha,train_count,method,mean_test_expected_burned_area,mean_test_worst_10pct_burned_area,"
            << "mean_train_expected_burned_area,mean_runtime_seconds,number_of_cases_solved,number_of_optimal_solves,mean_mip_gap\n";
        out << "Sub20,0.01000000,2,FPP-SAA,90,125,10,1,2,2,0\n";
        out << "Sub20,0.01000000,2,DPV-SAA,90,125,11,2,2,2,0\n";
    }
    {
        std::ofstream out(pairwise_csv);
        out << "experiment_id,case_id,landscape,alpha,train_count,test_count,test_expected_burned_area_fpp,"
            << "test_expected_burned_area_dpv,difference,relative_difference,winner\n";
        out << "exp,0,Sub20,0.01000000,2,3,100,90,-10,-0.1,DPV-SAA\n";
        out << "exp,1,Sub20,0.01000000,2,3,80,90,10,0.125,FPP-SAA\n";
        out << "exp,2,Sub20,0.01000000,2,3,70,70,0,0,Tie\n";
    }
    {
        std::ofstream out(runtime_csv);
        out << "method,alpha,train_count,count,mean_runtime_seconds,median_runtime_seconds,max_runtime_seconds,"
            << "mean_mip_gap,number_optimal,number_non_optimal,mean_num_variables,mean_num_constraints\n";
        out << "FPP-SAA,0.01000000,2,2,1.00000000,1.00000000,1.00000000,0,2,0,10,5\n";
        out << "DPV-SAA,0.01000000,2,2,2.00000000,2.00000000,2.00000000,0,2,0,20,10\n";
    }

    firebreak::analysis::BatchSummaryReporter reporter;
    reporter.write_report(batch_csv, summary_csv, pairwise_csv, runtime_csv, report, "test_report");

    assert(std::filesystem::exists(report));
    const auto text = read_text(report);
    assert(text.find("DPV wins: 1") != std::string::npos);
    assert(text.find("FPP wins: 1") != std::string::npos);
    assert(text.find("Ties: 1") != std::string::npos);
    assert(text.find("Runtime By Method") != std::string::npos);
    assert(text.find("mean_mip_gap") != std::string::npos);

    std::filesystem::remove_all(root);
    std::cout << "All batch summary reporter tests passed.\n";
    return 0;
}
