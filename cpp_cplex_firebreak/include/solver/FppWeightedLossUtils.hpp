#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "eval/FppRecourseEvaluator.hpp"
#include "opt/OptimizationInstance.hpp"
#include "risk/RiskMeasure.hpp"
#include "solver/ModelResult.hpp"

namespace firebreak::solver {

struct ScenarioLossCoefficientDescriptor {
    int scenario_id = 0;
    int node_index = -1;
    double coefficient = 0.0;
};

const std::vector<double>& direct_fpp_compact_weights(
    const opt::OptimizationInstance& opt);

std::vector<double> direct_fpp_compact_weights_or_unit(
    const opt::OptimizationInstance& opt);

double direct_fpp_max_scenario_loss_bound(
    const opt::OptimizationInstance& opt);

std::string weighted_objective_metric_label(
    const risk::RiskMeasureConfig& risk_config);

double weighted_objective_from_components(
    double expected_weighted_loss,
    double weighted_cvar_loss,
    const risk::RiskMeasureConfig& risk_config);

double weighted_objective_from_recourse(
    const eval::FppRecourseResult& recourse,
    const risk::RiskMeasureConfig& risk_config);

void attach_direct_fpp_weight_metadata(
    ModelResult& result,
    const opt::OptimizationInstance& opt,
    const std::filesystem::path& weight_map_file = {});

void attach_direct_fpp_validation(
    ModelResult& result,
    double evaluator_weighted_objective,
    double tolerance_abs = 1.0e-6,
    double tolerance_rel = 1.0e-6);

// Diagnostics from attaching a canonical weight map to an instance whose compact
// universe may be a strict subset of the map's physical universe.
struct WeightMapAttachmentDiagnostics {
    std::string mapping_method = "original_cell_id";
    int canonical_cell_count = 0;
    int instance_cell_count = 0;
    int mapped_instance_cell_count = 0;
    int unused_canonical_cell_count = 0;
    int missing_instance_cell_count = 0;
    int duplicate_instance_original_id_count = 0;
};

// Attach a canonical weight-map CSV to an optimization instance. Subset-tolerant: the
// map may cover more physical cells than the instance's compact universe (every
// canonical-map cell absent from the instance is simply unused); every instance compact
// cell must be present in the map (missing coverage is a hard error). When
// `expected_weight_map_hash` is nonempty, the loaded map's deterministic hash must match
// it exactly or this throws (verifies the same canonical map is used at every stage).
void attach_weight_map_to_optimization_instance(
    opt::OptimizationInstance& opt,
    const std::filesystem::path& weight_map_file,
    const std::string& expected_weight_map_hash = "",
    WeightMapAttachmentDiagnostics* diagnostics = nullptr);

}  // namespace firebreak::solver
