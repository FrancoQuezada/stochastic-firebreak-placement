#include "experiments/FppMasterLpDiagnosticRunner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "benders/FppLiftedLowerBound.hpp"
#include "benders/FppProjectedLlbi.hpp"
#include "benders/FppStrengthening.hpp"
#include "io/Cell2FireReader.hpp"
#include "io/PathUtils.hpp"
#include "io/ScenarioFileUtils.hpp"
#include "opt/OptimizationInstanceBuilder.hpp"
#include "solver/CplexEnvironment.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::experiments {

namespace {

struct DiagnosticResult {
    std::string experiment_id;
    std::string case_id;
    unsigned int seed_base = 0;
    unsigned int seed = 0;
    std::string landscape;
    double alpha = 0.0;
    int train_count = 0;
    std::vector<int> train_ids;
    std::string variant;
    std::string lp_status;
    double lp_objective_value = std::numeric_limits<double>::quiet_NaN();
    double lp_runtime_sec = 0.0;
    long long num_variables = 0;
    long long num_constraints = 0;
    long long num_nonzeros = 0;
    long long num_y_variables = 0;
    long long num_eta_variables = 0;
    long long num_x_variables = 0;
    long long llbi_num_constraints = 0;
    long long llbi_num_nonzeros = 0;
    long long coverage_llbi_num_zeta_vars = 0;
    long long coverage_llbi_num_constraints = 0;
    long long coverage_llbi_num_nonzeros = 0;
    long long path_llbi_num_b_vars = 0;
    long long path_llbi_num_path_constraints = 0;
    long long path_llbi_num_paths_used = 0;
    long long path_llbi_num_nonzeros = 0;
    bool projected_coverage_llbi_enabled = false;
    bool projected_path_llbi_enabled = false;
    std::string projected_llbi_family;
    std::string projected_llbi_strategy;
    std::string projected_llbi_mode;
    int projected_llbi_root_rounds = 0;
    int projected_llbi_cuts_added = 0;
    int projected_llbi_coverage_cuts_added = 0;
    int projected_llbi_path_cuts_added = 0;
    int projected_llbi_total_nonzeros = 0;
    double projected_llbi_avg_nonzeros_per_cut = 0.0;
    int projected_llbi_max_nonzeros_per_cut = 0;
    double projected_llbi_separation_time_sec = 0.0;
    double projected_llbi_solve_time_sec = 0.0;
    double projected_llbi_total_time_sec = 0.0;
    double projected_llbi_root_bound_initial = std::numeric_limits<double>::quiet_NaN();
    double projected_llbi_root_bound_final = std::numeric_limits<double>::quiet_NaN();
    double projected_llbi_root_bound_improvement_abs = 0.0;
    double projected_llbi_root_bound_improvement_pct = 0.0;
    int projected_poly_candidate_cuts_generated = 0;
    int projected_poly_candidate_cuts_added = 0;
    bool projected_poly_enumeration_truncated = false;
    int projected_poly_enumeration_limit = 0;
    int projected_exp_separated_cuts_added = 0;
    int projected_exp_separation_rounds = 0;
    int projected_exp_candidate_cuts_generated = 0;
    int projected_exp_candidate_cuts_added = 0;
    bool projected_exp_enumeration_truncated = false;
    int projected_exp_enumeration_limit = 0;
    double llbi_precompute_time_sec = 0.0;
    double coverage_llbi_precompute_time_sec = 0.0;
    double path_llbi_precompute_time_sec = 0.0;
    double total_static_lb_precompute_time_sec = 0.0;
    long long master_rows_before_static_lb = 0;
    long long master_cols_before_static_lb = 0;
    long long master_nonzeros_before_static_lb = 0;
    long long master_rows_after_static_lb = 0;
    long long master_cols_after_static_lb = 0;
    long long master_nonzeros_after_static_lb = 0;
    double eta_sum_value = 0.0;
    double min_eta_s = 0.0;
    double avg_eta_s = 0.0;
    double max_eta_s = 0.0;
    std::string warning;
};

std::filesystem::path default_forest_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / "data" / "CanadianFBP" / landscape;
}

std::filesystem::path default_results_path(const std::string& landscape) {
    return firebreak::io::repo_root() / "sample_test" / landscape;
}

std::string join_ints(const std::vector<int>& values, char delimiter = ';') {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << delimiter;
        }
        out << values[i];
    }
    return out.str();
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch; break;
        }
    }
    return out.str();
}

