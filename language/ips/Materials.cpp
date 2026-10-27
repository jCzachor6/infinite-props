#include "ips/Materials.hpp"

#include "ips/SourceError.hpp"

#include <map>
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
                return mk(Expr{Call{cloneExpr(*node.callee), std::move(args), node.location}}, expr);
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

void mergeMaterialFile(Program& importer, const Program& source) {
    for (const auto& def : source.materials) {
        MaterialDef copy;
        copy.name = def.name;
        copy.location = def.location;
        copy.sourceRange = def.sourceRange;
        for (const auto& field : def.fields)
            copy.fields.push_back(MaterialField{field.name, cloneExpr(*field.value), field.location});
        importer.asset.materials.push_back(std::move(copy));
    }
    for (const auto& palette : source.materialPalettes) {
        PaletteDecl copy;
        copy.name = palette.name;
        copy.location = palette.location;
        copy.sourceRange = palette.sourceRange;
        for (const auto& element : palette.elements) copy.elements.push_back(cloneExpr(*element));
        importer.asset.palettes.push_back(std::move(copy));
    }
    for (const auto& constant : source.materialConstants) {
        importer.asset.constants.push_back(Asset::Constant{
            constant.type, constant.name, cloneExpr(*constant.value), constant.location, constant.sourceRange});
    }
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

ValueType inferType(const Expr& expr) {
    return std::visit(
        [](const auto& node) -> ValueType {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Literal>) {
                switch (node.kind) {
                case Literal::Kind::Float: return ValueType::F32;
                case Literal::Kind::Color: return ValueType::Color;
                default: return ValueType::I32;
                }
            } else if constexpr (std::is_same_v<T, RandomRange>) {
                return inferType(*node.lower);
            } else if constexpr (std::is_same_v<T, VectorLit>) {
                return ValueType::Vec2;
            } else if constexpr (std::is_same_v<T, Unary>) {
                return inferType(*node.operand);
            } else if constexpr (std::is_same_v<T, Binary>) {
                const auto left = inferType(*node.left);
                const auto right = inferType(*node.right);
                if (left == ValueType::Vec2 || right == ValueType::Vec2) return ValueType::Vec2;
                if (left == ValueType::Color || right == ValueType::Color) return ValueType::Color;
                if (left == ValueType::F32 || right == ValueType::F32) return ValueType::F32;
                return ValueType::I32;
            } else if constexpr (std::is_same_v<T, SamplerCapture>) {
                return inferType(*node.value);
            } else if constexpr (std::is_same_v<T, Member>) {
                return ValueType::I32;
            } else {
                return ValueType::Color;
            }
        },
        expr);
}

DefInfo buildDefInfo(const MaterialDef& def) {
    DefInfo info;
    info.def = &def;
    std::set<std::string> seen;
    const MaterialField* base = nullptr;
    for (const auto& field : def.fields) {
        if (!seen.insert(field.name).second)
            throw SourceError{field.location, "duplicate material field '" + field.name + "' in '" + def.name + "'"};
        if (field.name == "base") base = &field;
    }
    if (!base)
        throw SourceError{def.location, "material '" + def.name + "' must define 'base'"};
    auto add = [&](const char* name, ValueType type) {
        const MaterialField* found = nullptr;
        for (const auto& field : def.fields)
            if (field.name == name) found = &field;
        info.fields.push_back(FieldInfo{name, type, found});
    };
    add("base", ValueType::Color);
    add("edge", ValueType::Color);
    add("taper", ValueType::F32);
    add("edge_width", ValueType::I32);
    for (const auto& field : def.fields) {
        if (field.name == "base" || field.name == "edge" || field.name == "taper" ||
            field.name == "edge_width")
            continue;
        info.fields.push_back(FieldInfo{field.name, inferType(*field.value), &field});
    }
    return info;
}

struct OrigParam {
    std::string name;
    bool isMaterial = false;
    const DefInfo* def = nullptr;
};

struct Expander {
    std::map<std::string, DefInfo> defs;
    std::map<std::string, std::vector<OrigParam>> funcs;
    std::map<std::string, const DefInfo*> vars;

    const DefInfo& requireDef(const std::string& name, SourceLocation at) const {
        const auto found = defs.find(name);
        if (found == defs.end())
            throw SourceError{at, "unknown material '" + name + "' (no imported definition with that name)"};
        return found->second;
    }

    void transformExpr(std::unique_ptr<Expr>& expr) { if (expr) transformExpr(*expr); }

