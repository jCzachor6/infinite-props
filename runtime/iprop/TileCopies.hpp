#pragma once

#include "iprop/VmInternals.hpp"

namespace iprop::detail {

// Inclusive bounds in raster coordinates. Always run the original draw, even
// off-canvas, for its usual argument checks and tariff. Visit neighbours in
// row-major translation order and charge their instruction dispatch as well.
template<class Draw>
void tileCopies(Execution& ex, double left, double top, double right, double bottom, Draw draw) {
    if (!ex.target.tile || right < left || bottom < top) {
        draw(0, 0);
        return;
    }
    const auto w = static_cast<i32>(ex.canvas().width);
    const auto h = static_cast<i32>(ex.canvas().height);
    for (const i32 dy : {-h, 0, h}) for (const i32 dx : {-w, 0, w}) {
        if (dx || dy) {
            if (right+dx < 0 || bottom+dy < 0 || left+dx >= w || top+dy >= h) continue;
            const u64 units = ex.fuelUnits, paid = ex.fuelPaid;
            ex.fuelUnits = ex.fuelPaid = 0;
            ex.spend(1);
            draw(dx, dy);
            ex.fuelUnits = units;
            ex.fuelPaid = paid;
            continue;
        }
        draw(dx, dy);
    }
}

} // namespace iprop::detail
