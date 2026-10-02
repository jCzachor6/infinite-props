#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "support/test_support.hpp"

using namespace iprop;
using namespace test_support;

TEST_CASE("vector arithmetic canonicalizes NaNs over the shared float boundary corpus") {
    const auto path = std::filesystem::path(__FILE__).parent_path() /
                      "fixtures/f32-boundaries.txt";
    std::ifstream input(path);
    REQUIRE(input.good());
    std::vector<f32> values;
    for (std::string line; std::getline(input, line);) {
        values.push_back(f32FromBits(static_cast<u32>(std::stoul(line, nullptr, 16))));
    }
    REQUIRE(values.size() >= 20);
    for (const auto op : {Opcode::Vec2Add, Opcode::Vec2Sub, Opcode::Vec2Mul,
                          Opcode::Vec2Div, Opcode::Vec2Neg, Opcode::Vec2Len,
                          Opcode::Vec2Dot, Opcode::Vec2Rot, Opcode::Vec2Fold,
                          Opcode::Vec2Polar}) {
        for (const f32 x : values) for (const f32 y : values) {
            INFO("opcode " << static_cast<unsigned>(op) << " input bits " <<
                 bitsOf(x) << ", " << bitsOf(y));
            Prog p;
            p.F(x).F(y).emit(static_cast<u8>(Opcode::Vec2Make));
            if (op == Opcode::Vec2Add || op == Opcode::Vec2Sub ||
                op == Opcode::Vec2Mul || op == Opcode::Vec2Div || op == Opcode::Vec2Dot) {
                p.F(y).F(x).emit(static_cast<u8>(Opcode::Vec2Make));
            } else if (op == Opcode::Vec2Rot) p.F(y);
            else if (op == Opcode::Vec2Fold) p.I(4);
            p.emit(static_cast<u8>(op));
            const auto result = evalProg(p);
            REQUIRE(result.size() == 1);
            const auto check = [](f32 v) {
                if (std::isnan(v)) CHECK(bitsOf(v) == 0x7FC00000u);
            };
            if (const auto* v = std::get_if<Vec2>(&result[0])) {
                check(v->x); check(v->y);
            } else check(std::get<f32>(result[0]));
        }
    }
    // Transport must not canonicalize payloads or the sign of zero.
    for (const f32 value : values) {
        CHECK(bitsOf(evalProgF32(Prog{}.F(value).F(0.f).emit(0x5B).emit(0x78))) ==
              bitsOf(value));
    }
}
