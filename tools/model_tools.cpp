#include "model_tools.hpp"

#include "json.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>

namespace iprop::tools {
namespace {

struct V3 {
    double x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 unit(V3 a) {
    const double length = std::sqrt(dot(a, a));
    return length > 0.0 ? a * (1.0 / length) : V3{0, 0, 1};
}

std::string hex64(u64 value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

u64 imageHash(const Canvas& canvas) {
    static_assert(sizeof(Color) == 4);
    return model::fnv1a64({reinterpret_cast<const u8*>(canvas.pixels.data()), canvas.pixels.size() * 4});
}

// ---- glTF -------------------------------------------------------------------

std::string number(double value) {
    char text[40];
    std::snprintf(text, sizeof(text), "%.9g", value);
    return text;
}

struct Binary {
    std::vector<u8> bytes;
    std::string views; // the bufferViews array, without brackets
    u32 viewCount = 0;

    // Appends a view, padded to four bytes, and returns its index.
    u32 add(const void* data, std::size_t size, int target = 0) {
        const std::size_t offset = bytes.size();
        const auto* begin = static_cast<const u8*>(data);
        bytes.insert(bytes.end(), begin, begin + size);
        while (bytes.size() % 4 != 0) bytes.push_back(0);
        if (viewCount) views += ",";
        views += "{\"buffer\":0,\"byteOffset\":" + std::to_string(offset) + ",\"byteLength\":" + std::to_string(size);
        if (target) views += ",\"target\":" + std::to_string(target);
        views += "}";
        return viewCount++;
    }
};

void appendChunk(std::vector<u8>& out, u32 type, const std::vector<u8>& data) {
    const auto word = [&](u32 value) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>(value >> (8 * i)));
    };
    word(static_cast<u32>(data.size()));
    word(type);
    out.insert(out.end(), data.begin(), data.end());
}

} // namespace

bool parseView(const std::string& text, PreviewOptions& options) {
    const auto comma = text.find(',');
    if (comma == std::string::npos) return false;
    const auto number = [](const std::string& part, f32& out) {
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), out);
        return !part.empty() && parsed.ec == std::errc{} && parsed.ptr == part.data() + part.size() && std::isfinite(out);
    };
    f32 yaw = 0.f, pitch = 0.f;
    if (!number(text.substr(0, comma), yaw) || !number(text.substr(comma + 1), pitch)) return false;
    if (pitch < -89.f || pitch > 89.f || std::fabs(yaw) > 36000.f) return false;
    options.yaw = yaw;
    options.pitch = pitch;
    return true;
}

bool isModelFile(std::span<const u8> bytes) {
    return bytes.size() >= 4 && std::equal(model::kModelDialect.magic, model::kModelDialect.magic + 4, bytes.begin());
}

