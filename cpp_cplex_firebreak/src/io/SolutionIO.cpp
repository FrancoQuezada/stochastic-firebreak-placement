#include "io/SolutionIO.hpp"

#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "io/PathUtils.hpp"

namespace firebreak::io {

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

int parse_positive_int(const std::string& token) {
    const std::string cleaned = trim(token);
    if (cleaned.empty()) {
        throw std::runtime_error("Empty firebreak node token.");
    }
    try {
        std::size_t consumed = 0;
        const int value = std::stoi(cleaned, &consumed);
        if (consumed != cleaned.size() || value <= 0) {
            throw std::runtime_error("Invalid firebreak node token: " + cleaned);
        }
        return value;
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Invalid firebreak node token: " + cleaned);
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Firebreak node token is out of range: " + cleaned);
    }
}

void write_int_array(std::ostream& out, const std::vector<int>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << values[i];
    }
    out << "]";
}

void write_double_array(std::ostream& out, const std::vector<double>& values) {
    out << std::fixed << std::setprecision(8);
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << values[i];
    }
    out << "]";
}

}  // namespace

void save_firebreak_solution_json(
    const std::filesystem::path& output_path,
    const FirebreakSolutionRecord& record) {
    ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open solution JSON file: " + output_path.string());
    }

    out << "{\n";
    out << "  \"method\": \"" << json_escape_local(record.method) << "\",\n";
    out << "  \"landscape\": \"" << json_escape_local(record.landscape) << "\",\n";
    out << "  \"alpha\": " << record.alpha << ",\n";
    out << "  \"budget\": " << record.budget << ",\n";
    out << "  \"selected_firebreak_original_nodes\": ";
    write_int_array(out, record.selected_firebreak_original_nodes);
    out << ",\n";
    out << "  \"selected_firebreak_indices\": ";
    write_int_array(out, record.selected_firebreak_indices);
    if (!record.objective_metric.empty()) {
        out << ",\n";
        out << "  \"objective_metric\": \"" << json_escape_local(record.objective_metric) << "\"";
    }
    if (!record.selected_firebreak_scores.empty()) {
        out << ",\n";
        out << "  \"selected_firebreak_scores\": ";
        write_double_array(out, record.selected_firebreak_scores);
    }
    out << "\n";
    out << "}\n";
}

void save_firebreak_solution_csv(
    const std::filesystem::path& output_path,
    const std::vector<int>& selected_firebreak_original_nodes) {
    ensure_parent_directory(output_path);
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open solution CSV file: " + output_path.string());
    }
    for (std::size_t i = 0; i < selected_firebreak_original_nodes.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << selected_firebreak_original_nodes[i];
    }
    out << "\n";
}

std::vector<int> load_firebreak_solution_csv(const std::filesystem::path& input_path) {
    std::ifstream in(input_path);
    if (!in) {
        throw std::runtime_error("Could not open solution CSV file: " + input_path.string());
    }

    std::vector<int> nodes;
    std::string line;
    while (std::getline(in, line)) {
        std::stringstream stream(line);
        std::string token;
        while (std::getline(stream, token, ',')) {
            if (trim(token).empty()) {
                continue;
            }
            nodes.push_back(parse_positive_int(token));
        }
    }
    return nodes;
}

}  // namespace firebreak::io
