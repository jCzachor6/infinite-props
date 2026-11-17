#include <catch2/catch_test_macros.hpp>
#include "iprop/D4Transform.hpp"
#include "iprop/NormalMap.hpp"
#include "iprop/VM.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/math/Distribution.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "support/test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace iprop;
using test_support::patch16;

namespace {

struct NormalExecution {
    LoadedFile file;
    detail::Execution ex{file};
    NormalExecution() {
        file.code.functions.emplace_back();
        ex.frames.emplace_back();
        ex.frame().stream = 41;
        ex.target = RenderTarget::create(3, 2, {1, 1});
        ex.budget = ex.fuel = 1000000000;
    }
    LayerValue run(Value receiver, f32 depth, f32 smoothing) {
        const u8 aux = std::holds_alternative<MaskValue>(receiver) ? 1 : 0;
        ex.push(std::move(receiver)); ex.push(depth); ex.push(smoothing);
        REQUIRE(detail::stepNormalMap(ex, {Opcode::SurfaceNormal, aux, 0}));
        return std::get<LayerValue>(ex.pop());
    }
    ErrorReason reason(Value receiver, f32 depth, f32 smoothing, u8 aux) {
        ex.push(std::move(receiver)); ex.push(depth); ex.push(smoothing);
        try { (void)detail::stepNormalMap(ex, {Opcode::SurfaceNormal, aux, 0}); }
        catch (const Trap& error) { ex.frame().stack.clear(); return error.reason; }
        ex.frame().stack.clear();
        return ErrorReason::None;
    }
};

std::shared_ptr<SurfaceBuffer> rgba(u32 w, u32 h, Vec2 scale = {1, 1}) {
    auto buffer = std::make_shared<SurfaceBuffer>();
    buffer->raster.resize(w, h); buffer->raster.scaleX = scale.x; buffer->raster.scaleY = scale.y;
    return buffer;
}
std::shared_ptr<SurfaceBuffer> a8(u32 w, u32 h, std::vector<u8> coverage, Vec2 scale = {1, 1}) {
    auto buffer = std::make_shared<SurfaceBuffer>();
    buffer->format = SurfaceBuffer::Format::A8;
    buffer->raster.width = w; buffer->raster.height = h;
    buffer->raster.scaleX = scale.x; buffer->raster.scaleY = scale.y;
    buffer->coverage = std::move(coverage);
    return buffer;
}

// Written from the contract: nearest stored value, ties away from zero.
u8 byteOf(double value) {
    if (value < 0) return static_cast<u8>(255 - byteOf(-value));
    return static_cast<u8>(std::floor(std::min(128.0 + 127.5 * value, 255.0)));
}

// A second implementation of the kernel: two whole planes instead of the
// in-place passes and their ring, with the contract's sums in the same order.
std::vector<Color> reference(std::vector<double> height, const std::vector<u8>& alpha, u32 w, u32 h,
                             f32 depth, f32 sigma) {
    const auto at = [&](const std::vector<double>& plane, i64 x, i64 y) {
        x = std::clamp(x, i64{0}, static_cast<i64>(w) - 1); y = std::clamp(y, i64{0}, static_cast<i64>(h) - 1);
        return plane[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)];
    };
    if (sigma > 0) {
        const int radius = static_cast<int>(std::ceil(3.0 * static_cast<double>(sigma)));
        std::vector<double> weights; double total = 0;
        for (int j = -radius; j <= radius; ++j) {
            weights.push_back(math::pinnedExp(-static_cast<double>(j * j) /
                (2.0 * static_cast<double>(sigma) * static_cast<double>(sigma))));
            total = total + weights.back();
        }
        for (auto& weight : weights) weight = weight / total;
        std::vector<double> across(height.size()), down(height.size());
        for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
            double sum = 0;
            for (int j = -radius; j <= radius; ++j) sum = sum + at(height, static_cast<i64>(x) + j, y) * weights[static_cast<std::size_t>(j + radius)];
            across[static_cast<std::size_t>(y) * w + x] = sum;
        }
        for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
            double sum = 0;
            for (int j = -radius; j <= radius; ++j) sum = sum + at(across, x, static_cast<i64>(y) + j) * weights[static_cast<std::size_t>(j + radius)];
            down[static_cast<std::size_t>(y) * w + x] = sum;
        }
        height = down;
    }
    const double k = static_cast<double>(depth) / 255.0;
    std::vector<Color> result;
    for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
        const double gx = (at(height, static_cast<i64>(x) + 1, y) - at(height, static_cast<i64>(x) - 1, y)) * 0.5;
        const double gy = (at(height, x, static_cast<i64>(y) + 1) - at(height, x, static_cast<i64>(y) - 1)) * 0.5;
        const double vx = -(gx * k), vy = gy * k, length = std::sqrt((vx * vx + vy * vy) + 1.0);
        result.push_back({byteOf(vx / length), byteOf(vy / length), byteOf(1.0 / length), alpha[static_cast<std::size_t>(y) * w + x]});
    }
    return result;
}