std::string csv_escape(const std::string& value) {
    const bool needs_quotes =
        value.find(',') != std::string::npos ||
        value.find('"') != std::string::npos ||
        value.find('\n') != std::string::npos ||
        value.find('\r') != std::string::npos;
    if (!needs_quotes) {
        return value;
    }
    std::string escaped = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string format_double(double value) {
    if (!std::isfinite(value)) {
        if (std::isnan(value)) {
            return "nan";
        }
        return value > 0.0 ? "inf" : "-inf";
    }
    std::ostringstream out;
    out << std::setprecision(12) << value;
    return out.str();
}

double scenario_probability_at(const opt::OptimizationInstance& opt, std::size_t scenario_index) {
    const double scenario_probability = opt.scenarios[scenario_index].probability;
    if (std::isfinite(scenario_probability)) {
        return scenario_probability;
    }
    if (scenario_index < opt.scenario_probabilities.size() &&
        std::isfinite(opt.scenario_probabilities[scenario_index])) {
        return opt.scenario_probabilities[scenario_index];
    }
    throw std::runtime_error("FPP LP diagnostic found a nonfinite scenario probability.");
}

std::vector<int> y_positions_by_node(const opt::OptimizationInstance& opt) {
    std::vector<int> positions(static_cast<std::size_t>(opt.node_mapper.size()), -1);
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        const int compact_node = opt.eligible_indices[pos];
        positions[static_cast<std::size_t>(compact_node)] = static_cast<int>(pos);
    }
    return positions;
}

void validate_result(const DiagnosticResult& result) {
    if (!std::isfinite(result.lp_objective_value)) {
        throw std::runtime_error("LP diagnostic objective is NaN or infinite.");
    }
    if (result.lp_objective_value < -1.0e-6) {
        throw std::runtime_error("LP diagnostic objective is negative below tolerance.");
    }
    if (result.lp_status.find("Unbounded") != std::string::npos ||
        result.lp_status.find("unbounded") != std::string::npos) {
        throw std::runtime_error("LP diagnostic returned an unbounded status.");
    }
    if (result.variant.find("master_lp_") == 0 && result.min_eta_s < -1.0e-6) {
        throw std::runtime_error("LP diagnostic found eta_s below -1e-6.");
    }
}

#ifdef FIREBREAK_WITH_CPLEX

DiagnosticResult solve_fpp_saa_lp_base(
    const opt::OptimizationInstance& opt,
    const FppMasterLpDiagnosticOptions& options) {
    const int node_count = opt.node_mapper.size();
    const auto y_pos = y_positions_by_node(opt);

    DiagnosticResult result;
    result.num_y_variables = static_cast<long long>(opt.eligible_indices.size());
    result.num_x_variables =
        static_cast<long long>(node_count) * static_cast<long long>(opt.scenarios.size());

    IloEnv env;
    try {
        IloModel model(env);
        IloNumVarArray y(env, static_cast<IloInt>(opt.eligible_indices.size()), 0.0, 1.0, ILOFLOAT);
        for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
            std::ostringstream name;
            name << "y_" << opt.eligible_indices[pos];
            y[static_cast<IloInt>(pos)].setName(name.str().c_str());
        }

        std::vector<IloNumVarArray> x;
        x.reserve(opt.scenarios.size());
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            IloNumVarArray x_s(env, node_count, 0.0, 1.0, ILOFLOAT);
            for (int i = 0; i < node_count; ++i) {
                std::ostringstream name;
                name << "x_s" << opt.scenarios[s].scenario_id << "_" << i;
                x_s[i].setName(name.str().c_str());
            }
            x.push_back(x_s);
        }

        IloExpr objective(env);
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            const double probability = scenario_probability_at(opt, s);
            for (int i = 0; i < node_count; ++i) {
                objective += probability * x[s][i];
            }
        }
        model.add(IloMinimize(env, objective));
        objective.end();

        IloExpr budget(env);
        for (IloInt pos = 0; pos < y.getSize(); ++pos) {
            budget += y[pos];
        }
        model.add(budget <= opt.budget);
        budget.end();

        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            model.add(x[s][opt.scenarios[s].ignition_index] == 1.0);
            for (const auto& arc : opt.scenarios[s].arcs) {
                IloExpr expr(env);
                expr += x[s][arc.u_index];
                expr -= x[s][arc.v_index];
                const int target_y_pos = y_pos[static_cast<std::size_t>(arc.v_index)];
                if (target_y_pos >= 0) {
                    expr -= y[static_cast<IloInt>(target_y_pos)];
                }
                model.add(expr <= 0.0);
                expr.end();
            }
        }

        IloCplex cplex(model);
        cplex.setOut(env.getNullStream());
        cplex.setWarning(env.getNullStream());
        if (options.threads > 0) {
            cplex.setParam(IloCplex::Param::Threads, options.threads);
        }

        const auto start = std::chrono::steady_clock::now();
        const bool solved = cplex.solve();
        const auto end = std::chrono::steady_clock::now();
        result.lp_runtime_sec = std::chrono::duration<double>(end - start).count();
        std::ostringstream status;
        status << cplex.getStatus();
        result.lp_status = solved ? status.str() : "No solution";
        if (solved) {
            result.lp_objective_value = cplex.getObjValue();
        }
        result.num_variables = static_cast<long long>(cplex.getNcols());
        result.num_constraints = static_cast<long long>(cplex.getNrows());
        result.num_nonzeros = static_cast<long long>(cplex.getNNZs());
        env.end();
        return result;
    } catch (...) {
        env.end();
        throw;
    }
}

