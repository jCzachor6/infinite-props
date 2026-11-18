// Textures across generations (spec models v1.2, M5): the stamp, the file of
// one texture and the store a host keeps them in.
#include "iprop/model/Model.hpp"

#include "iprop/Version.hpp"

#include <bit>
#include <cstring>
#include <stdexcept>

namespace iprop::model {
namespace {

constexpr u8 kMagic[4] = {'I', 'P', 'T', 'X'};
constexpr u32 kFileVersion = 1;
constexpr u32 kMaxNameBytes = 4096;

void put(std::vector<u8>& out, u64 value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) out.push_back(static_cast<u8>(value >> (8 * i)));
}

void putName(std::vector<u8>& out, const std::string& name) {
    put(out, name.size(), 4);
    out.insert(out.end(), name.begin(), name.end());
}

void putCanvas(std::vector<u8>& out, const Canvas& canvas) {
    put(out, std::bit_cast<u32>(canvas.scaleX), 4);
    put(out, std::bit_cast<u32>(canvas.scaleY), 4);
    for (const Color pixel : canvas.pixels) out.insert(out.end(), {pixel.r, pixel.g, pixel.b, pixel.a});
}

struct Reader {
    std::span<const u8> bytes;
    std::size_t at = 0;

    [[noreturn]] void damaged(const char* what) const {
        throw FormatError(std::string("stored texture: ") + what, at, ErrorReason::InvalidFormat, ErrorStage::Load);
    }
    u64 take(unsigned count) {
        if (bytes.size() - at < count) damaged("truncated");
        u64 value = 0;
        for (unsigned i = 0; i < count; ++i) value |= static_cast<u64>(bytes[at + i]) << (8 * i);
        at += count;
        return value;
    }
    std::string name() {
        const u32 length = static_cast<u32>(take(4));
        if (length > kMaxNameBytes || bytes.size() - at < length) damaged("bad name");
        std::string text(reinterpret_cast<const char*>(bytes.data() + at), length);
        at += length;
        return text;
    }
    Canvas canvas(u32 width, u32 height) {
        Canvas result;
        result.width = width;
        result.height = height;
        result.scaleX = std::bit_cast<f32>(static_cast<u32>(take(4)));
        result.scaleY = std::bit_cast<f32>(static_cast<u32>(take(4)));
        const std::size_t count = static_cast<std::size_t>(width) * height;
        if ((bytes.size() - at) / 4 < count) damaged("truncated");
        result.pixels.resize(count);
        for (std::size_t i = 0; i < count; ++i, at += 4)
            result.pixels[i] = {bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]};
        return result;
    }
};

} // namespace

u64 textureStamp() {
    const char* version = versionString();
    std::vector<u8> bytes(kMagic, kMagic + 4);
    bytes.insert(bytes.end(), version, version + std::strlen(version));
    put(bytes, kTextureRevision, 4);
    return fnv1a64(bytes);
}

std::string textureFileName(u64 key) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string name(16, '0');
    for (int i = 0; i < 16; ++i) name[static_cast<std::size_t>(15 - i)] = digits[(key >> (4 * i)) & 15];
    return name + ".iptx";
}

std::vector<u8> packTexture(const ModelTexture& texture) {
    if (texture.reused || texture.color.pixels.size() != static_cast<std::size_t>(texture.color.width) * texture.color.height)
        throw std::invalid_argument("a texture without its pixels cannot be stored");
    std::vector<u8> out(kMagic, kMagic + 4);
    put(out, kFileVersion, 4);
    put(out, textureStamp(), 8);
    put(out, texture.key, 8);
    put(out, texture.color.width, 4);
    put(out, texture.color.height, 4);
    put(out, texture.assetWidth, 4);
    put(out, texture.assetHeight, 4);
    put(out, texture.fuel, 8);
    put(out, texture.work, 8);
    u32 kinds = 0;
    for (const AuxImage& image : texture.images) kinds |= 1u << static_cast<u32>(image.kind);
    put(out, kinds, 4);
    put(out, texture.images.size(), 4);
    putName(out, texture.name);
    putCanvas(out, texture.color);
    for (const AuxImage& image : texture.images) {
        put(out, static_cast<u32>(image.kind), 4);
        put(out, static_cast<u32>(image.source), 4);
        putName(out, image.name);
        putCanvas(out, image.image);
    }
    put(out, fnv1a64(out), 8);
    return out;
}

