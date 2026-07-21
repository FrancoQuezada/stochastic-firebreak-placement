#include "solver/FppWeightedLossUtils.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "core/LandscapeWeightMap.hpp"
#include "io/PathUtils.hpp"

namespace firebreak::solver {

namespace {

void validate_direct_fpp_compact_weights(
    const std::vector<double>& weights,
    int node_count) {
    if (weights.size() != static_cast<std::size_t>(node_count)) {
        throw std::runtime_error(
            "Direct FPP-SAA compact weight vector does not cover the optimization node universe.");
    }
    for (const double weight : weights) {
        if (!std::isfinite(weight) || weight <= 0.0) {
            throw std::runtime_error(
                "Direct FPP-SAA compact weights must be finite and strictly positive.");
        }
    }
}

bool has_attached_weight_map(const opt::OptimizationInstance& opt) {
    return !opt.cell_weight_map.weight_by_original_cell_id.empty();
}

}  // namespace

const std::vector<double>& direct_fpp_compact_weights(
    const opt::OptimizationInstance& opt) {
    validate_direct_fpp_compact_weights(opt.compact_cell_weights, opt.node_mapper.size());
    return opt.compact_cell_weights;
}

std::vector<double> direct_fpp_compact_weights_or_unit(
    const opt::OptimizationInstance& opt) {
    if (opt.compact_cell_weights.empty()) {
        return std::vector<double>(static_cast<std::size_t>(opt.node_mapper.size()), 1.0);
    }
    validate_direct_fpp_compact_weights(opt.compact_cell_weights, opt.node_mapper.size());
    return opt.compact_cell_weights;
}

double direct_fpp_max_scenario_loss_bound(
    const opt::OptimizationInstance& opt) {
    const auto weights = direct_fpp_compact_weights_or_unit(opt);
    double total = 0.0;
    for (const double weight : weights) {
        total += weight;
    }
    if (!std::isfinite(total) || total <= 0.0) {
        throw std::runtime_error("Direct FPP-SAA total compact weight must be positive and finite.");
    }
    return total;
}

std::string weighted_objective_metric_label(
    const risk::RiskMeasureConfig& risk_config) {
    if (risk_config.type == risk::RiskMeasureType::Expected) {
        return "weighted_expected_burn_loss";
    }
    if (risk_config.type == risk::RiskMeasureType::CVaR) {
        return "weighted_cvar_burn_loss";
    }
    return "weighted_mean_cvar_burn_loss";
}

double weighted_objective_from_components(
    double expected_weighted_loss,
    double weighted_cvar_loss,
    const risk::RiskMeasureConfig& risk_config) {
    if (risk_config.type == risk::RiskMeasureType::Expected) {
        return expected_weighted_loss;
    }
    if (risk_config.type == risk::RiskMeasureType::CVaR) {
        return weighted_cvar_loss;
    }
    return (1.0 - risk_config.cvarLambda) * expected_weighted_loss +
           risk_config.cvarLambda * weighted_cvar_loss;
}

double weighted_objective_from_recourse(
    const eval::FppRecourseResult& recourse,
    const risk::RiskMeasureConfig& risk_config) {
    return weighted_objective_from_components(
        recourse.expected_weighted_burn_loss,
        recourse.weighted_loss_statistics.cvar,
        risk_config);
}

void attach_direct_fpp_weight_metadata(
    ModelResult& result,
    const opt::OptimizationInstance& opt,
    const std::filesystem::path& weight_map_file) {
    result.objective_metric = weighted_objective_metric_label(
        risk::RiskMeasureConfig{
            risk::parse_risk_measure_type(result.risk_measure),
            result.cvar_beta,
            result.cvar_lambda,
        });
    result.weight_map_file = weight_map_file.empty() ? "" : weight_map_file.string();
    result.weight_profile = has_attached_weight_map(opt) ? opt.cell_weight_map.profile : "homogeneous";
    result.weight_map_hash = has_attached_weight_map(opt)
        ? opt.cell_weight_map.deterministic_hash
        : "";
    result.weight_normalized = has_attached_weight_map(opt) && opt.cell_weight_map.normalized;
    result.weight_mean = has_attached_weight_map(opt)
        ? opt.cell_weight_map.normalized_mean
        : 1.0;
    result.weight_min = has_attached_weight_map(opt)
        ? opt.cell_weight_map.minimum_weight
        : 1.0;
    result.weight_max = has_attached_weight_map(opt)
        ? opt.cell_weight_map.maximum_weight
        : 1.0;
    result.weight_total = has_attached_weight_map(opt)
        ? opt.cell_weight_map.total_weight
        : direct_fpp_max_scenario_loss_bound(opt);
    result.solver_weighted_objective = result.objective_value;
}

void attach_direct_fpp_validation(
    ModelResult& result,
    double evaluator_weighted_objective,
    double tolerance_abs,
    double tolerance_rel) {
    result.evaluator_weighted_objective = evaluator_weighted_objective;
    result.objective_validation_abs_difference =
        std::fabs(result.objective_value - evaluator_weighted_objective);
    result.objective_validation_rel_difference =
        result.objective_validation_abs_difference /
        std::max(1.0, std::fabs(evaluator_weighted_objective));
    result.objective_validation_passed =
        result.objective_validation_abs_difference <= tolerance_abs ||
        result.objective_validation_rel_difference <= tolerance_rel;
    result.evaluator_objective = evaluator_weighted_objective;
    result.evaluator_abs_diff = result.objective_validation_abs_difference;
    result.evaluator_rel_diff = result.objective_validation_rel_difference;
    result.validation_status = result.objective_validation_passed ? "pass" : "warn";
}

void attach_weight_map_to_optimization_instance(
    opt::OptimizationInstance& opt,
    const std::filesystem::path& weight_map_file,
    const std::string& expected_weight_map_hash,
    WeightMapAttachmentDiagnostics* diagnostics) {
    if (weight_map_file.empty()) {
        return;
    }
    const auto path = io::resolve_input_path(weight_map_file.string());
    // Subset-tolerant: no expected-ID set is passed here, so the canonical map may cover
    // more physical cells than this instance's compact universe. Coverage of every
    // instance compact cell is still enforced below by build_compact_weight_vector,
    // which throws if any compact node's original ID is missing from the map.
    opt.cell_weight_map = core::load_landscape_weight_map_csv(path);
    if (!expected_weight_map_hash.empty() &&
        opt.cell_weight_map.deterministic_hash != expected_weight_map_hash) {
        throw std::runtime_error(
            "Weight map hash mismatch: expected " + expected_weight_map_hash +
            " but loaded map " + path.string() + " has hash " +
            opt.cell_weight_map.deterministic_hash + ".");
    }
    opt.compact_cell_weights = core::build_compact_weight_vector(
        opt.cell_weight_map,
        opt.node_mapper);
    if (diagnostics != nullptr) {
        diagnostics->mapping_method = "original_cell_id";
        diagnostics->canonical_cell_count =
            static_cast<int>(opt.cell_weight_map.weight_by_original_cell_id.size());
        diagnostics->instance_cell_count = opt.node_mapper.size();
        diagnostics->mapped_instance_cell_count = opt.node_mapper.size();
        diagnostics->missing_instance_cell_count = 0;
        diagnostics->duplicate_instance_original_id_count = 0;
        diagnostics->unused_canonical_cell_count =
            diagnostics->canonical_cell_count - diagnostics->mapped_instance_cell_count;
    }
    (void)direct_fpp_compact_weights(opt);
}

}  // namespace firebreak::solver