GenerationResult produce(const std::vector<u8>& bytes, GenerationInputs inputs = {},
                          const DerivedNormalRequest* derived = nullptr) {
    const auto file = loadFile(bytes); validate(file);
    VM vm(file, inputs);
    if (derived) vm.deriveNormal(*derived);
    vm.run();
    return vm.takeResult();
}
ErrorReason failure(const std::vector<u8>& bytes, GenerationInputs inputs = {}) {
    try {
        const auto file = loadFile(bytes); validate(file);
        VM vm(file, inputs); vm.run();
    } catch (const FormatError& error) { return error.reason; }
    return ErrorReason::None;
}

// A 4x3 root whose direction capture holds two pixels; the layer is the
// root's normal output.
std::string publishing(const std::string& exportLine, const std::string& capture = "layer.begin 0 3",
                       const std::string& after = "") {
    return "meta: size=4x3\ntypes: layer\n" + exportLine + "\nfn draw ( ) -> ( ):\nlocals: layer layer\n" + capture +
        "\nconst.i32 1\nconst.i32 1\nconst.color #40c0ffff\npixel\n"
        "const.i32 2\nconst.i32 0\nconst.color #20e08080\npixel\nend\n" + after +
        "local.get 0\nout.layer 0\nreturn\nendfn\n";
}

} // namespace

TEST_CASE("direction bytes encode exactly and re-orient as the D4 linear map", "[normal]") {
    CHECK(detail::directionByte(0.0) == 128);
    CHECK(detail::directionByte(1.0) == 255);
    CHECK(detail::directionByte(-1.0) == 0);
    CHECK(detail::directionByte(2.0) == 255);
    CHECK(detail::directionByte(-2.0) == 0);
    CHECK(detail::directionByte(-0.0) == 128);
    CHECK(detail::directionByte(std::numeric_limits<double>::quiet_NaN()) == 128);
    // An exact tie rounds away from zero on both sides: 127.5 * (2/3) = 85.
    CHECK(detail::directionByte(2.0 / 3.0) == 213);
    CHECK(detail::directionByte(-2.0 / 3.0) == 42);
    for (const double v : {1e-9, 0.1, 0.25, 1.0 / 3.0, 0.5, 0.999, 1.0})
        CHECK(detail::directionByte(v) + detail::directionByte(-v) == 255);
    for (int b = 0; b < 256; ++b) {
        // 255 - b is the exact negation under the reader's b/127.5 - 1.
        const double decoded = b / 127.5 - 1.0;
        CHECK(detail::directionByte(decoded) == b);
        CHECK(detail::directionByte(-decoded) == 255 - b);
    }
    CHECK(kFlatDirection == Color{128, 128, 255, 255});

    for (u8 kind = 0; kind < 8; ++kind) {
        const auto transform = *D4Transform::fromKind(kind);
        for (int r = 0; r < 256; r += 5) for (int g = 0; g < 256; g += 3) {
            const Color pixel{static_cast<u8>(r), static_cast<u8>(g), 201, 77};
            // Stored (x, y up) is the asset vector (x, -y); the pixel map's
            // linear part moves it, then it is stored again.
            const double x = r / 127.5 - 1.0, up = g / 127.5 - 1.0;
            const auto moved = transform.mapPoint({static_cast<f32>(x), static_cast<f32>(-up)}, {0, 0});
            const Color expected{detail::directionByte(moved.x), detail::directionByte(-static_cast<double>(moved.y)), 201, 77};
            CHECK(reorientDirection(pixel, kind) == expected);
        }
        for (u8 next = 0; next < 8; ++next) {
            const auto composed = transform.compose(*D4Transform::fromKind(next));
            for (const auto pixel : {Color{3, 250, 9, 1}, Color{128, 128, 255, 255}, Color{0, 255, 0, 0}})
                CHECK(reorientDirection(reorientDirection(pixel, kind), next) == reorientDirection(pixel, composed.kind()));
        }
    }
}

