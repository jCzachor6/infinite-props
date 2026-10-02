#pragma once

#include "iprop/Code.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace iprop::compiler {

// A slot is retained only while the emitter needs its value. Slots can be
// reused across sequential expressions, but never across overlapping lives.
class TemporaryLocals {
    struct Slot { u16 index; ValueType type; bool live; };
    std::vector<Slot> slots_;
    std::vector<ValueType>* locals_ = nullptr;
    std::size_t params_ = 0;

public:
    class Lease {
        friend class TemporaryLocals;
        TemporaryLocals* pool_;
        std::size_t slot_;
        Lease(TemporaryLocals& pool, std::size_t slot) : pool_(&pool), slot_(slot) {}
    public:
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept
            : pool_(std::exchange(other.pool_, nullptr)), slot_(other.slot_) {}
        ~Lease() { if (pool_) pool_->slots_[slot_].live = false; }
        u16 index() const { return pool_->slots_[slot_].index; }
    };

    void reset(std::size_t params, std::vector<ValueType>& locals) {
        slots_.clear();
        params_ = params;
        locals_ = &locals;
    }

    Lease acquire(ValueType type) {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (!slots_[i].live && slots_[i].type == type) {
                slots_[i].live = true;
                return Lease(*this, i);
            }
        }
        const auto index = params_ + locals_->size();
        if (index >= kMaxLocals) throw std::length_error("too many locals");
        locals_->push_back(type);
        slots_.push_back({static_cast<u16>(index), type, true});
        return Lease(*this, slots_.size() - 1);
    }
};

} // namespace iprop::compiler
