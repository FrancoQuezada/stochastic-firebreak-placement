#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "experiments/BatchExperimentConfig.hpp"
#include "experiments/ExperimentManifest.hpp"

namespace {

namespace fs = std::filesystem;

template <typename Fn>
void expect_throw(Fn&& fn, const std::string& fragment) {
    bool threw = false;
    try {
        fn();
    } catch (const std::runtime_error& exc) {
        threw = true;
        const std::string message = exc.what();
        assert(message.find(fragment) != std::string::npos);
    }
    assert(threw);
}

fs::path scratch_dir() {
    const char* base = std::getenv("FIREBREAK_TEST_TMP");
    fs::path root = base ? fs::path(base) : fs::temp_directory_path();
    root /= "phase8b_batch_config";
    fs::create_directories(root);
    return root;
}

void write_manifest(const fs::path& path, const std::string& body) {
    std::ofstream out(path);
    out << body;
}

std::string base_manifest_body(const fs::path& output_dir) {
    return
        "experiment_name=phase8b_test\n"
        "landscape=new20x20\n"
        "alphas=0.02\n"
        "train_counts=8\n"
        "test_count=4\n"
        "num_cases=1\n"
        "seed_base=1\n"
        "methods=FPP-SAA\n"
        "output_dir=" + output_dir.string() + "\n";
}

void test_legacy_manifest_resolves_to_homogeneous() {
    const auto path = scratch_dir() / "legacy_manifest.txt";
    write_manifest(path, base_manifest_body(scratch_dir() / "legacy_out"));

    const auto manifest = firebreak::experiments::load_experiment_manifest(path);
    assert(manifest.config.weight_profile.empty());
    assert(manifest.config.weight_map_file.empty());
    assert(manifest.config.weight_replicate == 0);
    assert(manifest.config.canonical_landscape_id.empty());
}

void test_weighted_manifest_round_trip() {
    const auto weights_csv = scratch_dir() / "weights.csv";
    {
        std::ofstream out(weights_csv);
        out << "cell_id,raw_weight,normalized_weight,cluster_id\n1,1.0,1.0,0\n";
    }
    std::ostringstream body;
    body << base_manifest_body(scratch_dir() / "weighted_out");
    body << "weight_map_file=" << weights_csv.string() << "\n"
         << "weight_profile=heterogeneous\n"
         << "weight_replicate=2\n"
         << "weight_generation_seed=123456789\n"
         << "weight_generator_version=1\n"
         << "canonical_landscape_id=new20x20__20x20__deadbeefdeadbeef\n"
         << "paired_landscape_id=new20x20_reburn__20x20__deadbeefdeadbeef\n"
         << "weight_map_hash=fnv1a64:1111111111111111\n"
         << "weight_source_universe_hash=fnv1a64:deadbeefdeadbeef\n"
         << "paired_reburn_instance_id=new20x20_reburn\n"
         << "paired_evaluation_enabled=true\n";

    const auto path = scratch_dir() / "weighted_manifest.txt";
    write_manifest(path, body.str());

    const auto manifest = firebreak::experiments::load_experiment_manifest(path);
    const auto& c = manifest.config;
    assert(c.weight_map_file == weights_csv);
    assert(c.weight_profile == "heterogeneous");
    assert(c.weight_replicate == 2);
    assert(c.weight_generation_seed == 123456789ull);
    assert(c.weight_generator_version == 1);
    assert(c.canonical_landscape_id == "new20x20__20x20__deadbeefdeadbeef");
    assert(c.paired_landscape_id == "new20x20_reburn__20x20__deadbeefdeadbeef");
    assert(c.weight_map_hash == "fnv1a64:1111111111111111");
    assert(c.weight_source_universe_hash == "fnv1a64:deadbeefdeadbeef");
    assert(c.paired_reburn_instance_id == "new20x20_reburn");
    assert(c.paired_evaluation_enabled);

    const auto description = firebreak::experiments::describe_manifest_config(manifest);
    assert(description.find("heterogeneous") != std::string::npos);
    assert(description.find("new20x20__20x20__deadbeefdeadbeef") != std::string::npos);
}

void test_weight_profile_without_map_file_rejected() {
    std::ostringstream body;
    body << base_manifest_body(scratch_dir() / "bad_out");
    body << "weight_profile=heterogeneous\n";
    const auto path = scratch_dir() / "bad_manifest.txt";
    write_manifest(path, body.str());

    expect_throw(
        [&] { firebreak::experiments::load_experiment_manifest(path); },
        "weight_map_file is empty");
}

void test_negative_weight_replicate_rejected() {
    firebreak::experiments::BatchExperimentConfig config;
    config.landscape = "new20x20";
    config.alpha_values = {0.02};
    config.train_counts = {8};
    config.test_count = 4;
    config.num_cases = 1;
    config.methods = {"FPP-SAA"};
    config.output_dir = scratch_dir() / "neg_replicate_out";
    config.output_csv = config.output_dir / "batch_results.csv";
    config.weight_replicate = -1;
    expect_throw(
        [&] { firebreak::experiments::validate_batch_experiment_config(config); },
        "weight_replicate must be nonnegative");
}

}  // namespace

int main() {
    test_legacy_manifest_resolves_to_homogeneous();
    test_weighted_manifest_round_trip();
    test_weight_profile_without_map_file_rejected();
    test_negative_weight_replicate_rejected();
    std::cout << "All weighted batch config tests passed.\n";
    return 0;
}