TEST_CASE("surface.normal matches a second implementation for both receivers", "[normal]") {
    for (const auto size : std::array<RasterPoint, 7>{{{1,1},{1,9},{11,1},{3,7},{8,5},{5,19},{2,2}}})
    for (const f32 sigma : {0.f, 0.25f, 0.75f, 1.f, 2.f, 64.f})
    for (const f32 depth : {0.f, 1.f, 6.5f})
    for (const Vec2 scale : {Vec2{1, 1}, Vec2{2, 0.5f}}) {
        if (sigma == 64 && size.x * size.y > 21) continue;
        CAPTURE(size.x, size.y, sigma, depth, scale.x, scale.y);
        const u32 w = static_cast<u32>(size.x), h = static_cast<u32>(size.y);
        auto image = rgba(w, h, scale);
        std::vector<u8> coverage; std::vector<double> layerHeight, maskHeight;
        for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
            const Color pixel{static_cast<u8>(31 + x * 37 + y * 11), static_cast<u8>(x * 83), 9, static_cast<u8>((x * 71 + y * 53 + 128) % 256)};
            image->raster.at(x, y) = pixel;
            coverage.push_back(pixel.a);
            layerHeight.push_back((static_cast<double>(pixel.r) * static_cast<double>(pixel.a)) / 255.0);
            maskHeight.push_back(pixel.a);
        }
        const auto original = image->raster.pixels;
        NormalExecution fixture;
        const SurfaceMapping mapping{*D4Transform::fromKind(5), {-3, 8}};
        const auto fromLayer = fixture.run(LayerValue{image, mapping}, depth, sigma);
        // The raster scale is carried along and never enters the arithmetic.
        CHECK(fromLayer.buffer->raster.pixels == reference(layerHeight, coverage, w, h, depth, sigma));
        // A mask over RGBA reads its alpha; over A8 its coverage.
        const auto fromAlpha = fixture.run(MaskValue{image, mapping}, depth, sigma);
        const auto fromCoverage = fixture.run(MaskValue{a8(w, h, coverage, scale), mapping}, depth, sigma);
        const auto expected = reference(maskHeight, coverage, w, h, depth, sigma);
        CHECK(fromAlpha.buffer->raster.pixels == expected);
        CHECK(fromCoverage.buffer->raster.pixels == expected);
        for (const auto& result : {fromLayer, fromAlpha, fromCoverage}) {
            CHECK(result.buffer->format == SurfaceBuffer::Format::RGBA8);
            CHECK(result.buffer->encoding == SurfaceBuffer::Encoding::Direction);
            CHECK(result.buffer->raster.width == w); CHECK(result.buffer->raster.height == h);
            CHECK(result.buffer->raster.scaleX == scale.x); CHECK(result.buffer->raster.scaleY == scale.y);
            CHECK(result.mapping.origin == mapping.origin); CHECK(result.mapping.transform == mapping.transform);
        }
        CHECK(image->raster.pixels == original);
        CHECK(image->encoding == SurfaceBuffer::Encoding::Colour);
        CHECK(fixture.ex.frame().stream == 41);
        CHECK(std::all_of(fixture.ex.canvas().pixels.begin(), fixture.ex.canvas().pixels.end(), [](Color c) { return c == Color{}; }));
    }
}

TEST_CASE("surface.normal has pinned bytes, slope signs and flat interiors", "[normal]") {
    NormalExecution fixture;
    // gx = 127.5, k = 1/255: v = (-0.5, 0, 1), |v| = sqrt(1.25).
    const auto step = fixture.run(MaskValue{a8(2, 1, {0, 255}), {}}, 1.f, 0.f);
    CHECK(step.buffer->raster.pixels == std::vector<Color>{{70, 128, 242, 0}, {70, 128, 242, 255}});
    // Height rising rightward faces left; rising downward faces up.
    const auto right = fixture.run(MaskValue{a8(3, 3, {0,100,200, 0,100,200, 0,100,200}), {}}, 2.f, 0.f);
    for (const auto pixel : right.buffer->raster.pixels) { CHECK(pixel.r < 128); CHECK(pixel.g == 128); CHECK(pixel.b < 255); }
    const auto down = fixture.run(MaskValue{a8(3, 3, {0,0,0, 100,100,100, 200,200,200}), {}}, 2.f, 0.f);
    for (const auto pixel : down.buffer->raster.pixels) { CHECK(pixel.r == 128); CHECK(pixel.g > 128); CHECK(pixel.b < 255); }
    // Depth is in pixels: the same bytes at another raster scale give the same directions.
    const auto coarse = fixture.run(MaskValue{a8(3, 1, {0, 100, 200}), {}}, 2.f, 0.f);
    const auto scaled = fixture.run(MaskValue{a8(3, 1, {0, 100, 200}, {4, 0.25f}), {}}, 2.f, 0.f);
    CHECK(scaled.buffer->raster.pixels == coarse.buffer->raster.pixels);
    for (const u8 level : {u8{0}, u8{1}, u8{128}, u8{255}}) for (const f32 sigma : {0.f, 0.5f, 3.f}) {
        const auto flat = fixture.run(MaskValue{a8(4, 3, std::vector<u8>(12, level)), {}}, 9.f, sigma);
        for (const auto pixel : flat.buffer->raster.pixels) CHECK(pixel == Color{128, 128, 255, level});
    }
    // Zero depth is flat whatever the height; alpha is still the coverage.
    const auto none = fixture.run(MaskValue{a8(3, 1, {0, 255, 7}), {}}, 0.f, 1.f);
    CHECK(none.buffer->raster.pixels == std::vector<Color>{{128,128,255,0}, {128,128,255,255}, {128,128,255,7}});
    // A layer's height is premultiplied red: hidden red under zero alpha is no height.
    auto hidden = rgba(3, 1);
    hidden->raster.pixels = {{255, 0, 0, 0}, {255, 0, 0, 0}, {255, 0, 0, 0}};
    const auto level = fixture.run(LayerValue{hidden, {}}, 9.f, 0.f);
    for (const auto pixel : level.buffer->raster.pixels) CHECK(pixel == Color{128, 128, 255, 0});
}

