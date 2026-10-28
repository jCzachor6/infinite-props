#include "ips/Materials.hpp"

#include "ips/AssetConstants.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"
#include "ips/Types.hpp"
#include "ips/Typing.hpp"

#include <map>
#include <regex>
#include <set>

namespace iprop::ips {
namespace {

std::unique_ptr<Expr> mk(Expr value, const Expr& from) {
    value.sourceRange = from.sourceRange;
    value.sourceCoreRange = from.sourceCoreRange;
    return std::make_unique<Expr>(std::move(value));
}

} // namespace

std::unique_ptr<Expr> cloneExpr(const Expr& expr) {
    return std::visit(
        [&](const auto& node) -> std::unique_ptr<Expr> {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Literal>) {
                return mk(Expr{node}, expr);
            } else if constexpr (std::is_same_v<T, Identifier>) {
                return mk(Expr{node}, expr);
            } else if constexpr (std::is_same_v<T, Member>) {
                return mk(Expr{Member{cloneExpr(*node.object), node.name, node.location}}, expr);
            } else if constexpr (std::is_same_v<T, Call>) {
                std::vector<Arg> args;
                for (const auto& arg : node.args)
                    args.push_back(Arg{arg.name, cloneExpr(*arg.value), arg.location});
                return mk(Expr{Call{cloneExpr(*node.callee), std::move(args), node.location, node.builtinOnly}}, expr);
            } else if constexpr (std::is_same_v<T, VectorLit>) {
                std::vector<std::unique_ptr<Expr>> items;
                for (const auto& item : node.elements) items.push_back(cloneExpr(*item));
                return mk(Expr{VectorLit{std::move(items), node.location}}, expr);
            } else if constexpr (std::is_same_v<T, ListLit>) {
                std::vector<std::unique_ptr<Expr>> items;
                for (const auto& item : node.elements) items.push_back(cloneExpr(*item));
                return mk(Expr{ListLit{std::move(items), node.location}}, expr);
            } else if constexpr (std::is_same_v<T, Index>) {
                return mk(Expr{Index{cloneExpr(*node.object), cloneExpr(*node.index), node.location}}, expr);
            } else if constexpr (std::is_same_v<T, Binary>) {
                return mk(Expr{Binary{node.op, cloneExpr(*node.left), cloneExpr(*node.right), node.location}}, expr);
            } else if constexpr (std::is_same_v<T, Unary>) {
                return mk(Expr{Unary{node.op, cloneExpr(*node.operand), node.location}}, expr);
            } else if constexpr (std::is_same_v<T, RandomRange>) {
                return mk(Expr{RandomRange{cloneExpr(*node.lower), cloneExpr(*node.upper), node.location,
                                           node.distribution ? cloneExpr(*node.distribution) : nullptr,
                                           node.withLocation}},
                          expr);
            } else if constexpr (std::is_same_v<T, SamplerCapture>) {
                return mk(Expr{SamplerCapture{cloneExpr(*node.value), node.location, node.stepper}}, expr);
            } else if constexpr (std::is_same_v<T, SamplerPick>) {
                return mk(Expr{SamplerPick{cloneExpr(*node.receiver),
                                           node.count ? cloneExpr(*node.count) : nullptr, node.location}},
                          expr);
            } else if constexpr (std::is_same_v<T, ListMethod>) {
                return mk(Expr{ListMethod{cloneExpr(*node.receiver), node.name, node.location}}, expr);
            } else if constexpr (std::is_same_v<T, SurfaceBlur>) {
                return mk(Expr{SurfaceBlur{cloneExpr(*node.receiver),
                                           Arg{node.sigma.name, cloneExpr(*node.sigma.value), node.sigma.location},
                                           node.location}},
                          expr);
            } else {
                std::vector<PointDistributionLiteral::Point> points;
                for (const auto& point : node.points)
                    points.push_back(PointDistributionLiteral::Point{
                        cloneExpr(*point.key), cloneExpr(*point.value), point.location});
                return mk(Expr{PointDistributionLiteral{node.family, std::move(points), node.location}}, expr);
            }
        },
        expr);
}

