#include "Compiler.hpp"

#include <optional>
#include <set>
#include <utility>

#include "iprop/Validator.hpp"
#include "CompilerDetail.hpp"

namespace iprop {
namespace detail {

Compiler::Compiler(const Program& entry,
                   const std::map<std::string, const Program*>& parts)
    : entry_(entry), parts_(parts), typer_(surfaces_) {
    // The Typer's part lookup resolves aliases against part surfaces; build
    // them from the provided programs.
    for (const auto& [alias, program] : parts) {
        ips::PartInfo info;
        for (const auto& input : program->asset.inputs) {
            info.inputs.push_back({input.type, input.name});
        }
        for (const auto& output : program->asset.outputs) {
            info.outputs.push_back({output.kind, output.isList, output.name});
        }
        surfaces_.emplace(alias, std::move(info));
    }

    typer_.resolveFunction = [this](const std::string& name,
                                    const SourceLocation&) {
        if (functionIndex_.count(name) == 0) {
            if (registry_.count(name) == 0) {
                return std::optional<ips::FunctionSurface>{};
            }
            prepareFunction(name); // on demand
        }
        const FunctionInfo& info = functions_[functionIndex_.at(name)];
        return std::optional<ips::FunctionSurface>(
            ips::FunctionSurface{info.ast->params.size(), info.langResult});
    };
}

// ---- preparation ------------------------------------------------------------

const InputDecl* Compiler::ownerInput(const Program* owner,
                                      const std::string& name) const {
    for (const auto& input : owner->asset.inputs) {
        if (input.name == name) {
            return &input;
        }
    }
    return nullptr;
}

const OutputDecl* Compiler::ownerOutput(const Program* owner,
                                        const std::string& name) const {
    for (const auto& output : owner->asset.outputs) {
        if (output.name == name) {
            return &output;
        }
    }
    return nullptr;
}

void Compiler::prepareAll() {
    for (const auto& output : entry_.asset.outputs) {
        rootSlots_.push_back({output.kind, output.isList});
        rootSlotIndex_.emplace(output.name,
                               static_cast<u32>(rootSlots_.size() - 1));
    }

    for (const auto& import : entry_.imports) {
        const auto found = parts_.find(import.alias);
        if (found == parts_.end()) {
            error(import.location,
                  "imported part '" + import.alias + "' was not provided");
        }
        const Program* part = found->second;
        if (!part->imports.empty()) {
            error(part->imports[0].location,
                  "nested part imports are not supported yet");
        }
        partIndex_.emplace(import.alias,
                           static_cast<u32>(orderedParts_.size()));
        orderedParts_.push_back(part);
    }

    // Plan the function table: draw first, then the entry's other functions
    // in declaration order, then each part's functions.
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
        const Program* part = parts_.at(import.alias);
        for (const auto& function : part->asset.functions) {
            const std::string name = import.alias + "." + function.name;
            planned.push_back(name);
            registry_.emplace(name, &function);
            registryDrawLike_.emplace(name, function.name == "draw");
        }
    }

    // Register names first (deterministic order): function names, slot
    // names, input names.
    for (const auto& name : planned) {
        pools_.addString(name);
    }
    for (const auto& output : entry_.asset.outputs) {
        pools_.addString(output.name);
    }
    for (const auto& import : entry_.imports) {
        for (const auto& output : parts_.at(import.alias)->asset.outputs) {
            pools_.addString(output.name);
        }
    }
    for (const auto& input : entry_.asset.inputs) {
        pools_.addString(input.name);
    }
    for (const auto& import : entry_.imports) {
        for (const auto& input : parts_.at(import.alias)->asset.inputs) {
            pools_.addString(input.name);
        }
    }

