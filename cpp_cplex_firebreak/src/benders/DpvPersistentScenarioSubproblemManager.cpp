#include "benders/DpvPersistentScenarioSubproblemManager.hpp"

#include <chrono>
#include <cmath>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "solver/CplexEnvironment.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::benders {

namespace {

void validate_scenario_position(const opt::OptimizationInstance& opt, int scenario_position) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("Persistent DPV subproblem manager requires at least one mapped node.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("Persistent DPV subproblem manager requires at least one eligible firebreak node.");
    }
    if (scenario_position < 0 || scenario_position >= static_cast<int>(opt.scenarios.size())) {
        throw std::runtime_error("Persistent DPV subproblem scenario position is out of range.");
    }
}

void validate_ybar(const opt::OptimizationInstance& opt, const std::vector<int>& ybar_binary) {
    if (ybar_binary.size() != opt.eligible_indices.size()) {
        throw std::runtime_error("Persistent DPV subproblem ybar length must equal the eligible-node count.");
    }
    for (const int value : ybar_binary) {
        if (value != 0 && value != 1) {
            throw std::runtime_error("Persistent DPV subproblem ybar values must be binary.");
        }
    }
}

void validate_fractional_ybar(const opt::OptimizationInstance& opt, const std::vector<double>& ybar_values) {
    if (ybar_values.size() != opt.eligible_indices.size()) {
        throw std::runtime_error("Persistent DPV subproblem fractional ybar length must equal the eligible-node count.");
    }
    for (const double value : ybar_values) {
        if (value < -1.0e-9 || value > 1.0 + 1.0e-9) {
            throw std::runtime_error("Persistent DPV subproblem fractional ybar values must be in [0, 1].");
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

DpvPersistentScenarioSubproblemDiagnostics finalize_averages(
    DpvPersistentScenarioSubproblemDiagnostics diagnostics) {
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

class DpvPersistentScenarioSubproblemManager::Impl {
public:
    Impl(const opt::OptimizationInstance& opt, bool)
        : opt_(opt) {
        diagnostics_.scenario_count = static_cast<int>(opt_.scenarios.size());
        diagnostics_.persistent_subproblems_enabled = false;
    }

    SubproblemResult solveScenario(int scenario_position, const std::vector<int>& ybar_binary) {
        validate_scenario_position(opt_, scenario_position);
        validate_ybar(opt_, ybar_binary);
        throw std::runtime_error(solver::cplex_unavailable_message());
    }

    SubproblemResult solveScenarioFractional(
        int scenario_position,
        const std::vector<double>& ybar_values) {
        validate_scenario_position(opt_, scenario_position);
        validate_fractional_ybar(opt_, ybar_values);
        throw std::runtime_error(solver::cplex_unavailable_message());
    }

    DpvPersistentScenarioSubproblemDiagnostics diagnostics() const {
        return diagnostics_;
    }

private:
    const opt::OptimizationInstance& opt_;
    DpvPersistentScenarioSubproblemDiagnostics diagnostics_;
};

#else

class DpvPersistentScenarioSubproblem {
public:
    DpvPersistentScenarioSubproblem(
        const opt::OptimizationInstance& opt,
        int scenario_position,
        bool verbose)
        : opt_(opt),
          scenario_(opt.scenarios[static_cast<std::size_t>(scenario_position)]),
          y_position_by_node_(build_y_position_by_node_index(opt)),
          structure_(analyze_dpv_scenario_subproblem_structure(opt, scenario_position)),
          env_(),
          model_(env_),
          x_(env_),
          z_(env_),
          y_copy_(env_),
          y_fix_(env_) {
        const auto build_start = std::chrono::steady_clock::now();
        buildModel(verbose);
        build_time_seconds_ = elapsed_since(build_start);
    }

    ~DpvPersistentScenarioSubproblem() {
        cplex_.reset();
        env_.end();
    }

    DpvPersistentScenarioSubproblem(const DpvPersistentScenarioSubproblem&) = delete;
    DpvPersistentScenarioSubproblem& operator=(const DpvPersistentScenarioSubproblem&) = delete;

    double buildTimeSeconds() const {
        return build_time_seconds_;
    }

    SubproblemResult solve(const std::vector<int>& ybar_binary, double& update_time_seconds) {
        validate_ybar(opt_, ybar_binary);
        std::vector<double> ybar_values;
        ybar_values.reserve(ybar_binary.size());
        for (const int value : ybar_binary) {
            ybar_values.push_back(static_cast<double>(value));
        }
        return solveWithValues(ybar_values, update_time_seconds);
    }

    SubproblemResult solveFractional(
        const std::vector<double>& ybar_values,
        double& update_time_seconds) {
        validate_fractional_ybar(opt_, ybar_values);
        return solveWithValues(ybar_values, update_time_seconds);
    }

private:
    SubproblemResult solveWithValues(
        const std::vector<double>& ybar_values,
        double& update_time_seconds) {
        const auto update_start = std::chrono::steady_clock::now();
        for (std::size_t pos = 0; pos < ybar_values.size(); ++pos) {
            const double value = ybar_values[pos];
            y_fix_[static_cast<IloInt>(pos)].setBounds(value, value);
        }
        update_time_seconds = elapsed_since(update_start);

        SubproblemResult result;
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
            throw std::runtime_error("Persistent DPV Benders subproblem did not solve to a feasible LP solution.");
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
            "Persistent DPV subproblem updates equality row bounds y_copy_i = ybar_i before re-solving.");
        cut.notes.push_back(
            "CPLEX getDual values from equality rows y_copy_i = ybar_i are used directly as Benders coefficients.");
        cut.notes.push_back(
            "The resulting cut is eta_s >= Q_s(ybar) + sum_i pi_i * (y_i - ybar_i).");
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

        const auto z_count = static_cast<IloInt>(scenario_.dpv.product_pairs.size());
        z_ = IloNumVarArray(env_, z_count, 0.0, 1.0, ILOFLOAT);
        for (IloInt p = 0; p < z_count; ++p) {
            std::ostringstream name;
            name << "z_s" << scenario_.scenario_id << "_" << p;
            z_[p].setName(name.str().c_str());
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
        for (IloInt p = 0; p < z_.getSize(); ++p) {
            objective += z_[p];
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

        for (std::size_t p = 0; p < scenario_.dpv.product_pairs.size(); ++p) {
            const auto& pair = scenario_.dpv.product_pairs[p];
            const IloNumVar z_var = z_[static_cast<IloInt>(p)];
            const IloNumVar x_successor = x_[pair.successor_index];
            const IloNumVar x_descendant = x_[pair.descendant_index];

            IloExpr lower(env_);
            lower += x_successor;
            lower += x_descendant;
            lower -= 1.0;
            model_.add(z_var >= lower);
            lower.end();

            model_.add(z_var <= x_successor);
            model_.add(z_var <= x_descendant);
        }

        cplex_ = std::make_unique<IloCplex>(model_);
        if (!verbose) {
            cplex_->setOut(env_.getNullStream());
            cplex_->setWarning(env_.getNullStream());
        }
    }

    const opt::OptimizationInstance& opt_;
    const opt::OptimizationScenario& scenario_;
    std::unordered_map<int, int> y_position_by_node_;
    DpvScenarioSubproblemStructure structure_;
    IloEnv env_;
    IloModel model_;
    IloNumVarArray x_;
    IloNumVarArray z_;
    IloNumVarArray y_copy_;
    IloRangeArray y_fix_;
    std::unique_ptr<IloCplex> cplex_;
    double build_time_seconds_ = 0.0;
};

class DpvPersistentScenarioSubproblemManager::Impl {
public:
    Impl(const opt::OptimizationInstance& opt, bool verbose)
        : opt_(opt) {
        diagnostics_.scenario_count = static_cast<int>(opt_.scenarios.size());
        diagnostics_.persistent_subproblems_enabled = true;
        subproblems_.reserve(opt_.scenarios.size());
        for (std::size_t s = 0; s < opt_.scenarios.size(); ++s) {
            validate_scenario_position(opt_, static_cast<int>(s));
            auto subproblem = std::make_unique<DpvPersistentScenarioSubproblem>(
                opt_,
                static_cast<int>(s),
                verbose);
            diagnostics_.subproblem_total_build_time += subproblem->buildTimeSeconds();
            subproblems_.push_back(std::move(subproblem));
            ++diagnostics_.subproblem_model_build_count;
        }
    }

    SubproblemResult solveScenario(int scenario_position, const std::vector<int>& ybar_binary) {
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

    SubproblemResult solveScenarioFractional(
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

    DpvPersistentScenarioSubproblemDiagnostics diagnostics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return finalize_averages(diagnostics_);
    }

private:
    const opt::OptimizationInstance& opt_;
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<DpvPersistentScenarioSubproblem>> subproblems_;
    DpvPersistentScenarioSubproblemDiagnostics diagnostics_;
};

#endif

DpvPersistentScenarioSubproblemManager::DpvPersistentScenarioSubproblemManager(
    const opt::OptimizationInstance& opt,
    bool verbose)
    : impl_(std::make_unique<Impl>(opt, verbose)) {}

DpvPersistentScenarioSubproblemManager::~DpvPersistentScenarioSubproblemManager() = default;

DpvPersistentScenarioSubproblemManager::DpvPersistentScenarioSubproblemManager(
    DpvPersistentScenarioSubproblemManager&&) noexcept = default;

DpvPersistentScenarioSubproblemManager& DpvPersistentScenarioSubproblemManager::operator=(
    DpvPersistentScenarioSubproblemManager&&) noexcept = default;

SubproblemResult DpvPersistentScenarioSubproblemManager::solveScenario(
    int scenario_position,
    const std::vector<int>& ybar_binary) {
    return impl_->solveScenario(scenario_position, ybar_binary);
}

SubproblemResult DpvPersistentScenarioSubproblemManager::solveScenarioFractional(
    int scenario_position,
    const std::vector<double>& ybar_values) {
    return impl_->solveScenarioFractional(scenario_position, ybar_values);
}

DpvPersistentScenarioSubproblemDiagnostics DpvPersistentScenarioSubproblemManager::diagnostics() const {
    return impl_->diagnostics();
}

}  // namespace firebreak::benders
