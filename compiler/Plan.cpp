#include "Plan.hpp"
#include "ips/Fold.hpp"

#include <set>
#include <utility>

namespace iprop::compiler {
namespace {

ValueType runtimeType(ips::ValueType type) {
    switch (type) {
    case ips::ValueType::I32: return ValueType::I32;
    case ips::ValueType::F32: return ValueType::F32;
    case ips::ValueType::Color: return ValueType::Color;
    case ips::ValueType::Vec2: return ValueType::Vec2;
    default: return ValueType::I32; // compounds never enter the tables
    }
}

} // namespace

std::optional<u32> Plan::functionId(const std::string& name,
                                    const std::string& ownerNamespace) const {
    const auto found = functionIndex.find(ownerNamespace + name);
    if (found == functionIndex.end()) {
        return std::nullopt;
    }
    return found->second;
}

const ips::ResolvedPalette& Plan::palette(const ips::Program& owner, const ips::Expr& expr) const {
    const auto* name = std::get_if<ips::Identifier>(&expr);
    if (!name) throw ips::SourceError{ips::locationOf(expr),"expected a palette name"};
    return palettes.at(&ips::findPalette(owner,*name));
}

Planner::Planner(const ips::Program& entry,
                 const std::map<std::string, const ips::Program*>& parts)
    : entry_(entry), parts_(parts), plan_(std::make_unique<Plan>()) {
    plan_->entry = &entry;
}

std::string Plan::sourcePath(const ips::Program* owner) const {
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] == owner) return entry->imports[i].path;
    }
    return {};
}

[[noreturn]] void Planner::error(const ips::SourceLocation& at,
                                 std::string message) {
    throw ips::SourceError{at, std::move(message)};
}

const ips::InputDecl* Planner::ownerInput(const ips::Program* owner,
                                          const std::string& name) const {
    for (const auto& input : owner->asset.inputs) {
        if (input.name == name) {
            return &input;
        }
    }
    return nullptr;
}

const ips::OutputDecl* Planner::ownerOutput(const ips::Program* owner,
                                            const std::string& name) const {
    for (const auto& output : owner->asset.outputs) {
        if (output.name == name) {
            return &output;
        }
    }
    return nullptr;
}

std::unique_ptr<Plan> Planner::plan() {
    planParts();
    const auto preparePalettes = [&](const ips::Program& owner) {
        for (const auto& palette : owner.asset.palettes) {
            if (plan_->palettes.contains(&palette)) continue;
            try { plan_->palettes.emplace(&palette,ips::resolvePalette(palette)); }
            catch (ips::SourceError& error) { error.path=plan_->sourcePath(&owner); throw; }
        }
    };
    preparePalettes(entry_);
    for (const auto* part : plan_->parts) preparePalettes(*part);
    planFunctions();
    registerNames();

    // Calls prepare callees lazily while their result types are inferred.
    for (const auto& [alias, part] : parts_) {
        ips::PartInfo info;
        for (const auto& input : part->asset.inputs) {
            info.inputs.push_back({input.type, input.name});
        }
        for (const auto& output : part->asset.outputs) {
            info.outputs.push_back({output.kind, output.isList, output.name});
        }
        surfaces_.emplace(alias, std::move(info));
    }
    typer_ = std::make_unique<ips::Typer>(surfaces_);
    typer_->resolveFunction = [this](const std::string& name,
                                           const ips::SourceLocation& at,
                                           const std::string& ns) {
        return resolveFunction(name, at, ns);
    };

    for (const auto& function : plan_->functions) {
        prepareFunction(function.name);
    }

    for (auto& function : plan_->functions) {
        const CallResolver resolver(*plan_, function);
        try {
            for (auto& [expr, annotation] : function.expressions) {
                if (const auto* call = std::get_if<ips::Call>(expr))
                    function.calls.emplace(call, resolver.resolve(*call));
                if (const auto* index = std::get_if<ips::Index>(expr))
                    annotation.palette = &plan_->palette(*function.owner, *index->object);
            }
        } catch (ips::SourceError& error) {
            if (error.path.empty()) error.path = plan_->sourcePath(function.owner);
            throw;
        }
    }

    // The local names are known only after preparation: register them now,
    // in function table order (deterministic).
    for (const auto& function : plan_->functions) {
        for (const auto& [name, unused] : function.slots) {
            (void)unused;
            plan_->pools.addString(name);
        }
    }

    return std::move(plan_);
}

std::optional<ips::FunctionSurface> Planner::resolveFunction(
    const std::string& name, const ips::SourceLocation&,
    const std::string& ns) {
    // A part was checked as a standalone program: only its own helpers
    // are visible, regardless of the parent asset's function names.
    const auto id = plan_->functionId(name, ns);
    if (!id) {
        return std::nullopt;
    }
    const std::string& effective = plan_->functions[*id].name;
    // The table slots were pre-created by planFunctions; a function counts
    // as resolved only once its body was prepared.
    if (prepared_.count(effective) == 0) {
        if (registry_.count(effective) == 0) {
            return std::nullopt;
        }
        prepareFunction(effective); // on demand
    }
    const PlannedFunction& info = plan_->functions[*id];
    return ips::FunctionSurface{ips::declaredParamTypes(*info.ast),
                                info.langResult};
}

