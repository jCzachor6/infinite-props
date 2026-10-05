#include <catch2/catch_test_macros.hpp>

#include "SourceCompilation.hpp"
#include "Compiler.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/SourceError.hpp"
#include "ips/SourceText.hpp"

#include <map>
#include <stdexcept>

using namespace iprop;

TEST_CASE("source identities and ranges preserve exact UTF-8 bytes including CRLF") {
    CHECK(ips::sourceContentId("")=="sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(ips::sourceContentId("abc")=="sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(ips::sourceContentId("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")==
          "sha256:248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(ips::sourceContentId(std::string(1000000,'a'))==
          "sha256:cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    const std::string source="// żółć\r\n📦 t { size 8x8; ✏ {\tvalue= } }";
    const auto tokens=ips::lex(source);
    const auto assetRange=ips::sourceRangeAt(source,tokens[0].location);
    REQUIRE(assetRange);
    CHECK(assetRange->start.line==2);
    CHECK(assetRange->start.column==1);
    CHECK(assetRange->end.column==2);
    CHECK(source.substr(assetRange->start.byteOffset,
          assetRange->end.byteOffset-assetRange->start.byteOffset)=="📦");
    CHECK(!ips::sourcePositionAt(source,assetRange->start.byteOffset+1));
    const auto result=compileSource("unicode.ips",[&](std::string_view) { return source; });
    REQUIRE(result.diagnostics.size()==1);
    const auto& diagnostic=result.diagnostics[0];
    REQUIRE(diagnostic.range);
    CHECK(source.substr(diagnostic.range->start.byteOffset,
          diagnostic.range->end.byteOffset-diagnostic.range->start.byteOffset)=="}");
    CHECK(diagnostic.sourceId==ips::sourceContentId(source));
    const auto eof=compileSource("eof.ips",[](std::string_view) { return std::string("asset t {"); });
    REQUIRE(eof.diagnostics.size()==1);
    REQUIRE(eof.diagnostics[0].range);
    CHECK(eof.diagnostics[0].range->start.byteOffset==9);
    CHECK(eof.diagnostics[0].range->end.byteOffset==9);
}

TEST_CASE("structured diagnostic codes come from causes and survive message changes") {
    using ips::DiagnosticCode;
    using ips::DiagnosticStage;
    struct Case { const char* source; DiagnosticCode code; DiagnosticStage stage; };
    const Case cases[] = {
        {"asset t { size 8x8 draw { n= } }", DiagnosticCode::ExpectedToken, DiagnosticStage::Parser},
        {"asset t { size 8x8 draw { @ } }", DiagnosticCode::LexicalError, DiagnosticStage::Lexer},
        {"asset t { size 8x8 input i32 n=2147483648 draw{} }", DiagnosticCode::SignedDefaultRange, DiagnosticStage::Typing},
        {"asset t { size 8x8 input u32 n=-1 draw{} }", DiagnosticCode::UnsignedDefaultNegative, DiagnosticStage::Typing},
        {"asset t { size 8x8 draw { n=1 n=0.5 } }", DiagnosticCode::TypeError, DiagnosticStage::Typing},
        {"asset t { size 8x8 draw { n=1 n=#fff } }", DiagnosticCode::TypeError, DiagnosticStage::Typing},
        {"asset t { size 8x8 origin v,0 draw{} }", DiagnosticCode::PlanError, DiagnosticStage::Planning},
    };
    for (const auto& test : cases) {
        const auto result=compileSource("t.ips",[&](std::string_view) { return std::string(test.source); });
        REQUIRE(result.diagnostics.size()==1);
        CHECK(result.diagnostics[0].code==test.code);
        CHECK(result.diagnostics[0].stage==test.stage);
        CHECK(ips::diagnosticCodeName(result.diagnostics[0].code).starts_with("IPS-"));
    }
    const auto internal=compileSource("t.ips",[](std::string_view)->std::string {
        throw std::logic_error("injected unexpected loader failure");
    });
    CHECK(internal.status==SourceCompilationStatus::InternalError);
    REQUIRE(internal.diagnostics.size()==1);
    CHECK(internal.diagnostics[0].code==DiagnosticCode::InternalError);
    CHECK(!internal.diagnostics[0].location);
    const ips::Diagnostic warning{"readable warning",{},ips::SourceLocation{1,2},
        DiagnosticCode::TypeError,DiagnosticStage::Typing,ips::DiagnosticSeverity::Warning};
    CHECK(ips::formatDiagnostic("abc",warning)=="1:2: warning: readable warning\nabc\n ^\n");
}

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
