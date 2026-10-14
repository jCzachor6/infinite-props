#pragma once

#include "iprop/Format.hpp"
#include "iprop/Value.hpp"
#include <map>
#include <set>

namespace iprop {

inline constexpr u16 kMaxListElements = 256;
inline constexpr u64 kMaxGenerationData = 64u * 1024u * 1024u;
inline constexpr u8 kDescriptorReference = 0x80;

struct CollectionPool {
    std::vector<TypeDescriptor> types;
    std::vector<ListValue> lists;
    std::vector<SamplerValue> samplers;
    std::vector<StepperValue> steppers;
    std::vector<Distribution> distributions;
};

TypeDescriptor readTypeReference(Reader& reader, std::span<const TypeDescriptor> types);
void writeTypeReference(Writer& writer, TypeDescriptor type,
                        std::span<const TypeDescriptor> types);
std::vector<TypeDescriptor> parseTypeDescriptors(std::span<const u8> data, const Section& section);
void writeTypeDescriptors(Writer& writer, std::span<const TypeDescriptor> types);
std::vector<ListValue> parseCollectionPool(std::span<const u8> data, const Section& section,
                                         std::span<const TypeDescriptor> types,
                                         std::span<const SamplerValue> samplers = {},
                                         std::span<const Distribution> distributions = {},
                                         std::span<const StepperValue> steppers = {});
void writeListElements(Writer& writer, const ListData& list,
                       std::span<const SamplerValue> samplers = {},
                       std::span<const Distribution> distributions = {},
                       std::span<const StepperValue> steppers = {});
std::vector<SamplerValue> parseSamplerPool(std::span<const u8> data, const Section& section,
                                          std::span<const TypeDescriptor> types,
                                          std::span<const Distribution> distributions = {});
// Payload after the type descriptor: mode, reserved byte and typed data.
void writeSamplerData(Writer& writer, const SamplerData& sampler,
                      std::span<const Distribution> distributions = {});
std::vector<StepperValue> parseStepperPool(std::span<const u8> data, const Section& section,
                                          std::span<const TypeDescriptor> types,
                                          std::span<const Distribution> distributions = {});
void writeStepperData(Writer& writer, const StepperData& stepper,
                      std::span<const Distribution> distributions = {});
std::vector<Distribution> parseDistributionPool(std::span<const u8> data, const Section& section);
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
    u64 peak = 0;
    std::vector<std::shared_ptr<const ListData>> lists;
    std::vector<std::shared_ptr<const SamplerData>> samplers;
    std::vector<std::shared_ptr<const StepperData>> steppers;
    std::map<u16, ListValue> constants;
    std::set<const SamplerData*> samplerConstants;
    std::set<const StepperData*> stepperConstants;
    std::vector<std::shared_ptr<const PointDistributionData>> distributions;
    std::set<const PointDistributionData*> distributionConstants;
    std::vector<std::weak_ptr<const SurfaceBuffer>> surfaces;
    // The weak accounting token makes externally retained internal handles safe
    // even after an execution is cleared or destroyed.
    struct SurfaceLifetime { u64* bytes = nullptr; };
    std::shared_ptr<SurfaceLifetime> surfaceLifetime;
    std::shared_ptr<SurfaceBuffer> makeSurface(u64 chargedBytes);
    void clear();
    bool canAllocate(u64 size) const { return size <= kMaxGenerationData - bytes; }
    void charge(u64 size) { bytes += size; if (bytes > peak) peak = bytes; }
    void release(u64 size) { bytes -= size; }
    // Fixed accounting is identical on native and WASM, covering storage,
    // builder/control metadata and arena ownership with a conservative slot.
    static u64 listBytes(std::size_t count) { return 128u + count * 64u; }
    static constexpr u64 samplerBytes() { return 256u; }
    static constexpr u64 stepperBytes() { return 256u; }
    static u64 distributionBytes(std::size_t count) { return 128u + count * 32u; }
};

} // namespace iprop
