#pragma once

#include <cstddef>
#include <string>

#include "ips/SourceError.hpp"

namespace iprop::ips {

// Half-open indices into Program::sourceTokens. Empty ranges also support
// ASTs constructed by tools rather than the parser.
struct TokenRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// Scalar asset metadata has last-declaration-wins semantics. Keep every
// source range, including superseded declarations, to preserve their trivia
// in the asset without duplicating semantic expression trees.
struct MetadataSyntax {
    enum class Kind { Size, Origin, Variants, Archetypes };
    Kind kind = Kind::Size;
    TokenRange range{};
};

// Own the exact bytes, including the delimiters and internal whitespace.
// Locations use the same byte offsets and Unicode columns as regular tokens.
struct SourceTrivia {
    enum class Kind { LineComment, BlockComment };
    Kind kind = Kind::LineComment;
    std::string text;
    SourceLocation location;
};

} // namespace iprop::ips
