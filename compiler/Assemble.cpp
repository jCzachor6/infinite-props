#include "Assemble.hpp"

#include <algorithm>
#include <utility>

#include "iprop/Binary.hpp"
#include "iprop/Constants.hpp"
#include "iprop/OpcodeSpec.hpp"
#include "ips/Fold.hpp"

namespace iprop::compiler {

Assembler::Assembler(const Plan& plan) : plan_(plan) {}

[[noreturn]] void Assembler::error(const ips::SourceLocation& at,
                                   std::string message) {
    throw ips::SourceError{at, std::move(message), sourcePath_};
}

f32 Assembler::foldOrigin(const ips::Expr& expr) {
    Value value;
    if (!ips::foldExpression(expr, value)) {
        error(ips::locationOf(expr), "origin must be a constant expression");
    }
    if (const auto* i = std::get_if<i32>(&value)) {
        return static_cast<f32>(*i);
    }
    if (const auto* f = std::get_if<f32>(&value)) {
        return *f;
    }
    error(ips::locationOf(expr), "origin must be a numeric constant");
}

void Assembler::writeInputEntry(Writer& out, const ips::InputDecl& input) {
    const Value& value = plan_.inputDefaults.at(&input).value;
    switch (input.type) {
    case ips::InputType::Color: {
        out.writeU8(static_cast<u8>(iprop::InputType::Color));
        out.writeU32(plan_.pools.indexOf(input.name));
        out.writeColor(std::get<Color>(value));
        break;
    }
    case ips::InputType::F32: {
        out.writeU8(static_cast<u8>(iprop::InputType::F32));
        out.writeU32(plan_.pools.indexOf(input.name));
        out.writeF32(std::get<f32>(value));
        break;
    }
    case ips::InputType::U32:
    case ips::InputType::I32: {
        out.writeU8(static_cast<u8>(iprop::InputType::U32));
        out.writeU32(plan_.pools.indexOf(input.name));
        out.writeU32(static_cast<u32>(std::get<i32>(value)));
        break;
    }
    }
}

std::vector<u8> Assembler::assemble() {
    const ips::Asset& asset = plan_.entry->asset;

    // ---- meta section (spec §4) ----
    Writer meta;
    meta.writeU32(*asset.sizeX);
    meta.writeU32(*asset.sizeY);
    meta.writeF32(asset.originX ? foldOrigin(*asset.originX) : 0.f);
    meta.writeF32(asset.originY ? foldOrigin(*asset.originY) : 0.f);
    meta.writeU32(asset.variants.value_or(1));
    meta.writeU32(asset.archetypes.value_or(1));
    meta.writeU64(2000000); // fuel
    meta.writeU16(0);       // emission: no offset/radius/colour
    for (int i = 0; i < 6; ++i) {
        meta.writeU8(0);
    }
    meta.writeU16(static_cast<u16>(asset.inputs.size()));
    for (const auto& input : asset.inputs) {
        writeInputEntry(meta, input);
    }
    meta.writeU16(static_cast<u16>(plan_.rootSlots.size()));
    for (const auto& output : asset.outputs) {
        if (ips::isResourceOutput(output.kind)) continue;
        meta.writeU8(static_cast<u8>(output.kind));
        meta.writeU8(output.isList ? 1 : 0);
        meta.writeU8(0);
        meta.writeU8(0);
        meta.writeU32(plan_.pools.indexOf(output.name));
    }

    // ---- const section (spec §5) ----
    Writer consts;
    consts.writeU32(static_cast<u32>(plan_.pools.stringsValue().size()));
    for (const auto& text : plan_.pools.stringsValue()) {
        consts.writeU32(static_cast<u32>(text.size()));
        consts.writeBytes({reinterpret_cast<const u8*>(text.data()),
                           text.size()});
    }
    consts.writeU32(static_cast<u32>(plan_.pools.intsValue().size()));
    for (const u32 bits : plan_.pools.intsValue()) {
        consts.writeU32(bits);
    }
    consts.writeU32(static_cast<u32>(plan_.pools.floatsValue().size()));
    for (const u32 bits : plan_.pools.floatsValue()) {
        consts.writeU32(bits);
    }
    consts.writeU32(static_cast<u32>(plan_.pools.colorsValue().size()));
    for (const Color& color : plan_.pools.colorsValue()) {
        consts.writeColor(color);
    }

    Writer types;
    Writer collections;
    Writer samplers;
    Writer distributions;
    if (!plan_.pools.distributionsValue().empty()) {
        distributions.writeU32(static_cast<u32>(plan_.pools.distributionsValue().size()));
        for (const auto& d : plan_.pools.distributionsValue()) writePointDistribution(distributions, *d.points);
    }
    if (!plan_.pools.typesValue().empty()) {
        writeTypeDescriptors(types, plan_.pools.typesValue());
    }
    if (!plan_.pools.listsValue().empty()) {
        collections.writeU32(static_cast<u32>(plan_.pools.listsValue().size()));
        for (const auto& list : plan_.pools.listsValue()) {
            const auto& descriptors = plan_.pools.typesValue();
            const auto found = std::find(descriptors.begin(), descriptors.end(), list.type);
            collections.writeU16(static_cast<u16>(found - descriptors.begin()));
            collections.writeU16(static_cast<u16>(list.data->elements.size()));
            writeListElements(collections, *list.data, plan_.pools.samplersValue(), plan_.pools.distributionsValue());
        }
    }
    if (!plan_.pools.samplersValue().empty()) {
        samplers.writeU32(static_cast<u32>(plan_.pools.samplersValue().size()));
        for (const auto& sampler : plan_.pools.samplersValue()) {
            const auto& descriptors = plan_.pools.typesValue();
            const auto found = std::find(descriptors.begin(), descriptors.end(), sampler.type);
            samplers.writeU16(static_cast<u16>(found - descriptors.begin()));
            writeSamplerData(samplers, *sampler.data, plan_.pools.distributionsValue());
        }
    }

    // ---- code section (spec §6) ----
    Writer table;
    Writer streams;
    u16 minor = kOldestWrittenMinor;
    // The table and the instruction streams are written interleaved: each
    // function's offset is the running size of the streams writer.
    for (const auto& function : plan_.functions) {
        table.writeU32(plan_.pools.indexOf(function.name));
        table.writeU32(static_cast<u32>(streams.size()));
        table.writeU32(static_cast<u32>(function.code.size() * 4));
        table.writeU16(static_cast<u16>(function.params.size()));
        for (const ValueType type : function.params) {
            minor = writtenMinor(minor, type);
            writeTypeReference(table, type, plan_.pools.typesValue());
        }
        table.writeU16(static_cast<u16>(function.results.size()));
        for (const ValueType type : function.results) {
            minor = writtenMinor(minor, type);
            writeTypeReference(table, type, plan_.pools.typesValue());
        }
        table.writeU16(static_cast<u16>(function.locals.size()));
        for (const ValueType type : function.locals) {
            minor = writtenMinor(minor, type);
            writeTypeReference(table, type, plan_.pools.typesValue());
        }
        for (const Instruction& ins : function.code) {
            minor = writtenMinor(minor, ins.opcode);
            streams.writeU8(static_cast<u8>(ins.opcode));
            streams.writeU8(ins.aux);
            streams.writeU16(ins.operand);
        }
    }

    Writer code;
    code.writeU32(static_cast<u32>(plan_.functions.size()));
    code.writeBytes(table.data());
    code.writeBytes(streams.data());
    code.writeU32(static_cast<u32>(plan_.parts.size()));
    for (std::size_t i = 0; i < plan_.parts.size(); ++i) {
        const ips::Program* part = plan_.parts[i];
        sourcePath_ = plan_.sourcePath(part);
        code.writeU32(plan_.functionIndex.at(plan_.instances[i].path + ".draw"));
        code.writeU32(*part->asset.sizeX);
        code.writeU32(*part->asset.sizeY);
        code.writeF32(part->asset.originX ? foldOrigin(*part->asset.originX) : 0.f);
        code.writeF32(part->asset.originY ? foldOrigin(*part->asset.originY) : 0.f);
        code.writeU16(static_cast<u16>(part->asset.inputs.size()));
        for (const auto& input : part->asset.inputs) {
            writeInputEntry(code, input);
        }
        const auto geometryCount = std::count_if(part->asset.outputs.begin(), part->asset.outputs.end(),
            [](const auto& output) { return !ips::isResourceOutput(output.kind); });
        code.writeU16(static_cast<u16>(geometryCount));
        for (const auto& output : part->asset.outputs) {
            if (ips::isResourceOutput(output.kind)) continue;
            code.writeU8(static_cast<u8>(output.kind));
            code.writeU8(output.isList ? 1 : 0);
            code.writeU8(0);
            code.writeU8(0);
            code.writeU32(plan_.pools.indexOf(output.name));
        }
    }

    // ---- parts section (spec v0.5 §6): instance alias paths and parents.
    //      Every compiled part has one, so outputs read by a parent are in
    //      its coordinates and the host receives root coordinates. ----
    Writer instances;
    if (!plan_.parts.empty()) {
        minor = std::max<u16>(minor, 5);
        instances.writeU32(static_cast<u32>(plan_.instances.size()));
        for (const auto& instance : plan_.instances) {
            instances.writeU32(plan_.pools.indexOf(instance.path));
            instances.writeU32(instance.parent);
        }
    }

    // ---- debug section (spec §7): local names + the .ips line per
    //      instruction ----
    Writer debug;
    debug.writeU32(static_cast<u32>(plan_.functions.size()));
    for (const auto& function : plan_.functions) {
        std::vector<const std::string*> names(
            function.params.size() + function.locals.size(), nullptr);
        for (const auto& [name, slot] : function.slots) {
            names.at(slot) = &name;
        }
        debug.writeU32(static_cast<u32>(names.size()));
        for (const auto* name : names) {
            debug.writeU32(name != nullptr ? plan_.pools.indexOf(*name)
                                           : kAnonymousName);
        }
        debug.writeU32(static_cast<u32>(function.code.size()));
        for (const u32 line : function.sourceLines) {
            debug.writeU32(line);
        }
    }

    Writer exports;
    if (!plan_.resourceExports.empty()) {
        exports.writeU32(static_cast<u32>(plan_.resourceExports.size()));
        for (const auto& output : plan_.resourceExports) {
            exports.writeU32(output.owner);
            exports.writeU32(plan_.pools.indexOf(output.name));
            const auto& descriptors = plan_.pools.typesValue();
            const auto type = std::find(descriptors.begin(), descriptors.end(), output.type);
            exports.writeU16(static_cast<u16>(type - descriptors.begin()));
            exports.writeU16(0);
        }
        exports.writeU32(static_cast<u32>(plan_.functions.size()));
        for (const auto& function : plan_.functions)
            exports.writeU32(function.partOwner.value_or(kRootParent));
    }
    if (types.size() || collections.size() || samplers.size() || distributions.size() || exports.size()) minor = 6;
    const u32 sectionCount = 4 + (plan_.parts.empty() ? 0u : 1u) + (types.size() ? 1u : 0u) +
        (collections.size() ? 1u : 0u) + (samplers.size() ? 1u : 0u) + (distributions.size() ? 1u : 0u) + (exports.size() ? 1u : 0u);
    // ---- file (spec §2) ----
    Writer file;
    for (const u8 magic : kMagic) {
        file.writeU8(magic);
    }
    file.writeU16(kVersionMajor);
    file.writeU16(minor);
    file.writeU32(kFlagDebug); // flags: debug section present
    file.writeU32(sectionCount); // section count
    file.writeU32(0);          // reserved
    u32 offset = kHeaderSize + sectionCount * 12;
    const auto section = [&](u32 id, const std::vector<u8>& bytes) {
        file.writeU32(id);
        file.writeU32(offset);
        file.writeU32(static_cast<u32>(bytes.size()));
        offset += static_cast<u32>(bytes.size());
    };
    section(static_cast<u32>(SectionId::Meta), meta.data());
    section(static_cast<u32>(SectionId::Const), consts.data());
    section(static_cast<u32>(SectionId::Code), code.data());
    if (!plan_.parts.empty()) section(static_cast<u32>(SectionId::Parts), instances.data());
    if (types.size()) section(static_cast<u32>(SectionId::Types), types.data());
    if (collections.size()) section(static_cast<u32>(SectionId::Collections), collections.data());
    if (samplers.size()) section(static_cast<u32>(SectionId::Samplers), samplers.data());
    if (distributions.size()) section(static_cast<u32>(SectionId::Distributions), distributions.data());
    if (exports.size()) section(static_cast<u32>(SectionId::ResourceExports), exports.data());
    section(static_cast<u32>(SectionId::Debug), debug.data());
    file.writeBytes(meta.data());
    file.writeBytes(consts.data());
    file.writeBytes(code.data());
    file.writeBytes(instances.data());
    file.writeBytes(types.data());
    file.writeBytes(collections.data());
    file.writeBytes(samplers.data());
    file.writeBytes(distributions.data());
    file.writeBytes(exports.data());
    file.writeBytes(debug.data());
    return file.data();
}

} // namespace iprop::compiler
