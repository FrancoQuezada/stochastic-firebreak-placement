#include "io/PathUtils.hpp"

#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace firebreak::io {

namespace fs = std::filesystem;

fs::path project_root() {
    return fs::weakly_canonical(fs::path(FIREBREAK_PROJECT_ROOT));
}

fs::path repo_root() {
    return fs::weakly_canonical(fs::path(FIREBREAK_REPO_ROOT));
}

fs::path resolve_input_path(const std::string& input) {
    fs::path path(input);
    if (path.is_absolute()) {
        return fs::weakly_canonical(path);
    }

    const std::vector<fs::path> candidates = {
        fs::current_path() / path,
        repo_root() / path,
        project_root() / path,
    };

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) {
            return fs::weakly_canonical(candidate);
        }
    }

    return fs::weakly_canonical(fs::current_path() / path);
}

fs::path resolve_output_path(const std::string& input) {
    fs::path path(input);
    if (path.is_absolute()) {
        return fs::weakly_canonical(path);
    }
    return fs::weakly_canonical(project_root() / path);
}

void ensure_parent_directory(const fs::path& path) {
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent);
    }
}

std::string path_to_string(const fs::path& path) {
    return path.lexically_normal().string();
}

}  // namespace firebreak::io