TEST_CASE("surface.normal preflights exact fuel and arena and releases its scratch", "[normal]") {
    for (const bool mask : {false, true}) for (const f32 sigma : {0.f, 1.f}) {
        CAPTURE(mask, sigma);
        const auto source = rgba(5, 19);
        const Value handle = mask ? Value(MaskValue{source, {}}) : Value(LayerValue{source, {}});
        const u64 radius = sigma > 0 ? 3u : 0u, kernel = sigma > 0 ? 7u : 0u;
        const u64 cost = kernel + 5u * 19u * (5u + 2u * kernel);
        const u64 resultBytes = surfaceBytes(5, 19);
        const u64 scratchBytes = 5u * 19u * sizeof(double) + 5u * radius * sizeof(double);
        NormalExecution fixture;
        const auto before = fixture.ex.fuel;
        Value result = fixture.run(handle, 3.f, sigma);
        CHECK(fixture.ex.fuel == before - cost);
        CHECK(fixture.ex.resources.bytes == resultBytes);
        result = i32{0}; CHECK(fixture.ex.resources.bytes == 0);
        for (int i = 0; i < 50; ++i) (void)fixture.run(handle, 3.f, sigma);
        CHECK(fixture.ex.resources.bytes == 0);
        fixture.ex.fuel = cost - 1;
        CHECK(fixture.reason(handle, 3.f, sigma, mask ? 1 : 0) == ErrorReason::FuelExhausted);
        CHECK(fixture.ex.resources.bytes == 0);
        fixture.ex.fuel = 1000000;
        fixture.ex.resources.bytes = kMaxGenerationData - resultBytes - scratchBytes + 1;
        const auto occupied = fixture.ex.resources.bytes;
        CHECK(fixture.reason(handle, 3.f, sigma, mask ? 1 : 0) == ErrorReason::OutOfMemory);
        CHECK(fixture.ex.resources.bytes == occupied);
        fixture.ex.resources.bytes = kMaxGenerationData - resultBytes - scratchBytes;
        (void)fixture.run(handle, 3.f, sigma);
        CHECK(fixture.ex.resources.bytes == kMaxGenerationData - resultBytes - scratchBytes);
        fixture.ex.resources.bytes = 0;
    }
}

TEST_CASE("surface.normal rejects invalid arguments, handles and receivers before publishing", "[normal]") {
    NormalExecution fixture;
    const auto source = rgba(2, 2);
    const auto nan = std::numeric_limits<f32>::quiet_NaN(), infinity = std::numeric_limits<f32>::infinity();
    for (const bool mask : {false, true}) {
        const Value handle = mask ? Value(MaskValue{source, {}}) : Value(LayerValue{source, {}});
        const u8 aux = mask ? 1 : 0;
        for (const f32 smoothing : {-1.f, 64.01f, nan, infinity, -infinity})
            CHECK(fixture.reason(handle, 1.f, smoothing, aux) == ErrorReason::NumericRange);
        for (const f32 depth : {-0.001f, nan, infinity, -infinity})
            CHECK(fixture.reason(handle, depth, 0.f, aux) == ErrorReason::NumericRange);
        // Smoothing is checked before depth, both before the receiver.
        fixture.ex.push(mask ? Value(MaskValue{}) : Value(LayerValue{})); fixture.ex.push(-1.f); fixture.ex.push(99.f);
        try { (void)detail::stepNormalMap(fixture.ex, {Opcode::SurfaceNormal, aux, 0}); FAIL("accepted"); }
        catch (const Trap& error) { CHECK(std::string(error.what()) == "normal smoothing must be finite and in 0..64"); }
        fixture.ex.frame().stack.clear();
        CHECK(fixture.reason(mask ? Value(MaskValue{}) : Value(LayerValue{}), 1.f, 0.f, aux) == ErrorReason::RuntimeViolation);
        CHECK(fixture.reason(handle, -0.f, -0.f, aux) == ErrorReason::None);
        CHECK(fixture.reason(handle, 1.f, 64.f, aux) == ErrorReason::None);
    }
    CHECK(fixture.reason(LayerValue{source, {}}, 1.f, 0.f, 2) == ErrorReason::InvalidInstruction);
    // The receiver kind is the instruction's, not the value's.
    CHECK(fixture.reason(MaskValue{source, {}}, 1.f, 0.f, 0) == ErrorReason::StackType);
    CHECK(fixture.reason(LayerValue{source, {}}, 1.f, 0.f, 1) == ErrorReason::StackType);
    auto directions = rgba(2, 2); directions->encoding = SurfaceBuffer::Encoding::Direction;
    CHECK(fixture.reason(LayerValue{directions, {}}, 1.f, 0.f, 0) == ErrorReason::RuntimeViolation);
    // A mask over a direction buffer is ordinary coverage.
    CHECK(fixture.reason(MaskValue{directions, {}}, 1.f, 0.f, 1) == ErrorReason::None);
    CHECK(fixture.ex.resources.bytes == 0);
}

