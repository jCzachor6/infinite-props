#include "iprop/VM.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: render-benchmark file.iprop iterations\n"; return 1; }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) { std::cerr << "cannot read fixture\n"; return 1; }
    const std::vector<iprop::u8> bytes((std::istreambuf_iterator<char>(input)), {});
    const auto file = iprop::loadFile(bytes);
    iprop::validate(file);
    const int iterations = std::stoi(argv[2]);
    if (iterations <= 0) return 1;
    iprop::u64 hash = 0, fuel = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i=0; i<iterations; ++i) {
        iprop::VM vm(file,{});
        vm.run();
        hash = 14695981039346656037ull;
        for (const auto c : vm.canvas().pixels)
            for (const auto byte : {c.r,c.g,c.b,c.a}) hash = (hash ^ byte) * 1099511628211ull;
        fuel = vm.fuelBudget() - vm.fuel();
    }
    const double ms = std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count();
    std::cout << "{\"msPerGeneration\":" << ms/iterations << ",\"fuel\":" << fuel
              << ",\"imageHash\":\"" << std::hex << hash << "\"}\n";
}
