#include "io/ExperimentResultWriter.hpp"

#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "io/PathUtils.hpp"

namespace firebreak::io {

namespace {

std::string join_ints(const std::vector<int>& values, const std::string& delimiter) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << delimiter;
        }
        out << values[i];
    }
    return out.str();
}

std::string join_strings(const std::vector<std::string>& values, const std::string& delimiter) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << delimiter;
        }
        out << values[i];
    }
    return out.str();
}

std::string json_escape_local(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += ch;
        }
    }
    return out;
}

std::string csv_escape(const std::string& value) {
    bool needs_quotes = false;
    for (const char ch : value) {
        if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return value;
    }
    std::string out = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            out += "\"\"";
        } else {
            out += ch;
        }
    }
    out += "\"";
    return out;
}

bool existing_csv_has_column(const std::filesystem::path& output_path, const std::string& column_name) {
    std::ifstream in(output_path);
    std::string header;
    if (!std::getline(in, header)) {
        return false;
    }
    return header.find(column_name) != std::string::npos;
}

std::string format_json_number(double value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(8) << value;
    return out.str();
}

std::string format_csv_number(double value) {
    if (!std::isfinite(value)) {
        return "";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(8) << value;
    return out.str();
}

void write_json_int_array(std::ostream& out, const std::vector<int>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << values[i];
    }
    out << "]";
}

void write_json_string_array(std::ostream& out, const std::vector<std::string>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "\"" << json_escape_local(values[i]) << "\"";
    }
    out << "]";
}

void write_candidate_score_pair_array(
    std::ostream& out,
    const std::vector<std::pair<int, double>>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "{\"candidate_id\":" << values[i].first
            << ",\"score\":" << format_json_number(values[i].second) << "}";
    }
    out << "]";
}

void write_nested_json_int_array(
    std::ostream& out,
    const std::vector<std::vector<int>>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        write_json_int_array(out, values[i]);
    }
    out << "]";
}

void write_tail_score_rank_array(
    std::ostream& out,
    const std::vector<benders::TailScoreCandidateRank>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "{\"candidate_id\":" << values[i].candidate_id
            << ",\"score\":" << format_json_number(values[i].score)
            << ",\"rank\":" << values[i].rank << "}";
    }
    out << "]";
}

void write_tail_score_candidate_events(
    std::ostream& out,
    const std::vector<benders::TailScoreCandidateEventDiagnostic>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        const auto& event = values[i];
        out << "{";
        out << "\"candidate_id\":" << event.candidate_id << ",";
        out << "\"event_type\":\"" << json_escape_local(event.event_type) << "\",";
        out << "\"generic_score\":" << format_json_number(event.generic_score) << ",";
        out << "\"tail_empirical_score\":" << format_json_number(event.tail_empirical_score) << ",";
        out << "\"tail_excess_score\":" << format_json_number(event.tail_excess_score) << ",";
        out << "\"recent_tail_score\":" << format_json_number(event.recent_tail_score) << ",";
        out << "\"tail_blend_score\":" << format_json_number(event.tail_blend_score) << ",";
        out << "\"was_active_before\":" << (event.was_active_before ? "true" : "false") << ",";
        out << "\"was_active_after\":" << (event.was_active_after ? "true" : "false") << ",";
        out << "\"was_selected\":" << (event.was_selected ? "true" : "false") << ",";
        out << "\"was_protected\":" << (event.was_protected ? "true" : "false") << ",";
        out << "\"was_tail_protected\":" << (event.was_tail_protected ? "true" : "false") << ",";
        out << "\"was_deactivated\":" << (event.was_deactivated ? "true" : "false") << ",";
        out << "\"was_activated\":" << (event.was_activated ? "true" : "false") << ",";
        out << "\"rank_generic\":" << event.rank_generic << ",";
        out << "\"rank_tail_empirical\":" << event.rank_tail_empirical << ",";
        out << "\"rank_tail_excess\":" << event.rank_tail_excess << ",";
        out << "\"rank_tail_blend\":" << event.rank_tail_blend << ",";
        out << "\"warning\":\"" << json_escape_local(event.warning) << "\"";
        out << "}";
    }
    out << "]";
}

bool has_benders_diagnostics(const StandardExperimentResult& result) {
    return !result.benders_status.empty() ||
           !result.benders_termination_reason.empty() ||
           !result.benders_iteration_log.empty() ||
           result.benders_use_lifted_lower_bounds ||
           result.benders_lifted_lower_bound_count > 0 ||
           result.benders_lifted_lower_bound_nonzero_coefficients > 0 ||
           result.benders_lifted_lower_bound_constraints_added > 0 ||
           result.coverage_llbi_enabled ||
           result.coverage_llbi_auxiliary_variables > 0 ||
           result.path_llbi_enabled ||
           result.path_llbi_auxiliary_variables > 0 ||
           !result.benders_lifted_lower_bound_notes.empty();
}

bool has_branch_benders_diagnostics(const StandardExperimentResult& result) {
    return result.branch_benders_enabled ||
           result.branch_benders_callback_calls > 0 ||
           result.branch_benders_candidate_incumbents_checked > 0 ||
           !result.branch_benders_incumbent_log.empty();
}

bool has_restricted_candidate_diagnostics(const StandardExperimentResult& result) {
    return result.restricted_candidate_enabled;
}

void write_benders_json_block(std::ostream& out, const StandardExperimentResult& result) {
    out << "  \"benders\": {\n";
    out << "    \"status\": \"" << json_escape_local(result.benders_status) << "\",\n";
    out << "    \"iterations\": " << result.benders_iterations << ",\n";
    out << "    \"cuts_added\": " << result.benders_cuts_added << ",\n";
    out << "    \"final_max_cut_violation\": "
        << format_json_number(result.benders_final_max_cut_violation) << ",\n";
    out << "    \"largest_intermediate_cut_violation\": "
        << format_json_number(result.benders_largest_intermediate_cut_violation) << ",\n";
    out << "    \"master_solve_time_sec\": "
        << format_json_number(result.benders_master_solve_time_sec) << ",\n";
    out << "    \"subproblem_time_sec\": "
        << format_json_number(result.benders_subproblem_time_sec) << ",\n";
    out << "    \"subproblems_solved\": " << result.benders_subproblems_solved << ",\n";
    out << "    \"average_subproblem_time_sec\": "
        << format_json_number(result.benders_average_subproblem_time_sec) << ",\n";
    out << "    \"max_subproblem_time_sec\": "
        << format_json_number(result.benders_max_subproblem_time_sec) << ",\n";
    out << "    \"termination_reason\": \""
        << json_escape_local(result.benders_termination_reason) << "\",\n";
    out << "    \"use_lifted_lower_bounds\": "
        << (result.benders_use_lifted_lower_bounds ? "true" : "false") << ",\n";
    out << "    \"lifted_lower_bound_count\": "
        << result.benders_lifted_lower_bound_count << ",\n";
    out << "    \"lifted_lower_bound_precompute_time_sec\": "
        << format_json_number(result.benders_lifted_lower_bound_precompute_time_sec) << ",\n";
    out << "    \"lifted_lower_bound_nonzero_coefficients\": "
        << result.benders_lifted_lower_bound_nonzero_coefficients << ",\n";
    out << "    \"lifted_lower_bound_min_rhs\": "
        << format_json_number(result.benders_lifted_lower_bound_min_rhs) << ",\n";
    out << "    \"lifted_lower_bound_max_rhs\": "
        << format_json_number(result.benders_lifted_lower_bound_max_rhs) << ",\n";
    out << "    \"lifted_lower_bound_weighted\": "
        << (result.benders_lifted_lower_bound_weighted ? "true" : "false") << ",\n";
    out << "    \"lifted_lower_bound_weight_map_hash\": \""
        << json_escape_local(result.benders_lifted_lower_bound_weight_map_hash) << "\",\n";
    out << "    \"lifted_lower_bound_scenarios_precomputed\": "
        << result.benders_lifted_lower_bound_scenarios_precomputed << ",\n";
    out << "    \"lifted_lower_bound_singletons_evaluated\": "
        << result.benders_lifted_lower_bound_singletons_evaluated << ",\n";
    out << "    \"lifted_lower_bound_no_firebreak_loss_min\": "
        << format_json_number(result.benders_lifted_lower_bound_no_firebreak_loss_min) << ",\n";
    out << "    \"lifted_lower_bound_no_firebreak_loss_max\": "
        << format_json_number(result.benders_lifted_lower_bound_no_firebreak_loss_max) << ",\n";
    out << "    \"lifted_lower_bound_singleton_benefit_min\": "
        << format_json_number(result.benders_lifted_lower_bound_singleton_benefit_min) << ",\n";
    out << "    \"lifted_lower_bound_singleton_benefit_max\": "
        << format_json_number(result.benders_lifted_lower_bound_singleton_benefit_max) << ",\n";
    out << "    \"lifted_lower_bound_constraints_added\": "
        << result.benders_lifted_lower_bound_constraints_added << ",\n";
    out << "    \"lifted_lower_bound_cache_hit\": "
        << (result.benders_lifted_lower_bound_cache_hit ? "true" : "false") << ",\n";
    out << "    \"lifted_lower_bound_validity_mode\": \""
        << json_escape_local(result.benders_lifted_lower_bound_validity_mode) << "\",\n";
    out << "    \"lifted_lower_bound_notes\": ";
    write_json_string_array(out, result.benders_lifted_lower_bound_notes);
    out << ",\n";
    out << "    \"iteration_log\": [\n";
    for (std::size_t i = 0; i < result.benders_iteration_log.size(); ++i) {
        const auto& log = result.benders_iteration_log[i];
        out << "      {\n";
        out << "        \"iteration\": " << log.iteration << ",\n";
        out << "        \"master_objective\": " << format_json_number(log.master_objective) << ",\n";
        out << "        \"master_best_bound\": " << format_json_number(log.master_best_bound) << ",\n";
        out << "        \"total_subproblem_objective\": "
            << format_json_number(log.total_subproblem_objective) << ",\n";
        out << "        \"weighted_recourse_objective\": "
            << format_json_number(log.weighted_recourse_objective) << ",\n";
        out << "        \"cuts_added\": " << log.cuts_added << ",\n";
        out << "        \"cumulative_cuts\": " << log.cumulative_cuts << ",\n";
        out << "        \"subproblems_attempted\": " << log.subproblems_attempted << ",\n";
        out << "        \"subproblems_solved\": " << log.subproblems_solved << ",\n";
        out << "        \"max_cut_violation\": " << format_json_number(log.max_cut_violation) << ",\n";
        out << "        \"avg_cut_violation\": " << format_json_number(log.avg_cut_violation) << ",\n";
        out << "        \"master_time_sec\": " << format_json_number(log.master_time_sec) << ",\n";
        out << "        \"subproblem_time_sec\": " << format_json_number(log.subproblem_time_sec) << ",\n";
        out << "        \"average_subproblem_time_sec\": "
            << format_json_number(log.average_subproblem_time_sec) << ",\n";
        out << "        \"max_subproblem_time_sec\": "
            << format_json_number(log.max_subproblem_time_sec) << ",\n";
        out << "        \"iteration_time_sec\": " << format_json_number(log.iteration_time_sec) << ",\n";
        out << "        \"selected_firebreak_count\": " << log.selected_firebreak_count << ",\n";
        out << "        \"selected_firebreaks\": ";
        write_json_int_array(out, log.selected_firebreaks);
        out << "\n";
        out << "      }" << (i + 1 == result.benders_iteration_log.size() ? "\n" : ",\n");
    }
    out << "    ]\n";
    out << "  }";
}

