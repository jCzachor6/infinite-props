#pragma once

// The compilation plan: the resolved product of the planning layer. Name
// resolution, local allocation, function ordering and slot/part indexing
// happen in the Planner (Plan.cpp); the plan is read by the emitter
// (Emit.hpp) and the assembler (Assemble.hpp).

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "iprop/Code.hpp"
#include "iprop/Instruction.hpp"
#include "ips/Ast.hpp"
#include "ips/Typing.hpp"
#include "Pools.hpp"

namespace iprop::compiler {

// One planned function: its signature, local layout and (after emission)
// its instruction stream. `slots`/`langTypes` map every name in scope —
// parameters first, then uniforms (for draw-like functions), then locals in
// first-assignment order — to local indices and language types.
struct PlannedFunction {
    std::string name;
    const ips::Function* ast = nullptr;
    const ips::Program* owner = nullptr;
    bool drawLike = false;

    std::vector<ValueType> params;  // runtime types
    std::vector<ValueType> results; // runtime types
    std::vector<ValueType> locals;  // extra locals beyond params
    std::optional<ips::ValueType> langResult;

    std::map<std::string, u32> slots;
    std::map<std::string, ips::ValueType> langTypes;

    std::vector<Instruction> code; // the emitted IR
    std::vector<u32> sourceLines;  // one .ips line per instruction (debug)
};

// One global output slot (spec §4.3: root outputs first, then part outputs
// in part order).
struct SlotInfo {
    ips::OutputKind kind;
    bool isList = false;
};

// The resolved plan of one compilation: the entry asset, the imported parts
// in import order, the function table in emission order, the global slot
// table, the constant pools and the shared type engine.
struct Plan {
    const ips::Program* entry = nullptr;

    std::vector<const ips::Program*> parts;
    std::map<std::string, u32> partIndex;

    std::vector<SlotInfo> rootSlots;
    std::map<std::string, u32> rootSlotIndex;

    std::vector<PlannedFunction> functions;
    std::map<std::string, u32> functionIndex;

    // The emitter adds literal constants during emission; the assembler
    // writes the pools. Names were registered during planning.
    Pools pools;

    std::map<std::string, ips::PartInfo> surfaces;
    std::unique_ptr<ips::Typer> typer;

    ips::Typer& typeEngine() const { return *typer; }
};

// Resolves a checked entry program plus its imported parts into a Plan.
// Throws ips::SourceError with a located message for problems the semantic
// checker does not cover (nested part imports, i32 uniforms).
//
// The Plan is heap-allocated: the Typer's function resolver captures its
// address, so the callback stays valid after the Planner is gone (the
// emitter keeps typing through the plan's Typer).
class Planner {
public:
    Planner(const ips::Program& entry,
            const std::map<std::string, const ips::Program*>& parts);

    std::unique_ptr<Plan> plan();

private:
    const ips::Program& entry_;
    const std::map<std::string, const ips::Program*>& parts_;

    std::unique_ptr<Plan> plan_;
    std::map<std::string, const ips::Function*> registry_;
    std::map<std::string, bool> registryDrawLike_;
    std::set<std::string> preparing_;
    std::set<std::string> prepared_;

    [[noreturn]] void error(const ips::SourceLocation& at, std::string message);

    void planParts();
    void planFunctions();
    void registerNames();
    void prepareFunction(const std::string& name);

    // The plan's Typer resolver, bound to the heap plan (stable address).
    std::optional<ips::FunctionSurface> resolveFunction(
        const std::string& name, const ips::SourceLocation& at);

    const ips::InputDecl* ownerInput(const ips::Program* owner,
                                     const std::string& name) const;
    const ips::OutputDecl* ownerOutput(const ips::Program* owner,
                                       const std::string& name) const;
};

} // namespace iprop::compiler
