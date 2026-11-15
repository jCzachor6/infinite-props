// Sweep real heap failures through the normal map paths, outside Catch2 and
// including persistent OOM during unwinding: the height kernel, the derived
// fallback and a whole generation that publishes or derives a normal image.
// A failure leaves no result, no arena charge and no live allocation.
#include "Compiler.hpp"
#include "iprop/NormalMap.hpp"
#include "iprop/VM.hpp"
#include "iprop/VmInternals.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "support/fault_injection.hpp"

#include <cstdio>
#include <vector>

using namespace fault_injection;

int main() {
    using namespace iprop;
    std::size_t faults = 0;

    // 1. surface.normal on both receivers, with and without smoothing.
    {
        LoadedFile file; file.code.functions.emplace_back();
        const auto source = std::make_shared<SurfaceBuffer>();
        source->raster.resize(5, 7); source->raster.at(2, 3) = {255, 80, 40, 128}; source->raster.at(3, 3) = {64, 0, 0, 255};
        const auto original = source->raster.pixels;
        for (const u8 mask : {u8{0}, u8{1}}) for (const f32 smoothing : {0.f, 1.5f}) {
            // The expected bytes, computed before any failure is armed.
            std::vector<Color> reference;
            {
                detail::Execution ex(file); ex.frames.emplace_back(); ex.fuel = 1000000;
                ex.push(mask ? Value(MaskValue{source, {}}) : Value(LayerValue{source, {}}));
                ex.push(3.0f); ex.push(smoothing);
                if (!detail::stepNormalMap(ex, {Opcode::SurfaceNormal, mask, 0})) return 1;
                reference = std::get<LayerValue>(ex.frame().stack.back()).buffer->raster.pixels;
            }
            for (const bool keep : {false, true}) {
                bool reachedSuccess = false;
                for (std::size_t limit = 0; limit < 64; ++limit) {
                    const auto baseline = live;
                    {
                        detail::Execution ex(file); ex.frames.emplace_back(); ex.fuel = 1000000;
                        ex.push(mask ? Value(MaskValue{source, {}}) : Value(LayerValue{source, {}}));
                        ex.push(3.0f); ex.push(smoothing);
                        arm(limit, keep);
                        bool succeeded = false;
                        try { succeeded = detail::stepNormalMap(ex, {Opcode::SurfaceNormal, mask, 0}); armed = false; }
                        catch (const std::bad_alloc&) { armed = false; ++faults; }
                        catch (...) { armed = false; return 1; }
                        if (source->raster.pixels != original) return 2;
                        if (succeeded) {
                            if (ex.frame().stack.size() != 1) return 3;
                            const auto& layer = std::get<LayerValue>(ex.frame().stack.back());
                            if (layer.buffer->encoding != SurfaceBuffer::Encoding::Direction) return 4;
                            if (layer.buffer->raster.pixels != reference) return 5;
                            ex.frame().stack.clear();
                            if (ex.resources.bytes != 0) return 6; // result and scratch both released
                            reachedSuccess = true;
                        } else if (!ex.frame().stack.empty() || ex.resources.bytes != 0) return 7;
                    }
                    if (live != baseline) return 8;
                    if (reachedSuccess) break;
                }
                if (!reachedSuccess) return 9;
            }
        }
    }

    // 2. The derived fallback of a finished image.
    {
        Canvas image; image.resize(9, 6);
        for (u32 y = 0; y < 6; ++y) for (u32 x = 0; x < 9; ++x)
            image.at(x, y) = {static_cast<u8>(x * 28), static_cast<u8>(y * 40), 90, static_cast<u8>((x + y) % 3 ? 255 : 0)};
        for (const DerivedNormalRequest request : {DerivedNormalRequest{}, DerivedNormalRequest{2.f, 1.5f, 0.25f}}) {
            detail::WorkBudget unlimited(ExecutionLimits{});
            const auto expected = detail::deriveNormalImage(image, request, unlimited).pixels;
            for (const bool keep : {false, true}) {
                bool reachedSuccess = false;
                for (std::size_t limit = 0; limit < 64 && !reachedSuccess; ++limit) {
                    const auto baseline = live;
                    {
                        detail::WorkBudget work(ExecutionLimits{});
                        arm(limit, keep);
                        try {
                            const auto derived = detail::deriveNormalImage(image, request, work);
                            armed = false;
                            if (derived.pixels != expected) return 10;
                            reachedSuccess = true;
                        }
                        catch (const std::bad_alloc&) { armed = false; ++faults; }
                        catch (...) { armed = false; return 11; }
                    }
                    if (live != baseline) return 12;
                }
                if (!reachedSuccess) return 13;
            }
        }
    }

    // 3. Whole generations: an authored image published after the run, and a
    // derived one. A failure anywhere publishes nothing and leaks nothing.
    for (const bool derive : {false, true}) {
        const auto program = ips::parse(derive
            ? "asset Plain { size 12x9 draw { rect(2, 2, 7, 5, #c08040) } }"
            : R"(asset Relief { size 12x9 output normal relief draw {
                layer base(alpha = surface.source_over) { rect(2, 2, 7, 5, #c08040) }
                layer directions(encoding = surface.normal) {
                    surface.paint(mask.from(base).normal(3.0, 1.0))
                    pixel(1, 1, color.normal(-0.5, -0.5, 0.7))
                }
                relief = directions } })");
        ips::checkAsset(program, {});
        const auto file = loadFile(compileProgram(program, {}));
        validate(file);
        const auto generate = [&] {
            VM vm(file, {});
            if (derive) vm.deriveNormal({});
            vm.run();
            return vm.takeResult();
        };
        const auto expected = generate();
        if (expected.images.size() != 1) return 14;
        for (const bool keep : {false, true}) {
            bool reachedSuccess = false;
            for (std::size_t limit = 0; limit < 4096 && !reachedSuccess; ++limit) {
                const auto baseline = live;
                {
                    arm(limit, keep);
                    try {
                        const auto result = generate();
                        armed = false;
                        if (result.image.pixels != expected.image.pixels || result.images.size() != 1 ||
                            result.images[0].image.pixels != expected.images[0].image.pixels ||
                            result.images[0].name != expected.images[0].name) return 15;
                        reachedSuccess = true;
                    }
                    catch (const std::bad_alloc&) { armed = false; ++faults; }
                    catch (const Trap& trap) {
                        // The VM may report the failed allocation as its own out-of-memory trap.
                        armed = false; ++faults;
                        if (trap.reason != ErrorReason::OutOfMemory) return 16;
                    }
                    catch (...) { armed = false; return 17; }
                }
                if (live != baseline) return 18;
            }
            if (!reachedSuccess) return 19;
        }
    }
    std::printf("Normal map allocations: PASS %zu injected transient/persistent failures across the height kernel on "
                "both receivers, the derived fallback and authored/derived generations; no partial result, arena charge or heap leak\n", faults);
    return 0;
}