    void transformExpr(Expr& expr) {
        if (auto* member = std::get_if<Member>(&expr)) {
            if (const auto* id = std::get_if<Identifier>(member->object.get())) {
                if (const auto found = vars.find(id->name); found != vars.end()) {
                    const DefInfo* def = found->second;
                    if (!def->find(member->name))
                        throw SourceError{member->location, "material '" + def->def->name +
                                                                 "' has no field '" + member->name + "'"};
                    const std::string flat = id->name + "__" + member->name;
                    const auto at = member->location;
                    expr = Identifier{flat, at};
                    return;
                }
            }
            transformExpr(member->object);
            return;
        }
        if (auto* call = std::get_if<Call>(&expr)) {
            if (const auto* callee = std::get_if<Identifier>(call->callee.get())) {
                if (const auto def = defs.find(callee->name); def != defs.end() && call->args.empty())
                    throw SourceError{callee->location, "material '" + callee->name +
                                                             "' can only be created by assignment, such as m = " +
                                                             callee->name + "()"};
                if (const auto fn = funcs.find(callee->name); fn != funcs.end()) {
                    expandCall(*call, fn->second);
                    return;
                }
            }
            transformExpr(call->callee);
            for (auto& arg : call->args) transformExpr(arg.value);
            return;
        }
        std::visit(
            [&](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Binary>) {
                    transformExpr(node.left); transformExpr(node.right);
                } else if constexpr (std::is_same_v<T, Unary>) {
                    transformExpr(node.operand);
                } else if constexpr (std::is_same_v<T, VectorLit> || std::is_same_v<T, ListLit>) {
                    for (auto& item : node.elements) transformExpr(item);
                } else if constexpr (std::is_same_v<T, Index>) {
                    transformExpr(node.object); transformExpr(node.index);
                } else if constexpr (std::is_same_v<T, RandomRange>) {
                    transformExpr(node.lower); transformExpr(node.upper);
                    if (node.distribution) transformExpr(node.distribution);
                } else if constexpr (std::is_same_v<T, SamplerCapture>) {
                    transformExpr(node.value);
                } else if constexpr (std::is_same_v<T, SamplerPick>) {
                    transformExpr(node.receiver); if (node.count) transformExpr(node.count);
                } else if constexpr (std::is_same_v<T, ListMethod>) {
                    transformExpr(node.receiver);
                } else if constexpr (std::is_same_v<T, SurfaceBlur>) {
                    transformExpr(node.receiver); transformExpr(node.sigma.value);
                } else if constexpr (std::is_same_v<T, PointDistributionLiteral>) {
                    for (auto& point : node.points) { transformExpr(point.key); transformExpr(point.value); }
                }
            },
            expr);
    }

    void expandCall(Call& call, const std::vector<OrigParam>& params) {
        std::map<std::string, Arg*> supplied;
        std::size_t positional = 0;
        for (auto& arg : call.args) {
            if (arg.name) supplied[*arg.name] = &arg;
            else if (positional < params.size()) supplied[params[positional++].name] = &arg;
        }
        std::vector<Arg> out;
        for (const auto& param : params) {
            const auto found = supplied.find(param.name);
            if (found == supplied.end()) continue;
            Arg* arg = found->second;
            if (!param.isMaterial) {
                transformExpr(arg->value);
                out.push_back(Arg{arg->name, std::move(arg->value), arg->location});
                continue;
            }
            const auto* id = std::get_if<Identifier>(arg->value.get());
            const auto var = id ? vars.find(id->name) : vars.end();
            if (!id || var == vars.end() || var->second != param.def)
                throw SourceError{arg->location, "material argument for '" + param.name +
                                                      "' must be a " + param.def->def->name + " variable"};
            for (const auto& field : param.def->fields) {
                auto value = std::make_unique<Expr>(Identifier{id->name + "__" + field.name, id->location});
                out.push_back(Arg{arg->name ? std::optional(param.name + "__" + field.name) : std::nullopt,
                                  std::move(value), arg->location});
            }
        }
        call.args = std::move(out);
    }

    std::unique_ptr<Expr> fieldValue(const std::string& var, const FieldInfo& field) {
        if (field.field) {
            auto value = cloneExpr(*field.field->value);
            transformExpr(value);
            return value;
        }
        if (field.name == "edge")
            return std::make_unique<Expr>(Identifier{var + "__base", {}});
        if (field.name == "taper")
            return std::make_unique<Expr>(Literal{Literal::Kind::Float, "1.0", {}});
        return std::make_unique<Expr>(Literal{Literal::Kind::Int, "0", {}});
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
                if (const auto* call = std::get_if<Call>(decl->value.get())) {
                    if (const auto* callee = std::get_if<Identifier>(call->callee.get())) {
                        if (const auto found = defs.find(callee->name);
                            found != defs.end() && call->args.empty()) {
                            const DefInfo* def = &found->second;
                            vars[decl->name] = def;
                            for (const auto& field : def->fields) {
                                auto value = fieldValue(decl->name, field);
                                out.push_back(Stmt{VarDecl{decl->name + "__" + field.name,
                                                           std::move(value), decl->location}});
                            }
                            return out;
                        }
                    }
                }
                if (const auto* id = std::get_if<Identifier>(decl->value.get())) {
                    if (const auto src = vars.find(id->name); src != vars.end()) {
                        vars[decl->name] = src->second;
                        for (const auto& field : src->second->fields)
                            out.push_back(Stmt{VarDecl{decl->name + "__" + field.name,
                                                       std::make_unique<Expr>(Identifier{
                                                           id->name + "__" + field.name, id->location}),
                                                       decl->location}});
                        return out;
                    }
                }
                transformExpr(decl->value);
            }
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
            forStmt->body = transformStmts(forStmt->body);
        } else if (auto* ret = std::get_if<ReturnStmt>(&stmt)) {
            if (ret->value) {
                if (const auto* id = std::get_if<Identifier>(ret->value.get());
                    id && vars.contains(id->name))
                    throw SourceError{id->location, "returning a material is not supported; return a field"};
                transformExpr(ret->value);
            }
        } else if (auto* layer = std::get_if<LayerStmt>(&stmt)) {
            vars.erase(layer->name);
            layer->body = transformStmts(layer->body);
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
    Expander expander;
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
        for (const auto& param : fn.params) {
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
        std::vector<Param> params;
        for (const auto& param : fn.params) {
            if (!param.materialType) {
                params.push_back(param);
                continue;
            }
            const DefInfo* def = &expander.requireDef(*param.materialType, param.location);
            expander.vars[param.name] = def;
            for (const auto& field : def->fields)
                params.push_back(Param{param.name + "__" + field.name, field.type, param.location});
        }
        fn.params = std::move(params);
        fn.body = expander.transformStmts(fn.body);
    }
    program.asset.materials.clear();
}

} // namespace iprop::ips

