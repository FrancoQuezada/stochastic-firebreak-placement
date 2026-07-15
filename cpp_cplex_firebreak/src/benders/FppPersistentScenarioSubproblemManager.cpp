#include "benders/FppPersistentScenarioSubproblemManager.hpp"

#include <chrono>
#include <cmath>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "solver/CplexEnvironment.hpp"
#include "solver/FppWeightedLossUtils.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::benders {

namespace {

void validate_scenario_position(const opt::OptimizationInstance& opt, int scenario_position) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("Persistent FPP subproblem manager requires at least one mapped node.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("Persistent FPP subproblem manager requires at least one eligible firebreak node.");
    }
    if (scenario_position < 0 || scenario_position >= static_cast<int>(opt.scenarios.size())) {
        throw std::runtime_error("Persistent FPP subproblem scenario position is out of range.");
    }
    if (!opt.compact_cell_weights.empty()) {
        (void)solver::direct_fpp_compact_weights(opt);
    }
}

void validate_ybar(const opt::OptimizationInstance& opt, const std::vector<int>& ybar_binary) {
    if (ybar_binary.size() != opt.eligible_indices.size()) {
        throw std::runtime_error("Persistent FPP subproblem ybar length must equal the eligible-node count.");
    }
    for (const int value : ybar_binary) {
        if (value != 0 && value != 1) {
            throw std::runtime_error("Persistent FPP subproblem ybar values must be binary.");
        }
    }
}

void validate_fractional_ybar(const opt::OptimizationInstance& opt, const std::vector<double>& ybar_values) {
    if (ybar_values.size() != opt.eligible_indices.size()) {
        throw std::runtime_error("Persistent FPP subproblem fractional ybar length must equal the eligible-node count.");
    }
    for (const double value : ybar_values) {
        if (value < -1.0e-9 || value > 1.0 + 1.0e-9) {
            throw std::runtime_error("Persistent FPP subproblem fractional ybar values must be in [0, 1].");
        }
    }
}

#ifdef FIREBREAK_WITH_CPLEX

std::unordered_map<int, int> build_y_position_by_node_index(const opt::OptimizationInstance& opt) {
    std::unordered_map<int, int> y_position_by_node;
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        y_position_by_node[opt.eligible_indices[pos]] = static_cast<int>(pos);
    }
    return y_position_by_node;
}

double elapsed_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

FppPersistentScenarioSubproblemDiagnostics finalize_averages(
    FppPersistentScenarioSubproblemDiagnostics diagnostics) {
    if (diagnostics.subproblem_fixed_y_update_count > 0) {
        diagnostics.subproblem_average_update_time =
            diagnostics.subproblem_total_update_time /
            static_cast<double>(diagnostics.subproblem_fixed_y_update_count);
    }
    if (diagnostics.subproblem_solve_count > 0) {
        diagnostics.subproblem_average_solve_time =
            diagnostics.subproblem_total_solve_time /
            static_cast<double>(diagnostics.subproblem_solve_count);
    }
    return diagnostics;
}

#endif

}  // namespace

#ifndef FIREBREAK_WITH_CPLEX

class FppPersistentScenarioSubproblemManager::Impl {
public:
    Impl(const opt::OptimizationInstance& opt, bool)
        : opt_(opt) {
        diagnostics_.scenario_count = static_cast<int>(opt_.scenarios.size());
        diagnostics_.persistent_subproblems_enabled = false;
        diagnostics_.weight_map_hash = opt_.cell_weight_map.deterministic_hash;
    }

    FppSubproblemResult solveScenario(int scenario_position, const std::vector<int>& ybar_binary) {
        validate_scenario_position(opt_, scenario_position);
        validate_ybar(opt_, ybar_binary);
        throw std::runtime_error(solver::cplex_unavailable_message());
    }

    FppSubproblemResult solveScenarioFractional(
        int scenario_position,
        const std::vector<double>& ybar_values) {
        validate_scenario_position(opt_, scenario_position);
        validate_fractional_ybar(opt_, ybar_values);
        throw std::runtime_error(solver::cplex_unavailable_message());
    }