void Planner::planParts() {
    for (const auto& output : entry_.asset.outputs) {
        plan_->rootSlots.push_back({output.kind, output.isList});
        plan_->rootSlotIndex.emplace(output.name,
                                    static_cast<u32>(plan_->rootSlots.size() - 1));
    }

    for (const auto& import : entry_.imports) {
        const auto found = parts_.find(import.alias);
        if (found == parts_.end()) {
            error(import.location,
                  "imported part '" + import.alias + "' was not provided");
        }
        const ips::Program* part = found->second;
        if (!part->imports.empty()) {
            throw ips::SourceError{part->imports[0].location,
                  "nested part imports are not supported yet", import.path};
        }
        plan_->partIndex.emplace(import.alias,
                                static_cast<u32>(plan_->parts.size()));
        plan_->parts.push_back(part);
    }
}

void Planner::planFunctions() {
    // Table order: draw first, then the entry's other functions in
    // declaration order, then each part's functions.
    std::vector<std::string> planned;
    for (const auto& function : entry_.asset.functions) {
        if (function.name == "draw") {
            planned.push_back("draw");
            registry_.emplace("draw", &function);
            registryDrawLike_.emplace("draw", true);
        }
    }
    for (const auto& function : entry_.asset.functions) {
        if (function.name != "draw") {
            planned.push_back(function.name);
            registry_.emplace(function.name, &function);
            registryDrawLike_.emplace(function.name, false);
        }
    }
    for (const auto& import : entry_.imports) {
        const ips::Program* part = parts_.at(import.alias);
        for (const auto& function : part->asset.functions) {
            const std::string name = import.alias + "." + function.name;
            planned.push_back(name);
            registry_.emplace(name, &function);
            registryDrawLike_.emplace(name, function.name == "draw");
        }
    }
    for (const auto& name : planned) {
        plan_->functionIndex.emplace(name,
                                    static_cast<u32>(plan_->functions.size()));
        plan_->functions.push_back({});
        plan_->functions.back().name = name;
    }
}

void Planner::registerNames() {
    // Deterministic order: function names, then slot names (root first, then
    // parts), then input names.
    for (const auto& function : plan_->functions) {
        plan_->pools.addString(function.name);
    }
    for (const auto& output : entry_.asset.outputs) {
        plan_->pools.addString(output.name);
    }
    for (const auto& import : entry_.imports) {
        for (const auto& output : parts_.at(import.alias)->asset.outputs) {
            plan_->pools.addString(output.name);
        }
    }
    for (const auto& input : entry_.asset.inputs) {
        plan_->pools.addString(input.name);
    }
    for (const auto& import : entry_.imports) {
        for (const auto& input : parts_.at(import.alias)->asset.inputs) {
            plan_->pools.addString(input.name);
        }
    }
}

// Prepares one function: fills its params, discovers locals in
// first-assignment order and computes the return type. The table index was
// reserved by planFunctions, so functions checked lazily during this walk
// keep coming later.
void Planner::prepareFunction(const std::string& name) {
    try {
        prepareFunctionBody(name);
    } catch (ips::SourceError& error) {
        const auto dot = name.find('.');
        if (error.path.empty() && dot != std::string::npos) {
            error.path = plan_->sourcePath(parts_.at(name.substr(0, dot)));
        }
        throw;
    }
}

