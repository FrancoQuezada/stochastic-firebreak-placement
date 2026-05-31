#include <cassert>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "benders/FppProjectedLlbi.hpp"
#include "experiments/BatchExperimentConfig.hpp"

void test_parse_alphas() {
    const auto alphas = firebreak::experiments::parse_alpha_list("0.01,0.02, 0.03");
    assert(alphas.size() == 3);
    assert(alphas[0] == 0.01);
    assert(alphas[1] == 0.02);
    assert(alphas[2] == 0.03);
}

void test_parse_train_counts() {
    const auto counts = firebreak::experiments::parse_count_list("2,5,10", "--train-counts");
    assert((counts == std::vector<std::size_t>{2, 5, 10}));
}

void test_parse_methods() {
    const auto methods = firebreak::experiments::parse_batch_method_list(
        "FPP-SAA,FPP-SAA-CVaR,FPP-SAA-MeanCVaR,FPP-Benders,FPP-Benders-CVaR,FPP-Benders-MeanCVaR,"
        "FPP-Branch-Benders,FPP-Branch-Benders-CVaR,FPP-Branch-Benders-MeanCVaR,"
        "FPP-Branch-Benders-Combinatorial,FPP-Branch-Benders-Combinatorial-CVaR,"
        "FPP-Branch-Benders-Combinatorial-MeanCVaR,"
        "FPP-Branch-Benders-LLBI,FPP-Branch-Benders-RootCuts,FPP-Branch-Benders-LLBI-RootCuts,"
        "FPP-Branch-Benders-CVaR-LLBI,FPP-Branch-Benders-CVaR-RootCuts,FPP-Branch-Benders-CVaR-LLBI-RootCuts,"
        "FPP-Branch-Benders-MeanCVaR-LLBI,FPP-Branch-Benders-MeanCVaR-RootCuts,"
        "FPP-Branch-Benders-MeanCVaR-LLBI-RootCuts,"
        "FPP-Restricted-Branch-Benders,FPP-Restricted-Branch-Benders-LLBI,FPP-Restricted-Branch-Benders-RootCuts,"
        "FPP-Restricted-Branch-Benders-LLBI-RootCuts,FPP-Restricted-Branch-Benders-CVaR,"
        "FPP-Restricted-Branch-Benders-CVaR-LLBI,FPP-Restricted-Branch-Benders-CVaR-RootCuts,"
        "FPP-Restricted-Branch-Benders-CVaR-LLBI-RootCuts,FPP-Restricted-Branch-Benders-MeanCVaR,"
        "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI,FPP-Restricted-Branch-Benders-MeanCVaR-RootCuts,"
        "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI-RootCuts,"
        "FPP-Restricted-Branch-Benders-Combinatorial,"
        "FPP-Restricted-Branch-Benders-Combinatorial-CVaR,"
        "FPP-Restricted-Branch-Benders-Combinatorial-MeanCVaR,"
        "dpv_saa,DPV-Benders,Static-DPV,static_dpv_mip,Greedy-DPV3,Greedy-DPV2,Greedy-Betweenness,Greedy-Closeness,"
        "DPV-Branch-Benders,dpv_branch_benders_llbi,DPV-Branch-Benders-RootCuts,"
        "DPV-Branch-Benders-LLBI-RootCuts");
    assert((methods == std::vector<std::string>{
        "FPP-SAA",
        "FPP-SAA-CVaR",
        "FPP-SAA-MeanCVaR",
        "FPP-Benders",
        "FPP-Benders-CVaR",
        "FPP-Benders-MeanCVaR",
        "FPP-Branch-Benders",
        "FPP-Branch-Benders-CVaR",
        "FPP-Branch-Benders-MeanCVaR",
        "FPP-Branch-Benders-Combinatorial",
        "FPP-Branch-Benders-Combinatorial-CVaR",
        "FPP-Branch-Benders-Combinatorial-MeanCVaR",
        "FPP-Branch-Benders-LLBI",
        "FPP-Branch-Benders-RootCuts",
        "FPP-Branch-Benders-LLBI-RootCuts",
        "FPP-Branch-Benders-CVaR-LLBI",
        "FPP-Branch-Benders-CVaR-RootCuts",
        "FPP-Branch-Benders-CVaR-LLBI-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-LLBI",
        "FPP-Branch-Benders-MeanCVaR-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-LLBI-RootCuts",
        "FPP-Restricted-Branch-Benders",
        "FPP-Restricted-Branch-Benders-LLBI",
        "FPP-Restricted-Branch-Benders-RootCuts",
        "FPP-Restricted-Branch-Benders-LLBI-RootCuts",
        "FPP-Restricted-Branch-Benders-CVaR",
        "FPP-Restricted-Branch-Benders-CVaR-LLBI",
        "FPP-Restricted-Branch-Benders-CVaR-RootCuts",
        "FPP-Restricted-Branch-Benders-CVaR-LLBI-RootCuts",
        "FPP-Restricted-Branch-Benders-MeanCVaR",
        "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI",
        "FPP-Restricted-Branch-Benders-MeanCVaR-RootCuts",
        "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI-RootCuts",
        "FPP-Restricted-Branch-Benders-Combinatorial",
        "FPP-Restricted-Branch-Benders-Combinatorial-CVaR",
        "FPP-Restricted-Branch-Benders-Combinatorial-MeanCVaR",
        "DPV-SAA",
        "DPV-Benders",
        "Static-DPV",
        "Static-DPV-MIP",
        "Greedy-DPV3",
        "Greedy-DPV2",
        "Greedy-Betweenness",
        "Greedy-Closeness",
        "DPV-Branch-Benders",
        "DPV-Branch-Benders-LLBI",
        "DPV-Branch-Benders-RootCuts",
        "DPV-Branch-Benders-LLBI-RootCuts",
    }));
}