    FppPersistentScenarioSubproblemDiagnostics diagnostics() const {
        return diagnostics_;
    }

private:
    const opt::OptimizationInstance& opt_;
    FppPersistentScenarioSubproblemDiagnostics diagnostics_;
};

#else

class PersistentScenarioSubproblem {
public:
    PersistentScenarioSubproblem(
        const opt::OptimizationInstance& opt,
        int scenario_position,
        bool verbose)
        : opt_(opt),
          scenario_position_(scenario_position),
          scenario_(opt.scenarios[static_cast<std::size_t>(scenario_position)]),
          y_position_by_node_(build_y_position_by_node_index(opt)),
          compact_weights_(solver::direct_fpp_compact_weights_or_unit(opt)),
          structure_(analyze_fpp_scenario_subproblem_structure(opt, scenario_position)),
          env_(),
          model_(env_),
          x_(env_),
          y_copy_(env_),
          y_fix_(env_) {
        const auto build_start = std::chrono::steady_clock::now();
        buildModel(verbose);
        build_time_seconds_ = elapsed_since(build_start);
    }

    ~PersistentScenarioSubproblem() {
        cplex_.reset();
        env_.end();
    }

    PersistentScenarioSubproblem(const PersistentScenarioSubproblem&) = delete;
    PersistentScenarioSubproblem& operator=(const PersistentScenarioSubproblem&) = delete;

    double buildTimeSeconds() const {
        return build_time_seconds_;
    }

    FppSubproblemResult solve(const std::vector<int>& ybar_binary, double& update_time_seconds) {
        validate_ybar(opt_, ybar_binary);
        std::vector<double> ybar_values;
        ybar_values.reserve(ybar_binary.size());
        for (const int value : ybar_binary) {
            ybar_values.push_back(static_cast<double>(value));
        }
        return solveWithValues(ybar_values, update_time_seconds);
    }

    FppSubproblemResult solveFractional(
        const std::vector<double>& ybar_values,
        double& update_time_seconds) {
        validate_fractional_ybar(opt_, ybar_values);
        return solveWithValues(ybar_values, update_time_seconds);
    }

private:
    FppSubproblemResult solveWithValues(
        const std::vector<double>& ybar_values,
        double& update_time_seconds) {
        const auto update_start = std::chrono::steady_clock::now();
        for (std::size_t pos = 0; pos < ybar_values.size(); ++pos) {
            const double value = ybar_values[pos];
            y_fix_[static_cast<IloInt>(pos)].setBounds(value, value);
        }
        update_time_seconds = elapsed_since(update_start);

        FppSubproblemResult result;
        result.scenario_id = scenario_.scenario_id;
        result.num_variables = structure_.total_variable_count;
        result.num_constraints = structure_.total_constraint_count;

        const auto solve_start = std::chrono::steady_clock::now();
        cplex_->setParam(IloCplex::Param::Advance, 0);
        const bool solved = cplex_->solve();
        result.runtime_seconds = elapsed_since(solve_start);

        std::ostringstream status;
        status << cplex_->getStatus();
        result.status = solved ? status.str() : "No feasible LP solution";
        if (!solved) {
            throw std::runtime_error("Persistent FPP Benders subproblem did not solve to a feasible LP solution.");
        }

        result.objective_value = cplex_->getObjValue();
        result.duals_for_y_copy.reserve(opt_.eligible_indices.size());
        for (IloInt pos = 0; pos < y_fix_.getSize(); ++pos) {
            result.duals_for_y_copy.push_back(cplex_->getDual(y_fix_[pos]));
        }

        BendersCut cut;
        cut.scenario_id = scenario_.scenario_id;
        cut.subproblem_objective = result.objective_value;
        cut.coefficients_by_compact_index.reserve(opt_.eligible_indices.size());
        cut.ybar_compact_values.reserve(opt_.eligible_indices.size());
        double dual_dot_ybar = 0.0;
        for (std::size_t pos = 0; pos < opt_.eligible_indices.size(); ++pos) {
            const double dual = result.duals_for_y_copy[pos];
            const int compact_index = opt_.eligible_indices[pos];
            const double ybar_value = ybar_values[pos];
            cut.coefficients_by_compact_index.push_back({compact_index, dual});
            cut.ybar_compact_values.push_back({compact_index, ybar_value});
            dual_dot_ybar += dual * ybar_value;
        }
        cut.rhs_constant = result.objective_value - dual_dot_ybar;
        cut.notes.push_back(
            "Persistent FPP subproblem updates equality row bounds y_copy_i = ybar_i before re-solving.");
        cut.notes.push_back(
            "CPLEX getDual values from equality rows y_copy_i = ybar_i are used directly as FPP Benders coefficients.");
        cut.notes.push_back(
            "The resulting cut is eta_s >= Q_s(ybar) + sum_i pi_i * (y_i - ybar_i).");
        cut.notes.push_back(
            "Q_s(ybar) is expressed in weighted burned-node loss units.");
        result.benders_cut = cut;
        result.notes = cut.notes;
        return result;
    }