long long coverage_nonzeros(const benders::FppCoverageLlbiData& data) {
    long long total = 0;
    if (!data.enabled) {
        return total;
    }
    for (const auto& scenario : data.scenarios) {
        long long zeta_count = 0;
        for (const auto& node : scenario.nodes) {
            total += 1 + static_cast<long long>(node.covering_candidate_compact_nodes.size());
            ++zeta_count;
        }
        if (zeta_count > 0) {
            total += 1 + zeta_count;
        }
    }
    return total;
}

long long path_nonzeros(const benders::FppPathLlbiData& data) {
    long long total = 0;
    if (!data.enabled) {
        return total;
    }
    for (const auto& scenario : data.scenarios) {
        long long b_count = 0;
        for (const auto& node : scenario.nodes) {
            ++b_count;
            for (const auto& path : node.paths) {
                total += 1 + static_cast<long long>(path.blocking_candidate_compact_nodes.size());
            }
        }
        if (b_count > 0) {
            total += 1 + b_count;
        }
    }
    return total;
}

DiagnosticResult solve_master_lp(
    const opt::OptimizationInstance& opt,
    const FppMasterLpDiagnosticOptions& options) {
    const bool use_llbi = options.variant == "master_lp_llbi";
    const bool use_coverage = options.variant == "master_lp_coverage_llbi";
    const bool use_path = options.variant == "master_lp_path_llbi";
    const bool use_projected_coverage_exp =
        options.variant == "master_lp_projected_coverage_exp";
    const bool use_projected_path_exp =
        options.variant == "master_lp_projected_path_exp";
    const bool use_projected_coverage_poly =
        options.variant == "master_lp_projected_coverage_poly";
    const bool use_projected_path_poly =
        options.variant == "master_lp_projected_path_poly";
    const int node_count = opt.node_mapper.size();
    const auto y_pos = y_positions_by_node(opt);

    DiagnosticResult result;
    result.num_y_variables = static_cast<long long>(opt.eligible_indices.size());
    result.num_eta_variables = static_cast<long long>(opt.scenarios.size());
    result.master_rows_before_static_lb = 1;
    result.master_cols_before_static_lb = result.num_y_variables + result.num_eta_variables;
    result.master_nonzeros_before_static_lb = result.num_y_variables;

    IloEnv env;
    try {
        IloModel model(env);
        IloNumVarArray y(env, static_cast<IloInt>(opt.eligible_indices.size()), 0.0, 1.0, ILOFLOAT);
        for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
            std::ostringstream name;
            name << "y_" << opt.eligible_indices[pos];
            y[static_cast<IloInt>(pos)].setName(name.str().c_str());
        }

        IloNumVarArray eta(env, static_cast<IloInt>(opt.scenarios.size()), 0.0, IloInfinity, ILOFLOAT);
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            std::ostringstream name;
            name << "eta_s" << opt.scenarios[s].scenario_id;
            eta[static_cast<IloInt>(s)].setName(name.str().c_str());
        }

        IloExpr objective(env);
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            objective += scenario_probability_at(opt, s) * eta[static_cast<IloInt>(s)];
        }
        model.add(IloMinimize(env, objective));
        objective.end();

        IloExpr budget(env);
        for (IloInt pos = 0; pos < y.getSize(); ++pos) {
            budget += y[pos];
        }
        model.add(budget <= opt.budget);
        budget.end();

        auto add_projected_cut = [&](const benders::BendersCut& cut, int scenario_index) {
            IloExpr expr(env);
            expr += eta[static_cast<IloInt>(scenario_index)];
            long long nz = 1;
            for (const auto& [compact_index, coefficient] : cut.coefficients_by_compact_index) {
                if (std::fabs(coefficient) <= 1.0e-12) {
                    continue;
                }
                const int pos = y_pos[static_cast<std::size_t>(compact_index)];
                if (pos < 0) {
                    expr.end();
                    throw std::runtime_error(
                        "Projected LLBI diagnostic cut references a non-eligible compact node.");
                }
                expr -= coefficient * y[static_cast<IloInt>(pos)];
                ++nz;
            }
            model.add(expr >= cut.rhs_constant);
            expr.end();
            ++result.projected_llbi_cuts_added;
            if (result.projected_coverage_llbi_enabled) {
                ++result.projected_llbi_coverage_cuts_added;
            }
            if (result.projected_path_llbi_enabled) {
                ++result.projected_llbi_path_cuts_added;
            }
            result.projected_llbi_total_nonzeros += static_cast<int>(nz);
            result.projected_llbi_max_nonzeros_per_cut =
                std::max(result.projected_llbi_max_nonzeros_per_cut, static_cast<int>(nz));
        };

        if (use_llbi) {
            const auto llbi_start = std::chrono::steady_clock::now();
            const auto llbi = benders::build_fpp_lifted_lower_bounds(opt);
            result.llbi_precompute_time_sec = llbi.precompute_time_sec;
            for (std::size_t s = 0; s < llbi.inequalities.size(); ++s) {
                const auto& inequality = llbi.inequalities[s];
                IloExpr expr(env);
                expr += eta[static_cast<IloInt>(s)];
                long long nz = 1;
                for (const auto& [compact_index, coefficient] : inequality.coefficients_by_compact_index) {
                    if (std::fabs(coefficient) <= 1.0e-12) {
                        continue;
                    }
                    const int pos = y_pos[static_cast<std::size_t>(compact_index)];
                    if (pos < 0) {
                        throw std::runtime_error("LLBI references a non-eligible compact node.");
                    }
                    expr -= coefficient * y[static_cast<IloInt>(pos)];
                    ++nz;
                }
                model.add(expr >= inequality.rhs_constant);
                expr.end();
                ++result.llbi_num_constraints;
                result.llbi_num_nonzeros += nz;
            }
            const auto llbi_end = std::chrono::steady_clock::now();
            if (result.llbi_precompute_time_sec <= 0.0) {
                result.llbi_precompute_time_sec =
                    std::chrono::duration<double>(llbi_end - llbi_start).count();
            }
        }

        const auto coverage = benders::build_fpp_coverage_llbi_data(opt, use_coverage);
        const auto path = benders::build_fpp_path_llbi_data(
            opt,
            use_path,
            options.path_llbi_max_paths_per_node);
        result.coverage_llbi_precompute_time_sec = coverage.precompute_time_sec;
        result.path_llbi_precompute_time_sec = path.precompute_time_sec;
        result.coverage_llbi_num_zeta_vars = coverage.num_zeta_vars;
        result.coverage_llbi_num_constraints = coverage.num_constraints;
        result.coverage_llbi_num_nonzeros = coverage_nonzeros(coverage);
        result.path_llbi_num_b_vars = path.num_b_vars;
        result.path_llbi_num_path_constraints = path.num_path_constraints;
        result.path_llbi_num_paths_used = path.num_paths_used;
        result.path_llbi_num_nonzeros = path_nonzeros(path);

        if (coverage.enabled) {
            for (const auto& scenario : coverage.scenarios) {
                IloExpr lower_bound_rhs(env);
                lower_bound_rhs += scenario.empty_burned_area;
                for (const auto& node : scenario.nodes) {
                    IloNumVar zeta(env, 0.0, 1.0, ILOFLOAT);
                    std::ostringstream name;
                    name << "coverage_zeta_s" << scenario.scenario_id << "_" << node.compact_node;
                    zeta.setName(name.str().c_str());
                    IloExpr cover(env);
                    for (const int candidate : node.covering_candidate_compact_nodes) {
                        const int pos = y_pos[static_cast<std::size_t>(candidate)];
                        if (pos >= 0) {
                            cover += y[static_cast<IloInt>(pos)];
                        }
                    }
                    cover -= zeta;
                    model.add(cover >= 0.0);
                    cover.end();
                    lower_bound_rhs -= zeta;
                }
                if (!scenario.nodes.empty()) {
                    IloExpr lhs(env);
                    lhs += eta[static_cast<IloInt>(scenario.scenario_index)];
                    lhs -= lower_bound_rhs;
                    model.add(lhs >= 0.0);
                    lhs.end();
                }
                lower_bound_rhs.end();
            }
        }

        if (path.enabled) {
            for (const auto& scenario : path.scenarios) {
                IloExpr eta_lower_bound(env);
                for (const auto& node : scenario.nodes) {
                    IloNumVar burn_lb(env, 0.0, 1.0, ILOFLOAT);
                    std::ostringstream b_name;
                    b_name << "path_b_s" << scenario.scenario_id << "_" << node.compact_node;
                    burn_lb.setName(b_name.str().c_str());
                    eta_lower_bound += burn_lb;
                    for (const auto& path_record : node.paths) {
                        IloExpr expr(env);
                        expr += burn_lb;
                        for (const int candidate : path_record.blocking_candidate_compact_nodes) {
                            const int pos = y_pos[static_cast<std::size_t>(candidate)];
                            if (pos >= 0) {
                                expr += y[static_cast<IloInt>(pos)];
                            }
                        }
                        model.add(expr >= 1.0);
                        expr.end();
                    }
                }
                if (!scenario.nodes.empty()) {
                    IloExpr lhs(env);
                    lhs += eta[static_cast<IloInt>(scenario.scenario_index)];
                    lhs -= eta_lower_bound;
                    model.add(lhs >= 0.0);
                    lhs.end();
                }
                eta_lower_bound.end();
            }
        }

        benders::FppProjectedLlbiOptions projected_options;
        projected_options.use_projected_coverage_llbi_exp = use_projected_coverage_exp;
        projected_options.use_projected_path_llbi_exp = use_projected_path_exp;
        projected_options.use_projected_coverage_llbi_poly = use_projected_coverage_poly;
        projected_options.use_projected_path_llbi_poly = use_projected_path_poly;
        projected_options.root_rounds = options.projected_llbi_root_rounds;
        projected_options.max_cuts_per_round = options.projected_llbi_max_cuts_per_round;
        projected_options.violation_tolerance = options.projected_llbi_violation_tolerance;
        projected_options.cut_density_limit = options.projected_llbi_cut_density_limit;
        projected_options.poly_max_cuts = options.projected_poly_max_cuts;
        result.projected_coverage_llbi_enabled =
            use_projected_coverage_exp || use_projected_coverage_poly;
        result.projected_path_llbi_enabled =
            use_projected_path_exp || use_projected_path_poly;
        if (result.projected_coverage_llbi_enabled) {
            result.projected_llbi_family = "coverage";
        } else if (result.projected_path_llbi_enabled) {
            result.projected_llbi_family = "path";
        }
        if (use_projected_coverage_poly || use_projected_path_poly) {
            result.projected_llbi_strategy = "poly";
            result.projected_llbi_mode = result.projected_llbi_strategy;
            result.projected_llbi_root_rounds = 0;
            benders::FppProjectedLlbiStats projected_stats;
            const auto cuts = benders::build_fpp_projected_llbi_poly_cuts(
                opt,
                projected_options,
                &projected_stats);
            result.projected_poly_candidate_cuts_generated =
                projected_stats.projected_poly_candidate_cuts_generated;
            result.projected_poly_candidate_cuts_added =
                projected_stats.projected_poly_candidate_cuts_added;
            result.projected_poly_enumeration_truncated =
                projected_stats.projected_poly_enumeration_truncated;
            result.projected_poly_enumeration_limit =
                projected_stats.projected_poly_enumeration_limit;
            for (const auto& cut : cuts) {
                int scenario_index = -1;
                for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                    if (opt.scenarios[s].scenario_id == cut.scenario_id) {
                        scenario_index = static_cast<int>(s);
                        break;
                    }
                }
                if (scenario_index < 0) {
                    throw std::runtime_error(
                        "Projected LLBI diagnostic poly cut references an unknown scenario id.");
                }
                add_projected_cut(cut, scenario_index);
            }
        } else if (use_projected_coverage_exp || use_projected_path_exp) {
            result.projected_llbi_strategy = "exp";
            result.projected_llbi_mode = result.projected_llbi_strategy;
            result.projected_llbi_root_rounds = projected_options.root_rounds;
        }

        result.total_static_lb_precompute_time_sec =
            result.llbi_precompute_time_sec +
            result.coverage_llbi_precompute_time_sec +
            result.path_llbi_precompute_time_sec +
            result.projected_llbi_separation_time_sec;
        result.master_rows_after_static_lb =
            result.master_rows_before_static_lb +
            result.llbi_num_constraints +
            result.coverage_llbi_num_constraints +
            result.path_llbi_num_path_constraints +
            (path.enabled ? static_cast<long long>(path.scenarios.size()) : 0) +
            result.projected_llbi_cuts_added;
        result.master_cols_after_static_lb =
            result.master_cols_before_static_lb +
            result.coverage_llbi_num_zeta_vars +
            result.path_llbi_num_b_vars;
        result.master_nonzeros_after_static_lb =
            result.master_nonzeros_before_static_lb +
            result.llbi_num_nonzeros +
            result.coverage_llbi_num_nonzeros +
            result.path_llbi_num_nonzeros +
            result.projected_llbi_total_nonzeros;

        IloCplex cplex(model);
        cplex.setOut(env.getNullStream());
        cplex.setWarning(env.getNullStream());
        if (options.threads > 0) {
            cplex.setParam(IloCplex::Param::Threads, options.threads);
        }

        const auto start = std::chrono::steady_clock::now();
        bool solved = false;
        if (use_projected_coverage_exp || use_projected_path_exp) {
            const auto projected_start = std::chrono::steady_clock::now();
            for (int round = 0; round < projected_options.root_rounds; ++round) {
                ++result.projected_exp_separation_rounds;
                const auto lp_start = std::chrono::steady_clock::now();
                solved = cplex.solve();
                result.projected_llbi_solve_time_sec += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - lp_start).count();
                if (!solved) {
                    break;
                }
                if (!std::isfinite(result.projected_llbi_root_bound_initial)) {
                    result.projected_llbi_root_bound_initial = cplex.getObjValue();
                }
                result.projected_llbi_root_bound_final = cplex.getObjValue();
                std::vector<double> ybar(static_cast<std::size_t>(y.getSize()), 0.0);
                for (IloInt pos = 0; pos < y.getSize(); ++pos) {
                    ybar[static_cast<std::size_t>(pos)] =
                        std::max(0.0, std::min(1.0, cplex.getValue(y[pos])));
                }
                std::vector<double> eta_values(static_cast<std::size_t>(eta.getSize()), 0.0);
                for (IloInt s = 0; s < eta.getSize(); ++s) {
                    eta_values[static_cast<std::size_t>(s)] = cplex.getValue(eta[s]);
                }
                const auto separated = benders::separate_fpp_projected_llbi_cuts(
                    opt,
                    projected_options,
                    ybar,
                    eta_values);
                result.projected_llbi_separation_time_sec += separated.separation_time_sec;
                if (separated.cuts.empty()) {
                    break;
                }
                for (const auto& separated_cut : separated.cuts) {
                    add_projected_cut(separated_cut.cut, separated_cut.scenario_index);
                }
            }
            result.projected_exp_separated_cuts_added =
                result.projected_llbi_cuts_added;
            if (result.projected_llbi_cuts_added > 0) {
                const auto lp_start = std::chrono::steady_clock::now();
                solved = cplex.solve();
                result.projected_llbi_solve_time_sec += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - lp_start).count();
                if (solved) {
                    result.projected_llbi_root_bound_final = cplex.getObjValue();
                }
            }
            result.projected_llbi_total_time_sec = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - projected_start).count();
            if (std::isfinite(result.projected_llbi_root_bound_initial) &&
                std::isfinite(result.projected_llbi_root_bound_final)) {
                result.projected_llbi_root_bound_improvement_abs =
                    result.projected_llbi_root_bound_final -
                    result.projected_llbi_root_bound_initial;
                const double denom =
                    std::max(1.0, std::fabs(result.projected_llbi_root_bound_initial));
                result.projected_llbi_root_bound_improvement_pct =
                    100.0 * result.projected_llbi_root_bound_improvement_abs / denom;
            }
        } else {
            solved = cplex.solve();
        }
        if (result.projected_llbi_cuts_added > 0) {
            result.projected_llbi_avg_nonzeros_per_cut =
                static_cast<double>(result.projected_llbi_total_nonzeros) /
                static_cast<double>(result.projected_llbi_cuts_added);
        }
        const auto end = std::chrono::steady_clock::now();
        result.lp_runtime_sec = std::chrono::duration<double>(end - start).count();
        std::ostringstream status;
        status << cplex.getStatus();
        result.lp_status = solved ? status.str() : "No solution";
        if (solved) {
            result.lp_objective_value = cplex.getObjValue();
            std::vector<double> eta_values;
            eta_values.reserve(opt.scenarios.size());
            for (IloInt s = 0; s < eta.getSize(); ++s) {
                eta_values.push_back(cplex.getValue(eta[s]));
            }
            result.eta_sum_value = std::accumulate(eta_values.begin(), eta_values.end(), 0.0);
            result.min_eta_s = *std::min_element(eta_values.begin(), eta_values.end());
            result.max_eta_s = *std::max_element(eta_values.begin(), eta_values.end());
            result.avg_eta_s = result.eta_sum_value / static_cast<double>(eta_values.size());
        }
        result.num_variables = static_cast<long long>(cplex.getNcols());
        result.num_constraints = static_cast<long long>(cplex.getNrows());
        result.num_nonzeros = static_cast<long long>(cplex.getNNZs());
        result.master_rows_after_static_lb = result.num_constraints;
        result.master_cols_after_static_lb = result.num_variables;
        result.master_nonzeros_after_static_lb = result.num_nonzeros;
        (void)node_count;
        env.end();
        return result;
    } catch (...) {
        env.end();
        throw;
    }
}

