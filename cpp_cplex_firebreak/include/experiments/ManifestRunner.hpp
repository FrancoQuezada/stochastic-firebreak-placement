#pragma once

#include <filesystem>
#include <string>

namespace firebreak::experiments {

struct ManifestRunOptions {
    std::filesystem::path manifest_path;
    std::string executable_command;
    bool rerun_existing = false;
};

class ManifestRunner {
public:
    int run(const ManifestRunOptions& options) const;
};

}  // namespace firebreak::experiments
