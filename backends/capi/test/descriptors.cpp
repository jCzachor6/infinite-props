#include "../Options.hpp"
#include "../Utf8View.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "descriptor: line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main() {
    std::string_view name = "preserved";
    CHECK(!iprop::capi::readUtf8View({nullptr,1}, name));
    CHECK(name == "preserved");
    const char malformed[] = {'a', static_cast<char>(0xc0), static_cast<char>(0x80)};
    CHECK(!iprop::capi::readUtf8View({malformed,3}, name));
    CHECK(name == "preserved");
    const char exact[] = {'x', static_cast<char>(0xc5), static_cast<char>(0x82), '\0', 'y'};
    CHECK(iprop::capi::readUtf8View({exact,5}, name));
    CHECK(name.size() == 5 && name[3] == '\0' && name[4] == 'y');
    CHECK(!iprop::capi::readUtf8View({exact,2}, name)); // truncated UTF-8
    CHECK(name.size() == 5);
    CHECK(iprop::capi::readUtf8View({nullptr,0}, name) && name.empty());
    using iprop::capi::GenerateOptions;
    using iprop::capi::OptionIssue;
    using iprop::capi::readGenerateOptions;
    iprop_generate_options_ex options{};
    CHECK(iprop_generate_options_ex_init(&options, sizeof(options)) == IPROP_EX_OK);
    CHECK(options.struct_size == sizeof(options));
    CHECK(options.flags == 0 && options.seed == 0 && options.variant == 0 &&
          options.archetype == 0 && options.size_x == 0 && options.size_y == 0);
    options.seed = UINT32_MAX; options.variant = 7; options.archetype = 3;
    options.size_x = 64; options.size_y = 32;
    for (uint32_t capacity = 4; capacity <= sizeof(options); ++capacity) {
        // Allocate exactly the claimed capacity: ASan can catch prefix reads,
        // whereas a full stack struct with a short size only catches writes.
        void* prefix = std::malloc(capacity);
        CHECK(prefix != nullptr);
        std::memcpy(prefix, &options, capacity);
        std::memcpy(prefix, &capacity, sizeof(capacity));
        GenerateOptions read;
        CHECK(readGenerateOptions(static_cast<iprop_generate_options_ex*>(prefix), read) == OptionIssue::None);
        CHECK(read.seed == (capacity >= 12 ? UINT32_MAX : 0));
        CHECK(read.variant == (capacity >= 16 ? 7u : 0u));
        CHECK(read.archetype == (capacity >= 20 ? 3u : 0u));
        CHECK(read.sizeX == (capacity >= 24 ? 64u : 0u));
        CHECK(read.sizeY == (capacity >= 28 ? 32u : 0u));
        std::free(prefix);
    }
    GenerateOptions preserved{1, 2, 3, 4, 5};
    for (uint32_t flags : {1u, 2u, UINT32_C(0x80000000), UINT32_MAX}) {
        options.flags = flags;
        CHECK(readGenerateOptions(&options, preserved) == OptionIssue::UnknownFlags);
        CHECK(preserved.seed == 1 && preserved.variant == 2 && preserved.archetype == 3 &&
              preserved.sizeX == 4 && preserved.sizeY == 5);
    }
    options.struct_size = 3;
    CHECK(readGenerateOptions(&options, preserved) == OptionIssue::DescriptorTooSmall);
    CHECK(readGenerateOptions(nullptr, preserved) == OptionIssue::NullDescriptor);
    CHECK(preserved.seed == 1 && preserved.sizeY == 5);
    CHECK(iprop_generate_options_ex_init(nullptr, 4) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_library_info_ex_init(nullptr, 4) == IPROP_EX_E_ARGUMENT);
    for (uint32_t capacity = 0; capacity <= sizeof(options) + 16; ++capacity) {
        union { iprop_generate_options_ex align; unsigned char bytes[sizeof(options) + 16]; } storage;
        std::memset(storage.bytes, 0xa5, sizeof(storage.bytes));
        CHECK(iprop_generate_options_ex_init(reinterpret_cast<iprop_generate_options_ex*>(storage.bytes), capacity) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        if (capacity < 4) {
            for (unsigned char byte : storage.bytes) CHECK(byte == 0xa5);
        } else {
            CHECK(std::memcmp(storage.bytes, &capacity, 4) == 0);
            for (std::size_t i = 4; i < sizeof(storage.bytes); ++i)
                CHECK(storage.bytes[i] == (i < sizeof(options) && (i / 4 + 1) * 4 <= capacity ? 0 : 0xa5));
        }
    }
    std::puts("sized initializer, bounded option reads/defaults and strict flags: PASS");
    return 0;
}
