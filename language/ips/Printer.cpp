#include "ips/Printer.hpp"

#include <string>
#include <vector>

namespace iprop::ips {
namespace {

// Expressions print on a single line.
std::string printExpr(const Expr& expr);

// Statements may span multiple lines. Returned lines carry no leading
// indentation; the caller indents them.
std::vector<std::string> printStmtLines(const Stmt& stmt);

// Appends n closing parens to the last line.
void closeLast(std::vector<std::string>& lines, const int n = 1) {
    lines.back().append(static_cast<std::size_t>(n), ')');
}

void indentBy(std::vector<std::string>& lines, const int n) {
    const std::string pad(static_cast<std::size_t>(n), ' ');
    for (auto& line : lines) {
        line = pad + line;
    }
}

const char* binaryOp(Binary::Op op) {
    switch (op) {
    case Binary::Op::Add: return "+";
    case Binary::Op::Sub: return "-";
    case Binary::Op::Mul: return "*";
    case Binary::Op::Div: return "/";
    case Binary::Op::Mod: return "%";
    case Binary::Op::Eq: return "==";
    case Binary::Op::Ne: return "!=";
    case Binary::Op::Lt: return "<";
    case Binary::Op::Le: return "<=";
    case Binary::Op::Gt: return ">";
    case Binary::Op::Ge: return ">=";
    case Binary::Op::And: return "&&";
    case Binary::Op::Or: return "||";
    case Binary::Op::Xor: return "^";
    case Binary::Op::BitAnd: return "&";
    case Binary::Op::BitOr: return "|";
    case Binary::Op::Shl: return "<<";
    case Binary::Op::Shr: return ">>";
    }
    return "?";
}

const char* inputTypeName(InputType type) {
    switch (type) {
    case InputType::Color: return "color";
    case InputType::F32: return "f32";
    case InputType::U32: return "u32";
    case InputType::I32: return "i32";
    }
    return "?";
}

const char* outputKindName(OutputKind outputKind) {
    switch (outputKind) {
    case OutputKind::Point: return "point";
    case OutputKind::Rect: return "rect";
    case OutputKind::Circle: return "circle";
    }
    return "?";
}

const char* paramTypeName(ParamType type) {
    switch (type) {
    case ParamType::I32: return "i32";
    case ParamType::F32: return "f32";
    case ParamType::Color: return "color";
    }
    return "?";
}

// Flattens `a.b.c` chains into (member a b c). `expr` must hold a Member.
std::string printMemberChain(const Expr& expr) {
    std::vector<std::string> names;
    const Expr* current = &expr;
    while (const auto* asMember = std::get_if<Member>(current)) {
        names.push_back(asMember->name);
        current = asMember->object.get();
    }
    std::string result = "(member";
    if (const auto* base = std::get_if<Identifier>(current)) {
        result += " " + base->name;
    } else {
        result += " " + printExpr(*current);
    }
    // The walk collects names innermost-first; print them outermost-first.
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        result += " " + *it;
    }
    return result + ")";
}

std::string printCall(const Call& call) {
    std::string result = "(call " + printExpr(*call.callee);
    for (const auto& arg : call.args) {
        result += " ";
        if (arg.name.has_value()) {
            result += "(" + *arg.name + " " + printExpr(*arg.value) + ")";
        } else {
            result += printExpr(*arg.value);
        }
    }
    return result + ")";
}

std::string printExpr(const Expr& expr) {
    if (const auto* lit = std::get_if<Literal>(&expr)) {
        switch (lit->kind) {
        case Literal::Kind::Int:
        case Literal::Kind::Float:
        case Literal::Kind::Color:
            return lit->text; // bare atoms: 42, 0.10, #808d74
        case Literal::Kind::String:
            return "\"" + lit->text + "\"";
        case Literal::Kind::True:
            return "true";
        case Literal::Kind::False:
            return "false";
        }
    }
    if (const auto* id = std::get_if<Identifier>(&expr)) {
        return id->name;
    }
    if (std::holds_alternative<Member>(expr)) {
        return printMemberChain(expr);
    }
    if (const auto* call = std::get_if<Call>(&expr)) {
        return printCall(*call);
    }
    if (const auto* vector = std::get_if<VectorLit>(&expr)) {
        std::string result = "(vector";
        for (const auto& element : vector->elements) {
            result += " " + printExpr(*element);
        }
        return result + ")";
    }
    if (const auto* bin = std::get_if<Binary>(&expr)) {
        return std::string("(") + binaryOp(bin->op) + " " + printExpr(*bin->left) +
               " " + printExpr(*bin->right) + ")";
    }
    if (const auto* un = std::get_if<Unary>(&expr)) {
        const char* op = un->op == Unary::Op::Neg
                             ? "neg"
                             : (un->op == Unary::Op::Not ? "not" : "bitnot");
        return std::string("(") + op + " " + printExpr(*un->operand) + ")";
    }
    return "(?)";
}

std::vector<std::string> printStmtLines(const Stmt& stmt) {
    std::vector<std::string> lines;

    if (const auto* decl = std::get_if<VarDecl>(&stmt)) {
        lines.push_back("(let " + decl->name + " " + printExpr(*decl->value) + ")");
        return lines;
    }
    if (const auto* exprStmt = std::get_if<ExprStmt>(&stmt)) {
        lines.push_back("(expr " + printExpr(*exprStmt->expr) + ")");
        return lines;
    }
    if (const auto* ifStmt = std::get_if<IfStmt>(&stmt)) {
        lines.push_back("(if " + printExpr(*ifStmt->condition));
        for (const auto& s : ifStmt->thenBranch) {
            auto sub = printStmtLines(s);
            indentBy(sub, 2);
            lines.insert(lines.end(), sub.begin(), sub.end());
        }
        if (!ifStmt->elseBranch.empty()) {
            // "(else" is a child of the if-form, so the whole else subtree
            // sits one level deeper.
            std::vector<std::string> elseLines;
            elseLines.push_back("(else");
            for (const auto& s : ifStmt->elseBranch) {
                auto sub = printStmtLines(s);
                indentBy(sub, 2);
                elseLines.insert(elseLines.end(), sub.begin(), sub.end());
            }
            indentBy(elseLines, 2);
            lines.insert(lines.end(), elseLines.begin(), elseLines.end());
            // An if-with-else opened two parens: "(if" and "(else".
            closeLast(lines, 2);
        } else {
            closeLast(lines);
        }
        return lines;
    }
    if (const auto* whileStmt = std::get_if<WhileStmt>(&stmt)) {
        lines.push_back("(while " + printExpr(*whileStmt->condition));
        for (const auto& s : whileStmt->body) {
            auto sub = printStmtLines(s);
            indentBy(sub, 2);
            lines.insert(lines.end(), sub.begin(), sub.end());
        }
        closeLast(lines);
        return lines;
    }
    if (const auto* ret = std::get_if<ReturnStmt>(&stmt)) {
        if (ret->value) {
            lines.push_back("(return " + printExpr(*ret->value) + ")");
        } else {
            lines.push_back("(return)");
        }
        return lines;
    }
    lines.push_back("(?)");
    return lines;
}

std::vector<std::string> printFunctionLines(const Function& function) {
    std::vector<std::string> lines;
    // "(fn draw ((v i32) (a i32))" — each param prints as a (name type)
    // pair; the single ')' closes the params list, the fn form itself stays
    // open and is closed by closeLast after the body.
    std::string header = "(fn " + function.name + " (";
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        if (i != 0) {
            header += " ";
        }
        header += "(" + function.params[i].name + " " +
                  paramTypeName(function.params[i].type) + ")";
    }
    header += ")";
    if (function.returnType.has_value()) {
        header += " -> ";
        header += paramTypeName(*function.returnType);
    }
    lines.push_back(std::move(header));
    for (const auto& stmt : function.body) {
        auto sub = printStmtLines(stmt);
        indentBy(sub, 2);
        lines.insert(lines.end(), sub.begin(), sub.end());
    }
    closeLast(lines);
    return lines;
}

