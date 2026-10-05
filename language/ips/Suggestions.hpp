#pragma once

#include <span>
#include <string>
#include <string_view>

namespace iprop::ips {

// A bounded, deterministic edit-distance hint; empty if no close name exists.
std::string didYouMean(std::string_view misspelled, std::span<const std::string> candidates);

} // namespace iprop::ips
