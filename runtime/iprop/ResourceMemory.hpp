#pragma once

// Internal, capacity-based ownership for new geometry and its scratch buffers.
// Fixed slots make quota boundaries identical on native and WASM. The legacy
// collection/surface tariffs and their ownership are unchanged.
#include "iprop/Collections.hpp"

#include <limits>
#include <algorithm>
#include <iterator>
#include <utility>

namespace iprop::detail {

using ResourceLifetime = ResourceArena::SurfaceLifetime;

inline void reserveMemory(const std::shared_ptr<ResourceLifetime>& lifetime, u64 bytes) {
    if (!lifetime || !lifetime->bytes)
        throw Trap("resource arena is closed", ErrorReason::RuntimeViolation, ErrorStage::Execute);
    auto& used = *lifetime->bytes;
    if (used > kMaxGenerationData || bytes > kMaxGenerationData - used)
        throw Trap("generation data exceeds 64 MiB", ErrorReason::OutOfMemory, ErrorStage::Execute, lifetime->location);
    used += bytes;
}
inline void releaseMemory(const std::shared_ptr<ResourceLifetime>& lifetime, u64 bytes) noexcept {
    if (lifetime && lifetime->bytes) *lifetime->bytes -= bytes;
}

// For bounded automatic workspaces or an external API whose allocation size
// is known up front. The reservation must enclose the actual workspace's life.
class ResourceReservation {
public:
    ResourceReservation(ResourceArena& arena, u64 bytes) : lifetime_(arena.resourceLifetime()), bytes_(bytes) {
        reserveMemory(lifetime_, bytes_);
    }
    ResourceReservation(const ResourceReservation&) = delete;
    ResourceReservation& operator=(const ResourceReservation&) = delete;
    ~ResourceReservation() { releaseMemory(lifetime_, bytes_); }
private:
    std::shared_ptr<ResourceLifetime> lifetime_;
    u64 bytes_;
};

// Allocator traits preserve the arena on copies and moves. Reallocation
// charges both live capacities until the old allocation has been released.
template<class T, u64 SlotBytes>
struct ResourceAllocator {
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    template<class U> struct rebind {
        using other = ResourceAllocator<U, (sizeof(U) > SlotBytes ? sizeof(U) : SlotBytes)>;
    };
    std::shared_ptr<ResourceLifetime> lifetime;

