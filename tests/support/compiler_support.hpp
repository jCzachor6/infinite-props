#pragma once

#include "iprop/VM.hpp"

#include <bit>
#include <string>
#include <vector>

namespace compiler_test {

struct RunOutcome {
    iprop::Canvas canvas;
    std::vector<iprop::NamedOutput> outputs;
};

// Parse, check and compile one asset source.
std::vector<iprop::u8> compileChecked(const std::string& source, bool optimize = true);
RunOutcome execute(const std::vector<iprop::u8>& bytes, iprop::u32 seed = 42);
RunOutcome run(const std::vector<iprop::u8>& bytes, iprop::u32 v, iprop::u32 a);
// Generates with uniform `v` = variant; zero width/height keep the declared size.
iprop::GenerationResult generateVariant(const std::vector<iprop::u8>& bytes, iprop::u32 seed,
    iprop::u32 variant = 0, iprop::u32 width = 0, iprop::u32 height = 0);
// Compiling `source` fails with a located SourceError containing `message`.
void rejectSource(const std::string& source, const std::string& message);
// Compiling `source` through compileSource fails with a located diagnostic containing `message`.
void rejectDiagnostic(const std::string& source, const std::string& message);
// The message of the SourceError that checking `source` raises.
std::string checkErrorMessage(const std::string& source);
// x/y bits of every output point, in slot order.
template <class Result> std::vector<iprop::u32> outputPointBits(const Result& result) {
    std::vector<iprop::u32> bits;
    for (const auto& output : result.outputs)
        for (const auto& point : output.points) {
            bits.push_back(std::bit_cast<iprop::u32>(point.x));
            bits.push_back(std::bit_cast<iprop::u32>(point.y));
        }
    return bits;
}
// Canvas pixels and every output point are bit-identical.
void sameOutcome(const RunOutcome& actual, const RunOutcome& expected);
void sameOutcome(const std::vector<iprop::u8>& actual, const std::vector<iprop::u8>& expected, iprop::u32 seed);

} // namespace compiler_test
