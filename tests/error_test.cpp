#include <catch2/catch_test_macros.hpp>

#include "../backends/capi/Error.hpp"
#include "iprop/Compression.hpp"
#include "iprop/VM.hpp"
#include "support/compiler_support.hpp"
#include "support/test_support.hpp"

#include <cstdlib>
#include <cstring>
#include <type_traits>

using namespace iprop;
using namespace test_support;

TEST_CASE("typed error codes are independent of wording and absent is not offset zero", "[error-metadata]") {
    FormatError first("wording one", ErrorReason::InvalidMetadata, ErrorStage::Load);
    FormatError second("different text", ErrorReason::InvalidMetadata, ErrorStage::Load);
    const auto a = capi::describeError(IPROP_EX_E_FORMAT, first);
    const auto b = capi::describeError(IPROP_EX_E_FORMAT, second);
    CHECK(a.reason == IPROP_REASON_INVALID_METADATA);
    CHECK(a.reason == b.reason);
    CHECK(a.stage == IPROP_STAGE_LOAD);
    CHECK(a.location_flags == 0);
    CHECK(a.byte_offset == 0);
    const FormatError atZero("located", 0, ErrorReason::BadMagic, ErrorStage::Load, OffsetSpace::Container);
    const auto located = capi::describeError(IPROP_EX_E_FORMAT, atZero);
    CHECK(located.location_flags == IPROP_LOCATION_BYTE_OFFSET);
    CHECK(located.byte_offset == 0);
    CHECK(located.offset_space == IPROP_OFFSET_CONTAINER);
    static_assert(std::is_base_of_v<FormatError, Trap>);
}

TEST_CASE("load errors identify container and expanded coordinates", "[error-metadata]") {
    const auto valid = fullFile(minimalMeta(), richPool({"draw"}), codeSection({drawSpec(retIns())}, {}));
    auto magic = valid;
    magic[0] = 0;
    try {
        (void)loadFile(magic);
        FAIL("bad magic accepted");
    } catch (const FormatError& error) {
        CHECK(error.reason == ErrorReason::BadMagic);
        CHECK(error.location.offsetSpace == OffsetSpace::Container);
        CHECK(error.location.byteOffset == 0);
        CHECK(error.location.flags == ErrorLocation::ByteOffset);
    }
    auto invalid = valid;
    const auto layout = parseLayout(valid);
    const auto metaAt = layout.find(SectionId::Meta)->offset;
    for (std::size_t i = metaAt; i < metaAt + 4; ++i) invalid[i] = 0; // zero width
    for (const auto& bytes : {invalid, compressFile(invalid)}) {
        try {
            (void)loadFile(bytes);
            FAIL("invalid metadata accepted");
        } catch (const FormatError& error) {
            CHECK(error.reason == ErrorReason::InvalidMetadata);
            CHECK(error.stage == ErrorStage::Load);
            CHECK(error.offset == metaAt);
            CHECK(error.location.byteOffset == metaAt);
            CHECK(error.location.offsetSpace == OffsetSpace::ExpandedFile);
        }
    }
    auto compressed = compressFile(valid);
    REQUIRE(parseHeader(compressed).hasCompression());
    compressed.back() ^= 1;
    try {
        (void)loadFile(compressed);
        FAIL("corrupt compressed stream accepted");
    } catch (const FormatError& error) {
        CHECK(error.reason == ErrorReason::Compression);
        CHECK(error.location.offsetSpace == OffsetSpace::Container);
    }
}

TEST_CASE("static validation supplies typed function and instruction location", "[error-metadata]") {
    const auto bytes = fullFile(minimalMeta(), richPool({"draw"}),
        codeSection({drawSpec(ins(0xff, 0, 0))}, {}));
    const auto loaded = loadFile(bytes);
    try {
        validate(loaded);
        FAIL("reserved instruction accepted");
    } catch (const FormatError& error) {
        const auto view = capi::describeError(IPROP_EX_E_FORMAT, error, &loaded);
        CHECK(view.reason == IPROP_REASON_INVALID_INSTRUCTION);
        CHECK(view.stage == IPROP_STAGE_VALIDATE);
        CHECK(view.location_flags == (IPROP_LOCATION_BYTE_OFFSET | IPROP_LOCATION_FUNCTION | IPROP_LOCATION_PC));
        CHECK(view.function_index == 0);
        CHECK(view.pc == 0);
        CHECK(view.byte_offset == loaded.code.entry().fileOffset);
        CHECK(view.debug_line == 0);
    }
    const auto signature = loadFile(fullFile(minimalMeta(), richPool({"wrong"}),
        codeSection({drawSpec(retIns())}, {})));
    try {
        validate(signature);
        FAIL("wrong entry accepted");
    } catch (const FormatError& error) {
        CHECK(error.reason == ErrorReason::InvalidSignature);
        CHECK(error.location.flags == (ErrorLocation::ByteOffset | ErrorLocation::Function));
        CHECK(error.location.functionIndex == 0);
    }
}

