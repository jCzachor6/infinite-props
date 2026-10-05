#include <catch2/catch_test_macros.hpp>

#include "SourceCompilation.hpp"
#include "Compiler.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"

#include <map>
#include <stdexcept>

using namespace iprop;

TEST_CASE("full source pipeline owns snapshots and loads each shared import once") {
    const std::map<std::string,std::string> sources{
        {"entry.ips",R"(import left from "part.ips"; import right from "part.ips";
            asset t { size 8x8; draw { left.draw(seed,v,a,at={0,0}) right.draw(seed,v,a,at={0,0}) } })"},
        {"part.ips",R"(asset p { size 8x8; input i32 n=-7; output point value;
            draw { value={n,unit()} } })"}
    };
    for (bool optimize : {false,true}) for (bool strip : {false,true}) for (bool compress : {false,true}) {
        std::map<std::string,int> reads;
        auto result = compileSource("entry.ips", [&](std::string_view path) {
            ++reads[std::string(path)];
            return sources.at(std::string(path));
        }, {optimize,strip,compress});
        REQUIRE(result.succeeded());
        CHECK(reads.at("entry.ips") == 1);
        CHECK(reads.at("part.ips") == 1);
        CHECK(result.sources == sources);
        CHECK(result.diagnostics.empty());
        const auto& entry=result.programs.at("entry.ips");
        const auto& part=result.programs.at("part.ips");
        auto expected=compileProgram(entry,{{"left",&part},{"right",&part}},optimize);
        if (strip) expected=stripDebug(expected);
        if (compress) expected=compressFile(expected);
        CHECK(result.bytes == expected);
        const auto file=loadFile(result.bytes);
        CHECK_NOTHROW(validate(file));
        VM vm(file,{});
        vm.run();
        REQUIRE(vm.result().outputs.size()==2);
        CHECK(vm.result().outputs[0].points[0].x == -7.f);
    }
}

TEST_CASE("source pipeline retains the exact failing import snapshot") {
    int reads=0;
    const auto result=compileSource("entry.ips",[&](std::string_view path) {
        if (path=="entry.ips") return std::string(R"(import p from "bad.ips"
            asset t { size 8x8 draw { p.draw(seed,v,a,at={0,0}) } })");
        ++reads;
        return reads==1 ? std::string("asset bad { size 8x8 draw { value= } }") :
                         std::string("asset corrected { size 8x8 draw{} }");
    });
    CHECK(result.status==SourceCompilationStatus::InvalidSource);
    CHECK(result.bytes.empty());
    CHECK(reads==1);
    REQUIRE(result.diagnostics.size()==1);
    CHECK(result.diagnostics[0].path=="bad.ips");
    CHECK(result.diagnostics[0].location.has_value());
    CHECK(result.sources.at("bad.ips").find("value= }")!=std::string::npos);
}

TEST_CASE("check pipeline reaches planner and reports missing sources without fake ranges") {
    const auto planner=compileSource("entry.ips",[](std::string_view) {
        return std::string("asset t { size 8x8 origin v,0 draw{} }");
    });
    CHECK(planner.status==SourceCompilationStatus::InvalidSource);
    CHECK(planner.bytes.empty());
    REQUIRE(planner.diagnostics.size()==1);
    CHECK(planner.diagnostics[0].message.find("origin must be a constant")!=std::string::npos);
    const auto missing=compileSource("missing.ips",[](std::string_view)->std::string {
        throw std::runtime_error("absent");
    });
    CHECK(missing.status==SourceCompilationStatus::InvalidSource);
    REQUIRE(missing.diagnostics.size()==1);
    CHECK(!missing.diagnostics[0].location.has_value());
    CHECK(missing.bytes.empty());
}
