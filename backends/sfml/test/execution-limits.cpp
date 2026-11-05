#include "../src/Host.hpp"
#include <chrono>
#include <iostream>
#include <thread>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}
int main(int argc, char** argv) {
    using namespace iprop::sfml_host;
    try {
        require(argc == 2, "usage: sfml_execution_limits execution-slow.iprop");
        Program program(loadProgram(readFile(argv[1])),iprop_program_free);
        auto token = createCancellation();
        require(!iprop_cancellation_is_cancelled(token.get()),"fresh token");
        std::jthread cancel([retained=token] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            iprop_cancellation_cancel(retained.get());
        });
        try {
            Result rejected(generate(program.get(),26,nullptr,0,0,{1'000'000'000,0,token}),iprop_result_free);
            throw std::runtime_error("active cancellation ignored");
        } catch (const Failure& failure) {
            require(failure.reason == IPROP_REASON_CANCELLED && failure.stage == IPROP_STAGE_EXECUTE &&
                    (failure.locationFlags & IPROP_LOCATION_PC) != 0,"cancellation must interrupt an active opcode");
        }
        cancel.join();
        for (int i=0;i<32;++i) {
            try {
                Result rejected(generate(program.get(),26,nullptr,0,0,{0,0,token}),iprop_result_free);
                throw std::runtime_error("pre-cancel ignored");
            } catch (const Failure& failure) { require(failure.reason == IPROP_REASON_CANCELLED,"pre-cancel reason"); }
        }
        auto parameters=createParameters(program.get());
        setParameter(parameters.get(),"enabled","0");
        Result recovered(generate(program.get(),26,parameters.get()),iprop_result_free);
        require(queryImage(recovered.get()).width == 1,"recovery");
        std::cout << "SFML execution controls: PASS active opcode cancellation, repeated cancellation and recovery\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