void test_official_method_labels_are_supported() {
    const std::vector<std::string> official = {
        "FPP-SAA",
        "FPP-SAA-CVaR",
        "FPP-SAA-MeanCVaR",
        "FPP-Benders",
        "FPP-Benders-CVaR",
        "FPP-Benders-MeanCVaR",
        "FPP-Branch-Benders",
        "FPP-Branch-Benders-CVaR",
        "FPP-Branch-Benders-MeanCVaR",
        "FPP-Branch-Benders-Combinatorial",
        "FPP-Branch-Benders-Combinatorial-CVaR",
        "FPP-Branch-Benders-Combinatorial-MeanCVaR",
        "FPP-Branch-Benders-LLBI",
        "FPP-Branch-Benders-RootCuts",
        "FPP-Branch-Benders-LLBI-RootCuts",
        "FPP-Branch-Benders-CVaR-LLBI",
        "FPP-Branch-Benders-CVaR-RootCuts",
        "FPP-Branch-Benders-CVaR-LLBI-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-LLBI",
        "FPP-Branch-Benders-MeanCVaR-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-LLBI-RootCuts",
        "FPP-Restricted-Branch-Benders",
        "FPP-Restricted-Branch-Benders-Combinatorial",
        "FPP-Restricted-Branch-Benders-Combinatorial-CVaR",
        "FPP-Restricted-Branch-Benders-Combinatorial-MeanCVaR",
        "FPP-Restricted-Branch-Benders-LLBI",
        "FPP-Restricted-Branch-Benders-RootCuts",
        "FPP-Restricted-Branch-Benders-LLBI-RootCuts",
        "FPP-Restricted-Branch-Benders-CVaR",
        "FPP-Restricted-Branch-Benders-CVaR-LLBI",
        "FPP-Restricted-Branch-Benders-CVaR-RootCuts",
        "FPP-Restricted-Branch-Benders-CVaR-LLBI-RootCuts",
        "FPP-Restricted-Branch-Benders-MeanCVaR",
        "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI",
        "FPP-Restricted-Branch-Benders-MeanCVaR-RootCuts",
        "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI-RootCuts",
        "DPV-SAA",
        "DPV-Benders",
        "DPV-Branch-Benders",
        "DPV-Branch-Benders-LLBI",
        "DPV-Branch-Benders-RootCuts",
        "DPV-Branch-Benders-LLBI-RootCuts",
        "Static-DPV",
        "Static-DPV-MIP",
        "Greedy-DPV3",
        "Greedy-DPV2",
        "Greedy-Betweenness",
        "Greedy-Closeness",
    };

    for (const auto& label : official) {
        assert(firebreak::experiments::normalize_batch_method_name(label) == label);
    }

    const auto parsed = firebreak::experiments::parse_batch_method_list(
        "FPP-SAA,FPP-SAA-CVaR,FPP-SAA-MeanCVaR,FPP-Benders,FPP-Benders-CVaR,FPP-Benders-MeanCVaR,"
        "FPP-Branch-Benders,"
        "FPP-Branch-Benders-CVaR,FPP-Branch-Benders-MeanCVaR,"
        "FPP-Branch-Benders-Combinatorial,FPP-Branch-Benders-Combinatorial-CVaR,"
        "FPP-Branch-Benders-Combinatorial-MeanCVaR,"
        "FPP-Branch-Benders-LLBI,FPP-Branch-Benders-RootCuts,FPP-Branch-Benders-LLBI-RootCuts,"
        "FPP-Branch-Benders-CVaR-LLBI,FPP-Branch-Benders-CVaR-RootCuts,FPP-Branch-Benders-CVaR-LLBI-RootCuts,"
        "FPP-Branch-Benders-MeanCVaR-LLBI,FPP-Branch-Benders-MeanCVaR-RootCuts,"
        "FPP-Branch-Benders-MeanCVaR-LLBI-RootCuts,"
        "FPP-Restricted-Branch-Benders,FPP-Restricted-Branch-Benders-Combinatorial,"
        "FPP-Restricted-Branch-Benders-Combinatorial-CVaR,"
        "FPP-Restricted-Branch-Benders-Combinatorial-MeanCVaR,"
        "FPP-Restricted-Branch-Benders-LLBI,FPP-Restricted-Branch-Benders-RootCuts,"
        "FPP-Restricted-Branch-Benders-LLBI-RootCuts,FPP-Restricted-Branch-Benders-CVaR,"
        "FPP-Restricted-Branch-Benders-CVaR-LLBI,FPP-Restricted-Branch-Benders-CVaR-RootCuts,"
        "FPP-Restricted-Branch-Benders-CVaR-LLBI-RootCuts,FPP-Restricted-Branch-Benders-MeanCVaR,"
        "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI,FPP-Restricted-Branch-Benders-MeanCVaR-RootCuts,"
        "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI-RootCuts,"
        "DPV-SAA,DPV-Benders,"
        "DPV-Branch-Benders,DPV-Branch-Benders-LLBI,DPV-Branch-Benders-RootCuts,"
        "DPV-Branch-Benders-LLBI-RootCuts,Static-DPV,Static-DPV-MIP,Greedy-DPV3,Greedy-DPV2,"
        "Greedy-Betweenness,Greedy-Closeness");
    assert(parsed == official);

    const auto& supported = firebreak::experiments::supported_batch_methods();
    for (const auto& label : official) {
        assert(std::find(supported.begin(), supported.end(), label) != supported.end());
    }
}