#endif

std::vector<std::string> result_fields() {
    return {
        "experiment_id",
        "case_id",
        "seed_base",
        "seed",
        "landscape",
        "alpha",
        "train_count",
        "train_ids",
        "variant",
        "lp_status",
        "lp_objective_value",
        "lp_runtime_sec",
        "num_variables",
        "num_constraints",
        "num_nonzeros",
        "num_y_variables",
        "num_eta_variables",
        "num_x_variables",
        "llbi_num_constraints",
        "llbi_num_nonzeros",
        "coverage_llbi_num_zeta_vars",
        "coverage_llbi_num_constraints",
        "coverage_llbi_num_nonzeros",
        "path_llbi_num_b_vars",
        "path_llbi_num_path_constraints",
        "path_llbi_num_paths_used",
        "path_llbi_num_nonzeros",
        "projected_coverage_llbi_enabled",
        "projected_path_llbi_enabled",
        "projected_llbi_family",
        "projected_llbi_strategy",
        "projected_llbi_mode",
        "projected_llbi_root_rounds",
        "projected_llbi_cuts_added",
        "projected_llbi_coverage_cuts_added",
        "projected_llbi_path_cuts_added",
        "projected_llbi_separation_time_sec",
        "projected_llbi_solve_time_sec",
        "projected_llbi_total_time_sec",
        "projected_llbi_total_nonzeros",
        "projected_llbi_avg_nonzeros_per_cut",
        "projected_llbi_max_nonzeros_per_cut",
        "projected_llbi_root_bound_initial",
        "projected_llbi_root_bound_final",
        "projected_llbi_root_bound_improvement_abs",
        "projected_llbi_root_bound_improvement_pct",
        "projected_poly_candidate_cuts_generated",
        "projected_poly_candidate_cuts_added",
        "projected_poly_enumeration_truncated",
        "projected_poly_enumeration_limit",
        "projected_exp_separated_cuts_added",
        "projected_exp_separation_rounds",
        "projected_exp_candidate_cuts_generated",
        "projected_exp_candidate_cuts_added",
        "projected_exp_enumeration_truncated",
        "projected_exp_enumeration_limit",
        "llbi_precompute_time_sec",
        "coverage_llbi_precompute_time_sec",
        "path_llbi_precompute_time_sec",
        "total_static_lb_precompute_time_sec",
        "master_rows_before_static_lb",
        "master_cols_before_static_lb",
        "master_nonzeros_before_static_lb",
        "master_rows_after_static_lb",
        "master_cols_after_static_lb",
        "master_nonzeros_after_static_lb",
        "eta_sum_value",
        "min_eta_s",
        "avg_eta_s",
        "max_eta_s",
        "warning",
    };
}

