// Compile the actual CLI entry into a test executable, without a production
// fault flag. Allocation faults must never leave partial JSON on stdout.
#include <cstdlib>
#include <cstdio>
#include <new>

namespace {
bool faultArmed=false, persistentFault=false;
std::size_t allocationIndex=0, failAt=0;
}

void* operator new(std::size_t size) {
    if (faultArmed && allocationIndex++ >= failAt) {
        if (!persistentFault) faultArmed=false;
        throw std::bad_alloc();
    }
    if (void* pointer=std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

#define main ipropActualCompilerMain
#include "../../tools/ipc.cpp"
#undef main

int main(int argc, char* argv[]) {
    if (const auto* value=std::getenv("IPROP_DIAGNOSTIC_FAIL_AT")) {
        failAt=std::strtoull(value,nullptr,10);
        persistentFault=std::getenv("IPROP_DIAGNOSTIC_FAIL_PERSIST")!=nullptr;
        faultArmed=true;
    }
    const int status=ipropActualCompilerMain(argc,argv);
    if (std::getenv("IPROP_DIAGNOSTIC_MEASURE")) std::fprintf(stderr,"%zu\n",allocationIndex);
    return status;
}
