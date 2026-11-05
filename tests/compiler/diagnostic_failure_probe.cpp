// Compile the actual CLI entry into a test executable, without a production
// fault flag. Allocation faults must never leave partial JSON on stdout.
#include <cstdlib>
#include <cstdio>

#include "../support/fault_injection.hpp"

#define main ipropActualCompilerMain
#include "../../tools/ipc.cpp"
#undef main
using namespace fault_injection;

int main(int argc, char* argv[]) {
    if (const auto* value=std::getenv("IPROP_DIAGNOSTIC_FAIL_AT")) {
        arm(std::strtoull(value,nullptr,10),std::getenv("IPROP_DIAGNOSTIC_FAIL_PERSIST")!=nullptr);
    }
    const int status=ipropActualCompilerMain(argc,argv);
    if (std::getenv("IPROP_DIAGNOSTIC_MEASURE")) std::fprintf(stderr,"%zu\n",allocations);
    return status;
}