namespace {

// Calls `visit` on each direct child expression of `expr`.
template <class Visit>
void forEachChild(Expr& expr, Visit&& visit) {
    std::visit(
        [&](auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Member>) {
                visit(node.object);
            } else if constexpr (std::is_same_v<T, Call>) {
                visit(node.callee);
                for (auto& arg : node.args) visit(arg.value);
            } else if constexpr (std::is_same_v<T, Binary>) {
                visit(node.left); visit(node.right);
            } else if constexpr (std::is_same_v<T, Unary>) {
                visit(node.operand);
            } else if constexpr (std::is_same_v<T, VectorLit> || std::is_same_v<T, ListLit>) {
                for (auto& item : node.elements) visit(item);
            } else if constexpr (std::is_same_v<T, Index>) {
                visit(node.object); visit(node.index);
            } else if constexpr (std::is_same_v<T, RandomRange>) {
                visit(node.lower); visit(node.upper);
                if (node.distribution) visit(node.distribution);
            } else if constexpr (std::is_same_v<T, SamplerCapture>) {
                visit(node.value);
            } else if constexpr (std::is_same_v<T, SamplerPick>) {
                visit(node.receiver); if (node.count) visit(node.count);
            } else if constexpr (std::is_same_v<T, ListMethod>) {
                visit(node.receiver);
            } else if constexpr (std::is_same_v<T, SurfaceBlur>) {
                visit(node.receiver); visit(node.sigma.value);
            } else if constexpr (std::is_same_v<T, PointDistributionLiteral>) {
                for (auto& point : node.points) { visit(point.key); visit(point.value); }
            }
        },
        expr);
}

// '$' cannot occur in a source identifier. Keep internal fields and private
// declarations disjoint from source names, even names containing '__'.
std::string fieldName(const std::string& owner, const std::string& field) {
    return "$material$local$" + owner + "$" + field;
}

std::string privateName(const std::string& alias, const std::string& name) {
    return "$material$private$" + alias + "$" + name;
}

// A material file has no helpers. Preserve its checked builtin resolution
// when its expressions are copied into an asset with its own helpers.
void bindMaterialBuiltins(Expr& expr) {
    if (auto* call = std::get_if<Call>(&expr)) call->builtinOnly = true;
    forEachChild(expr, [&](std::unique_ptr<Expr>& child) { bindMaterialBuiltins(*child); });
}

// Renames value references, leaving the independently bound callees alone.
void renameReferences(std::unique_ptr<Expr>& expr, const std::map<std::string, std::string>& names) {
    if (auto* id = std::get_if<Identifier>(expr.get())) {
        if (const auto found = names.find(id->name); found != names.end()) id->name = found->second;
        return;
    }
    if (auto* call = std::get_if<Call>(expr.get())) {
        for (auto& arg : call->args) renameReferences(arg.value, names);
        return;
    }
    forEachChild(*expr, [&](std::unique_ptr<Expr>& child) { renameReferences(child, names); });
}

} // namespace