std::vector<std::string> printAssetLines(const Asset& asset) {
    std::vector<std::string> lines;
    lines.push_back("(asset " + asset.name);

    // Everything inside the asset is one level deeper.
    const auto child = [&](std::string line) { lines.push_back("  " + line); };

    if (asset.sizeX.has_value()) {
        child("(size " + std::to_string(*asset.sizeX) + " " +
              std::to_string(*asset.sizeY) + ")");
    }
    if (asset.originX) {
        child("(origin " + printExpr(*asset.originX) + " " +
              printExpr(*asset.originY) + ")");
    }
    if (asset.variants.has_value()) {
        child("(variants " + std::to_string(*asset.variants) + ")");
    }
    if (asset.archetypes.has_value()) {
        child("(archetypes " + std::to_string(*asset.archetypes) + ")");
    }
    for (const auto& input : asset.inputs) {
        child("(input " + std::string(inputTypeName(input.type)) + " " +
              input.name + " " + printExpr(*input.defaultValue) + ")");
    }
    for (const auto& output : asset.outputs) {
        std::string line = "(output ";
        if (output.isList) {
            line += "list ";
        }
        line += outputKindName(output.kind);
        line += " " + output.name + ")";
        child(std::move(line));
    }
    for (const auto& function : asset.functions) {
        auto sub = printFunctionLines(function);
        indentBy(sub, 2);
        lines.insert(lines.end(), sub.begin(), sub.end());
    }
    closeLast(lines);
    return lines;
}

} // namespace

std::string printProgram(const Program& program) {
    std::vector<std::string> lines;
    lines.push_back("(program");
    for (const auto& import : program.imports) {
        lines.push_back("  (import " + import.alias + " \"" + import.path + "\")");
    }
    auto assetLines = printAssetLines(program.asset);
    indentBy(assetLines, 2);
    lines.insert(lines.end(), assetLines.begin(), assetLines.end());
    closeLast(lines);

    std::string result;
    for (const auto& line : lines) {
        result += line;
        result += "\n";
    }
    return result;
}

} // namespace iprop::ips
