#include "iprop/VmInternals.hpp"
#include "iprop/Texture.hpp"
#include "iprop/Noise.hpp"
#include "iprop/math/Scalar.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace iprop::detail {
namespace {

template<class Map>
void mapLayer(Execution& ex, const char* what, Map map) {
    const auto source = ex.popLayer();
    if (source.buffer->encoding == SurfaceBuffer::Encoding::Direction)
        ex.trap(what, ErrorReason::RuntimeViolation);
    const auto& raster = source.buffer->raster;
    const auto bytes = surfaceBytes(raster.width, raster.height);
    ex.spend(raster.pixels.size());
    ex.allocateResource(bytes);
    auto buffer = ex.resources.makeSurface(bytes);
    auto& result = buffer->raster;
    result.resize(raster.width, raster.height, ex.workBudget.get());
    result.scaleX = raster.scaleX;
    result.scaleY = raster.scaleY;
    buffer->tile = source.buffer->tile;
    for (std::size_t i = 0; i < raster.pixels.size(); ++i) {
        ex.work();
        result.pixels[i] = map(raster.pixels[i]);
    }
    ex.push(LayerValue{std::move(buffer), source.mapping});
}

void gradient(Execution& ex) {
    const auto colors = ex.popHandle<ListValue>("gradient colors");
    const auto& stops = colors.data->elements;
    if (colors.type != ValueType::list(ValueType::Color) || stops.size() < 2 || stops.size() > 256)
        ex.trap("gradient needs 2..256 color stops", ErrorReason::NumericRange);
    std::array<Color,256> lut;
    ResourceReservation scratch(ex.resources, sizeof(lut));
    ex.spend(lut.size());
    for (unsigned i = 0; i < lut.size(); ++i) {
        ex.work();
        const auto position = i * (stops.size()-1);
        const auto index = position / 255;
        const auto weight = static_cast<unsigned>(position % 255);
        const Color a = std::get<Color>(stops[index]);
        const Color b = std::get<Color>(stops[std::min(index+1, stops.size()-1)]);
        const auto mix = [&](u8 x, u8 y) { return static_cast<u8>((x*(255-weight)+y*weight+127)/255); };
        lut[i] = {mix(a.r,b.r), mix(a.g,b.g), mix(a.b,b.b), mix(a.a,b.a)};
    }
    mapLayer(ex, "gradient needs a colour layer, not a direction layer", [&](Color pixel) {
        Color result = lut[(77u*pixel.r + 150u*pixel.g + 29u*pixel.b + 128u) >> 8];
        result.a = multiplyCoverage(pixel.a, result.a);
        // A transparent pixel holds no colour, as in a capture.
        return result.a == 0 ? Color{} : result;
    });
}

void levels(Execution& ex) {
    const f32 gamma = ex.popF32(), white = ex.popF32(), black = ex.popF32();
    if (!std::isfinite(gamma) || gamma <= 0 || !(black >= 0 && black <= 1) ||
        !(white >= 0 && white <= 1) || black == white)
        ex.trap("levels needs distinct black and white in 0..1 and finite positive gamma", ErrorReason::NumericRange);
    std::array<u8,256> lut;
    ResourceReservation scratch(ex.resources, sizeof(lut));
    ex.spend(lut.size());
    for (unsigned i = 0; i < lut.size(); ++i) {
        ex.work();
        const f32 t = std::clamp((static_cast<f32>(i)/255.f-black)/(white-black), 0.f, 1.f);
        const f32 curved = t == 0 || t == 1 || gamma == 1 ? t : math::f32Pow(t, 1.f/gamma);
        lut[i] = static_cast<u8>(std::floor(curved*255.f + 0.5f));
    }
    mapLayer(ex, "levels needs a colour layer, not a direction layer", [&](Color p) {
        // A transparent pixel holds no colour, as in a capture.
        return p.a == 0 ? Color{} : Color{lut[p.r], lut[p.g], lut[p.b], p.a};
    });
}
} // namespace

bool stepTexture(Execution& ex, Instruction ins) {
    if (ins.opcode != Opcode::Texture) return false;
    const auto sub = static_cast<TextureOp>(ins.aux);
    const auto base = static_cast<Opcode>(ins.aux);
    // Dispatch has paid one unit at the page's plain rate. The operation
    // continues that instruction's ledger at its own rate, as if the rate had
    // applied from the start: one unit rounds up to one at every rate.
    ex.fuelRate = textureFuelPercent(ins.aux);
    ex.fuelPaid = ex.fuelUnits = 1;
    if (sub == TextureOp::PaintLayer || sub == TextureOp::PaintMask) {
        if (!validCompositionMode(ins.operand)) ex.trap("unknown surface blend mode", ErrorReason::InvalidInstruction);
    } else if (ins.operand != 0) ex.trap("texture operand must be zero", ErrorReason::InvalidInstruction);
    switch (sub) {
    case TextureOp::PaintLayer: {
        const auto opacity = popOpacity(ex);
        const auto image = ex.popLayer();
        ex.spend(image.buffer->raster.pixels.size());
        paintSurface(ex, image, static_cast<CompositionMode>(ins.operand), opacity);
        break;
    }
    case TextureOp::PaintMask: paintMask(ex, ins.operand, popOpacity(ex)); break;
    case TextureOp::Gradient: gradient(ex); break;
    case TextureOp::Levels: levels(ex); break;
    default: {
        if (!periodicNoiseOpcode(base)) ex.trap("unknown texture operation", ErrorReason::InvalidInstruction);
        if (base == Opcode::NoiseMask || base == Opcode::NoiseMaskConfig) tiledNoiseMaskInstruction(ex, base);
        else periodicNoiseInstruction(ex, base);
        break;
    }
    }
    ++ex.frame().ip;
    return true;
}
} // namespace iprop::detail
