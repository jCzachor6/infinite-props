#include "ips/Printer.hpp"
#include "ips/BuiltinArguments.hpp"
#include "ips/CallPlan.hpp"
#include "ips/Literals.hpp"
#include "ips/Types.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
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
            // Plain string appends: an iostream would turn an allocation
            // failure into a silently truncated literal on some libraries.
            constexpr char kHex[] = "0123456789abcdef";
            std::string out = "#";
            const auto append = [&](unsigned channel) {
                out += kHex[channel >> 4];
                out += kHex[channel & 15];
            };
            append(color.r);
            append(color.g);
            append(color.b);
            if (color.a != 255) append(color.a);
            return out;
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

// A token-aware sink associates emitted AST syntax with the owning source
// snapshot. Nested ranges, rather than comment fields on every AST variant,
// keep trivia at declaration/statement/expression boundaries.
class SourceOutput {
    const std::vector<Token>* tokens_ = nullptr;
    struct Frame { TokenRange range; std::size_t cursor; };
    std::vector<Frame> frames_;
    std::vector<bool> leading_, trailing_;
    std::string text_;
    std::size_t level_ = 0;
    bool commentEndedLine_ = false;

    bool lineHasCode() const {
        const auto begin = text_.find_last_of('\n');
        return text_.find_first_not_of(" \t\r", begin == std::string::npos ? 0 : begin + 1) != std::string::npos;
    }
    void indentation() {
        if (lineHasCode()) return;
        while (!text_.empty() && (text_.back() == ' ' || text_.back() == '\t')) text_.pop_back();
        text_ += std::string(level_ * 2, ' ');
    }
    void comment(const SourceTrivia& trivia, bool leading) {
        leading = leading || !lineHasCode();
        if (leading && lineHasCode()) text_ += '\n';
        if (!lineHasCode()) indentation();
        else if (!text_.empty() && text_.back() != ' ') text_ += ' ';
        text_ += trivia.text;
        if (leading || trivia.kind == SourceTrivia::Kind::LineComment) {
            text_ += '\n';
            commentEndedLine_ = !leading;
        } else text_ += ' ';
    }
    void trivia(std::size_t index, bool before, bool after) {
        if (!tokens_ || index >= tokens_->size()) return;
        const auto& token = (*tokens_)[index];
        if (before && !leading_[index]) {
            for (const auto& item : token.leadingTrivia) comment(item, true);
            leading_[index] = true;
        }
        if (after && !trailing_[index]) {
            for (const auto& item : token.trailingTrivia) comment(item, false);
            trailing_[index] = true;
        }
    }
    void drain(std::size_t end) {
        if (frames_.empty()) return;
        auto& frame = frames_.back();
        end = std::min(end, frame.range.end);
        while (frame.cursor < end) {
            const auto at = frame.cursor++;
            const auto& token = (*tokens_)[at];
            // A separator carrying comments must remain a token: erasing it
            // would change the owner after multiline or standalone comments.
            if (token.kind == TokenKind::Semi &&
                (!token.leadingTrivia.empty() || !token.trailingTrivia.empty())) {
                trivia(at, true, false);
                indentation(); text_ += ';'; commentEndedLine_ = false;
                trivia(at, false, true);
                // Retained separators close their statement/declaration even
                // when the parser discarded an empty separator production.
                if (lineHasCode()) { text_ += '\n'; commentEndedLine_ = true; }
            } else trivia(at, true, true);
        }
    }
    void token(TokenKind kind, std::string_view text) {
        std::optional<std::size_t> original;
        if (!frames_.empty() && tokens_) {
            auto& frame = frames_.back();
            // Match emitted AST syntax to the remaining source tokens.
            auto at = frame.cursor;
            while (at < frame.range.end && (*tokens_)[at].kind != kind) ++at;
            if (at < frame.range.end) {
                drain(at);
                trivia(at, true, false);
                original = at;
                frame.cursor = at + 1;
            }
        }
        indentation();
        text_ += text;
        commentEndedLine_ = false;
        if (original) trivia(*original, false, true);
    }
public:
    class Scope {
        SourceOutput* output_;
        std::size_t depth_;
    public:
        Scope(SourceOutput& output, TokenRange range, bool ordered)
            : output_(&output), depth_(output.frames_.size()) {
            try { output.begin(range, ordered); }
            catch (...) { output.discard(depth_); throw; }
        }
        Scope(const Scope&) = delete;
        // Unwinding must never try to append comments or allocate output.
        ~Scope() noexcept { if (output_) output_->discard(depth_); }
        void close() {
            output_->end(depth_);
            output_ = nullptr;
        }
    };
    void source(const std::vector<Token>& tokens) {
        tokens_ = &tokens;
        leading_.assign(tokens.size(), false);
        trailing_.assign(tokens.size(), false);
    }
    void level(std::size_t value) { level_ = value; }
    void begin(TokenRange range, bool ordered = true) {
        if (!tokens_ || range.end > tokens_->size() || range.begin > range.end) range = {};
        if (range.begin < range.end && ordered && !frames_.empty() && frames_.back().cursor <= range.begin) drain(range.begin);
        frames_.push_back({range, range.begin});
        if (range.begin < range.end) trivia(range.begin, true, false);
    }
    void discard(std::size_t depth) noexcept {
        while (frames_.size() > depth) frames_.pop_back();
    }
    void end(std::size_t depth) {
        const auto range = frames_[depth].range;
        drain(range.end);
        discard(depth);
        if (!frames_.empty() && range.begin < range.end) frames_.back().cursor = range.end;
    }
    void finishRange() { if (!frames_.empty()) drain(frames_.back().range.end); }
    bool sourceStarts(TokenKind kind) const {
        return tokens_ && !frames_.empty() && frames_.back().cursor < frames_.back().range.end &&
            (*tokens_)[frames_.back().cursor].kind == kind;
    }
    void prepare(TokenKind kind) {
        if (!tokens_ || frames_.empty()) return;
        const auto& frame = frames_.back();
        auto at = frame.cursor;
        while (at < frame.range.end && (*tokens_)[at].kind != kind) ++at;
        if (at < frame.range.end) { drain(at); trivia(at, true, false); }
    }
    bool hasTrivia(TokenRange range) const {
        if (!tokens_ || range.end > tokens_->size()) return false;
        for (auto i = range.begin; i < range.end; ++i)
            if (!(*tokens_)[i].leadingTrivia.empty() || !(*tokens_)[i].trailingTrivia.empty()) return true;
        return false;
    }
    std::size_t retainedGroups(TokenRange range, TokenRange core) const {
        if (!tokens_ || range.end > tokens_->size() || core.begin < range.begin ||
            core.end > range.end || core.begin == core.end) return 0;
        const auto count = core.begin - range.begin;
        if (range.end - core.end != count || count == 0) return 0;
        for (auto i = range.begin; i < core.begin; ++i)
            if ((*tokens_)[i].kind != TokenKind::LParen) return 0;
        for (auto i = core.end; i < range.end; ++i)
            if ((*tokens_)[i].kind != TokenKind::RParen) return 0;
        return hasTrivia({range.begin, core.begin}) || hasTrivia({core.end, range.end}) ? count : 0;
    }
    bool hasComments(TokenRange range) const {
        if (!tokens_ || range.end > tokens_->size()) return false;
        for (auto i = range.begin; i < range.end; ++i)
            if ((i > range.begin && !(*tokens_)[i].leadingTrivia.empty()) ||
                (i + 1 < range.end && !(*tokens_)[i].trailingTrivia.empty())) return true;
        return false;
    }
    // Synthetic binary/unary parentheses have no source token. Emit leading
    // trivia first, then let child ranges consume redundant original groups.
    void raw(std::string_view text) { indentation(); text_ += text; commentEndedLine_ = false; }
    SourceOutput& operator<<(std::string_view text) {
        if (text.empty()) return *this;
        const auto tokens = lex(text);
        std::size_t cursor = 0;
        for (const auto& item : tokens) {
            const auto gap = text.substr(cursor, item.location.byteOffset - cursor);
            for (const auto c : gap) {
                if (c == '\n') {
                    if (!commentEndedLine_) text_ += c;
                    commentEndedLine_ = false;
                } else if (lineHasCode() && (text_.empty() || text_.back() != ' ')) text_ += ' ';
            }
            if (item.kind == TokenKind::End) break;
            token(item.kind, text.substr(item.location.byteOffset, item.location.endByteOffset - item.location.byteOffset));
            cursor = item.location.endByteOffset;
        }
        return *this;
    }
    SourceOutput& operator<<(const std::string& text) { return *this << std::string_view(text); }
    SourceOutput& operator<<(const char* text) { return *this << std::string_view(text); }
    SourceOutput& operator<<(char c) { return *this << std::string_view(&c, 1); }
    SourceOutput& operator<<(std::uint32_t value) { return *this << std::to_string(value); }
    std::string str() {
        // EOF comments are owned by the End token. Also retain trivia from
        // optional empty separators, which do not produce AST nodes.
        if (tokens_) for (std::size_t i = 0; i < tokens_->size(); ++i) trivia(i, true, true);
        while (!text_.empty() && (text_.back() == ' ' || text_.back() == '\t')) text_.pop_back();
        if (!text_.empty() && text_.back() != '\n') text_ += '\n';
        return text_;
    }
};

