#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "io/ScenarioFileUtils.hpp"
#include "io/ScenarioSplitUtils.hpp"

namespace {

bool disjoint(const std::vector<int>& a, const std::vector<int>& b) {
    const std::unordered_set<int> a_set(a.begin(), a.end());
    for (const int value : b) {
        if (a_set.find(value) != a_set.end()) {
            return false;
        }
    }
    return true;
}

void test_deterministic_split() {
    const std::vector<int> ids{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const auto split_a = firebreak::io::generate_train_test_split(ids, 123, 3, 4);
    const auto split_b = firebreak::io::generate_train_test_split(ids, 123, 3, 4);

    assert(split_a.train_ids == split_b.train_ids);
    assert(split_a.test_ids == split_b.test_ids);
    assert(split_a.train_ids.size() == 3);
    assert(split_a.test_ids.size() == 4);
    assert(disjoint(split_a.train_ids, split_a.test_ids));
    assert(std::is_sorted(split_a.train_ids.begin(), split_a.train_ids.end()));
    assert(std::is_sorted(split_a.test_ids.begin(), split_a.test_ids.end()));
}

void test_invalid_request_fails() {
    bool threw = false;
    try {
        (void)firebreak::io::generate_train_test_split({1, 2, 3}, 123, 2, 2);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_duplicate_available_ids_fail() {
    bool threw = false;
    try {
        (void)firebreak::io::generate_train_test_split({1, 2, 2, 3}, 123, 1, 1);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_parse_scenario_id_ranges() {
    const auto dash_range = firebreak::io::parse_scenario_id_list("1-5");
    assert((dash_range == std::vector<int>{1, 2, 3, 4, 5}));

    const auto colon_range = firebreak::io::parse_scenario_id_list("6:10");
    assert((colon_range == std::vector<int>{6, 7, 8, 9, 10}));

    const auto mixed = firebreak::io::parse_scenario_id_list("1-3, 7, 10:12");
    assert((mixed == std::vector<int>{1, 2, 3, 7, 10, 11, 12}));
}

void test_invalid_scenario_id_ranges_fail() {
    bool descending_threw = false;
    try {
        (void)firebreak::io::parse_scenario_id_list("5-1");
    } catch (const std::runtime_error&) {
        descending_threw = true;
    }
    assert(descending_threw);

    bool malformed_threw = false;
    try {
        (void)firebreak::io::parse_scenario_id_list("1-2-3");
    } catch (const std::runtime_error&) {
        malformed_threw = true;
    }
    assert(malformed_threw);
}

}  // namespace

int main() {
    test_deterministic_split();
    test_invalid_request_fails();
    test_duplicate_available_ids_fail();
    test_parse_scenario_id_ranges();
    test_invalid_scenario_id_ranges_fail();
    std::cout << "All scenario split utility tests passed.\n";
    return 0;
}
