#include "ips/Printer.hpp"
#include "ips/BuiltinArguments.hpp"
#include "ips/Literals.hpp"
#include "ips/Types.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <iomanip>
#include <sstream>
#include <type_traits>

namespace iprop::ips {
namespace {

std::string quoteSource(std::string_view text) {
    std::string result = "\"";
    for (const char c : text) {
        switch (c) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += c;
        }
    }
    return result + "\"";
}

std::string canonicalLiteral(const Literal& literal) {
    u32 bits = 0;
    if (literal.kind == Literal::Kind::Int && parseIntBits(literal.text, bits))
        return std::to_string(bits);
    if (literal.kind == Literal::Kind::Float && parseFloatBits(literal.text, bits)) {
        char text[64];
        const auto [end, status] = std::to_chars(text, text + sizeof(text), std::bit_cast<f32>(bits),
            std::chars_format::general);
        if (status != std::errc{}) throw SourceError{literal.location, "cannot format float literal"};
        std::string result(text, end);
        if (result.find_first_of(".eE") == std::string::npos) result += ".0";
        return result;
    }
    if (literal.kind == Literal::Kind::Color) {
        Color color;
        if (parseColor(literal.text, color)) {
            std::ostringstream out;
            out << '#' << std::hex << std::setfill('0');
            for (const auto channel : {color.r, color.g, color.b}) out << std::setw(2) << static_cast<unsigned>(channel);
            if (color.a != 255) out << std::setw(2) << static_cast<unsigned>(color.a);
            return out.str();
        }
    }
    if (literal.kind == Literal::Kind::String) return quoteSource(literal.text);
    if (literal.kind == Literal::Kind::True) return "true";
    if (literal.kind == Literal::Kind::False) return "false";
    throw SourceError{literal.location, "invalid literal in source printer"};
}

const char* binarySymbol(Binary::Op op) {
    switch (op) {
    case Binary::Op::Add: return "+"; case Binary::Op::Sub: return "-";
    case Binary::Op::Mul: return "*"; case Binary::Op::Div: return "/";
    case Binary::Op::Mod: return "%"; case Binary::Op::Eq: return "==";
    case Binary::Op::Ne: return "!="; case Binary::Op::Lt: return "<";
    case Binary::Op::Le: return "<="; case Binary::Op::Gt: return ">";
    case Binary::Op::Ge: return ">="; case Binary::Op::And: return "&&";
    case Binary::Op::Or: return "||"; case Binary::Op::Xor: return "^";
    case Binary::Op::BitAnd: return "&"; case Binary::Op::BitOr: return "|";
    case Binary::Op::Shl: return "<<"; case Binary::Op::Shr: return ">>";
    }
    throw std::logic_error("unknown binary operator");
}

std::string calleeName(const Expr& expr) {
    if (const auto* id = std::get_if<Identifier>(&expr)) return id->name;
    if (const auto* member = std::get_if<Member>(&expr)) {
        const auto base = calleeName(*member->object);
        if (!base.empty()) return base + "." + member->name;
    }
    return {};
}

class SourcePrinter {
    std::ostringstream out_;

    void indent(std::size_t level) { out_ << std::string(level * 2, ' '); }
    void expression(const Expr& expr) {
        std::visit([&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Literal>) out_ << canonicalLiteral(node);
            else if constexpr (std::is_same_v<T, Identifier>) out_ << node.name;
            else if constexpr (std::is_same_v<T, Member>) { expression(*node.object); out_ << '.' << node.name; }
            else if constexpr (std::is_same_v<T, Index>) { expression(*node.object); out_ << '['; expression(*node.index); out_ << ']'; }
            else if constexpr (std::is_same_v<T, Unary>) {
                out_ << '(' << (node.op == Unary::Op::Neg ? "-" : node.op == Unary::Op::Not ? "!" : "~");
                expression(*node.operand); out_ << ')';
            } else if constexpr (std::is_same_v<T, Binary>) {
                out_ << '('; expression(*node.left); out_ << ' ' << binarySymbol(node.op) << ' ';
                expression(*node.right); out_ << ')';
            } else if constexpr (std::is_same_v<T, VectorLit>) {
                out_ << '{';
                for (std::size_t i = 0; i < node.elements.size(); ++i) {
                    if (i) out_ << ", ";
                    expression(*node.elements[i]);
                }
                out_ << '}';
            } else if constexpr (std::is_same_v<T, Call>) {
                expression(*node.callee); out_ << '(';
                std::vector<const Arg*> args;
                const auto name = calleeName(*node.callee);
                if (const auto found = builtinSpecs().find(name); found != builtinSpecs().end())
                    args = builtinArguments(node, found->second, name);
                else if (name == "ramp") {
                    const std::vector<std::string> names{"palette", "t"};
                    args = bindArguments(node, names, name);
                } else {
                    for (const auto& arg : node.args) args.push_back(&arg);
                    if (name.ends_with(".draw")) std::stable_sort(args.begin(), args.end(), [](const Arg* a, const Arg* b) {
                        if (a->name && b->name) return *a->name < *b->name;
                        return !a->name && b->name;
                    });
                }
                for (std::size_t i = 0; i < args.size(); ++i) {
                    if (i) out_ << ", ";
                    if (args[i]->name) out_ << *args[i]->name << " = ";
                    expression(*args[i]->value);
                }
                out_ << ')';
            }
        }, expr);
    }