std::vector<u8> encodeGlb(const model::ModelResult& result) {
    Binary binary;
    std::string accessors, primitives, materials, textures, images, samplers;
    u32 accessorCount = 0;
    const auto accessor = [&](u32 view, int componentType, std::size_t count, const char* type, const std::string& extra = {}) {
        if (accessorCount) accessors += ",";
        accessors += "{\"bufferView\":" + std::to_string(view) + ",\"componentType\":" + std::to_string(componentType) +
                     ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"" + extra + "}";
        return accessorCount++;
    };

    // One image per texture, per normal image, per packed occlusion-
    // roughness-metallic image and per emission map; a sampler per texture.
    struct Images { int color = -1, normal = -1, orm = -1, emission = -1; bool occlusion = false; };
    std::vector<Images> made(result.textures.size());
    u32 imageCount = 0, textureCount = 0;
    const auto image = [&](const Canvas& canvas, u32 sampler) {
        const auto png = encodePng(canvasImage(canvas));
        const u32 view = binary.add(png.data(), png.size());
        if (imageCount) images += ",";
        images += "{\"bufferView\":" + std::to_string(view) + ",\"mimeType\":\"image/png\"}";
        if (textureCount) textures += ",";
        textures += "{\"sampler\":" + std::to_string(sampler) + ",\"source\":" + std::to_string(imageCount++) + "}";
        return static_cast<int>(textureCount++);
    };
    // Samplers: repeat or clamp, smooth or crisp. 9729 LINEAR, 9987
    // LINEAR_MIPMAP_LINEAR, 9728 NEAREST, 10497 REPEAT, 33071 CLAMP_TO_EDGE.
    for (int i = 0; i < 4; ++i) {
        const bool repeat = i & 1, crisp = i & 2;
        if (i) samplers += ",";
        samplers += std::string("{\"magFilter\":") + (crisp ? "9728" : "9729") + ",\"minFilter\":" +
                    (crisp ? "9728" : "9987") + ",\"wrapS\":" + (repeat ? "10497" : "33071") + ",\"wrapT\":" +
                    (repeat ? "10497" : "33071") + "}";
    }

    for (std::size_t s = 0; s < result.surfaces.size(); ++s) {
        const auto& surface = result.surfaces[s];
        const auto& texture = result.textures[surface.texture];
        const u32 sampler = (texture.repeats ? 1u : 0u) | (surface.crisp ? 2u : 0u);
        // Crisp and smooth uses of one texture are separate glTF textures.
        Images use;
        if (!surface.crisp && made[surface.texture].color >= 0) {
            use = made[surface.texture];
        } else {
            use.color = image(texture.color, sampler);
            const Canvas* maps[6] = {};
            for (const auto& extra : texture.images) {
                const u32 kind = static_cast<u32>(extra.kind);
                if (kind <= 5 && !maps[kind]) maps[kind] = &extra.image;
            }
            if (maps[1]) use.normal = image(*maps[1], sampler);
            // glTF reads occlusion from red, roughness from green and
            // metallic from blue; an absent map is 255, so its factor stands.
            const Canvas* scalar[3] = {maps[static_cast<u32>(AuxImageKind::Ao)], maps[static_cast<u32>(AuxImageKind::Roughness)],
                                       maps[static_cast<u32>(AuxImageKind::Metallic)]};
            if (scalar[0] || scalar[1] || scalar[2]) {
                Canvas orm;
                orm.width = texture.color.width;
                orm.height = texture.color.height;
                orm.pixels.assign(texture.color.pixels.size(), Color{255, 255, 255, 255});
                for (std::size_t i = 0; i < orm.pixels.size(); ++i) {
                    if (scalar[0]) orm.pixels[i].r = scalar[0]->pixels[i].r;
                    if (scalar[1]) orm.pixels[i].g = scalar[1]->pixels[i].r;
                    if (scalar[2]) orm.pixels[i].b = scalar[2]->pixels[i].r;
                }
                use.orm = image(orm, sampler);
                use.occlusion = scalar[0] != nullptr;
            }
            if (maps[static_cast<u32>(AuxImageKind::Emission)]) use.emission = image(*maps[static_cast<u32>(AuxImageKind::Emission)], sampler);
            if (!surface.crisp) made[surface.texture] = use;
        }
        const int color = use.color, normal = use.normal;

        const std::size_t count = surface.positions.size() / 3;
        f32 low[3] = {0, 0, 0}, high[3] = {0, 0, 0};
        for (std::size_t i = 0; i < count; ++i)
            for (int axis = 0; axis < 3; ++axis) {
                const f32 value = surface.positions[i * 3 + axis];
                if (i == 0 || value < low[axis]) low[axis] = value;
                if (i == 0 || value > high[axis]) high[axis] = value;
            }
        const std::string bounds = ",\"min\":[" + number(low[0]) + "," + number(low[1]) + "," + number(low[2]) + "],\"max\":[" +
                                   number(high[0]) + "," + number(high[1]) + "," + number(high[2]) + "]";
        const u32 position = accessor(binary.add(surface.positions.data(), surface.positions.size() * 4, 34962), 5126, count, "VEC3", bounds);
        const u32 normals = accessor(binary.add(surface.normals.data(), surface.normals.size() * 4, 34962), 5126, count, "VEC3");
        const u32 tangents = accessor(binary.add(surface.tangents.data(), surface.tangents.size() * 4, 34962), 5126, count, "VEC4");
        const u32 uvs = accessor(binary.add(surface.uvs.data(), surface.uvs.size() * 4, 34962), 5126, count, "VEC2");
        const u32 indices = accessor(binary.add(surface.indices.data(), surface.indices.size() * 4, 34963), 5125, surface.indices.size(), "SCALAR");

        if (s) {
            primitives += ",";
            materials += ",";
        }
        primitives += "{\"attributes\":{\"POSITION\":" + std::to_string(position) + ",\"NORMAL\":" + std::to_string(normals) +
                      ",\"TANGENT\":" + std::to_string(tangents) + ",\"TEXCOORD_0\":" + std::to_string(uvs) +
                      "},\"indices\":" + std::to_string(indices) + ",\"material\":" + std::to_string(s) + "}";
        materials += "{\"name\":\"" + jsonEscape(texture.name) + "\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":" +
                     std::to_string(color) + "},\"metallicFactor\":" + number(surface.metallic) + ",\"roughnessFactor\":" +
                     number(surface.roughness);
        if (use.orm >= 0) materials += ",\"metallicRoughnessTexture\":{\"index\":" + std::to_string(use.orm) + "}";
        materials += "}";
        if (normal >= 0) materials += ",\"normalTexture\":{\"index\":" + std::to_string(normal) + "}";
        if (use.occlusion) materials += ",\"occlusionTexture\":{\"index\":" + std::to_string(use.orm) + "}";
        if (surface.emission > 0.f) {
            // The emission map when the texture has one, else its colour.
            const double factor = std::min(1.0, static_cast<double>(surface.emission));
            materials += ",\"emissiveTexture\":{\"index\":" + std::to_string(use.emission >= 0 ? use.emission : color) +
                         "},\"emissiveFactor\":[" + number(factor) + "," + number(factor) + "," + number(factor) + "]";
        }
        if (surface.alpha == model::SkinAlpha::Cutout)
            materials += ",\"alphaMode\":\"MASK\",\"alphaCutoff\":" + number(surface.cutoff);
        if (surface.alpha == model::SkinAlpha::Blend) materials += ",\"alphaMode\":\"BLEND\"";
        if (surface.twoSided) materials += ",\"doubleSided\":true";
        materials += "}";
    }

    const double scale = 1.0 / static_cast<double>(result.unitsPerMeter);
    std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"ipgen (Infinite Props)\"},\"scene\":0,"
                       "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"name\":\"model\",\"scale\":[" +
                       number(scale) + "," + number(scale) + "," + number(scale) + "]";
    if (!result.surfaces.empty()) json += ",\"mesh\":0";
    json += "}]";
    if (!result.surfaces.empty()) {
        json += ",\"meshes\":[{\"primitives\":[" + primitives + "]}],\"materials\":[" + materials + "]";
        json += ",\"textures\":[" + textures + "],\"images\":[" + images + "],\"samplers\":[" + samplers + "]";
        json += ",\"accessors\":[" + accessors + "],\"bufferViews\":[" + binary.views + "]";
        json += ",\"buffers\":[{\"byteLength\":" + std::to_string(binary.bytes.size()) + "}]";
    }
    json += "}";

    std::vector<u8> text(json.begin(), json.end());
    while (text.size() % 4 != 0) text.push_back(' ');
    std::vector<u8> out{'g', 'l', 'T', 'F', 2, 0, 0, 0, 0, 0, 0, 0};
    appendChunk(out, 0x4E4F534A, text); // JSON
    if (!binary.bytes.empty()) appendChunk(out, 0x004E4942, binary.bytes); // BIN
    const u32 total = static_cast<u32>(out.size());
    for (int i = 0; i < 4; ++i) out[8 + i] = static_cast<u8>(total >> (8 * i));
    return out;
}

