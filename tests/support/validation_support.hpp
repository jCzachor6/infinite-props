#pragma once

#include "test_support.hpp"

#include <cstddef>
#include <vector>

namespace validation_test {

std::size_t streamRel(const std::vector<test_support::FuncSpec>& funcs, std::size_t fn);
void expectValidateError(const std::vector<iprop::u8>& fileBytes, std::size_t rel,
                         const char* messagePart);

} // namespace validation_test