TEST_CASE("painting a direction layer moves its vectors with its pixels", "[normal]") {
    for (const bool direction : {true, false}) for (u8 kind = 0; kind < 8; ++kind) {
        CAPTURE(direction, kind);
        auto source = rgba(3, 2);
        if (direction) source->encoding = SurfaceBuffer::Encoding::Direction;
        for (u32 y = 0; y < 2; ++y) for (u32 x = 0; x < 3; ++x)
            source->raster.at(x, y) = {static_cast<u8>(10 + 40 * x), static_cast<u8>(200 - 70 * y), static_cast<u8>(90 + x + y), 255};
        const auto transform = *D4Transform::fromKind(kind);
        NormalExecution fixture;
        fixture.ex.target = RenderTarget::create(5, 5, {1, 1}, CompositionMode::SourceOver);
        fixture.ex.spend(6);
        detail::paintSurface(fixture.ex, LayerValue{source, {transform, {1, 1}}});
        const RasterPoint extent{3, 2};
        std::vector<Color> expected(25);
        for (i32 y = 0; y < 2; ++y) for (i32 x = 0; x < 3; ++x) {
            const auto mapped = transform.mapPixel({x, y}, extent);
            const auto pixel = source->raster.at(static_cast<u32>(x), static_cast<u32>(y));
            expected[static_cast<std::size_t>(mapped.y + 1) * 5 + static_cast<std::size_t>(mapped.x + 1)] =
                direction ? reorientDirection(pixel, kind) : pixel;
        }
        CHECK(fixture.ex.canvas().pixels == expected);
        // The same pixels reach a clipped target through the per-pixel path.
        NormalExecution clipped;
        clipped.ex.target = RenderTarget::create(5, 5, {1, 1}, CompositionMode::SourceOver);
        clipped.ex.target.clips.push_back(MaskValue{a8(5, 5, std::vector<u8>(25, 255)), {}});
        clipped.ex.spend(6);
        detail::paintSurface(clipped.ex, LayerValue{source, {transform, {1, 1}}});
        CHECK(clipped.ex.canvas().pixels == expected);
        CHECK(clipped.ex.workBudget->used() >= fixture.ex.workBudget->used());
    }
}

TEST_CASE("the root's normal output is published as a flat-backed auxiliary image", "[normal]") {
    const auto bytes = assemble(publishing("export: normal relief root"));
    const auto result = produce(bytes);
    REQUIRE(result.images.size() == 1);
    const auto& image = result.images[0];
    CHECK(image.kind == AuxImageKind::Normal);
    CHECK(image.source == AuxImageSource::Authored);
    CHECK(image.name == "relief");
    CHECK(image.image.width == 4); CHECK(image.image.height == 3);
    std::vector<Color> expected(12, kFlatDirection);
    expected[1 * 4 + 1] = {0x40, 0xc0, 0xff, 255};
    // Half coverage interpolates toward flat with the source-over kernel.
    expected[0 * 4 + 2] = composePixel(kFlatDirection, {0x20, 0xe0, 0x80, 0x80}, CompositionMode::SourceOver);
    CHECK(image.image.pixels == expected);
    CHECK(std::all_of(image.image.pixels.begin(), image.image.pixels.end(), [](Color c) { return c.a == 255; }));
    // The capture is never painted on the root: the main image stays empty.
    CHECK(std::all_of(result.image.pixels.begin(), result.image.pixels.end(), [](Color c) { return c == Color{}; }));

    // A copied result owns equal images and leaves the VM able to hand over its own.
    const auto file = loadFile(bytes); validate(file);
    VM vm(file, {}); vm.run();
    const auto copied = vm.result();
    REQUIRE(copied.images.size() == 1);
    CHECK(copied.images[0].image.pixels == expected); CHECK(copied.images[0].name == "relief");
    CHECK(vm.takeResult().images[0].image.pixels == expected);

    // The same program with an ordinary layer export spends the same fuel:
    // publication is result packing and costs work only.
    const auto plainBytes = assemble(publishing("export: layer relief root"));
    const auto plainFile = loadFile(plainBytes); validate(plainFile);
    VM plain(plainFile, {}); plain.run();
    VM flagged(file, {}); flagged.run();
    CHECK(plain.fuel() == flagged.fuel());
    CHECK(plain.takeResult().images.empty());
    // 12 initialized pixels, then 3 rows of 1 + 4 steps, then the name.
    CHECK(plain.work() - flagged.work() == 12u + 3u * 5u + 6u);
}

TEST_CASE("normal outputs publish nothing when unwritten and reject colour layers", "[normal]") {
    const std::string unwritten = "meta: size=2x2\ntypes: layer\nexport: normal relief root\nfn draw ( ) -> ( ):\nreturn\nendfn\n";
    CHECK(produce(assemble(unwritten)).images.empty());
    // A colour capture (modes 0-2) is not a direction layer.
    for (const std::string capture : {"layer.begin 0", "layer.begin 0 1", "layer.begin 0 2"}) {
        const auto file = loadFile(assemble(publishing("export: normal relief root", capture)));
        validate(file); VM vm(file, {});
        try { vm.run(); FAIL("colour layer accepted"); }
        catch (const Trap& error) {
            CHECK(error.reason == ErrorReason::InvalidOutput);
            CHECK(std::string(error.what()) == "normal output needs a direction layer");
        }
        CHECK_THROWS_AS(vm.takeResult(), std::logic_error);
    }
    // An ordinary layer export accepts either encoding.
    CHECK(failure(assemble(publishing("export: layer relief root"))) == ErrorReason::None);
    // A blurred direction layer is still one.
    const auto blurred = produce(assemble(publishing("export: normal relief root", "layer.begin 0 3",
        "local.get 0\nconst.f32 1.0\nlayer.blur\nlocal.set 0\n")));
    REQUIRE(blurred.images.size() == 1);
    CHECK(blurred.images[0].image.at(1, 1) != kFlatDirection);
    // weather has no meaning on unit vectors.
    const std::string weather = "meta: size=2x2\ntypes: layer\nfn draw ( ) -> ( ):\nlocals: layer\nlayer.begin 0 3\n"
        "const.i32 7\nconst.i32 3\nweather\nend\nreturn\nendfn\n";
    const auto weatherFile = loadFile(assemble(weather)); validate(weatherFile);
    VM weatherVm(weatherFile, {});
    try { weatherVm.run(); FAIL("weather ran on directions"); }
    catch (const Trap& error) { CHECK(std::string(error.what()) == "weather cannot run in a direction capture"); }
    // Dynamic parts keep colour layers only.
    const std::string dynamic = "meta: size=2x2\ntypes: layer part2d\nfn draw ( ) -> ( ):\nlocals: layer\nlayer.begin 0 3\n"
        "const.i32 0\nconst.i32 0\nconst.color #8080ffff\npixel\nend\nlocal.get 0\npart.create.layer\ndrop\nreturn\nendfn\n";
    const auto dynamicFile = loadFile(assemble(dynamic)); validate(dynamicFile);
    VM dynamicVm(dynamicFile, {});
    try { dynamicVm.run(); FAIL("dynamic part took directions"); }
    catch (const Trap& error) { CHECK(std::string(error.what()) == "dynamic parts do not support direction layers"); }
}

