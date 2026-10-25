#include <cstddef>

// Negative controls run in separate Python processes: each must be diagnosed
// even with the interpreter's allocator-only LSan suppressions installed.
extern "C" __attribute__((visibility("default"), noinline))
void iprop_probe_overflow(std::size_t index) {
    auto* bytes = new char[8];
    static_cast<volatile char*>(bytes)[index] = 1;
    delete[] bytes;
}

extern "C" __attribute__((visibility("default"), noinline))
void iprop_probe_leak(std::size_t size) {
    auto* bytes = new char[size];
    asm volatile("" : : "r"(bytes) : "memory");
}
