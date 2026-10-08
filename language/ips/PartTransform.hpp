#pragma once

#include "iprop/D4Transform.hpp"
#include "ips/CallPlan.hpp"

namespace iprop::ips {

// Shared by semantic checking and call planning. Validates constant domains.
D4Transform resolvePartTransform(const BoundCallPlan& call);

} // namespace iprop::ips