std::vector<SourceError> checkMaterialFile(Program& file, std::size_t limit) {
    std::vector<SourceError> errors;
    const auto report = [&](SourceError error) {
        if (errors.size() < limit) errors.push_back(std::move(error));
    };
    std::set<std::string> declarations;
    for (const auto& palette : file.materialPalettes) declarations.insert(palette.name);
    for (const auto& constant : file.materialConstants) declarations.insert(constant.name);
    std::set<std::string> names;
    for (const auto& def : file.materials) {
        if (!names.insert(def.name).second)
            report(SourceError{def.location, "duplicate material '" + def.name + "'"});
        std::set<std::string> seen;
        bool hasBase = false;
        for (const auto& field : def.fields) {
            if (!seen.insert(field.name).second)
                report(SourceError{field.location, "duplicate material field '" + field.name + "' in '" + def.name + "'"});
            // A field may read an earlier field by name, so a field cannot
            // share its name with a palette or const of the file.
            if (declarations.contains(field.name))
                report(SourceError{field.location, "material field '" + field.name +
                                                       "' has the same name as a palette or const of this file"});
            hasBase = hasBase || field.name == "base";
        }
        if (!hasBase) report(SourceError{def.location, "material '" + def.name + "' must define 'base'"});
    }

    // The palettes and consts are checked as the declarations of an empty
    // asset, then each field is typed in that asset's draw scope, with the
    // earlier fields of its material in scope as locals.
    Program probe;
    probe.asset.name = "materials";
    probe.asset.location = file.materials.front().location;
    probe.asset.sizeX = probe.asset.sizeY = 1;
    for (const auto& palette : file.materialPalettes) {
        PaletteDecl copy{palette.name, {}, palette.location, palette.sourceRange};
        for (const auto& element : palette.elements) copy.elements.push_back(cloneExpr(*element));
        probe.asset.palettes.push_back(std::move(copy));
    }
    for (const auto& constant : file.materialConstants)
        probe.asset.constants.push_back(Asset::Constant{
            constant.type, constant.name, cloneExpr(*constant.value), constant.location, constant.sourceRange});
    probe.asset.functions.push_back(Function{"draw", {}, std::nullopt, {}, probe.asset.location});
    const std::map<std::string, PartInfo> noParts;
    for (auto& error : checkAssetDiagnostics(probe, noParts, limit)) report(std::move(error));
    if (!errors.empty()) return errors;

    Typer typer(noParts);
    typer.resolveFunction = [](const std::string&, const SourceLocation&, const std::string&) {
        return std::optional<FunctionSurface>{};
    };
    typer.registerAssetDeclarations(probe, resolveAssetDeclarations(probe));
    for (auto& def : file.materials) {
        typer.enterFunction(probe, probe.asset.functions.front(), true);
        for (auto& field : def.fields) {
            if (errors.size() >= limit) return errors;
            const auto expected = field.name == "base" ? std::optional(ValueType::Color) : std::nullopt;
            try {
                const auto type = typer.typeOf(*field.value, expected);
                if (expected && type != *expected)
                    throw SourceError{field.location, "material field 'base' must be a color, got " +
                                                          std::string(typeName(type)),
                                      {}, DiagnosticCode::TypeError, DiagnosticStage::Typing};
                if (type != ValueType::Color && type != ValueType::I32 && type != ValueType::F32 &&
                    type != ValueType::Vec2)
                    throw SourceError{field.location, "material field '" + field.name +
                                                          "' must be a color, i32, f32 or vec2, got " +
                                                          typeName(type),
                                      {}, DiagnosticCode::TypeError, DiagnosticStage::Typing};
                field.type = type;
                typer.declareLocal(field.name, type);
            } catch (const SourceError& error) {
                report(error);
                typer.declareInvalidLocal(field.name);
            } catch (const UnresolvedType&) {
                typer.declareInvalidLocal(field.name);
            }
        }
    }
    return errors;
}

