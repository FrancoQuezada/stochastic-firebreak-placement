#include "experiments/LandscapeUniverseBuilder.hpp"

#include <stdexcept>

namespace firebreak::experiments {

core::LandscapeCellUniverse build_landscape_cell_universe(
    const io::Cell2FireReader::ForestInfo& forest_info) {
    if (!forest_info.has_size || forest_info.n_cells <= 0) {
        throw std::runtime_error("Could not determine a nonempty landscape cell universe.");
    }

    std::vector<int> ids;
    std::string source;
    if (forest_info.available_known && !forest_info.available_nodes.empty()) {
        ids = forest_info.available_nodes;
        source = "forest.available_nodes from fuels.asc and fbp_lookup_table.csv";
    } else {
        ids.reserve(static_cast<std::size_t>(forest_info.n_cells));
        for (int cell_id = 1; cell_id <= forest_info.n_cells; ++cell_id) {
            ids.push_back(cell_id);
        }
        source = "fallback all Cell2Fire IDs 1..NCells from forest metadata";
    }

    const int cols = forest_info.cols > 0 ? forest_info.cols : forest_info.n_cells;
    core::LandscapeCellUniverse universe;
    universe.source = source;
    universe.rows = forest_info.rows;
    universe.cols = forest_info.cols;
    universe.cells.reserve(ids.size());
    for (const int cell_id : ids) {
        const int zero_based = cell_id - 1;
        universe.cells.push_back(core::WeightedLandscapeCell{
            cell_id,
            zero_based / cols + 1,
            zero_based % cols + 1});
    }
    return universe;
}

core::LandscapeCellUniverse load_landscape_cell_universe(
    const std::filesystem::path& forest_path,
    const std::filesystem::path& results_path,
    std::vector<std::string>& warnings) {
    io::Cell2FireReader reader;
    auto layout = io::Cell2FireLayout::Legacy;
    try {
        layout = reader.detect_layout(results_path);
    } catch (const std::runtime_error& exc) {
        warnings.push_back(
            std::string("Could not detect Cell2Fire result layout for metadata checks: ") +
            exc.what());
    }
    const auto forest_info = reader.read_forest_info(forest_path, results_path, layout, warnings);
    return build_landscape_cell_universe(forest_info);
}

}  // namespace firebreak::experiments
