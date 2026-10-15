#include "iprop/VmInternals.hpp"
#include "iprop/ListOperations.hpp"

namespace iprop::detail {

void Execution::allocateResource(u64 size) {
    if (!resources.canAllocate(size)) trap("generation data exceeds 64 MiB", ErrorReason::OutOfMemory);
    resources.charge(size);
}

bool stepCollections(Execution& ex, Instruction ins) {
    const auto type = [&]() {
        if (ins.operand >= ex.file.collections.types.size()) ex.trap("type descriptor index out of range", ErrorReason::IndexRange);
        return ex.file.collections.types[ins.operand];
    };
    const auto builder = [&](TypeDescriptor expected) {
        auto handle = ex.popHandle<ListBuilder>("list builder");
        if (handle.data->list.type != expected) ex.trap("list builder type mismatch", ErrorReason::StackType);
        return std::move(handle.data);
    };
    switch (ins.opcode) {
    case Opcode::ListDistinct:
    case Opcode::ListSort: {
        const auto expected=type();const auto source=ex.popHandle<ListValue>("list handle");
        if(source.type!=expected) ex.trap("list type mismatch",ErrorReason::StackType);
        try {
            const bool sort=ins.opcode==Opcode::ListSort;
            // Inspection itself is prepaid; reserve all bounded comparison/copy
            // work before either algorithm or an output-buffer allocation.
            const auto n=source.data->elements.size();ex.spend(2u*n);
            const auto checked=checkListSelection(source,sort);ex.spend(checked.work-2u*n);
            const auto selection=selectList(checked);
            if(selection.unchanged(source)) ex.push(source);
            else {
                ex.allocateResource(ResourceArena::listBytes(selection.count));
                auto result=materializeListSelection(source,selection);
                ex.resources.lists.push_back(result.data);ex.push(std::move(result));
            }
        } catch(const std::invalid_argument& error) {ex.trap(error.what(),ErrorReason::RuntimeViolation);}
        break;
    }
    case Opcode::ConstList: {
        if (ins.operand >= ex.file.collections.lists.size()) ex.trap("collection index out of range", ErrorReason::IndexRange);
        auto cached = ex.resources.constants.find(ins.operand);
        if (cached == ex.resources.constants.end()) {
            const auto& list = ex.file.collections.lists[ins.operand];
            u64 bytes = ResourceArena::listBytes(list.data->elements.size());
            std::set<const CapturedRangeData*> descriptions;
            std::set<const PointDistributionData*> points;
            const auto retainPoints = [&](const Distribution& d) {
                if (d.points && !ex.resources.distributionConstants.contains(d.points.get())) points.insert(d.points.get());
            };
            auto& descriptionConstants = ex.resources.descriptionConstants;
            if (list.type.item().isDescription()) {
                ex.spend(list.data->elements.size());
                for (const auto& value : list.data->elements) {
                    const auto* data = descriptionData(value);
                    if (!descriptionConstants.contains(data)) descriptions.insert(data);
                    retainPoints(data->distribution);
                }
                bytes += descriptions.size() * ResourceArena::descriptionBytes();
            }
            if (list.type.item() == ValueType::Distribution) {
                for (const auto& value : list.data->elements) retainPoints(std::get<Distribution>(value));
                if (!points.empty()) ex.spend(list.data->elements.size());
            }
            for (const auto* data : points) {
                ex.spend(data->points.size()); bytes += ResourceArena::distributionBytes(data->points.size());
            }
            ex.allocateResource(bytes);
            descriptionConstants.insert(descriptions.begin(), descriptions.end());
            ex.resources.distributionConstants.insert(points.begin(), points.end());
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
        const auto list = ex.popHandle<ListValue>("list handle");
        if (list.type != expected) ex.trap("list type mismatch", ErrorReason::StackType);
        if (index < 0 || static_cast<std::size_t>(index) >= list.data->elements.size())
            ex.trap("list index out of range", ErrorReason::IndexRange);
        ex.push(list.data->elements[static_cast<std::size_t>(index)]);
        break;
    }
    case Opcode::ListLen: {
        const auto expected = type();
        const auto list = ex.popHandle<ListValue>("list handle");
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