void mergeMaterialFile(Program& importer, const Program& source, const Import& import) {
    std::map<std::string, std::string> names;
    for (const auto& palette : source.materialPalettes) names[palette.name] = privateName(import.alias, palette.name);
    for (const auto& constant : source.materialConstants) names[constant.name] = privateName(import.alias, constant.name);
    const auto clone = [&](const Expr& expr) {
        auto copy = cloneExpr(expr);
        renameReferences(copy, names);
        bindMaterialBuiltins(*copy);
        return copy;
    };
    for (const auto& def : source.materials) {
        MaterialDef copy;
        copy.name = import.alias + "." + def.name;
        for (const auto& existing : importer.asset.materials)
            if (existing.name == copy.name)
                throw SourceError{import.location, "duplicate import alias '" + import.alias + "'"};
        copy.location = def.location;
        copy.sourceRange = def.sourceRange;
        for (const auto& field : def.fields)
            copy.fields.push_back(MaterialField{field.name, clone(*field.value), field.location, field.type});
        importer.asset.materials.push_back(std::move(copy));
    }
    for (const auto& palette : source.materialPalettes) {
        PaletteDecl copy;
        copy.name = names.at(palette.name);
        copy.location = palette.location;
        copy.sourceRange = palette.sourceRange;
        for (const auto& element : palette.elements) copy.elements.push_back(clone(*element));
        importer.asset.palettes.push_back(std::move(copy));
    }
    for (const auto& constant : source.materialConstants) {
        importer.asset.constants.push_back(Asset::Constant{
            constant.type, names.at(constant.name), clone(*constant.value), constant.location, constant.sourceRange});
    }
}

std::string demangleMaterialNames(const std::string& text) {
    static const std::regex mangled{R"(\$material\$(?:private|local)\$([A-Za-z_][A-Za-z0-9_]*)\$([A-Za-z_][A-Za-z0-9_]*)(?:\$[0-9]+)?)"};
    return std::regex_replace(text, mangled, "$1.$2");
}

namespace {

struct FieldInfo {
    std::string name;
    ValueType type = ValueType::Color;
    const MaterialField* field = nullptr;
};

struct DefInfo {
    const MaterialDef* def = nullptr;
    std::vector<FieldInfo> fields;
    const FieldInfo* find(std::string_view name) const {
        for (const auto& field : fields)
            if (field.name == name) return &field;
        return nullptr;
    }
};

// Fields in definition order, so creation rolls each range in the order it
// was written and a field can read the fields before it.
DefInfo buildDefInfo(const MaterialDef& def) {
    DefInfo info;
    info.def = &def;
    for (const auto& field : def.fields) info.fields.push_back(FieldInfo{field.name, *field.type, &field});
    return info;
}

// `forest.Bark` for a callee written as a material type, otherwise empty.
std::string qualifiedName(const Expr& callee) {
    const auto* member = std::get_if<Member>(&callee);
    if (!member) return {};
    const auto* alias = std::get_if<Identifier>(member->object.get());
    return alias ? alias->name + "." + member->name : std::string{};
}

struct OrigParam {
    std::string name;
    bool isMaterial = false;
    const DefInfo* def = nullptr;
};

struct Expander {
    const Program& owner;
    std::map<std::string, DefInfo> defs;
    std::map<std::string, std::vector<OrigParam>> funcs;
    // nullptr records an ordinary local. Keep every source binding for the
    // whole function, just as scalar checking does across branches and loops.
    std::map<std::string, const DefInfo*> vars;
    std::set<std::string> parameters;
    std::size_t nextTemporary = 0;

    explicit Expander(const Program& program) : owner(program) {}

    void checkWritable(const std::string& name, SourceLocation at) const {
        if (parameters.contains(name))
            throw SourceError{at, "cannot assign to parameter '" + name + "'"};
        if (name == "seed") throw SourceError{at, "'seed' is a reserved name"};
        for (const auto& input : owner.asset.inputs)
            if (input.name == name) throw SourceError{at, "cannot assign to input '" + name + "'"};
        for (const auto& palette : owner.asset.palettes)
            if (palette.name == name) throw SourceError{at, "cannot assign to palette '" + name + "'"};
        if (findAssetConstant(owner, name))
            throw SourceError{at, "cannot assign to const '" + name + "'"};
    }

