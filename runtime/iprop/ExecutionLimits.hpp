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

    void spend(u64 units = 1) {
        if (units > remaining_)
            throw Trap("native work limit exhausted", ErrorReason::WorkExhausted, stage, location);
        if (units >= untilPoll_) {
            poll();
            untilPoll_ = units >= kWorkPollInterval ? 1 : kWorkPollInterval - units;
        } else untilPoll_ -= units;
        remaining_ -= units;
    }
    // Use in a prepaid loop or a bounded copy. Polling has no effect on work
    // totals and is safe even on paths whose memory layout differs by host.
    void checkpoint() {
        if (--untilPoll_ == 0) { poll(); untilPoll_ = kWorkPollInterval; }
    }
    void poll() const {
        if ((limits_.cancellation && limits_.cancellation->cancelled()) ||
            (limits_.cancelled && limits_.cancelled(limits_.context)))
            throw Trap("generation cancelled", ErrorReason::Cancelled, stage, location);
        if (limits_.deadline && ExecutionLimits::Clock::now() >= *limits_.deadline)
            throw Trap("generation deadline exceeded", ErrorReason::DeadlineExceeded, stage, location);
    }
    u64 budget() const noexcept { return budget_; }
    u64 remaining() const noexcept { return remaining_; }
    u64 used() const noexcept { return budget_ - remaining_; }

private:
    ExecutionLimits limits_;
    u64 budget_, remaining_;
    u64 untilPoll_ = 1;
};

// Chunk initialization and copies as well as algorithm loops. reserve may
// enter the system allocator, but no unbounded element initialization/copy is
// delegated to it. Call on new output storage, with no preexisting elements.
template<class T>
void initializeWork(std::vector<T>& output, std::size_t count, const T& value, WorkBudget& work) {
    work.spend(static_cast<u64>(count));
    work.poll();
    output.reserve(count);
    for (std::size_t at = 0; at < count;) {
        const auto end = std::min(count, at + static_cast<std::size_t>(kWorkPollInterval));
        output.insert(output.end(), end - at, value);
        work.poll(); at = end;
    }
}

template<class T>
void copyWork(std::vector<T>& output, const std::vector<T>& source, WorkBudget& work) {
    work.spend(static_cast<u64>(source.size()));
    work.poll();
    output.reserve(source.size());
    for (std::size_t at = 0; at < source.size();) {
        const auto end = std::min(source.size(), at + static_cast<std::size_t>(kWorkPollInterval));
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