void write_branch_benders_json_block(std::ostream& out, const StandardExperimentResult& result) {
    out << "  \"branch_benders\": {\n";
    out << "    \"enabled\": " << (result.branch_benders_enabled ? "true" : "false") << ",\n";
    out << "    \"callback_calls\": " << result.branch_benders_callback_calls << ",\n";
    out << "    \"candidate_callback_calls\": "
        << result.branch_benders_candidate_callback_calls << ",\n";
    out << "    \"incumbent_callback_calls\": "
        << result.branch_benders_incumbent_callback_calls << ",\n";
    out << "    \"candidate_incumbents_checked\": "
        << result.branch_benders_candidate_incumbents_checked << ",\n";
    out << "    \"subproblems_attempted\": "
        << result.branch_benders_subproblems_attempted << ",\n";
    out << "    \"subproblems_solved\": "
        << result.branch_benders_subproblems_solved << ",\n";
    out << "    \"lazy_cuts_added\": "
        << result.branch_benders_lazy_cuts_added << ",\n";
    out << "    \"max_cut_violation\": "
        << format_json_number(result.branch_benders_max_cut_violation) << ",\n";
    out << "    \"largest_incumbent_cut_violation\": "
        << format_json_number(result.branch_benders_largest_incumbent_cut_violation) << ",\n";
    out << "    \"callback_time_sec\": "
        << format_json_number(result.branch_benders_callback_time_sec) << ",\n";
    out << "    \"subproblem_time_sec\": "
        << format_json_number(result.branch_benders_subproblem_time_sec) << ",\n";
    out << "    \"average_subproblem_time_sec\": "
        << format_json_number(result.branch_benders_average_subproblem_time_sec) << ",\n";
    out << "    \"max_subproblem_time_sec\": "
        << format_json_number(result.branch_benders_max_subproblem_time_sec) << ",\n";
    out << "    \"cut_construction_time_sec\": "
        << format_json_number(result.branch_benders_cut_construction_time_sec) << ",\n";
    out << "    \"lazy_cut_insertion_time_sec\": "
        << format_json_number(result.branch_benders_lazy_cut_insertion_time_sec) << ",\n";
    out << "    \"violated_cuts\": " << result.branch_benders_violated_cuts << ",\n";
    out << "    \"nonviolated_cuts\": " << result.branch_benders_nonviolated_cuts << ",\n";
    out << "    \"skipped_cuts\": " << result.branch_benders_skipped_cuts << ",\n";
    out << "    \"duplicate_cuts\": " << result.branch_benders_duplicate_cuts << ",\n";
    out << "    \"use_root_user_cuts\": "
        << (result.branch_benders_use_root_user_cuts ? "true" : "false") << ",\n";
    out << "    \"root_user_cut_max_rounds\": "
        << result.branch_benders_root_user_cut_max_rounds << ",\n";
    out << "    \"root_user_cut_tolerance\": "
        << format_json_number(result.branch_benders_root_user_cut_tolerance) << ",\n";
    out << "    \"root_user_cut_rounds_executed\": "
        << result.branch_benders_root_user_cut_rounds_executed << ",\n";
    out << "    \"root_user_cut_callback_calls\": "
        << result.branch_benders_root_user_cut_callback_calls << ",\n";
    out << "    \"root_user_cuts_added\": "
        << result.branch_benders_root_user_cuts_added << ",\n";
    out << "    \"root_user_cut_scenarios_solved\": "
        << result.branch_benders_root_user_cut_scenarios_solved << ",\n";
    out << "    \"root_user_cut_max_violation\": "
        << format_json_number(result.branch_benders_root_user_cut_max_violation) << ",\n";
    out << "    \"root_user_cut_total_time_sec\": "
        << format_json_number(result.branch_benders_root_user_cut_total_time_sec) << ",\n";
    out << "    \"root_user_cut_subproblem_time_sec\": "
        << format_json_number(result.branch_benders_root_user_cut_subproblem_time_sec) << ",\n";
    out << "    \"root_user_cut_skipped_reason\": \""
        << json_escape_local(result.branch_benders_root_user_cut_skipped_reason) << "\",\n";
    out << "    \"root_user_cut_only_at_root_confirmed\": "
        << (result.branch_benders_root_user_cut_only_at_root_confirmed ? "true" : "false") << ",\n";
    out << "    \"root_user_cut_round_log\": [\n";
    for (std::size_t i = 0; i < result.branch_benders_root_user_cut_round_log.size(); ++i) {
        const auto& log = result.branch_benders_root_user_cut_round_log[i];
        out << "      {\n";
        out << "        \"round_index\": " << log.round_index << ",\n";
        out << "        \"scenarios_solved\": " << log.scenarios_solved << ",\n";
        out << "        \"cuts_added\": " << log.cuts_added << ",\n";
        out << "        \"max_violation\": "
            << format_json_number(log.max_violation) << ",\n";
        out << "        \"avg_violation\": "
            << format_json_number(log.avg_violation) << ",\n";
        out << "        \"time_sec\": "
            << format_json_number(log.time_sec) << "\n";
        out << "      }" << (i + 1 == result.branch_benders_root_user_cut_round_log.size() ? "\n" : ",\n");
    }
    out << "    ],\n";
    out << "    \"incumbent_log\": [\n";
    for (std::size_t i = 0; i < result.branch_benders_incumbent_log.size(); ++i) {
        const auto& log = result.branch_benders_incumbent_log[i];
        out << "      {\n";
        out << "        \"incumbent_index\": " << log.incumbent_index << ",\n";
        out << "        \"incumbent_objective\": "
            << format_json_number(log.incumbent_objective) << ",\n";
        out << "        \"selected_firebreak_count\": "
            << log.selected_firebreak_count << ",\n";
        out << "        \"cuts_added\": " << log.cuts_added << ",\n";
        out << "        \"max_cut_violation\": "
            << format_json_number(log.max_cut_violation) << ",\n";
        out << "        \"subproblems_attempted\": " << log.subproblems_attempted << ",\n";
        out << "        \"subproblems_solved\": " << log.subproblems_solved << ",\n";
        out << "        \"subproblem_time_sec\": "
            << format_json_number(log.subproblem_time_sec) << ",\n";
        out << "        \"average_subproblem_time_sec\": "
            << format_json_number(log.average_subproblem_time_sec) << ",\n";
        out << "        \"max_subproblem_time_sec\": "
            << format_json_number(log.max_subproblem_time_sec) << ",\n";
        out << "        \"cut_construction_time_sec\": "
            << format_json_number(log.cut_construction_time_sec) << ",\n";
        out << "        \"lazy_cut_insertion_time_sec\": "
            << format_json_number(log.lazy_cut_insertion_time_sec) << ",\n";
        out << "        \"violated_cuts\": " << log.violated_cuts << ",\n";
        out << "        \"nonviolated_cuts\": " << log.nonviolated_cuts << ",\n";
        out << "        \"skipped_cuts\": " << log.skipped_cuts << ",\n";
        out << "        \"duplicate_cuts\": " << log.duplicate_cuts << "\n";
        out << "      }" << (i + 1 == result.branch_benders_incumbent_log.size() ? "\n" : ",\n");
    }
    out << "    ]\n";
    out << "  }";
}

