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
#include <unordered_map>

#include "iprop/Code.hpp"
#include "iprop/Instruction.hpp"
#include "ips/Ast.hpp"
#include "ips/Typing.hpp"
#include "ips/Palette.hpp"
#include "ips/DeclarationConstants.hpp"
#include "ips/AssetConstants.hpp"
#include "Pools.hpp"
#include "CallResolver.hpp"

namespace iprop::compiler {

struct PlannedExpression {
    ips::ValueType type;
    std::optional<Value> constant;
    const ips::ResolvedPalette* palette = nullptr; // indexed palette, if any
};

// One planned function: its signature, local layout and (after emission)
// its instruction stream. `slots`/`langTypes` map every name in scope —
// parameters first, then uniforms (for draw-like functions), then locals in
// first-assignment order — to local indices and language types.
struct PlannedFunction {
    std::string name;
    const ips::Function* ast = nullptr;
    const ips::Program* owner = nullptr;
    std::optional<u32> partOwner; // distinct even when aliases share one AST
    bool drawLike = false;

    std::vector<ValueType> params;  // runtime types
    std::vector<ValueType> results; // runtime types
    std::vector<ValueType> locals;  // extra locals beyond params
    std::optional<ips::ValueType> langResult;

    std::map<std::string, u32> slots;
    std::map<std::string, ips::ValueType> langTypes;
    std::unordered_map<const ips::Expr*, PlannedExpression> expressions;
    std::unordered_map<const ips::Call*, ResolvedCall> calls;
    ips::CallPlans boundCalls;

    const PlannedExpression& expression(const ips::Expr& expr) const {
        return expressions.at(&expr);
    }
    ips::ValueType typeOf(const ips::Expr& expr) const { return expression(expr).type; }

    std::vector<Instruction> code; // the emitted IR
    std::vector<u32> sourceLines;  // one .ips line per instruction (debug)
};

// One global output slot (spec §4.3: root outputs first, then part outputs
// in part order).
struct SlotInfo {
    ips::OutputKind kind;
    bool isList = false;
};

// One part instance of the flattened import tree. The same program may back
// several instances, each with its own functions, inputs and output slots.
struct PartInstance {
    std::string path;          // alias path from the entry, e.g. "hilt.grip"
    u32 parent = kRootParent;  // the drawing instance; kRootParent: the entry
    std::string source;        // resolved source path, for diagnostics
};

// The namespace of a planned function: its instance path plus '.', or ""
// for the entry asset's functions ("hilt.grip.draw" -> "hilt.grip.").
std::string functionNamespace(const std::string& functionName);

// The resolved plan of one compilation: the entry asset, the part instances
// in preorder of import order, the function table in emission order, the
// global slot table, the constant pools and the typed expressions.
struct Plan {
    const ips::Program* entry = nullptr;

    std::vector<const ips::Program*> parts;
    std::vector<PartInstance> instances; // parallel to parts
    std::map<std::string, u32> partIndex; // by alias path

    std::vector<SlotInfo> rootSlots;
    std::map<std::string, u32> rootSlotIndex;

    std::vector<PlannedFunction> functions;
    std::map<std::string, u32> functionIndex;

    // The emitter adds literal constants during emission; the assembler
    // writes the pools. Names were registered during planning.
    Pools pools;

    // Palette annotations are immutable and retain stable addresses in plan copies.
    std::map<const ips::PaletteDecl*, std::shared_ptr<const ips::ResolvedPalette>> palettes;
    std::map<const ips::InputDecl*, ips::ResolvedConstant> inputDefaults;
    std::map<const ips::Program*, ips::AssetConstants> assetConstants;
    std::optional<u32> functionId(const std::string& name,
                                  const std::string& ownerNamespace) const;
    std::string sourcePath(const ips::Program* owner) const;
    // The global slot of `output` in the part instance `part`.
    u32 partSlot(u32 part, const std::string& output) const;
    const ips::ResolvedPalette& palette(const ips::Program& owner, const ips::Expr& name) const;
};

// Resolves a checked entry program plus its imported parts, keyed by alias
// path ("hilt", "hilt.grip"), into a Plan. partSources names each part's file
// for diagnostics (else its import path, resolved lexically). Throws ips::SourceError with a located message
// for problems the semantic checker does not cover (the instance limit,
// i32 uniforms).
//
// Type inference belongs to planning; the returned plan owns its complete
// annotations and does not retain callbacks into the Planner or Typer.
class Planner {
public:
    Planner(const ips::Program& entry,
            const std::map<std::string, const ips::Program*>& parts,
            const std::map<std::string, std::string>& partSources = {});

    std::unique_ptr<Plan> plan();

private:
    const ips::Program& entry_;
    const std::map<std::string, const ips::Program*>& parts_;
    std::map<std::string, std::string> partSources_;

    std::unique_ptr<Plan> plan_;
    std::map<std::string, ips::PartInfo> surfaces_;
    std::unique_ptr<ips::Typer> typer_;
    std::map<std::string, const ips::Function*> registry_;
    std::map<std::string, bool> registryDrawLike_;
    std::set<std::string> preparing_;
    std::set<std::string> prepared_;

    [[noreturn]] void error(const ips::SourceLocation& at, std::string message);

    void planParts();
    void planFunctions();
    void registerNames();
    void prepareFunction(const std::string& name);
    void prepareFunctionBody(const std::string& name);

    // The planning-only Typer resolver prepares callees lazily.
    std::optional<ips::FunctionSurface> resolveFunction(
        const std::string& name, const ips::SourceLocation& at,
        const std::string& ns);

    const ips::InputDecl* ownerInput(const ips::Program* owner,
                                     const std::string& name) const;
    const ips::OutputDecl* ownerOutput(const ips::Program* owner,
                                       const std::string& name) const;
};

} // namespace iprop::compiler