std::optional<ModelTexture> unpackTexture(std::span<const u8> bytes) {
    Reader in{bytes};
    if (bytes.size() < 16 || std::memcmp(bytes.data(), kMagic, 4) != 0) in.damaged("not an IPTX file");
    in.at = 4;
    if (in.take(4) != kFileVersion) return std::nullopt;
    if (in.take(8) != textureStamp()) return std::nullopt;
    if (fnv1a64(bytes.first(bytes.size() - 8)) != Reader{bytes, bytes.size() - 8}.take(8)) in.damaged("checksum mismatch");
    ModelTexture texture;
    texture.key = in.take(8);
    const u32 width = static_cast<u32>(in.take(4)), height = static_cast<u32>(in.take(4));
    if (width == 0 || height == 0 || width > kMaxTextureSide || height > kMaxTextureSide) in.damaged("bad size");
    texture.assetWidth = static_cast<u32>(in.take(4));
    texture.assetHeight = static_cast<u32>(in.take(4));
    texture.fuel = in.take(8);
    texture.work = in.take(8);
    const u32 kinds = static_cast<u32>(in.take(4));
    const u32 images = static_cast<u32>(in.take(4));
    if (images > kAuxImageKinds) in.damaged("too many images");
    texture.name = in.name();
    texture.color = in.canvas(width, height);
    for (u32 i = 0; i < images; ++i) {
        AuxImage image;
        const u32 kind = static_cast<u32>(in.take(4));
        if (kind < 1 || kind > kAuxImageKinds || ((texture.kinds >> kind) & 1u)) in.damaged("bad image kind");
        image.kind = static_cast<AuxImageKind>(kind);
        const u32 source = static_cast<u32>(in.take(4));
        if (source != static_cast<u32>(AuxImageSource::Authored) && source != static_cast<u32>(AuxImageSource::Derived))
            in.damaged("bad image source");
        image.source = static_cast<AuxImageSource>(source);
        image.name = in.name();
        image.image = in.canvas(width, height);
        texture.kinds |= 1u << kind;
        texture.images.push_back(std::move(image));
    }
    if (in.at != bytes.size() - 8) in.damaged("trailing bytes");
    if (texture.kinds != kinds) in.damaged("image kinds differ from the header");
    return texture;
}

std::optional<KnownTexture> peekTexture(std::span<const u8> header) {
    if (header.size() < kTextureHeaderBytes || std::memcmp(header.data(), kMagic, 4) != 0) return std::nullopt;
    Reader in{header, 4};
    if (in.take(4) != kFileVersion || in.take(8) != textureStamp()) return std::nullopt;
    KnownTexture record;
    record.key = in.take(8);
    in.at += 16; // the sizes
    record.fuel = in.take(8);
    record.work = in.take(8);
    record.kinds = static_cast<u32>(in.take(4));
    return record;
}

const ModelTexture* TextureStore::find(u64 key) const {
    const auto found = textures_.find(key);
    return found == textures_.end() ? nullptr : &found->second;
}

bool TextureStore::add(ModelTexture texture) {
    if (texture.reused) throw std::invalid_argument("a texture without its pixels cannot be stored");
    if (textures_.contains(texture.key)) return false;
    texture.repeats = false; // a property of one result's triangles
    known_.push_back(knownTexture(texture));
    const u64 key = texture.key;
    textures_.emplace(key, std::move(texture));
    return true;
}

void TextureStore::complete(ModelResult& result) {
    for (ModelTexture& texture : result.textures) {
        if (!texture.reused) {
            add(texture);
            continue;
        }
        const ModelTexture* held = find(texture.key);
        if (!held) throw std::logic_error("a result reuses a texture its store does not hold");
        texture.color = held->color;
        texture.images = held->images;
        texture.reused = false;
    }
}

} // namespace iprop::model
