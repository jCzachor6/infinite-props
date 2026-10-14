#pragma once

// Recursive-descent parser for .ips: a token stream (from the lexer) into
// the AST. Parsing collects bounded errors without exposing partial ASTs;
// parse() is the same pass stopped at, and throwing, the first error.

#include <string_view>
#include <cstddef>
#include <optional>
#include <vector>

#include "ips/Ast.hpp"
#include "ips/Lexer.hpp"

namespace iprop::ips {

// Lexes and parses a whole .ips source file, throwing the first
// diagnostic of parseRecovering as a SourceError.
Program parse(std::string_view source);

// Diagnostic parsing never exposes an incomplete AST to generation. Only a
// completely parsed, error-free file has program; imports contains only fully
// parsed import declarations and remains available on failure for discovery.
// Recovery uses token/brace/semicolon boundaries, never source newlines.
struct ParseResult {
    std::optional<Program> program;
    std::vector<Import> imports;
    std::vector<SourceError> diagnostics;
    bool limitReached = false;

    bool valid() const noexcept { return program.has_value(); }
};

inline constexpr std::size_t kMaxParseDiagnostics = 20;

// limit is clamped to [1, 20]. limitReached means parsing stopped at the cap,
// not that another error is known to exist. EOF produces at most one error;
// a lexical failure produces one lexer diagnostic and no AST/imports.
// Synchronization is conservative: the body of a malformed declaration or
// conditional header is discarded rather than diagnosed as sibling code.
ParseResult parseRecovering(std::string_view source,
                            std::size_t limit = kMaxParseDiagnostics);

// The parser grammar (reference):
//
//   program    := import* asset
//   import     := "import" ident "from" string ";"?
//   asset      := "asset" ident "{" (sizeDecl | originDecl | inputDecl | outputDecl | paletteDecl
//                  | function)* "}"
//   sizeDecl   := "size" int ("x" digits | int) ";"?
//   originDecl := "origin" expr "," expr ";"?
//   inputDecl  := "input" inputType ident "=" declarationConstant ";"?
//   declarationConstant := literal | "(" declarationConstant ")"
//                        | "-" constantAtom
//   constantAtom := literal | "(" constantAtom ")" // no second minus
//   outputDecl := "output" "list"? outputKind ident ";"?
//   paletteDecl := "palette" ident "=" "[" (expr ("," expr)*)? "]" ";"?
//   function   := "draw" block | name "(" params ")" returnType? block
//   params     := (param ("," param)*)?      // a trailing comma is allowed
//   param      := ident ":" paramType        // the type is mandatory pre-1.0
//   returnType := "->" paramType             // required iff the function returns
//   paramType  := "i32" | "f32" | "color" | "vec2"
//   block      := "{" (";" | statement)* "}"    // ';' is an optional separator
//   statement  := varDecl | exprStmt | ifStmt | whileStmt | returnStmt
//   varDecl    := ident "=" expr
//   exprStmt   := call
//
// The entry block has implicit read-only i32 builtins v and a; its runtime
// signature is synthesized by the compiler. Helpers keep typed parameters.
//
// Soft keywords (draw, at, color, f32/u32/i32, point/rect/circle, palette)
// are valid names in every name position; hard keywords stay reserved.
//
// Expressions use C-like precedence; postfix adds the palette index
// `expr "[" expr "]"`:
//   || < && < | < ^ < & < ==/!= < </<=/>/>= < <</>> < +,- < *,/,%
//   < unary < postfix

} // namespace iprop::ips
