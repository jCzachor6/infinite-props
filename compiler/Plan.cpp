#include "Plan.hpp"
#include "ips/Fold.hpp"
#include "ips/Imports.hpp"
#include "ips/Semantics.hpp"

#include <set>
#include <utility>

namespace iprop::compiler {
namespace {

// The geometric outputs of an asset in declaration order: its output slots.
std::vector<SlotInfo> geometrySlots(const ips::Asset& asset) {
    std::vector<SlotInfo> slots;
    for (const auto& output : asset.outputs)
        if (!ips::isResourceOutput(output.kind)) slots.push_back({output.kind, output.isList, output.name});
    return slots;
}

} // namespace

std::string functionNamespace(const std::string& functionName) {
    const auto dot = functionName.rfind('.');
    return dot == std::string::npos ? std::string() : functionName.substr(0, dot + 1);
}

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
    return *palettes.at(&ips::findPalette(owner,*name));
}

Planner::Planner(const ips::Program& entry,
                 const std::map<std::string, const ips::Program*>& parts,
                 const std::map<std::string, std::string>& partSources)
    : entry_(entry), parts_(parts), partSources_(partSources),
      plan_(std::make_unique<Plan>()) {
    plan_->entry = &entry;
}

std::string Plan::sourcePath(const ips::Program* owner) const {
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] == owner) return instances[i].source;
    }
    return {};
}

[[noreturn]] void Planner::error(const ips::SourceLocation& at,
                                 std::string message) {
    throw ips::SourceError{at, std::move(message), {}, ips::DiagnosticCode::PlanError,
                          ips::DiagnosticStage::Planning};
}

