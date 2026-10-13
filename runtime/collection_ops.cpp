#include "iprop/VmInternals.hpp"

namespace iprop::detail {

void Execution::allocateResource(u64 size) {
    if (!resources.canAllocate(size)) trap("generation data exceeds 64 MiB", ErrorReason::OutOfMemory);
    resources.charge(size);
}

ListValue Execution::popList() {
    const auto value = pop();
    const auto* list = std::get_if<ListValue>(&value);
    if (!list) trap("stack type mismatch: expected list", ErrorReason::StackType);
    if (!list->data) trap("list handle is unassigned", ErrorReason::RuntimeViolation);
    return *list;
}

bool stepCollections(Execution& ex, Instruction ins) {
    const auto type = [&]() {
        if (ins.operand >= ex.file.collections.types.size()) ex.trap("type descriptor index out of range", ErrorReason::IndexRange);
        return ex.file.collections.types[ins.operand];
    };
    const auto builder = [&](TypeDescriptor expected) {
        auto value = ex.pop();
        const auto* handle = std::get_if<ListBuilder>(&value);
        if (!handle || !handle->data || handle->data->list.type != expected)
            ex.trap("invalid list builder", ErrorReason::StackType);
        return handle->data;
    };
    switch (ins.opcode) {
    case Opcode::ConstList: {
        if (ins.operand >= ex.file.collections.lists.size()) ex.trap("collection index out of range", ErrorReason::IndexRange);
        auto cached = ex.resources.constants.find(ins.operand);
        if (cached == ex.resources.constants.end()) {
            const auto& list = ex.file.collections.lists[ins.operand];
            u64 bytes = ResourceArena::listBytes(list.data->elements.size());
            std::set<const SamplerData*> descriptions;
            if (list.type.item().isSampler()) {
                ex.spend(list.data->elements.size());
                for (const auto& value : list.data->elements) {
                    const auto* data = std::get<SamplerValue>(value).data.get();
                    if (!ex.resources.samplerConstants.contains(data)) descriptions.insert(data);
                }
                bytes += descriptions.size() * ResourceArena::samplerBytes();
            }
            ex.allocateResource(bytes);
            ex.resources.samplerConstants.insert(descriptions.begin(), descriptions.end());
            cached = ex.resources.constants.emplace(ins.operand, list).first;
        }
        ex.push(cached->second);
        break;
    }
    case Opcode::ListBegin: {
        const auto expected = type();
        const auto count = ex.popI32();
        if (count < 0 || count > kMaxListElements) ex.trap("list size must be between 0 and 256", ErrorReason::NumericRange);
        ex.spend(static_cast<u64>(count));
        ex.allocateResource(ResourceArena::listBytes(static_cast<std::size_t>(count)));
        auto data = std::make_shared<ListBuilderData>();
        data->list.type = expected;
        data->expected = static_cast<std::size_t>(count);
        data->list.elements.reserve(data->expected);
        ex.push(ListBuilder{std::move(data)});
        break;
    }
    case Opcode::ListAppend: {
        const auto expected = type();
        auto value = ex.pop();
        if (valueType(value) != expected.item()) ex.trap("list element type mismatch", ErrorReason::StackType);
        auto data = builder(expected);
        if (data->list.elements.size() >= data->expected) ex.trap("too many list elements", ErrorReason::IndexRange);
        data->list.elements.push_back(std::move(value));
        ex.push(ListBuilder{std::move(data)});
        break;
    }
    case Opcode::ListEnd: {
        const auto expected = type();
        auto data = builder(expected);
        if (data->list.elements.size() != data->expected) ex.trap("incomplete list builder", ErrorReason::IndexRange);
        // Freeze without copying the element storage. No mutable alias is
        // observable by the source or any later instruction.
        std::shared_ptr<const ListData> frozen(data, &data->list);
        ex.resources.lists.push_back(frozen);
        ex.push(ListValue{expected, std::move(frozen)});
        break;
    }
    case Opcode::ListGet: {
        const auto expected = type();
        const auto index = ex.popI32();
        const auto list = ex.popList();
        if (list.type != expected) ex.trap("list type mismatch", ErrorReason::StackType);
        if (index < 0 || static_cast<std::size_t>(index) >= list.data->elements.size())
            ex.trap("list index out of range", ErrorReason::IndexRange);
        ex.push(list.data->elements[static_cast<std::size_t>(index)]);
        break;
    }
    case Opcode::ListLen: {
        const auto expected = type();
        const auto list = ex.popList();
        if (list.type != expected) ex.trap("list type mismatch", ErrorReason::StackType);
        ex.push(static_cast<i32>(list.data->elements.size()));
        break;
    }
    default: return false;
    }
    ++ex.frame().ip;
    return true;
}
} // namespace iprop::detail