    void block(const std::vector<Stmt>& body, std::size_t level) {
        out_ << " {\n";
        for (const auto& stmt : body) statement(stmt, level + 1);
        indent(level); out_ << '}';
    }
    void statement(const Stmt& stmt, std::size_t level) {
        indent(level);
        std::visit([&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, VarDecl>) { out_ << node.name << " = "; expression(*node.value); }
            else if constexpr (std::is_same_v<T, ExprStmt>) expression(*node.expr);
            else if constexpr (std::is_same_v<T, ReturnStmt>) {
                out_ << "return";
                if (node.value) { out_ << ' '; expression(*node.value); }
                else out_ << ';';
            } else if constexpr (std::is_same_v<T, BreakStmt>) out_ << "break";
            else if constexpr (std::is_same_v<T, ContinueStmt>) out_ << "continue";
            else if constexpr (std::is_same_v<T, ForStmt>) {
                out_ << "for " << node.variable << " in "; expression(*node.from);
                out_ << ".."; expression(*node.to); block(node.body, level);
            } else if constexpr (std::is_same_v<T, WhileStmt> || std::is_same_v<T, IfStmt>) {
                out_ << (std::is_same_v<T, WhileStmt> ? "while (" : "if ("); expression(*node.condition); out_ << ')';
                if constexpr (std::is_same_v<T, WhileStmt>) block(node.body, level);
                else {
                    block(node.thenBranch, level);
                    if (!node.elseBranch.empty()) { out_ << " else"; block(node.elseBranch, level); }
                }
            }
        }, stmt);
        out_ << '\n';
    }

public:
    std::string print(const Program& program) {
        for (const auto& import : program.imports)
            out_ << "import " << import.alias << " from " << quoteSource(import.path) << '\n';
        if (!program.imports.empty()) out_ << '\n';
        const auto& asset = program.asset;
        if (asset.sizeX.has_value() != asset.sizeY.has_value())
            throw SourceError{asset.location, "size requires both width and height"};
        if (static_cast<bool>(asset.originX) != static_cast<bool>(asset.originY))
            throw SourceError{asset.location, "origin requires both coordinates"};
        out_ << "asset " << asset.name << " {\n";
        if (asset.sizeX && asset.sizeY) out_ << "  size " << *asset.sizeX << 'x' << *asset.sizeY << '\n';
        if (asset.originX && asset.originY) {
            out_ << "  origin "; expression(*asset.originX); out_ << ", "; expression(*asset.originY); out_ << '\n';
        }
        if (asset.variants) out_ << "  variants " << *asset.variants << '\n';
        if (asset.archetypes) out_ << "  archetypes " << *asset.archetypes << '\n';
        for (const auto& input : asset.inputs) {
            out_ << "  input " << (input.type == InputType::U32 ? "u32" : typeName(valueTypeOf(input.type)))
                 << ' ' << input.name << " = "; expression(*input.defaultValue); out_ << '\n';
        }
        for (const auto& output : asset.outputs) {
            const char* kinds[] = {"point", "rect", "circle"};
            if (static_cast<unsigned>(output.kind) >= 3)
                throw SourceError{output.location, "invalid output kind in source printer"};
            out_ << "  output " << (output.isList ? "list " : "")
                 << kinds[static_cast<unsigned>(output.kind)] << ' ' << output.name << '\n';
        }
        for (const auto& palette : asset.palettes) {
            out_ << "  palette " << palette.name << " = [";
            for (std::size_t i = 0; i < palette.elements.size(); ++i) {
                if (i) out_ << ", ";
                expression(*palette.elements[i]);
            }
            out_ << "]\n";
        }
        for (const auto& function : asset.functions) {
            if (function.name == "draw" && (!function.params.empty() || function.returnType))
                throw SourceError{function.location, "draw has no declared parameters or return type"};
            out_ << '\n' << "  " << function.name;
            if (function.name != "draw") {
                out_ << '(';
                for (std::size_t i = 0; i < function.params.size(); ++i) {
                    if (i) out_ << ", ";
                    out_ << function.params[i].name << ": " << typeName(valueTypeOf(function.params[i].type));
                }
                out_ << ')';
                if (function.returnType) out_ << " -> " << typeName(valueTypeOf(*function.returnType));
            }
            block(function.body, 1); out_ << '\n';
        }
        out_ << "}\n";
        return out_.str();
    }
};
}

std::string formatProgram(const Program& program) { return SourcePrinter{}.print(program); }

} // namespace iprop::ips
