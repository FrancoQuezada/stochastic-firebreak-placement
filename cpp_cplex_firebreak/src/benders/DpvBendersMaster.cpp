#include "benders/DpvBendersMaster.hpp"

#include <chrono>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "solver/CplexEnvironment.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::benders {

namespace {

void validate_master_instance(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("DPV Benders master requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("DPV Benders master requires at least one scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("DPV Benders master requires at least one eligible firebreak node.");
    }
    if (opt.budget < 0) {
        throw std::runtime_error("DPV Benders master budget must be nonnegative.");
    }
    if (opt.budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error("DPV Benders master budget exceeds eligible-node count.");
    }
}

}  // namespace

#ifndef FIREBREAK_WITH_CPLEX

struct DpvBendersMaster::Impl {};

DpvBendersMaster::DpvBendersMaster() : impl_(std::make_unique<Impl>()) {}
DpvBendersMaster::~DpvBendersMaster() = default;

void DpvBendersMaster::initialize(const opt::OptimizationInstance& opt) {
    validate_master_instance(opt);
    throw std::runtime_error(solver::cplex_unavailable_message());
}

void DpvBendersMaster::setParameters(double, double, int, bool) {}

void DpvBendersMaster::setWarmStart(const std::vector<int>&) {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

MasterSolveResult DpvBendersMaster::solve() {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

std::vector<int> DpvBendersMaster::getCurrentY() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

std::vector<double> DpvBendersMaster::getEtaValues() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

void DpvBendersMaster::addCut(int, const BendersCut&) {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

double DpvBendersMaster::getObjective() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

double DpvBendersMaster::getBestBound() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

double DpvBendersMaster::getMipGap() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

double DpvBendersMaster::getRuntime() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

std::vector<int> DpvBendersMaster::getSelectedFirebreaks() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

std::vector<int> DpvBendersMaster::getSelectedFirebreakOriginalNodes() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

std::size_t DpvBendersMaster::getNumVariables() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

std::size_t DpvBendersMaster::getNumConstraints() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

int DpvBendersMaster::getCutCount() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

void DpvBendersMaster::addLiftedLowerBound(
    int,
    const DpvLiftedLowerBoundInequality&) {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

int DpvBendersMaster::getLiftedLowerBoundCount() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

#else

struct DpvBendersMaster::Impl {
    IloEnv env;
    IloModel model;
    IloBoolVarArray y;
    IloNumVarArray eta;
    std::unique_ptr<IloCplex> cplex;
    const opt::OptimizationInstance* opt = nullptr;
    std::vector<int> y_position_by_node;
    std::vector<int> last_y_values;
    std::vector<double> last_eta_values;
    double time_limit_seconds = 0.0;
    double mip_gap = -1.0;
    int threads = 0;
    bool verbose = false;
    bool initialized = false;
    int cut_count = 0;
    int lifted_lower_bound_count = 0;
    double last_objective = 0.0;
    double last_best_bound = 0.0;
    double last_mip_gap = 0.0;
    double last_runtime_seconds = 0.0;
    int last_status_code = 0;
    std::string last_status;

    Impl() : env(), model(env), y(env), eta(env) {}

    ~Impl() {
        cplex.reset();
        env.end();
    }
};

namespace {

void apply_parameters(DpvBendersMaster::Impl& impl) {
    if (!impl.cplex) {
        return;
    }
    if (!impl.verbose) {
        impl.cplex->setOut(impl.env.getNullStream());
        impl.cplex->setWarning(impl.env.getNullStream());
    }
    if (impl.time_limit_seconds > 0.0) {
        impl.cplex->setParam(IloCplex::Param::TimeLimit, impl.time_limit_seconds);
    }
    if (impl.mip_gap >= 0.0) {
        impl.cplex->setParam(IloCplex::Param::MIP::Tolerances::MIPGap, impl.mip_gap);
    }
    if (impl.threads > 0) {
        impl.cplex->setParam(IloCplex::Param::Threads, impl.threads);
    }
}

void ensure_initialized(const DpvBendersMaster::Impl& impl) {
    if (!impl.initialized || !impl.opt || !impl.cplex) {
        throw std::runtime_error("DPV Benders master has not been initialized.");
    }
}

}  // namespace

DpvBendersMaster::DpvBendersMaster() : impl_(std::make_unique<Impl>()) {}
DpvBendersMaster::~DpvBendersMaster() = default;

void DpvBendersMaster::initialize(const opt::OptimizationInstance& opt) {
    validate_master_instance(opt);
    if (impl_->initialized) {
        throw std::runtime_error("DPV Benders master cannot be initialized twice.");
    }

    impl_->opt = &opt;
    impl_->y_position_by_node.assign(static_cast<std::size_t>(opt.node_mapper.size()), -1);

    impl_->y = IloBoolVarArray(impl_->env, static_cast<IloInt>(opt.eligible_indices.size()));
    for (std::size_t pos = 0; pos < opt.eligible_indices.size(); ++pos) {
        const int node_index = opt.eligible_indices[pos];
        impl_->y_position_by_node[static_cast<std::size_t>(node_index)] = static_cast<int>(pos);
        std::ostringstream name;
        name << "y_" << node_index;
        impl_->y[static_cast<IloInt>(pos)].setName(name.str().c_str());
    }

    impl_->eta = IloNumVarArray(
        impl_->env,
        static_cast<IloInt>(opt.scenarios.size()),
        0.0,
        IloInfinity,
        ILOFLOAT);
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        std::ostringstream name;
        name << "eta_s" << opt.scenarios[s].scenario_id;
        impl_->eta[static_cast<IloInt>(s)].setName(name.str().c_str());
    }

    IloExpr objective(impl_->env);
    for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
        objective += opt.scenarios[s].probability * impl_->eta[static_cast<IloInt>(s)];
    }
    impl_->model.add(IloMinimize(impl_->env, objective));
    objective.end();

    IloExpr budget_expr(impl_->env);
    for (IloInt pos = 0; pos < impl_->y.getSize(); ++pos) {
        budget_expr += impl_->y[pos];
    }
    impl_->model.add(budget_expr <= opt.budget);
    budget_expr.end();

    impl_->cplex = std::make_unique<IloCplex>(impl_->model);
    impl_->initialized = true;
    apply_parameters(*impl_);
}

void DpvBendersMaster::setParameters(double time_limit_seconds, double mip_gap, int threads, bool verbose) {
    impl_->time_limit_seconds = time_limit_seconds;
    impl_->mip_gap = mip_gap;
    impl_->threads = threads;
    impl_->verbose = verbose;
    apply_parameters(*impl_);
}

void DpvBendersMaster::setWarmStart(const std::vector<int>& y_start_binary) {
    ensure_initialized(*impl_);
    if (y_start_binary.size() != static_cast<std::size_t>(impl_->y.getSize())) {
        throw std::runtime_error("DPV Benders master warm start length must equal the eligible-node count.");
    }

    IloNumVarArray start_vars(impl_->env);
    IloNumArray start_vals(impl_->env);
    for (IloInt pos = 0; pos < impl_->y.getSize(); ++pos) {
        const int value = y_start_binary[static_cast<std::size_t>(pos)];
        if (value != 0 && value != 1) {
            start_vals.end();
            start_vars.end();
            throw std::runtime_error("DPV Benders master warm start values must be binary.");
        }
        start_vars.add(impl_->y[pos]);
        start_vals.add(static_cast<double>(value));
    }
    impl_->cplex->addMIPStart(start_vars, start_vals);
    start_vals.end();
    start_vars.end();
}

MasterSolveResult DpvBendersMaster::solve() {
    ensure_initialized(*impl_);

    const auto start = std::chrono::steady_clock::now();
    const bool solved = impl_->cplex->solve();
    const auto end = std::chrono::steady_clock::now();
    impl_->last_runtime_seconds = std::chrono::duration<double>(end - start).count();

    std::ostringstream status;
    status << impl_->cplex->getStatus();
    impl_->last_status = solved ? status.str() : "No feasible master solution";
    impl_->last_status_code = static_cast<int>(impl_->cplex->getCplexStatus());
    if (!solved) {
        MasterSolveResult result;
        result.status = impl_->last_status;
        result.runtime_seconds = impl_->last_runtime_seconds;
        result.solver_status_code = impl_->last_status_code;
        return result;
    }

    impl_->last_objective = impl_->cplex->getObjValue();
    impl_->last_best_bound = impl_->cplex->getBestObjValue();
    impl_->last_mip_gap = impl_->cplex->getMIPRelativeGap();

    impl_->last_y_values.clear();
    impl_->last_y_values.reserve(static_cast<std::size_t>(impl_->y.getSize()));
    for (IloInt pos = 0; pos < impl_->y.getSize(); ++pos) {
        impl_->last_y_values.push_back(impl_->cplex->getValue(impl_->y[pos]) > 0.5 ? 1 : 0);
    }

    impl_->last_eta_values.clear();
    impl_->last_eta_values.reserve(static_cast<std::size_t>(impl_->eta.getSize()));
    for (IloInt s = 0; s < impl_->eta.getSize(); ++s) {
        impl_->last_eta_values.push_back(impl_->cplex->getValue(impl_->eta[s]));
    }

    MasterSolveResult result;
    result.status = impl_->last_status;
    result.objective_value = impl_->last_objective;
    result.best_bound = impl_->last_best_bound;
    result.mip_gap = impl_->last_mip_gap;
    result.runtime_seconds = impl_->last_runtime_seconds;
    result.solver_status_code = impl_->last_status_code;
    result.y_values = impl_->last_y_values;
    result.eta_values = impl_->last_eta_values;
    return result;
}

std::vector<int> DpvBendersMaster::getCurrentY() const {
    ensure_initialized(*impl_);
    return impl_->last_y_values;
}

std::vector<double> DpvBendersMaster::getEtaValues() const {
    ensure_initialized(*impl_);
    return impl_->last_eta_values;
}

void DpvBendersMaster::addCut(int scenario_position, const BendersCut& cut) {
    ensure_initialized(*impl_);
    if (scenario_position < 0 || scenario_position >= impl_->eta.getSize()) {
        throw std::runtime_error("DPV Benders master cut scenario position is out of range.");
    }

    IloExpr expr(impl_->env);
    expr += impl_->eta[scenario_position];
    for (const auto& [compact_index, coefficient] : cut.coefficients_by_compact_index) {
        if (std::fabs(coefficient) <= 1.0e-12) {
            continue;
        }
        if (compact_index < 0 ||
            compact_index >= static_cast<int>(impl_->y_position_by_node.size()) ||
            impl_->y_position_by_node[static_cast<std::size_t>(compact_index)] < 0) {
            expr.end();
            throw std::runtime_error("DPV Benders cut references a compact node without a master y variable.");
        }
        const int y_pos = impl_->y_position_by_node[static_cast<std::size_t>(compact_index)];
        expr -= coefficient * impl_->y[static_cast<IloInt>(y_pos)];
    }
    IloRange cut_range = (expr >= cut.rhs_constant);
    std::ostringstream name;
    name << "benders_s" << cut.scenario_id << "_c" << (impl_->cut_count + 1);
    cut_range.setName(name.str().c_str());
    impl_->model.add(cut_range);
    expr.end();
    ++impl_->cut_count;
}

void DpvBendersMaster::addLiftedLowerBound(
    int scenario_position,
    const DpvLiftedLowerBoundInequality& inequality) {
    ensure_initialized(*impl_);
    if (scenario_position < 0 || scenario_position >= impl_->eta.getSize()) {
        throw std::runtime_error("DPV Benders master LLBI scenario position is out of range.");
    }

    IloExpr expr(impl_->env);
    expr += impl_->eta[scenario_position];
    for (const auto& [compact_index, coefficient] : inequality.coefficients_by_compact_index) {
        if (std::fabs(coefficient) <= 1.0e-12) {
            continue;
        }
        if (compact_index < 0 ||
            compact_index >= static_cast<int>(impl_->y_position_by_node.size()) ||
            impl_->y_position_by_node[static_cast<std::size_t>(compact_index)] < 0) {
            expr.end();
            throw std::runtime_error("DPV Benders LLBI references a compact node without a master y variable.");
        }
        const int y_pos = impl_->y_position_by_node[static_cast<std::size_t>(compact_index)];
        expr -= coefficient * impl_->y[static_cast<IloInt>(y_pos)];
    }
    IloRange cut_range = (expr >= inequality.rhs_constant);
    std::ostringstream name;
    name << "llbi_s" << inequality.scenario_id << "_c" << (impl_->lifted_lower_bound_count + 1);
    cut_range.setName(name.str().c_str());
    impl_->model.add(cut_range);
    expr.end();
    ++impl_->lifted_lower_bound_count;
}

double DpvBendersMaster::getObjective() const {
    ensure_initialized(*impl_);
    return impl_->last_objective;
}

double DpvBendersMaster::getBestBound() const {
    ensure_initialized(*impl_);
    return impl_->last_best_bound;
}

double DpvBendersMaster::getMipGap() const {
    ensure_initialized(*impl_);
    return impl_->last_mip_gap;
}

double DpvBendersMaster::getRuntime() const {
    ensure_initialized(*impl_);
    return impl_->last_runtime_seconds;
}

std::vector<int> DpvBendersMaster::getSelectedFirebreaks() const {
    ensure_initialized(*impl_);
    std::vector<int> selected;
    for (std::size_t pos = 0; pos < impl_->last_y_values.size(); ++pos) {
        if (impl_->last_y_values[pos] == 1) {
            selected.push_back(impl_->opt->eligible_indices[pos]);
        }
    }
    return selected;
}

std::vector<int> DpvBendersMaster::getSelectedFirebreakOriginalNodes() const {
    ensure_initialized(*impl_);
    std::vector<int> selected;
    for (const int compact_index : getSelectedFirebreaks()) {
        selected.push_back(impl_->opt->node_mapper.to_node(compact_index));
    }
    return selected;
}

std::size_t DpvBendersMaster::getNumVariables() const {
    ensure_initialized(*impl_);
    return static_cast<std::size_t>(impl_->y.getSize() + impl_->eta.getSize());
}

std::size_t DpvBendersMaster::getNumConstraints() const {
    ensure_initialized(*impl_);
    return 1 +
           static_cast<std::size_t>(impl_->eta.getSize()) +
           static_cast<std::size_t>(impl_->cut_count) +
           static_cast<std::size_t>(impl_->lifted_lower_bound_count);
}

int DpvBendersMaster::getCutCount() const {
    ensure_initialized(*impl_);
    return impl_->cut_count;
}

int DpvBendersMaster::getLiftedLowerBoundCount() const {
    ensure_initialized(*impl_);
    return impl_->lifted_lower_bound_count;
}

#endif

}  // namespace firebreak::benders