void test_recommended_candidate_labels_are_supported() {
    const auto fpp_candidates = firebreak::experiments::parse_batch_method_list(
        "FPP-SAA,FPP-SAA-CVaR,FPP-Branch-Benders,FPP-Branch-Benders-CVaR,"
        "FPP-Branch-Benders-CVaR-LLBI,FPP-Branch-Benders-CVaR-LLBI-RootCuts");
    assert((fpp_candidates == std::vector<std::string>{
        "FPP-SAA",
        "FPP-SAA-CVaR",
        "FPP-Branch-Benders",
        "FPP-Branch-Benders-CVaR",
        "FPP-Branch-Benders-CVaR-LLBI",
        "FPP-Branch-Benders-CVaR-LLBI-RootCuts",
    }));

    const auto dpv_candidates = firebreak::experiments::parse_batch_method_list(
        "DPV-SAA,DPV-Branch-Benders,DPV-Branch-Benders-LLBI,"
        "DPV-Branch-Benders-LLBI-RootCuts,Static-DPV,Static-DPV-MIP,Greedy-DPV3");
    assert((dpv_candidates == std::vector<std::string>{
        "DPV-SAA",
        "DPV-Branch-Benders",
        "DPV-Branch-Benders-LLBI",
        "DPV-Branch-Benders-LLBI-RootCuts",
        "Static-DPV",
        "Static-DPV-MIP",
        "Greedy-DPV3",
    }));
}