TEST_CASE("surface.normal runs under the 60 percent tariff and feeds a normal output", "[normal]") {
    // A 4x4 root: an opaque 2x2 block, its alpha as height, published.
    const std::string source = "meta: size=4x4\ntypes: layer mask\nexport: normal relief root\nfn draw ( ) -> ( ):\nlocals: layer\n"
        "layer.begin 0 2\nconst.f32 1.0\nconst.f32 1.0\nconst.f32 2.0\nconst.f32 2.0\nconst.color #ffffffff\nrect\nend\n";
    const std::string tail = "out.layer 0\nreturn\nendfn\n";
    const auto withKernel = assemble(source + "local.get 0\nmask.from\nconst.f32 4.0\nconst.f32 0.0\nsurface.normal 1\n" + tail);
    const auto result = produce(withKernel);
    REQUIRE(result.images.size() == 1);
    const auto& pixels = result.images[0].image;
    // Outside the block the result's alpha is zero: flat. Inside, the block's
    // edges face outward: left column faces left, top row faces up.
    CHECK(pixels.at(0, 0) == kFlatDirection); CHECK(pixels.at(3, 3) == kFlatDirection);
    CHECK(pixels.at(1, 1).r < 128); CHECK(pixels.at(1, 1).g > 128);
    CHECK(pixels.at(2, 1).r > 128); CHECK(pixels.at(2, 1).g > 128);
    CHECK(pixels.at(1, 2).r < 128); CHECK(pixels.at(1, 2).g < 128);
    CHECK(pixels.at(2, 2).r > 128); CHECK(pixels.at(2, 2).g < 128);
    CHECK(pixels.at(1, 1).r + pixels.at(2, 1).r == 255);
    CHECK(pixels.at(1, 1).g + pixels.at(1, 2).g == 255);

    // Fuel: the instruction's own unit joins its 16 * 5 kernel units at 60%.
    const auto fuelOf = [](const std::vector<u8>& bytes) {
        const auto file = loadFile(bytes); validate(file);
        VM vm(file, {}); vm.run();
        return vm.fuelBudget() - vm.fuel();
    };
    const auto base = assemble(source + "local.get 0\nmask.from\nconst.f32 4.0\nconst.f32 0.0\ndrop\ndrop\ndrop\nreturn\nendfn\n");
    const auto kernelOnly = assemble(source + "local.get 0\nmask.from\nconst.f32 4.0\nconst.f32 0.0\nsurface.normal 1\ndrop\nreturn\nendfn\n");
    // base spends three drops; kernelOnly one drop and the kernel.
    CHECK(fuelOf(kernelOnly) + 2u == fuelOf(base) + (1u + 16u * 5u) * 60u / 100u + ((1u + 16u * 5u) * 60u % 100u ? 1u : 0u));
}