ValueType Planner::storedType(ips::ValueType type, const ips::SourceLocation& at, const std::string& slot) {
    if (!type.isStorable())
        error(at, "cannot store " + std::string(ips::typeName(type)) + " in " + slot + " (only scalars can be stored)");
    return type;
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
    // Calls prepare callees lazily while their result types are inferred.
    // Surfaces are keyed by alias path; the Typer prefixes its namespace.
    for (std::size_t i = 0; i < plan_->parts.size(); ++i)
        surfaces_.emplace(plan_->instances[i].path, ips::partInfoOf(plan_->parts[i]->asset));
    typer_ = std::make_unique<ips::Typer>(surfaces_);
    typer_->resolveFunction = [this](const std::string& name,
                                     const ips::SourceLocation& at,
                                     const std::string& ns) {
        return resolveFunction(name, at, ns);
    };
    // Each program's declarations are resolved once, for the plan and for
    // the Typer's probe of statically known runtime values.
    const auto prepareConstants = [&](const ips::Program& owner) {
        if (plan_->assetConstants.contains(&owner)) return;
        try {
            auto declarations = ips::resolveAssetDeclarations(owner);
            typer_->registerAssetDeclarations(owner, declarations);
            plan_->assetConstants.emplace(&owner, std::move(declarations.constants));
            for (auto& [palette, value] : declarations.palettes)
                plan_->palettes.emplace(palette, std::make_shared<const ips::ResolvedPalette>(std::move(value)));
            plan_->paletteLists.merge(declarations.paletteLists);
        }
        catch (ips::SourceError& error) { error.path = plan_->sourcePath(&owner); throw; }
    };
    prepareConstants(entry_);
    for (const auto* part : plan_->parts) prepareConstants(*part);
    const auto prepareDefaults = [&](const ips::Program& owner) {
        for (const auto& input : owner.asset.inputs) {
            if (plan_->inputDefaults.contains(&input)) continue;
            try { plan_->inputDefaults.emplace(&input, ips::resolveInputDefault(input)); }
            catch (ips::SourceError& error) { error.path = plan_->sourcePath(&owner); throw; }
        }
    };
    prepareDefaults(entry_);
    for (const auto* part : plan_->parts) prepareDefaults(*part);
    planFunctions();
    registerNames();

    for (const auto& function : plan_->functions) {
        prepareFunction(function.name);
    }

    for (auto& function : plan_->functions) {
        const CallResolver resolver(*plan_, function);
        try {
            for (const auto& entry : function.boundCalls)
                function.calls.emplace(entry.first, resolver.resolve(*entry.first));
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
                                info.langResult, ips::declaredParamNames(*info.ast)};
}

void Planner::planParts() {
    plan_->rootSlots = geometrySlots(entry_.asset);
    for (u32 slot = 0; slot < plan_->rootSlots.size(); ++slot)
        plan_->rootSlotIndex.emplace(plan_->rootSlots[slot].name, slot);

    // Instances in preorder of import order: a flat source keeps its import
    // order, and each child directly follows its parent's earlier children.
    const auto expand = [&](auto&& self, const ips::Program& owner, const std::string& prefix,
                            u32 parent, const std::string& ownerSource) -> void {
        for (const auto& import : owner.imports) {
            // Stop expanding before the plan grows past the format limit.
            if (plan_->parts.size() >= kMaxParts) {
                throw ips::SourceError{import.location, "too many part instances (limit " +
                    std::to_string(kMaxParts) + " after expanding nested imports)",
                    ownerSource, ips::DiagnosticCode::ResourceLimit, ips::DiagnosticStage::Planning};
            }
            const std::string path = prefix + import.alias;
            const auto found = parts_.find(path);
            if (found == parts_.end()) {
                throw ips::SourceError{import.location, "imported part '" + path + "' was not provided",
                    ownerSource, ips::DiagnosticCode::PlanError, ips::DiagnosticStage::Planning};
            }
            const auto index = static_cast<u32>(plan_->parts.size());
            const auto named = partSources_.find(path);
            const auto source = named != partSources_.end() ? named->second
                                                            : ips::resolveImportPath(ownerSource, import.path);
            plan_->partIndex.emplace(path, index);
            plan_->parts.push_back(found->second);
            plan_->instances.push_back({path, parent, source});
            self(self, *found->second, path + ".", index, source);
        }
    };
    expand(expand, entry_, "", kRootParent, "");

    // Part slots follow the root slots, in instance order.
    auto slot = static_cast<u32>(plan_->rootSlots.size());
    for (u32 part = 0; part < plan_->parts.size(); ++part) {
        plan_->partSlots.push_back(geometrySlots(plan_->parts[part]->asset));
        for (const auto& output : plan_->partSlots.back())
            plan_->partSlotIndex.emplace(std::pair{part, output.name}, slot++);
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
    for (std::size_t i = 0; i < plan_->parts.size(); ++i) {
        for (const auto& function : plan_->parts[i]->asset.functions) {
            const std::string name = plan_->instances[i].path + "." + function.name;
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
    const auto exports = [&](const ips::Program& program, u32 owner) {
        for (const auto& output : program.asset.outputs) {
            if (!ips::isResourceOutput(output.kind)) continue;
            if (plan_->resourceExports.size() >= kMaxConstPoolEntries)
                error(output.location, "too many resource exports");
            const auto type = output.kind == ips::OutputKind::Mask
                ? ips::ValueType::Mask : ips::ValueType::Layer;
            plan_->pools.addType(type);
            plan_->resourceSlotIndex.emplace(std::pair{owner, output.name},
                static_cast<u16>(plan_->resourceExports.size()));
            plan_->resourceExports.push_back({owner, output.name, type});
        }
    };
    exports(entry_, kRootParent);
    for (u32 p = 0; p < plan_->parts.size(); ++p) exports(*plan_->parts[p], p);
    // Deterministic order: function names, then slot names (root first, then
    // parts), then input names, then part instance names.
    for (const auto& function : plan_->functions) {
        plan_->pools.addString(function.name);
    }
    for (const auto& output : entry_.asset.outputs) {
        plan_->pools.addString(output.name);
    }
    for (const auto* part : plan_->parts) {
        for (const auto& output : part->asset.outputs) {
            plan_->pools.addString(output.name);
        }
    }
    for (const auto& input : entry_.asset.inputs) {
        plan_->pools.addString(input.name);
    }
    for (const auto* part : plan_->parts) {
        for (const auto& input : part->asset.inputs) {
            plan_->pools.addString(input.name);
        }
    }
    for (const auto& instance : plan_->instances) {
        plan_->pools.addString(instance.path);
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
        const auto ns = functionNamespace(name);
        if (error.path.empty() && !ns.empty()) {
            error.path = plan_->instances[plan_->partIndex.at(ns.substr(0, ns.size() - 1))].source;
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
    const std::string ns = functionNamespace(name);
    PlannedFunction& info = plan_->functions.at(plan_->functionIndex.at(name));
    if (!ns.empty()) {
        info.partOwner = plan_->partIndex.at(ns.substr(0, ns.size() - 1));
        owner = plan_->parts[*info.partOwner];
    }
    info.ast = ast;
    info.owner = owner;
    info.drawLike = drawLike;

    u32 slot = 0;
    u32 forCount = 0; // names the hidden for-loop bound locals
    if (drawLike) {
        if (!ast->params.empty())
            error(ast->location, "'draw' takes no declared parameters; use 'draw { ... }'");
        for (const auto& input : owner->asset.inputs) {
            info.slots.emplace(input.name, slot);
            info.langTypes.emplace(input.name, ips::valueTypeOf(input.type));
            info.params.push_back(storedType(ips::valueTypeOf(input.type), input.location, "input '" + input.name + "'"));
            ++slot;
        }
    } else {
        for (const auto& param : ast->params) {
            const ips::ValueType type = ips::valueTypeOf(param.type);
            info.slots.emplace(param.name, slot);
            info.langTypes.emplace(param.name, type);
            info.params.push_back(storedType(type, param.location, "parameter '" + param.name + "'"));
            ++slot;
        }
    }

    ips::Typer::Scope savedScope = typer_->saveScope();
    typer_->enterFunction(*owner, *ast, drawLike, ns);
    std::optional<ips::ValueType> returnType;
    std::size_t surfaceScopes = 0; // open layer/clip blocks
    const auto enterSurfaceScope = [&](const ips::SourceLocation& at) {
        ips::checkSurfaceScope(surfaceScopes++, at, ips::DiagnosticStage::Planning);
    };

    const auto walk = [&](auto&& self, const std::vector<ips::Stmt>& body) -> void {
        for (const auto& stmt : body) {
            if (const auto* decl = std::get_if<ips::VarDecl>(&stmt)) {
                if (ips::findAssetConstant(*owner, decl->name))
                    error(decl->location, "cannot assign to const '" + decl->name + "'");
                std::optional<ips::ValueType> context = decl->declaredType;
                if (const auto found = info.langTypes.find(decl->name); found != info.langTypes.end()) context = found->second;
                const ips::ValueType type = typer_->typeOf(*decl->value, context);
                if (ownerOutput(owner, decl->name) != nullptr ||
                    ownerInput(owner, decl->name) != nullptr ||
                    info.slots.count(decl->name) != 0) {
                    continue; // output write, input or parameter
                }
                info.slots.emplace(decl->name, slot);
                info.langTypes.emplace(decl->name, type);
                typer_->declareLocal(decl->name, type);
                info.locals.push_back(storedType(type, decl->location, "local '" + decl->name + "'"));
                ++slot;
            } else if (const auto* expression = std::get_if<ips::ExprStmt>(&stmt)) {
                typer_->typeOf(*expression->expr);
            } else if (const auto* ret = std::get_if<ips::ReturnStmt>(&stmt)) {
                if (ret->value) {
                    const ips::ValueType type =
                        typer_->typeOf(*ret->value, ast->returnType);
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
                if (ips::findAssetConstant(*owner, forStmt->variable))
                    error(forStmt->location, "cannot use const '" + forStmt->variable + "' as a loop counter");
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
            } else if (const auto* capture = std::get_if<ips::LayerStmt>(&stmt)) {
                enterSurfaceScope(capture->location);
                self(self, capture->body);
                --surfaceScopes;
                if (!info.slots.contains(capture->name)) {
                    info.slots.emplace(capture->name, slot++);
                    info.langTypes.emplace(capture->name, ips::ValueType::Layer);
                    info.locals.push_back(ValueType::Layer);
                }
                typer_->declareLocal(capture->name, ips::ValueType::Layer);
            } else if (const auto* clip = std::get_if<ips::ClipStmt>(&stmt)) {
                typer_->typeOf(*clip->mask);
                enterSurfaceScope(clip->location);
                self(self, clip->body);
                --surfaceScopes;
            }
        }
    };
    walk(walk, ast->body);

    ips::ConstantFolder constants;
    const auto& assetConstants = plan_->assetConstants.at(owner);
    const auto& resolutions = typer_->resolutions();
    for (const auto& [expr, type] : typer_->expressions()) {
        PlannedExpression planned{type, std::nullopt};
        // Constant point literals are still emitted through their builder in
        // runtime code, preserving operand effects, construction fuel and traps.
        if (Value value; constants.fold(*expr, value)) planned.constant = value;
        if (const auto* id = std::get_if<ips::Identifier>(expr))
            if (const auto found = assetConstants.find(id->name); found != assetConstants.end()) planned.constant = found->second;
        // The Typer decided which names read a palette: a bare name reads its
        // shared list constant, an index lowers through the palette.
        if (const auto found = resolutions.find(expr); found != resolutions.end()) {
            planned.overload = found->second.overload;
            if (const auto* palette = found->second.palette) {
                if (std::holds_alternative<ips::Identifier>(*expr)) planned.constant = plan_->paletteLists.at(palette);
                else planned.palette = plan_->palettes.at(palette).get();
            }
        }
        info.expressions.emplace(expr, std::move(planned));
    }
    info.boundCalls = typer_->calls();
    typer_->restoreScope(std::move(savedScope));

    if (ast->returnType.has_value()) {
        const ips::ValueType declared = ips::valueTypeOf(*ast->returnType);
        info.langResult = declared;
        info.results.push_back(storedType(declared, ast->location, "the result of '" + name + "'"));
    } else if (returnType.has_value()) {
        // The checker rejects a missing declaration; keep the inference as a
        // fallback for programs planned without a prior checkAsset.
        info.langResult = returnType;
        info.results.push_back(storedType(*returnType, ast->location, "the result of '" + name + "'"));
    }
    if (name == "draw" && ast->returnType.has_value()) {
        error(ast->location, "'draw' cannot return a value");
    }
    preparing_.erase(name);
    prepared_.insert(name);
}

} // namespace iprop::compiler
