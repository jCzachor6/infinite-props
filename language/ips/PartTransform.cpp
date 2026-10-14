#include "ips/PartTransform.hpp"
#include "ips/Fold.hpp"

namespace iprop::ips {

D4Transform resolvePartTransform(const BoundCallPlan& call) {
    i32 rot = 0, mx = 0, my = 0;
    for (std::size_t slot = 2; slot < 5; ++slot) {
        const auto* supplied = call.arguments[slot];
        const auto& name = call.signature.parameters[slot].name;
        i32 value = std::get<i32>(call.signature.defaults[slot]);
        if (supplied) {
            Value folded;
            if (!foldExpression(*supplied->value, folded) || !std::holds_alternative<i32>(folded)) {
                throw SourceError{locationOf(*supplied->value), "part transforms must be constant integers"};
            }
            value = std::get<i32>(folded);
        }
        const auto at = supplied ? locationOf(*supplied->value) : SourceLocation{};
        if (slot == 2) {
            if (value < 0 || value > 270 || value % 90 != 0) {
                throw SourceError{at, "the rot transform must be 0, 90, 180 or 270"};
            }
            rot = value;
        } else {
            if (value != 0 && value != 1) {
                throw SourceError{at, "the " + name + " transform must be 0 or 1"};
            }
            (slot == 3 ? mx : my) = value;
        }
    }
    return *D4Transform::fromParameters(rot, mx, my);
}

} // namespace iprop::ips