// ---- the preview ------------------------------------------------------------

Image renderPreview(const model::ModelResult& result, const PreviewOptions& options) {
    constexpr u32 kSamples = 2; // supersampling per axis
    const u32 width = options.width * kSamples, height = options.height * kSamples;
    const double yaw = options.yaw * 3.141592653589793 / 180.0, pitch = options.pitch * 3.141592653589793 / 180.0;
    // The camera stands at `toward` from the model and looks back at it.
    const V3 toward = unit({std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)});
    const V3 right = unit(cross({0, 1, 0}, toward));
    const V3 up = cross(toward, right);
    const V3 light = unit(right * -0.45 + up * 0.75 + toward * 0.55);
    const V3 fill = unit(right * 0.8 + up * 0.15 + toward * 0.6);

    // Frame the nominal box together with what was actually built.
    V3 low{-result.size[0] * 0.5, 0.0, -result.size[2] * 0.5}, high{result.size[0] * 0.5, result.size[1], result.size[2] * 0.5};
    if (result.vertexCount) {
        low = {std::min<double>(low.x, result.boundsMin[0]), std::min<double>(low.y, result.boundsMin[1]),
               std::min<double>(low.z, result.boundsMin[2])};
        high = {std::max<double>(high.x, result.boundsMax[0]), std::max<double>(high.y, result.boundsMax[1]),
                std::max<double>(high.z, result.boundsMax[2])};
    }
    const V3 centre = (low + high) * 0.5;
    // The box fills the image in the direction that runs out of room first.
    double across = 1e-9, along = 1e-9;
    for (int corner = 0; corner < 8; ++corner) {
        const V3 p{corner & 1 ? high.x : low.x, corner & 2 ? high.y : low.y, corner & 4 ? high.z : low.z};
        across = std::max(across, std::fabs(dot(p - centre, right)));
        along = std::max(along, std::fabs(dot(p - centre, up)));
    }
    const double pixels = 0.46 * std::min(width / across, height / along);

    std::vector<double> depth(static_cast<std::size_t>(width) * height, -std::numeric_limits<double>::infinity());
    std::vector<std::array<double, 4>> colour(depth.size(), {0, 0, 0, 0});

    struct Corner {
        double sx, sy, sz;
        V3 n, t;
        double w, u, v;
    };
    const V3 half = unit(light + toward);
    // Opaque and cutout surfaces first; blended ones over them, in surface
    // order, testing depth without writing it. A preview, not a contract.
    for (int pass = 0; pass < 2; ++pass)
    for (const auto& surface : result.surfaces) {
        const bool blend = surface.alpha == model::SkinAlpha::Blend;
        if (blend != (pass == 1)) continue;
        const auto& texture = result.textures[surface.texture];
        const Canvas* maps[6] = {};
        for (const auto& extra : texture.images) {
            const u32 kind = static_cast<u32>(extra.kind);
            if (kind <= 5 && !maps[kind]) maps[kind] = &extra.image;
        }
        const Canvas* normalMap = maps[static_cast<u32>(AuxImageKind::Normal)];
        const Canvas* roughnessMap = maps[static_cast<u32>(AuxImageKind::Roughness)];
        const Canvas* metallicMap = maps[static_cast<u32>(AuxImageKind::Metallic)];
        const Canvas* occlusionMap = maps[static_cast<u32>(AuxImageKind::Ao)];
        const Canvas* emissionMap = maps[static_cast<u32>(AuxImageKind::Emission)];
        const auto corner = [&](u32 index) {
            const V3 p{surface.positions[index * 3], surface.positions[index * 3 + 1], surface.positions[index * 3 + 2]};
            const V3 q = p - centre;
            return Corner{width * 0.5 + dot(q, right) * pixels, height * 0.5 - dot(q, up) * pixels, dot(q, toward),
                          {surface.normals[index * 3], surface.normals[index * 3 + 1], surface.normals[index * 3 + 2]},
                          {surface.tangents[index * 4], surface.tangents[index * 4 + 1], surface.tangents[index * 4 + 2]},
                          surface.tangents[index * 4 + 3], surface.uvs[index * 2], surface.uvs[index * 2 + 1]};
        };
        const auto texel = [&](const Canvas& canvas, double u, double v) {
            const auto wrap = [&](double coordinate, u32 size) {
                if (texture.repeats) coordinate -= std::floor(coordinate);
                const double scaled = std::clamp(coordinate, 0.0, 1.0) * size;
                return std::min(size - 1, static_cast<u32>(scaled));
            };
            return canvas.at(wrap(u, canvas.width), wrap(v, canvas.height));
        };
        for (std::size_t i = 0; i + 2 < surface.indices.size(); i += 3) {
            const Corner a = corner(surface.indices[i]), b = corner(surface.indices[i + 1]), c = corner(surface.indices[i + 2]);
            // Twice the signed area on screen; y runs down, so a front face is negative.
            const double area = (b.sx - a.sx) * (c.sy - a.sy) - (b.sy - a.sy) * (c.sx - a.sx);
            if (area == 0.0 || (area > 0.0 && !surface.twoSided)) continue;
            const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.sx, b.sx, c.sx}))));
            const int x1 = std::min(static_cast<int>(width) - 1, static_cast<int>(std::ceil(std::max({a.sx, b.sx, c.sx}))));
            const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.sy, b.sy, c.sy}))));
            const int y1 = std::min(static_cast<int>(height) - 1, static_cast<int>(std::ceil(std::max({a.sy, b.sy, c.sy}))));
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const double px = x + 0.5, py = y + 0.5;
                    const double wa = ((b.sx - px) * (c.sy - py) - (b.sy - py) * (c.sx - px)) / area;
                    const double wb = ((c.sx - px) * (a.sy - py) - (c.sy - py) * (a.sx - px)) / area;
                    const double wc = 1.0 - wa - wb;
                    if (wa < 0.0 || wb < 0.0 || wc < 0.0) continue;
                    const double z = a.sz * wa + b.sz * wb + c.sz * wc;
                    const std::size_t at = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
                    if (z <= depth[at]) continue;
                    const double u = a.u * wa + b.u * wb + c.u * wc, v = a.v * wa + b.v * wb + c.v * wc;
                    const Color albedo = texel(texture.color, u, v);
                    if (surface.alpha == model::SkinAlpha::Cutout && albedo.a < surface.cutoff * 255.0) continue;
                    V3 n = unit(a.n * wa + b.n * wb + c.n * wc);
                    if (area > 0.0) n = n * -1.0; // the back of a two-sided face
                    if (normalMap) {
                        const Color stored = texel(*normalMap, u, v);
                        const V3 t = unit(a.t * wa + b.t * wb + c.t * wc);
                        const V3 bitangent = cross(n, t) * (a.w * wa + b.w * wb + c.w * wc);
                        const V3 local{stored.r / 127.5 - 1.0, stored.g / 127.5 - 1.0, stored.b / 127.5 - 1.0};
                        n = unit(t * local.x + bitangent * local.y + n * local.z);
                    }
                    // Lambert light with ambient occlusion, a Blinn-Phong
                    // highlight that narrows as roughness falls and takes
                    // the albedo's tint on metal, a sky-to-ground reflection
                    // that metal shows in its own colour, then emission.
                    const double roughness = surface.roughness * (roughnessMap ? texel(*roughnessMap, u, v).r / 255.0 : 1.0);
                    const double metallic = surface.metallic * (metallicMap ? texel(*metallicMap, u, v).r / 255.0 : 1.0);
                    const double occlusion = occlusionMap ? texel(*occlusionMap, u, v).r / 255.0 : 1.0;
                    const double diffuse = 0.30 * occlusion + 0.74 * std::max(0.0, dot(n, light)) + 0.22 * std::max(0.0, dot(n, fill));
                    const double gloss = (1.0 - roughness) * (1.0 - roughness);
                    const double highlight = gloss * std::pow(std::max(0.0, dot(n, half)), 2.0 + 120.0 * gloss) * occlusion;
                    const V3 mirrored = n * (2.0 * dot(n, toward)) - toward;
                    const double sky = 0.5 + 0.5 * std::clamp(mirrored.y, -1.0, 1.0);
                    const double reflection = (0.3 + 0.75 * sky) * (0.6 + 0.4 * (1.0 - roughness)) * occlusion;
                    const double base[3] = {static_cast<double>(albedo.r), static_cast<double>(albedo.g), static_cast<double>(albedo.b)};
                    const Color glow = emissionMap ? texel(*emissionMap, u, v) : Color{};
                    const double emitted[3] = {static_cast<double>(glow.r), static_cast<double>(glow.g), static_cast<double>(glow.b)};
                    double shaded[3];
                    for (int channel = 0; channel < 3; ++channel) {
                        const double tint = 255.0 + (base[channel] - 255.0) * metallic;
                        shaded[channel] = std::min(255.0, base[channel] * diffuse * (1.0 - 0.7 * metallic) +
                                                              base[channel] * reflection * metallic +
                                                              tint * highlight * (0.25 + 0.75 * metallic) +
                                                              (emissionMap ? emitted[channel] : base[channel]) * surface.emission);
                    }
                    if (!blend) {
                        depth[at] = z;
                        colour[at] = {shaded[0], shaded[1], shaded[2], 255.0};
                    } else {
                        // Over what is already there, by the texel's alpha.
                        auto& target = colour[at];
                        const double opacity = albedo.a / 255.0, behind = target[3] / 255.0;
                        const double covered = opacity + behind * (1.0 - opacity);
                        for (int channel = 0; channel < 3; ++channel)
                            target[channel] = covered > 0.0
                                ? (shaded[channel] * opacity + target[channel] * behind * (1.0 - opacity)) / covered : 0.0;
                        target[3] = covered * 255.0;
                    }
                }
            }
        }
    }

    Image image;
    image.width = options.width;
    image.height = options.height;
    image.rgba.resize(static_cast<std::size_t>(options.width) * options.height * 4);
    const Color back = options.background;
    for (u32 y = 0; y < options.height; ++y) {
        for (u32 x = 0; x < options.width; ++x) {
            double sum[4] = {0, 0, 0, 0};
            for (u32 sy = 0; sy < kSamples; ++sy)
                for (u32 sx = 0; sx < kSamples; ++sx) {
                    const auto& sample = colour[static_cast<std::size_t>(y * kSamples + sy) * width + (x * kSamples + sx)];
                    const double cover = sample[3] / 255.0;
                    sum[0] += sample[0] * cover + back.r * (back.a / 255.0) * (1.0 - cover);
                    sum[1] += sample[1] * cover + back.g * (back.a / 255.0) * (1.0 - cover);
                    sum[2] += sample[2] * cover + back.b * (back.a / 255.0) * (1.0 - cover);
                    sum[3] += sample[3] * cover + back.a * (1.0 - cover);
                }
            const double count = kSamples * kSamples;
            const double alpha = sum[3] / count;
            u8* out = &image.rgba[(static_cast<std::size_t>(y) * options.width + x) * 4];
            // Straight alpha: colours were accumulated premultiplied.
            for (int channel = 0; channel < 3; ++channel)
                out[channel] = static_cast<u8>(std::lround(alpha > 0.0 ? std::min(255.0, sum[channel] / count * 255.0 / alpha) : 0.0));
            out[3] = static_cast<u8>(std::lround(alpha));
        }
    }
    return image;
}

