#pragma once

#include <filesystem>
#include <string>

namespace firebreak::io {

std::filesystem::path project_root();
std::filesystem::path repo_root();

std::filesystem::path resolve_input_path(const std::string& input);
std::filesystem::path resolve_output_path(const std::string& input);

void ensure_parent_directory(const std::filesystem::path& path);
std::string path_to_string(const std::filesystem::path& path);

}  // namespace firebreak::io