std::vector<std::string> result_values(const DiagnosticResult& r) {
    return {
        r.experiment_id,
        r.case_id,
        std::to_string(r.seed_base),
        std::to_string(r.seed),
        r.landscape,
        format_double(r.alpha),
        std::to_string(r.train_count),
        join_ints(r.train_ids),
        r.variant,
        r.lp_status,
        format_double(r.lp_objective_value),
        format_double(r.lp_runtime_sec),
        std::to_string(r.num_variables),
        std::to_string(r.num_constraints),
        std::to_string(r.num_nonzeros),
        std::to_string(r.num_y_variables),
        std::to_string(r.num_eta_variables),
        std::to_string(r.num_x_variables),
        std::to_string(r.llbi_num_constraints),
        std::to_string(r.llbi_num_nonzeros),
        std::to_string(r.coverage_llbi_num_zeta_vars),
        std::to_string(r.coverage_llbi_num_constraints),
        std::to_string(r.coverage_llbi_num_nonzeros),
        std::to_string(r.path_llbi_num_b_vars),
        std::to_string(r.path_llbi_num_path_constraints),
        std::to_string(r.path_llbi_num_paths_used),
        std::to_string(r.path_llbi_num_nonzeros),
        r.projected_coverage_llbi_enabled ? "true" : "false",
        r.projected_path_llbi_enabled ? "true" : "false",
        r.projected_llbi_family,
        r.projected_llbi_strategy,
        r.projected_llbi_mode,
        std::to_string(r.projected_llbi_root_rounds),
        std::to_string(r.projected_llbi_cuts_added),
        std::to_string(r.projected_llbi_coverage_cuts_added),
        std::to_string(r.projected_llbi_path_cuts_added),
        format_double(r.projected_llbi_separation_time_sec),
        format_double(r.projected_llbi_solve_time_sec),
        format_double(r.projected_llbi_total_time_sec),
        std::to_string(r.projected_llbi_total_nonzeros),
        format_double(r.projected_llbi_avg_nonzeros_per_cut),
        std::to_string(r.projected_llbi_max_nonzeros_per_cut),
        format_double(r.projected_llbi_root_bound_initial),
        format_double(r.projected_llbi_root_bound_final),
        format_double(r.projected_llbi_root_bound_improvement_abs),
        format_double(r.projected_llbi_root_bound_improvement_pct),
        std::to_string(r.projected_poly_candidate_cuts_generated),
        std::to_string(r.projected_poly_candidate_cuts_added),
        r.projected_poly_enumeration_truncated ? "true" : "false",
        std::to_string(r.projected_poly_enumeration_limit),
        std::to_string(r.projected_exp_separated_cuts_added),
        std::to_string(r.projected_exp_separation_rounds),
        std::to_string(r.projected_exp_candidate_cuts_generated),
        std::to_string(r.projected_exp_candidate_cuts_added),
        r.projected_exp_enumeration_truncated ? "true" : "false",
        std::to_string(r.projected_exp_enumeration_limit),
        format_double(r.llbi_precompute_time_sec),
        format_double(r.coverage_llbi_precompute_time_sec),
        format_double(r.path_llbi_precompute_time_sec),
        format_double(r.total_static_lb_precompute_time_sec),
        std::to_string(r.master_rows_before_static_lb),
        std::to_string(r.master_cols_before_static_lb),
        std::to_string(r.master_nonzeros_before_static_lb),
        std::to_string(r.master_rows_after_static_lb),
        std::to_string(r.master_cols_after_static_lb),
        std::to_string(r.master_nonzeros_after_static_lb),
        format_double(r.eta_sum_value),
        format_double(r.min_eta_s),
        format_double(r.avg_eta_s),
        format_double(r.max_eta_s),
        r.warning,
    };
}