void write_restricted_candidate_json_block(std::ostream& out, const StandardExperimentResult& result) {
    out << "  \"restricted_candidate\": {\n";
    out << "    \"enabled\": " << (result.restricted_candidate_enabled ? "true" : "false") << ",\n";
    out << "    \"exact_mode\": " << (result.restricted_candidate_exact_mode ? "true" : "false") << ",\n";
    out << "    \"candidate_bounds_enabled\": "
        << (result.restricted_candidate_bounds_enabled ? "true" : "false") << ",\n";
    out << "    \"candidate_bounds_weighted\": "
        << (result.restricted_candidate_bounds_weighted ? "true" : "false") << ",\n";
    out << "    \"candidate_bound_type\": \""
        << json_escape_local(result.restricted_candidate_bound_type) << "\",\n";
    out << "    \"candidate_bound_map_hash\": \""
        << json_escape_local(result.restricted_candidate_bound_map_hash) << "\",\n";
    out << "    \"candidates_evaluated_by_bound\": "
        << result.restricted_candidates_evaluated_by_bound << ",\n";
    out << "    \"candidates_permanently_pruned\": "
        << result.restricted_candidates_permanently_pruned << ",\n";
    out << "    \"candidates_not_pruned_due_to_safety\": "
        << result.restricted_candidates_not_pruned_due_to_safety << ",\n";
    out << "    \"early_exactness_certificate_used\": "
        << (result.restricted_candidate_early_exactness_certificate_used ? "true" : "false") << ",\n";
    out << "    \"full_activation_avoided\": "
        << (result.restricted_candidate_full_activation_avoided ? "true" : "false") << ",\n";
    out << "    \"unvalidated_bound_rejected\": "
        << (result.restricted_candidate_unvalidated_bound_rejected ? "true" : "false") << ",\n";
    out << "    \"initial_candidate_policy\": \""
        << json_escape_local(result.restricted_candidate_initial_policy) << "\",\n";
    out << "    \"activation_policy\": \""
        << json_escape_local(result.restricted_candidate_activation_policy) << "\",\n";
    out << "    \"initial_active_count\": "
        << result.restricted_candidate_initial_active_count << ",\n";
    out << "    \"final_active_count\": "
        << result.restricted_candidate_final_active_count << ",\n";
    out << "    \"final_active_fraction\": "
        << format_json_number(result.restricted_candidate_final_active_fraction) << ",\n";
    out << "    \"eventually_activated_all\": "
        << (result.restricted_candidate_eventually_activated_all ? "true" : "false") << ",\n";
    out << "    \"full_activation_performed\": "
        << (result.restricted_candidate_full_activation_performed ? "true" : "false") << ",\n";
    out << "    \"restricted_lower_bound_is_global\": "
        << (result.restricted_candidate_restricted_lower_bound_is_global ? "true" : "false") << ",\n";
    out << "    \"final_lower_bound_is_global\": "
        << (result.restricted_candidate_final_lower_bound_is_global ? "true" : "false") << ",\n";
    out << "    \"cut_reuse_enabled\": "
        << (result.restricted_candidate_cut_reuse_enabled ? "true" : "false") << ",\n";
    out << "    \"cut_pool_size\": " << result.restricted_candidate_cut_pool_size << ",\n";
    out << "    \"cut_pool_peak_size\": "
        << result.restricted_candidate_cut_pool_peak_size << ",\n";
    out << "    \"cut_pool_evictions\": "
        << result.restricted_candidate_cut_pool_evictions << ",\n";
    out << "    \"cut_pool_reinstantiations\": "
        << result.restricted_candidate_cut_pool_reinstantiations << ",\n";
    out << "    \"candidate_rounds\": " << result.restricted_candidate_rounds << ",\n";
    out << "    \"cuts_reused_in_full_stage\": "
        << result.restricted_candidate_cuts_reused_in_full_stage << ",\n";
    out << "    \"duplicate_cuts_skipped\": "
        << result.restricted_candidate_duplicate_cuts_skipped << ",\n";
    out << "    \"heuristic_mode_enabled\": "
        << (result.restricted_candidate_heuristic_mode_enabled ? "true" : "false") << ",\n";
    out << "    \"stopped_before_full_activation\": "
        << (result.restricted_candidate_stopped_before_full_activation ? "true" : "false") << ",\n";
    out << "    \"global_optimality_certified\": "
        << (result.restricted_candidate_global_optimality_certified ? "true" : "false") << ",\n";
    out << "    \"global_time_budget_enabled\": "
        << (result.restricted_candidate_global_time_budget_enabled ? "true" : "false") << ",\n";
    out << "    \"time_budget_exhausted\": "
        << (result.restricted_candidate_time_budget_exhausted ? "true" : "false") << ",\n";
    out << "    \"global_time_limit_seconds\": "
        << format_json_number(result.restricted_candidate_global_time_limit_seconds) << ",\n";
    out << "    \"elapsed_time_total_seconds\": "
        << format_json_number(result.restricted_candidate_elapsed_time_total_seconds) << ",\n";
    out << "    \"initial_stage_runtime_seconds\": "
        << format_json_number(result.restricted_candidate_initial_stage_runtime) << ",\n";
    out << "    \"activation_stage_runtime_total_seconds\": "
        << format_json_number(result.restricted_candidate_activation_stage_runtime_total) << ",\n";
    out << "    \"final_full_stage_runtime_seconds\": "
        << format_json_number(result.restricted_candidate_final_full_stage_runtime) << ",\n";
    out << "    \"final_stage_time_limit_seconds\": "
        << format_json_number(result.restricted_candidate_final_stage_time_limit) << ",\n";
    out << "    \"reason_for_heuristic_stop\": \""
        << json_escape_local(result.restricted_candidate_reason_for_heuristic_stop) << "\",\n";
    out << "    \"restricted_objective\": "
        << format_json_number(result.restricted_candidate_restricted_objective) << ",\n";
    out << "    \"restricted_best_bound\": "
        << format_json_number(result.restricted_candidate_restricted_best_bound) << ",\n";
    out << "    \"restricted_bound_is_global\": "
        << (result.restricted_candidate_restricted_bound_is_global ? "true" : "false") << ",\n";
    out << "    \"active_candidate_fraction_at_stop\": "
        << format_json_number(result.restricted_candidate_active_fraction_at_stop) << ",\n";
    out << "    \"candidate_maintenance_policy\": \""
        << json_escape_local(result.restricted_candidate_maintenance_policy) << "\",\n";
    out << "    \"maintenance_weighted\": "
        << (result.restricted_candidate_maintenance_weighted ? "true" : "false") << ",\n";
    out << "    \"maintenance_map_hash\": \""
        << json_escape_local(result.restricted_candidate_maintenance_map_hash) << "\",\n";
    out << "    \"candidate_score_mode\": \""
        << json_escape_local(result.restricted_candidate_score_mode) << "\",\n";
    out << "    \"candidate_tail_score_gamma\": "
        << format_json_number(result.restricted_candidate_tail_score_gamma) << ",\n";
    out << "    \"candidate_tail_protection_size\": "
        << result.restricted_candidate_tail_protection_size << ",\n";
    out << "    \"candidate_scorer\": \""
        << json_escape_local(result.restricted_candidate_scorer) << "\",\n";
    out << "    \"candidate_scorer_weighted\": "
        << (result.restricted_candidate_scorer_weighted ? "true" : "false") << ",\n";
    out << "    \"candidate_score_map_hash\": \""
        << json_escape_local(result.restricted_candidate_score_map_hash) << "\",\n";
    out << "    \"initial_candidate_ids\": ";
    write_json_int_array(out, result.restricted_initial_candidate_ids);
    out << ",\n";
    out << "    \"initial_candidate_scores\": ";
    write_candidate_score_pair_array(out, result.restricted_initial_candidate_scores);
    out << ",\n";
    out << "    \"score_recomputations\": "
        << result.restricted_score_recomputations << ",\n";
    out << "    \"candidates_activated_by_score\": ";
    write_json_int_array(out, result.restricted_candidates_activated_by_score);
    out << ",\n";
    out << "    \"candidates_activated_by_full_fallback\": ";
    write_json_int_array(out, result.restricted_candidates_activated_by_full_fallback);
    out << ",\n";
    out << "    \"deactivation_enabled\": "
        << (result.restricted_candidate_deactivation_enabled ? "true" : "false") << ",\n";
    out << "    \"deactivation_rounds\": "
        << result.restricted_candidate_deactivation_rounds << ",\n";
    out << "    \"active_candidate_target\": "
        << result.restricted_candidate_active_target << ",\n";
    out << "    \"candidates_considered_for_deactivation\": "
        << result.restricted_candidate_considered_for_deactivation << ",\n";
    out << "    \"candidates_deactivated\": "
        << result.restricted_candidate_deactivated_total << ",\n";
    out << "    \"candidates_reactivated\": "
        << result.restricted_candidate_reactivated_total << ",\n";
    out << "    \"candidates_protected_from_deactivation\": "
        << result.restricted_candidate_protected_from_deactivation_total << ",\n";
    out << "    \"full_activation_overrode_maintenance\": "
        << (result.restricted_candidate_full_activation_overrode_maintenance ? "true" : "false") << ",\n";
    out << "    \"candidate_min_active_size\": "
        << result.restricted_candidate_min_active_size << ",\n";
    out << "    \"candidate_max_active_size\": "
        << result.restricted_candidate_max_active_size << ",\n";
    out << "    \"candidate_deactivation_batch_size\": "
        << result.restricted_candidate_deactivation_batch_size << ",\n";
    out << "    \"candidate_deactivation_min_age\": "
        << result.restricted_candidate_deactivation_min_age << ",\n";
    out << "    \"candidate_reactivation_cooldown_rounds\": "
        << result.restricted_candidate_reactivation_cooldown_rounds << ",\n";
    out << "    \"protect_selected_candidates\": "
        << (result.restricted_candidate_protect_selected_candidates ? "true" : "false") << ",\n";
    out << "    \"protected_selected_count\": "
        << result.restricted_candidate_protected_selected_count << ",\n";
    out << "    \"protected_min_age_count\": "
        << result.restricted_candidate_protected_min_age_count << ",\n";
    out << "    \"protected_cooldown_count\": "
        << result.restricted_candidate_protected_cooldown_count << ",\n";
    out << "    \"protected_newly_activated_count\": "
        << result.restricted_candidate_protected_newly_activated_count << ",\n";
    out << "    \"protected_tail_count\": "
        << result.restricted_candidate_protected_tail_count << ",\n";
    out << "    \"deactivation_blocked_by_tail_protection_count\": "
        << result.restricted_candidate_deactivation_blocked_by_tail_protection_count << ",\n";
    out << "    \"activated_by_tail_blend_count\": "
        << result.restricted_candidate_activated_by_tail_blend_count << ",\n";
    out << "    \"activated_tail_top_k_overlap\": "
        << result.restricted_candidate_activated_tail_top_k_overlap << ",\n";
    out << "    \"deactivated_tail_top_k_warning_count\": "
        << result.restricted_candidate_deactivated_tail_top_k_warning_count << ",\n";
    out << "    \"tail_protected_candidates_by_round\": ";
    write_nested_json_int_array(out, result.restricted_candidate_tail_protected_candidates_by_round);
    out << ",\n";
    out << "    \"tail_protected_count_by_round\": ";
    write_json_int_array(out, result.restricted_candidate_tail_protected_count_by_round);
    out << ",\n";
    out << "    \"deactivation_candidate_count\": "
        << result.restricted_candidate_deactivation_candidate_count << ",\n";
    out << "    \"reactivation_blocked_by_cooldown_count\": "
        << result.restricted_candidate_reactivation_blocked_by_cooldown_count << ",\n";
    out << "    \"oscillation_event_count\": "
        << result.restricted_candidate_oscillation_event_count << ",\n";
    out << "    \"max_candidate_state_changes\": "
        << result.restricted_candidate_max_state_changes << ",\n";
    out << "    \"average_candidate_state_changes\": "
        << format_json_number(result.restricted_candidate_average_state_changes) << ",\n";
    out << "    \"persistent_subproblems_enabled\": "
        << (result.restricted_candidate_persistent_subproblems_enabled ? "true" : "false") << ",\n";
    out << "    \"subproblem_model_build_count\": "
        << result.restricted_candidate_subproblem_model_build_count << ",\n";
    out << "    \"subproblem_fixed_y_update_count\": "
        << result.restricted_candidate_subproblem_fixed_y_update_count << ",\n";
    out << "    \"subproblem_solve_count\": "
        << result.restricted_candidate_subproblem_solve_count << ",\n";
    out << "    \"subproblem_model_rebuild_count\": "
        << result.restricted_candidate_subproblem_model_rebuild_count << ",\n";
    out << "    \"subproblem_total_build_time_seconds\": "
        << format_json_number(result.restricted_candidate_subproblem_total_build_time) << ",\n";
    out << "    \"subproblem_total_update_time_seconds\": "
        << format_json_number(result.restricted_candidate_subproblem_total_update_time) << ",\n";
    out << "    \"subproblem_total_solve_time_seconds\": "
        << format_json_number(result.restricted_candidate_subproblem_total_solve_time) << ",\n";
    out << "    \"subproblem_average_update_time_seconds\": "
        << format_json_number(result.restricted_candidate_subproblem_average_update_time) << ",\n";
    out << "    \"subproblem_average_solve_time_seconds\": "
        << format_json_number(result.restricted_candidate_subproblem_average_solve_time) << ",\n";
    out << "    \"persistent_master_enabled\": "
        << (result.restricted_candidate_persistent_master_enabled ? "true" : "false") << ",\n";
    out << "    \"master_model_build_count\": "
        << result.restricted_candidate_master_model_build_count << ",\n";
    out << "    \"master_model_rebuild_count\": "
        << result.restricted_candidate_master_model_rebuild_count << ",\n";
    out << "    \"master_bound_update_count\": "
        << result.restricted_candidate_master_bound_update_count << ",\n";
    out << "    \"master_cut_insertions\": "
        << result.restricted_candidate_master_cut_insertions << ",\n";
    out << "    \"master_duplicate_cut_insertions_skipped\": "
        << result.restricted_candidate_master_duplicate_cut_insertions_skipped << ",\n";
    out << "    \"master_total_build_time_seconds\": "
        << format_json_number(result.restricted_candidate_master_total_build_time) << ",\n";
    out << "    \"master_total_bound_update_time_seconds\": "
        << format_json_number(result.restricted_candidate_master_total_bound_update_time) << ",\n";
    out << "    \"master_total_cut_insertion_time_seconds\": "
        << format_json_number(result.restricted_candidate_master_total_cut_insertion_time) << ",\n";
    out << "    \"persistent_master_note\": \""
        << json_escape_local(result.restricted_candidate_persistent_master_note) << "\",\n";
    out << "    \"tail_score_diagnostics_enabled\": "
        << (result.restricted_candidate_tail_score_diagnostics_enabled ? "true" : "false") << ",\n";
    out << "    \"tail_score_diagnostics\": [\n";
    for (std::size_t i = 0; i < result.restricted_candidate_tail_score_diagnostics.size(); ++i) {
        const auto& diag = result.restricted_candidate_tail_score_diagnostics[i];
        out << "      {\n";
        out << "        \"round_index\": " << diag.round_index << ",\n";
        out << "        \"risk_measure\": \"" << json_escape_local(diag.risk_measure) << "\",\n";
        out << "        \"cvar_beta\": " << format_json_number(diag.cvar_beta) << ",\n";
        out << "        \"weighted\": " << (diag.weighted ? "true" : "false") << ",\n";
        out << "        \"weight_profile\": \""
            << json_escape_local(diag.weight_profile) << "\",\n";
        out << "        \"weight_map_hash\": \""
            << json_escape_local(diag.weight_map_hash) << "\",\n";
        out << "        \"risk_threshold\": " << format_json_number(diag.risk_threshold) << ",\n";
        out << "        \"tail_definition_used\": \""
            << json_escape_local(diag.tail_definition_used) << "\",\n";
        out << "        \"tail_scenario_count\": " << diag.tail_scenario_count << ",\n";
        out << "        \"tail_scenario_ids\": ";
        write_json_int_array(out, diag.tail_scenario_ids);
        out << ",\n";
        out << "        \"scenario_diagnostics\": [";
        for (std::size_t j = 0; j < diag.scenario_diagnostics.size(); ++j) {
            const auto& scenario = diag.scenario_diagnostics[j];
            out << (j == 0 ? "\n" : ",\n");
            out << "          {"
                << "\"scenario_id\":" << scenario.scenario_id << ","
                << "\"scenario_probability\":"
                << format_json_number(scenario.scenario_probability) << ","
                << "\"weighted_loss\":"
                << format_json_number(scenario.weighted_loss) << ","
                << "\"weighted_var_threshold\":"
                << format_json_number(scenario.weighted_var_threshold) << ","
                << "\"tail_membership\":"
                << (scenario.tail_membership ? "true" : "false") << ","
                << "\"tail_excess\":"
                << format_json_number(scenario.tail_excess)
                << "}";
        }
        if (!diag.scenario_diagnostics.empty()) {
            out << "\n        ";
        }
        out << "],\n";
        out << "        \"candidate_count\": " << diag.candidate_count << ",\n";
        out << "        \"active_count_before_round\": " << diag.active_count_before_round << ",\n";
        out << "        \"active_count_after_round\": " << diag.active_count_after_round << ",\n";
        out << "        \"activated_candidates\": ";
        write_json_int_array(out, diag.activated_candidates);
        out << ",\n";
        out << "        \"deactivated_candidates\": ";
        write_json_int_array(out, diag.deactivated_candidates);
        out << ",\n";
        out << "        \"selected_candidates\": ";
        write_json_int_array(out, diag.selected_candidates);
        out << ",\n";
        out << "        \"protected_selected_candidates\": ";
        write_json_int_array(out, diag.protected_selected_candidates);
        out << ",\n";
        out << "        \"top_generic_candidates\": ";
        write_tail_score_rank_array(out, diag.top_generic_candidates);
        out << ",\n";
        out << "        \"top_tail_empirical_candidates\": ";
        write_tail_score_rank_array(out, diag.top_tail_empirical_candidates);
        out << ",\n";
        out << "        \"top_tail_excess_candidates\": ";
        write_tail_score_rank_array(out, diag.top_tail_excess_candidates);
        out << ",\n";
        out << "        \"top_recent_tail_candidates\": ";
        write_tail_score_rank_array(out, diag.top_recent_tail_candidates);
        out << ",\n";
        out << "        \"top_tail_blend_candidates\": ";
        write_tail_score_rank_array(out, diag.top_tail_blend_candidates);
        out << ",\n";
        out << "        \"bottom_generic_active_candidates\": ";
        write_tail_score_rank_array(out, diag.bottom_generic_active_candidates);
        out << ",\n";
        out << "        \"bottom_tail_empirical_active_candidates\": ";
        write_tail_score_rank_array(out, diag.bottom_tail_empirical_active_candidates);
        out << ",\n";
        out << "        \"bottom_tail_excess_active_candidates\": ";
        write_tail_score_rank_array(out, diag.bottom_tail_excess_active_candidates);
        out << ",\n";
        out << "        \"top_k_overlap_generic_tail\": "
            << diag.top_k_overlap_generic_tail << ",\n";
        out << "        \"top_k_overlap_blend_tail\": "
            << diag.top_k_overlap_blend_tail << ",\n";
        out << "        \"top_k_overlap_blend_generic\": "
            << diag.top_k_overlap_blend_generic << ",\n";
        out << "        \"activated_tail_top_k_overlap\": "
            << diag.activated_tail_top_k_overlap << ",\n";
        out << "        \"deactivated_tail_bottom_k_overlap\": "
            << diag.deactivated_tail_bottom_k_overlap << ",\n";
        out << "        \"deactivated_tail_top_k_warning_count\": "
            << diag.deactivated_tail_top_k_warning_count << ",\n";
        out << "        \"deactivation_blocked_by_tail_protection_count\": "
            << diag.deactivation_blocked_by_tail_protection_count << ",\n";
        out << "        \"selected_tail_top_k_overlap\": "
            << diag.selected_tail_top_k_overlap << ",\n";
        out << "        \"spearman_generic_tail\": "
            << format_json_number(diag.spearman_generic_tail) << ",\n";
        out << "        \"pearson_generic_tail\": "
            << format_json_number(diag.pearson_generic_tail) << ",\n";
        out << "        \"candidate_events\": ";
        write_tail_score_candidate_events(out, diag.candidate_events);
        out << ",\n";
        out << "        \"notes\": ";
        write_json_string_array(out, diag.notes);
        out << "\n";
        out << "      }"
            << (i + 1 == result.restricted_candidate_tail_score_diagnostics.size() ? "\n" : ",\n");
    }
    out << "    ],\n";
    out << "    \"round_log\": [\n";
    for (std::size_t i = 0; i < result.restricted_candidate_round_log.size(); ++i) {
        const auto& log = result.restricted_candidate_round_log[i];
        out << "      {\n";
        out << "        \"round_index\": " << log.round_index << ",\n";
        out << "        \"stage_index\": " << log.round_index << ",\n";
        out << "        \"stage_type\": \"" << json_escape_local(log.stage_type) << "\",\n";
        out << "        \"risk_measure\": \"" << json_escape_local(log.risk_measure) << "\",\n";
        out << "        \"active_candidate_count\": " << log.active_candidate_count << ",\n";
        out << "        \"active_candidate_fraction\": "
            << format_json_number(log.active_candidate_fraction) << ",\n";
        out << "        \"activation_policy\": \""
            << json_escape_local(log.activation_policy) << "\",\n";
        out << "        \"newly_activated_candidates\": ";
        write_json_int_array(out, log.newly_activated_candidates);
        out << ",\n";
        out << "        \"deactivated_candidates\": ";
        write_json_int_array(out, log.deactivated_candidates);
        out << ",\n";
        out << "        \"active_count_before_maintenance\": "
            << log.active_count_before_maintenance << ",\n";
        out << "        \"active_count_after_activation\": "
            << log.active_count_after_activation << ",\n";
        out << "        \"active_count_after_deactivation\": "
            << log.active_count_after_deactivation << ",\n";
        out << "        \"protected_selected_count\": "
            << log.protected_selected_count << ",\n";
        out << "        \"protected_min_age_count\": "
            << log.protected_min_age_count << ",\n";
        out << "        \"protected_cooldown_count\": "
            << log.protected_cooldown_count << ",\n";
        out << "        \"protected_newly_activated_count\": "
            << log.protected_newly_activated_count << ",\n";
        out << "        \"protected_tail_count\": "
            << log.protected_tail_count << ",\n";
        out << "        \"deactivation_candidate_count\": "
            << log.deactivation_candidate_count << ",\n";
        out << "        \"reactivation_blocked_by_cooldown_count\": "
            << log.reactivation_blocked_by_cooldown_count << ",\n";
        out << "        \"oscillation_event_count\": "
            << log.oscillation_event_count << ",\n";
        out << "        \"selected_candidates_protected\": ";
        write_json_int_array(out, log.selected_candidates_protected);
        out << ",\n";
        out << "        \"tail_protected_candidates\": ";
        write_json_int_array(out, log.tail_protected_candidates);
        out << ",\n";
        out << "        \"candidate_score_mode\": \""
            << json_escape_local(log.candidate_score_mode) << "\",\n";
        out << "        \"candidate_tail_score_gamma\": "
            << format_json_number(log.candidate_tail_score_gamma) << ",\n";
        out << "        \"candidate_tail_protection_size\": "
            << log.candidate_tail_protection_size << ",\n";
        out << "        \"top_blend_candidates\": ";
        write_candidate_score_pair_array(out, log.top_blend_candidates);
        out << ",\n";
        out << "        \"top_generic_candidates_for_score_mode\": ";
        write_candidate_score_pair_array(out, log.top_generic_candidates_for_score_mode);
        out << ",\n";
        out << "        \"top_tail_candidates\": ";
        write_candidate_score_pair_array(out, log.top_tail_candidates);
        out << ",\n";
        out << "        \"top_blend_tail_overlap\": "
            << log.top_blend_tail_overlap << ",\n";
        out << "        \"top_blend_generic_overlap\": "
            << log.top_blend_generic_overlap << ",\n";
        out << "        \"activated_tail_top_k_overlap\": "
            << log.activated_tail_top_k_overlap << ",\n";
        out << "        \"deactivated_tail_top_k_warning_count\": "
            << log.deactivated_tail_top_k_warning_count << ",\n";
        out << "        \"solve_status\": \"" << json_escape_local(log.solve_status) << "\",\n";
        out << "        \"status\": \"" << json_escape_local(log.solve_status) << "\",\n";
        out << "        \"objective\": " << format_json_number(log.objective) << ",\n";
        out << "        \"best_bound\": " << format_json_number(log.best_bound) << ",\n";
        out << "        \"mip_gap\": " << format_json_number(log.mip_gap) << ",\n";
        out << "        \"runtime_seconds\": " << format_json_number(log.runtime_seconds) << ",\n";
        out << "        \"time_limit_seconds\": " << format_json_number(log.time_limit_seconds) << ",\n";
        out << "        \"remaining_global_time_before_stage\": "
            << format_json_number(log.remaining_global_time_before_stage) << ",\n";
        out << "        \"remaining_global_time_after_stage\": "
            << format_json_number(log.remaining_global_time_after_stage) << ",\n";
        out << "        \"cuts_added\": " << log.cuts_added << ",\n";
        out << "        \"lazy_cuts_added\": " << log.cuts_added << ",\n";
        out << "        \"candidate_incumbents_checked\": "
            << log.candidate_incumbents_checked << ",\n";
        out << "        \"subproblem_solves\": " << log.subproblem_solves << ",\n";
        out << "        \"callback_time_seconds\": "
            << format_json_number(log.callback_time_seconds) << ",\n";
        out << "        \"subproblem_time_seconds\": "
            << format_json_number(log.subproblem_time_seconds) << ",\n";
        out << "        \"cut_pool_size_before_stage\": "
            << log.cut_pool_size_before_stage << ",\n";
        out << "        \"cuts_inserted_from_pool\": "
            << log.cuts_reused_at_stage << ",\n";
        out << "        \"cuts_reused_at_stage\": " << log.cuts_reused_at_stage << ",\n";
        out << "        \"new_cuts_added_to_pool\": "
            << log.new_cuts_added_to_pool << ",\n";
        out << "        \"cut_pool_size_after_stage\": "
            << (log.cut_pool_size_before_stage + log.new_cuts_added_to_pool) << ",\n";
        out << "        \"duplicate_cuts_skipped\": "
            << log.duplicate_cuts_skipped << ",\n";
        out << "        \"selected_firebreaks\": ";
        write_json_int_array(out, log.selected_firebreaks);
        out << ",\n";
        out << "        \"max_cut_violation\": "
            << format_json_number(log.max_cut_violation) << "\n";
        out << "      }" << (i + 1 == result.restricted_candidate_round_log.size() ? "\n" : ",\n");
    }
    out << "    ]\n";
    out << "  }";
}

}  // namespace