// ---- the dump ---------------------------------------------------------------

void writeModelDump(std::ostream& out, const model::ModelResult& result, u32 seed) {
    const auto bits = [](f32 value) { return std::to_string(std::bit_cast<u32>(value)); };
    const auto floats = [&](const std::vector<f32>& values) {
        std::string text = "[";
        for (std::size_t i = 0; i < values.size(); ++i) text += (i ? "," : "") + bits(values[i]);
        return text + "]";
    };
    const auto triple = [&](const f32 values[3]) { return "[" + bits(values[0]) + "," + bits(values[1]) + "," + bits(values[2]) + "]"; };
    out << "{\n  \"seed\": " << seed << ",\n  \"size\": " << triple(result.size) << ",\n  \"units_per_meter\": "
        << bits(result.unitsPerMeter) << ",\n  \"bounds\": {\"min\": " << triple(result.boundsMin) << ", \"max\": "
        << triple(result.boundsMax) << "},\n  \"vertices\": " << result.vertexCount << ",\n  \"triangles\": "
        << result.triangleCount << ",\n  \"fuel\": {\"budget\": " << result.fuelBudget << ", \"spent\": " << result.fuelUsed
        << "},\n  \"work\": {\"budget\": " << result.workBudget << ", \"spent\": " << result.workUsed << "},\n  \"textures\": [";
    for (std::size_t i = 0; i < result.textures.size(); ++i) {
        const auto& texture = result.textures[i];
        out << (i ? "," : "") << "\n    {\"key\": \"" << hex64(texture.key) << "\", \"name\": \"" << jsonEscape(texture.name)
            << "\", \"width\": " << texture.color.width << ", \"height\": " << texture.color.height << ", \"asset\": ["
            << texture.assetWidth << ", " << texture.assetHeight << "], \"repeats\": " << (texture.repeats ? "true" : "false")
            << ", \"color\": \"" << hex64(imageHash(texture.color)) << "\", \"images\": [";
        for (std::size_t j = 0; j < texture.images.size(); ++j)
            out << (j ? ", " : "") << "{\"kind\": " << static_cast<u32>(texture.images[j].kind) << ", \"name\": \""
                << jsonEscape(texture.images[j].name) << "\", \"pixels\": \"" << hex64(imageHash(texture.images[j].image)) << "\"}";
        out << "]}";
    }
    out << "\n  ],\n  \"surfaces\": [";
    for (std::size_t i = 0; i < result.surfaces.size(); ++i) {
        const auto& surface = result.surfaces[i];
        out << (i ? "," : "") << "\n    {\"texture\": " << surface.texture << ", \"roughness\": " << bits(surface.roughness)
            << ", \"metallic\": " << bits(surface.metallic) << ", \"emission\": " << bits(surface.emission)
            << ", \"alpha\": " << static_cast<u32>(surface.alpha) << ", \"cutoff\": " << bits(surface.cutoff)
            << ", \"two_sided\": " << (surface.twoSided ? "true" : "false") << ", \"crisp\": " << (surface.crisp ? "true" : "false")
            << ",\n     \"positions\": " << floats(surface.positions) << ",\n     \"normals\": " << floats(surface.normals)
            << ",\n     \"tangents\": " << floats(surface.tangents) << ",\n     \"uvs\": " << floats(surface.uvs)
            << ",\n     \"indices\": [";
        for (std::size_t j = 0; j < surface.indices.size(); ++j) out << (j ? "," : "") << surface.indices[j];
        out << "]}";
    }
    out << "\n  ]\n}\n";
}

} // namespace iprop::tools