TEST_CASE("normal exports and new aux values survive tools and reject malformed wire data", "[normal]") {
    const auto bytes = assemble(publishing("export: normal relief root", "layer.begin 0 3",
        "local.get 0\nmask.from\nconst.f32 1.0\nconst.f32 0.5\nsurface.normal 1\nlocal.set 1\n"
        "local.get 1\nconst.f32 2.0\nconst.f32 0.0\nsurface.normal\ndrop\n"));
    const auto file = loadFile(bytes);
    REQUIRE(file.resourceExports.size() == 1);
    CHECK(file.resourceExports[0].normal);
    CHECK(file.resourceExports[0].type == ValueType::Layer);
    const auto text = disassemble(bytes);
    CHECK(text.find("export: normal relief root") != std::string::npos);
    CHECK(text.find("layer.begin 0 3") != std::string::npos);
    CHECK(text.find("surface.normal 1") != std::string::npos);
    CHECK(text.find("surface.normal\n") != std::string::npos);
    CHECK(assemble(text) == bytes);
    // The second kernel reads a direction layer as height: a runtime trap.
    CHECK(failure(bytes) == ErrorReason::RuntimeViolation);

    const auto record = file.layout.find(SectionId::ResourceExports)->offset + 4;
    // Bits 1-4 are the material maps of v1.2; two image bits never load.
    for (const u16 flags : {u16{3}, u16{0x20}, u16{0x8000}, u16{0xffff}}) {
        auto bad = bytes; patch16(bad, record + 10, flags);
        CHECK_THROWS_AS(loadFile(bad), FormatError);
    }
    auto plain = bytes; patch16(plain, record + 10, 0);
    CHECK_FALSE(loadFile(plain).resourceExports[0].normal);

    // A normal output is a layer, and an owner has one.
    CHECK_THROWS_AS(assemble("meta: size=1x1\ntypes: layer\nexport: normal a root\nexport: normal b root\n"
        "fn draw ( ) -> ( ):\nreturn\nendfn\n"), std::exception);
    for (const std::string other : {"layer", "mask"}) {
        const auto mixed = assemble("meta: size=1x1\ntypes: layer mask\nexport: normal a root\nexport: " + other + " b root\n"
            "fn draw ( ) -> ( ):\nreturn\nendfn\n");
        const auto second = loadFile(mixed).layout.find(SectionId::ResourceExports)->offset + 4 + 12;
        auto twice = mixed; patch16(twice, second + 10, 1);
        try { (void)loadFile(twice); FAIL("second normal output accepted"); }
        catch (const FormatError& error) {
            CHECK(error.reason == ErrorReason::InvalidMetadata);
            CHECK(std::string(error.what()) == (other == "mask" ? "a normal output must be a layer export"
                                                                : "an asset declares more than one normal output"));
        }
    }

    for (std::size_t i = 0; i < file.code.functions[0].code.size() / 4; ++i) {
        const auto at = file.code.functions[0].fileOffset + i * 4;
        const auto ins = decodeInstruction(file.code.functions[0].code.subspan(i * 4, 4));
        if (ins.opcode == Opcode::LayerBegin) {
            for (const u8 aux : {u8{4}, u8{0x83}, u8{255}}) { auto bad = bytes; bad.at(at + 1) = aux; CHECK_THROWS_AS(validate(loadFile(bad)), FormatError); }
        }
        if (ins.opcode == Opcode::SurfaceNormal) {
            for (const u8 aux : {u8{2}, u8{255}}) { auto bad = bytes; bad.at(at + 1) = aux; CHECK_THROWS_AS(validate(loadFile(bad)), FormatError); }
            auto bad = bytes; patch16(bad, at + 2, 1); CHECK_THROWS_AS(validate(loadFile(bad)), FormatError);
            // The receiver kind must match the stack.
            bad = bytes; bad.at(at + 1) = ins.aux ? 0 : 1; CHECK_THROWS_AS(validate(loadFile(bad)), FormatError);
        }
        if (ins.opcode == Opcode::SurfacePaint || ins.opcode == Opcode::SurfacePaintMask) {
            auto bad = bytes; patch16(bad, at + 2, 3); CHECK_THROWS_AS(validate(loadFile(bad)), FormatError);
        }
    }
    // Aux values are checked in unreachable code too; stack types where reachable.
    for (const std::string dead : {"layer.begin 0 4\nend\n", "surface.normal 2\n"})
        CHECK_THROWS_AS(assemble("meta: size=1x1\ntypes: layer mask\nfn draw ( ) -> ( ):\nlocals: layer\nreturn\n" + dead + "endfn\n"), std::exception);
    for (const std::string live : {"surface.normal\n", "const.f32 1\nconst.f32 1\nsurface.normal 1\n",
            "const.f32 1\nconst.f32 1\nconst.f32 1\nsurface.normal\n", "local.get 0\nconst.f32 1\nconst.i32 1\nsurface.normal\n",
            "local.get 0\nconst.f32 1\nconst.f32 1\nsurface.normal\n"})
        CHECK_THROWS_AS(assemble("meta: size=1x1\ntypes: layer mask\nfn draw ( ) -> ( ):\nlocals: layer\n" + live + "return\nendfn\n"), std::exception);
    CHECK_NOTHROW(assemble("meta: size=1x1\ntypes: layer mask\nfn draw ( ) -> ( ):\nlocals: layer\n"
        "local.get 0\nconst.f32 1\nconst.f32 1\nsurface.normal\ndrop\nreturn\nendfn\n"));
}