    void buildModel(bool verbose) {
        const int node_count = opt_.node_mapper.size();

        x_ = IloNumVarArray(env_, node_count, 0.0, 1.0, ILOFLOAT);
        for (int i = 0; i < node_count; ++i) {
            std::ostringstream name;
            name << "x_s" << scenario_.scenario_id << "_" << i;
            x_[i].setName(name.str().c_str());
        }

        y_copy_ = IloNumVarArray(
            env_,
            static_cast<IloInt>(opt_.eligible_indices.size()),
            0.0,
            1.0,
            ILOFLOAT);
        for (std::size_t pos = 0; pos < opt_.eligible_indices.size(); ++pos) {
            std::ostringstream name;
            name << "ycopy_s" << scenario_.scenario_id << "_" << opt_.eligible_indices[pos];
            y_copy_[static_cast<IloInt>(pos)].setName(name.str().c_str());
        }

        for (std::size_t pos = 0; pos < opt_.eligible_indices.size(); ++pos) {
            IloRange fix = (y_copy_[static_cast<IloInt>(pos)] == 0.0);
            std::ostringstream name;
            name << "fix_ycopy_s" << scenario_.scenario_id << "_" << opt_.eligible_indices[pos];
            fix.setName(name.str().c_str());
            y_fix_.add(fix);
            model_.add(fix);
        }

        IloExpr objective(env_);
        for (int i = 0; i < node_count; ++i) {
            objective += compact_weights_[static_cast<std::size_t>(i)] * x_[i];
        }
        model_.add(IloMinimize(env_, objective));
        objective.end();

        model_.add(x_[scenario_.ignition_index] == 1.0);

        for (const auto& arc : scenario_.arcs) {
            IloExpr expr(env_);
            expr += x_[arc.u_index];
            expr -= x_[arc.v_index];
            const auto y_it = y_position_by_node_.find(arc.v_index);
            if (y_it != y_position_by_node_.end()) {
                expr -= y_copy_[static_cast<IloInt>(y_it->second)];
            }
            model_.add(expr <= 0.0);
            expr.end();
        }

        cplex_ = std::make_unique<IloCplex>(model_);
        if (!verbose) {
            cplex_->setOut(env_.getNullStream());
            cplex_->setWarning(env_.getNullStream());
        }
    }

    const opt::OptimizationInstance& opt_;
    int scenario_position_ = -1;
    const opt::OptimizationScenario& scenario_;
    std::unordered_map<int, int> y_position_by_node_;
    std::vector<double> compact_weights_;
    FppScenarioSubproblemStructure structure_;
    IloEnv env_;
    IloModel model_;
    IloNumVarArray x_;
    IloNumVarArray y_copy_;
    IloRangeArray y_fix_;
    std::unique_ptr<IloCplex> cplex_;
    double build_time_seconds_ = 0.0;
};