    ResourceAllocator() = default;
    explicit ResourceAllocator(ResourceArena& arena) : lifetime(arena.resourceLifetime()) {}
    template<class U, u64 OtherSlot>
    ResourceAllocator(const ResourceAllocator<U, OtherSlot>& other) : lifetime(other.lifetime) {}
    T* allocate(std::size_t count) {
        static_assert(sizeof(T) <= SlotBytes);
        static_assert(alignof(T) <= alignof(std::max_align_t));
        if (count > (std::numeric_limits<u64>::max() - 32u) / SlotBytes ||
            count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw Trap("geometry allocation size overflow", ErrorReason::OutOfMemory, ErrorStage::Execute);
        const auto bytes = 32u + static_cast<u64>(count) * SlotBytes;
        reserveMemory(lifetime, bytes);
        try { return static_cast<T*>(::operator new(count * sizeof(T))); }
        catch (...) { releaseMemory(lifetime, bytes); throw; }
    }
    void deallocate(T* data, std::size_t count) noexcept {
        ::operator delete(data);
        releaseMemory(lifetime, 32u + static_cast<u64>(count) * SlotBytes);
    }
    template<class U, class... Args> void construct(U* at, Args&&... args) {
        if (lifetime) if (auto work = lifetime->work.lock()) work->checkpoint();
        std::construct_at(at, std::forward<Args>(args)...);
    }
    template<class U, u64 OtherSlot> bool operator==(const ResourceAllocator<U, OtherSlot>& other) const {
        return lifetime == other.lifetime;
    }
};

template<class T, u64 SlotBytes>
class ResourceVector {
    using Base = std::vector<T, ResourceAllocator<T, SlotBytes>>;
    Base data_;
    void work(std::size_t count) const {
        const auto lifetime = data_.get_allocator().lifetime;
        if (lifetime) if (auto budget = lifetime->work.lock()) {
            budget->location = lifetime->location;
            budget->spend(static_cast<u64>(count));
        }
    }
    void grow(std::size_t required) {
        if (required <= data_.capacity()) return;
        const auto capacity = data_.capacity(), maximum = data_.max_size();
        if (required > maximum)
            throw Trap("geometry allocation size overflow", ErrorReason::OutOfMemory, ErrorStage::Execute);
        const auto doubled = capacity > maximum / 2 ? maximum : capacity * 2;
        work(data_.size());
        data_.reserve(std::max(required, doubled));
    }
public:
    // Explicit reserve/count constructors retain the requested capacity.
    // Every growing append uses max(required, 2*capacity), independently of
    // STL growth heuristics. Only the bounded operations needed by the core
    // are exposed, so another vector mutator cannot bypass this policy.
    using allocator_type = ResourceAllocator<T, SlotBytes>;
    ResourceVector() = default;
    explicit ResourceVector(allocator_type allocator) : data_(allocator) {}
    ResourceVector(std::size_t count, const T& value, allocator_type allocator) : data_(allocator) {
        work(count); data_.reserve(count); data_.resize(count, value);
    }
    ResourceVector(const ResourceVector& other) : data_(other.data_.get_allocator()) {
        work(other.size()); data_.reserve(other.size());
        data_.insert(data_.end(), other.begin(), other.end());
    }
    ResourceVector(ResourceVector&&) = default;
    ResourceVector& operator=(ResourceVector&&) = default;
    ResourceVector& operator=(const ResourceVector& other) {
        if (this != &other) {
            work(other.size()); grow(other.size()); data_.clear();
            data_.insert(data_.end(), other.begin(), other.end());
        }
        return *this;
    }
    void push_back(const T& value) { emplace_back(value); }
    void push_back(T&& value) { emplace_back(std::move(value)); }
    template<class... Args> T& emplace_back(Args&&... args) {
        // Materialize before growth so push_back(vector.front()) is safe.
        T value(std::forward<Args>(args)...);
        work(1);
        grow(data_.size() + 1);
        return data_.emplace_back(std::move(value));
    }
    template<std::forward_iterator Iterator>
    typename Base::iterator insert(typename Base::const_iterator at, Iterator first, Iterator last) {
        // As for vector::insert, the input range must not refer to this vector.
        const auto offset = at - data_.cbegin();
        const auto count = static_cast<std::size_t>(std::distance(first, last));
        if (count > data_.max_size() - data_.size())
            throw Trap("geometry allocation size overflow", ErrorReason::OutOfMemory, ErrorStage::Execute);
        grow(data_.size() + count);
        // All algorithm insertions append immutable ranges. Copy them with
        // allocator construction checkpoints instead of an opaque bulk memcpy.
        if (offset != static_cast<typename Base::difference_type>(data_.size()))
            throw std::logic_error("geometry insertion must append");
        work(count);
        for (; first != last; ++first) data_.emplace_back(*first);
        return data_.begin() + offset;
    }
    void resize(std::size_t count) { if (count > data_.size()) work(count - data_.size()); grow(count); data_.resize(count); }
    void resize(std::size_t count, const T& value) {
        T copy(value); if (count > data_.size()) work(count - data_.size()); grow(count); data_.resize(count, copy);
    }
    void reserve(std::size_t count) { if (count > data_.capacity()) work(data_.size()); data_.reserve(count); }
    void clear() noexcept { data_.clear(); }
    auto size() const noexcept { return data_.size(); }
    auto capacity() const noexcept { return data_.capacity(); }
    bool empty() const noexcept { return data_.empty(); }
    auto get_allocator() const noexcept { return data_.get_allocator(); }
    auto begin() noexcept { return data_.begin(); }
    auto end() noexcept { return data_.end(); }
    auto begin() const noexcept { return data_.begin(); }
    auto end() const noexcept { return data_.end(); }
    auto cbegin() const noexcept { return data_.cbegin(); }
    auto cend() const noexcept { return data_.cend(); }
    T* data() noexcept { return data_.data(); }
    const T* data() const noexcept { return data_.data(); }
    T& operator[](std::size_t i) { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }
    T& at(std::size_t i) { return data_.at(i); }
    const T& at(std::size_t i) const { return data_.at(i); }
    T& front() { return data_.front(); }
    const T& front() const { return data_.front(); }
    T& back() { return data_.back(); }
    const T& back() const { return data_.back(); }
};

// Resource headers include vector objects, shared_ptr control blocks and the
// accounting token. Members and constructor failures unwind before the header
// charge is refunded. No arena registry retains an otherwise dead resource.
template<class T, u64 HeaderBytes, class... Args>
std::shared_ptr<T> makeResource(ResourceArena& arena, Args&&... args) {
    static_assert(sizeof(T) + 64u <= HeaderBytes);
    if (!arena.canAllocate(HeaderBytes))
        throw Trap("generation data exceeds 64 MiB", ErrorReason::OutOfMemory, ErrorStage::Execute,
            arena.surfaceLifetime ? arena.surfaceLifetime->location : ErrorLocation{});
    const auto lifetime = arena.resourceLifetime();
    reserveMemory(lifetime, HeaderBytes);
    T* raw;
    try { raw = new T(std::forward<Args>(args)...); }
    catch (...) { releaseMemory(lifetime, HeaderBytes); throw; }
    return std::shared_ptr<T>(raw, [lifetime](T* value) {
        delete value;
        releaseMemory(lifetime, HeaderBytes);
    });
}

} // namespace iprop::detail