class SourcePrinter {
    SourceOutput out_;
    const Program* program_ = nullptr;
    const std::map<std::string, const Program*>& parts_;

    void indent(std::size_t level) { out_.level(level); }
    void expression(const Expr& expr) {
        SourceOutput::Scope range(out_, expr.sourceRange, true);
        const auto groups = out_.retainedGroups(expr.sourceRange, expr.sourceCoreRange);
        for (std::size_t i = 0; i < groups; ++i) out_ << '(';
        std::visit([&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Literal>) out_ << canonicalLiteral(node);
            else if constexpr (std::is_same_v<T, Identifier>) out_ << node.name;
            else if constexpr (std::is_same_v<T, Member>) { expression(*node.object); out_ << '.' << node.name; }
            else if constexpr (std::is_same_v<T, Index>) { expression(*node.object); out_ << '['; expression(*node.index); out_ << ']'; }
            else if constexpr (std::is_same_v<T, Unary>) {
                if (!groups) out_.raw("(");
                out_ << (node.op == Unary::Op::Neg ? "-" : node.op == Unary::Op::Not ? "!" : "~");
                expression(*node.operand); if (!groups) out_ << ')';
            } else if constexpr (std::is_same_v<T, Binary>) {
                if (!groups) out_.raw("(");
                expression(*node.left); out_ << ' ' << binarySymbol(node.op) << ' ';
                expression(*node.right); if (!groups) out_ << ')';
            } else if constexpr (std::is_same_v<T, RandomRange>) {
                // Always grouped, so no surrounding operator seems to bind a bound.
                if (!groups) out_.raw("(");
                expression(*node.lower); out_ << " to ";
                expression(*node.upper); if (!groups) out_ << ')';
            } else if constexpr (std::is_same_v<T, VectorLit>) {
                out_ << '{';
                for (std::size_t i = 0; i < node.elements.size(); ++i) {
                    if (i) out_ << ", ";
                    expression(*node.elements[i]);
                }
                out_ << '}';
            } else if constexpr (std::is_same_v<T, Call>) {
                expression(*node.callee); out_ << '(';
                std::vector<PrintedArgument> args;
                if (out_.hasComments(expr.sourceRange)) {
                    for (const auto& arg : node.args) args.push_back({&arg, arg.name});
                } else {
                    const auto plan = sourceCallPlan(*program_, node, parts_);
                    args = printedCallArguments(node, plan ? &*plan : nullptr);
                }
                for (std::size_t i = 0; i < args.size(); ++i) {
                    if (i) out_ << ", ";
                    if (args[i].name) out_ << *args[i].name << " = ";
                    expression(*args[i].argument->value);
                }
                out_ << ')';
            }
        }, expr);
        for (std::size_t i = 0; i < groups; ++i) out_ << ')';
        range.close();
    }

    void block(const std::vector<Stmt>& body, std::size_t level, bool synthetic = false) {
        if (synthetic) { out_ << ' '; out_.raw("{"); out_ << '\n'; }
        else out_ << " {\n";
        for (const auto& stmt : body) statement(stmt, level + 1);
        indent(level + 1);
        if (!synthetic) out_.prepare(TokenKind::RBrace);
        indent(level);
        if (synthetic) out_.raw("}");
        else out_ << '}';
    }
    void statement(const Stmt& stmt, std::size_t level) {
        indent(level);
        SourceOutput::Scope range(out_, stmt.sourceRange, true);
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
                    if (!node.elseBranch.empty() || out_.hasTrivia(node.elseRange)) {
                        out_ << " else";
                        block(node.elseBranch, level, out_.sourceStarts(TokenKind::KeywordIf));
                    }
                }
            }
        }, stmt);
        out_.finishRange(); out_ << '\n'; range.close();
    }

    void sourceSyntax(const Program& program, TokenRange sourceRange) {
        if (sourceRange.end > program.sourceTokens.size()) return;
        indent(1); SourceOutput::Scope range(out_, sourceRange, false);
        bool first = true;
        for (auto i = sourceRange.begin; i < sourceRange.end; ++i) {
            const auto& token = program.sourceTokens[i];
            if (!first) out_ << ' ';
            out_ << (token.kind == TokenKind::String ? quoteSource(token.text) : token.text);
            first = false;
        }
        out_.finishRange(); out_ << '\n'; range.close();
    }
    TokenRange metadata(const Program& program, MetadataSyntax::Kind kind) {
        std::vector<TokenRange> ranges;
        for (const auto& declaration : program.asset.metadataSyntax)
            if (declaration.kind == kind) ranges.push_back(declaration.range);
        for (std::size_t i = 0; i + 1 < ranges.size(); ++i) sourceSyntax(program, ranges[i]);
        return ranges.empty() ? TokenRange{} : ranges.back();
    }
