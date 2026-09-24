#include "experiments/BatchExperimentConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace firebreak::experiments {

namespace {

std::string trim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

std::string normalized_key(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        if (ch == '-' || ch == '_' || std::isspace(static_cast<unsigned char>(ch))) {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

struct ParsedFppStrengthenedMethod {
    bool recognized = false;
    std::string canonical;
    bool is_saa = false;
    bool is_branch_benders = false;
    bool is_restricted_branch_benders = false;
    bool use_lifted_lower_bounds = false;
    bool use_root_user_cuts = false;
    bool use_coverage_llbi = false;
    bool use_path_llbi = false;
    bool use_projected_coverage_llbi_exp = false;
    bool use_projected_path_llbi_exp = false;
    bool use_projected_coverage_llbi_poly = false;
    bool use_projected_path_llbi_poly = false;
    bool use_global_dominance_preprocessing = false;
    bool use_conditional_zero_benefit_fixing = false;
    bool use_combinatorial_benders = false;
    bool has_combinatorial_scenario_order = false;
    benders::FppCombinatorialBendersScenarioOrder combinatorial_scenario_order =
        benders::FppCombinatorialBendersScenarioOrder::EtaAscending;
    risk::RiskMeasureConfig risk_config;
};

bool erase_token(std::string& key, const std::string& token) {
    const std::size_t pos = key.find(token);
    if (pos == std::string::npos) {
        return false;
    }
    key.erase(pos, token.size());
    return true;
}

ParsedFppStrengthenedMethod parse_fpp_strengthened_method_key(std::string key) {
    ParsedFppStrengthenedMethod parsed;
    parsed.use_projected_coverage_llbi_exp =
        erase_token(key, "projectedcoveragellbiexp");
    parsed.use_projected_path_llbi_exp =
        erase_token(key, "projectedpathllbiexp");
    parsed.use_projected_coverage_llbi_poly =
        erase_token(key, "projectedcoveragellbipoly");
    parsed.use_projected_path_llbi_poly =
        erase_token(key, "projectedpathllbipoly");
    parsed.use_coverage_llbi = erase_token(key, "coveragellbi");
    parsed.use_path_llbi = erase_token(key, "pathllbi");
    parsed.use_global_dominance_preprocessing =
        erase_token(key, "dominancepreprocess") ||
        erase_token(key, "dominancepreprocessing") ||
        erase_token(key, "globaldominancepreprocess") ||
        erase_token(key, "globaldominancepreprocessing");
    parsed.use_conditional_zero_benefit_fixing =
        erase_token(key, "conditionalzerofixing") ||
        erase_token(key, "conditionalzerobenefitfixing") ||
        erase_token(key, "conditionalzerobenefit");
    const bool eta_desc =
        erase_token(key, "etadesc") ||
        erase_token(key, "descendingeta");
    const bool eta_asc =
        erase_token(key, "etaasc") ||
        erase_token(key, "ascendingeta");
    if (eta_desc && eta_asc) {
        throw std::runtime_error(
            "FPP combinatorial method label cannot request both EtaAsc and EtaDesc.");
    }
    if (eta_desc || eta_asc) {
        parsed.has_combinatorial_scenario_order = true;
        parsed.combinatorial_scenario_order = eta_desc
            ? benders::FppCombinatorialBendersScenarioOrder::EtaDescending
            : benders::FppCombinatorialBendersScenarioOrder::EtaAscending;
    }
    parsed.use_combinatorial_benders =
        erase_token(key, "combinatorial") ||
        erase_token(key, "combinatorialbenders") ||
        erase_token(key, "iplussfh") ||
        erase_token(key, "iplussf") ||
        erase_token(key, "ipluss") ||
        erase_token(key, "iplus");
    parsed.use_root_user_cuts = erase_token(key, "rootcuts");
    parsed.use_lifted_lower_bounds = erase_token(key, "llbi");

    if (erase_token(key, "meancvar")) {
        parsed.risk_config.type = risk::RiskMeasureType::MeanCVaR;
        parsed.risk_config.cvarLambda = 0.5;
    } else if (erase_token(key, "cvar")) {
        parsed.risk_config.type = risk::RiskMeasureType::CVaR;
        parsed.risk_config.cvarLambda = 1.0;
    }

    if (key == "fppsaa") {
        parsed.recognized = true;
        parsed.is_saa = true;
        parsed.canonical = "FPP-SAA";
    } else if (key == "fppbranchbenders" ||
               key == "fppcallbackbenders" ||
               key == "fppsaabranchbenders") {
        parsed.recognized = true;
        parsed.is_branch_benders = true;
        parsed.canonical = "FPP-Branch-Benders";
    } else if (key == "fpprestrictedbranchbenders" ||
               key == "fpprestrictedcandidatebranchbenders" ||
               key == "fppbranchbendersrestricted") {
        parsed.recognized = true;
        parsed.is_restricted_branch_benders = true;
        parsed.canonical = "FPP-Restricted-Branch-Benders";
    }
    if (!parsed.recognized) {
        return parsed;
    }
    if (parsed.has_combinatorial_scenario_order &&
        !parsed.use_combinatorial_benders) {
        parsed.recognized = false;
        return parsed;
    }

    if (parsed.use_combinatorial_benders) {
        parsed.canonical += "-Combinatorial";
    }
    if (parsed.risk_config.type == risk::RiskMeasureType::CVaR) {
        parsed.canonical += "-CVaR";
    } else if (parsed.risk_config.type == risk::RiskMeasureType::MeanCVaR) {
        parsed.canonical += "-MeanCVaR";
    }
    if (parsed.use_combinatorial_benders &&
        parsed.combinatorial_scenario_order ==
            benders::FppCombinatorialBendersScenarioOrder::EtaDescending) {
        parsed.canonical += "-EtaDesc";
    }
    if (parsed.use_lifted_lower_bounds) {
        parsed.canonical += "-LLBI";
    }
    const bool has_projected_llbi =
        parsed.use_projected_coverage_llbi_exp ||
        parsed.use_projected_path_llbi_exp ||
        parsed.use_projected_coverage_llbi_poly ||
        parsed.use_projected_path_llbi_poly;
    if (parsed.use_root_user_cuts && !has_projected_llbi) {
        parsed.canonical += "-RootCuts";
    }
    if (parsed.use_global_dominance_preprocessing) {
        parsed.canonical += "-DominancePreprocess";
    }
    if (parsed.use_coverage_llbi) {
        parsed.canonical += "-CoverageLLBI";
    }
    if (parsed.use_path_llbi) {
        parsed.canonical += "-PathLLBI";
    }
    if (parsed.use_projected_coverage_llbi_exp) {
        parsed.canonical += "-ProjectedCoverageLLBI-exp";
    }
    if (parsed.use_projected_path_llbi_exp) {
        parsed.canonical += "-ProjectedPathLLBI-exp";
    }
    if (parsed.use_projected_coverage_llbi_poly) {
        parsed.canonical += "-ProjectedCoverageLLBI-poly";
    }
    if (parsed.use_projected_path_llbi_poly) {
        parsed.canonical += "-ProjectedPathLLBI-poly";
    }
    if (parsed.use_root_user_cuts && has_projected_llbi) {
        parsed.canonical += "-RootCuts";
    }
    if (parsed.use_conditional_zero_benefit_fixing) {
        parsed.canonical += "-ConditionalZeroFixing";
    }
    return parsed;
}

std::vector<std::string> split_csv(const std::string& value) {
    std::vector<std::string> tokens;
    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        const std::string cleaned = trim(token);
        if (!cleaned.empty()) {
            tokens.push_back(cleaned);
        }
    }
    return tokens;
}

}  // namespace

const std::vector<std::string>& supported_batch_methods() {
    static const std::vector<std::string> methods = {
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
        "FPP-Branch-Benders-Combinatorial-EtaDesc",
        "FPP-Branch-Benders-Combinatorial-CVaR",
        "FPP-Branch-Benders-Combinatorial-CVaR-EtaDesc",
        "FPP-Branch-Benders-Combinatorial-MeanCVaR",
        "FPP-Branch-Benders-Combinatorial-MeanCVaR-EtaDesc",
        "FPP-Branch-Benders-LLBI",
        "FPP-Branch-Benders-RootCuts",
        "FPP-Branch-Benders-LLBI-RootCuts",
        "FPP-Branch-Benders-CVaR-LLBI",
        "FPP-Branch-Benders-CVaR-RootCuts",
        "FPP-Branch-Benders-CVaR-LLBI-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-LLBI",
        "FPP-Branch-Benders-MeanCVaR-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-LLBI-RootCuts",
        "FPP-Branch-Benders-CVaR-CoverageLLBI",
        "FPP-Branch-Benders-CVaR-PathLLBI",
        "FPP-Branch-Benders-CVaR-CoverageLLBI-PathLLBI",
        "FPP-Branch-Benders-MeanCVaR-CoverageLLBI",
        "FPP-Branch-Benders-MeanCVaR-PathLLBI",
        "FPP-Branch-Benders-ProjectedCoverageLLBI-poly",
        "FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts",
        "FPP-Branch-Benders-ProjectedPathLLBI-poly",
        "FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts",
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp",
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts",
        "FPP-Branch-Benders-ProjectedPathLLBI-exp",
        "FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts",
        "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-poly",
        "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-poly-RootCuts",
        "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-poly",
        "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-poly-RootCuts",
        "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-exp",
        "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-exp-RootCuts",
        "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-exp",
        "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-exp-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-poly",
        "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-poly-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-poly",
        "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-poly-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-exp",
        "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-exp-RootCuts",
        "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-exp",
        "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-exp-RootCuts",
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
        "FPP-Restricted-Branch-Benders-CVaR-CoverageLLBI",
        "FPP-Restricted-Branch-Benders-CVaR-PathLLBI",
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
    };
    return methods;
}

const std::vector<std::string>& supported_warm_start_policies() {
    static const std::vector<std::string> policies = {
        "none",
        "greedy-dpv3-for-fpp",
        "greedy-dpv3-for-dpv",
        "static-dpv-for-fpp",
        "static-dpv-for-dpv",
    };
    return policies;
}

const std::vector<std::string>& supported_fpp_formulations() {
    static const std::vector<std::string> formulations = {
        "base",
        "cut",
    };
    return formulations;
}

const std::vector<std::string>& supported_fpp_modes() {
    static const std::vector<std::string> modes = {
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
    return modes;
}

std::vector<double> parse_alpha_list(const std::string& value) {
    const auto tokens = split_csv(value);
    if (tokens.empty()) {
        throw std::runtime_error("--alphas must contain at least one value.");
    }

    std::vector<double> alphas;
    alphas.reserve(tokens.size());
    for (const auto& token : tokens) {
        try {
            std::size_t consumed = 0;
            const double alpha = std::stod(token, &consumed);
            if (consumed != token.size() || alpha < 0.0) {
                throw std::runtime_error("Invalid alpha value: " + token);
            }
            alphas.push_back(alpha);
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("Invalid alpha value: " + token);
        } catch (const std::out_of_range&) {
            throw std::runtime_error("Alpha value is out of range: " + token);
        }
    }
    return alphas;
}

std::vector<std::size_t> parse_count_list(const std::string& value, const std::string& label) {
    const auto tokens = split_csv(value);
    if (tokens.empty()) {
        throw std::runtime_error(label + " must contain at least one value.");
    }

    std::vector<std::size_t> counts;
    counts.reserve(tokens.size());
    for (const auto& token : tokens) {
        try {
            std::size_t consumed = 0;
            const int parsed = std::stoi(token, &consumed);
            if (consumed != token.size() || parsed <= 0) {
                throw std::runtime_error("Invalid positive integer in " + label + ": " + token);
            }
            counts.push_back(static_cast<std::size_t>(parsed));
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("Invalid positive integer in " + label + ": " + token);
        } catch (const std::out_of_range&) {
            throw std::runtime_error("Integer value is out of range in " + label + ": " + token);
        }
    }
    return counts;
}

std::string normalize_batch_method_name(const std::string& value) {
    const std::string key = normalized_key(value);
    const auto parsed_fpp = parse_fpp_strengthened_method_key(key);
    if (parsed_fpp.recognized) {
        return parsed_fpp.canonical;
    }
    if (key == "fppsaa") {
        return "FPP-SAA";
    }
    if (key == "fppsaacvar") {
        return "FPP-SAA-CVaR";
    }
    if (key == "fppsaameancvar") {
        return "FPP-SAA-MeanCVaR";
    }
    if (key == "fppbenders" || key == "fppsaabenders") {
        return "FPP-Benders";
    }
    if (key == "fppbenderscvar" || key == "fppsaabenderscvar") {
        return "FPP-Benders-CVaR";
    }
    if (key == "fppbendersmeancvar" || key == "fppsaabendersmeancvar") {
        return "FPP-Benders-MeanCVaR";
    }
    if (key == "fppbranchbenders" ||
        key == "fppcallbackbenders" ||
        key == "fppsaabranchbenders") {
        return "FPP-Branch-Benders";
    }
    if (key == "fppbranchbenderscvar" ||
        key == "fppcallbackbenderscvar" ||
        key == "fppsaabranchbenderscvar") {
        return "FPP-Branch-Benders-CVaR";
    }
    if (key == "fppbranchbendersmeancvar" ||
        key == "fppcallbackbendersmeancvar" ||
        key == "fppsaabranchbendersmeancvar") {
        return "FPP-Branch-Benders-MeanCVaR";
    }
    if (key == "fppbranchbendersllbi" ||
        key == "fppcallbackbendersllbi") {
        return "FPP-Branch-Benders-LLBI";
    }
    if (key == "fppbranchbendersrootcuts" ||
        key == "fppcallbackbendersrootcuts") {
        return "FPP-Branch-Benders-RootCuts";
    }
    if (key == "fppbranchbendersllbirootcuts" ||
        key == "fppbranchbendersrootcutsllbi" ||
        key == "fppcallbackbendersllbirootcuts" ||
        key == "fppcallbackbendersrootcutsllbi") {
        return "FPP-Branch-Benders-LLBI-RootCuts";
    }
    if (key == "fppbranchbenderscvarllbi" ||
        key == "fppcallbackbenderscvarllbi") {
        return "FPP-Branch-Benders-CVaR-LLBI";
    }
    if (key == "fppbranchbenderscvarrootcuts" ||
        key == "fppcallbackbenderscvarrootcuts") {
        return "FPP-Branch-Benders-CVaR-RootCuts";
    }
    if (key == "fppbranchbenderscvarllbirootcuts" ||
        key == "fppbranchbenderscvarrootcutsllbi" ||
        key == "fppcallbackbenderscvarllbirootcuts" ||
        key == "fppcallbackbenderscvarrootcutsllbi") {
        return "FPP-Branch-Benders-CVaR-LLBI-RootCuts";
    }
    if (key == "fpprestrictedbranchbenders" ||
        key == "fpprestrictedcandidatebranchbenders" ||
        key == "fppbranchbendersrestricted") {
        return "FPP-Restricted-Branch-Benders";
    }
    if (key == "fpprestrictedbranchbendersllbi" ||
        key == "fpprestrictedcandidatebranchbendersllbi" ||
        key == "fppbranchbendersrestrictedllbi") {
        return "FPP-Restricted-Branch-Benders-LLBI";
    }
    if (key == "fpprestrictedbranchbendersrootcuts" ||
        key == "fpprestrictedcandidatebranchbendersrootcuts" ||
        key == "fppbranchbendersrestrictedrootcuts") {
        return "FPP-Restricted-Branch-Benders-RootCuts";
    }
    if (key == "fpprestrictedbranchbendersllbirootcuts" ||
        key == "fpprestrictedbranchbendersrootcutsllbi" ||
        key == "fpprestrictedcandidatebranchbendersllbirootcuts" ||
        key == "fpprestrictedcandidatebranchbendersrootcutsllbi" ||
        key == "fppbranchbendersrestrictedllbirootcuts" ||
        key == "fppbranchbendersrestrictedrootcutsllbi") {
        return "FPP-Restricted-Branch-Benders-LLBI-RootCuts";
    }
    if (key == "fpprestrictedbranchbenderscvar" ||
        key == "fpprestrictedcandidatebranchbenderscvar" ||
        key == "fppbranchbendersrestrictedcvar") {
        return "FPP-Restricted-Branch-Benders-CVaR";
    }
    if (key == "fpprestrictedbranchbenderscvarllbi" ||
        key == "fpprestrictedcandidatebranchbenderscvarllbi" ||
        key == "fppbranchbendersrestrictedcvarllbi") {
        return "FPP-Restricted-Branch-Benders-CVaR-LLBI";
    }
    if (key == "fpprestrictedbranchbenderscvarrootcuts" ||
        key == "fpprestrictedcandidatebranchbenderscvarrootcuts" ||
        key == "fppbranchbendersrestrictedcvarrootcuts") {
        return "FPP-Restricted-Branch-Benders-CVaR-RootCuts";
    }
    if (key == "fpprestrictedbranchbenderscvarllbirootcuts" ||
        key == "fpprestrictedbranchbenderscvarrootcutsllbi" ||
        key == "fpprestrictedcandidatebranchbenderscvarllbirootcuts" ||
        key == "fpprestrictedcandidatebranchbenderscvarrootcutsllbi" ||
        key == "fppbranchbendersrestrictedcvarllbirootcuts" ||
        key == "fppbranchbendersrestrictedcvarrootcutsllbi") {
        return "FPP-Restricted-Branch-Benders-CVaR-LLBI-RootCuts";
    }
    if (key == "fpprestrictedbranchbendersmeancvar" ||
        key == "fpprestrictedcandidatebranchbendersmeancvar" ||
        key == "fppbranchbendersrestrictedmeancvar") {
        return "FPP-Restricted-Branch-Benders-MeanCVaR";
    }
    if (key == "fpprestrictedbranchbendersmeancvarllbi" ||
        key == "fpprestrictedcandidatebranchbendersmeancvarllbi" ||
        key == "fppbranchbendersrestrictedmeancvarllbi") {
        return "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI";
    }
    if (key == "fpprestrictedbranchbendersmeancvarrootcuts" ||
        key == "fpprestrictedcandidatebranchbendersmeancvarrootcuts" ||
        key == "fppbranchbendersrestrictedmeancvarrootcuts") {
        return "FPP-Restricted-Branch-Benders-MeanCVaR-RootCuts";
    }
    if (key == "fpprestrictedbranchbendersmeancvarllbirootcuts" ||
        key == "fpprestrictedbranchbendersmeancvarrootcutsllbi" ||
        key == "fpprestrictedcandidatebranchbendersmeancvarllbirootcuts" ||
        key == "fpprestrictedcandidatebranchbendersmeancvarrootcutsllbi" ||
        key == "fppbranchbendersrestrictedmeancvarllbirootcuts" ||
        key == "fppbranchbendersrestrictedmeancvarrootcutsllbi") {
        return "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI-RootCuts";
    }
    if (key.find("dpv") == 0 && key.find("cvar") != std::string::npos) {
        throw std::runtime_error("DPV-CVaR optimization is out of scope and not implemented.");
    }
    if (key == "dpvsaa") {
        return "DPV-SAA";
    }
    if (key == "dpvbenders" || key == "dpvsaabenders") {
        return "DPV-Benders";
    }
    if (key == "staticdpv") {
        return "Static-DPV";
    }
    if (key == "staticdpvmip") {
        return "Static-DPV-MIP";
    }
    if (key == "greedydpv3") {
        return "Greedy-DPV3";
    }
    if (key == "greedydpv2") {
        return "Greedy-DPV2";
    }
    if (key == "greedybetweenness") {
        return "Greedy-Betweenness";
    }
    if (key == "greedycloseness") {
        return "Greedy-Closeness";
    }
    if (key == "dpvbranchbenders" || key == "dpvcallbackbenders") {
        return "DPV-Branch-Benders";
    }
    if (key == "dpvbranchbendersllbi" || key == "dpvcallbackbendersllbi") {
        return "DPV-Branch-Benders-LLBI";
    }
    if (key == "dpvbranchbendersrootcuts" || key == "dpvcallbackbendersrootcuts") {
        return "DPV-Branch-Benders-RootCuts";
    }
    if (key == "dpvbranchbendersllbirootcuts" ||
        key == "dpvbranchbendersrootcutsllbi" ||
        key == "dpvcallbackbendersllbirootcuts" ||
        key == "dpvcallbackbendersrootcutsllbi") {
        return "DPV-Branch-Benders-LLBI-RootCuts";
    }

    std::ostringstream message;
    message << "Unsupported batch method '" << value << "'. Supported methods:";
    for (const auto& method : supported_batch_methods()) {
        message << " " << method;
    }
    throw std::runtime_error(message.str());
}

std::vector<std::string> parse_batch_method_list(const std::string& value) {
    const auto tokens = split_csv(value);
    if (tokens.empty()) {
        throw std::runtime_error("--methods must contain at least one method.");
    }

    std::vector<std::string> methods;
    std::unordered_set<std::string> seen;
    for (const auto& token : tokens) {
        const std::string method = normalize_batch_method_name(token);
        if (seen.insert(method).second) {
            methods.push_back(method);
        }
    }
    return methods;
}

std::vector<std::string> parse_fpp_mode_list(const std::string& value) {
    const auto tokens = split_csv(value);
    if (tokens.empty()) {
        throw std::runtime_error("fpp_modes must contain at least one mode.");
    }

    std::vector<std::string> modes;
    std::unordered_set<std::string> seen;
    for (const auto& token : tokens) {
        const std::string mode = normalize_fpp_mode(token);
        if (seen.insert(mode).second) {
            modes.push_back(mode);
        }
    }
    return modes;
}

std::string normalize_warm_start_policy(const std::string& value) {
    const std::string key = normalized_key(value);
    if (key == "none") {
        return "none";
    }
    if (key == "greedydpv3forfpp") {
        return "greedy-dpv3-for-fpp";
    }
    if (key == "greedydpv3fordpv") {
        return "greedy-dpv3-for-dpv";
    }
    if (key == "staticdpvforfpp") {
        return "static-dpv-for-fpp";
    }
    if (key == "staticdpvfordpv") {
        return "static-dpv-for-dpv";
    }

    std::ostringstream message;
    message << "Unsupported warm-start policy '" << value << "'. Supported policies:";
    for (const auto& policy : supported_warm_start_policies()) {
        message << " " << policy;
    }
    throw std::runtime_error(message.str());
}

std::string normalize_fpp_formulation(const std::string& value) {
    const std::string key = normalized_key(value);
    if (key == "base" || key == "direct" || key == "compact") {
        return "base";
    }
    if (key == "cut" || key == "cutreachability" || key == "reachability") {
        return "cut";
    }

    std::ostringstream message;
    message << "Unsupported FPP formulation '" << value << "'. Supported formulations:";
    for (const auto& formulation : supported_fpp_formulations()) {
        message << " " << formulation;
    }
    throw std::runtime_error(message.str());
}

std::string normalize_fpp_mode(const std::string& value) {
    const std::string key = normalized_key(value);
    if (key == "fppbase" || key == "base") {
        return "fpp_base";
    }
    if (key == "fppbasegreedy" || key == "basegreedy") {
        return "fpp_base_greedy";
    }
    if (key == "fppbasedominator" || key == "basedominator") {
        return "fpp_base_dominator";
    }
    if (key == "fppbaseseparator" || key == "baseseparator") {
        return "fpp_base_separator";
    }
    if (key == "fppbasedominatorseparator" || key == "basedominatorseparator") {
        return "fpp_base_dominator_separator";
    }
    if (key == "fppbasedominatorseparatorgreedy" || key == "basedominatorseparatorgreedy") {
        return "fpp_base_dominator_separator_greedy";
    }
    if (key == "fppcut" || key == "cut") {
        return "fpp_cut";
    }
    if (key == "fppcutgreedy" || key == "cutgreedy") {
        return "fpp_cut_greedy";
    }
    if (key == "fppcutdominator" || key == "cutdominator") {
        return "fpp_cut_dominator";
    }
    if (key == "fppcutseparator" || key == "cutseparator") {
        return "fpp_cut_separator";
    }
    if (key == "fppcutdominatorseparator" || key == "cutdominatorseparator") {
        return "fpp_cut_dominator_separator";
    }
    if (key == "fppcutdominatorseparatorgreedy" || key == "cutdominatorseparatorgreedy") {
        return "fpp_cut_dominator_separator_greedy";
    }

    std::ostringstream message;
    message << "Unsupported FPP mode '" << value << "'. Supported modes:";
    for (const auto& mode : supported_fpp_modes()) {
        message << " " << mode;
    }
    throw std::runtime_error(message.str());
}

FppModeSettings fpp_mode_settings(const std::string& mode) {
    const std::string normalized = normalize_fpp_mode(mode);
    FppModeSettings settings;
    settings.mode = normalized;
    settings.formulation = normalized.find("fpp_cut") == 0 ? "cut" : "base";
    settings.enable_greedy_warm_start = normalized.find("greedy") != std::string::npos;
    settings.enable_dominator_cuts = normalized.find("dominator") != std::string::npos;
    settings.enable_separator_cuts = normalized.find("separator") != std::string::npos;
    settings.enable_local_search = false;
    return settings;
}

DpvBranchBendersVariantSettings dpv_branch_benders_variant_settings(const std::string& method) {
    const std::string normalized = normalize_batch_method_name(method);
    DpvBranchBendersVariantSettings settings;
    if (normalized == "DPV-Branch-Benders") {
        settings.is_branch_benders = true;
        return settings;
    }
    if (normalized == "DPV-Branch-Benders-LLBI") {
        settings.is_branch_benders = true;
        settings.use_lifted_lower_bounds = true;
        return settings;
    }
    if (normalized == "DPV-Branch-Benders-RootCuts") {
        settings.is_branch_benders = true;
        settings.use_root_user_cuts = true;
        settings.root_user_cut_max_rounds = 1;
        return settings;
    }
    if (normalized == "DPV-Branch-Benders-LLBI-RootCuts") {
        settings.is_branch_benders = true;
        settings.use_lifted_lower_bounds = true;
        settings.use_root_user_cuts = true;
        settings.root_user_cut_max_rounds = 1;
        return settings;
    }
    return settings;
}

bool is_dpv_branch_benders_method(const std::string& method) {
    return dpv_branch_benders_variant_settings(method).is_branch_benders;
}

FppMethodVariantSettings fpp_method_variant_settings(const std::string& method) {
    const std::string normalized = normalize_batch_method_name(method);
    FppMethodVariantSettings settings;
    settings.risk_config.type = risk::RiskMeasureType::Expected;
    settings.risk_config.cvarBeta = 0.9;
    settings.risk_config.cvarLambda = 1.0;

    const auto parsed = parse_fpp_strengthened_method_key(normalized_key(normalized));
    if (parsed.recognized) {
        settings.is_fpp_solver = true;
        settings.is_fpp_saa = parsed.is_saa;
        settings.is_fpp_branch_benders = parsed.is_branch_benders;
        settings.is_fpp_restricted_branch_benders = parsed.is_restricted_branch_benders;
        settings.use_lifted_lower_bounds = parsed.use_lifted_lower_bounds;
        settings.use_root_user_cuts = parsed.use_root_user_cuts;
        settings.use_coverage_llbi = parsed.use_coverage_llbi;
        settings.use_path_llbi = parsed.use_path_llbi;
        settings.projected_llbi_options.use_projected_coverage_llbi_exp =
            parsed.use_projected_coverage_llbi_exp;
        settings.projected_llbi_options.use_projected_path_llbi_exp =
            parsed.use_projected_path_llbi_exp;
        settings.projected_llbi_options.use_projected_coverage_llbi_poly =
            parsed.use_projected_coverage_llbi_poly;
        settings.projected_llbi_options.use_projected_path_llbi_poly =
            parsed.use_projected_path_llbi_poly;
        settings.use_global_dominance_preprocessing =
            parsed.use_global_dominance_preprocessing;
        settings.use_conditional_zero_benefit_fixing =
            parsed.use_conditional_zero_benefit_fixing;
        settings.use_combinatorial_benders = parsed.use_combinatorial_benders;
        settings.combinatorial_options.enabled = parsed.use_combinatorial_benders;
        settings.combinatorial_options.lift_mode =
            benders::FppCombinatorialBendersLiftMode::Heuristic;
        settings.combinatorial_options.scenario_order =
            parsed.combinatorial_scenario_order;
        settings.combinatorial_options.cut_sampling_ratio = 0.10;
        settings.combinatorial_options.separate_fractional = true;
        settings.combinatorial_options.initial_cuts = true;
        settings.risk_config = parsed.risk_config;
        return settings;
    }

    if (normalized == "FPP-SAA") {
        settings.is_fpp_solver = true;
        settings.is_fpp_saa = true;
        return settings;
    }
    if (normalized == "FPP-SAA-CVaR") {
        settings.is_fpp_solver = true;
        settings.is_fpp_saa = true;
        settings.risk_config.type = risk::RiskMeasureType::CVaR;
        settings.risk_config.cvarLambda = 1.0;
        return settings;
    }
    if (normalized == "FPP-SAA-MeanCVaR") {
        settings.is_fpp_solver = true;
        settings.is_fpp_saa = true;
        settings.risk_config.type = risk::RiskMeasureType::MeanCVaR;
        settings.risk_config.cvarLambda = 0.5;
        return settings;
    }
    if (normalized == "FPP-Benders") {
        settings.is_fpp_solver = true;
        settings.is_fpp_benders = true;
        return settings;
    }
    if (normalized == "FPP-Benders-CVaR") {
        settings.is_fpp_solver = true;
        settings.is_fpp_benders = true;
        settings.risk_config.type = risk::RiskMeasureType::CVaR;
        settings.risk_config.cvarLambda = 1.0;
        return settings;
    }
    if (normalized == "FPP-Benders-MeanCVaR") {
        settings.is_fpp_solver = true;
        settings.is_fpp_benders = true;
        settings.risk_config.type = risk::RiskMeasureType::MeanCVaR;
        settings.risk_config.cvarLambda = 0.5;
        return settings;
    }

    auto set_branch = [&settings]() {
        settings.is_fpp_solver = true;
        settings.is_fpp_branch_benders = true;
    };
    if (normalized == "FPP-Branch-Benders") {
        set_branch();
        return settings;
    }
    if (normalized == "FPP-Branch-Benders-CVaR") {
        set_branch();
        settings.risk_config.type = risk::RiskMeasureType::CVaR;
        settings.risk_config.cvarLambda = 1.0;
        return settings;
    }
    if (normalized == "FPP-Branch-Benders-MeanCVaR") {
        set_branch();
        settings.risk_config.type = risk::RiskMeasureType::MeanCVaR;
        settings.risk_config.cvarLambda = 0.5;
        return settings;
    }
    if (normalized == "FPP-Branch-Benders-LLBI") {
        set_branch();
        settings.use_lifted_lower_bounds = true;
        return settings;
    }
    if (normalized == "FPP-Branch-Benders-RootCuts") {
        set_branch();
        settings.use_root_user_cuts = true;
        return settings;
    }
    if (normalized == "FPP-Branch-Benders-LLBI-RootCuts") {
        set_branch();
        settings.use_lifted_lower_bounds = true;
        settings.use_root_user_cuts = true;
        return settings;
    }
    if (normalized == "FPP-Branch-Benders-CVaR-LLBI") {
        set_branch();
        settings.risk_config.type = risk::RiskMeasureType::CVaR;
        settings.risk_config.cvarLambda = 1.0;
        settings.use_lifted_lower_bounds = true;
        return settings;
    }
    if (normalized == "FPP-Branch-Benders-CVaR-RootCuts") {
        set_branch();
        settings.risk_config.type = risk::RiskMeasureType::CVaR;
        settings.risk_config.cvarLambda = 1.0;
        settings.use_root_user_cuts = true;
        return settings;
    }
    if (normalized == "FPP-Branch-Benders-CVaR-LLBI-RootCuts") {
        set_branch();
        settings.risk_config.type = risk::RiskMeasureType::CVaR;
        settings.risk_config.cvarLambda = 1.0;
        settings.use_lifted_lower_bounds = true;
        settings.use_root_user_cuts = true;
        return settings;
    }
    auto set_restricted = [&]() {
        settings.is_fpp_solver = true;
        settings.is_fpp_restricted_branch_benders = true;
    };
    if (normalized == "FPP-Restricted-Branch-Benders") {
        set_restricted();
        return settings;
    }
    if (normalized == "FPP-Restricted-Branch-Benders-LLBI") {
        set_restricted();
        settings.use_lifted_lower_bounds = true;
        return settings;
    }
    if (normalized == "FPP-Restricted-Branch-Benders-RootCuts") {
        set_restricted();
        settings.use_root_user_cuts = true;
        return settings;
    }
    if (normalized == "FPP-Restricted-Branch-Benders-LLBI-RootCuts") {
        set_restricted();
        settings.use_lifted_lower_bounds = true;
        settings.use_root_user_cuts = true;
        return settings;
    }
    if (normalized == "FPP-Restricted-Branch-Benders-CVaR") {
        set_restricted();
        settings.risk_config.type = risk::RiskMeasureType::CVaR;
        settings.risk_config.cvarLambda = 1.0;
        return settings;
    }
    if (normalized == "FPP-Restricted-Branch-Benders-CVaR-LLBI") {
        set_restricted();
        settings.risk_config.type = risk::RiskMeasureType::CVaR;
        settings.risk_config.cvarLambda = 1.0;
        settings.use_lifted_lower_bounds = true;
        return settings;
    }
    if (normalized == "FPP-Restricted-Branch-Benders-CVaR-RootCuts") {
        set_restricted();
        settings.risk_config.type = risk::RiskMeasureType::CVaR;
        settings.risk_config.cvarLambda = 1.0;
        settings.use_root_user_cuts = true;
        return settings;
    }
    if (normalized == "FPP-Restricted-Branch-Benders-CVaR-LLBI-RootCuts") {
        set_restricted();
        settings.risk_config.type = risk::RiskMeasureType::CVaR;
        settings.risk_config.cvarLambda = 1.0;
        settings.use_lifted_lower_bounds = true;
        settings.use_root_user_cuts = true;
        return settings;
    }
    if (normalized == "FPP-Restricted-Branch-Benders-MeanCVaR") {
        set_restricted();
        settings.risk_config.type = risk::RiskMeasureType::MeanCVaR;
        settings.risk_config.cvarLambda = 0.5;
        return settings;
    }
    if (normalized == "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI") {
        set_restricted();
        settings.risk_config.type = risk::RiskMeasureType::MeanCVaR;
        settings.risk_config.cvarLambda = 0.5;
        settings.use_lifted_lower_bounds = true;
        return settings;
    }
    if (normalized == "FPP-Restricted-Branch-Benders-MeanCVaR-RootCuts") {
        set_restricted();
        settings.risk_config.type = risk::RiskMeasureType::MeanCVaR;
        settings.risk_config.cvarLambda = 0.5;
        settings.use_root_user_cuts = true;
        return settings;
    }
    if (normalized == "FPP-Restricted-Branch-Benders-MeanCVaR-LLBI-RootCuts") {
        set_restricted();
        settings.risk_config.type = risk::RiskMeasureType::MeanCVaR;
        settings.risk_config.cvarLambda = 0.5;
        settings.use_lifted_lower_bounds = true;
        settings.use_root_user_cuts = true;
        return settings;
    }

    return settings;
}

bool is_fpp_solver_method(const std::string& method) {
    return fpp_method_variant_settings(method).is_fpp_solver;
}

std::string fpp_mode_name_from_settings(
    const std::string& formulation,
    bool enable_greedy_warm_start,
    bool enable_dominator_cuts,
    bool enable_separator_cuts,
    bool enable_local_search) {
    if (enable_local_search) {
        return "";
    }

    const std::string normalized_formulation = normalize_fpp_formulation(formulation);
    std::string mode = normalized_formulation == "cut" ? "fpp_cut" : "fpp_base";
    if (enable_dominator_cuts && enable_separator_cuts) {
        mode += "_dominator_separator";
    } else if (enable_dominator_cuts) {
        mode += "_dominator";
    } else if (enable_separator_cuts) {
        mode += "_separator";
    }
    if (enable_greedy_warm_start) {
        mode += "_greedy";
    }
    return normalize_fpp_mode(mode);
}

std::string fpp_enhancement_config_summary(const BatchExperimentConfig& config) {
    std::ostringstream out;
    out << "FPP formulation options:\n"
        << "  fpp_modes: " << (config.fpp_modes.empty() ? "(single explicit flag set)" : "")
        << (config.fpp_modes.empty() ? "" : [&config]() {
            std::ostringstream modes;
            for (std::size_t i = 0; i < config.fpp_modes.size(); ++i) {
                if (i > 0) {
                    modes << ",";
                }
                modes << normalize_fpp_mode(config.fpp_modes[i]);
            }
            return modes.str();
        }()) << "\n"
        << "  formulation: " << normalize_fpp_formulation(config.fpp_formulation) << "\n"
        << "  enable_dominator_cuts: " << (config.enable_dominator_cuts ? "true" : "false") << "\n"
        << "  enable_separator_cuts: " << (config.enable_separator_cuts ? "true" : "false") << "\n"
        << "  enable_greedy_warm_start: " << (config.enable_greedy_warm_start ? "true" : "false") << "\n"
        << "  enable_local_search: " << (config.enable_local_search ? "true" : "false") << "\n"
        << "Separator parameters:\n"
        << "  sep_at_root: " << (config.sep_at_root ? "true" : "false") << "\n"
        << "  sep_frequency_nodes: " << config.sep_frequency_nodes << "\n"
        << "  sep_max_scenarios_per_call: " << config.sep_max_scenarios_per_call << "\n"
        << "  sep_max_nodes_per_scenario: " << config.sep_max_nodes_per_scenario << "\n"
        << "  sep_max_cuts_per_call: " << config.sep_max_cuts_per_call << "\n"
        << "  sep_min_violation: " << config.sep_min_violation << "\n"
        << "  sep_max_cut_cardinality: " << config.sep_max_cut_cardinality << "\n"
        << "Heuristic parameters:\n"
        << "  candidate_pool_size_multiplier: " << config.candidate_pool_size_multiplier << "\n"
        << "  candidate_pool_min_size: " << config.candidate_pool_min_size << "\n"
        << "  enable_greedy_exact_marginal: " << (config.enable_greedy_exact_marginal ? "true" : "false") << "\n"
        << "  local_search_max_iterations: " << config.local_search_max_iterations << "\n"
        << "  local_search_time_limit_sec: " << config.local_search_time_limit_sec << "\n"
        << "Dominator parameters:\n"
        << "  max_aggregate_dominator_cuts_per_scenario: "
        << config.max_aggregate_dominator_cuts_per_scenario << "\n"
        << "  max_individual_dominator_cuts_per_scenario: "
        << config.max_individual_dominator_cuts_per_scenario << "\n"
        << "DPV Branch-Benders variant parameters:\n"
        << "  root_user_cut_max_rounds: " << config.root_user_cut_max_rounds << "\n"
        << "  root_user_cut_tolerance: ";
    if (std::isnan(config.root_user_cut_tolerance)) {
        out << "(use Benders tolerance)\n";
    } else {
        out << config.root_user_cut_tolerance << "\n";
    }
    out << "FPP strengthening parameters:\n"
        << "  use_coverage_llbi: " << (config.use_coverage_llbi ? "true" : "false") << "\n"
        << "  use_path_llbi: " << (config.use_path_llbi ? "true" : "false") << "\n"
        << "  path_llbi_max_paths_per_node: " << config.path_llbi_max_paths_per_node << "\n"
        << "  use_projected_coverage_llbi_exp: "
        << (config.projected_llbi_options.use_projected_coverage_llbi_exp ? "true" : "false") << "\n"
        << "  use_projected_path_llbi_exp: "
        << (config.projected_llbi_options.use_projected_path_llbi_exp ? "true" : "false") << "\n"
        << "  use_projected_coverage_llbi_poly: "
        << (config.projected_llbi_options.use_projected_coverage_llbi_poly ? "true" : "false") << "\n"
        << "  use_projected_path_llbi_poly: "
        << (config.projected_llbi_options.use_projected_path_llbi_poly ? "true" : "false") << "\n"
        << "  projected_llbi_root_rounds: " << config.projected_llbi_options.root_rounds << "\n"
        << "  projected_llbi_max_cuts_per_round: "
        << config.projected_llbi_options.max_cuts_per_round << "\n"
        << "  projected_llbi_violation_tolerance: "
        << config.projected_llbi_options.violation_tolerance << "\n"
        << "  projected_llbi_cut_density_limit: "
        << config.projected_llbi_options.cut_density_limit << "\n"
        << "  use_global_dominance_preprocessing: "
        << (config.use_global_dominance_preprocessing ? "true" : "false") << "\n"
        << "  use_conditional_zero_benefit_fixing: "
        << (config.use_conditional_zero_benefit_fixing ? "true" : "false") << "\n";
    out << "FPP combinatorial Branch-Benders parameters:\n"
        << "  enabled: " << (config.combinatorial_options.enabled ? "true" : "false") << "\n"
        << "  lift_mode: " << benders::to_string(config.combinatorial_options.lift_mode) << "\n"
        << "  scenario_order: "
        << benders::to_string(config.combinatorial_options.scenario_order) << "\n"
        << "  cut_sampling_ratio: " << config.combinatorial_options.cut_sampling_ratio << "\n"
        << "  separate_fractional: "
        << (config.combinatorial_options.separate_fractional ? "true" : "false") << "\n"
        << "  initial_cuts: "
        << (config.combinatorial_options.initial_cuts ? "true" : "false") << "\n";
    out << "Risk parameters:\n"
        << "  risk_measure: "
        << (config.risk_measure_specified ? risk::to_string(config.risk_config.type) : "(method label default)")
        << "\n"
        << "  cvar_beta: " << config.risk_config.cvarBeta << "\n"
        << "  cvar_lambda: " << config.risk_config.cvarLambda << "\n";
    if (config.enable_greedy_warm_start) {
        const std::string formulation = normalize_fpp_formulation(config.fpp_formulation);
        out << "  note: reachability-greedy warm start generates "
            << (formulation == "cut" ? "a full y/x/q" : "a y-only")
            << " MIP start for FPP-SAA.\n";
    }
    return out.str();
}

void validate_batch_experiment_config(const BatchExperimentConfig& config) {
    if (config.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (config.alpha_values.empty()) {
        throw std::runtime_error("--alphas is required.");
    }
    if (config.train_counts.empty()) {
        throw std::runtime_error("--train-counts is required.");
    }
    if (config.test_count == 0) {
        throw std::runtime_error("--test-count must be positive.");
    }
    if (config.num_cases == 0) {
        throw std::runtime_error("--num-cases must be positive.");
    }
    if (config.methods.empty()) {
        throw std::runtime_error("--methods is required.");
    }
    if (config.output_dir.empty()) {
        throw std::runtime_error("--output-dir is required.");
    }
    if (config.output_csv.empty()) {
        throw std::runtime_error("--output-csv is required.");
    }
    if (config.shared_splits && config.split_dir.empty()) {
        throw std::runtime_error("split_dir is required when shared_splits=true.");
    }
    if (config.threads < 0) {
        throw std::runtime_error("--threads must be nonnegative.");
    }
    if (config.time_limit_seconds < 0.0) {
        throw std::runtime_error("--time-limit must be nonnegative.");
    }
    if (config.mip_gap < -1.0) {
        throw std::runtime_error("--mip-gap must be nonnegative, or omitted.");
    }
    if (config.sep_frequency_nodes < 0) {
        throw std::runtime_error("sep_frequency_nodes must be nonnegative.");
    }
    if (config.sep_max_scenarios_per_call < 0) {
        throw std::runtime_error("sep_max_scenarios_per_call must be nonnegative.");
    }
    if (config.sep_max_nodes_per_scenario < 0) {
        throw std::runtime_error("sep_max_nodes_per_scenario must be nonnegative.");
    }
    if (config.sep_max_cuts_per_call < 0) {
        throw std::runtime_error("sep_max_cuts_per_call must be nonnegative.");
    }
    if (config.sep_min_violation < 0.0) {
        throw std::runtime_error("sep_min_violation must be nonnegative.");
    }
    if (config.sep_max_cut_cardinality < 0) {
        throw std::runtime_error("sep_max_cut_cardinality must be nonnegative.");
    }
    if (config.candidate_pool_size_multiplier < 0) {
        throw std::runtime_error("candidate_pool_size_multiplier must be nonnegative.");
    }
    if (config.candidate_pool_min_size < 0) {
        throw std::runtime_error("candidate_pool_min_size must be nonnegative.");
    }
    if (config.local_search_max_iterations < 0) {
        throw std::runtime_error("local_search_max_iterations must be nonnegative.");
    }
    if (config.local_search_time_limit_sec < 0.0) {
        throw std::runtime_error("local_search_time_limit_sec must be nonnegative.");
    }
    if (config.max_aggregate_dominator_cuts_per_scenario < 0) {
        throw std::runtime_error("max_aggregate_dominator_cuts_per_scenario must be nonnegative.");
    }
    if (config.max_individual_dominator_cuts_per_scenario < 0) {
        throw std::runtime_error("max_individual_dominator_cuts_per_scenario must be nonnegative.");
    }
    if (config.root_user_cut_max_rounds <= 0) {
        throw std::runtime_error("root_user_cut_max_rounds must be positive.");
    }
    if (!std::isnan(config.root_user_cut_tolerance) &&
        config.root_user_cut_tolerance < 0.0) {
        throw std::runtime_error("root_user_cut_tolerance must be nonnegative.");
    }
    if (config.path_llbi_max_paths_per_node <= 0) {
        throw std::runtime_error("path_llbi_max_paths_per_node must be positive.");
    }
    benders::validate_fpp_projected_llbi_options(config.projected_llbi_options);
    const int exclusive_llbi_count =
        (config.use_coverage_llbi ? 1 : 0) +
        (config.use_path_llbi ? 1 : 0) +
        (config.projected_llbi_options.use_projected_coverage_llbi_exp ? 1 : 0) +
        (config.projected_llbi_options.use_projected_path_llbi_exp ? 1 : 0) +
        (config.projected_llbi_options.use_projected_coverage_llbi_poly ? 1 : 0) +
        (config.projected_llbi_options.use_projected_path_llbi_poly ? 1 : 0);
    if (exclusive_llbi_count > 1) {
        throw std::runtime_error(
            "Extended CoverageLLBI, extended PathLLBI, and projected LLBI variants are mutually exclusive in one batch config.");
    }
    benders::validate_fpp_combinatorial_benders_options(config.combinatorial_options);
    risk::validate_risk_measure_config(config.risk_config);

    for (const double alpha : config.alpha_values) {
        if (alpha < 0.0) {
            throw std::runtime_error("Alpha values must be nonnegative.");
        }
    }
    for (const auto train_count : config.train_counts) {
        if (train_count == 0) {
            throw std::runtime_error("Train counts must be positive.");
        }
    }
    for (const auto& method : config.methods) {
        (void)normalize_batch_method_name(method);
        if (!is_fpp_solver_method(method) &&
            config.risk_measure_specified &&
            config.risk_config.type != risk::RiskMeasureType::Expected) {
            if (method == "DPV-SAA" ||
                method == "DPV-Benders" ||
                is_dpv_branch_benders_method(method)) {
                throw std::runtime_error("DPV-CVaR optimization is out of scope and not implemented.");
            }
            throw std::runtime_error("risk_measure=cvar is only supported for FPP CVaR method labels.");
        }
        if (is_fpp_solver_method(method) && config.risk_measure_specified) {
            const auto variant = fpp_method_variant_settings(method);
            if (variant.risk_config.type != config.risk_config.type) {
                throw std::runtime_error(
                    "Manifest risk_measure conflicts with FPP method label " + method + ".");
            }
        }
    }
    for (const auto& mode : config.fpp_modes) {
        (void)fpp_mode_settings(mode);
    }
    (void)normalize_warm_start_policy(config.warm_start_policy);
    (void)normalize_fpp_formulation(config.fpp_formulation);
    if (!config.weight_profile.empty() && config.weight_map_file.empty()) {
        throw std::runtime_error(
            "weight_profile is set but weight_map_file is empty; a weighted row must "
            "name the canonical registry CSV it was resolved against.");
    }
    if (config.weight_replicate < 0) {
        throw std::runtime_error("weight_replicate must be nonnegative.");
    }
}

}  // namespace firebreak::experiments
