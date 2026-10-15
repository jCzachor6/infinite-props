#pragma once

#include "iprop/Format.hpp"
#include "iprop/Value.hpp"
#include <map>
#include <set>

namespace iprop {

inline constexpr u16 kMaxListElements = 256;
inline constexpr u64 kMaxGenerationData = 64u * 1024u * 1024u;
inline constexpr u8 kDescriptorReference = 0x80;

// Every collection, description and distribution pool shares one 64 MiB
// accounted-size limit (spec §34). Each parser charges entries as it reads
// them, so an oversized file fails before the remaining pools are built.
struct PoolBudget {
    u64 bytes = 0;
    void charge(u64 size, std::size_t at);
};

struct CollectionPool {
    std::vector<TypeDescriptor> types;
    std::vector<ListValue> lists;
    std::vector<SamplerValue> samplers;
    std::vector<StepperValue> steppers;
    std::vector<Distribution> distributions;
};

TypeDescriptor readTypeReference(Reader& reader, std::span<const TypeDescriptor> types);
// The index of a parameterized type in a descriptor table; a writer that
// lost the descriptor has a bug, so a missing type throws std::logic_error.
u16 typeDescriptorIndex(TypeDescriptor type, std::span<const TypeDescriptor> types);
void writeTypeReference(Writer& writer, TypeDescriptor type,
                        std::span<const TypeDescriptor> types);
std::vector<TypeDescriptor> parseTypeDescriptors(std::span<const u8> data, const Section& section);
void writeTypeDescriptors(Writer& writer, std::span<const TypeDescriptor> types);
std::vector<ListValue> parseCollectionPool(std::span<const u8> data, const Section& section,
                                         const CollectionPool& pools, PoolBudget& budget);
void writeListElements(Writer& writer, const ListData& list,
                       std::span<const SamplerValue> samplers = {},
                       std::span<const Distribution> distributions = {},
                       std::span<const StepperValue> steppers = {});
std::vector<SamplerValue> parseSamplerPool(std::span<const u8> data, const Section& section,
                                          const CollectionPool& pools, PoolBudget& budget);
std::vector<StepperValue> parseStepperPool(std::span<const u8> data, const Section& section,
                                          const CollectionPool& pools, PoolBudget& budget);
// A sampler or stepper pool entry after its type descriptor: mode, reserved
// byte and typed data.
void writeDescriptionData(Writer& writer, const CapturedRangeData& description,
                          std::span<const Distribution> distributions = {});
std::vector<Distribution> parseDistributionPool(std::span<const u8> data, const Section& section,
                                                PoolBudget& budget);
void writePointDistribution(Writer& writer, const PointDistributionData& data);
Distribution readDistributionData(Reader& reader, std::span<const Distribution> distributions = {});
void writeDistributionData(Writer& writer, const Distribution& distribution,
                           std::span<const Distribution> distributions = {});

// One generation owns immutable buffers and charges each allocation once.
// A handle copy, helper argument or return shares the original allocation.
struct ResourceArena {
    ResourceArena() = default;
    ResourceArena(const ResourceArena&) = delete;
    ResourceArena& operator=(const ResourceArena&) = delete;
    ~ResourceArena();
    u64 bytes = 0;
    std::vector<std::shared_ptr<const ListData>> lists;
    // Captured samplers and steppers.
    std::vector<std::shared_ptr<const CapturedRangeData>> descriptions;
    std::map<u16, ListValue> constants;
    // Pooled samplers and steppers already charged in this generation.
    std::set<const CapturedRangeData*> descriptionConstants;
    std::vector<std::shared_ptr<const PointDistributionData>> distributions;
    std::set<const PointDistributionData*> distributionConstants;
    // The weak accounting token makes externally retained internal handles safe
    // even after an execution is cleared or destroyed.
    struct SurfaceLifetime { u64* bytes = nullptr; };
    std::shared_ptr<SurfaceLifetime> surfaceLifetime;
    std::shared_ptr<SurfaceBuffer> makeSurface(u64 chargedBytes);
    void clear();
    bool canAllocate(u64 size) const { return size <= kMaxGenerationData - bytes; }
    void charge(u64 size) { bytes += size; }
    void release(u64 size) { bytes -= size; }
    // Fixed accounting is identical on native and WASM, covering storage,
    // builder/control metadata and arena ownership with a conservative slot.
    static u64 listBytes(std::size_t count) { return 128u + count * 64u; }
    static constexpr u64 descriptionBytes() { return 256u; }
    static u64 distributionBytes(std::size_t count) { return 128u + count * 32u; }
};

} // namespace iprop
