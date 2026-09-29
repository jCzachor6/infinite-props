#include "Plan.hpp"

#include <set>
#include <utility>

namespace iprop::compiler {
namespace {

ips::ValueType inputValueType(ips::InputType type) {
    switch (type) {
    case ips::InputType::Color: return ips::ValueType::Color;
    case ips::InputType::F32: return ips::ValueType::F32;
    case ips::InputType::U32:
    case ips::InputType::I32: return ips::ValueType::I32;
    }
    return ips::ValueType::I32;
}

ValueType runtimeType(ips::ValueType type) {
    switch (type) {
    case ips::ValueType::I32: return ValueType::I32;
    case ips::ValueType::F32: return ValueType::F32;
    case ips::ValueType::Color: return ValueType::Color;
    default: return ValueType::I32; // compounds never enter the tables
    }
}

} // namespace

Planner::Planner(const ips::Program& entry,
                 const std::map<std::string, const ips::Program*>& parts)
    : entry_(entry), parts_(parts), plan_(std::make_unique<Plan>()) {
    plan_->entry = &entry;
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
    planFunctions();
    registerNames();

    // The shared type engine resolves part calls and output reads against
    // the part surfaces; user functions resolve through the planner so a
    // call checks its callee lazily (recursion was already rejected). The
    // resolver captures `this`, so it lives exactly as long as the Planner;
    // the plan stays on the heap, keeping the callback valid after plan()
    // returns (the emitter keeps typing through the plan's Typer).
    for (const auto& [alias, part] : parts_) {
        ips::PartInfo info;
        for (const auto& input : part->asset.inputs) {
            info.inputs.push_back({input.type, input.name});
        }
        for (const auto& output : part->asset.outputs) {
            info.outputs.push_back({output.kind, output.isList, output.name});
        }
        plan_->surfaces.emplace(alias, std::move(info));
    }
    plan_->typer = std::make_unique<ips::Typer>(plan_->surfaces);
    plan_->typer->resolveFunction = [this](const std::string& name,
                                           const ips::SourceLocation& at) {
        return resolveFunction(name, at);
    };

    for (const auto& function : plan_->functions) {
        prepareFunction(function.name);
    }

    // The local names are known only after preparation: register them now,
    // in function table order (deterministic).
    for (const auto& function : plan_->functions) {
        for (const auto& [name, unused] : function.slots) {
            (void)unused;
            plan_->pools.addString(name);
        }
    }

    // Planning is complete: rebind the resolver to a self-contained lookup
    // over the heap plan, so the emitter can keep typing through the Typer
    // after this Planner is gone.
    Plan* self = plan_.get();
    plan_->typer->resolveFunction =
        [self](const std::string& name, const ips::SourceLocation&)
        -> std::optional<ips::FunctionSurface> {
        const auto found = self->functionIndex.find(name);
        if (found == self->functionIndex.end()) {
            return std::nullopt;
        }
        const PlannedFunction& info = self->functions[found->second];
        return ips::FunctionSurface{info.ast->params.size(), info.langResult};
    };

    return std::move(plan_);
}

std::optional<ips::FunctionSurface> Planner::resolveFunction(
    const std::string& name, const ips::SourceLocation&) {
    // The table slots were pre-created by planFunctions; a function counts
    // as resolved only once its body was prepared.
    if (prepared_.count(name) == 0) {
        if (registry_.count(name) == 0) {
            return std::nullopt;
        }
        prepareFunction(name); // on demand
    }
    const PlannedFunction& info = plan_->functions[plan_->functionIndex.at(name)];
    return ips::FunctionSurface{info.ast->params.size(), info.langResult};
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
            error(part->imports[0].location,
                  "nested part imports are not supported yet");
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
    info.drawLike = drawLike;

    u32 slot = 0;
    if (drawLike) {
        info.params = {ValueType::I32, ValueType::I32}; // variant, archetype
        for (int i = 0; i < 2; ++i) {
            const std::string& pname = ast->params[static_cast<std::size_t>(i)];
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
            info.langTypes.emplace(input.name, inputValueType(input.type));
            info.params.push_back(runtimeType(inputValueType(input.type)));
            ++slot;
        }
    } else {
        for (const auto& param : ast->params) {
            info.slots.emplace(param, slot);
            info.langTypes.emplace(param, ips::ValueType::I32);
            info.params.push_back(ValueType::I32);
            ++slot;
        }
    }

    ips::Typer::Scope savedScope = plan_->typeEngine().saveScope();
    plan_->typeEngine().enterFunction(*owner, *ast, drawLike);
    std::optional<ips::ValueType> returnType;

    const auto walk = [&](auto&& self, const std::vector<ips::Stmt>& body) -> void {
        for (const auto& stmt : body) {
            if (const auto* decl = std::get_if<ips::VarDecl>(&stmt)) {
                if (ownerOutput(owner, decl->name) != nullptr ||
                    ownerInput(owner, decl->name) != nullptr ||
                    info.slots.count(decl->name) != 0) {
                    continue; // output write, input or parameter
                }
                const ips::ValueType type = plan_->typeEngine().typeOf(*decl->value);
                info.slots.emplace(decl->name, slot);
                info.langTypes.emplace(decl->name, type);
                plan_->typeEngine().declareLocal(decl->name, type);
                info.locals.push_back(runtimeType(type));
                ++slot;
            } else if (const auto* ret = std::get_if<ips::ReturnStmt>(&stmt)) {
                if (ret->value) {
                    const ips::ValueType type =
                        plan_->typeEngine().typeOf(*ret->value);
                    if (returnType.has_value() && *returnType != type) {
                        error(ret->location,
                              "inconsistent return type in '" + name + "'");
                    }
                    returnType = type;
                }
            } else if (const auto* ifStmt = std::get_if<ips::IfStmt>(&stmt)) {
                self(self, ifStmt->thenBranch);
                self(self, ifStmt->elseBranch);
            } else if (const auto* whileStmt =
                           std::get_if<ips::WhileStmt>(&stmt)) {
                self(self, whileStmt->body);
            }
        }
    };
    walk(walk, ast->body);

    plan_->typeEngine().restoreScope(std::move(savedScope));

    if (returnType.has_value()) {
        info.langResult = returnType;
        info.results.push_back(runtimeType(*returnType));
    }
    if (name == "draw" && returnType.has_value()) {
        error(ast->location, "'draw' cannot return a value");
    }
    preparing_.erase(name);
    prepared_.insert(name);
}

} // namespace iprop::compiler