TEST_CASE("VM traps retain legacy policy while structured setup locations are absent", "[error-metadata]") {
    const auto loaded = loadFile(fullFile(minimalMeta(), richPool({"draw"}),
        codeSection({drawSpec(retIns())}, {})));
    validate(loaded);
    GenerationInputs inputs;
    inputs.sizeX = kMaxCanvasSide + 1;
    try {
        VM vm(loaded, inputs);
        FAIL("invalid canvas size accepted");
    } catch (const Trap& error) {
        CHECK(error.offset == 0); // legacy text keeps its original offset
        const auto view = capi::describeError(IPROP_EX_E_TRAP, error, &loaded);
        CHECK(view.status == IPROP_EX_E_TRAP);
        CHECK(view.reason == IPROP_REASON_CANVAS_SIZE);
        CHECK(view.stage == IPROP_STAGE_ARGUMENTS);
        CHECK(view.location_flags == 0);
        CHECK(view.offset_space == IPROP_OFFSET_NONE);
    }
}

TEST_CASE("execution traps recover real debug lines and stripped maps stay absent", "[error-metadata]") {
    const auto original = compiler_test::compileChecked(
        "asset t { input i32 v = 0 \n size 8x8\n draw {\n  x = v\n  y = 1 / x\n  pixel(y,0,#fff)\n }\n}");
    for (const auto& bytes : {original, compressFile(original), stripDebug(original)}) {
        const auto loaded = loadFile(bytes);
        validate(loaded);
        try {
            VM vm(loaded, {});
            vm.run();
            FAIL("division by zero accepted");
        } catch (const Trap& error) {
            const auto view = capi::describeError(IPROP_EX_E_TRAP, error, &loaded);
            CHECK(view.reason == IPROP_REASON_DIVIDE_BY_ZERO);
            CHECK(view.stage == IPROP_STAGE_EXECUTE);
            CHECK(view.offset_space == IPROP_OFFSET_EXPANDED_FILE);
            CHECK(view.function_index == 0);
            CHECK(view.byte_offset == loaded.code.entry().fileOffset + view.pc * 4);
            if (loaded.layout.header.hasDebug()) {
                CHECK((view.location_flags & IPROP_LOCATION_DEBUG_LINE) != 0);
                CHECK(view.debug_line == 5);
            } else {
                CHECK((view.location_flags & IPROP_LOCATION_DEBUG_LINE) == 0);
                CHECK(view.debug_line == 0);
            }
        }
    }
}

TEST_CASE("structured call depth location identifies the call before the legacy PC advances", "[error-metadata]") {
    // Supply both entry params before each recursive call; static validation
    // accepts recursion, while the VM enforces the actual call depth.
    std::vector<u8> recursive;
    for (const auto& instruction : {ins(0x08, 0, 0)})
        recursive.insert(recursive.end(), instruction.begin(), instruction.end());
    const auto file = loadFile(fullFile(minimalMeta(), richPool({"draw"}),
        codeSection({drawSpec(recursive)}, {})));
    validate(file);
    try {
        VM vm(file, {});
        vm.run();
        FAIL("unbounded recursion accepted");
    } catch (const Trap& error) {
        CHECK(error.reason == ErrorReason::CallDepth);
        CHECK(error.location.pc == 0);
        CHECK(error.location.byteOffset == file.code.entry().fileOffset);
        CHECK(error.offset == file.code.entry().fileOffset + 4);
    }
}

