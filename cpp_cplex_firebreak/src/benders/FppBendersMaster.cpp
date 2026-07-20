#include "benders/FppBendersMaster.hpp"

#include <chrono>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "solver/CplexEnvironment.hpp"

#ifdef FIREBREAK_WITH_CPLEX
#include <ilcplex/ilocplex.h>
#endif

namespace firebreak::benders {

namespace {

void validate_master_instance(const opt::OptimizationInstance& opt) {
    if (opt.node_mapper.size() <= 0) {
        throw std::runtime_error("FPP Benders master requires at least one mapped node.");
    }
    if (opt.scenarios.empty()) {
        throw std::runtime_error("FPP Benders master requires at least one scenario.");
    }
    if (opt.eligible_indices.empty()) {
        throw std::runtime_error("FPP Benders master requires at least one eligible firebreak node.");
    }
    if (opt.budget < 0) {
        throw std::runtime_error("FPP Benders master budget must be nonnegative.");
    }
    if (opt.budget > static_cast<int>(opt.eligible_indices.size())) {
        throw std::runtime_error("FPP Benders master budget exceeds eligible-node count.");
    }
}

}  // namespace

#ifndef FIREBREAK_WITH_CPLEX

struct FppBendersMaster::Impl {};

FppBendersMaster::FppBendersMaster() : impl_(std::make_unique<Impl>()) {}
FppBendersMaster::~FppBendersMaster() = default;

void FppBendersMaster::initialize(
    const opt::OptimizationInstance& opt,
    const risk::RiskMeasureConfig& risk_config) {
    (void)risk_config;
    validate_master_instance(opt);
    throw std::runtime_error(solver::cplex_unavailable_message());
}

void FppBendersMaster::setParameters(double, double, int, bool) {}

FppMasterSolveResult FppBendersMaster::solve() {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

std::vector<int> FppBendersMaster::getCurrentY() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

std::vector<double> FppBendersMaster::getEtaValues() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

void FppBendersMaster::addCut(int, const BendersCut&) {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

void FppBendersMaster::addLiftedLowerBound(int, const FppLiftedLowerBoundInequality&) {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

double FppBendersMaster::addCoverageLlbi(const FppCoverageLlbiData&) {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

double FppBendersMaster::getObjective() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

double FppBendersMaster::getBestBound() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

double FppBendersMaster::getMipGap() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

double FppBendersMaster::getRuntime() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

std::vector<int> FppBendersMaster::getSelectedFirebreaks() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

std::vector<int> FppBendersMaster::getSelectedFirebreakOriginalNodes() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

std::size_t FppBendersMaster::getNumVariables() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

std::size_t FppBendersMaster::getNumConstraints() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

int FppBendersMaster::getCutCount() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

int FppBendersMaster::getLiftedLowerBoundCount() const {
    throw std::runtime_error(solver::cplex_unavailable_message());
}

#else

struct FppBendersMaster::Impl {
    IloEnv env;
    IloModel model;
    IloBoolVarArray y;
    IloNumVarArray eta;
    IloNumVar risk_threshold;
    IloNumVarArray cvar_excess;
    std::unique_ptr<IloCplex> cplex;
    const opt::OptimizationInstance* opt = nullptr;
    risk::RiskMeasureConfig risk_config;
    std::vector<int> y_position_by_node;
    std::vector<int> last_y_values;
    std::vector<double> last_eta_values;
    double last_risk_threshold_value = 0.0;
    std::vector<double> last_cvar_excess_values;
    double time_limit_seconds = 0.0;
    double mip_gap = -1.0;
    int threads = 0;
    bool verbose = false;
    bool initialized = false;
    int cut_count = 0;
    int lifted_lower_bound_count = 0;
    int risk_constraint_count = 0;
    double last_objective = 0.0;
    double last_best_bound = 0.0;
    double last_mip_gap = 0.0;
    double last_runtime_seconds = 0.0;
    int last_status_code = 0;
    std::string last_status;

    Impl() : env(), model(env), y(env), eta(env), risk_threshold(), cvar_excess(env) {}

    ~Impl() {
        cplex.reset();
        env.end();
    }
};

namespace {

void apply_parameters(FppBendersMaster::Impl& impl) {
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

void ensure_initialized(const FppBendersMaster::Impl& impl) {
    if (!impl.initialized || !impl.opt || !impl.cplex) {
        throw std::runtime_error("FPP Benders master has not been initialized.");
    }
}

bool uses_cvar_risk(const risk::RiskMeasureConfig& config) {
    return config.type == risk::RiskMeasureType::CVaR ||
           config.type == risk::RiskMeasureType::MeanCVaR;
}

}  // namespace

FppBendersMaster::FppBendersMaster() : impl_(std::make_unique<Impl>()) {}
FppBendersMaster::~FppBendersMaster() = default;

void FppBendersMaster::initialize(
    const opt::OptimizationInstance& opt,
    const risk::RiskMeasureConfig& risk_config) {
    validate_master_instance(opt);
    risk::RiskMeasureConfig effective_risk_config = risk_config;
    if (effective_risk_config.type == risk::RiskMeasureType::CVaR) {
        effective_risk_config.cvarLambda = 1.0;
    }
    risk::validate_risk_measure_config(effective_risk_config);
    if (impl_->initialized) {
        throw std::runtime_error("FPP Benders master cannot be initialized twice.");
    }

    impl_->opt = &opt;
    impl_->risk_config = effective_risk_config;
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

    const bool risk_enabled = uses_cvar_risk(effective_risk_config);
    if (risk_enabled) {
        impl_->risk_threshold = IloNumVar(impl_->env, -IloInfinity, IloInfinity, ILOFLOAT);
        impl_->risk_threshold.setName("risk_threshold");
        impl_->cvar_excess = IloNumVarArray(
            impl_->env,
            static_cast<IloInt>(opt.scenarios.size()),
            0.0,
            IloInfinity,
            ILOFLOAT);
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            std::ostringstream name;
            name << "cvar_excess_s" << opt.scenarios[s].scenario_id;
            impl_->cvar_excess[static_cast<IloInt>(s)].setName(name.str().c_str());

            IloExpr excess_lhs(impl_->env);
            excess_lhs += impl_->cvar_excess[static_cast<IloInt>(s)];
            excess_lhs -= impl_->eta[static_cast<IloInt>(s)];
            excess_lhs += impl_->risk_threshold;
            IloRange excess_range = (excess_lhs >= 0.0);
            std::ostringstream constraint_name;
            constraint_name << "cvar_excess_link_s" << opt.scenarios[s].scenario_id;
            excess_range.setName(constraint_name.str().c_str());
            impl_->model.add(excess_range);
            excess_lhs.end();
            ++impl_->risk_constraint_count;
        }
    }

    IloExpr objective(impl_->env);
    const double cvar_tail_scale = 1.0 / (1.0 - effective_risk_config.cvarBeta);
    if (effective_risk_config.type == risk::RiskMeasureType::Expected) {
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            objective += opt.scenarios[s].probability * impl_->eta[static_cast<IloInt>(s)];
        }
    } else {
        const bool include_expected_term =
            effective_risk_config.type == risk::RiskMeasureType::MeanCVaR;
        const double expected_weight = include_expected_term
            ? (1.0 - effective_risk_config.cvarLambda)
            : 0.0;
        if (include_expected_term && expected_weight != 0.0) {
            for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
                objective +=
                    expected_weight *
                    opt.scenarios[s].probability *
                    impl_->eta[static_cast<IloInt>(s)];
            }
        }

        IloExpr cvar_tail(impl_->env);
        for (std::size_t s = 0; s < opt.scenarios.size(); ++s) {
            cvar_tail +=
                opt.scenarios[s].probability *
                impl_->cvar_excess[static_cast<IloInt>(s)];
        }
        objective +=
            effective_risk_config.cvarLambda * impl_->risk_threshold +
            effective_risk_config.cvarLambda * cvar_tail_scale * cvar_tail;
        cvar_tail.end();
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

void FppBendersMaster::setParameters(double time_limit_seconds, double mip_gap, int threads, bool verbose) {
    impl_->time_limit_seconds = time_limit_seconds;
    impl_->mip_gap = mip_gap;
    impl_->threads = threads;
    impl_->verbose = verbose;
    apply_parameters(*impl_);
}

FppMasterSolveResult FppBendersMaster::solve() {
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
        FppMasterSolveResult result;
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
    impl_->last_cvar_excess_values.clear();
    impl_->last_risk_threshold_value = 0.0;
    if (uses_cvar_risk(impl_->risk_config)) {
        impl_->last_risk_threshold_value = impl_->cplex->getValue(impl_->risk_threshold);
        impl_->last_cvar_excess_values.reserve(
            static_cast<std::size_t>(impl_->cvar_excess.getSize()));
        for (IloInt s = 0; s < impl_->cvar_excess.getSize(); ++s) {
            impl_->last_cvar_excess_values.push_back(
                impl_->cplex->getValue(impl_->cvar_excess[s]));
        }
    }

    FppMasterSolveResult result;
    result.status = impl_->last_status;
    result.objective_value = impl_->last_objective;
    result.best_bound = impl_->last_best_bound;
    result.mip_gap = impl_->last_mip_gap;
    result.runtime_seconds = impl_->last_runtime_seconds;
    result.solver_status_code = impl_->last_status_code;
    result.y_values = impl_->last_y_values;
    result.eta_values = impl_->last_eta_values;
    result.risk_threshold_value = impl_->last_risk_threshold_value;
    result.cvar_excess_values = impl_->last_cvar_excess_values;
    return result;
}

std::vector<int> FppBendersMaster::getCurrentY() const {
    ensure_initialized(*impl_);
    return impl_->last_y_values;
}

std::vector<double> FppBendersMaster::getEtaValues() const {
    ensure_initialized(*impl_);
    return impl_->last_eta_values;
}

void FppBendersMaster::addCut(int scenario_position, const BendersCut& cut) {
    ensure_initialized(*impl_);
    if (scenario_position < 0 || scenario_position >= impl_->eta.getSize()) {
        throw std::runtime_error("FPP Benders master cut scenario position is out of range.");
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
            throw std::runtime_error("FPP Benders cut references a compact node without a master y variable.");
        }
        const int y_pos = impl_->y_position_by_node[static_cast<std::size_t>(compact_index)];
        expr -= coefficient * impl_->y[static_cast<IloInt>(y_pos)];
    }
    IloRange cut_range = (expr >= cut.rhs_constant);
    std::ostringstream name;
    name << "fpp_benders_s" << cut.scenario_id << "_c" << (impl_->cut_count + 1);
    cut_range.setName(name.str().c_str());
    impl_->model.add(cut_range);
    expr.end();
    ++impl_->cut_count;
}

void FppBendersMaster::addLiftedLowerBound(
    int scenario_position,
    const FppLiftedLowerBoundInequality& inequality) {
    ensure_initialized(*impl_);
    if (scenario_position < 0 || scenario_position >= impl_->eta.getSize()) {
        throw std::runtime_error("FPP Benders master LLBI scenario position is out of range.");
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
            throw std::runtime_error(
                "FPP lifted lower-bound inequality references a compact node without a master y variable.");
        }
        const int y_pos = impl_->y_position_by_node[static_cast<std::size_t>(compact_index)];
        expr -= coefficient * impl_->y[static_cast<IloInt>(y_pos)];
    }
    IloRange range = (expr >= inequality.rhs_constant);
    std::ostringstream name;
    name << "fpp_llbi_s" << inequality.scenario_id
         << "_c" << (impl_->lifted_lower_bound_count + 1);
    range.setName(name.str().c_str());
    impl_->model.add(range);
    expr.end();
    ++impl_->lifted_lower_bound_count;
}

double FppBendersMaster::addCoverageLlbi(const FppCoverageLlbiData& data) {
    ensure_initialized(*impl_);
    const auto start = std::chrono::steady_clock::now();
    if (!data.enabled) {
        return 0.0;
    }
    for (const auto& scenario_record : data.scenarios) {
        if (scenario_record.scenario_index < 0 ||
            scenario_record.scenario_index >= impl_->eta.getSize()) {
            throw std::runtime_error("FPP Benders master CoverageLLBI scenario position is out of range.");
        }
        IloExpr lower_bound_rhs(impl_->env);
        lower_bound_rhs += scenario_record.empty_burned_area;
        for (const auto& node_record : scenario_record.nodes) {
            IloNumVar zeta(impl_->env, 0.0, 1.0, ILOFLOAT);
            std::ostringstream zeta_name;
            zeta_name << "coverage_zeta_s" << scenario_record.scenario_id
                      << "_" << node_record.compact_node;
            zeta.setName(zeta_name.str().c_str());

            IloExpr cover(impl_->env);
            for (const int candidate : node_record.covering_candidate_compact_nodes) {
                if (candidate < 0 ||
                    candidate >= static_cast<int>(impl_->y_position_by_node.size()) ||
                    impl_->y_position_by_node[static_cast<std::size_t>(candidate)] < 0) {
                    continue;
                }
                cover += impl_->y[static_cast<IloInt>(
                    impl_->y_position_by_node[static_cast<std::size_t>(candidate)])];
            }
            cover -= zeta;
            IloRange link_range = (cover >= 0.0);
            std::ostringstream link_name;
            link_name << "coverage_link_s" << scenario_record.scenario_id
                      << "_" << node_record.compact_node;
            link_range.setName(link_name.str().c_str());
            impl_->model.add(link_range);
            cover.end();
            lower_bound_rhs -= node_record.cell_weight * zeta;
        }
        if (!scenario_record.nodes.empty()) {
            IloExpr lhs(impl_->env);
            lhs += impl_->eta[static_cast<IloInt>(scenario_record.scenario_index)];
            lhs -= lower_bound_rhs;
            IloRange loss_range = (lhs >= 0.0);
            std::ostringstream loss_name;
            loss_name << "coverage_loss_s" << scenario_record.scenario_id;
            loss_range.setName(loss_name.str().c_str());
            impl_->model.add(loss_range);
            lhs.end();
        }
        lower_bound_rhs.end();
    }
    impl_->cplex = std::make_unique<IloCplex>(impl_->model);
    apply_parameters(*impl_);
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

double FppBendersMaster::getObjective() const {
    ensure_initialized(*impl_);
    return impl_->last_objective;
}

double FppBendersMaster::getBestBound() const {
    ensure_initialized(*impl_);
    return impl_->last_best_bound;
}

double FppBendersMaster::getMipGap() const {
    ensure_initialized(*impl_);
    return impl_->last_mip_gap;
}

double FppBendersMaster::getRuntime() const {
    ensure_initialized(*impl_);
    return impl_->last_runtime_seconds;
}

std::vector<int> FppBendersMaster::getSelectedFirebreaks() const {
    ensure_initialized(*impl_);
    std::vector<int> selected;
    for (std::size_t pos = 0; pos < impl_->last_y_values.size(); ++pos) {
        if (impl_->last_y_values[pos] == 1) {
            selected.push_back(impl_->opt->eligible_indices[pos]);
        }
    }
    return selected;
}

std::vector<int> FppBendersMaster::getSelectedFirebreakOriginalNodes() const {
    ensure_initialized(*impl_);
    std::vector<int> selected;
    for (const int compact_index : getSelectedFirebreaks()) {
        selected.push_back(impl_->opt->node_mapper.to_node(compact_index));
    }
    return selected;
}

std::size_t FppBendersMaster::getNumVariables() const {
    ensure_initialized(*impl_);
    std::size_t count = static_cast<std::size_t>(impl_->y.getSize() + impl_->eta.getSize());
    if (uses_cvar_risk(impl_->risk_config)) {
        count += 1 + static_cast<std::size_t>(impl_->cvar_excess.getSize());
    }
    return count;
}

std::size_t FppBendersMaster::getNumConstraints() const {
    ensure_initialized(*impl_);
    return 1 +
           static_cast<std::size_t>(impl_->eta.getSize()) +
           static_cast<std::size_t>(impl_->risk_constraint_count) +
           static_cast<std::size_t>(impl_->cut_count) +
           static_cast<std::size_t>(impl_->lifted_lower_bound_count);
}

int FppBendersMaster::getCutCount() const {
    ensure_initialized(*impl_);
    return impl_->cut_count;
}

int FppBendersMaster::getLiftedLowerBoundCount() const {
    ensure_initialized(*impl_);
    return impl_->lifted_lower_bound_count;
}

#endif

}  // namespace firebreak::benders