void Planner::prepareFunctionBody(const std::string& name) {
    if (prepared_.count(name) != 0) {
        return;
    }
    if (preparing_.count(name) != 0) {
        error(registry_.at(name)->location,
              "recursive call to '" + name + "' is not supported");
    }
    preparing_.insert(name);

    const ips::Function* ast = registry_.at(name);
    const bool drawLike = registryDrawLike_.at(name);
    const ips::Program* owner = &entry_;
    const auto dot = name.find('.');
    if (dot != std::string::npos) {
        owner = parts_.at(name.substr(0, dot));
    }

    PlannedFunction& info = plan_->functions.at(plan_->functionIndex.at(name));
    info.ast = ast;
    info.owner = owner;
    if (dot != std::string::npos) {
        info.partOwner = plan_->partIndex.at(name.substr(0, dot));
    }
    info.drawLike = drawLike;

    u32 slot = 0;
    u32 forCount = 0; // names the hidden for-loop bound locals
    if (drawLike) {
        if (!ast->params.empty())
            error(ast->location, "'draw' takes no declared parameters; use 'draw { ... }'");
        info.params = {ValueType::I32, ValueType::I32}; // variant, archetype
        for (const std::string pname : {"v", "a"}) {
            info.slots.emplace(pname, slot);
            info.langTypes.emplace(pname, ips::ValueType::I32);
            ++slot;
        }
        for (const auto& input : owner->asset.inputs) {
            if (input.type == ips::InputType::I32) {
                error(input.location,
                      "input i32 is not supported by the v0.1 format (use u32)");
            }
            info.slots.emplace(input.name, slot);
            info.langTypes.emplace(input.name, ips::valueTypeOf(input.type));
            info.params.push_back(runtimeType(ips::valueTypeOf(input.type)));
            ++slot;
        }
    } else {
        for (const auto& param : ast->params) {
            const ips::ValueType type = ips::valueTypeOf(param.type);
            info.slots.emplace(param.name, slot);
            info.langTypes.emplace(param.name, type);
            info.params.push_back(runtimeType(type));
            ++slot;
        }
    }

    ips::Typer::Scope savedScope = typer_->saveScope();
    const std::string ns =
        dot != std::string::npos ? name.substr(0, dot + 1) : "";
    typer_->enterFunction(*owner, *ast, drawLike, ns);
    std::optional<ips::ValueType> returnType;

    const auto walk = [&](auto&& self, const std::vector<ips::Stmt>& body) -> void {
        for (const auto& stmt : body) {
            if (const auto* decl = std::get_if<ips::VarDecl>(&stmt)) {
                const ips::ValueType type = typer_->typeOf(*decl->value);
                if (ownerOutput(owner, decl->name) != nullptr ||
                    ownerInput(owner, decl->name) != nullptr ||
                    info.slots.count(decl->name) != 0) {
                    continue; // output write, input or parameter
                }
                info.slots.emplace(decl->name, slot);
                info.langTypes.emplace(decl->name, type);
                typer_->declareLocal(decl->name, type);
                info.locals.push_back(runtimeType(type));
                ++slot;
            } else if (const auto* expression = std::get_if<ips::ExprStmt>(&stmt)) {
                typer_->typeOf(*expression->expr);
            } else if (const auto* ret = std::get_if<ips::ReturnStmt>(&stmt)) {
                if (ret->value) {
                    const ips::ValueType type =
                        typer_->typeOf(*ret->value);
                    if (ast->returnType.has_value()) {
                        const ips::ValueType declared =
                            ips::valueTypeOf(*ast->returnType);
                        if (!ips::argFits(declared, type)) {
                            error(ret->location,
                                  "return type mismatch in '" + name + "'");
                        }
                    } else if (returnType.has_value() && *returnType != type) {
                        error(ret->location,
                              "inconsistent return type in '" + name + "'");
                    }
                    returnType = type;
                }
            } else if (const auto* ifStmt = std::get_if<ips::IfStmt>(&stmt)) {
                typer_->typeOf(*ifStmt->condition);
                self(self, ifStmt->thenBranch);
                self(self, ifStmt->elseBranch);
            } else if (const auto* whileStmt =
                           std::get_if<ips::WhileStmt>(&stmt)) {
                typer_->typeOf(*whileStmt->condition);
                self(self, whileStmt->body);
            } else if (const auto* forStmt =
                           std::get_if<ips::ForStmt>(&stmt)) {
                typer_->typeOf(*forStmt->from);
                typer_->typeOf(*forStmt->to);
                // The loop counter is a real local (the body reads it); the
                // bound is a hidden local holding the once-evaluated upper
                // bound. Both are planned here so their slots stay stable
                // regardless of the scratch-local flags.
                if (info.slots.count(forStmt->variable) == 0) {
                    info.slots.emplace(forStmt->variable, slot);
                    info.langTypes.emplace(forStmt->variable,
                                           ips::ValueType::I32);
                    typer_->declareLocal(forStmt->variable,
                                                     ips::ValueType::I32);
                    info.locals.push_back(ValueType::I32);
                    ++slot;
                }
                const std::string boundName = "$for" + std::to_string(forCount);
                ++forCount;
                info.slots.emplace(boundName, slot);
                info.locals.push_back(ValueType::I32);
                ++slot;
                self(self, forStmt->body);
            }
        }
    };
    walk(walk, ast->body);

    ips::ConstantFolder constants;
    for (const auto& [expr, type] : typer_->expressions()) {
        Value value;
        const auto constant = constants.fold(*expr, value) ? std::optional<Value>(value) : std::nullopt;
        info.expressions.emplace(expr, PlannedExpression{type, constant});
    }
    typer_->restoreScope(std::move(savedScope));

    if (ast->returnType.has_value()) {
        const ips::ValueType declared = ips::valueTypeOf(*ast->returnType);
        info.langResult = declared;
        info.results.push_back(runtimeType(declared));
    } else if (returnType.has_value()) {
        // The checker rejects a missing declaration; keep the inference as a
        // fallback for programs planned without a prior checkAsset.
        info.langResult = returnType;
        info.results.push_back(runtimeType(*returnType));
    }
    if (name == "draw" && ast->returnType.has_value()) {
        error(ast->location, "'draw' cannot return a value");
    }
    preparing_.erase(name);
    prepared_.insert(name);
}

} // namespace iprop::compiler