    void checkMaterialBinding(const VarDecl& decl, const DefInfo& def) const {
        checkWritable(decl.name, decl.location);
        for (const auto& output : owner.asset.outputs)
            if (output.name == decl.name)
                throw SourceError{decl.location, "cannot assign a material to output '" + decl.name + "'"};
        if (decl.declaredType)
            throw SourceError{decl.location, std::string(typeName(*decl.declaredType)) + " '" + decl.name +
                                                  "' cannot hold material '" + def.def->name + "'"};
        if (const auto found = vars.find(decl.name); found != vars.end() && found->second != &def) {
            const std::string previous = found->second ? found->second->def->name : "a non-material value";
            throw SourceError{decl.location, "local '" + decl.name + "' is " + previous +
                                                  ", cannot assign material '" + def.def->name + "'"};
        }
    }

    void bindPlain(const std::string& name, SourceLocation at) {
        checkWritable(name, at);
        if (const auto found = vars.find(name); found != vars.end() && found->second)
            throw SourceError{at, "local '" + name + "' is " + found->second->def->name +
                                      ", cannot assign a non-material value"};
        vars.emplace(name, nullptr);
    }

    const DefInfo& requireDef(const std::string& name, SourceLocation at) const {
        const auto found = defs.find(name);
        if (found == defs.end())
            throw SourceError{at, "unknown material '" + name + "' (no imported definition with that name)"};
        return found->second;
    }

    void transformExpr(std::unique_ptr<Expr>& expr) { if (expr) transformExpr(*expr); }

    void transformExpr(Expr& expr) {
        if (const auto* id = std::get_if<Identifier>(&expr)) {
            if (const auto found = vars.find(id->name); found != vars.end() && found->second)
                throw SourceError{id->location, "material '" + id->name +
                                                    "' must be copied, passed to a material parameter or read by field"};
            return;
        }
        if (auto* member = std::get_if<Member>(&expr)) {
            if (const auto* id = std::get_if<Identifier>(member->object.get())) {
                if (const auto found = vars.find(id->name); found != vars.end() && found->second) {
                    const DefInfo* def = found->second;
                    if (!def->find(member->name))
                        throw SourceError{member->location, "material '" + def->def->name +
                                                                 "' has no field '" + member->name + "'"};
                    const std::string flat = fieldName(id->name, member->name);
                    const auto at = member->location;
                    expr = Identifier{flat, at};
                    return;
                }
            }
            transformExpr(member->object);
            return;
        }
        if (auto* call = std::get_if<Call>(&expr)) {
            if (const auto name = qualifiedName(*call->callee); defs.contains(name))
                throw SourceError{call->location, "material '" + name +
                                                      "' can only be created by assignment, such as m = " + name + "()"};
            if (const auto* callee = std::get_if<Identifier>(call->callee.get())) {
                const bool builtin = callee->name == "ramp" || builtinSpecs().contains(callee->name);
                if (const auto fn = funcs.find(callee->name); fn != funcs.end() && !call->builtinOnly &&
                    (!builtin || builtinYieldsToHelper(callee->name))) {
                    expandCall(*call, fn->second);
                    return;
                }
                if (!builtin)
                    for (const auto& [name, def] : defs)
                        if (name.ends_with("." + callee->name))
                            throw SourceError{callee->location, "material '" + callee->name +
                                                                     "' needs its import alias: " + name + "()"};
                // A callee has its own namespace; it is not a value read of
                // a same-named material local.
                for (auto& arg : call->args) transformExpr(arg.value);
                return;
            }
            transformExpr(call->callee);
            for (auto& arg : call->args) transformExpr(arg.value);
            return;
        }
        forEachChild(expr, [&](std::unique_ptr<Expr>& child) { transformExpr(child); });
    }