public:
    explicit SourcePrinter(const std::map<std::string, const Program*>& parts) : parts_(parts) {}
    std::string print(const Program& program) {
        program_ = &program;
        out_.source(program.sourceTokens);
        for (const auto& import : program.imports) {
            SourceOutput::Scope range(out_, import.sourceRange, false);
            out_ << "import " << import.alias << " from " << quoteSource(import.path); out_.finishRange(); out_ << '\n'; range.close();
        }
        if (!program.imports.empty()) out_ << '\n';
        const auto& asset = program.asset;
        if (asset.sizeX.has_value() != asset.sizeY.has_value())
            throw SourceError{asset.location, "size requires both width and height"};
        if (static_cast<bool>(asset.originX) != static_cast<bool>(asset.originY))
            throw SourceError{asset.location, "origin requires both coordinates"};
        SourceOutput::Scope assetRange(out_, asset.sourceRange, false);
        indent(0);
        out_ << "asset " << asset.name << " {\n";
        if (asset.sizeX && asset.sizeY) {
            indent(1); SourceOutput::Scope range(out_, metadata(program, MetadataSyntax::Kind::Size), false);
            out_ << "size " << (std::to_string(*asset.sizeX) + "x" + std::to_string(*asset.sizeY));
            out_.finishRange(); out_ << '\n'; range.close();
        }
        if (asset.originX && asset.originY) {
            indent(1); SourceOutput::Scope range(out_, metadata(program, MetadataSyntax::Kind::Origin), false);
            out_ << "  origin "; expression(*asset.originX); out_ << ", "; expression(*asset.originY); out_.finishRange(); out_ << '\n'; range.close();
        }
        if (asset.variants) {
            indent(1); SourceOutput::Scope range(out_, metadata(program, MetadataSyntax::Kind::Variants), false);
            out_ << "variants " << *asset.variants; out_.finishRange(); out_ << '\n'; range.close();
        }
        if (asset.archetypes) {
            indent(1); SourceOutput::Scope range(out_, metadata(program, MetadataSyntax::Kind::Archetypes), false);
            out_ << "archetypes " << *asset.archetypes; out_.finishRange(); out_ << '\n'; range.close();
        }
        for (const auto& input : asset.inputs) {
            indent(1); SourceOutput::Scope range(out_, input.sourceRange, false);
            out_ << "  input " << (input.type == InputType::U32 ? "u32" : typeName(valueTypeOf(input.type)))
                 << ' ' << input.name << " = "; expression(*input.defaultValue); out_.finishRange(); out_ << '\n'; range.close();
        }
        for (const auto& output : asset.outputs) {
            indent(1); SourceOutput::Scope range(out_, output.sourceRange, false);
            const char* kinds[] = {"point", "rect", "circle"};
            if (static_cast<unsigned>(output.kind) >= 3)
                throw SourceError{output.location, "invalid output kind in source printer"};
            out_ << "  output " << (output.isList ? "list " : "")
                 << kinds[static_cast<unsigned>(output.kind)] << ' ' << output.name; out_.finishRange(); out_ << '\n'; range.close();
        }
        for (const auto& palette : asset.palettes) {
            indent(1); SourceOutput::Scope range(out_, palette.sourceRange, false);
            out_ << "  palette " << palette.name << " = [";
            for (std::size_t i = 0; i < palette.elements.size(); ++i) {
                if (i) out_ << ", ";
                expression(*palette.elements[i]);
            }
            out_ << ']'; out_.finishRange(); out_ << '\n'; range.close();
        }
        for (const auto& function : asset.functions) {
            out_ << '\n';
            indent(1); SourceOutput::Scope range(out_, function.sourceRange, false);
            if (function.name == "draw" && (!function.params.empty() || function.returnType))
                throw SourceError{function.location, "draw has no declared parameters or return type"};
            out_ << function.name;
            if (function.name != "draw") {
                out_ << '(';
                for (std::size_t i = 0; i < function.params.size(); ++i) {
                    if (i) out_ << ", ";
                    out_ << function.params[i].name << ": " << typeName(valueTypeOf(function.params[i].type));
                }
                out_ << ')';
                if (function.returnType) out_ << " -> " << typeName(valueTypeOf(*function.returnType));
            }
            block(function.body, 1); out_.finishRange(); out_ << '\n'; range.close();
        }
        indent(1); out_.prepare(TokenKind::RBrace);
        indent(0); out_ << "}\n";
        assetRange.close();
        return out_.str();
    }
};
}

std::string formatProgram(const Program& program,
    const std::map<std::string, const Program*>& parts) { return SourcePrinter{parts}.print(program); }

} // namespace iprop::ips