std::string current_timestamp_utc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm utc_tm{};
#if defined(_WIN32)
    gmtime_s(&utc_tm, &now_time);
#else
    gmtime_r(&now_time, &utc_tm);
#endif
    std::ostringstream out;
    out << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

void write_experiment_result_json(
    const std::filesystem::path& output_path,
    const StandardExperimentResult& result) {
    ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open experiment JSON file: " + output_path.string());
    }

    out << "{\n";
    out << "  \"experiment_id\": \"" << json_escape_local(result.experiment_id) << "\",\n";
    out << "  \"case_id\": " << result.case_id << ",\n";
    out << "  \"run_id\": \"" << json_escape_local(result.run_id) << "\",\n";
    out << "  \"timestamp\": \"" << json_escape_local(result.timestamp) << "\",\n";
    out << "  \"landscape\": \"" << json_escape_local(result.landscape) << "\",\n";
    out << "  \"method\": \"" << json_escape_local(result.method) << "\",\n";
    out << "  \"objective_metric\": \"" << json_escape_local(result.objective_metric) << "\",\n";
    out << "  \"alpha\": " << format_json_number(result.alpha) << ",\n";
    out << "  \"budget\": " << result.budget << ",\n";
    out << "  \"train_scenario_count\": " << result.train_scenario_count << ",\n";
    out << "  \"test_scenario_count\": " << result.test_scenario_count << ",\n";
    out << "  \"train_ids\": ";
    write_json_int_array(out, result.train_ids);
    out << ",\n";
    out << "  \"test_ids\": ";
    write_json_int_array(out, result.test_ids);
    out << ",\n";
    out << "  \"solver_status\": \"" << json_escape_local(result.solver_status) << "\",\n";
    out << "  \"objective_in_sample\": " << format_json_number(result.objective_in_sample) << ",\n";
    out << "  \"best_bound\": " << format_json_number(result.best_bound) << ",\n";
    out << "  \"mip_gap\": " << format_json_number(result.mip_gap) << ",\n";
    out << "  \"runtime_seconds\": " << format_json_number(result.runtime_seconds) << ",\n";
    out << "  \"solver_status_code\": " << result.solver_status_code << ",\n";
    out << "  \"explored_nodes\": " << result.explored_nodes << ",\n";
    out << "  \"num_variables\": " << result.num_variables << ",\n";
    out << "  \"num_constraints\": " << result.num_constraints << ",\n";
    out << "  \"solver_iterations\": " << result.solver_iterations << ",\n";
    out << "  \"cuts_added\": " << result.cuts_added << ",\n";
    out << "  \"max_cut_violation\": " << format_json_number(result.max_cut_violation) << ",\n";
    out << "  \"combinatorial_benders_enabled\": "
        << (result.combinatorial_benders_enabled ? "true" : "false") << ",\n";
    out << "  \"combinatorial_benders_lift_mode\": \""
        << json_escape_local(result.combinatorial_benders_lift_mode) << "\",\n";
    out << "  \"combinatorial_benders_scenario_order\": \""
        << json_escape_local(result.combinatorial_benders_scenario_order) << "\",\n";
    out << "  \"combinatorial_benders_cut_sampling_ratio\": "
        << format_json_number(result.combinatorial_benders_cut_sampling_ratio) << ",\n";
    out << "  \"combinatorial_benders_fractional_separation_enabled\": "
        << (result.combinatorial_benders_fractional_separation_enabled ? "true" : "false")
        << ",\n";
    out << "  \"combinatorial_benders_initial_cuts_enabled\": "
        << (result.combinatorial_benders_initial_cuts_enabled ? "true" : "false") << ",\n";
    out << "  \"combinatorial_benders_integer_cuts_added\": "
        << result.combinatorial_benders_integer_cuts_added << ",\n";
    out << "  \"combinatorial_benders_fractional_cuts_added\": "
        << result.combinatorial_benders_fractional_cuts_added << ",\n";
    out << "  \"combinatorial_benders_initial_cuts_added\": "
        << result.combinatorial_benders_initial_cuts_added << ",\n";
    out << "  \"combinatorial_benders_scenarios_checked\": "
        << result.combinatorial_benders_scenarios_checked << ",\n";
    out << "  \"combinatorial_benders_separation_time_sec\": "
        << format_json_number(result.combinatorial_benders_separation_time_sec) << ",\n";
    out << "  \"combinatorial_benders_avg_paths_per_cut\": "
        << format_json_number(result.combinatorial_benders_avg_paths_per_cut) << ",\n";
    out << "  \"combinatorial_benders_avg_cut_nonzeros\": "
        << format_json_number(result.combinatorial_benders_avg_cut_nonzeros) << ",\n";
    out << "  \"combinatorial_benders_num_violated_cuts\": "
        << result.combinatorial_benders_num_violated_cuts << ",\n";
    out << "  \"combinatorial_benders_weighted\": "
        << (result.combinatorial_benders_weighted ? "true" : "false") << ",\n";
    out << "  \"combinatorial_benders_mode\": \""
        << json_escape_local(result.combinatorial_benders_mode) << "\",\n";
    out << "  \"combinatorial_benders_weight_map_hash\": \""
        << json_escape_local(result.combinatorial_benders_weight_map_hash) << "\",\n";
    out << "  \"combinatorial_benders_weighted_recourse_evaluations\": "
        << result.combinatorial_benders_weighted_recourse_evaluations << ",\n";
    out << "  \"combinatorial_benders_duplicate_cuts\": "
        << result.combinatorial_benders_duplicate_cuts << ",\n";
    out << "  \"combinatorial_benders_cuts_tight_at_incumbent\": "
        << result.combinatorial_benders_cuts_tight_at_incumbent << ",\n";
    out << "  \"combinatorial_benders_lifting_enabled\": "
        << (result.combinatorial_benders_lifting_enabled ? "true" : "false") << ",\n";
    out << "  \"combinatorial_benders_scenario_sampling_enabled\": "
        << (result.combinatorial_benders_scenario_sampling_enabled ? "true" : "false") << ",\n";
    out << "  \"combinatorial_benders_max_tightness_error\": "
        << format_json_number(result.combinatorial_benders_max_tightness_error) << ",\n";
    out << "  \"combinatorial_benders_max_violation\": "
        << format_json_number(result.combinatorial_benders_max_violation) << ",\n";
    out << "  \"combinatorial_benders_propagation_time_sec\": "
        << format_json_number(result.combinatorial_benders_propagation_time_sec) << ",\n";
    out << "  \"combinatorial_benders_cut_build_time_sec\": "
        << format_json_number(result.combinatorial_benders_cut_build_time_sec) << ",\n";
    out << "  \"combinatorial_benders_validity_mode\": \""
        << json_escape_local(result.combinatorial_benders_validity_mode) << "\",\n";
    out << "  \"combinatorial_weighted\": "
        << (result.combinatorial_weighted ? "true" : "false") << ",\n";
    out << "  \"combinatorial_mode\": \""
        << json_escape_local(result.combinatorial_mode) << "\",\n";
    out << "  \"combinatorial_weight_map_hash\": \""
        << json_escape_local(result.combinatorial_weight_map_hash) << "\",\n";
    out << "  \"combinatorial_candidate_callbacks\": "
        << result.combinatorial_candidate_callbacks << ",\n";
    out << "  \"combinatorial_scenarios_evaluated\": "
        << result.combinatorial_scenarios_evaluated << ",\n";
    out << "  \"combinatorial_weighted_recourse_evaluations\": "
        << result.combinatorial_weighted_recourse_evaluations << ",\n";
    out << "  \"combinatorial_cuts_generated\": "
        << result.combinatorial_cuts_generated << ",\n";
    out << "  \"combinatorial_cuts_added\": "
        << result.combinatorial_cuts_added << ",\n";
    out << "  \"combinatorial_duplicate_cuts\": "
        << result.combinatorial_duplicate_cuts << ",\n";
    out << "  \"combinatorial_cuts_tight_at_incumbent\": "
        << result.combinatorial_cuts_tight_at_incumbent << ",\n";
    out << "  \"combinatorial_max_tightness_error\": "
        << format_json_number(result.combinatorial_max_tightness_error) << ",\n";
    out << "  \"combinatorial_max_violation\": "
        << format_json_number(result.combinatorial_max_violation) << ",\n";
    out << "  \"combinatorial_propagation_time_sec\": "
        << format_json_number(result.combinatorial_propagation_time_sec) << ",\n";
    out << "  \"combinatorial_cut_build_time_sec\": "
        << format_json_number(result.combinatorial_cut_build_time_sec) << ",\n";
    out << "  \"combinatorial_callback_time_sec\": "
        << format_json_number(result.combinatorial_callback_time_sec) << ",\n";
    out << "  \"combinatorial_validity_mode\": \""
        << json_escape_local(result.combinatorial_validity_mode) << "\",\n";
    out << "  \"combinatorial_lifting_enabled\": "
        << (result.combinatorial_lifting_enabled ? "true" : "false") << ",\n";
    out << "  \"combinatorial_fractional_cuts_enabled\": "
        << (result.combinatorial_fractional_cuts_enabled ? "true" : "false")
        << ",\n";
    out << "  \"combinatorial_initial_cuts_enabled\": "
        << (result.combinatorial_initial_cuts_enabled ? "true" : "false") << ",\n";
    out << "  \"combinatorial_scenario_sampling_enabled\": "
        << (result.combinatorial_scenario_sampling_enabled ? "true" : "false")
        << ",\n";
    out << "  \"coverage_llbi_enabled\": " << (result.coverage_llbi_enabled ? "true" : "false") << ",\n";
    out << "  \"coverage_llbi_num_zeta_vars\": " << result.coverage_llbi_num_zeta_vars << ",\n";
    out << "  \"coverage_llbi_num_constraints\": " << result.coverage_llbi_num_constraints << ",\n";
    out << "  \"coverage_llbi_precompute_time_sec\": "
        << format_json_number(result.coverage_llbi_precompute_time_sec) << ",\n";
    out << "  \"coverage_llbi_weighted\": "
        << (result.coverage_llbi_weighted ? "true" : "false") << ",\n";
    out << "  \"coverage_llbi_weight_map_hash\": \""
        << json_escape_local(result.coverage_llbi_weight_map_hash) << "\",\n";
    out << "  \"coverage_llbi_scenarios_precomputed\": "
        << result.coverage_llbi_scenarios_precomputed << ",\n";
    out << "  \"coverage_llbi_baseline_cells\": "
        << result.coverage_llbi_baseline_cells << ",\n";
    out << "  \"coverage_llbi_auxiliary_variables\": "
        << result.coverage_llbi_auxiliary_variables << ",\n";
    out << "  \"coverage_llbi_linking_constraints\": "
        << result.coverage_llbi_linking_constraints << ",\n";
    out << "  \"coverage_llbi_loss_constraints\": "
        << result.coverage_llbi_loss_constraints << ",\n";
    out << "  \"coverage_llbi_nonempty_coverage_sets\": "
        << result.coverage_llbi_nonempty_coverage_sets << ",\n";
    out << "  \"coverage_llbi_total_incidence_terms\": "
        << result.coverage_llbi_total_incidence_terms << ",\n";
    out << "  \"coverage_llbi_build_time_sec\": "
        << format_json_number(result.coverage_llbi_build_time_sec) << ",\n";
    out << "  \"coverage_llbi_validity_mode\": \""
        << json_escape_local(result.coverage_llbi_validity_mode) << "\",\n";
    out << "  \"path_llbi_enabled\": " << (result.path_llbi_enabled ? "true" : "false") << ",\n";
    out << "  \"path_llbi_num_b_vars\": " << result.path_llbi_num_b_vars << ",\n";
    out << "  \"path_llbi_num_path_constraints\": " << result.path_llbi_num_path_constraints << ",\n";
    out << "  \"path_llbi_num_paths_used\": " << result.path_llbi_num_paths_used << ",\n";
    out << "  \"path_llbi_weighted\": "
        << (result.path_llbi_weighted ? "true" : "false") << ",\n";
    out << "  \"path_llbi_weight_map_hash\": \""
        << json_escape_local(result.path_llbi_weight_map_hash) << "\",\n";
    out << "  \"path_llbi_scenarios_precomputed\": "
        << result.path_llbi_scenarios_precomputed << ",\n";
    out << "  \"path_llbi_baseline_nodes\": "
        << result.path_llbi_baseline_nodes << ",\n";
    out << "  \"path_llbi_auxiliary_variables\": "
        << result.path_llbi_auxiliary_variables << ",\n";
    out << "  \"path_llbi_path_constraints\": "
        << result.path_llbi_path_constraints << ",\n";
    out << "  \"path_llbi_loss_constraints\": "
        << result.path_llbi_loss_constraints << ",\n";
    out << "  \"path_llbi_total_paths\": " << result.path_llbi_total_paths << ",\n";
    out << "  \"path_llbi_total_candidate_incidence_terms\": "
        << result.path_llbi_total_candidate_incidence_terms << ",\n";
    out << "  \"path_llbi_nodes_without_paths\": "
        << result.path_llbi_nodes_without_paths << ",\n";
    out << "  \"path_llbi_path_enumeration_complete\": "
        << (result.path_llbi_path_enumeration_complete ? "true" : "false") << ",\n";
    out << "  \"path_llbi_paths_truncated\": "
        << result.path_llbi_paths_truncated << ",\n";
    out << "  \"path_llbi_precompute_time_sec\": "
        << format_json_number(result.path_llbi_precompute_time_sec) << ",\n";
    out << "  \"path_llbi_build_time_sec\": "
        << format_json_number(result.path_llbi_build_time_sec) << ",\n";
    out << "  \"path_llbi_validity_mode\": \""
        << json_escape_local(result.path_llbi_validity_mode) << "\",\n";
    out << "  \"projected_coverage_llbi_enabled\": "
        << (result.projected_coverage_llbi_enabled ? "true" : "false") << ",\n";
    out << "  \"projected_path_llbi_enabled\": "
        << (result.projected_path_llbi_enabled ? "true" : "false") << ",\n";
    out << "  \"projected_llbi_family\": \""
        << json_escape_local(result.projected_llbi_family) << "\",\n";
    out << "  \"projected_llbi_strategy\": \""
        << json_escape_local(result.projected_llbi_strategy) << "\",\n";
    out << "  \"projected_llbi_mode\": \""
        << json_escape_local(result.projected_llbi_mode) << "\",\n";
    out << "  \"projected_llbi_root_rounds\": "
        << result.projected_llbi_root_rounds << ",\n";
    out << "  \"projected_llbi_cuts_added\": "
        << result.projected_llbi_cuts_added << ",\n";
    out << "  \"projected_llbi_coverage_cuts_added\": "
        << result.projected_llbi_coverage_cuts_added << ",\n";
    out << "  \"projected_llbi_path_cuts_added\": "
        << result.projected_llbi_path_cuts_added << ",\n";
    out << "  \"projected_llbi_violated_cuts_found\": "
        << result.projected_llbi_violated_cuts_found << ",\n";
    out << "  \"projected_llbi_separation_time_sec\": "
        << format_json_number(result.projected_llbi_separation_time_sec) << ",\n";
    out << "  \"projected_llbi_solve_time_sec\": "
        << format_json_number(result.projected_llbi_solve_time_sec) << ",\n";
    out << "  \"projected_llbi_total_time_sec\": "
        << format_json_number(result.projected_llbi_total_time_sec) << ",\n";
    out << "  \"projected_llbi_total_nonzeros\": "
        << result.projected_llbi_total_nonzeros << ",\n";
    out << "  \"projected_llbi_avg_nonzeros_per_cut\": "
        << format_json_number(result.projected_llbi_avg_nonzeros_per_cut) << ",\n";
    out << "  \"projected_llbi_max_nonzeros_per_cut\": "
        << result.projected_llbi_max_nonzeros_per_cut << ",\n";
    out << "  \"projected_llbi_min_violation\": "
        << format_json_number(result.projected_llbi_min_violation) << ",\n";
    out << "  \"projected_llbi_max_violation\": "
        << format_json_number(result.projected_llbi_max_violation) << ",\n";
    out << "  \"projected_llbi_avg_violation\": "
        << format_json_number(result.projected_llbi_avg_violation) << ",\n";
    out << "  \"projected_llbi_root_bound_initial\": "
        << format_json_number(result.projected_llbi_root_bound_initial) << ",\n";
    out << "  \"projected_llbi_root_bound_final\": "
        << format_json_number(result.projected_llbi_root_bound_final) << ",\n";
    out << "  \"projected_llbi_root_bound_improvement_abs\": "
        << format_json_number(result.projected_llbi_root_bound_improvement_abs) << ",\n";
    out << "  \"projected_llbi_root_bound_improvement_pct\": "
        << format_json_number(result.projected_llbi_root_bound_improvement_pct) << ",\n";
    out << "  \"projected_poly_candidate_cuts_generated\": "
        << result.projected_poly_candidate_cuts_generated << ",\n";
    out << "  \"projected_poly_candidate_cuts_added\": "
        << result.projected_poly_candidate_cuts_added << ",\n";
    out << "  \"projected_poly_enumeration_truncated\": "
        << (result.projected_poly_enumeration_truncated ? "true" : "false") << ",\n";
    out << "  \"projected_poly_enumeration_limit\": "
        << result.projected_poly_enumeration_limit << ",\n";
    out << "  \"projected_exp_separated_cuts_added\": "
        << result.projected_exp_separated_cuts_added << ",\n";
    out << "  \"projected_exp_separation_rounds\": "
        << result.projected_exp_separation_rounds << ",\n";
    out << "  \"projected_exp_candidate_cuts_generated\": "
        << result.projected_exp_candidate_cuts_generated << ",\n";
    out << "  \"projected_exp_candidate_cuts_added\": "
        << result.projected_exp_candidate_cuts_added << ",\n";
    out << "  \"projected_exp_enumeration_truncated\": "
        << (result.projected_exp_enumeration_truncated ? "true" : "false") << ",\n";
    out << "  \"projected_exp_enumeration_limit\": "
        << result.projected_exp_enumeration_limit << ",\n";
    out << "  \"projected_coverage_llbi_weighted\": "
        << (result.projected_coverage_llbi_weighted ? "true" : "false") << ",\n";
    out << "  \"projected_coverage_llbi_mode\": \""
        << json_escape_local(result.projected_coverage_llbi_mode) << "\",\n";
    out << "  \"projected_coverage_llbi_weight_map_hash\": \""
        << json_escape_local(result.projected_coverage_llbi_weight_map_hash) << "\",\n";
    out << "  \"projected_coverage_llbi_scenarios_precomputed\": "
        << result.projected_coverage_llbi_scenarios_precomputed << ",\n";
    out << "  \"projected_coverage_llbi_baseline_cells\": "
        << result.projected_coverage_llbi_baseline_cells << ",\n";
    out << "  \"projected_coverage_llbi_nonempty_coverage_sets\": "
        << result.projected_coverage_llbi_nonempty_coverage_sets << ",\n";
    out << "  \"projected_coverage_llbi_total_incidence_terms\": "
        << result.projected_coverage_llbi_total_incidence_terms << ",\n";
    out << "  \"projected_coverage_llbi_separation_calls\": "
        << result.projected_coverage_llbi_separation_calls << ",\n";
    out << "  \"projected_coverage_llbi_cuts_generated\": "
        << result.projected_coverage_llbi_cuts_generated << ",\n";
    out << "  \"projected_coverage_llbi_cuts_added\": "
        << result.projected_coverage_llbi_cuts_added << ",\n";
    out << "  \"projected_coverage_llbi_duplicate_cuts\": "
        << result.projected_coverage_llbi_duplicate_cuts << ",\n";
    out << "  \"projected_coverage_llbi_max_violation\": "
        << format_json_number(result.projected_coverage_llbi_max_violation) << ",\n";
    out << "  \"projected_coverage_llbi_precompute_time_sec\": "
        << format_json_number(result.projected_coverage_llbi_precompute_time_sec) << ",\n";
    out << "  \"projected_coverage_llbi_separation_time_sec\": "
        << format_json_number(result.projected_coverage_llbi_separation_time_sec) << ",\n";
    out << "  \"projected_coverage_llbi_validity_mode\": \""
        << json_escape_local(result.projected_coverage_llbi_validity_mode) << "\",\n";
    out << "  \"projected_path_llbi_weighted\": "
        << (result.projected_path_llbi_weighted ? "true" : "false") << ",\n";
    out << "  \"projected_path_llbi_mode\": \""
        << json_escape_local(result.projected_path_llbi_mode) << "\",\n";
    out << "  \"projected_path_llbi_weight_map_hash\": \""
        << json_escape_local(result.projected_path_llbi_weight_map_hash) << "\",\n";
    out << "  \"projected_path_llbi_scenarios_precomputed\": "
        << result.projected_path_llbi_scenarios_precomputed << ",\n";
    out << "  \"projected_path_llbi_destination_nodes\": "
        << result.projected_path_llbi_destination_nodes << ",\n";
    out << "  \"projected_path_llbi_total_paths\": "
        << result.projected_path_llbi_total_paths << ",\n";
    out << "  \"projected_path_llbi_total_incidence_terms\": "
        << result.projected_path_llbi_total_incidence_terms << ",\n";
    out << "  \"projected_path_llbi_nodes_without_paths\": "
        << result.projected_path_llbi_nodes_without_paths << ",\n";
    out << "  \"projected_path_llbi_enumeration_complete\": "
        << (result.projected_path_llbi_enumeration_complete ? "true" : "false") << ",\n";
    out << "  \"projected_path_llbi_paths_truncated\": "
        << result.projected_path_llbi_paths_truncated << ",\n";
    out << "  \"projected_path_llbi_separation_calls\": "
        << result.projected_path_llbi_separation_calls << ",\n";
    out << "  \"projected_path_llbi_cuts_generated\": "
        << result.projected_path_llbi_cuts_generated << ",\n";
    out << "  \"projected_path_llbi_cuts_added\": "
        << result.projected_path_llbi_cuts_added << ",\n";
    out << "  \"projected_path_llbi_duplicate_cuts\": "
        << result.projected_path_llbi_duplicate_cuts << ",\n";
    out << "  \"projected_path_llbi_max_violation\": "
        << format_json_number(result.projected_path_llbi_max_violation) << ",\n";
    out << "  \"projected_path_llbi_precompute_time_sec\": "
        << format_json_number(result.projected_path_llbi_precompute_time_sec) << ",\n";
    out << "  \"projected_path_llbi_separation_time_sec\": "
        << format_json_number(result.projected_path_llbi_separation_time_sec) << ",\n";
    out << "  \"projected_path_llbi_validity_mode\": \""
        << json_escape_local(result.projected_path_llbi_validity_mode) << "\",\n";
    out << "  \"global_dominance_enabled\": " << (result.global_dominance_enabled ? "true" : "false") << ",\n";
    out << "  \"global_dominance_structural_weight_safe\": "
        << (result.global_dominance_structural_weight_safe ? "true" : "false") << ",\n";
    out << "  \"global_dominance_original_candidate_count\": "
        << result.global_dominance_original_candidate_count << ",\n";
    out << "  \"global_dominance_candidates_removed\": "
        << result.global_dominance_candidates_removed << ",\n";
    out << "  \"global_dominance_equivalence_classes\": "
        << result.global_dominance_equivalence_classes << ",\n";
    out << "  \"global_dominance_post_candidate_count\": "
        << result.global_dominance_post_candidate_count << ",\n";
    out << "  \"global_dominance_warm_start_replacements\": "
        << result.global_dominance_warm_start_replacements << ",\n";
    out << "  \"global_dominance_precompute_time_sec\": "
        << format_json_number(result.global_dominance_precompute_time_sec) << ",\n";
    out << "  \"conditional_zero_benefit_enabled\": "
        << (result.conditional_zero_benefit_enabled ? "true" : "false") << ",\n";
    out << "  \"conditional_zero_benefit_structural_weight_safe\": "
        << (result.conditional_zero_benefit_structural_weight_safe ? "true" : "false") << ",\n";
    out << "  \"conditional_zero_benefit_callback_calls\": "
        << result.conditional_zero_benefit_callback_calls << ",\n";
    out << "  \"conditional_zero_benefit_nodes_checked\": "
        << result.conditional_zero_benefit_nodes_checked << ",\n";
    out << "  \"conditional_zero_benefit_candidates_checked\": "
        << result.conditional_zero_benefit_candidates_checked << ",\n";
    out << "  \"conditional_zero_benefit_fixings_attempted\": "
        << result.conditional_zero_benefit_fixings_attempted << ",\n";
    out << "  \"conditional_zero_benefit_fixings_applied\": "
        << result.conditional_zero_benefit_fixings_applied << ",\n";
    out << "  \"conditional_zero_benefit_variables_fixed_zero\": "
        << result.conditional_zero_benefit_variables_fixed_zero << ",\n";
    out << "  \"conditional_zero_benefit_scenarios_reachability_computed\": "
        << result.conditional_zero_benefit_scenarios_reachability_computed << ",\n";
    out << "  \"conditional_zero_benefit_time_sec\": "
        << format_json_number(result.conditional_zero_benefit_time_sec) << ",\n";
    if (has_benders_diagnostics(result)) {
        write_benders_json_block(out, result);
        out << ",\n";
    }
    if (has_branch_benders_diagnostics(result)) {
        write_branch_benders_json_block(out, result);
        out << ",\n";
    }
    if (has_restricted_candidate_diagnostics(result)) {
        write_restricted_candidate_json_block(out, result);
        out << ",\n";
    }
    out << "  \"fpp_mode\": \"" << json_escape_local(result.fpp_mode) << "\",\n";
    out << "  \"formulation\": \"" << json_escape_local(result.formulation) << "\",\n";
    out << "  \"dominator_cuts_enabled\": " << (result.dominator_cuts_enabled ? "true" : "false") << ",\n";
    out << "  \"separator_cuts_enabled\": " << (result.separator_cuts_enabled ? "true" : "false") << ",\n";
    out << "  \"greedy_warm_start_enabled\": " << (result.greedy_warm_start_enabled ? "true" : "false") << ",\n";
    out << "  \"local_search_enabled\": " << (result.local_search_enabled ? "true" : "false") << ",\n";
    out << "  \"separator_cuts_added\": " << result.separator_cuts_added << ",\n";
    out << "  \"separator_min_cut_calls\": " << result.separator_min_cut_calls << ",\n";
    out << "  \"separator_callback_invocations\": " << result.separator_callback_invocations << ",\n";
    out << "  \"separator_duplicate_cuts_skipped\": " << result.separator_duplicate_cuts_skipped << ",\n";
    out << "  \"separator_large_cuts_skipped\": " << result.separator_large_cuts_skipped << ",\n";
    out << "  \"separator_time_sec\": " << format_json_number(result.separator_time_sec) << ",\n";
    out << "  \"dominator_cuts_added\": " << result.dominator_cuts_added << ",\n";
    out << "  \"dominator_aggregate_cuts_added\": " << result.dominator_aggregate_cuts_added << ",\n";
    out << "  \"dominator_individual_cuts_added\": " << result.dominator_individual_cuts_added << ",\n";
    out << "  \"dominator_dag_scenarios\": " << result.dominator_dag_scenarios << ",\n";
    out << "  \"dominator_fallback_scenarios\": " << result.dominator_fallback_scenarios << ",\n";
    out << "  \"dominator_preprocessing_time_sec\": "
        << format_json_number(result.dominator_preprocessing_time_sec) << ",\n";
    out << "  \"heuristic_time_sec\": " << format_json_number(result.heuristic_time_sec) << ",\n";
    out << "  \"heuristic_objective\": " << format_json_number(result.heuristic_objective) << ",\n";
    out << "  \"heuristic_exact_evaluations\": " << result.heuristic_exact_evaluations << ",\n";
    out << "  \"heuristic_selected_count\": " << result.heuristic_selected_count << ",\n";
    out << "  \"compact_node_count\": " << result.compact_node_count << ",\n";
    out << "  \"eligible_node_count\": " << result.eligible_node_count << ",\n";
    out << "  \"total_observed_scenario_nodes\": " << result.total_observed_scenario_nodes << ",\n";
    out << "  \"total_scenario_arcs\": " << result.total_scenario_arcs << ",\n";
    out << "  \"evaluator_objective\": " << format_json_number(result.evaluator_objective) << ",\n";
    out << "  \"evaluator_abs_diff\": " << format_json_number(result.evaluator_abs_diff) << ",\n";
    out << "  \"evaluator_rel_diff\": " << format_json_number(result.evaluator_rel_diff) << ",\n";
    out << "  \"weight_profile\": \"" << json_escape_local(result.weight_profile) << "\",\n";
    out << "  \"weight_map_file\": \"" << json_escape_local(result.weight_map_file) << "\",\n";
    out << "  \"weight_map_hash\": \"" << json_escape_local(result.weight_map_hash) << "\",\n";
    out << "  \"weight_normalized\": " << (result.weight_normalized ? "true" : "false") << ",\n";
    out << "  \"weight_mean\": " << format_json_number(result.weight_mean) << ",\n";
    out << "  \"weight_min\": " << format_json_number(result.weight_min) << ",\n";
    out << "  \"weight_max\": " << format_json_number(result.weight_max) << ",\n";
    out << "  \"weight_total\": " << format_json_number(result.weight_total) << ",\n";
    out << "  \"solver_weighted_objective\": "
        << format_json_number(result.solver_weighted_objective) << ",\n";
    out << "  \"evaluator_weighted_objective\": "
        << format_json_number(result.evaluator_weighted_objective) << ",\n";
    out << "  \"objective_validation_abs_difference\": "
        << format_json_number(result.objective_validation_abs_difference) << ",\n";
    out << "  \"objective_validation_rel_difference\": "
        << format_json_number(result.objective_validation_rel_difference) << ",\n";
    out << "  \"objective_validation_passed\": "
        << (result.objective_validation_passed ? "true" : "false") << ",\n";
    out << "  \"risk_measure\": \"" << json_escape_local(result.risk_measure) << "\",\n";
    out << "  \"cvar_beta\": " << format_json_number(result.cvar_beta) << ",\n";
    out << "  \"cvar_lambda\": " << format_json_number(result.cvar_lambda) << ",\n";
    out << "  \"risk_threshold_value\": " << format_json_number(result.risk_threshold_value) << ",\n";
    out << "  \"expected_loss_component\": " << format_json_number(result.expected_loss_component) << ",\n";
    out << "  \"cvar_loss_component\": " << format_json_number(result.cvar_loss_component) << ",\n";
    out << "  \"train_empirical_var_burned_area\": "
        << format_json_number(result.train_empirical_var_burned_area) << ",\n";
    out << "  \"train_empirical_cvar_burned_area\": "
        << format_json_number(result.train_empirical_cvar_burned_area) << ",\n";
    out << "  \"test_empirical_var_burned_area\": "
        << format_json_number(result.test_empirical_var_burned_area) << ",\n";
    out << "  \"test_empirical_cvar_burned_area\": "
        << format_json_number(result.test_empirical_cvar_burned_area) << ",\n";
    out << "  \"train_expected_weighted_burn_loss\": "
        << format_json_number(result.train_expected_weighted_burn_loss) << ",\n";
    out << "  \"test_expected_weighted_burn_loss\": "
        << format_json_number(result.test_expected_weighted_burn_loss) << ",\n";
    out << "  \"train_weighted_var\": "
        << format_json_number(result.train_weighted_var) << ",\n";
    out << "  \"test_weighted_var\": "
        << format_json_number(result.test_weighted_var) << ",\n";
    out << "  \"train_weighted_cvar\": "
        << format_json_number(result.train_weighted_cvar) << ",\n";
    out << "  \"test_weighted_cvar\": "
        << format_json_number(result.test_weighted_cvar) << ",\n";
    out << "  \"train_percentage_landscape_value_burned\": "
        << format_json_number(result.train_percentage_landscape_value_burned) << ",\n";
    out << "  \"test_percentage_landscape_value_burned\": "
        << format_json_number(result.test_percentage_landscape_value_burned) << ",\n";
    out << "  \"train_percentage_high_value_weight_burned\": "
        << format_json_number(result.train_percentage_high_value_weight_burned) << ",\n";
    out << "  \"test_percentage_high_value_weight_burned\": "
        << format_json_number(result.test_percentage_high_value_weight_burned) << ",\n";
    out << "  \"validation_status\": \"" << json_escape_local(result.validation_status) << "\",\n";
    out << "  \"selected_firebreaks\": ";
    write_json_int_array(out, result.selected_firebreaks);
    out << ",\n";
    out << "  \"warm_start_used\": " << (result.warm_start_used ? "true" : "false") << ",\n";
    out << "  \"mip_start_accepted\": " << (result.mip_start_accepted ? "true" : "false") << ",\n";
    out << "  \"warm_start_source\": \"" << json_escape_local(result.warm_start_source) << "\",\n";
    out << "  \"warm_start_valid_nodes\": ";
    write_json_int_array(out, result.warm_start_valid_nodes);
    out << ",\n";
    out << "  \"warm_start_ignored_nodes\": ";
    write_json_int_array(out, result.warm_start_ignored_nodes);
    out << ",\n";
    out << "  \"warm_start_notes\": ";
    write_json_string_array(out, result.warm_start_notes);
    out << ",\n";
    out << "  \"train_expected_burned_area\": " << format_json_number(result.train_expected_burned_area) << ",\n";
    out << "  \"train_worst_10pct_burned_area\": " << format_json_number(result.train_worst_10pct_burned_area) << ",\n";
    out << "  \"test_expected_burned_area\": " << format_json_number(result.test_expected_burned_area) << ",\n";
    out << "  \"test_worst_10pct_burned_area\": " << format_json_number(result.test_worst_10pct_burned_area) << ",\n";
    out << "  \"train_evaluation_runtime_seconds\": " << format_json_number(result.train_evaluation_runtime_seconds) << ",\n";
    out << "  \"test_evaluation_runtime_seconds\": " << format_json_number(result.test_evaluation_runtime_seconds) << ",\n";
    out << "  \"test_scenario_loading_runtime_seconds\": " << format_json_number(result.test_scenario_loading_runtime_seconds) << ",\n";
    out << "  \"train_graph_classification_ratios\": \"" << json_escape_local(result.train_graph_classification_ratios) << "\",\n";
    out << "  \"test_graph_classification_ratios\": \"" << json_escape_local(result.test_graph_classification_ratios) << "\",\n";
    out << "  \"graph_type_note\": \"" << json_escape_local(result.graph_type_note) << "\",\n";
    out << "  \"notes\": ";
    write_json_string_array(out, result.notes);
    out << "\n";
    out << "}\n";
}

