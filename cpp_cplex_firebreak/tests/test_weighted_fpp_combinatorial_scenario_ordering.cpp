#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "benders/FppCombinatorialBenders.hpp"

namespace {

void assert_vector_eq(const std::vector<int>& actual, const std::vector<int>& expected) {
    assert(actual == expected);
}

void test_eta_ascending_uses_eta_and_scenario_id_ties() {
    const std::vector<double> eta = {5.0, 1.0, 5.0, 1.0};
    const std::vector<int> scenario_ids = {40, 30, 10, 20};
    const auto order = firebreak::benders::order_fpp_combinatorial_scenarios_by_eta(
        eta,
        scenario_ids,
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaAscending);
    assert_vector_eq(order, {3, 1, 2, 0});
}

void test_eta_descending_uses_eta_and_scenario_id_ties() {
    const std::vector<double> eta = {5.0, 1.0, 5.0, 1.0};
    const std::vector<int> scenario_ids = {40, 30, 10, 20};
    const auto order = firebreak::benders::order_fpp_combinatorial_scenarios_by_eta(
        eta,
        scenario_ids,
        firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaDescending);
    assert_vector_eq(order, {2, 0, 3, 1});
}

void test_complete_permutation() {
    const std::vector<double> eta = {3.0, 3.0, 2.0, 4.0, 4.0};
    const std::vector<int> scenario_ids = {5, 4, 3, 2, 1};
    for (const auto mode : {
             firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaAscending,
             firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaDescending,
         }) {
        auto order = firebreak::benders::order_fpp_combinatorial_scenarios_by_eta(
            eta,
            scenario_ids,
            mode);
        std::sort(order.begin(), order.end());
        assert_vector_eq(order, {0, 1, 2, 3, 4});
    }
}

void test_mismatched_ids_rejected() {
    bool threw = false;
    try {
        (void)firebreak::benders::order_fpp_combinatorial_scenarios_by_eta(
            std::vector<double>{1.0, 2.0},
            std::vector<int>{1},
            firebreak::benders::FppCombinatorialBendersScenarioOrder::EtaAscending);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

}  // namespace

int main() {
    test_eta_ascending_uses_eta_and_scenario_id_ties();
    test_eta_descending_uses_eta_and_scenario_id_ties();
    test_complete_permutation();
    test_mismatched_ids_rejected();
    std::cout << "All weighted FPP combinatorial scenario-ordering tests passed.\n";
    return 0;
}
