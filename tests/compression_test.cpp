#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <zlib.h>

#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "ipdis.hpp"
#include "support/compiler_support.hpp"

using namespace iprop;

namespace {
std::vector<u8> original() {
    return compiler_test::compileChecked("asset t { input i32 v=0 input i32 a=0 size 8x8 output point p draw { pixel(v,0,#abcdef) p={v,a} } }");
}
void word(std::vector<u8>& bytes, std::size_t offset, u32 value) {
    for (unsigned i=0;i<4;++i) bytes.at(offset+i)=static_cast<u8>(value>>(8*i));
}
std::vector<u8> envelope(const std::vector<u8>& inner, bool dictionary = false) {
    auto header = parseHeader(original());
    header.flags = static_cast<u32>(inner[8]);
    REQUIRE(inner.size() <= std::numeric_limits<uInt>::max());
    z_stream stream{};
    REQUIRE(deflateInit(&stream, Z_BEST_COMPRESSION) == Z_OK);
    const std::string dict = "IPRP meta const code";
    if (dictionary) REQUIRE(deflateSetDictionary(&stream,reinterpret_cast<const Bytef*>(dict.data()),static_cast<uInt>(dict.size())) == Z_OK);
    std::vector<u8> compressed(compressBound(static_cast<uLong>(inner.size())));
    REQUIRE(compressed.size() <= std::numeric_limits<uInt>::max());
    stream.next_in=const_cast<Bytef*>(inner.data()); stream.avail_in=static_cast<uInt>(inner.size());
    stream.next_out=compressed.data(); stream.avail_out=static_cast<uInt>(compressed.size());
    REQUIRE(deflate(&stream,Z_FINISH) == Z_STREAM_END);
    compressed.resize(stream.total_out);
    REQUIRE(deflateEnd(&stream) == Z_OK);
    Writer out;
    for (const auto magic : kMagic) out.writeU8(magic);
    out.writeU16(0); out.writeU16(kVersionMinor); out.writeU32(header.flags|kFlagCompressed);
    out.writeU32(header.sectionCount); out.writeU32(0); out.writeU32(static_cast<u32>(inner.size())); out.writeBytes(compressed);
    return out.data();
}
}

TEST_CASE("compression preserves current files and source maps exactly") {
    for (u16 minor : {u16{7}}) {
        auto plain=original(); plain[6]=static_cast<u8>(minor);
        const auto packed=compressFile(plain);
        REQUIRE(parseHeader(packed).hasCompression());
        CHECK(packed.size()<plain.size());
        CHECK(compressFile(plain)==packed);
        CHECK(compressFile(packed)==packed);
        CHECK(decompressFile(packed)==plain);
        auto loaded=loadFile(packed);
        validate(loaded);
        CHECK(loaded.layout.header.versionMinor==minor);
        CHECK(loaded.containerHeader.versionMinor==7);
        CHECK(loaded.containerHeader.hasCompression());
        CHECK(std::vector<u8>(loaded.bytes().begin(),loaded.bytes().end())==plain);
        CHECK(disassembleWithSource(packed)==disassembleWithSource(plain));
        const auto a=compiler_test::run(packed,1,0),b=compiler_test::run(plain,1,0);
        CHECK(a.canvas.pixels==b.canvas.pixels);
        CHECK(a.outputs[0].points[0].x==b.outputs[0].points[0].x);
        CHECK_FALSE(parseHeader(stripDebug(packed)).hasDebug());
        CHECK(decompressFile(stripDebug(packed))==stripDebug(plain));
        CHECK(stripDebug(stripDebug(packed))==stripDebug(packed));
        auto moved=std::move(loaded);
        plain.assign(plain.size(),0);
        VM vm(moved,GenerationInputs{42,0,0, {{"v", {InputType::U32, {}, 0.f, 1}}}});
        vm.run();
        CHECK(vm.canvas().pixels==b.canvas.pixels);
    }
}

TEST_CASE("compressed stream framing checksum lengths nesting and dictionaries are strict") {
    const auto plain=original(),packed=envelope(plain);
    CHECK_THROWS_AS(parseLayout(packed),FormatError);
    auto changed=packed; changed.back()^=1;
    CHECK_THROWS_AS(loadFile(changed),FormatError);
    changed=packed; changed.pop_back();
    CHECK_THROWS_AS(loadFile(changed),FormatError);
    changed=packed; changed.push_back(0);
    CHECK_THROWS_AS(loadFile(changed),FormatError);
    changed=packed; changed.insert(changed.end(),packed.begin()+24,packed.end());
    CHECK_THROWS_AS(loadFile(changed),FormatError);
    for (const auto size : {u32{0},u32{20},static_cast<u32>(plain.size()-1),static_cast<u32>(plain.size()+1),kMaxCompressedFileSize+1}) {
        changed=packed; word(changed,20,size);
        CHECK_THROWS_AS(loadFile(changed),FormatError);
    }
    changed=packed; word(changed,8,parseHeader(packed).flags^kFlagDebug);
    CHECK_THROWS_AS(loadFile(changed),FormatError);
    changed=packed; word(changed,12,parseHeader(packed).sectionCount-1);
    CHECK_THROWS_AS(loadFile(changed),FormatError);
    changed=packed; changed[6]=3;
    CHECK_THROWS_AS(loadFile(changed),FormatError);
    changed=packed; word(changed,16,1);
    CHECK_THROWS_AS(loadFile(changed),FormatError);
    changed=packed; word(changed,8,parseHeader(packed).flags|4u);
    CHECK_THROWS_AS(loadFile(changed),FormatError);
    CHECK_THROWS_AS(loadFile(envelope(packed)),FormatError);
    CHECK_THROWS_AS(loadFile(envelope(plain,true)),FormatError);
    changed=packed; changed.resize(kMaxCompressedFileSize+1);
    CHECK_THROWS_AS(loadFile(changed),FormatError);
}

TEST_CASE("uncompressed files retain identity and malformed inner layouts are rejected") {
    const auto plain=original();
    CHECK(decompressFile(plain)==plain);
    auto invalid=plain;
    word(invalid,20,99); // first inner section id
    CHECK_THROWS_AS(loadFile(envelope(invalid)),FormatError);
    for (std::size_t size=0;size<=24;++size) {
        auto truncated=envelope(plain); truncated.resize(size);
        CHECK_THROWS_AS(loadFile(truncated),FormatError);
    }
}

TEST_CASE("compression rejects retired versions in both envelope and payload") {
    const auto plain=original();
    for (u16 minor=0;minor<kVersionMinor;++minor) {
        auto old=plain; old[6]=static_cast<u8>(minor);
        CHECK_THROWS_AS(compressFile(old),FormatError);
        auto outer=envelope(plain); outer[6]=static_cast<u8>(minor);
        CHECK_THROWS_AS(loadFile(outer),FormatError);
        // Construct the stream directly: a current envelope cannot conceal
        // a retired payload, even when its zlib checksum is valid.
        CHECK_THROWS_AS(loadFile(envelope(old)),FormatError);
    }
}