void append_experiment_result_csv(
    const std::filesystem::path& output_path,
    const StandardExperimentResult& result) {
    const bool write_header = !std::filesystem::exists(output_path) || std::filesystem::file_size(output_path) == 0;
    const bool include_batch_metadata = write_header || existing_csv_has_column(output_path, "experiment_id");
    const bool include_objective_metric = write_header || existing_csv_has_column(output_path, "objective_metric");
    const bool include_warm_start = write_header || existing_csv_has_column(output_path, "warm_start_used");
    const bool include_explored_nodes = write_header || existing_csv_has_column(output_path, "explored_nodes");
    const bool include_benders_metadata =
        write_header || existing_csv_has_column(output_path, "solver_iterations");
    const bool include_benders_summary =
        write_header || existing_csv_has_column(output_path, "benders_iterations");
    const bool include_benders_timing_summary =
        write_header || existing_csv_has_column(output_path, "benders_master_solve_time_sec");
    const bool include_lifted_lower_bound_summary =
        write_header || existing_csv_has_column(output_path, "benders_use_lifted_lower_bounds");
    const bool include_lifted_lower_bound_extended =
        write_header || existing_csv_has_column(output_path, "benders_lifted_lower_bound_weighted");
    const bool include_branch_benders_summary =
        write_header || existing_csv_has_column(output_path, "branch_benders_lazy_cuts_added");
    const bool include_branch_benders_timing_summary =
        write_header || existing_csv_has_column(output_path, "branch_benders_subproblems_attempted");
    const bool include_combinatorial_benders_summary =
        write_header || existing_csv_has_column(output_path, "combinatorial_benders_enabled");
    const bool include_combinatorial_benders_extended =
        write_header || existing_csv_has_column(output_path, "combinatorial_benders_weighted");
    const bool include_fpp_strengthening_summary =
        write_header || existing_csv_has_column(output_path, "coverage_llbi_enabled");
    const bool include_coverage_llbi_extended =
        write_header || existing_csv_has_column(output_path, "coverage_llbi_weighted");
    const bool include_path_llbi_extended =
        write_header || existing_csv_has_column(output_path, "path_llbi_weighted");
    const bool include_projected_coverage_llbi_extended =
        write_header || existing_csv_has_column(output_path, "projected_coverage_llbi_weighted");
    const bool include_projected_path_llbi_extended =
        write_header || existing_csv_has_column(output_path, "projected_path_llbi_weighted");
    const bool include_branch_benders_root_user_cut_summary =
        write_header || existing_csv_has_column(output_path, "branch_benders_use_root_user_cuts");
    const bool include_restricted_candidate_summary =
        write_header || existing_csv_has_column(output_path, "restricted_candidate_enabled");
    const bool include_fpp_formulation_metadata =
        write_header || existing_csv_has_column(output_path, "formulation");
    const bool include_extended_fpp_metadata =
        write_header || existing_csv_has_column(output_path, "fpp_mode");
    const bool include_evaluation_timing =
        write_header || existing_csv_has_column(output_path, "train_evaluation_runtime_seconds");
    const bool include_validation_status =
        write_header || existing_csv_has_column(output_path, "validation_status");
    const bool include_risk_reporting =
        write_header || existing_csv_has_column(output_path, "risk_measure");
    const bool include_weight_reporting =
        write_header || existing_csv_has_column(output_path, "weight_profile");
    const bool include_graph_ratios =
        write_header || existing_csv_has_column(output_path, "train_graph_classification_ratios");
    ensure_parent_directory(output_path);
    std::ofstream out(output_path, std::ios::app);
    if (!out) {
        throw std::runtime_error("Could not open experiment CSV file: " + output_path.string());
    }

    if (write_header) {
        out << "experiment_id,case_id,run_id,timestamp,landscape,method,alpha,budget,train_scenario_count,test_scenario_count,"
            << "objective_metric,train_ids,test_ids,solver_status,objective_in_sample,best_bound,mip_gap,runtime_seconds,"
            << "solver_status_code,explored_nodes,num_variables,num_constraints,"
            << "solver_iterations,cuts_added,max_cut_violation,"
            << "benders_iterations,benders_cuts_added,benders_final_max_cut_violation,"
            << "benders_largest_intermediate_cut_violation,benders_termination_reason,"
            << "benders_master_solve_time_sec,benders_subproblem_time_sec,"
            << "benders_subproblems_solved,benders_average_subproblem_time_sec,"
            << "benders_max_subproblem_time_sec,"
            << "benders_use_lifted_lower_bounds,benders_lifted_lower_bound_count,"
            << "benders_lifted_lower_bound_precompute_time_sec,"
            << "benders_lifted_lower_bound_weighted,benders_lifted_lower_bound_weight_map_hash,"
            << "benders_lifted_lower_bound_scenarios_precomputed,"
            << "benders_lifted_lower_bound_singletons_evaluated,"
            << "benders_lifted_lower_bound_no_firebreak_loss_min,"
            << "benders_lifted_lower_bound_no_firebreak_loss_max,"
            << "benders_lifted_lower_bound_singleton_benefit_min,"
            << "benders_lifted_lower_bound_singleton_benefit_max,"
            << "benders_lifted_lower_bound_constraints_added,"
            << "benders_lifted_lower_bound_cache_hit,"
            << "benders_lifted_lower_bound_validity_mode,"
            << "branch_benders_lazy_cuts_added,branch_benders_candidate_incumbents_checked,"
            << "branch_benders_max_cut_violation,"
            << "branch_benders_candidate_callback_calls,branch_benders_subproblems_attempted,"
            << "branch_benders_subproblems_solved,branch_benders_subproblem_time_sec,"
            << "branch_benders_average_subproblem_time_sec,branch_benders_max_subproblem_time_sec,"
            << "branch_benders_callback_time_sec,branch_benders_cut_construction_time_sec,"
            << "branch_benders_lazy_cut_insertion_time_sec,branch_benders_violated_cuts,"
            << "branch_benders_nonviolated_cuts,branch_benders_skipped_cuts,"
            << "branch_benders_duplicate_cuts,"
            << "combinatorial_benders_enabled,combinatorial_benders_lift_mode,"
            << "combinatorial_benders_scenario_order,"
            << "combinatorial_benders_cut_sampling_ratio,"
            << "combinatorial_benders_fractional_separation_enabled,"
            << "combinatorial_benders_initial_cuts_enabled,"
            << "combinatorial_benders_integer_cuts_added,"
            << "combinatorial_benders_fractional_cuts_added,"
            << "combinatorial_benders_initial_cuts_added,"
            << "combinatorial_benders_scenarios_checked,"
            << "combinatorial_benders_separation_time_sec,"
            << "combinatorial_benders_avg_paths_per_cut,"
            << "combinatorial_benders_avg_cut_nonzeros,"
            << "combinatorial_benders_num_violated_cuts,"
            << "combinatorial_benders_weighted,combinatorial_benders_mode,"
            << "combinatorial_benders_weight_map_hash,"
            << "combinatorial_benders_weighted_recourse_evaluations,"
            << "combinatorial_benders_duplicate_cuts,"
            << "combinatorial_benders_cuts_tight_at_incumbent,"
            << "combinatorial_benders_lifting_enabled,"
            << "combinatorial_benders_scenario_sampling_enabled,"
            << "combinatorial_benders_max_tightness_error,"
            << "combinatorial_benders_max_violation,"
            << "combinatorial_benders_propagation_time_sec,"
            << "combinatorial_benders_cut_build_time_sec,"
            << "combinatorial_benders_validity_mode,"
            << "combinatorial_weighted,combinatorial_mode,"
            << "combinatorial_weight_map_hash,"
            << "combinatorial_candidate_callbacks,"
            << "combinatorial_scenarios_evaluated,"
            << "combinatorial_weighted_recourse_evaluations,"
            << "combinatorial_cuts_generated,"
            << "combinatorial_cuts_added,"
            << "combinatorial_duplicate_cuts,"
            << "combinatorial_cuts_tight_at_incumbent,"
            << "combinatorial_max_tightness_error,"
            << "combinatorial_max_violation,"
            << "combinatorial_propagation_time_sec,"
            << "combinatorial_cut_build_time_sec,"
            << "combinatorial_callback_time_sec,"
            << "combinatorial_validity_mode,"
            << "combinatorial_lifting_enabled,"
            << "combinatorial_fractional_cuts_enabled,"
            << "combinatorial_initial_cuts_enabled,"
            << "combinatorial_scenario_sampling_enabled,"
            << "coverage_llbi_enabled,coverage_llbi_num_zeta_vars,coverage_llbi_num_constraints,"
            << "coverage_llbi_precompute_time_sec,"
            << "coverage_llbi_weighted,coverage_llbi_weight_map_hash,"
            << "coverage_llbi_scenarios_precomputed,coverage_llbi_baseline_cells,"
            << "coverage_llbi_auxiliary_variables,coverage_llbi_linking_constraints,"
            << "coverage_llbi_loss_constraints,coverage_llbi_nonempty_coverage_sets,"
            << "coverage_llbi_total_incidence_terms,coverage_llbi_build_time_sec,"
            << "coverage_llbi_validity_mode,"
            << "path_llbi_enabled,path_llbi_num_b_vars,"
            << "path_llbi_num_path_constraints,path_llbi_num_paths_used,"
            << "path_llbi_precompute_time_sec,"
            << "path_llbi_weighted,path_llbi_weight_map_hash,"
            << "path_llbi_scenarios_precomputed,path_llbi_baseline_nodes,"
            << "path_llbi_auxiliary_variables,path_llbi_path_constraints,"
            << "path_llbi_loss_constraints,path_llbi_total_paths,"
            << "path_llbi_total_candidate_incidence_terms,path_llbi_nodes_without_paths,"
            << "path_llbi_path_enumeration_complete,path_llbi_paths_truncated,"
            << "path_llbi_build_time_sec,path_llbi_validity_mode,"
            << "projected_coverage_llbi_enabled,projected_path_llbi_enabled,"
            << "projected_llbi_family,projected_llbi_strategy,projected_llbi_mode,"
            << "projected_llbi_root_rounds,projected_llbi_cuts_added,"
            << "projected_llbi_coverage_cuts_added,projected_llbi_path_cuts_added,"
            << "projected_llbi_violated_cuts_found,projected_llbi_separation_time_sec,"
            << "projected_llbi_solve_time_sec,projected_llbi_total_time_sec,"
            << "projected_llbi_total_nonzeros,projected_llbi_avg_nonzeros_per_cut,"
            << "projected_llbi_max_nonzeros_per_cut,projected_llbi_min_violation,"
            << "projected_llbi_max_violation,projected_llbi_avg_violation,"
            << "projected_llbi_root_bound_initial,projected_llbi_root_bound_final,"
            << "projected_llbi_root_bound_improvement_abs,projected_llbi_root_bound_improvement_pct,"
            << "projected_poly_candidate_cuts_generated,projected_poly_candidate_cuts_added,"
            << "projected_poly_enumeration_truncated,projected_poly_enumeration_limit,"
            << "projected_exp_separated_cuts_added,projected_exp_separation_rounds,"
            << "projected_exp_candidate_cuts_generated,projected_exp_candidate_cuts_added,"
            << "projected_exp_enumeration_truncated,projected_exp_enumeration_limit,";
        if (include_projected_coverage_llbi_extended) {
            out
                << "projected_coverage_llbi_weighted,projected_coverage_llbi_mode,"
            << "projected_coverage_llbi_weight_map_hash,"
            << "projected_coverage_llbi_scenarios_precomputed,"
            << "projected_coverage_llbi_baseline_cells,"
            << "projected_coverage_llbi_nonempty_coverage_sets,"
            << "projected_coverage_llbi_total_incidence_terms,"
            << "projected_coverage_llbi_separation_calls,"
            << "projected_coverage_llbi_cuts_generated,"
            << "projected_coverage_llbi_cuts_added,"
            << "projected_coverage_llbi_duplicate_cuts,"
            << "projected_coverage_llbi_max_violation,"
            << "projected_coverage_llbi_precompute_time_sec,"
                << "projected_coverage_llbi_separation_time_sec,"
                << "projected_coverage_llbi_validity_mode,";
        }
        if (include_projected_path_llbi_extended) {
            out
                << "projected_path_llbi_weighted,projected_path_llbi_mode,"
                << "projected_path_llbi_weight_map_hash,"
                << "projected_path_llbi_scenarios_precomputed,"
                << "projected_path_llbi_destination_nodes,"
                << "projected_path_llbi_total_paths,"
                << "projected_path_llbi_total_incidence_terms,"
                << "projected_path_llbi_nodes_without_paths,"
                << "projected_path_llbi_enumeration_complete,"
                << "projected_path_llbi_paths_truncated,"
                << "projected_path_llbi_separation_calls,"
                << "projected_path_llbi_cuts_generated,"
                << "projected_path_llbi_cuts_added,"
                << "projected_path_llbi_duplicate_cuts,"
                << "projected_path_llbi_max_violation,"
                << "projected_path_llbi_precompute_time_sec,"
                << "projected_path_llbi_separation_time_sec,"
                << "projected_path_llbi_validity_mode,";
        }
        if (include_fpp_strengthening_summary) {
            out
            << "global_dominance_enabled,global_dominance_structural_weight_safe,"
            << "global_dominance_original_candidate_count,global_dominance_candidates_removed,"
            << "global_dominance_equivalence_classes,global_dominance_post_candidate_count,"
            << "global_dominance_warm_start_replacements,global_dominance_precompute_time_sec,"
            << "conditional_zero_benefit_enabled,conditional_zero_benefit_structural_weight_safe,"
            << "conditional_zero_benefit_callback_calls,conditional_zero_benefit_nodes_checked,"
            << "conditional_zero_benefit_candidates_checked,conditional_zero_benefit_fixings_attempted,"
            << "conditional_zero_benefit_fixings_applied,conditional_zero_benefit_variables_fixed_zero,"
            << "conditional_zero_benefit_scenarios_reachability_computed,conditional_zero_benefit_time_sec,"
            << "branch_benders_use_root_user_cuts,branch_benders_root_user_cuts_added,"
            << "branch_benders_root_user_cut_rounds,branch_benders_root_user_cut_max_violation,"
            << "restricted_candidate_enabled,restricted_candidate_exact_mode,"
            << "restricted_candidate_initial_active_count,restricted_candidate_final_active_count,"
            << "restricted_candidate_final_active_fraction,restricted_candidate_eventually_activated_all,"
            << "restricted_candidate_rounds,restricted_candidate_cut_pool_size,"
            << "restricted_candidate_heuristic_mode_enabled,restricted_candidate_stopped_before_full_activation,"
            << "restricted_candidate_global_optimality_certified,"
            << "fpp_mode,formulation,dominator_cuts_enabled,separator_cuts_enabled,greedy_warm_start_enabled,local_search_enabled,"
            << "compact_node_count,eligible_node_count,total_observed_scenario_nodes,total_scenario_arcs,"
            << "separator_cuts_added,separator_min_cut_calls,separator_callback_invocations,"
            << "separator_duplicate_cuts_skipped,separator_large_cuts_skipped,separator_time_sec,"
            << "dominator_cuts_added,dominator_aggregate_cuts_added,dominator_individual_cuts_added,"
            << "dominator_dag_scenarios,dominator_fallback_scenarios,dominator_preprocessing_time_sec,"
            << "heuristic_time_sec,heuristic_objective,heuristic_exact_evaluations,heuristic_selected_count,"
            << "evaluator_objective,evaluator_abs_diff,evaluator_rel_diff,"
            << "weight_profile,weight_map_file,weight_map_hash,weight_normalized,"
            << "weight_mean,weight_min,weight_max,weight_total,"
            << "solver_weighted_objective,evaluator_weighted_objective,"
            << "objective_validation_abs_difference,objective_validation_rel_difference,"
            << "objective_validation_passed,"
            << "validation_status,"
            << "risk_measure,cvar_beta,cvar_lambda,train_cvar_burned_area,test_cvar_burned_area,"
            << "train_expected_weighted_burn_loss,test_expected_weighted_burn_loss,"
            << "train_weighted_var,test_weighted_var,train_weighted_cvar,test_weighted_cvar,"
            << "train_percentage_landscape_value_burned,test_percentage_landscape_value_burned,"
            << "train_percentage_high_value_weight_burned,test_percentage_high_value_weight_burned,"
            << "selected_firebreaks,"
            << "warm_start_used,mip_start_accepted,warm_start_source,warm_start_valid_nodes,warm_start_ignored_nodes,warm_start_notes,"
            << "train_expected_burned_area,train_worst_10pct_burned_area,"
            << "test_expected_burned_area,test_worst_10pct_burned_area,"
            << "train_evaluation_runtime_seconds,test_evaluation_runtime_seconds,test_scenario_loading_runtime_seconds,"
            << "train_graph_classification_ratios,test_graph_classification_ratios,"
            << "graph_type_note,notes\n";
        }
    }

    if (include_batch_metadata) {
        out << csv_escape(result.experiment_id) << ","
            << result.case_id << ",";
    }
    out << csv_escape(result.run_id) << ","
        << csv_escape(result.timestamp) << ","
        << csv_escape(result.landscape) << ","
        << csv_escape(result.method) << ","
        << format_csv_number(result.alpha) << ","
        << result.budget << ","
        << result.train_scenario_count << ","
        << result.test_scenario_count << ",";
    if (include_objective_metric) {
        out << csv_escape(result.objective_metric) << ",";
    }
    out << csv_escape(join_ints(result.train_ids, ";")) << ","
        << csv_escape(join_ints(result.test_ids, ";")) << ","
        << csv_escape(result.solver_status) << ","
        << format_csv_number(result.objective_in_sample) << ","
        << format_csv_number(result.best_bound) << ","
        << format_csv_number(result.mip_gap) << ","
        << format_csv_number(result.runtime_seconds) << ","
        << result.solver_status_code << ",";
    if (include_explored_nodes) {
        out << result.explored_nodes << ",";
    }
    out << result.num_variables << ","
        << result.num_constraints << ",";
    if (include_benders_metadata) {
        out << result.solver_iterations << ","
            << result.cuts_added << ","
            << format_csv_number(result.max_cut_violation) << ",";
    }
    if (include_benders_summary) {
        out << result.benders_iterations << ","
            << result.benders_cuts_added << ","
            << format_csv_number(result.benders_final_max_cut_violation) << ","
            << format_csv_number(result.benders_largest_intermediate_cut_violation) << ","
            << csv_escape(result.benders_termination_reason) << ",";
    }
    if (include_benders_timing_summary) {
        out << format_csv_number(result.benders_master_solve_time_sec) << ","
            << format_csv_number(result.benders_subproblem_time_sec) << ","
            << result.benders_subproblems_solved << ","
            << format_csv_number(result.benders_average_subproblem_time_sec) << ","
            << format_csv_number(result.benders_max_subproblem_time_sec) << ",";
    }
    if (include_lifted_lower_bound_summary) {
        out << (result.benders_use_lifted_lower_bounds ? "true" : "false") << ","
            << result.benders_lifted_lower_bound_count << ","
            << format_csv_number(result.benders_lifted_lower_bound_precompute_time_sec) << ",";
    }
    if (include_lifted_lower_bound_extended) {
        out << (result.benders_lifted_lower_bound_weighted ? "true" : "false") << ","
            << csv_escape(result.benders_lifted_lower_bound_weight_map_hash) << ","
            << result.benders_lifted_lower_bound_scenarios_precomputed << ","
            << result.benders_lifted_lower_bound_singletons_evaluated << ","
            << format_csv_number(result.benders_lifted_lower_bound_no_firebreak_loss_min) << ","
            << format_csv_number(result.benders_lifted_lower_bound_no_firebreak_loss_max) << ","
            << format_csv_number(result.benders_lifted_lower_bound_singleton_benefit_min) << ","
            << format_csv_number(result.benders_lifted_lower_bound_singleton_benefit_max) << ","
            << result.benders_lifted_lower_bound_constraints_added << ","
            << (result.benders_lifted_lower_bound_cache_hit ? "true" : "false") << ","
            << csv_escape(result.benders_lifted_lower_bound_validity_mode) << ",";
    }
    if (include_branch_benders_summary) {
        out << result.branch_benders_lazy_cuts_added << ","
            << result.branch_benders_candidate_incumbents_checked << ","
            << format_csv_number(result.branch_benders_max_cut_violation) << ",";
    }
    if (include_branch_benders_timing_summary) {
        out << result.branch_benders_candidate_callback_calls << ","
            << result.branch_benders_subproblems_attempted << ","
            << result.branch_benders_subproblems_solved << ","
            << format_csv_number(result.branch_benders_subproblem_time_sec) << ","
            << format_csv_number(result.branch_benders_average_subproblem_time_sec) << ","
            << format_csv_number(result.branch_benders_max_subproblem_time_sec) << ","
            << format_csv_number(result.branch_benders_callback_time_sec) << ","
            << format_csv_number(result.branch_benders_cut_construction_time_sec) << ","
            << format_csv_number(result.branch_benders_lazy_cut_insertion_time_sec) << ","
            << result.branch_benders_violated_cuts << ","
            << result.branch_benders_nonviolated_cuts << ","
            << result.branch_benders_skipped_cuts << ","
            << result.branch_benders_duplicate_cuts << ",";
    }
    if (include_combinatorial_benders_summary) {
        out << (result.combinatorial_benders_enabled ? "true" : "false") << ","
            << csv_escape(result.combinatorial_benders_lift_mode) << ","
            << csv_escape(result.combinatorial_benders_scenario_order) << ","
            << format_csv_number(result.combinatorial_benders_cut_sampling_ratio) << ","
            << (result.combinatorial_benders_fractional_separation_enabled ? "true" : "false") << ","
            << (result.combinatorial_benders_initial_cuts_enabled ? "true" : "false") << ","
            << result.combinatorial_benders_integer_cuts_added << ","
            << result.combinatorial_benders_fractional_cuts_added << ","
            << result.combinatorial_benders_initial_cuts_added << ","
            << result.combinatorial_benders_scenarios_checked << ","
            << format_csv_number(result.combinatorial_benders_separation_time_sec) << ","
            << format_csv_number(result.combinatorial_benders_avg_paths_per_cut) << ","
            << format_csv_number(result.combinatorial_benders_avg_cut_nonzeros) << ","
            << result.combinatorial_benders_num_violated_cuts << ",";
    }
    if (include_combinatorial_benders_extended) {
        out << (result.combinatorial_benders_weighted ? "true" : "false") << ","
            << csv_escape(result.combinatorial_benders_mode) << ","
            << csv_escape(result.combinatorial_benders_weight_map_hash) << ","
            << result.combinatorial_benders_weighted_recourse_evaluations << ","
            << result.combinatorial_benders_duplicate_cuts << ","
            << result.combinatorial_benders_cuts_tight_at_incumbent << ","
            << (result.combinatorial_benders_lifting_enabled ? "true" : "false") << ","
            << (result.combinatorial_benders_scenario_sampling_enabled ? "true" : "false") << ","
            << format_csv_number(result.combinatorial_benders_max_tightness_error) << ","
            << format_csv_number(result.combinatorial_benders_max_violation) << ","
            << format_csv_number(result.combinatorial_benders_propagation_time_sec) << ","
            << format_csv_number(result.combinatorial_benders_cut_build_time_sec) << ","
            << csv_escape(result.combinatorial_benders_validity_mode) << ","
            << (result.combinatorial_weighted ? "true" : "false") << ","
            << csv_escape(result.combinatorial_mode) << ","
            << csv_escape(result.combinatorial_weight_map_hash) << ","
            << result.combinatorial_candidate_callbacks << ","
            << result.combinatorial_scenarios_evaluated << ","
            << result.combinatorial_weighted_recourse_evaluations << ","
            << result.combinatorial_cuts_generated << ","
            << result.combinatorial_cuts_added << ","
            << result.combinatorial_duplicate_cuts << ","
            << result.combinatorial_cuts_tight_at_incumbent << ","
            << format_csv_number(result.combinatorial_max_tightness_error) << ","
            << format_csv_number(result.combinatorial_max_violation) << ","
            << format_csv_number(result.combinatorial_propagation_time_sec) << ","
            << format_csv_number(result.combinatorial_cut_build_time_sec) << ","
            << format_csv_number(result.combinatorial_callback_time_sec) << ","
            << csv_escape(result.combinatorial_validity_mode) << ","
            << (result.combinatorial_lifting_enabled ? "true" : "false") << ","
            << (result.combinatorial_fractional_cuts_enabled ? "true" : "false") << ","
            << (result.combinatorial_initial_cuts_enabled ? "true" : "false") << ","
            << (result.combinatorial_scenario_sampling_enabled ? "true" : "false") << ",";
    }
    if (include_fpp_strengthening_summary) {
        out << (result.coverage_llbi_enabled ? "true" : "false") << ","
            << result.coverage_llbi_num_zeta_vars << ","
            << result.coverage_llbi_num_constraints << ","
            << format_csv_number(result.coverage_llbi_precompute_time_sec) << ",";
    }
    if (include_coverage_llbi_extended) {
        out << (result.coverage_llbi_weighted ? "true" : "false") << ","
            << csv_escape(result.coverage_llbi_weight_map_hash) << ","
            << result.coverage_llbi_scenarios_precomputed << ","
            << result.coverage_llbi_baseline_cells << ","
            << result.coverage_llbi_auxiliary_variables << ","
            << result.coverage_llbi_linking_constraints << ","
            << result.coverage_llbi_loss_constraints << ","
            << result.coverage_llbi_nonempty_coverage_sets << ","
            << result.coverage_llbi_total_incidence_terms << ","
            << format_csv_number(result.coverage_llbi_build_time_sec) << ","
            << csv_escape(result.coverage_llbi_validity_mode) << ",";
    }
    if (include_fpp_strengthening_summary) {
        out
            << (result.path_llbi_enabled ? "true" : "false") << ","
            << result.path_llbi_num_b_vars << ","
            << result.path_llbi_num_path_constraints << ","
            << result.path_llbi_num_paths_used << ","
            << format_csv_number(result.path_llbi_precompute_time_sec) << ",";
    }
    if (include_path_llbi_extended) {
        out << (result.path_llbi_weighted ? "true" : "false") << ","
            << csv_escape(result.path_llbi_weight_map_hash) << ","
            << result.path_llbi_scenarios_precomputed << ","
            << result.path_llbi_baseline_nodes << ","
            << result.path_llbi_auxiliary_variables << ","
            << result.path_llbi_path_constraints << ","
            << result.path_llbi_loss_constraints << ","
            << result.path_llbi_total_paths << ","
            << result.path_llbi_total_candidate_incidence_terms << ","
            << result.path_llbi_nodes_without_paths << ","
            << (result.path_llbi_path_enumeration_complete ? "true" : "false") << ","
            << result.path_llbi_paths_truncated << ","
            << format_csv_number(result.path_llbi_build_time_sec) << ","
            << csv_escape(result.path_llbi_validity_mode) << ",";
    }
    if (include_fpp_strengthening_summary) {
        out
            << (result.projected_coverage_llbi_enabled ? "true" : "false") << ","
            << (result.projected_path_llbi_enabled ? "true" : "false") << ","
            << csv_escape(result.projected_llbi_family) << ","
            << csv_escape(result.projected_llbi_strategy) << ","
            << csv_escape(result.projected_llbi_mode) << ","
            << result.projected_llbi_root_rounds << ","
            << result.projected_llbi_cuts_added << ","
            << result.projected_llbi_coverage_cuts_added << ","
            << result.projected_llbi_path_cuts_added << ","
            << result.projected_llbi_violated_cuts_found << ","
            << format_csv_number(result.projected_llbi_separation_time_sec) << ","
            << format_csv_number(result.projected_llbi_solve_time_sec) << ","
            << format_csv_number(result.projected_llbi_total_time_sec) << ","
            << result.projected_llbi_total_nonzeros << ","
            << format_csv_number(result.projected_llbi_avg_nonzeros_per_cut) << ","
            << result.projected_llbi_max_nonzeros_per_cut << ","
            << format_csv_number(result.projected_llbi_min_violation) << ","
            << format_csv_number(result.projected_llbi_max_violation) << ","
            << format_csv_number(result.projected_llbi_avg_violation) << ","
            << format_csv_number(result.projected_llbi_root_bound_initial) << ","
            << format_csv_number(result.projected_llbi_root_bound_final) << ","
            << format_csv_number(result.projected_llbi_root_bound_improvement_abs) << ","
            << format_csv_number(result.projected_llbi_root_bound_improvement_pct) << ","
            << result.projected_poly_candidate_cuts_generated << ","
            << result.projected_poly_candidate_cuts_added << ","
            << (result.projected_poly_enumeration_truncated ? "true" : "false") << ","
            << result.projected_poly_enumeration_limit << ","
            << result.projected_exp_separated_cuts_added << ","
            << result.projected_exp_separation_rounds << ","
            << result.projected_exp_candidate_cuts_generated << ","
            << result.projected_exp_candidate_cuts_added << ","
            << (result.projected_exp_enumeration_truncated ? "true" : "false") << ","
            << result.projected_exp_enumeration_limit << ",";
    }
    if (include_projected_coverage_llbi_extended) {
        out
            << (result.projected_coverage_llbi_weighted ? "true" : "false") << ","
            << csv_escape(result.projected_coverage_llbi_mode) << ","
            << csv_escape(result.projected_coverage_llbi_weight_map_hash) << ","
            << result.projected_coverage_llbi_scenarios_precomputed << ","
            << result.projected_coverage_llbi_baseline_cells << ","
            << result.projected_coverage_llbi_nonempty_coverage_sets << ","
            << result.projected_coverage_llbi_total_incidence_terms << ","
            << result.projected_coverage_llbi_separation_calls << ","
            << result.projected_coverage_llbi_cuts_generated << ","
            << result.projected_coverage_llbi_cuts_added << ","
            << result.projected_coverage_llbi_duplicate_cuts << ","
            << format_csv_number(result.projected_coverage_llbi_max_violation) << ","
            << format_csv_number(result.projected_coverage_llbi_precompute_time_sec) << ","
            << format_csv_number(result.projected_coverage_llbi_separation_time_sec) << ","
            << csv_escape(result.projected_coverage_llbi_validity_mode) << ",";
    }
    if (include_projected_path_llbi_extended) {
        out
            << (result.projected_path_llbi_weighted ? "true" : "false") << ","
            << csv_escape(result.projected_path_llbi_mode) << ","
            << csv_escape(result.projected_path_llbi_weight_map_hash) << ","
            << result.projected_path_llbi_scenarios_precomputed << ","
            << result.projected_path_llbi_destination_nodes << ","
            << result.projected_path_llbi_total_paths << ","
            << result.projected_path_llbi_total_incidence_terms << ","
            << result.projected_path_llbi_nodes_without_paths << ","
            << (result.projected_path_llbi_enumeration_complete ? "true" : "false") << ","
            << result.projected_path_llbi_paths_truncated << ","
            << result.projected_path_llbi_separation_calls << ","
            << result.projected_path_llbi_cuts_generated << ","
            << result.projected_path_llbi_cuts_added << ","
            << result.projected_path_llbi_duplicate_cuts << ","
            << format_csv_number(result.projected_path_llbi_max_violation) << ","
            << format_csv_number(result.projected_path_llbi_precompute_time_sec) << ","
            << format_csv_number(result.projected_path_llbi_separation_time_sec) << ","
            << csv_escape(result.projected_path_llbi_validity_mode) << ",";
    }
    if (include_fpp_strengthening_summary) {
        out
            << (result.global_dominance_enabled ? "true" : "false") << ","
            << (result.global_dominance_structural_weight_safe ? "true" : "false") << ","
            << result.global_dominance_original_candidate_count << ","
            << result.global_dominance_candidates_removed << ","
            << result.global_dominance_equivalence_classes << ","
            << result.global_dominance_post_candidate_count << ","
            << result.global_dominance_warm_start_replacements << ","
            << format_csv_number(result.global_dominance_precompute_time_sec) << ","
            << (result.conditional_zero_benefit_enabled ? "true" : "false") << ","
            << (result.conditional_zero_benefit_structural_weight_safe ? "true" : "false") << ","
            << result.conditional_zero_benefit_callback_calls << ","
            << result.conditional_zero_benefit_nodes_checked << ","
            << result.conditional_zero_benefit_candidates_checked << ","
            << result.conditional_zero_benefit_fixings_attempted << ","
            << result.conditional_zero_benefit_fixings_applied << ","
            << result.conditional_zero_benefit_variables_fixed_zero << ","
            << result.conditional_zero_benefit_scenarios_reachability_computed << ","
            << format_csv_number(result.conditional_zero_benefit_time_sec) << ",";
    }
    if (include_branch_benders_root_user_cut_summary) {
        out << (result.branch_benders_use_root_user_cuts ? "true" : "false") << ","
            << result.branch_benders_root_user_cuts_added << ","
            << result.branch_benders_root_user_cut_rounds_executed << ","
            << format_csv_number(result.branch_benders_root_user_cut_max_violation) << ",";
    }
    if (include_restricted_candidate_summary) {
        out << (result.restricted_candidate_enabled ? "true" : "false") << ","
            << (result.restricted_candidate_exact_mode ? "true" : "false") << ","
            << result.restricted_candidate_initial_active_count << ","
            << result.restricted_candidate_final_active_count << ","
            << format_csv_number(result.restricted_candidate_final_active_fraction) << ","
            << (result.restricted_candidate_eventually_activated_all ? "true" : "false") << ","
            << result.restricted_candidate_rounds << ","
            << result.restricted_candidate_cut_pool_size << ","
            << (result.restricted_candidate_heuristic_mode_enabled ? "true" : "false") << ","
            << (result.restricted_candidate_stopped_before_full_activation ? "true" : "false") << ","
            << (result.restricted_candidate_global_optimality_certified ? "true" : "false") << ",";
    }
    if (include_fpp_formulation_metadata) {
        if (include_extended_fpp_metadata) {
            out << csv_escape(result.fpp_mode) << ","
                << csv_escape(result.formulation) << ","
                << (result.dominator_cuts_enabled ? "true" : "false") << ","
                << (result.separator_cuts_enabled ? "true" : "false") << ","
                << (result.greedy_warm_start_enabled ? "true" : "false") << ","
                << (result.local_search_enabled ? "true" : "false") << ","
                << result.compact_node_count << ","
                << result.eligible_node_count << ","
                << result.total_observed_scenario_nodes << ","
                << result.total_scenario_arcs << ","
                << result.separator_cuts_added << ","
                << result.separator_min_cut_calls << ","
                << result.separator_callback_invocations << ","
                << result.separator_duplicate_cuts_skipped << ","
                << result.separator_large_cuts_skipped << ","
                << format_csv_number(result.separator_time_sec) << ","
                << result.dominator_cuts_added << ","
                << result.dominator_aggregate_cuts_added << ","
                << result.dominator_individual_cuts_added << ","
                << result.dominator_dag_scenarios << ","
                << result.dominator_fallback_scenarios << ","
                << format_csv_number(result.dominator_preprocessing_time_sec) << ","
                << format_csv_number(result.heuristic_time_sec) << ","
                << format_csv_number(result.heuristic_objective) << ","
                << result.heuristic_exact_evaluations << ","
                << result.heuristic_selected_count << ","
                << format_csv_number(result.evaluator_objective) << ","
                << format_csv_number(result.evaluator_abs_diff) << ","
                << format_csv_number(result.evaluator_rel_diff) << ",";
        } else {
            out << csv_escape(result.formulation) << ","
                << (result.dominator_cuts_enabled ? "true" : "false") << ","
                << (result.separator_cuts_enabled ? "true" : "false") << ","
                << (result.greedy_warm_start_enabled ? "true" : "false") << ","
                << result.separator_cuts_added << ","
                << result.separator_min_cut_calls << ","
                << format_csv_number(result.separator_time_sec) << ","
                << result.dominator_cuts_added << ","
                << format_csv_number(result.dominator_preprocessing_time_sec) << ","
                << format_csv_number(result.heuristic_time_sec) << ","
                << format_csv_number(result.heuristic_objective) << ",";
        }
    }
    if (include_weight_reporting) {
        out << csv_escape(result.weight_profile) << ","
            << csv_escape(result.weight_map_file) << ","
            << csv_escape(result.weight_map_hash) << ","
            << (result.weight_normalized ? "true" : "false") << ","
            << format_csv_number(result.weight_mean) << ","
            << format_csv_number(result.weight_min) << ","
            << format_csv_number(result.weight_max) << ","
            << format_csv_number(result.weight_total) << ","
            << format_csv_number(result.solver_weighted_objective) << ","
            << format_csv_number(result.evaluator_weighted_objective) << ","
            << format_csv_number(result.objective_validation_abs_difference) << ","
            << format_csv_number(result.objective_validation_rel_difference) << ","
            << (result.objective_validation_passed ? "true" : "false") << ",";
    }
    if (include_validation_status) {
        out << csv_escape(result.validation_status) << ",";
    }
    if (include_risk_reporting) {
        out << csv_escape(result.risk_measure) << ","
            << format_csv_number(result.cvar_beta) << ","
            << format_csv_number(result.cvar_lambda) << ","
            << format_csv_number(result.train_empirical_cvar_burned_area) << ","
            << format_csv_number(result.test_empirical_cvar_burned_area) << ",";
    }
    if (include_weight_reporting) {
        out << format_csv_number(result.train_expected_weighted_burn_loss) << ","
            << format_csv_number(result.test_expected_weighted_burn_loss) << ","
            << format_csv_number(result.train_weighted_var) << ","
            << format_csv_number(result.test_weighted_var) << ","
            << format_csv_number(result.train_weighted_cvar) << ","
            << format_csv_number(result.test_weighted_cvar) << ","
            << format_csv_number(result.train_percentage_landscape_value_burned) << ","
            << format_csv_number(result.test_percentage_landscape_value_burned) << ","
            << format_csv_number(result.train_percentage_high_value_weight_burned) << ","
            << format_csv_number(result.test_percentage_high_value_weight_burned) << ",";
    }
    out << csv_escape(join_ints(result.selected_firebreaks, ";")) << ",";
    if (include_warm_start) {
        out << (result.warm_start_used ? "true" : "false") << ","
            << (include_extended_fpp_metadata ? std::string(result.mip_start_accepted ? "true," : "false,") : std::string())
            << csv_escape(result.warm_start_source) << ","
            << csv_escape(join_ints(result.warm_start_valid_nodes, ";")) << ","
            << csv_escape(join_ints(result.warm_start_ignored_nodes, ";")) << ","
            << csv_escape(join_strings(result.warm_start_notes, " | ")) << ",";
    }
    out
        << format_csv_number(result.train_expected_burned_area) << ","
        << format_csv_number(result.train_worst_10pct_burned_area) << ","
        << format_csv_number(result.test_expected_burned_area) << ","
        << format_csv_number(result.test_worst_10pct_burned_area) << ",";
    if (include_evaluation_timing) {
        out << format_csv_number(result.train_evaluation_runtime_seconds) << ","
            << format_csv_number(result.test_evaluation_runtime_seconds) << ","
            << format_csv_number(result.test_scenario_loading_runtime_seconds) << ",";
    }
    if (include_graph_ratios) {
        out << csv_escape(result.train_graph_classification_ratios) << ","
            << csv_escape(result.test_graph_classification_ratios) << ",";
    }
    out << csv_escape(result.graph_type_note) << ","
        << csv_escape(join_strings(result.notes, " | ")) << "\n";
}

}  // namespace firebreak::io
