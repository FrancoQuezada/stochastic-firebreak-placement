#include "analysis/ExperimentAggregator.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "io/PathUtils.hpp"

namespace firebreak::analysis {

namespace {

struct BatchRow {
    std::string experiment_id;
    int case_id = -1;
    std::string landscape;
    std::string method;
    std::string fpp_mode;
    double alpha = 0.0;
    int train_count = 0;
    int test_count = 0;
    std::string solver_status;
    double mip_gap = std::numeric_limits<double>::quiet_NaN();
    double runtime_seconds = 0.0;
    double train_expected = 0.0;
    double test_expected = 0.0;
    double test_worst = 0.0;
};

struct SummaryAccumulator {
    int cases = 0;
    int optimal_solves = 0;
    int mip_gap_count = 0;
    double sum_test_expected = 0.0;
    double sum_test_worst = 0.0;
    double sum_train_expected = 0.0;
    double sum_runtime = 0.0;
    double sum_mip_gap = 0.0;
};

std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (in_quotes) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                current.push_back(ch);
            }
        } else if (ch == '"') {
            in_quotes = true;
        } else if (ch == ',') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    return fields;
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
            out.push_back(ch);
        }
    }
    out += "\"";
    return out;
}

std::string format_number(double value) {
    if (!std::isfinite(value)) {
        return "";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(8) << value;
    return out.str();
}

double parse_double_or_nan(const std::string& value) {
    if (value.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    try {
        return std::stod(value);
    } catch (...) {
        return std::numeric_limits<double>::quiet_NaN();
    }
}

int parse_int_or_default(const std::string& value, int fallback) {
    if (value.empty()) {
        return fallback;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::string get_field(
    const std::vector<std::string>& fields,
    const std::unordered_map<std::string, std::size_t>& column,
    const std::string& name) {
    const auto found = column.find(name);
    if (found == column.end() || found->second >= fields.size()) {
        return "";
    }
    return fields[found->second];
}

std::vector<BatchRow> read_batch_rows(const std::filesystem::path& input_csv) {
    std::ifstream in(input_csv);
    if (!in) {
        throw std::runtime_error("Could not open batch CSV: " + input_csv.string());
    }

    std::string header_line;
    if (!std::getline(in, header_line)) {
        throw std::runtime_error("Batch CSV is empty: " + input_csv.string());
    }
    const auto header = parse_csv_line(header_line);
    std::unordered_map<std::string, std::size_t> column;
    for (std::size_t i = 0; i < header.size(); ++i) {
        column[header[i]] = i;
    }

    std::vector<BatchRow> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = parse_csv_line(line);
        BatchRow row;
        row.experiment_id = get_field(fields, column, "experiment_id");
        row.case_id = parse_int_or_default(get_field(fields, column, "case_id"), -1);
        row.landscape = get_field(fields, column, "landscape");
        row.method = get_field(fields, column, "method");
        row.fpp_mode = get_field(fields, column, "fpp_mode");
        row.alpha = parse_double_or_nan(get_field(fields, column, "alpha"));
        row.train_count = parse_int_or_default(get_field(fields, column, "train_scenario_count"), 0);
        row.test_count = parse_int_or_default(get_field(fields, column, "test_scenario_count"), 0);
        row.solver_status = get_field(fields, column, "solver_status");
        row.mip_gap = parse_double_or_nan(get_field(fields, column, "mip_gap"));
        row.runtime_seconds = parse_double_or_nan(get_field(fields, column, "runtime_seconds"));
        row.train_expected = parse_double_or_nan(get_field(fields, column, "train_expected_burned_area"));
        row.test_expected = parse_double_or_nan(get_field(fields, column, "test_expected_burned_area"));
        row.test_worst = parse_double_or_nan(get_field(fields, column, "test_worst_10pct_burned_area"));
        rows.push_back(row);
    }

    return rows;
}

std::string summary_key(const BatchRow& row) {
    std::ostringstream out;
    out << row.landscape << "|" << std::fixed << std::setprecision(8) << row.alpha
        << "|" << row.train_count << "|" << row.method << "|" << row.fpp_mode;
    return out.str();
}

std::string pairwise_key(const BatchRow& row) {
    std::ostringstream out;
    out << row.experiment_id << "|" << row.case_id
        << "|" << row.landscape << "|" << std::fixed << std::setprecision(8) << row.alpha
        << "|" << row.train_count << "|" << row.test_count;
    return out.str();
}

bool status_is_optimal(const std::string& status) {
    return status.find("Optimal") != std::string::npos;
}

std::string display_method(const BatchRow& row) {
    if (row.method == "FPP-SAA" && !row.fpp_mode.empty()) {
        return row.method + ":" + row.fpp_mode;
    }
    return row.method;
}

}  // namespace

AggregationSummary ExperimentAggregator::aggregate(
    const std::filesystem::path& input_csv,
    const std::filesystem::path& output_dir) const {
    const auto rows = read_batch_rows(input_csv);
    if (rows.empty()) {
        throw std::runtime_error("Batch CSV contains no result rows.");
    }

    std::filesystem::create_directories(output_dir);

    std::map<std::string, std::pair<BatchRow, SummaryAccumulator>> grouped;
    for (const auto& row : rows) {
        auto& entry = grouped[summary_key(row)];
        if (entry.second.cases == 0) {
            entry.first = row;
        }
        auto& acc = entry.second;
        ++acc.cases;
        acc.sum_test_expected += row.test_expected;
        acc.sum_test_worst += row.test_worst;
        acc.sum_train_expected += row.train_expected;
        acc.sum_runtime += row.runtime_seconds;
        if (status_is_optimal(row.solver_status)) {
            ++acc.optimal_solves;
        }
        if (std::isfinite(row.mip_gap)) {
            acc.sum_mip_gap += row.mip_gap;
            ++acc.mip_gap_count;
        }
    }

    const auto summary_path = output_dir / "summary_by_method.csv";
    firebreak::io::ensure_parent_directory(summary_path);
    std::ofstream summary_out(summary_path);
    if (!summary_out) {
        throw std::runtime_error("Could not open summary output: " + summary_path.string());
    }
    summary_out
        << "landscape,alpha,train_count,method,mean_test_expected_burned_area,"
        << "mean_test_worst_10pct_burned_area,mean_train_expected_burned_area,"
        << "mean_runtime_seconds,number_of_cases_solved,number_of_optimal_solves,mean_mip_gap\n";
    for (const auto& [key, entry] : grouped) {
        (void)key;
        const auto& row = entry.first;
        const auto& acc = entry.second;
        const double denom = static_cast<double>(acc.cases);
        summary_out
            << csv_escape(row.landscape) << ","
            << format_number(row.alpha) << ","
            << row.train_count << ","
            << csv_escape(display_method(row)) << ","
            << format_number(acc.sum_test_expected / denom) << ","
            << format_number(acc.sum_test_worst / denom) << ","
            << format_number(acc.sum_train_expected / denom) << ","
            << format_number(acc.sum_runtime / denom) << ","
            << acc.cases << ","
            << acc.optimal_solves << ","
            << format_number(acc.mip_gap_count > 0 ? acc.sum_mip_gap / acc.mip_gap_count : std::numeric_limits<double>::quiet_NaN())
            << "\n";
    }

    std::vector<BatchRow> fpp_rows;
    std::map<std::string, BatchRow> dpv_rows;
    for (const auto& row : rows) {
        if (row.method != "FPP-SAA" && row.method != "DPV-SAA") {
            continue;
        }
        if (row.method == "FPP-SAA") {
            fpp_rows.push_back(row);
        } else {
            dpv_rows[pairwise_key(row)] = row;
        }
    }

    AggregationSummary result;
    double relative_sum = 0.0;
    const auto pairwise_path = output_dir / "pairwise_comparison_fpp_vs_dpv.csv";
    std::ofstream pairwise_out(pairwise_path);
    if (!pairwise_out) {
        throw std::runtime_error("Could not open pairwise output: " + pairwise_path.string());
    }
    pairwise_out
        << "experiment_id,case_id,landscape,alpha,train_count,test_count,"
        << "fpp_mode,"
        << "test_expected_burned_area_fpp,test_expected_burned_area_dpv,"
        << "difference,relative_difference,winner\n";

    for (const auto& fpp : fpp_rows) {
        const auto found_dpv = dpv_rows.find(pairwise_key(fpp));
        if (found_dpv == dpv_rows.end()) {
            continue;
        }
        const auto& dpv = found_dpv->second;
        const double difference = dpv.test_expected - fpp.test_expected;
        const double relative = fpp.test_expected == 0.0
            ? std::numeric_limits<double>::quiet_NaN()
            : difference / fpp.test_expected;
        std::string winner = "Tie";
        if (dpv.test_expected < fpp.test_expected) {
            winner = "DPV-SAA";
            ++result.dpv_wins;
        } else if (fpp.test_expected < dpv.test_expected) {
            winner = "FPP-SAA";
            ++result.fpp_wins;
        } else {
            ++result.ties;
        }
        if (std::isfinite(relative)) {
            relative_sum += relative;
        }
        ++result.pairwise_cases;

        pairwise_out
            << csv_escape(fpp.experiment_id) << ","
            << fpp.case_id << ","
            << csv_escape(fpp.landscape) << ","
            << format_number(fpp.alpha) << ","
            << fpp.train_count << ","
            << fpp.test_count << ","
            << csv_escape(fpp.fpp_mode) << ","
            << format_number(fpp.test_expected) << ","
            << format_number(dpv.test_expected) << ","
            << format_number(difference) << ","
            << format_number(relative) << ","
            << winner << "\n";
    }

    if (result.pairwise_cases > 0) {
        result.average_relative_difference = relative_sum / static_cast<double>(result.pairwise_cases);
    }

    std::cout << "Aggregation complete.\n";
    std::cout << "Summary: " << firebreak::io::path_to_string(summary_path) << "\n";
    std::cout << "Pairwise: " << firebreak::io::path_to_string(pairwise_path) << "\n";
    std::cout << "FPP-SAA vs DPV-SAA cases: " << result.pairwise_cases << "\n";
    std::cout << "DPV-SAA wins: " << result.dpv_wins << "\n";
    std::cout << "FPP-SAA wins: " << result.fpp_wins << "\n";
    std::cout << "Ties: " << result.ties << "\n";
    std::cout << "Average relative difference (DPV - FPP) / FPP: "
              << format_number(result.average_relative_difference) << "\n";

    return result;
}

}  // namespace firebreak::analysis
