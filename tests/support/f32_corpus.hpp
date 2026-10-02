#pragma once
#include "test_support.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace test_support {
inline std::vector<iprop::f32> f32BoundaryCorpus() {
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "fixtures/f32-boundaries.txt";
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot read float boundary corpus");
    std::vector<iprop::f32> values;
    for (std::string line; std::getline(input, line);)
        values.push_back(f32FromBits(static_cast<iprop::u32>(std::stoul(line, nullptr, 16))));
    return values;
}
}
