#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "benders/FppProjectedLlbi.hpp"
#include "experiments/BatchExperimentConfig.hpp"

namespace {

void assert_close(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1.0e-6);
}

firebreak::opt::OptimizationInstance make_path_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "projected_llbi_path";
    opt.alpha = 1.0 / 3.0;
    opt.n_cells = 3;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3});
    opt.eligible_indices = {0, 1, 2};
    opt.eligible_original_nodes = {1, 2, 3};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 7;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2};
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 2, 2, 3});

    opt.total_arcs = scenario.arcs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

firebreak::opt::OptimizationInstance make_dag_instance() {
    firebreak::opt::OptimizationInstance opt;
    opt.landscape_name = "projected_llbi_dag";
    opt.alpha = 0.5;
    opt.n_cells = 4;
    opt.budget = 1;
    opt.node_mapper.build_from_nodes({1, 2, 3, 4});
    opt.eligible_indices = {0, 1, 2, 3};
    opt.eligible_original_nodes = {1, 2, 3, 4};

    firebreak::opt::OptimizationScenario scenario;
    scenario.scenario_id = 11;
    scenario.probability = 1.0;
    scenario.ignition_index = 0;
    scenario.ignition_original_node = 1;
    scenario.observed_node_indices = {0, 1, 2, 3};
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 1, 1, 2});
    scenario.arcs.push_back(firebreak::opt::CompactArc{0, 2, 1, 3});
    scenario.arcs.push_back(firebreak::opt::CompactArc{1, 3, 2, 4});
    scenario.arcs.push_back(firebreak::opt::CompactArc{2, 3, 3, 4});

    opt.total_arcs = scenario.arcs.size();
    opt.scenarios.push_back(scenario);
    opt.scenario_probabilities = {1.0};
    return opt;
}

std::vector<double> compact_values(const firebreak::opt::OptimizationInstance& opt,
                                   const std::vector<double>& eligible_values) {
    std::vector<double> values(static_cast<std::size_t>(opt.node_mapper.size()), 0.0);
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        values[static_cast<std::size_t>(opt.eligible_indices[pos])] =
            eligible_values[pos];
    }
    return values;
}

void test_projected_coverage_exp_tree_separation() {
    const auto opt = make_path_instance();
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_coverage_llbi_exp = true;
    options.max_cuts_per_round = 10;
    const std::vector<double> ybar_by_eligible = {0.0, 0.25, 0.50};
    const std::vector<double> etabar = {0.0};

    const auto separated = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        ybar_by_eligible,
        etabar);

    assert(separated.scenarios_checked == 1);
    assert(separated.violated_cuts_found == 1);
    assert(separated.cuts.size() == 1);
    const auto& cut = separated.cuts.front().cut;
    assert_close(cut.rhs_constant, 3.0);
    assert(cut.coefficients_by_compact_index.size() == 2);
    assert((cut.coefficients_by_compact_index[0] == std::pair<int, double>{1, -2.0}));
    assert((cut.coefficients_by_compact_index[1] == std::pair<int, double>{2, -1.0}));
    assert_close(cut.evaluateAt(compact_values(opt, ybar_by_eligible)), 2.0);
    assert_close(separated.cuts.front().violation, 2.0);
}

void test_projected_coverage_poly_uses_original_variables_only() {
    const auto opt = make_path_instance();
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_coverage_llbi_poly = true;
    firebreak::benders::FppProjectedLlbiStats stats;
    const auto cuts =
        firebreak::benders::build_fpp_projected_llbi_poly_cuts(opt, options, &stats);

    assert(cuts.size() == 1);
    assert(stats.projected_poly_candidate_cuts_generated == 1);
    assert(stats.projected_poly_candidate_cuts_added == 1);
    assert(firebreak::benders::active_projected_llbi_mode(options) ==
           firebreak::benders::FppProjectedLlbiMode::Poly);
    assert_close(cuts.front().rhs_constant, 3.0);
    for (const auto& [compact_node, _] : cuts.front().coefficients_by_compact_index) {
        assert(compact_node >= 0);
        assert(compact_node < opt.node_mapper.size());
    }
}

void test_projected_path_exp_tree_separation() {
    const auto opt = make_path_instance();
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_path_llbi_exp = true;
    options.max_cuts_per_round = 10;
    const std::vector<double> ybar_by_eligible = {0.0, 0.25, 0.50};
    const std::vector<double> etabar = {0.0};

    const auto separated = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        ybar_by_eligible,
        etabar);

    assert(separated.scenarios_checked == 1);
    assert(separated.violated_cuts_found == 1);
    assert(separated.cuts.size() == 1);
    const auto& cut = separated.cuts.front().cut;
    assert_close(cut.rhs_constant, 3.0);
    assert((cut.coefficients_by_compact_index[0] == std::pair<int, double>{1, -2.0}));
    assert((cut.coefficients_by_compact_index[1] == std::pair<int, double>{2, -1.0}));
    assert_close(cut.evaluateAt(compact_values(opt, ybar_by_eligible)), 2.0);
}

