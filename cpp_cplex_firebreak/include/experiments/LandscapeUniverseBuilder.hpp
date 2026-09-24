#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/LandscapeWeightGenerator.hpp"
#include "io/Cell2FireReader.hpp"

namespace firebreak::experiments {

// Build the full physical landscape cell universe (original Cell2Fire IDs, grid
// coordinates) from Cell2Fire forest metadata. Uses available_nodes when known, else all
// IDs 1..n_cells. This is the single source of the canonical physical universe; it is
// never derived from a scenario graph, the reduced compact graph, or burned cells.
core::LandscapeCellUniverse build_landscape_cell_universe(
    const io::Cell2FireReader::ForestInfo& forest_info);

// Load forest info for a landscape and build its physical universe. Appends any reader
// warnings to `warnings`.
core::LandscapeCellUniverse load_landscape_cell_universe(
    const std::filesystem::path& forest_path,
    const std::filesystem::path& results_path,
    std::vector<std::string>& warnings);

}  // namespace firebreak::experiments
