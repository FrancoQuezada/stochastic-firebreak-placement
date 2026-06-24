#pragma once

#include <filesystem>

namespace firebreak::experiments {

struct NewInstancesSmokeOptions {
    std::filesystem::path instances_root;
    std::filesystem::path output_path;
    bool strict_metadata = false;
};

class NewInstancesSmokeRunner {
public:
    int run(const NewInstancesSmokeOptions& options) const;
};

}  // namespace firebreak::experiments
