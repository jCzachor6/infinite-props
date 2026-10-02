#include "iprop/Code.hpp"

#include "iprop/Binary.hpp"

#include <set>
#include <string>

namespace iprop {
namespace {

// Reads one §6.1 type byte; throws a located error for unknown codes.
ValueType readValueType(Reader& reader, const char* what) {
    const u8 raw = reader.readU8();
    if (raw > 3) {
        throw FormatError(std::string("unknown ") + what + " type", reader.position() - 1);
    }
    return static_cast<ValueType>(raw);
}

} // namespace

CodeSection parseCode(std::span<const u8> data, const Section& section, const ConstPool& pool) {
    if (section.id != SectionId::Code) {
        throw FormatError("not a code section", section.offset);
    }
    Reader reader = sectionReader(data, section);
    CodeSection code;

    const u32 functionCount = reader.readU32();
    if (functionCount < 1 || functionCount > kMaxFunctions) {
        throw FormatError("function count out of range", reader.position() - 4);
    }

    // Function table (spec §6.1). Offsets and lengths are validated after the
    // table is read so errors can point at the exact entry.
    std::vector<std::size_t> entryStarts;
    std::vector<u64> streamOffsets;
    std::vector<u64> streamLengths;
    code.functions.reserve(functionCount);
    for (u32 i = 0; i < functionCount; ++i) {
        entryStarts.push_back(reader.position());

        const u32 nameIndex = reader.readU32();
        if (nameIndex != kAnonymousName && nameIndex >= pool.strings.size()) {
            throw FormatError("function name outside const string pool", reader.position() - 4);
        }
        streamOffsets.push_back(reader.readU32());
        streamLengths.push_back(reader.readU32());

        Function function;
        function.nameIndex = nameIndex;

        const u16 paramCount = reader.readU16();
        function.params.reserve(paramCount);
        for (u16 p = 0; p < paramCount; ++p) {
            function.params.push_back(readValueType(reader, "parameter"));
        }
        const u16 resultCount = reader.readU16();
        function.results.reserve(resultCount);
        for (u16 p = 0; p < resultCount; ++p) {
            function.results.push_back(readValueType(reader, "result"));
        }
        const u16 localCount = reader.readU16();
        function.locals.reserve(localCount);
        for (u16 p = 0; p < localCount; ++p) {
            function.locals.push_back(readValueType(reader, "local"));
        }
        if (function.params.size() + function.locals.size() > kMaxLocals) {
            throw FormatError("too many locals", entryStarts.back());
        }
        code.functions.push_back(std::move(function));
    }

    // Instruction streams (spec §6.2): contiguous, 4-byte aligned, starting
    // at the end of the function table.
    const std::size_t streamBase = reader.position();
    for (std::size_t i = 0; i < code.functions.size(); ++i) {
        if (streamLengths[i] % 4 != 0) {
            throw FormatError("instruction stream not 4-byte aligned", entryStarts[i]);
        }
        const u64 expected = i == 0 ? 0 : streamOffsets[i - 1] + streamLengths[i - 1];
        if (streamOffsets[i] != expected) {
            throw FormatError("instruction streams must be contiguous", entryStarts[i]);
        }
    }
    const u64 regionEnd =
        code.functions.empty() ? 0 : streamOffsets.back() + streamLengths.back();
    if (streamBase + regionEnd > section.offset + static_cast<std::size_t>(section.size)) {
        throw FormatError("instruction streams exceed section",
                          section.offset + section.size);
    }
    for (std::size_t i = 0; i < code.functions.size(); ++i) {
        code.functions[i].code =
            data.subspan(streamBase + static_cast<std::size_t>(streamOffsets[i]),
                         static_cast<std::size_t>(streamLengths[i]));
        code.functions[i].fileOffset =
            streamBase + static_cast<std::size_t>(streamOffsets[i]);
    }
    reader.skip(static_cast<std::size_t>(regionEnd));

    // Part table (spec §6.3).
    const u32 partCount = reader.readU32();
    if (partCount > kMaxParts) {
        throw FormatError("part count out of range", reader.position() - 4);
    }
    code.parts.reserve(partCount);
    for (u32 p = 0; p < partCount; ++p) {
        Part part;
        part.functionIndex = reader.readU32();
        if (part.functionIndex >= functionCount) {
            throw FormatError("part function index out of range", reader.position() - 4);
        }
        part.sizeX = reader.readU32();
        part.sizeY = reader.readU32();
        if (part.sizeX == 0 || part.sizeY == 0) {
            throw FormatError("part canvas size must be positive", reader.position() - 4);
        }
        part.origin = {reader.readF32(), reader.readF32()};

        const u16 inputCount = reader.readU16();
        part.inputs.reserve(inputCount);
        std::set<std::string> inputNames;
        for (u16 i = 0; i < inputCount; ++i) {
            const std::size_t entryStart = reader.position();
            InputDecl decl = readInputEntry(reader, pool);
            if (!inputNames.insert(pool.strings[decl.nameIndex]).second) {
                throw FormatError("duplicate part input name", entryStart);
            }
            part.inputs.push_back(decl);
        }

        const u16 outputCount = reader.readU16();
        part.outputs.reserve(outputCount);
        std::set<std::string> outputNames;
        for (u16 i = 0; i < outputCount; ++i) {
            const std::size_t entryStart = reader.position();
            OutputDecl decl = readOutputEntry(reader, pool);
            if (!outputNames.insert(pool.strings[decl.nameIndex]).second) {
                throw FormatError("duplicate part output name", entryStart);
            }
            part.outputs.push_back(decl);
        }
        code.parts.push_back(std::move(part));
    }

    if (!reader.empty()) {
        throw FormatError("trailing bytes in code section", reader.position());
    }

    return code;
}

} // namespace iprop