void test_fpp_strengthening_method_labels() {
    const auto labels = firebreak::experiments::parse_batch_method_list(
        "FPP-Branch-Benders-CVaR-CoverageLLBI,"
        "FPP-Branch-Benders-CVaR-PathLLBI,"
        "FPP-Branch-Benders-CVaR-CoverageLLBI-PathLLBI,"
        "FPP-Branch-Benders-MeanCVaR-CoverageLLBI,"
        "FPP-Branch-Benders-MeanCVaR-PathLLBI,"
        "FPP-Restricted-Branch-Benders-CVaR-CoverageLLBI,"
        "FPP-Restricted-Branch-Benders-CVaR-PathLLBI,"
        "FPP-Branch-Benders-CoverageLLBI,"
        "FPP-Restricted-Branch-Benders-PathLLBI,"
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp,"
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts,"
        "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-poly,"
        "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-poly");
    assert((labels == std::vector<std::string>{
        "FPP-Branch-Benders-CVaR-CoverageLLBI",
        "FPP-Branch-Benders-CVaR-PathLLBI",
        "FPP-Branch-Benders-CVaR-CoverageLLBI-PathLLBI",
        "FPP-Branch-Benders-MeanCVaR-CoverageLLBI",
        "FPP-Branch-Benders-MeanCVaR-PathLLBI",
        "FPP-Restricted-Branch-Benders-CVaR-CoverageLLBI",
        "FPP-Restricted-Branch-Benders-CVaR-PathLLBI",
        "FPP-Branch-Benders-CoverageLLBI",
        "FPP-Restricted-Branch-Benders-PathLLBI",
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp",
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts",
        "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-poly",
        "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-poly",
    }));

    const auto coverage = firebreak::experiments::fpp_method_variant_settings(
        "FPP-Branch-Benders-CVaR-CoverageLLBI");
    assert(coverage.is_fpp_branch_benders);
    assert(coverage.use_coverage_llbi);
    assert(!coverage.use_path_llbi);
    assert(coverage.risk_config.type == firebreak::risk::RiskMeasureType::CVaR);

    const auto both = firebreak::experiments::fpp_method_variant_settings(
        "FPP-Branch-Benders-CVaR-CoverageLLBI-PathLLBI");
    assert(both.use_coverage_llbi);
    assert(both.use_path_llbi);
    assert(both.risk_config.type == firebreak::risk::RiskMeasureType::CVaR);

    const auto mean_cvar = firebreak::experiments::fpp_method_variant_settings(
        "FPP-Branch-Benders-MeanCVaR-PathLLBI");
    assert(mean_cvar.use_path_llbi);
    assert(mean_cvar.risk_config.type == firebreak::risk::RiskMeasureType::MeanCVaR);

    const auto expected = firebreak::experiments::fpp_method_variant_settings(
        "FPP-Restricted-Branch-Benders-CoverageLLBI");
    assert(expected.is_fpp_restricted_branch_benders);
    assert(expected.use_coverage_llbi);
    assert(expected.risk_config.type == firebreak::risk::RiskMeasureType::Expected);

    const auto projected_coverage = firebreak::experiments::fpp_method_variant_settings(
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp");
    assert(projected_coverage.is_fpp_branch_benders);
    assert(projected_coverage.projected_llbi_options.use_projected_coverage_llbi_exp);
    assert(firebreak::benders::active_projected_llbi_mode(
               projected_coverage.projected_llbi_options) ==
           firebreak::benders::FppProjectedLlbiMode::Exp);
    assert(!projected_coverage.use_coverage_llbi);
    assert(!projected_coverage.use_path_llbi);

    const auto projected_coverage_root = firebreak::experiments::fpp_method_variant_settings(
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts");
    assert(projected_coverage_root.is_fpp_branch_benders);
    assert(projected_coverage_root.use_root_user_cuts);
    assert(projected_coverage_root.projected_llbi_options.use_projected_coverage_llbi_exp);
    assert(firebreak::experiments::normalize_batch_method_name(
               "FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts") ==
           "FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts");

    const auto projected_path_cvar = firebreak::experiments::fpp_method_variant_settings(
        "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-poly");
    assert(projected_path_cvar.is_fpp_branch_benders);
    assert(projected_path_cvar.projected_llbi_options.use_projected_path_llbi_poly);
    assert(firebreak::benders::active_projected_llbi_mode(
               projected_path_cvar.projected_llbi_options) ==
           firebreak::benders::FppProjectedLlbiMode::Poly);
    assert(projected_path_cvar.risk_config.type == firebreak::risk::RiskMeasureType::CVaR);

    const auto projected_coverage_mean = firebreak::experiments::fpp_method_variant_settings(
        "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-poly");
    assert(projected_coverage_mean.is_fpp_branch_benders);
    assert(projected_coverage_mean.projected_llbi_options.use_projected_coverage_llbi_poly);
    assert(projected_coverage_mean.risk_config.type ==
           firebreak::risk::RiskMeasureType::MeanCVaR);
}

void test_projected_llbi_scaling_grid_labels() {
    const std::vector<std::string> labels = {
        "FPP-SAA",
        "FPP-SAA-CVaR",
        "FPP-SAA-MeanCVaR",
        "FPP-Branch-Benders-RootCuts",
        "FPP-Branch-Benders-CVaR-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-RootCuts",
        "FPP-Branch-Benders-LLBI-RootCuts",
        "FPP-Branch-Benders-CVaR-LLBI-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-LLBI-RootCuts",
        "FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts",
        "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-poly-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-poly-RootCuts",
        "FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts",
        "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-poly-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-poly-RootCuts",
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts",
        "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-exp-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-exp-RootCuts",
        "FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts",
        "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-exp-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-exp-RootCuts",
        "FPP-Branch-Benders-Combinatorial",
        "FPP-Branch-Benders-Combinatorial-CVaR",
        "FPP-Branch-Benders-Combinatorial-MeanCVaR",
    };

    std::string csv;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (i > 0) {
            csv += ",";
        }
        csv += labels[i];
    }
    assert(firebreak::experiments::parse_batch_method_list(csv) == labels);
    assert(firebreak::experiments::normalize_fpp_mode("fpp_base") == "fpp_base");

    for (const auto& label : labels) {
        const auto settings = firebreak::experiments::fpp_method_variant_settings(label);
        assert(settings.is_fpp_solver);
        if (label.find("CVaR") != std::string::npos &&
            label.find("MeanCVaR") == std::string::npos) {
            assert(settings.risk_config.type == firebreak::risk::RiskMeasureType::CVaR);
            assert(settings.risk_config.cvarLambda == 1.0);
        } else if (label.find("MeanCVaR") != std::string::npos) {
            assert(settings.risk_config.type == firebreak::risk::RiskMeasureType::MeanCVaR);
            assert(settings.risk_config.cvarLambda == 0.5);
        } else {
            assert(settings.risk_config.type == firebreak::risk::RiskMeasureType::Expected);
        }
        if (label.find("RootCuts") != std::string::npos) {
            assert(settings.use_root_user_cuts);
        }
        if (label.find("LLBI-RootCuts") != std::string::npos) {
            assert(settings.use_lifted_lower_bounds);
            assert(settings.use_root_user_cuts);
        }
        if (label.find("ProjectedCoverageLLBI-poly") != std::string::npos) {
            assert(settings.projected_llbi_options.use_projected_coverage_llbi_poly);
            assert(!settings.projected_llbi_options.use_projected_path_llbi_poly);
        }
        if (label.find("ProjectedPathLLBI-poly") != std::string::npos) {
            assert(settings.projected_llbi_options.use_projected_path_llbi_poly);
            assert(!settings.projected_llbi_options.use_projected_coverage_llbi_poly);
        }
        if (label.find("ProjectedCoverageLLBI-exp") != std::string::npos) {
            assert(settings.projected_llbi_options.use_projected_coverage_llbi_exp);
            assert(!settings.projected_llbi_options.use_projected_path_llbi_exp);
        }
        if (label.find("ProjectedPathLLBI-exp") != std::string::npos) {
            assert(settings.projected_llbi_options.use_projected_path_llbi_exp);
            assert(!settings.projected_llbi_options.use_projected_coverage_llbi_exp);
        }
        if (label.find("Combinatorial") != std::string::npos) {
            assert(settings.use_combinatorial_benders);
            assert(settings.combinatorial_options.enabled);
            assert(settings.combinatorial_options.separate_fractional);
            assert(settings.combinatorial_options.initial_cuts);
            assert(settings.combinatorial_options.cut_sampling_ratio == 0.10);
        }
    }
}

void test_parse_fpp_formulation() {
    assert(firebreak::experiments::normalize_fpp_formulation("base") == "base");
    assert(firebreak::experiments::normalize_fpp_formulation("cut") == "cut");
    assert(firebreak::experiments::normalize_fpp_formulation("cut_reachability") == "cut");

    bool threw = false;
    try {
        (void)firebreak::experiments::normalize_fpp_formulation("flow");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_fpp_mode_expansion() {
    const std::vector<std::string> modes = {
        "fpp_base",
        "fpp_base_greedy",
        "fpp_base_dominator",
        "fpp_base_separator",
        "fpp_base_dominator_separator",
        "fpp_base_dominator_separator_greedy",
        "fpp_cut",
        "fpp_cut_greedy",
        "fpp_cut_dominator",
        "fpp_cut_separator",
        "fpp_cut_dominator_separator",
        "fpp_cut_dominator_separator_greedy",
    };
    for (const auto& mode : modes) {
        const auto settings = firebreak::experiments::fpp_mode_settings(mode);
        assert(settings.mode == mode);
        assert(!settings.enable_local_search);
        assert(settings.formulation == (mode.find("fpp_cut") == 0 ? "cut" : "base"));
        assert(settings.enable_greedy_warm_start == (mode.find("greedy") != std::string::npos));
        assert(settings.enable_dominator_cuts == (mode.find("dominator") != std::string::npos));
        assert(settings.enable_separator_cuts == (mode.find("separator") != std::string::npos));
    }

    const auto parsed = firebreak::experiments::parse_fpp_mode_list("base,cut_dominator_separator_greedy");
    assert((parsed == std::vector<std::string>{"fpp_base", "fpp_cut_dominator_separator_greedy"}));
}

void test_fpp_variant_settings() {
    const auto saa_cvar = firebreak::experiments::fpp_method_variant_settings("FPP-SAA-CVaR");
    assert(saa_cvar.is_fpp_solver);
    assert(saa_cvar.is_fpp_saa);
    assert(saa_cvar.risk_config.type == firebreak::risk::RiskMeasureType::CVaR);
    assert(saa_cvar.risk_config.cvarLambda == 1.0);

    const auto saa_mean = firebreak::experiments::fpp_method_variant_settings("FPP-SAA-MeanCVaR");
    assert(saa_mean.is_fpp_saa);
    assert(saa_mean.risk_config.type == firebreak::risk::RiskMeasureType::MeanCVaR);
    assert(saa_mean.risk_config.cvarLambda == 0.5);

    const auto benders_cvar =
        firebreak::experiments::fpp_method_variant_settings("FPP-Benders-CVaR");
    assert(benders_cvar.is_fpp_benders);
    assert(benders_cvar.risk_config.type == firebreak::risk::RiskMeasureType::CVaR);

    const auto callback_both =
        firebreak::experiments::fpp_method_variant_settings(
            "FPP-Branch-Benders-CVaR-LLBI-RootCuts");
    assert(callback_both.is_fpp_branch_benders);
    assert(callback_both.risk_config.type == firebreak::risk::RiskMeasureType::CVaR);
    assert(callback_both.use_lifted_lower_bounds);
    assert(callback_both.use_root_user_cuts);

    const auto restricted =
        firebreak::experiments::fpp_method_variant_settings("fpp_restricted_branch_benders");
    assert(restricted.is_fpp_solver);
    assert(restricted.is_fpp_restricted_branch_benders);
    assert(!restricted.is_fpp_branch_benders);
    assert(restricted.risk_config.type == firebreak::risk::RiskMeasureType::Expected);

    const auto restricted_cvar =
        firebreak::experiments::fpp_method_variant_settings("FPP-Restricted-Branch-Benders-CVaR");
    assert(restricted_cvar.is_fpp_solver);
    assert(restricted_cvar.is_fpp_restricted_branch_benders);
    assert(restricted_cvar.risk_config.type == firebreak::risk::RiskMeasureType::CVaR);
    assert(restricted_cvar.risk_config.cvarLambda == 1.0);

    const auto restricted_cvar_both =
        firebreak::experiments::fpp_method_variant_settings(
            "FPP-Restricted-Branch-Benders-CVaR-LLBI-RootCuts");
    assert(restricted_cvar_both.is_fpp_restricted_branch_benders);
    assert(restricted_cvar_both.risk_config.type == firebreak::risk::RiskMeasureType::CVaR);
    assert(restricted_cvar_both.use_lifted_lower_bounds);
    assert(restricted_cvar_both.use_root_user_cuts);

    const auto restricted_mean =
        firebreak::experiments::fpp_method_variant_settings("FPP-Restricted-Branch-Benders-MeanCVaR");
    assert(restricted_mean.is_fpp_restricted_branch_benders);
    assert(restricted_mean.risk_config.type == firebreak::risk::RiskMeasureType::MeanCVaR);
    assert(restricted_mean.risk_config.cvarLambda == 0.5);

    const auto restricted_mean_root =
        firebreak::experiments::fpp_method_variant_settings(
            "FPP-Restricted-Branch-Benders-MeanCVaR-RootCuts");
    assert(restricted_mean_root.is_fpp_restricted_branch_benders);
    assert(restricted_mean_root.risk_config.type == firebreak::risk::RiskMeasureType::MeanCVaR);
    assert(!restricted_mean_root.use_lifted_lower_bounds);
    assert(restricted_mean_root.use_root_user_cuts);

    const auto combinatorial =
        firebreak::experiments::fpp_method_variant_settings(
            "FPP-Branch-Benders-Combinatorial-CVaR");
    assert(combinatorial.is_fpp_branch_benders);
    assert(combinatorial.use_combinatorial_benders);
    assert(combinatorial.combinatorial_options.enabled);
    assert(combinatorial.combinatorial_options.separate_fractional);
    assert(combinatorial.combinatorial_options.initial_cuts);
    assert(combinatorial.risk_config.type == firebreak::risk::RiskMeasureType::CVaR);

    const auto restricted_combinatorial =
        firebreak::experiments::fpp_method_variant_settings(
            "FPP-Restricted-Branch-Benders-Combinatorial-MeanCVaR");
    assert(restricted_combinatorial.is_fpp_restricted_branch_benders);
    assert(restricted_combinatorial.use_combinatorial_benders);
    assert(restricted_combinatorial.combinatorial_options.enabled);
    assert(restricted_combinatorial.risk_config.type ==
           firebreak::risk::RiskMeasureType::MeanCVaR);
}

void test_dpv_branch_benders_variant_settings() {
    assert(!firebreak::experiments::is_dpv_branch_benders_method("DPV-SAA"));

    const auto baseline =
        firebreak::experiments::dpv_branch_benders_variant_settings("DPV-Branch-Benders");
    assert(baseline.is_branch_benders);
    assert(!baseline.use_lifted_lower_bounds);
    assert(!baseline.use_root_user_cuts);
    assert(baseline.root_user_cut_max_rounds == 1);

    const auto llbi =
        firebreak::experiments::dpv_branch_benders_variant_settings("dpv_branch_benders_llbi");
    assert(llbi.is_branch_benders);
    assert(llbi.use_lifted_lower_bounds);
    assert(!llbi.use_root_user_cuts);

    const auto root =
        firebreak::experiments::dpv_branch_benders_variant_settings("DPV-Branch-Benders-RootCuts");
    assert(root.is_branch_benders);
    assert(!root.use_lifted_lower_bounds);
    assert(root.use_root_user_cuts);
    assert(root.root_user_cut_max_rounds == 1);

    const auto both = firebreak::experiments::dpv_branch_benders_variant_settings(
        "DPV-Branch-Benders-LLBI-RootCuts");
    assert(both.is_branch_benders);
    assert(both.use_lifted_lower_bounds);
    assert(both.use_root_user_cuts);
    assert(both.root_user_cut_max_rounds == 1);
}

void test_dpv_cvar_rejection() {
    bool threw = false;
    try {
        (void)firebreak::experiments::normalize_batch_method_name("DPV-SAA-CVaR");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_invalid_method() {
    bool threw = false;
    try {
        (void)firebreak::experiments::parse_batch_method_list("FPP-SAA,Unknown");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_validate_config() {
    firebreak::experiments::BatchExperimentConfig config;
    config.landscape = "Sub20";
    config.alpha_values = {0.01};
    config.train_counts = {2};
    config.test_count = 3;
    config.num_cases = 1;
    config.seed_base = 123;
    config.methods = {"FPP-SAA", "Static-DPV"};
    config.output_dir = "results/batch/test";
    config.output_csv = "results/batch/test/batch_results.csv";
    config.warm_start_policy = "none";
    firebreak::experiments::validate_batch_experiment_config(config);
    assert(config.fpp_formulation == "base");
    assert(!config.enable_dominator_cuts);
    assert(!config.enable_separator_cuts);
    assert(!config.enable_greedy_warm_start);
    assert(!config.enable_local_search);
    assert(config.root_user_cut_max_rounds == 1);
    assert(std::isnan(config.root_user_cut_tolerance));

    config.methods = {"DPV-Branch-Benders-RootCuts"};
    config.root_user_cut_max_rounds = 0;
    bool threw = false;
    try {
        firebreak::experiments::validate_batch_experiment_config(config);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_shared_splits_require_split_dir() {
    firebreak::experiments::BatchExperimentConfig config;
    config.landscape = "Sub20";
    config.alpha_values = {0.01};
    config.train_counts = {2};
    config.test_count = 3;
    config.num_cases = 1;
    config.seed_base = 123;
    config.methods = {"Static-DPV"};
    config.output_dir = "results/batch/test";
    config.output_csv = "results/batch/test/batch_results.csv";
    config.shared_splits = true;

    bool threw = false;
    try {
        firebreak::experiments::validate_batch_experiment_config(config);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);

    config.split_dir = "results/batch/shared/splits";
    firebreak::experiments::validate_batch_experiment_config(config);
}

int main() {
    test_parse_alphas();
    test_parse_train_counts();
    test_parse_methods();
    test_official_method_labels_are_supported();
    test_recommended_candidate_labels_are_supported();
    test_fpp_strengthening_method_labels();
    test_projected_llbi_scaling_grid_labels();
    test_parse_fpp_formulation();
    test_fpp_mode_expansion();
    test_fpp_variant_settings();
    test_dpv_branch_benders_variant_settings();
    test_dpv_cvar_rejection();
    test_invalid_method();
    test_validate_config();
    test_shared_splits_require_split_dir();
    std::cout << "All batch experiment config tests passed.\n";
    return 0;
}
