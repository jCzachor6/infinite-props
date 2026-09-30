// The HTML5/WASM binding: exposes the reference interpreter to JavaScript
// through one entry point — `generate(fileBytes, inputs) -> {width, height,
// data, outputs}`. The returned `data` is a Uint8Array view into the module
// heap; it stays valid until the next generate() call.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstring>
#include <string>
#include <vector>

#include "iprop/VM.hpp"
#include "iprop/Validator.hpp"

using namespace iprop;

namespace {

// A reusable heap buffer for the latest RGBA result. generate() returns a
// view over it; the next call reuses (and invalidates) it.
std::vector<u8> g_rgba;

[[noreturn]] void throwJs(const std::string& message) {
    emscripten::val::global("Error")
        .new_(emscripten::val(message))
        .throw_();
}

} // namespace

emscripten::val generate(emscripten::val fileBytes, u32 seed, u32 variant,
                         u32 archetype, u32 sizeX, u32 sizeY) {
    // Copy the input bytes out of the JS array view into our own buffer.
    const std::size_t length = fileBytes["byteLength"].as<std::size_t>();
    std::vector<iprop::u8> bytes(length);
    emscripten::val view{emscripten::typed_memory_view(length, bytes.data())};
    view.call<void>("set", fileBytes);

    try {
        const auto loaded = iprop::loadFile(bytes);
        iprop::validate(loaded);
        iprop::VM vm(loaded, {seed, variant, archetype, sizeX, sizeY, {}});
        vm.run();

        const iprop::Canvas& canvas = vm.canvas();
        g_rgba.resize(static_cast<std::size_t>(canvas.width) * canvas.height * 4);
        for (u32 y = 0; y < canvas.height; ++y) {
            for (u32 x = 0; x < canvas.width; ++x) {
                const iprop::Color& pixel = canvas.at(x, y);
                const std::size_t at =
                    (static_cast<std::size_t>(y) * canvas.width + x) * 4;
                g_rgba[at] = pixel.r;
                g_rgba[at + 1] = pixel.g;
                g_rgba[at + 2] = pixel.b;
                g_rgba[at + 3] = pixel.a;
            }
        }

        const iprop::GenerationResult result = vm.result();
        emscripten::val outputs = emscripten::val::array();
        for (const auto& output : result.outputs) {
            emscripten::val entry = emscripten::val::object();
            entry.set("name", output.name);
            entry.set("isList", output.isList);
            entry.set("kind", static_cast<u8>(output.kind));
            emscripten::val points = emscripten::val::array();
            for (const auto& point : output.points) {
                emscripten::val p = emscripten::val::object();
                p.set("x", point.x);
                p.set("y", point.y);
                points.call<void>("push", p);
            }
            emscripten::val rects = emscripten::val::array();
            for (const auto& rect : output.rects) {
                emscripten::val r = emscripten::val::object();
                r.set("x", rect.x);
                r.set("y", rect.y);
                r.set("w", rect.w);
                r.set("h", rect.h);
                rects.call<void>("push", r);
            }
            emscripten::val circles = emscripten::val::array();
            for (const auto& circle : output.circles) {
                emscripten::val c = emscripten::val::object();
                c.set("x", circle.x);
                c.set("y", circle.y);
                c.set("r", circle.r);
                circles.call<void>("push", c);
            }
            // Export bits before widening to a JS Number, which can lose
            // a NaN payload or sign. This is the exact conformance surface.
            emscripten::val valueBits = emscripten::val::array();
            const auto bits = [](f32 value) {
                u32 encoded = 0;
                std::memcpy(&encoded, &value, sizeof(encoded));
                return encoded;
            };
            for (const auto& point : output.points) {
                auto p = emscripten::val::object();
                p.set("x", bits(point.x));
                p.set("y", bits(point.y));
                valueBits.call<void>("push", p);
            }
            for (const auto& rect : output.rects) {
                auto r = emscripten::val::object();
                r.set("x", bits(rect.x));
                r.set("y", bits(rect.y));
                r.set("w", bits(rect.w));
                r.set("h", bits(rect.h));
                valueBits.call<void>("push", r);
            }
            for (const auto& circle : output.circles) {
                auto c = emscripten::val::object();
                c.set("x", bits(circle.x));
                c.set("y", bits(circle.y));
                c.set("r", bits(circle.r));
                valueBits.call<void>("push", c);
            }
            entry.set("valueBits", valueBits);
            entry.set("points", points);
            entry.set("rects", rects);
            entry.set("circles", circles);
            outputs.call<void>("push", entry);
        }

        emscripten::val resultObject = emscripten::val::object();
        resultObject.set("width", canvas.width);
        resultObject.set("height", canvas.height);
        resultObject.set("data",
                         emscripten::val(emscripten::typed_memory_view(
                             g_rgba.size(), g_rgba.data())));
        resultObject.set("outputs", outputs);
        return resultObject;
    } catch (const iprop::FormatError& error) {
        throwJs(std::string("iprop: ") + error.what());
    }
}

EMSCRIPTEN_BINDINGS(iprop) {
    emscripten::function("generate", &generate);
}
