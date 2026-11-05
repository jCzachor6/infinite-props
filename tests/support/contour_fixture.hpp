#pragma once
#include "iprop/Contours.hpp"
#include "iprop/VmInternals.hpp"

namespace iprop::test_support {
inline MaskValue contourMask(detail::Execution& ex,u32 width,u32 height,std::span<const u8> pixels,
                             Vec2 scale={1,1},u8 d4=0,RasterPoint origin={}) {
    ex.allocateResource(maskBytes(width,height));
    auto b=ex.resources.makeSurface(maskBytes(width,height));
    b->format=SurfaceBuffer::Format::A8;
    b->raster.width=width; b->raster.height=height;
    b->raster.scaleX=scale.x; b->raster.scaleY=scale.y;
    b->coverage.assign(pixels.begin(),pixels.end());
    return {std::move(b),{*D4Transform::fromKind(d4),origin}};
}
} // namespace iprop::test_support
