#pragma once

#include <span>
#include <string>
#include <string_view>

namespace iprop::ips {

// A bounded, deterministic edit-distance hint; empty if no close name exists.
std::string didYouMean(std::string_view misspelled, std::span<const std::string> candidates);

// Spellings the language accepted before 1.0. They are not aliases: the
// table only lets a diagnostic name the replacement instead of guessing one.
// Returns a hint such as "; it was renamed to 'noise.value'", or nothing.
std::string formerBuiltinHint(std::string_view function);
// The same for a named argument of a current builtin, e.g. noise.mask(kind=).
std::string formerArgumentHint(std::string_view function, std::string_view argument);

} // namespace iprop::ips