    // Splices each material argument into one argument per field. Arguments
    // keep their source order, and an argument that matches no parameter
    // stays as written, so semantic checking still reports extra, unknown
    // and duplicate arguments.
    void expandCall(Call& call, const std::vector<OrigParam>& params) {
        std::vector<Arg> out;
        std::size_t positional = 0;
        for (auto& arg : call.args) {
            const OrigParam* param = nullptr;
            if (arg.name) {
                for (const auto& candidate : params)
                    if (candidate.name == *arg.name) param = &candidate;
            } else if (positional < params.size()) {
                param = &params[positional];
            }
            if (!arg.name) ++positional;
            if (!param || !param->isMaterial) {
                transformExpr(arg.value);
                out.push_back(std::move(arg));
                continue;
            }
            const auto* id = std::get_if<Identifier>(arg.value.get());
            const auto var = id ? vars.find(id->name) : vars.end();
            if (!id || var == vars.end() || var->second != param->def)
                throw SourceError{arg.location, "material argument for '" + param->name +
                                                     "' must be a " + param->def->def->name + " variable"};
            for (const auto& field : param->def->fields) {
                auto value = std::make_unique<Expr>(Identifier{fieldName(id->name, field.name), id->location});
                out.push_back(Arg{arg.name ? std::optional(fieldName(param->name, field.name)) : std::nullopt,
                                  std::move(value), arg.location});
            }
        }
        call.args = std::move(out);
    }

    // Evaluate into fresh temporaries in definition order. Defaults read the
    // new instance's earlier fields; overrides still see the old source
    // bindings. Commit only when the entire RHS has been evaluated.
    std::vector<Stmt> create(const std::string& var, const DefInfo& def, Call& call, SourceLocation at) {
        std::map<std::string, Arg*> overrides;
        for (auto& arg : call.args) {
            if (!arg.name)
                throw SourceError{arg.location, "material fields are set by name, such as " + def.def->name +
                                                     "(" + def.fields.front().name + " = ...)"};
            if (!def.find(*arg.name))
                throw SourceError{arg.location, "material '" + def.def->name + "' has no field '" + *arg.name + "'"};
            if (!overrides.emplace(*arg.name, &arg).second)
                throw SourceError{arg.location, "field '" + *arg.name + "' is set twice"};
        }
        std::vector<Stmt> out;
        std::map<std::string, std::string> earlier;
        const auto temporary = "$" + std::to_string(nextTemporary++);
        for (const auto& field : def.fields) {
            VarDecl decl{fieldName(var, field.name) + temporary, nullptr, at, field.type};
            if (const auto found = overrides.find(field.name); found != overrides.end()) {
                decl.value = std::move(found->second->value);
                transformExpr(decl.value);
            } else {
                decl.value = cloneExpr(*field.field->value);
                renameReferences(decl.value, earlier);
            }
            earlier[field.name] = decl.name;
            out.push_back(Stmt{std::move(decl)});
        }
        for (const auto& field : def.fields)
            out.push_back(Stmt{VarDecl{fieldName(var, field.name),
                std::make_unique<Expr>(Identifier{earlier.at(field.name), at}), at, field.type}});
        vars[var] = &def;
        return out;
    }

    std::vector<Stmt> transformStmts(std::vector<Stmt>& body) {
        std::vector<Stmt> out;
        for (auto& stmt : body) {
            auto expanded = transformStmt(stmt);
            for (auto& item : expanded) out.push_back(std::move(item));
        }
        return out;
    }