TEST_CASE("a host can ask for a derived normal image when the program publishes none", "[normal]") {
    // An opaque 4x4 block in an 8x8 image.
    const std::string source = "meta: size=8x8\nfn draw ( ) -> ( ):\n"
        "const.f32 2.0\nconst.f32 2.0\nconst.f32 4.0\nconst.f32 4.0\nconst.color #c08040ff\nrect\nreturn\nendfn\n";
    const auto bytes = assemble(source);
    CHECK(produce(bytes).images.empty());
    DerivedNormalRequest request{};
    const auto derived = produce(bytes, {}, &request);
    REQUIRE(derived.images.size() == 1);
    const auto& image = derived.images[0];
    CHECK(image.kind == AuxImageKind::Normal);
    CHECK(image.source == AuxImageSource::Derived);
    CHECK(image.name == "normal");
    CHECK(image.image.width == 8); CHECK(image.image.height == 8);
    CHECK(std::all_of(image.image.pixels.begin(), image.image.pixels.end(), [](Color c) { return c.a == 255 && c.b > 128; }));
    // A pillow: its left edge faces left, its top edge faces up, and the
    // block is symmetric about both axes.
    CHECK(image.image.at(2, 3).r < 128); CHECK(image.image.at(5, 3).r > 128);
    CHECK(image.image.at(3, 2).g > 128); CHECK(image.image.at(3, 5).g < 128);
    // Mirrored pixels are exact complements, or both flat where the slope is zero.
    const auto mirrored = [](u8 a, u8 b) { return a + b == 255 || (a == 128 && b == 128); };
    for (u32 y = 0; y < 8; ++y) for (u32 x = 0; x < 8; ++x) {
        CHECK(mirrored(image.image.at(x, y).r, image.image.at(7 - x, y).r));
        CHECK(mirrored(image.image.at(x, y).g, image.image.at(x, 7 - y).g));
    }
    // The main image and the fuel are those of an ordinary generation.
    CHECK(derived.image.pixels == produce(bytes).image.pixels);
    const auto file = loadFile(bytes); validate(file);
    VM plain(file, {}); plain.run();
    VM asked(file, {}); asked.deriveNormal(request); asked.run();
    CHECK_THROWS_AS(asked.deriveNormal(request), std::logic_error);
    CHECK(plain.fuel() == asked.fuel());
    CHECK(asked.work() < plain.work());

    // Explicit parameters differ from the defaults; detail reads brightness.
    request = DerivedNormalRequest{12.f, 0.5f, 0.f};
    const auto sharp = produce(bytes, {}, &request);
    CHECK(sharp.images[0].image.pixels != image.image.pixels);
    request = DerivedNormalRequest{12.f, 0.5f, 1.f};
    CHECK(produce(bytes, {}, &request).images[0].image.pixels != sharp.images[0].image.pixels);
    // A transparent image is flat.
    const auto empty = assemble("meta: size=3x2\nfn draw ( ) -> ( ):\nreturn\nendfn\n");
    request = DerivedNormalRequest{};
    const auto flat = produce(empty, {}, &request);
    REQUIRE(flat.images.size() == 1);
    CHECK(std::all_of(flat.images[0].image.pixels.begin(), flat.images[0].image.pixels.end(), [](Color c) { return c == kFlatDirection; }));

    // An authored normal image wins; an unwritten slot does not count.
    const auto authored = produce(assemble(publishing("export: normal relief root")), {}, &request);
    REQUIRE(authored.images.size() == 1);
    CHECK(authored.images[0].source == AuxImageSource::Authored);
    const auto unwritten = produce(assemble("meta: size=2x2\ntypes: layer\nexport: normal relief root\nfn draw ( ) -> ( ):\nreturn\nendfn\n"), {}, &request);
    REQUIRE(unwritten.images.size() == 1);
    CHECK(unwritten.images[0].source == AuxImageSource::Derived);

    // Invalid parameters are host argument errors before execution.
    const auto nan = std::numeric_limits<f32>::quiet_NaN(), infinity = std::numeric_limits<f32>::infinity();
    for (const auto invalid : {DerivedNormalRequest{-1.f, 0.f, 0.f}, DerivedNormalRequest{nan, 0.f, 0.f},
            DerivedNormalRequest{infinity, 0.f, 0.f}, DerivedNormalRequest{1.f, -0.5f, 0.f}, DerivedNormalRequest{1.f, 64.5f, 0.f},
            DerivedNormalRequest{1.f, nan, 0.f}, DerivedNormalRequest{1.f, 1.f, -0.1f}, DerivedNormalRequest{1.f, 1.f, 1.1f},
            DerivedNormalRequest{1.f, 1.f, nan}}) {
        CHECK_FALSE(validDerivedNormalRequest(invalid));
        VM vm(file, {});
        try { vm.deriveNormal(invalid); FAIL("invalid request accepted"); }
        catch (const Trap& error) { CHECK(error.reason == ErrorReason::InvalidArgument); CHECK(error.stage == ErrorStage::Arguments); }
    }
    CHECK(validDerivedNormalRequest({0.f, 64.f, 1.f}));
}

TEST_CASE("publication respects the work budget and publishes nothing on failure", "[normal]") {
    const auto bytes = assemble(publishing("export: normal relief root"));
    const auto file = loadFile(bytes); validate(file);
    VM measured(file, {}); measured.run();
    const auto needed = measured.workBudget() - measured.work();
    GenerationInputs inputs; inputs.limits.workLimit = needed;
    CHECK(produce(bytes, inputs).images.size() == 1);
    for (const u64 short_ : {u64{1}, u64{6}, u64{20}}) {
        inputs.limits.workLimit = needed - short_;
        VM vm(file, inputs);
        try { vm.run(); FAIL("work limit ignored"); }
        catch (const Trap& error) {
            CHECK(error.reason == ErrorReason::WorkExhausted);
            CHECK(error.location.flags == 0u);
        }
        CHECK_THROWS_AS(vm.takeResult(), std::logic_error);
        CHECK_THROWS_AS(vm.result(), std::logic_error);
    }
}