    for (const auto& name : planned) {
        prepareFunction(name);
    }
}

// Prepares one function: fills its params, discovers locals in
// first-assignment order and computes the return type. Called on demand, so
// calls between functions resolve naturally (the checker already rejected
// recursion).
void Compiler::prepareFunction(const std::string& name) {
    if (functionIndex_.count(name) != 0) {
        return;
    }
    if (preparing_.count(name) != 0) {
        error(registry_.at(name)->location,
              "recursive call to '" + name + "' is not supported");
    }
    // Reserve the table index up front: functions checked lazily during this
    // walk must come later in the function table.
    const u32 index = static_cast<u32>(functions_.size());
    functionIndex_.emplace(name, index);
    functions_.emplace_back(); // placeholder
    preparing_.insert(name);

    const Function* ast = registry_.at(name);
    const bool drawLike = registryDrawLike_.at(name);
    const Program* owner = &entry_;
    const auto dot = name.find('.');
    if (dot != std::string::npos) {
        owner = parts_.at(name.substr(0, dot));
    }

    FunctionInfo info;
    info.name = name;
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

    current_ = &info;
    ips::Typer::Scope savedScope = typer_.saveScope();
    typer_.enterFunction(*owner, *ast, drawLike);
    std::optional<ips::ValueType> returnType;

    const auto walk = [&](auto&& self, const std::vector<Stmt>& body) -> void {
        for (const auto& stmt : body) {
            if (const auto* decl = std::get_if<VarDecl>(&stmt)) {
                if (ownerOutput(owner, decl->name) != nullptr ||
                    ownerInput(owner, decl->name) != nullptr ||
                    info.slots.count(decl->name) != 0) {
                    continue; // output write, input or parameter
                }
                const ips::ValueType type = typer_.typeOf(*decl->value);
                info.slots.emplace(decl->name, slot);
                info.langTypes.emplace(decl->name, type);
                typer_.declareLocal(decl->name, type);
                info.locals.push_back(runtimeType(type));
                ++slot;
            } else if (const auto* ret = std::get_if<ReturnStmt>(&stmt)) {
                if (ret->value) {
                    const ips::ValueType type = typer_.typeOf(*ret->value);
                    if (returnType.has_value() && *returnType != type) {
                        error(ret->location,
                              "inconsistent return type in '" + name + "'");
                    }
                    returnType = type;
                }
            } else if (const auto* ifStmt = std::get_if<IfStmt>(&stmt)) {
                self(self, ifStmt->thenBranch);
                self(self, ifStmt->elseBranch);
            } else if (const auto* whileStmt = std::get_if<WhileStmt>(&stmt)) {
                self(self, whileStmt->body);
            }
        }
    };
    walk(walk, ast->body);

    typer_.restoreScope(std::move(savedScope));
    current_ = nullptr;

    if (returnType.has_value()) {
        info.langResult = returnType;
        info.results.push_back(runtimeType(*returnType));
    }
    if (name == "draw" && returnType.has_value()) {
        error(ast->location, "'draw' cannot return a value");
    }

    functions_[index] = std::move(info);
    preparing_.erase(name);
}

// ---- meta / sections --------------------------------------------------------

f32 Compiler::foldOrigin(const Expr& expr) {
    Value value;
    if (!tryFold(expr, value)) {
        error(locationOf(expr), "origin must be a constant expression");
    }
    if (const auto* i = std::get_if<i32>(&value)) {
        return static_cast<f32>(*i);
    }
    if (const auto* f = std::get_if<f32>(&value)) {
        return *f;
    }
    error(locationOf(expr), "origin must be a numeric constant");
}

void Compiler::writeInputEntry(Writer& out, const InputDecl& input) {
    switch (input.type) {
    case ips::InputType::Color: {
        out.writeU8(static_cast<u8>(iprop::InputType::Color));
        out.writeU32(pools_.stringIndex.at(input.name));
        const auto* lit = std::get_if<Literal>(input.defaultValue.get());
        Color color;
        parseColor(lit->text, color);
        out.writeColor(color);
        break;
    }
    case ips::InputType::F32: {
        out.writeU8(static_cast<u8>(iprop::InputType::F32));
        out.writeU32(pools_.stringIndex.at(input.name));
        const auto* lit = std::get_if<Literal>(input.defaultValue.get());
        u32 bits = 0;
        parseFloatBits(lit->text, bits);
        out.writeU32(bits);
        break;
    }
    case ips::InputType::U32:
    case ips::InputType::I32: {
        out.writeU8(static_cast<u8>(iprop::InputType::U32));
        out.writeU32(pools_.stringIndex.at(input.name));
        const auto* lit = std::get_if<Literal>(input.defaultValue.get());
        u32 bits = 0;
        parseIntBits(lit->text, bits);
        out.writeU32(bits);
        break;
    }
    }
}

std::vector<u8> Compiler::assemble() {
    Writer meta;
    meta.writeU32(*entry_.asset.sizeX);
    meta.writeU32(*entry_.asset.sizeY);
    meta.writeF32(entry_.asset.originX ? foldOrigin(*entry_.asset.originX) : 0.f);
    meta.writeF32(entry_.asset.originY ? foldOrigin(*entry_.asset.originY) : 0.f);
    meta.writeU32(entry_.asset.variants.value_or(1));
    meta.writeU32(entry_.asset.archetypes.value_or(1));
    meta.writeU64(1000000); // fuel
    meta.writeU16(0);       // emission: no offset/radius/colour
    for (int i = 0; i < 6; ++i) {
        meta.writeU8(0);
    }
    meta.writeU16(static_cast<u16>(entry_.asset.inputs.size()));
    for (const auto& input : entry_.asset.inputs) {
        writeInputEntry(meta, input);
    }
    meta.writeU16(static_cast<u16>(entry_.asset.outputs.size()));
    for (const auto& output : entry_.asset.outputs) {
        meta.writeU8(static_cast<u8>(output.kind));
        meta.writeU8(output.isList ? 1 : 0);
        meta.writeU8(0);
        meta.writeU8(0);
        meta.writeU32(pools_.stringIndex.at(output.name));
    }

    Writer consts;
    consts.writeU32(static_cast<u32>(pools_.strings.size()));
    for (const auto& text : pools_.strings) {
        consts.writeU32(static_cast<u32>(text.size()));
        consts.writeBytes({reinterpret_cast<const u8*>(text.data()),
                           text.size()});
    }
    consts.writeU32(static_cast<u32>(pools_.ints.size()));
    for (const u32 bits : pools_.ints) {
        consts.writeU32(bits);
    }
    consts.writeU32(static_cast<u32>(pools_.floats.size()));
    for (const u32 bits : pools_.floats) {
        consts.writeU32(bits);
    }
    consts.writeU32(static_cast<u32>(pools_.colors.size()));
    for (const Color& color : pools_.colors) {
        consts.writeColor(color);
    }

    Writer table;
    Writer streams;
    // The table and the instruction streams are written interleaved: each
    // function's offset is the running size of the streams writer.
    for (const auto& info : functions_) {
        table.writeU32(pools_.stringIndex.at(info.name));
        table.writeU32(static_cast<u32>(streams.size()));
        table.writeU32(static_cast<u32>(info.code.size()));
        table.writeU16(static_cast<u16>(info.params.size()));
        for (const ValueType type : info.params) {
            table.writeU8(static_cast<u8>(type));
        }
        table.writeU16(static_cast<u16>(info.results.size()));
        for (const ValueType type : info.results) {
            table.writeU8(static_cast<u8>(type));
        }
        table.writeU16(static_cast<u16>(info.locals.size()));
        for (const ValueType type : info.locals) {
            table.writeU8(static_cast<u8>(type));
        }
        streams.writeBytes(info.code);
    }

    Writer code;
    code.writeU32(static_cast<u32>(functions_.size()));
    code.writeBytes(table.data());
    code.writeBytes(streams.data());
    code.writeU32(static_cast<u32>(orderedParts_.size()));
    for (std::size_t i = 0; i < orderedParts_.size(); ++i) {
        const Program* part = orderedParts_[i];
        code.writeU32(functionIndex_.at(entry_.imports[i].alias + ".draw"));
        code.writeU32(*part->asset.sizeX);
        code.writeU32(*part->asset.sizeY);
        code.writeF32(part->asset.originX ? foldOrigin(*part->asset.originX) : 0.f);
        code.writeF32(part->asset.originY ? foldOrigin(*part->asset.originY) : 0.f);
        code.writeU16(static_cast<u16>(part->asset.inputs.size()));
        for (const auto& input : part->asset.inputs) {
            writeInputEntry(code, input);
        }
        code.writeU16(static_cast<u16>(part->asset.outputs.size()));
        for (const auto& output : part->asset.outputs) {
            code.writeU8(static_cast<u8>(output.kind));
            code.writeU8(output.isList ? 1 : 0);
            code.writeU8(0);
            code.writeU8(0);
            code.writeU32(pools_.stringIndex.at(output.name));
        }
    }

    Writer file;
    for (const u8 magic : kMagic) {
        file.writeU8(magic);
    }
    file.writeU16(kVersionMajor);
    file.writeU16(kVersionMinor);
    file.writeU32(0); // flags
    file.writeU32(3); // section count
    file.writeU32(0); // reserved
    u32 offset = kHeaderSize + 3 * 12;
    const auto section = [&](u32 id, const std::vector<u8>& bytes) {
        file.writeU32(id);
        file.writeU32(offset);
        file.writeU32(static_cast<u32>(bytes.size()));
        offset += static_cast<u32>(bytes.size());
    };
    section(static_cast<u32>(SectionId::Meta), meta.data());
    section(static_cast<u32>(SectionId::Const), consts.data());
    section(static_cast<u32>(SectionId::Code), code.data());
    file.writeBytes(meta.data());
    file.writeBytes(consts.data());
    file.writeBytes(code.data());
    return file.data();
}

} // namespace detail

std::vector<u8> compileProgram(
    const ips::Program& entry,
    const std::map<std::string, const ips::Program*>& parts) {
    detail::Compiler compiler(entry, parts);
    const std::vector<u8> bytes = compiler.run();

    // Dogfood: the compiled file must pass the runtime validator.
    try {
        const auto loaded = loadFile(bytes);
        validate(loaded);
    } catch (const FormatError& error) {
        throw ips::SourceError{{1, 1},
                               std::string("compiled program failed validation: ") +
                                   error.what()};
    }
    return bytes;
}

} // namespace iprop