TEST_CASE("unusable debug maps do not claim a source line or alter the trap", "[error-metadata]") {
    const auto original = compiler_test::compileChecked(
        "asset t { input i32 v = 0  size 8x8 draw { x = v y = 1/x pixel(y,0,#fff) } }");
    const auto invoke = [](const LoadedFile& file) {
        validate(file);
        try {
            VM vm(file, {});
            vm.run();
        } catch (const Trap& error) {
            return capi::describeError(IPROP_EX_E_TRAP, error, &file);
        }
        FAIL("division by zero accepted");
        return iprop_error_ex{};
    };
    const auto loaded = loadFile(original);
    const auto before = invoke(loaded);
    REQUIRE((before.location_flags & IPROP_LOCATION_DEBUG_LINE) != 0);
    const auto section = *loaded.layout.find(SectionId::Debug);
    const auto debug = parseDebug(loaded.bytes(), section);
    const auto instructionsAt = section.offset + 8 + debug.functions[0].localNames.size() * 4;
    const auto lineAt = instructionsAt + 4 + before.pc * 4;
    for (const auto& [at, value] : {std::pair<std::size_t, u32>{section.offset, 0},
             {section.offset + 4, UINT32_MAX}, {instructionsAt, 0}, {lineAt, 0}}) {
        auto bytes = original;
        for (unsigned shift = 0; shift < 4; ++shift)
            bytes[at + shift] = static_cast<u8>(value >> (shift * 8));
        const auto file = loadFile(bytes); // legacy load ignores informative debug payloads
        const auto after = invoke(file);
        CHECK(after.reason == before.reason);
        CHECK(after.stage == before.stage);
        CHECK(after.byte_offset == before.byte_offset);
        CHECK(after.pc == before.pc);
        CHECK((after.location_flags & IPROP_LOCATION_DEBUG_LINE) == 0);
        CHECK(after.debug_line == 0);
    }
    auto truncated = section;
    truncated.size = 4;
    CHECK(debugLineAt(loaded.bytes(), truncated, 0, before.pc, 1,
        static_cast<u32>(loaded.code.entry().code.size() / 4)) == 0);
}

TEST_CASE("error descriptor writers never touch absent fields or unknown tails", "[error-metadata]") {
    auto location = ErrorLocation::instruction(1234, 7, 11);
    location.flags |= ErrorLocation::DebugLine;
    location.debugLine = 42;
    const FormatError error("ignored", ErrorReason::FuelExhausted, ErrorStage::Execute, location);
    const auto value = capi::describeError(IPROP_EX_E_TRAP, error);
    CHECK(!capi::writeError(nullptr, value));
    for (uint32_t capacity = 0; capacity <= sizeof(iprop_error_ex) + 16; ++capacity) {
        auto* bytes = static_cast<unsigned char*>(std::malloc(capacity < 4 ? 4 : capacity));
        REQUIRE(bytes != nullptr);
        std::memset(bytes, 0xa5, capacity < 4 ? 4 : capacity);
        std::memcpy(bytes, &capacity, 4);
        auto* descriptor = reinterpret_cast<iprop_error_ex*>(bytes);
        CHECK(capi::writeError(descriptor, value) == (capacity >= 4));
        CHECK(std::memcmp(bytes, &capacity, 4) == 0);
        const std::pair<std::size_t, std::size_t> fields[] = {
            {offsetof(iprop_error_ex, status),4}, {offsetof(iprop_error_ex, reason),4},
            {offsetof(iprop_error_ex, stage),4}, {offsetof(iprop_error_ex, byte_offset),8},
            {offsetof(iprop_error_ex, location_flags),4}, {offsetof(iprop_error_ex, offset_space),4},
            {offsetof(iprop_error_ex, function_index),4}, {offsetof(iprop_error_ex, pc),4},
            {offsetof(iprop_error_ex, debug_line),4},
            {offsetof(iprop_error_ex, message_required_bytes),8}, {offsetof(iprop_error_ex, message_truncated),4}};
        for (std::size_t byte = 4; byte < capacity; ++byte) {
            bool complete = false;
            for (auto [at, width] : fields)
                if (at + width <= capacity && byte >= at && byte < at + width) complete = true;
            if (!complete) CHECK(bytes[byte] == 0xa5);
        }
        if (capacity >= offsetof(iprop_error_ex, location_flags) + 4) {
            uint32_t flags = 0;
            std::memcpy(&flags, bytes + offsetof(iprop_error_ex, location_flags), 4);
            CHECK(((flags & IPROP_LOCATION_BYTE_OFFSET) != 0) == (capacity >= offsetof(iprop_error_ex, offset_space) + 4));
            CHECK(((flags & IPROP_LOCATION_FUNCTION) != 0) == (capacity >= offsetof(iprop_error_ex, function_index) + 4));
            CHECK(((flags & IPROP_LOCATION_PC) != 0) == (capacity >= offsetof(iprop_error_ex, pc) + 4));
            CHECK(((flags & IPROP_LOCATION_DEBUG_LINE) != 0) == (capacity >= offsetof(iprop_error_ex, debug_line) + 4));
        }
        std::free(bytes);
    }
}
