#pragma once

#include "iprop/Format.hpp"
#include "iprop/Value.hpp"
#include <map>

namespace iprop {

inline constexpr u16 kMaxListElements = 256;
inline constexpr u64 kMaxGenerationData = 64u * 1024u * 1024u;
inline constexpr u8 kDescriptorReference = 0x80;

struct CollectionPool {
    std::vector<TypeDescriptor> types;
    std::vector<ListValue> lists;
};

TypeDescriptor readTypeReference(Reader& reader, std::span<const TypeDescriptor> types);
void writeTypeReference(Writer& writer, TypeDescriptor type,
                        std::span<const TypeDescriptor> types);
std::vector<TypeDescriptor> parseTypeDescriptors(std::span<const u8> data, const Section& section);
std::vector<ListValue> parseCollectionPool(std::span<const u8> data, const Section& section,
                                         std::span<const TypeDescriptor> types);
void writeListElements(Writer& writer, const ListData& list);

// One generation owns immutable buffers and charges each allocation once.
// A handle copy, helper argument or return shares the original allocation.
struct ResourceArena {
    u64 bytes = 0;
    u64 peak = 0;
    std::vector<std::shared_ptr<const ListData>> lists;
    std::map<u16, ListValue> constants;
    bool canAllocate(u64 size) const { return size <= kMaxGenerationData - bytes; }
    void charge(u64 size) { bytes += size; if (bytes > peak) peak = bytes; }
    void release(u64 size) { bytes -= size; }
    // Fixed accounting is identical on native and WASM, covering storage,
    // builder/control metadata and arena ownership with a conservative slot.
    static u64 listBytes(std::size_t count) { return 128u + count * 64u; }
};

} // namespace iprop