void write_json(const std::filesystem::path& path, const DiagnosticResult& r) {
    firebreak::io::ensure_parent_directory(path);
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Could not open diagnostic JSON output: " + path.string());
    }
    const auto fields = result_fields();
    const auto values = result_values(r);
    out << "{\n";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        out << "  \"" << fields[i] << "\": ";
        const bool numeric =
            fields[i] != "experiment_id" &&
            fields[i] != "case_id" &&
            fields[i] != "landscape" &&
            fields[i] != "train_ids" &&
            fields[i] != "variant" &&
            fields[i] != "projected_llbi_family" &&
            fields[i] != "projected_llbi_strategy" &&
            fields[i] != "projected_llbi_mode" &&
            fields[i] != "projected_poly_enumeration_truncated" &&
            fields[i] != "projected_exp_enumeration_truncated" &&
            fields[i] != "lp_status" &&
            fields[i] != "warning";
        if (fields[i] == "train_ids") {
            out << "[";
            for (std::size_t j = 0; j < r.train_ids.size(); ++j) {
                if (j > 0) {
                    out << ", ";
                }
                out << r.train_ids[j];
            }
            out << "]";
        } else if (numeric) {
            out << values[i];
        } else {
            out << "\"" << json_escape(values[i]) << "\"";
        }
        out << (i + 1 == fields.size() ? "\n" : ",\n");
    }
    out << "}\n";
}

