#pragma once

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

#include "core/Instance.hpp"
#include "io/ScenarioFileUtils.hpp"

namespace firebreak::io {

std::string json_escape(const std::string& value);

void write_smoke_summary_json(
    const std::filesystem::path& output_path,
    const core::Instance& instance,
    const ScenarioInventory& inventory,
    const std::vector<int>& requested_ids,
    const std::vector<std::string>& warnings);

void print_smoke_summary(
    std::ostream& out,
    const core::Instance& instance,
    const ScenarioInventory& inventory,
    const std::vector<int>& requested_ids,
    const std::vector<std::string>& warnings);

}  // namespace firebreak::io

