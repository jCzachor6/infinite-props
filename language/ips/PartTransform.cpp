#include "ips/PartTransform.hpp"
#include "ips/Fold.hpp"

namespace iprop::ips {

D4Transform resolvePartTransform(const Call& call) {
    i32 rot = 0, mx = 0, my = 0;
    for (const auto& arg : call.args) {
        if (!arg.name || (*arg.name != "rot" && *arg.name != "mirror_x" &&
                         *arg.name != "mirror_y")) continue;
        Value folded;
        if (!foldExpression(*arg.value, folded) || !std::holds_alternative<i32>(folded)) {
            throw SourceError{locationOf(*arg.value), "part transforms must be constant integers"};
        }
        const i32 value = std::get<i32>(folded);
        if (*arg.name == "rot") {
            if (value < 0 || value > 270 || value % 90 != 0) {
                throw SourceError{locationOf(*arg.value), "the rot transform must be 0, 90, 180 or 270"};
            }
            rot = value;
        } else {
            if (value != 0 && value != 1) {
                throw SourceError{locationOf(*arg.value), "the " + *arg.name + " transform must be 0 or 1"};
            }
            (*arg.name == "mirror_x" ? mx : my) = value;
        }
    }
    return *D4Transform::fromParameters(rot, mx, my);
}

} // namespace iprop::ips
