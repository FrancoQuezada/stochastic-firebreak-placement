#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "opt/IndexMapper.hpp"

namespace {

void test_forward_and_reverse_mapping() {
    firebreak::opt::IndexMapper mapper;
    mapper.build_from_nodes({7, 1, 4});

    assert(mapper.size() == 3);
    assert(mapper.to_index(1) == 0);
    assert(mapper.to_index(4) == 1);
    assert(mapper.to_index(7) == 2);
    assert(mapper.to_node(0) == 1);
    assert(mapper.to_node(1) == 4);
    assert(mapper.to_node(2) == 7);
}

void test_missing_node_behavior() {
    firebreak::opt::IndexMapper mapper;
    mapper.build_from_nodes({1, 4, 7});

    bool missing_node_threw = false;
    try {
        (void)mapper.to_index(99);
    } catch (const std::runtime_error&) {
        missing_node_threw = true;
    }
    assert(missing_node_threw);

    bool missing_index_threw = false;
    try {
        (void)mapper.to_node(3);
    } catch (const std::runtime_error&) {
        missing_index_threw = true;
    }
    assert(missing_index_threw);
}

void test_contains_and_deterministic_ordering() {
    firebreak::opt::IndexMapper mapper;
    mapper.build_from_nodes({4, 1, 4, 7, 1});

    assert(mapper.size() == 3);
    assert(mapper.contains_node(4));
    assert(!mapper.contains_node(5));
    assert((mapper.original_nodes() == std::vector<int>{1, 4, 7}));
}

void test_invalid_node_rejected() {
    firebreak::opt::IndexMapper mapper;
    bool threw = false;
    try {
        mapper.build_from_nodes({1, 0, 2});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

}  // namespace

int main() {
    test_forward_and_reverse_mapping();
    test_missing_node_behavior();
    test_contains_and_deterministic_ordering();
    test_invalid_node_rejected();
    std::cout << "All index mapper tests passed.\n";
    return 0;
}

