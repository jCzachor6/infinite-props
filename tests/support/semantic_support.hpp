#pragma once

#include "ips/Semantics.hpp"

#include <map>
#include <string>

namespace semantic_test {

void expectCheckError(const std::string& source,
                      const std::map<std::string, iprop::ips::PartInfo>& parts,
                      const char* messagePart);
void expectCheckError(const std::string& source, const char* messagePart);
std::string wrap(const std::string& body);

} // namespace semantic_test