void append_csv(const std::filesystem::path& path, const DiagnosticResult& r) {
    firebreak::io::ensure_parent_directory(path);
    const bool write_header = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
    std::ofstream out(path, std::ios::app);
    if (!out) {
        throw std::runtime_error("Could not open diagnostic CSV output: " + path.string());
    }
    const auto fields = result_fields();
    const auto values = result_values(r);
    if (write_header) {
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i > 0) {
                out << ",";
            }
            out << fields[i];
        }
        out << "\n";
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << csv_escape(values[i]);
    }
    out << "\n";
}

}  // namespace

int FppMasterLpDiagnosticRunner::run(const FppMasterLpDiagnosticOptions& options) const {
    if (options.landscape.empty()) {
        throw std::runtime_error("--landscape is required.");
    }
    if (options.alpha < 0.0) {
        throw std::runtime_error("--alpha is required and must be nonnegative.");
    }
    if (options.train_ids.empty()) {
        throw std::runtime_error("--train-ids is required.");
    }
    static const std::vector<std::string> supported = {
        "fpp_saa_lp_base",
        "master_lp_none",
        "master_lp_llbi",
        "master_lp_coverage_llbi",
        "master_lp_path_llbi",
        "master_lp_projected_coverage_poly",
        "master_lp_projected_path_poly",
        "master_lp_projected_coverage_exp",
        "master_lp_projected_path_exp",
    };
    if (std::find(supported.begin(), supported.end(), options.variant) == supported.end()) {
        throw std::runtime_error("Unsupported --variant for diagnose-fpp-master-lp: " + options.variant);
    }
#ifndef FIREBREAK_WITH_CPLEX
    throw std::runtime_error(solver::cplex_unavailable_message());
#else
    const auto forest_path = options.forest_path.empty()
        ? default_forest_path(options.landscape)
        : firebreak::io::resolve_input_path(options.forest_path.string());
    const auto results_path = options.results_path.empty()
        ? default_results_path(options.landscape)
        : firebreak::io::resolve_input_path(options.results_path.string());
    const auto output_json = options.output_json_path.empty()
        ? firebreak::io::resolve_output_path("results/diagnostics/fpp_master_lp.json")
        : firebreak::io::resolve_output_path(options.output_json_path.string());
    const auto output_csv = options.output_csv_path.empty()
        ? firebreak::io::resolve_output_path("results/diagnostics/fpp_master_lp.csv")
        : firebreak::io::resolve_output_path(options.output_csv_path.string());

    const auto inventory = firebreak::io::detect_message_files(results_path);
    firebreak::io::validate_scenario_ids(inventory, options.train_ids);
    firebreak::io::Cell2FireReader reader;
    std::vector<std::string> warnings;
    const auto instance = reader.load_instance(
        options.landscape,
        forest_path,
        results_path,
        options.train_ids,
        warnings);
    opt::OptimizationInstanceBuilder builder;
    const auto opt = builder.build(instance, options.alpha, false);

    DiagnosticResult result = options.variant == "fpp_saa_lp_base"
        ? solve_fpp_saa_lp_base(opt, options)
        : solve_master_lp(opt, options);
    result.experiment_id = options.experiment_id;
    result.case_id = options.case_id;
    result.seed_base = options.seed_base;
    result.seed = options.seed;
    result.landscape = options.landscape;
    result.alpha = options.alpha;
    result.train_count = static_cast<int>(options.train_ids.size());
    result.train_ids = options.train_ids;
    result.variant = options.variant;
    if (!warnings.empty()) {
        result.warning = warnings.front();
    }
    validate_result(result);
    write_json(output_json, result);
    append_csv(output_csv, result);

    std::cout << "FPP master LP diagnostic completed: variant=" << options.variant
              << " objective=" << result.lp_objective_value
              << " status=" << result.lp_status
              << " json=" << firebreak::io::path_to_string(output_json)
              << "\n";
    return 0;
#endif
}

}  // namespace firebreak::experiments
