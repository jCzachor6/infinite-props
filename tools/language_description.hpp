#pragma once

#include <iosfwd>
#include <string_view>

namespace iprop::tools {
void writeLanguageDescription(std::ostream& out, std::string_view productVersion);
}
