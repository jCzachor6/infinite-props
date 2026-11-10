#pragma once

#include "iprop/Types.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <optional>
#include <string_view>

namespace iprop {

// Independent of the fuel tariff. Zero in a host request selects this finite
// default; UINT64_MAX is an explicit (still finite) host override.
inline constexpr u64 kDefaultWorkLimit = 100'000'000;
inline constexpr u64 kWorkPollInterval = 1024;
inline constexpr std::size_t kMaxWorkCopyBytes = 32 * 1024;

class CancellationToken {
public:
    void cancel() noexcept { cancelled_.store(true, std::memory_order_relaxed); }
    bool cancelled() const noexcept { return cancelled_.load(std::memory_order_relaxed); }
private:
    std::atomic<bool> cancelled_{false};
};

struct ExecutionLimits {
    using Clock = std::chrono::steady_clock;
    u64 workLimit = kDefaultWorkLimit;
    std::optional<Clock::time_point> deadline;
    std::shared_ptr<const CancellationToken> cancellation;
    // Optional synchronous host poll (for example shared-memory cancellation
    // in a WASM worker). It must not throw, reenter the VM or modify inputs.
    bool (*cancelled)(void*) = nullptr;
    void* context = nullptr;

    static std::optional<Clock::time_point> deadlineAfter(u64 nanoseconds) {
        if (!nanoseconds) return std::nullopt;
        const auto now = Clock::now();
        const auto capped = std::min(nanoseconds, static_cast<u64>(std::chrono::nanoseconds::max().count()));
        const auto duration = std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(capped));
        return duration > Clock::time_point::max() - now ? Clock::time_point::max() : now + duration;
    }
};

namespace detail {

// One owner for setup, every nested call, builtins and result packing. Time
// and cancellation can only stop execution; they never change its accounting.
class WorkBudget {
public:
    explicit WorkBudget(ExecutionLimits limits = {}) : limits_(std::move(limits)),
        budget_(limits_.workLimit ? limits_.workLimit : kDefaultWorkLimit), remaining_(budget_) {}

    ErrorLocation location;
    ErrorStage stage = ErrorStage::Execute;

    // The counters stay inline; exhaustion, cancellation and the clock are
    // out of line so every per-step call is a few register operations.
    void spend(u64 units = 1) {
        if (units > remaining_) [[unlikely]] exhausted();
        if (units >= untilPoll_) [[unlikely]] {
            poll();
            untilPoll_ = units >= kWorkPollInterval ? 1 : kWorkPollInterval - units;
        } else untilPoll_ -= units;
        remaining_ -= units;
    }
    // Exactly `count` consecutive spend(1) calls: the same totals, the same
    // polls and the same counters when it fails part-way. A bounded kernel
    // reserves the steps of one pixel or row here instead of once per step.
    void spendEach(u64 count) {
        if (count < untilPoll_ && count <= remaining_) [[likely]] {
            untilPoll_ -= count;
            remaining_ -= count;
            return;
        }
        spendEachSlow(count);
    }
    // Whether `count` further unit steps fit in the budget.
    bool affords(u64 count) const noexcept { return count <= remaining_; }
    // Use in a prepaid loop or a bounded copy. Polling has no effect on work
    // totals and is safe even on paths whose memory layout differs by host.
    void checkpoint() {
        if (--untilPoll_ == 0) [[unlikely]] { poll(); untilPoll_ = kWorkPollInterval; }
    }
    void poll() const;
    u64 budget() const noexcept { return budget_; }
    u64 remaining() const noexcept { return remaining_; }
    u64 used() const noexcept { return budget_ - remaining_; }

private:
    [[noreturn]] void exhausted() const;
    void spendEachSlow(u64 count);

    ExecutionLimits limits_;
    u64 budget_, remaining_;
    u64 untilPoll_ = 1;
};

// Chunk initialization and copies as well as algorithm loops. reserve may
// enter the system allocator, but no unbounded element initialization/copy is
// delegated to it. Call on new output storage, with no preexisting elements.
template<class T>
void initializeWork(std::vector<T>& output, std::size_t count, const T& value, WorkBudget& work) {
    static_assert(sizeof(T) <= kMaxWorkCopyBytes);
    constexpr auto chunk = std::min(static_cast<std::size_t>(kWorkPollInterval), kMaxWorkCopyBytes / sizeof(T));
    work.spend(static_cast<u64>(count));
    work.poll();
    output.reserve(count);
    for (std::size_t at = 0; at < count;) {
        const auto end = std::min(count, at + chunk);
        output.insert(output.end(), end - at, value);
        work.poll(); at = end;
    }
}

template<class T>
void copyWork(std::vector<T>& output, const std::vector<T>& source, WorkBudget& work) {
    static_assert(sizeof(T) <= kMaxWorkCopyBytes);
    constexpr auto chunk = std::min(static_cast<std::size_t>(kWorkPollInterval), kMaxWorkCopyBytes / sizeof(T));
    work.spend(static_cast<u64>(source.size()));
    work.poll();
    output.reserve(source.size());
    for (std::size_t at = 0; at < source.size();) {
        const auto end = std::min(source.size(), at + chunk);
        output.insert(output.end(), source.begin() + static_cast<std::ptrdiff_t>(at),
            source.begin() + static_cast<std::ptrdiff_t>(end));
        work.poll(); at = end;
    }
}

inline std::string copyStringWork(std::string_view source, WorkBudget& work) {
    work.spend(static_cast<u64>(source.size()));
    std::string output;
    output.reserve(source.size());
    for (std::size_t at = 0; at < source.size();) {
        const auto count = std::min(source.size() - at, static_cast<std::size_t>(kWorkPollInterval));
        output.append(source.data() + at, count);
        work.poll(); at += count;
    }
    return output;
}

template<class T, class U>
void appendWork(std::vector<T>& output, U&& value, WorkBudget& work) {
    work.spend();
    if (output.size() == output.capacity()) {
        work.spend(static_cast<u64>(output.size()));
        std::vector<T> grown;
        const auto maximum = grown.max_size();
        const auto capacity = output.size() > maximum / 2 ? maximum : std::max<std::size_t>(1, output.size() * 2);
        grown.reserve(capacity);
        for (auto& item : output) { work.checkpoint(); grown.push_back(std::move(item)); }
        output.swap(grown);
    }
    output.push_back(std::forward<U>(value));
}

} // namespace detail
} // namespace iprop
