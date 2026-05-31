#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "solver/WarmStart.hpp"

namespace {

firebreak::opt::OptimizationInstance make_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "synthetic";
    opt.n_cells = 5;
    opt.budget = 3;
    opt.node_mapper.build_from_nodes({10, 20, 30, 40, 50});
    opt.eligible_original_nodes = {10, 20, 30, 40, 50};
    for (const int original_node : opt.eligible_original_nodes) {
        opt.eligible_indices.push_back(opt.node_mapper.to_index(original_node));
    }
    return opt;
}

std::filesystem::path write_temp_csv(const std::string& content) {
    const auto path = std::filesystem::temp_directory_path() / "firebreak_warm_start_test.csv";
    std::ofstream out(path);
    out << content;
    return path;
}

void test_load_simple_csv() {
    const auto opt = make_instance();
    const auto path = write_temp_csv("10,20,30\n");
    const auto warm_start = firebreak::solver::load_warm_start_from_csv(path, opt, 3);

    assert(warm_start.enabled);
    assert((warm_start.original_node_ids == std::vector<int>{10, 20, 30}));
    assert((warm_start.compact_indices == std::vector<int>{0, 1, 2}));
    assert(warm_start.status == "Ready");
}

void test_duplicate_removal() {
    const auto opt = make_instance();
    const auto warm_start = firebreak::solver::prepare_warm_start_from_original_nodes(
        {10, 20, 10, 30},
        opt,
        3,
        "memory");

    assert((warm_start.original_node_ids == std::vector<int>{10, 20, 30}));
    assert((warm_start.duplicate_original_node_ids == std::vector<int>{10}));
}

void test_mapping_to_compact_indices() {
    const auto opt = make_instance();
    const auto warm_start = firebreak::solver::prepare_warm_start_from_original_nodes(
        {30, 10},
        opt,
        3,
        "memory");

    assert((warm_start.original_node_ids == std::vector<int>{30, 10}));
    assert((warm_start.compact_indices == std::vector<int>{2, 0}));
}

void test_eligibility_filtering() {
    auto opt = make_instance();
    opt.eligible_original_nodes = {10, 30};
    opt.eligible_indices = {
        opt.node_mapper.to_index(10),
        opt.node_mapper.to_index(30),
    };

    const auto warm_start = firebreak::solver::prepare_warm_start_from_original_nodes(
        {10, 20, 30},
        opt,
        3,
        "memory");

    assert((warm_start.original_node_ids == std::vector<int>{10, 30}));
    assert((warm_start.ignored_original_node_ids == std::vector<int>{20}));
    assert(warm_start.status == "Partial");
}

void test_budget_trimming() {
    const auto opt = make_instance();
    const auto warm_start = firebreak::solver::prepare_warm_start_from_original_nodes(
        {10, 20, 30, 40, 50},
        opt,
        3,
        "memory");

    assert((warm_start.original_node_ids == std::vector<int>{10, 20, 30}));
    assert((warm_start.compact_indices == std::vector<int>{0, 1, 2}));
    assert((warm_start.trimmed_original_node_ids == std::vector<int>{40, 50}));
    assert(warm_start.status == "Ready");
}

}  // namespace

int main() {
    test_load_simple_csv();
    test_duplicate_removal();
    test_mapping_to_compact_indices();
    test_eligibility_filtering();
    test_budget_trimming();
    std::cout << "All warm-start tests passed.\n";
    return 0;
}