void test_projected_path_exp_dag_shortest_path_separation() {
    const auto opt = make_dag_instance();
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_path_llbi_exp = true;
    options.max_cuts_per_round = 10;
    const std::vector<double> ybar_by_eligible = {0.0, 0.20, 0.60, 0.30};
    const std::vector<double> etabar = {0.0};

    const auto separated = firebreak::benders::separate_fpp_projected_llbi_cuts(
        opt,
        options,
        ybar_by_eligible,
        etabar);

    assert(separated.scenarios_checked == 1);
    assert(separated.violated_cuts_found == 1);
    assert(separated.cuts.size() == 1);
    const auto& cut = separated.cuts.front().cut;
    assert_close(cut.rhs_constant, 4.0);
    assert((cut.coefficients_by_compact_index[0] == std::pair<int, double>{1, -2.0}));
    assert((cut.coefficients_by_compact_index[1] == std::pair<int, double>{2, -1.0}));
    assert((cut.coefficients_by_compact_index[2] == std::pair<int, double>{3, -1.0}));
    assert_close(cut.evaluateAt(compact_values(opt, ybar_by_eligible)), 2.7);
}

void test_projected_path_poly_uses_original_variables_only() {
    const auto opt = make_path_instance();
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_path_llbi_poly = true;
    firebreak::benders::FppProjectedLlbiStats stats;
    const auto cuts =
        firebreak::benders::build_fpp_projected_llbi_poly_cuts(opt, options, &stats);

    assert(cuts.size() == 1);
    assert(stats.projected_poly_candidate_cuts_generated == 1);
    assert(stats.projected_poly_candidate_cuts_added == 1);
    assert(firebreak::benders::active_projected_llbi_mode(options) ==
           firebreak::benders::FppProjectedLlbiMode::Poly);
    assert_close(cuts.front().rhs_constant, 3.0);
    for (const auto& [compact_node, _] : cuts.front().coefficients_by_compact_index) {
        assert(compact_node >= 0);
        assert(compact_node < opt.node_mapper.size());
    }
}

void test_incompatible_projected_options_rejected() {
    firebreak::benders::FppProjectedLlbiOptions options;
    options.use_projected_coverage_llbi_exp = true;
    options.use_projected_path_llbi_poly = true;
    bool threw = false;
    try {
        firebreak::benders::validate_fpp_projected_llbi_options(options);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_projected_label_parsing() {
    const std::vector<std::string> labels = {
        "FPP-Branch-Benders-ProjectedCoverageLLBI-poly",
        "FPP-Branch-Benders-ProjectedPathLLBI-poly",
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp",
        "FPP-Branch-Benders-ProjectedPathLLBI-exp",
        "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-poly",
        "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-poly",
        "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-exp",
        "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-exp",
        "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-poly",
        "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-poly",
        "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-exp",
        "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-exp",
    };
    const auto parsed =
        firebreak::experiments::parse_batch_method_list(
            "FPP-Branch-Benders-ProjectedCoverageLLBI-poly,"
            "FPP-Branch-Benders-ProjectedPathLLBI-poly,"
            "FPP-Branch-Benders-ProjectedCoverageLLBI-exp,"
            "FPP-Branch-Benders-ProjectedPathLLBI-exp,"
            "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-poly,"
            "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-poly,"
            "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-exp,"
            "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-exp,"
            "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-poly,"
            "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-poly,"
            "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-exp,"
            "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-exp");
    assert(parsed == labels);

    const auto coverage = firebreak::experiments::fpp_method_variant_settings(
        "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-poly");
    assert(coverage.is_fpp_branch_benders);
    assert(coverage.risk_config.type == firebreak::risk::RiskMeasureType::CVaR);
    assert(coverage.projected_llbi_options.use_projected_coverage_llbi_poly);
    assert(!coverage.projected_llbi_options.use_projected_path_llbi_poly);
    assert(firebreak::benders::active_projected_llbi_mode(coverage.projected_llbi_options) ==
           firebreak::benders::FppProjectedLlbiMode::Poly);

    const auto path = firebreak::experiments::fpp_method_variant_settings(
        "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-exp");
    assert(path.is_fpp_branch_benders);
    assert(path.risk_config.type == firebreak::risk::RiskMeasureType::MeanCVaR);
    assert(path.projected_llbi_options.use_projected_path_llbi_exp);
    assert(firebreak::benders::active_projected_llbi_mode(path.projected_llbi_options) ==
           firebreak::benders::FppProjectedLlbiMode::Exp);
}

}  // namespace

int main() {
    test_projected_coverage_exp_tree_separation();
    test_projected_coverage_poly_uses_original_variables_only();
    test_projected_path_exp_tree_separation();
    test_projected_path_exp_dag_shortest_path_separation();
    test_projected_path_poly_uses_original_variables_only();
    test_incompatible_projected_options_rejected();
    test_projected_label_parsing();
    std::cout << "All FPP projected LLBI tests passed.\n";
    return 0;
}
