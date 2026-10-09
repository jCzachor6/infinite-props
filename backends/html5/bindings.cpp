// The HTML5/WASM binding: exposes the reference interpreter to JavaScript
// through one entry point, `generate(fileBytes, seed, variant, archetype,
// sizeX, sizeY[, uniforms]) -> {width, height, data, outputs}`. `uniforms`
// maps declared input names to values: numbers for u32/f32 inputs and
// "#RRGGBB" or "#RRGGBBAA" strings for colours, as `ipgen --uniform` takes
// them. The returned `data` is a Uint8Array view into the module heap; it
// stays valid until the next generate() call.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstring>
#include <map>
#include <stdexcept>
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

bool hexDigit(char c, u8& out) {
    if (c >= '0' && c <= '9') { out = static_cast<u8>(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { out = static_cast<u8>(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { out = static_cast<u8>(c - 'A' + 10); return true; }
    return false;
}

// #RRGGBB (opaque) or #RRGGBBAA, as ipgen parses --uniform colours.
bool parseColor(const std::string& text, Color& color) {
    if ((text.size() != 7 && text.size() != 9) || text[0] != '#') return false;
    u8* channels[4] = {&color.r, &color.g, &color.b, &color.a};
    color.a = 255;
    for (std::size_t i = 0; i < (text.size() - 1) / 2; ++i) {
        u8 hi = 0, lo = 0;
        if (!hexDigit(text[1 + i * 2], hi) || !hexDigit(text[2 + i * 2], lo)) return false;
        *channels[i] = static_cast<u8>(hi * 16u + lo);
    }
    return true;
}

// Reads the uniforms object against the file's declared root inputs. Throws
// std::invalid_argument, which generate() turns into a JS Error.
std::map<std::string, UniformValue> readUniforms(const LoadedFile& loaded, emscripten::val uniforms) {
    std::map<std::string, UniformValue> values;
    if (uniforms.isUndefined() || uniforms.isNull()) return values;
    const auto names = emscripten::vecFromJSArray<std::string>(
        emscripten::val::global("Object").call<emscripten::val>("keys", uniforms));
    for (const auto& name : names) {
        const InputDecl* decl = loaded.meta.findInput(name, loaded.pool);
        if (decl == nullptr) throw std::invalid_argument("unknown uniform '" + name + "'");
        const emscripten::val value = uniforms[name];
        UniformValue uniform;
        uniform.type = decl->type;
        switch (decl->type) {
        case InputType::Color:
            if (!value.isString() || !parseColor(value.as<std::string>(), uniform.color))
                throw std::invalid_argument("uniform '" + name + "' must be a #RRGGBB or #RRGGBBAA colour");
            break;
        case InputType::F32:
            if (!value.isNumber()) throw std::invalid_argument("uniform '" + name + "' must be a number");
            uniform.f = static_cast<f32>(value.as<double>());
            break;
        case InputType::U32: {
            const double number = value.isNumber() ? value.as<double>() : -1.0;
            if (!(number >= 0.0 && number <= 4294967295.0) || number != static_cast<double>(static_cast<u32>(number)))
                throw std::invalid_argument("uniform '" + name + "' must be an unsigned 32-bit integer");
            uniform.u = static_cast<u32>(number);
            break;
        }
        }
        values[name] = uniform;
    }
    return values;
}

} // namespace

emscripten::val generateWith(emscripten::val fileBytes, u32 seed, u32 variant,
                             u32 archetype, u32 sizeX, u32 sizeY,
                             emscripten::val uniforms) {
    // Copy the input bytes out of the JS array view into our own buffer.
    const std::size_t length = fileBytes["byteLength"].as<std::size_t>();
    std::vector<iprop::u8> bytes(length);
    emscripten::val view{emscripten::typed_memory_view(length, bytes.data())};
    view.call<void>("set", fileBytes);

    try {
        const auto loaded = iprop::loadFile(bytes);
        iprop::validate(loaded);
        iprop::VM vm(loaded, {seed, variant, archetype, sizeX, sizeY, readUniforms(loaded, uniforms)});
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
    } catch (const std::invalid_argument& error) {
        throwJs(std::string("iprop: ") + error.what());
    }
}

emscripten::val generate(emscripten::val fileBytes, u32 seed, u32 variant,
                         u32 archetype, u32 sizeX, u32 sizeY) {
    return generateWith(fileBytes, seed, variant, archetype, sizeX, sizeY,
                        emscripten::val::undefined());
}

EMSCRIPTEN_BINDINGS(iprop) {
    emscripten::function("generate", &generate);
    emscripten::function("generate", &generateWith);
}
