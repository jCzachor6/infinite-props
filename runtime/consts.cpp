#include "iprop/Consts.hpp"

#include "iprop/Binary.hpp"
#include "iprop/Utf8.hpp"

namespace iprop {

ConstPool parseConsts(std::span<const u8> data, const Section& section) {
    if (section.id != SectionId::Const) {
        throw FormatError("not a const section", section.offset, ErrorReason::InvalidConstants);
    }
    Reader reader = sectionReader(data, section);
    ConstPool pool;

    const u32 stringCount = reader.readU32();
    if (stringCount > kMaxConstPoolEntries) {
        throw FormatError("string pool too large", reader.position() - 4, ErrorReason::InvalidConstants);
    }
    pool.strings.reserve(stringCount);
    for (u32 i = 0; i < stringCount; ++i) {
        const u32 len = reader.readU32();
        if (len > reader.remaining()) {
            throw FormatError("string length exceeds section", reader.position() - 4, ErrorReason::InvalidConstants);
        }
        const auto bytes = reader.readBytes(len);
        if (!isValidUtf8(bytes)) {
            throw FormatError("string is not valid UTF-8", reader.position() - len, ErrorReason::InvalidConstants);
        }
        pool.strings.emplace_back(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    const u32 intCount = reader.readU32();
    if (intCount > kMaxConstPoolEntries) {
        throw FormatError("int pool too large", reader.position() - 4, ErrorReason::InvalidConstants);
    }
    pool.ints.reserve(intCount);
    for (u32 i = 0; i < intCount; ++i) {
        pool.ints.push_back(reader.readI32());
    }

    const u32 floatCount = reader.readU32();
    if (floatCount > kMaxConstPoolEntries) {
        throw FormatError("float pool too large", reader.position() - 4, ErrorReason::InvalidConstants);
    }
    pool.floats.reserve(floatCount);
    for (u32 i = 0; i < floatCount; ++i) {
        pool.floats.push_back(reader.readF32());
    }

    const u32 colorCount = reader.readU32();
    if (colorCount > kMaxConstPoolEntries) {
        throw FormatError("color pool too large", reader.position() - 4, ErrorReason::InvalidConstants);
    }
    pool.colors.reserve(colorCount);
    for (u32 i = 0; i < colorCount; ++i) {
        pool.colors.push_back(reader.readColor());
    }

    if (!reader.empty()) {
        throw FormatError("trailing bytes in const section", reader.position(), ErrorReason::InvalidConstants);
    }

    return pool;
}

} // namespace iprop
