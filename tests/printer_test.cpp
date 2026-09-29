#include <catch2/catch_test_macros.hpp>

#include <string>

#include "ips/Parser.hpp"
#include "ips/Printer.hpp"

// The S-expression AST printer: pinned golden output for the sketches plus
// coverage of every print branch (else chains, literals, unary operators,
// member chains with non-identifier bases, returns).

using namespace iprop::ips;

namespace {

const std::string kRockSketch = R"(asset rock {
  size 192x192
  origin 96,170
  variants 5
  archetypes 4
  input  color palette = #808d74
  output rect  collider

  draw(v, a) {
    w = range(21, 31)
    h = range(35, 49)
    collider = {96 - 20, 170 - 23, 40, 23}
    stone_mass(-w * 0.10, 0, w * 0.87, h, palette, seed)
    if (index(3) == 0) {
      stone_mass(-w * 0.67, 2, w * 0.39, h * 0.43,
                 shade(palette, -4), hash(seed ^ 9467))
    }
  }
})";

const std::string kPortraitSketch = R"(
import hair from "hair.ips"
import eyes from "eyes.ips"

asset portrait {
  size 64x64
  variants 8
  input  color skin = #d9b58c
  input  color hairColor = #33291f
  output point pupilCenter

  draw(v, a) {
    face(v, a)
    hair.draw(seed ^ 0x9e3779b9, v, a, at = {32, 4}, color = hairColor)
    eyes.draw(seed ^ 0x517cc1b7, v, a, at = {32, 24})
    pupilCenter = eyes.outputs.pupil
  }
})";

} // namespace

TEST_CASE("the S-expression printer renders the rock sketch") {
    const std::string expected = R"((program
  (asset rock
    (size 192 192)
    (origin 96 170)
    (variants 5)
    (archetypes 4)
    (input color palette #808d74)
    (output rect collider)
    (fn draw (v a)
      (let w (call range 21 31))
      (let h (call range 35 49))
      (let collider (vector (- 96 20) (- 170 23) 40 23))
      (expr (call stone_mass (* (neg w) 0.10) 0 (* w 0.87) h palette seed))
      (if (== (call index 3) 0)
        (expr (call stone_mass (* (neg w) 0.67) 2 (* w 0.39) (* h 0.43) (call shade palette (neg 4)) (call hash (^ seed 9467))))))))
)";
    REQUIRE(printProgram(parse(kRockSketch)) == expected);
}

TEST_CASE("the S-expression printer renders the portrait sketch") {
    const std::string printed = printProgram(parse(kPortraitSketch));
    REQUIRE(printed.find("(import hair \"hair.ips\")") != std::string::npos);
    REQUIRE(printed.find("(import eyes \"eyes.ips\")") != std::string::npos);
    REQUIRE(printed.find("(member hair draw)") != std::string::npos);
    REQUIRE(printed.find("(member eyes outputs pupil)") != std::string::npos);
    REQUIRE(printed.find("(at (vector 32 4))") != std::string::npos);
    REQUIRE(printed.find("(color hairColor)") != std::string::npos);
}

TEST_CASE("the printer renders else branches and else-if chains") {
    const std::string source = R"(
asset c {
  draw(v, a) {
    if (a) {
      x = 1
    } else {
      x = 2
    }
    if (b) {
      y = 3
    } else if (c) {
      y = 4
    } else {
      y = 5
    }
  }
})";
    const std::string expected = R"((program
  (asset c
    (fn draw (v a)
      (if a
        (let x 1)
        (else
          (let x 2)))
      (if b
        (let y 3)
        (else
          (if c
            (let y 4)
            (else
              (let y 5))))))))
)";
    REQUIRE(printProgram(parse(source)) == expected);
}

TEST_CASE("the printer renders literal atoms and unary operators") {
    const std::string source = R"(
asset l {
  draw(v, a) {
    s = "text"
    t = true
    f = false
    n = !x
    m = ~x
    q = -x
  }
})";
    const std::string printed = printProgram(parse(source));
    REQUIRE(printed.find("(let s \"text\")") != std::string::npos);
    REQUIRE(printed.find("(let t true)") != std::string::npos);
    REQUIRE(printed.find("(let f false)") != std::string::npos);
    REQUIRE(printed.find("(let n (not x))") != std::string::npos);
    REQUIRE(printed.find("(let m (bitnot x))") != std::string::npos);
    REQUIRE(printed.find("(let q (neg x))") != std::string::npos);
}

TEST_CASE("the printer renders member chains with non-identifier bases") {
    // f().g — a call as the base of a member access.
    const std::string source = R"(
asset m {
  draw(v, a) {
    z = f().g
  }
})";
    const std::string printed = printProgram(parse(source));
    REQUIRE(printed.find("(let z (member (call f) g))") != std::string::npos);
}

TEST_CASE("the printer renders return values and bare returns") {
    const std::string source = R"(
asset r {
  draw(v, a) {
    return
  }
  helper(p) {
    return p
  }
})";
    const std::string printed = printProgram(parse(source));
    REQUIRE(printed.find("(return)") != std::string::npos);
    REQUIRE(printed.find("(return p)") != std::string::npos);
}

TEST_CASE("the printer renders list outputs and while loops") {
    const std::string source = R"(
asset loop {
  output list point pts
  draw(v, a) {
    i = 0
    while (i < 10) {
      i = i + 1
    }
  }
})";
    const std::string printed = printProgram(parse(source));
    REQUIRE(printed.find("(output list point pts)") != std::string::npos);
    REQUIRE(printed.find("(while (< i 10)") != std::string::npos);
    REQUIRE(printed.find("(let i (+ i 1))") != std::string::npos);
}
