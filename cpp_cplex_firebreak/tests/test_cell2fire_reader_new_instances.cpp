#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "io/Cell2FireReader.hpp"
#include "io/ScenarioFileUtils.hpp"

namespace {

namespace fs = std::filesystem;

fs::path project_root() {
#ifdef FIREBREAK_PROJECT_ROOT
    return fs::path(FIREBREAK_PROJECT_ROOT);
#else
    return fs::current_path();
#endif
}

bool contains_warning(const std::vector<std::string>& warnings, const std::string& needle) {
    for (const auto& warning : warnings) {
        if (warning.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void test_legacy_sub20_still_loads() {
    const fs::path root = project_root();
    const fs::path forest_path = root.parent_path() / "sample_test" / "data" / "CanadianFBP" / "Sub20";
    const fs::path results_path = root.parent_path() / "sample_test" / "Sub20";

    firebreak::io::Cell2FireReader reader;
    assert(reader.detect_layout(results_path) == firebreak::io::Cell2FireLayout::Legacy);

    std::vector<std::string> warnings;
    const auto instance = reader.load_instance(
        "Sub20",
        forest_path,
        results_path,
        {1},
        warnings);

    assert(instance.landscape_name == "Sub20");
    assert(instance.has_forest_size);
    assert(instance.n_cells == 400);
    assert(instance.scenarios.size() == 1);
    assert(instance.scenarios.front().scenario_id == 1);
    assert(instance.scenarios.front().ignition_node > 0);
    assert(instance.scenarios.front().weather_metadata.empty());
}

void assert_new_folder_loads(const std::string& folder_name, int expected_n_cells) {
    const fs::path folder = project_root() / "new_instances" / folder_name;

    firebreak::io::Cell2FireReader reader;
    assert(reader.detect_layout(folder) == firebreak::io::Cell2FireLayout::NewInstances);

    const auto inventory = firebreak::io::detect_message_files(folder);
    assert(inventory.count() == 10000);
    assert(inventory.contains(1));
    assert(inventory.contains(2));
    assert(inventory.contains(10000));

    std::vector<std::string> warnings;
    const auto instance = reader.load_instance(
        folder_name,
        folder,
        folder,
        {1, 2, 10000},
        warnings);

    assert(instance.has_forest_size);
    assert(instance.n_cells == expected_n_cells);
    assert(instance.available_nodes_known);
    assert(static_cast<int>(instance.available_nodes.size()) == expected_n_cells);
    assert(instance.scenarios.size() == 3);
    assert(instance.scenarios[0].scenario_id == 1);
    assert(instance.scenarios[0].message_filename == "MessagesFile00001.csv");
    assert(instance.scenarios[0].ignition_node > 0);
    assert(!instance.scenarios[0].weather_metadata.empty());
    assert(instance.scenarios[1].scenario_id == 2);
    assert(instance.scenarios[1].message_filename == "MessagesFile00002.csv");
    assert(instance.scenarios[2].scenario_id == 10000);
    assert(instance.scenarios[2].message_filename == "MessagesFile10000.csv");
}

void test_valid_new_instance_folders_load() {
    assert_new_folder_loads("20x20", 400);
    assert_new_folder_loads("20x20_reburn", 400);
    assert_new_folder_loads("40x40", 1600);
    assert_new_folder_loads("40x40_reburn", 1600);
}

void test_100x100_metadata_inconsistency_is_explicit() {
    for (const std::string folder_name : {"100x100", "100x100_reburn"}) {
        const fs::path folder = project_root() / "new_instances" / folder_name;
        firebreak::io::Cell2FireReader reader;
        std::vector<std::string> warnings;
        const auto instance = reader.load_instance(
            folder_name,
            folder,
            folder,
            {1, 2, 10000},
            warnings);

        assert(instance.has_forest_size);
        assert(instance.n_cells == 1600);
        assert(contains_warning(warnings, "Metadata inconsistency"));
        assert(contains_warning(warnings, "folder name suggests 100x100"));
    }
}

}  // namespace

int main() {
    test_legacy_sub20_still_loads();
    test_valid_new_instance_folders_load();
    test_100x100_metadata_inconsistency_is_explicit();
    std::cout << "All Cell2Fire reader new-instances tests passed.\n";
    return 0;
}