class FppPersistentScenarioSubproblemManager::Impl {
public:
    Impl(const opt::OptimizationInstance& opt, bool verbose)
        : opt_(opt),
          verbose_(verbose) {
        diagnostics_.scenario_count = static_cast<int>(opt_.scenarios.size());
        diagnostics_.persistent_subproblems_enabled = true;
        diagnostics_.weight_map_hash = opt_.cell_weight_map.deterministic_hash;
        subproblems_.reserve(opt_.scenarios.size());
        for (std::size_t s = 0; s < opt_.scenarios.size(); ++s) {
            validate_scenario_position(opt_, static_cast<int>(s));
            auto subproblem = std::make_unique<PersistentScenarioSubproblem>(
                opt_,
                static_cast<int>(s),
                verbose_);
            diagnostics_.subproblem_total_build_time += subproblem->buildTimeSeconds();
            subproblems_.push_back(std::move(subproblem));
            ++diagnostics_.subproblem_model_build_count;
        }
    }

    FppSubproblemResult solveScenario(int scenario_position, const std::vector<int>& ybar_binary) {
        validate_scenario_position(opt_, scenario_position);
        std::lock_guard<std::mutex> lock(mutex_);
        double update_time = 0.0;
        auto result =
            subproblems_[static_cast<std::size_t>(scenario_position)]->solve(ybar_binary, update_time);
        diagnostics_.subproblem_total_update_time += update_time;
        diagnostics_.subproblem_total_solve_time += result.runtime_seconds;
        ++diagnostics_.subproblem_fixed_y_update_count;
        ++diagnostics_.subproblem_solve_count;
        return result;
    }

    FppSubproblemResult solveScenarioFractional(
        int scenario_position,
        const std::vector<double>& ybar_values) {
        validate_scenario_position(opt_, scenario_position);
        std::lock_guard<std::mutex> lock(mutex_);
        double update_time = 0.0;
        auto result =
            subproblems_[static_cast<std::size_t>(scenario_position)]->solveFractional(ybar_values, update_time);
        diagnostics_.subproblem_total_update_time += update_time;
        diagnostics_.subproblem_total_solve_time += result.runtime_seconds;
        ++diagnostics_.subproblem_fixed_y_update_count;
        ++diagnostics_.subproblem_solve_count;
        return result;
    }

    FppPersistentScenarioSubproblemDiagnostics diagnostics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return finalize_averages(diagnostics_);
    }

private:
    const opt::OptimizationInstance& opt_;
    bool verbose_ = false;
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<PersistentScenarioSubproblem>> subproblems_;
    FppPersistentScenarioSubproblemDiagnostics diagnostics_;
};

#endif

FppPersistentScenarioSubproblemManager::FppPersistentScenarioSubproblemManager(
    const opt::OptimizationInstance& opt,
    bool verbose)
    : impl_(std::make_unique<Impl>(opt, verbose)) {}

FppPersistentScenarioSubproblemManager::~FppPersistentScenarioSubproblemManager() = default;

FppPersistentScenarioSubproblemManager::FppPersistentScenarioSubproblemManager(
    FppPersistentScenarioSubproblemManager&&) noexcept = default;

FppPersistentScenarioSubproblemManager& FppPersistentScenarioSubproblemManager::operator=(
    FppPersistentScenarioSubproblemManager&&) noexcept = default;

FppSubproblemResult FppPersistentScenarioSubproblemManager::solveScenario(
    int scenario_position,
    const std::vector<int>& ybar_binary) {
    return impl_->solveScenario(scenario_position, ybar_binary);
}

FppSubproblemResult FppPersistentScenarioSubproblemManager::solveScenarioFractional(
    int scenario_position,
    const std::vector<double>& ybar_values) {
    return impl_->solveScenarioFractional(scenario_position, ybar_values);
}

FppPersistentScenarioSubproblemDiagnostics FppPersistentScenarioSubproblemManager::diagnostics() const {
    return impl_->diagnostics();
}

}  // namespace firebreak::benders
