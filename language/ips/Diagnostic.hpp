#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace iprop::ips {

// One-based Unicode codepoint positions. Byte ranges are added separately;
// neither a tab nor an astral Unicode scalar is multiple columns here.
struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;
};

enum class DiagnosticSeverity { Error, Warning, Note };
enum class DiagnosticStage { Unspecified, Lexer, Parser, Import, Typing, Planning, Validation, IO, CLI, Internal };
enum class DiagnosticCode {
    Unspecified, InvalidUtf8, LexicalError, ExpectedToken, InvalidSyntax,
    ImportCycle, SourceIO, TypeError, DeclarationConstant, SignedDefaultRange,
    UnsignedDefaultNegative, PlanError, ResourceLimit, CompiledValidation,
    CLIArgument, InternalError
};

// Stable protocol identifiers are assigned from typed causes/stages, never
// inferred by matching a human message. Generic codes cover a stage until a
// specific cause is explicitly annotated; existing identifiers never change.
std::string_view diagnosticCodeName(DiagnosticCode code) noexcept;
std::string_view diagnosticStageName(DiagnosticStage stage) noexcept;
std::string_view diagnosticSeverityName(DiagnosticSeverity severity) noexcept;
DiagnosticCode defaultDiagnosticCode(DiagnosticStage stage) noexcept;

struct Diagnostic {
    std::string message;
    std::string path;
    std::optional<SourceLocation> location;
    DiagnosticCode code = DiagnosticCode::InternalError;
    DiagnosticStage stage = DiagnosticStage::Internal;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
};

} // namespace iprop::ips
