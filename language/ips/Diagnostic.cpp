#include "ips/Diagnostic.hpp"

namespace iprop::ips {
std::string_view diagnosticCodeName(DiagnosticCode code) noexcept {
    switch (code) {
    case DiagnosticCode::Unspecified: return "IPS-INTERNAL-001";
    case DiagnosticCode::InvalidUtf8: return "IPS-LEX-001";
    case DiagnosticCode::LexicalError: return "IPS-LEX-002";
    case DiagnosticCode::ExpectedToken: return "IPS-PARSE-001";
    case DiagnosticCode::InvalidSyntax: return "IPS-PARSE-002";
    case DiagnosticCode::ImportCycle: return "IPS-IMPORT-001";
    case DiagnosticCode::SourceIO: return "IPS-IO-001";
    case DiagnosticCode::TypeError: return "IPS-TYPE-001";
    case DiagnosticCode::DeclarationConstant: return "IPS-TYPE-002";
    case DiagnosticCode::SignedDefaultRange: return "IPS-TYPE-003";
    case DiagnosticCode::UnsignedDefaultNegative: return "IPS-TYPE-004";
    case DiagnosticCode::PlanError: return "IPS-PLAN-001";
    case DiagnosticCode::ResourceLimit: return "IPS-LIMIT-001";
    case DiagnosticCode::CompiledValidation: return "IPS-VALIDATE-001";
    case DiagnosticCode::CLIArgument: return "IPS-CLI-001";
    case DiagnosticCode::InternalError: return "IPS-INTERNAL-001";
    }
    return "IPS-INTERNAL-001";
}
std::string_view diagnosticStageName(DiagnosticStage stage) noexcept {
    switch (stage) {
    case DiagnosticStage::Unspecified: return "internal";
    case DiagnosticStage::Lexer: return "lex";
    case DiagnosticStage::Parser: return "parse";
    case DiagnosticStage::Import: return "import";
    case DiagnosticStage::Typing: return "typing";
    case DiagnosticStage::Planning: return "planning";
    case DiagnosticStage::Validation: return "validation";
    case DiagnosticStage::IO: return "io";
    case DiagnosticStage::CLI: return "cli";
    case DiagnosticStage::Internal: return "internal";
    }
    return "internal";
}
std::string_view diagnosticSeverityName(DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case DiagnosticSeverity::Error: return "error";
    case DiagnosticSeverity::Warning: return "warning";
    case DiagnosticSeverity::Note: return "note";
    }
    return "error";
}
DiagnosticCode defaultDiagnosticCode(DiagnosticStage stage) noexcept {
    switch (stage) {
    case DiagnosticStage::Lexer: return DiagnosticCode::LexicalError;
    case DiagnosticStage::Parser: return DiagnosticCode::InvalidSyntax;
    case DiagnosticStage::Import: return DiagnosticCode::ImportCycle;
    case DiagnosticStage::Typing: return DiagnosticCode::TypeError;
    case DiagnosticStage::Planning: return DiagnosticCode::PlanError;
    case DiagnosticStage::Validation: return DiagnosticCode::CompiledValidation;
    case DiagnosticStage::IO: return DiagnosticCode::SourceIO;
    case DiagnosticStage::CLI: return DiagnosticCode::CLIArgument;
    case DiagnosticStage::Unspecified:
    case DiagnosticStage::Internal: return DiagnosticCode::InternalError;
    }
    return DiagnosticCode::InternalError;
}
} // namespace iprop::ips
