// Isolated allocator replacement: no test framework allocations occur while
// faults are armed. Every measured formatter allocation fails in each mode.
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "../support/fault_injection.hpp"

#include <array>
#include <cstdio>
#include <limits>
#include <string>

using namespace fault_injection;

int main() {
    const std::string longComment = "/* " + std::string(4096, 'X') + " */";
    const std::array sources{
        "asset t { size 8x8 draw { rect(color=#fff,h=range(1,2),x=0,w=1,y=0) "
            + longComment + " } }",
        longComment + "\nimport tile from \"tile.ips\"; " + longComment + "\n"
            "asset t { size 4x4; " + longComment + " size 8x8\n"
            "origin 1,2; " + longComment + " origin (0),(0)\n"
            "size 1x1; " + longComment + " size 2x2\n"
            "origin 1,1; " + longComment + " origin 2,2\n"
            "input color c=(#fff " + longComment + ")\n"
            "output point p; " + longComment + "\n"
            "palette colors=[#fff, " + longComment + " #000]\n"
            "draw { x=((1 " + longComment + ")); " + longComment + "\n"
            "if(true) {} else { " + longComment + " }\n"
            "rect(color=c, " + longComment + " h=2,x=0,w=1,y=0) }\n"
            "helper(n:i32)->i32 { return n+1; " + longComment + " } }\n" + longComment
    };
    std::size_t faults = 0;
    for (std::size_t fixture = 0; fixture < sources.size(); ++fixture) {
        const auto program = iprop::ips::parse(sources[fixture]);
        // Warm any library initialization before counting the same call shape.
        const auto expected = iprop::ips::formatProgram(program);
        arm(std::numeric_limits<std::size_t>::max(), false);
        const auto measured = iprop::ips::formatProgram(program);
        armed = false;
        const auto count = allocations;
        if (count == 0 || measured != expected ||
            iprop::ips::formatProgram(iprop::ips::parse(expected)) != expected) {
            std::fprintf(stderr, "unstable formatting: fixture %zu, %zu allocations\n", fixture, count);
            return 1;
        }
        for (const bool mode : {false, true}) {
            for (std::size_t at = 0; at < count; ++at) {
                arm(at, mode);
                bool caught = false;
                try {
                    const auto output = iprop::ips::formatProgram(program);
                    armed = false;
                    if (output != expected) {
                        std::fprintf(stderr, "fault changed the output: fixture %zu, allocation %zu, persistent %d\n",
                            fixture, at, mode);
                        return 2;
                    }
                } catch (const std::bad_alloc&) {
                    armed = false;
                    caught = true;
                } catch (...) {
                    armed = false;
                    std::fprintf(stderr, "unexpected exception: fixture %zu, allocation %zu, persistent %d\n",
                        fixture, at, mode);
                    return 3;
                }
                if (!triggered || !caught) {
                    std::fprintf(stderr, "fault not propagated: fixture %zu, allocation %zu, persistent %d\n",
                        fixture, at, mode);
                    return 4;
                }
                ++faults;
            }
        }
        // A failed formatter call must not mutate the caller's source snapshot.
        if (iprop::ips::formatProgram(program) != expected) {
            std::fprintf(stderr, "failed formatting changed the source: fixture %zu\n", fixture);
            return 5;
        }
        std::printf("fixture %zu: %zu allocations, both fault modes contained\n", fixture, count);
    }
    std::printf("%zu allocation failures contained\n", faults);
}
