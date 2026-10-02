#pragma once

#include "iprop/D4Transform.hpp"
#include "ips/Ast.hpp"

namespace iprop::ips {

// Shared by semantic checking and call planning. Validates constant domains.
D4Transform resolvePartTransform(const Call& call);

} // namespace iprop::ips