    std::vector<Stmt> transformStmt(Stmt& stmt) {
        std::vector<Stmt> out;
        if (auto* decl = std::get_if<VarDecl>(&stmt)) {
            if (decl->value) {
                if (auto* call = std::get_if<Call>(decl->value.get())) {
                    if (const auto found = defs.find(qualifiedName(*call->callee)); found != defs.end()) {
                        checkMaterialBinding(*decl, found->second);
                        return create(decl->name, found->second, *call, decl->location);
                    }
                }
                if (const auto* id = std::get_if<Identifier>(decl->value.get())) {
                    if (const auto src = vars.find(id->name); src != vars.end() && src->second) {
                        checkMaterialBinding(*decl, *src->second);
                        for (const auto& field : src->second->fields)
                            out.push_back(Stmt{VarDecl{fieldName(decl->name, field.name),
                                                       std::make_unique<Expr>(Identifier{
                                                           fieldName(id->name, field.name), id->location}),
                                                       decl->location}});
                        vars[decl->name] = src->second;
                        return out;
                    }
                }
                transformExpr(decl->value);
            }
            bindPlain(decl->name, decl->location);
            out.push_back(std::move(stmt));
            return out;
        }
        if (auto* exprStmt = std::get_if<ExprStmt>(&stmt)) {
            transformExpr(exprStmt->expr);
        } else if (auto* ifStmt = std::get_if<IfStmt>(&stmt)) {
            transformExpr(ifStmt->condition);
            ifStmt->thenBranch = transformStmts(ifStmt->thenBranch);
            ifStmt->elseBranch = transformStmts(ifStmt->elseBranch);
        } else if (auto* whileStmt = std::get_if<WhileStmt>(&stmt)) {
            transformExpr(whileStmt->condition);
            whileStmt->body = transformStmts(whileStmt->body);
        } else if (auto* forStmt = std::get_if<ForStmt>(&stmt)) {
            transformExpr(forStmt->from);
            transformExpr(forStmt->to);
            bindPlain(forStmt->variable, forStmt->location);
            forStmt->body = transformStmts(forStmt->body);
        } else if (auto* ret = std::get_if<ReturnStmt>(&stmt)) {
            if (ret->value) {
                if (const auto* id = std::get_if<Identifier>(ret->value.get());
                    id && vars.contains(id->name) && vars.at(id->name))
                    throw SourceError{id->location, "returning a material is not supported; return a field"};
                transformExpr(ret->value);
            }
        } else if (auto* layer = std::get_if<LayerStmt>(&stmt)) {
            layer->body = transformStmts(layer->body);
            bindPlain(layer->name, layer->location);
        } else if (auto* clip = std::get_if<ClipStmt>(&stmt)) {
            transformExpr(clip->mask);
            clip->body = transformStmts(clip->body);
        }
        out.push_back(std::move(stmt));
        return out;
    }
};

} // namespace

void expandMaterials(Program& program) {
    Expander expander(program);
    for (const auto& def : program.asset.materials) {
        if (expander.defs.contains(def.name))
            throw SourceError{def.location, "duplicate material '" + def.name + "'"};
        expander.defs.emplace(def.name, buildDefInfo(def));
    }
    bool anyMaterial = !expander.defs.empty();
    for (const auto& fn : program.asset.functions)
        for (const auto& param : fn.params)
            if (param.materialType) anyMaterial = true;
    if (!anyMaterial) return;
    for (const auto& fn : program.asset.functions) {
        std::vector<OrigParam> params;
        std::set<std::string> names;
        for (const auto& param : fn.params) {
            if (!names.insert(param.name).second)
                throw SourceError{param.location, "duplicate parameter '" + param.name + "'"};
            if (param.materialType && findAssetConstant(program, param.name))
                throw SourceError{param.location, "parameter '" + param.name + "' cannot shadow an asset const"};
            if (param.materialType) {
                params.push_back(OrigParam{param.name, true, &expander.requireDef(*param.materialType, param.location)});
            } else {
                params.push_back(OrigParam{param.name, false, nullptr});
            }
        }
        expander.funcs.emplace(fn.name, std::move(params));
    }
    for (auto& fn : program.asset.functions) {
        expander.vars.clear();
        expander.parameters.clear();
        expander.nextTemporary = 0;
        std::vector<Param> params;
        for (const auto& param : fn.params) {
            expander.parameters.insert(param.name);
            if (!param.materialType) {
                expander.vars.emplace(param.name, nullptr);
                params.push_back(param);
                continue;
            }
            const DefInfo* def = &expander.requireDef(*param.materialType, param.location);
            expander.vars[param.name] = def;
            for (const auto& field : def->fields)
                params.push_back(Param{fieldName(param.name, field.name), field.type, param.location});
        }
        fn.params = std::move(params);
        fn.body = expander.transformStmts(fn.body);
    }
    program.asset.materials.clear();
}

} // namespace iprop::ips
