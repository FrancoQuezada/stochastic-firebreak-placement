#include "analysis/RuntimeProfiler.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
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

struct RuntimeRow {
    std::string method;
    std::string fpp_mode;
    double alpha = 0.0;
    int train_count = 0;
    std::string solver_status;
    double mip_gap = std::numeric_limits<double>::quiet_NaN();
    double runtime = 0.0;
    double variables = std::numeric_limits<double>::quiet_NaN();
    double constraints = std::numeric_limits<double>::quiet_NaN();
};

struct RuntimeAccumulator {
    std::string method;
    double alpha = 0.0;
    int train_count = 0;
    std::vector<double> runtimes;
    double max_runtime = 0.0;
    double mip_gap_sum = 0.0;
    int mip_gap_count = 0;
    int optimal_count = 0;
    int non_optimal_count = 0;
    double variable_sum = 0.0;
    int variable_count = 0;
    double constraint_sum = 0.0;
    int constraint_count = 0;
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

std::string get_field(
    const std::vector<std::string>& fields,
    const std::unordered_map<std::string, std::size_t>& columns,
    const std::string& name) {
    const auto found = columns.find(name);
    if (found == columns.end() || found->second >= fields.size()) {
        return "";
    }
    return fields[found->second];
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

int parse_int_or_zero(const std::string& value) {
    if (value.empty()) {
        return 0;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return 0;
    }
}

std::vector<RuntimeRow> read_runtime_rows(const std::filesystem::path& batch_results_csv) {
    std::ifstream in(batch_results_csv);
    if (!in) {
        throw std::runtime_error("Could not open batch CSV for runtime profiling: " + batch_results_csv.string());
    }

    std::string header_line;
    if (!std::getline(in, header_line)) {
        throw std::runtime_error("Batch CSV is empty: " + batch_results_csv.string());
    }
    const auto header = parse_csv_line(header_line);
    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < header.size(); ++i) {
        columns[header[i]] = i;
    }

    std::vector<RuntimeRow> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = parse_csv_line(line);
        RuntimeRow row;
        row.method = get_field(fields, columns, "method");
        row.fpp_mode = get_field(fields, columns, "fpp_mode");
        if (row.method == "FPP-SAA" && !row.fpp_mode.empty()) {
            row.method += ":" + row.fpp_mode;
        }
        row.alpha = parse_double_or_nan(get_field(fields, columns, "alpha"));
        row.train_count = parse_int_or_zero(get_field(fields, columns, "train_scenario_count"));
        row.solver_status = get_field(fields, columns, "solver_status");
        row.mip_gap = parse_double_or_nan(get_field(fields, columns, "mip_gap"));
        row.runtime = parse_double_or_nan(get_field(fields, columns, "runtime_seconds"));
        row.variables = parse_double_or_nan(get_field(fields, columns, "num_variables"));
        row.constraints = parse_double_or_nan(get_field(fields, columns, "num_constraints"));
        if (!row.method.empty() && std::isfinite(row.alpha) && row.train_count > 0 && std::isfinite(row.runtime)) {
            rows.push_back(row);
        }
    }
    return rows;
}

std::string group_key(const RuntimeRow& row) {
    std::ostringstream out;
    out << row.method << "|" << std::fixed << std::setprecision(8) << row.alpha << "|" << row.train_count;
    return out.str();
}

bool is_optimal(const std::string& status) {
    return status.find("Optimal") != std::string::npos;
}

double mean(const std::vector<double>& values) {
    if (values.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double sum = 0.0;
    for (const double value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::sort(values.begin(), values.end());
    const std::size_t mid = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[mid];
    }
    return 0.5 * (values[mid - 1] + values[mid]);
}

std::string format_number(double value) {
    if (!std::isfinite(value)) {
        return "";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(8) << value;
    return out.str();
}

}  // namespace

void RuntimeProfiler::write_runtime_summary(
    const std::filesystem::path& batch_results_csv,
    const std::filesystem::path& output_csv) const {
    const auto rows = read_runtime_rows(batch_results_csv);

    std::map<std::string, RuntimeAccumulator> groups;
    for (const auto& row : rows) {
        auto& acc = groups[group_key(row)];
        if (acc.runtimes.empty()) {
            acc.method = row.method;
            acc.alpha = row.alpha;
            acc.train_count = row.train_count;
        }
        acc.runtimes.push_back(row.runtime);
        acc.max_runtime = std::max(acc.max_runtime, row.runtime);
        if (std::isfinite(row.mip_gap)) {
            acc.mip_gap_sum += row.mip_gap;
            ++acc.mip_gap_count;
        }
        if (is_optimal(row.solver_status)) {
            ++acc.optimal_count;
        } else {
            ++acc.non_optimal_count;
        }
        if (std::isfinite(row.variables)) {
            acc.variable_sum += row.variables;
            ++acc.variable_count;
        }
        if (std::isfinite(row.constraints)) {
            acc.constraint_sum += row.constraints;
            ++acc.constraint_count;
        }
    }

    firebreak::io::ensure_parent_directory(output_csv);
    std::ofstream out(output_csv);
    if (!out) {
        throw std::runtime_error("Could not open runtime summary CSV: " + output_csv.string());
    }

    out << "method,alpha,train_count,count,mean_runtime_seconds,median_runtime_seconds,max_runtime_seconds,"
        << "mean_mip_gap,number_optimal,number_non_optimal,mean_num_variables,mean_num_constraints\n";
    for (const auto& [key, acc] : groups) {
        (void)key;
        out << acc.method << ","
            << format_number(acc.alpha) << ","
            << acc.train_count << ","
            << acc.runtimes.size() << ","
            << format_number(mean(acc.runtimes)) << ","
            << format_number(median(acc.runtimes)) << ","
            << format_number(acc.max_runtime) << ","
            << format_number(acc.mip_gap_count > 0 ? acc.mip_gap_sum / acc.mip_gap_count : std::numeric_limits<double>::quiet_NaN()) << ","
            << acc.optimal_count << ","
            << acc.non_optimal_count << ","
            << format_number(acc.variable_count > 0 ? acc.variable_sum / static_cast<double>(acc.variable_count) : std::numeric_limits<double>::quiet_NaN()) << ","
            << format_number(acc.constraint_count > 0 ? acc.constraint_sum / static_cast<double>(acc.constraint_count) : std::numeric_limits<double>::quiet_NaN()) << "\n";
    }
}

}  // namespace firebreak::analysis
