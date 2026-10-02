#pragma once

// The assembly layer: encodes a fully planned and emitted Plan into .iprop
// file bytes (spec §2–§6: header, section table, meta, const and code
// sections, part table). Reads the plan and the pools; produces the final
// byte vector.

#include <cstdint>
#include <vector>

#include "Plan.hpp"

namespace iprop::compiler {

class Assembler {
public:
    explicit Assembler(const Plan& plan);

    // Encodes the complete file. Throws ips::SourceError with a located
    // message for non-constant origins.
    std::vector<u8> assemble();

private:
    const Plan& plan_;
    std::string sourcePath_;

    [[noreturn]] void error(const ips::SourceLocation& at, std::string message);

    f32 foldOrigin(const ips::Expr& expr);
    void writeInputEntry(Writer& out, const ips::InputDecl& input);
};

} // namespace iprop::compiler
