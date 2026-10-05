#pragma once

// Recursive-descent parser for .ips: a token stream (from the lexer) into
// the AST. Throws SourceError on the first syntax error, with the location
// of the offending token.

#include <string_view>
#include <vector>

#include "ips/Ast.hpp"
#include "ips/Lexer.hpp"

namespace iprop::ips {

// Lexes and parses a whole .ips source file.
Program parse(std::string_view source);

// Parses an already-lexed token stream (mostly for tests and tools that
// want to pre-process the tokens).
Program parseTokens(const std::vector<Token>& tokens);

// The parser grammar (reference):
//
//   program    := import* asset
//   import     := "import" ident "from" string
//   asset      := "asset" ident "{" (sizeDecl | originDecl | variantsDecl
//                  | archetypesDecl | inputDecl | outputDecl | paletteDecl
//                  | function)* "}"
//   sizeDecl   := "size" int ("x" digits | int)
//   originDecl := "origin" expr "," expr
//   inputDecl  := "input" inputType ident "=" declarationConstant
//   declarationConstant := literal | "(" declarationConstant ")"
//                        | "-" constantAtom
//   constantAtom := literal | "(" constantAtom ")" // no second minus
//   outputDecl := "output" "list"? outputKind ident
//   paletteDecl := "palette" ident "=" "[" (expr ("," expr)*)? "]"
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
