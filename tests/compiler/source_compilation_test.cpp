#include <catch2/catch_test_macros.hpp>

#include "SourceCompilation.hpp"
#include "Compiler.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/SourceError.hpp"
#include "ips/SourceText.hpp"
#include "ips/Semantics.hpp"

#include <map>
#include <algorithm>
#include <stdexcept>

using namespace iprop;

TEST_CASE("semantic recovery reports independent errors and suppresses unknown-type cascades") {
    const std::string source=R"(asset t { size 8x8 draw {
        first=missing; dependent=first+1; dependent2=dependent+1;
        if(first){skipped=4;} skippedDependent=skipped+1;
        independent=another; stable=1; stable=#fff; pixel(stable,0,#fff);
        first=2; recovered=first+1;
    } })";
    const auto result=compileSource("t.ips",[&](std::string_view) { return source; });
    CHECK(result.status==SourceCompilationStatus::InvalidSource);
    CHECK(result.bytes.empty());
    REQUIRE(result.diagnostics.size()==3);
    CHECK(result.diagnostics[0].message.find("missing")!=std::string::npos);
    CHECK(result.diagnostics[1].message.find("another")!=std::string::npos);
    CHECK(result.diagnostics[2].expectedType=="i32");
    CHECK(result.diagnostics[2].actualType=="color");
    for (const auto& diagnostic : result.diagnostics) {
        CHECK(diagnostic.stage==ips::DiagnosticStage::Typing);
        CHECK(diagnostic.sourceId==ips::sourceContentId(source));
    }
    CHECK_THROWS_AS(ips::checkAsset(ips::parse(source),{}),ips::SourceError);
}

TEST_CASE("recovering lazy helper checks restore caller locals and loop depth") {
    const std::string source=R"(asset t { input i32 v = 0  size 8x8
        draw { value=1; while(v){ value=broken(value); break; inner=unknown; }
               pixel(value,0,#fff); break; last=unrelated; }
        broken(x:i32)->i32 { failed=notDefined; return failed; }
    })";
    const auto result=compileSource("t.ips",[&](std::string_view) { return source; });
    REQUIRE(result.diagnostics.size()==4);
    CHECK(result.diagnostics[0].message.find("notDefined")!=std::string::npos);
    CHECK(result.diagnostics[1].message.find("unknown")!=std::string::npos);
    CHECK(result.diagnostics[2].message.find("outside a loop")!=std::string::npos);
    CHECK(result.diagnostics[3].message.find("unrelated")!=std::string::npos);
    CHECK(result.bytes.empty());
}

TEST_CASE("default and statement diagnostics share a bounded global budget") {
    const std::string source=R"(asset t { size 8x8 input u32 one=-1;
        input i32 two=2147483648; draw { x=unknown; y=another; } })";
    auto loader=[&](std::string_view) { return source; };
    const auto result=compileSource("t.ips",loader);
    REQUIRE(result.diagnostics.size()==4);
    CHECK(result.diagnostics[0].code==ips::DiagnosticCode::UnsignedDefaultNegative);
    CHECK(result.diagnostics[1].code==ips::DiagnosticCode::SignedDefaultRange);
    for (std::size_t limit : {0u,1u,2u,3u}) {
        SourceCompilationOptions options;
        options.diagnosticLimit=limit;
        const auto bounded=compileSource("t.ips",loader,options);
        CHECK(bounded.diagnostics.size()==std::max<std::size_t>(1,limit));
        CHECK(bounded.bytes.empty());
    }
    std::string many="asset t { size 8x8 draw {";
    for (int i=0;i<100;++i) many+="local"+std::to_string(i)+"=missing"+std::to_string(i)+";";
    many+="} }";
    SourceCompilationOptions options;
    options.diagnosticLimit=100;
    CHECK(compileSource("t.ips",[&](std::string_view) { return many; },options).diagnostics.size()==20);
}

TEST_CASE("independent import diagnostics have their own snapshots without duplicate alias reports") {
    const std::map<std::string,std::string> sources{
        {"entry.ips",R"(import one from "one.ips"; import again from "one.ips";
            import two from "two.ips"; asset t { size 8x8 draw{root=missing;} })"},
        {"one.ips","asset t {size 8x8 draw{one=bad; dependent=one+1;}}"},
        {"two.ips","asset t {size 8x8 draw{two=bad;}}"}
    };
    std::map<std::string,int> reads;
    const auto result=compileSource("entry.ips",[&](std::string_view path) {
        ++reads[std::string(path)];
        return sources.at(std::string(path));
    });
    REQUIRE(result.diagnostics.size()==3);
    CHECK(result.diagnostics[0].path=="one.ips");
    CHECK(result.diagnostics[1].path=="two.ips");
    CHECK(result.diagnostics[2].path=="entry.ips");
    for (const auto& diagnostic : result.diagnostics)
        CHECK(diagnostic.sourceId==ips::sourceContentId(sources.at(diagnostic.path)));
    for (const auto& [path,count] : reads) { (void)path; CHECK(count==1); }
    CHECK(result.bytes.empty());
}

TEST_CASE("import discovery collects parse failures without exposing damaged programs") {
    const std::map<std::string,std::string> sources{
        {"entry.ips",R"(import one from "one.ips"; import two from "two.ips";
            asset t { size 8x8 draw{root=;} })"},
        {"one.ips","asset t {size 8x8 draw{one=; two=;}}"},
        {"two.ips","asset t {size 8x8 draw{three=; four=;}}"}
    };
    const auto result=compileSource("entry.ips",[&](std::string_view path) {
        return sources.at(std::string(path));
    });
    REQUIRE(result.diagnostics.size()==5);
    CHECK(result.sources==sources);
    CHECK(result.programs.empty());
    CHECK(result.bytes.empty());
    for (const auto& diagnostic : result.diagnostics) {
        CHECK(diagnostic.stage==ips::DiagnosticStage::Parser);
        CHECK(diagnostic.sourceId==ips::sourceContentId(sources.at(diagnostic.path)));
    }
}

TEST_CASE("late validation errors remain structured check diagnostics") {
    const auto result=compileSource("late.ips",[](std::string_view) {
        return std::string("asset t {size 8x8 incomplete()->i32{} draw{x=incomplete();}}");
    });
    CHECK(result.status==SourceCompilationStatus::InvalidSource);
    REQUIRE(result.diagnostics.size()==1);
    CHECK(result.diagnostics[0].code==ips::DiagnosticCode::CompiledValidation);
    CHECK(result.diagnostics[0].stage==ips::DiagnosticStage::Validation);
    CHECK(result.bytes.empty());
}

TEST_CASE("diagnostic exit classes ignore warnings and keep error identities") {
    using namespace ips;
    Diagnostic warning{"hint",{},std::nullopt,DiagnosticCode::TypeError,DiagnosticStage::Typing,
                       DiagnosticSeverity::Warning};
    CHECK(diagnosticExitCode(diagnosticStatusFor({warning}))==0);
    auto error=warning;
    error.severity=DiagnosticSeverity::Error;
    CHECK(diagnosticExitCode(diagnosticStatusFor({warning,error}))==1);
    error.stage=DiagnosticStage::CLI;
    error.code=DiagnosticCode::CLIArgument;
    CHECK(diagnosticExitCode(diagnosticStatusFor({error}))==2);
    error.stage=DiagnosticStage::Internal;
    error.code=DiagnosticCode::InternalError;
    CHECK(diagnosticExitCode(diagnosticStatusFor({error}))==3);
}

TEST_CASE("signed-default fix-it is bounded and bound to its owning snapshot") {
    const std::string source="asset t { size 8x8 input u32 n=-7 draw{} }";
    const auto result=compileSource("t.ips",[&](std::string_view) { return source; });
    REQUIRE(result.diagnostics.size()==1);
    const auto& diagnostic=result.diagnostics[0];
    REQUIRE(diagnostic.fixIts.size()==1);
    const auto& fix=diagnostic.fixIts[0];
    CHECK(fix.sourceId==ips::sourceContentId(source));
    CHECK(fix.replacement=="i32");
    CHECK(source.substr(fix.range.start.byteOffset,fix.range.end.byteOffset-fix.range.start.byteOffset)=="u32");
    auto repaired=source;
    repaired.replace(fix.range.start.byteOffset,fix.range.end.byteOffset-fix.range.start.byteOffset,fix.replacement);
    CHECK(compileSource("t.ips",[&](std::string_view) { return repaired; }).succeeded());
    const auto invalid=compileSource("t.ips",[](std::string_view) {
        return std::string("asset t {size 8x8 input u32 n=-2147483649 draw{}}");
    });
    REQUIRE(invalid.diagnostics.size()==1);
    CHECK(invalid.diagnostics[0].fixIts.empty()); // changing type cannot repair an out-of-range default
}

TEST_CASE("source identities and ranges preserve exact UTF-8 bytes including CRLF") {
    CHECK(ips::sourceContentId("")=="sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(ips::sourceContentId("abc")=="sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(ips::sourceContentId("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")==
          "sha256:248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(ips::sourceContentId(std::string(1000000,'a'))==
          "sha256:cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // An astral scalar is one column and four bytes; no position splits it.
    const std::string glyph="// żółć\r\n📦 t { }";
    const auto rejected=compileSource("glyph.ips",[&](std::string_view) { return glyph; });
    REQUIRE(rejected.diagnostics.size()==1);
    REQUIRE(rejected.diagnostics[0].range);
    const auto& glyphRange=*rejected.diagnostics[0].range;
    CHECK(glyphRange.start.line==2);
    CHECK(glyphRange.start.column==1);
    CHECK(glyphRange.end.column==2);
    CHECK(glyph.substr(glyphRange.start.byteOffset,
          glyphRange.end.byteOffset-glyphRange.start.byteOffset)=="📦");
    CHECK(!ips::sourcePositionAt(glyph,glyphRange.start.byteOffset+1));
    const std::string source="// żółć\r\nasset t { size 8x8; draw {\tvalue= } }";
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
            asset t { size 8x8; draw { left.draw(seed,at={0,0}) right.draw(seed,at={0,0}) } })"},
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
            asset t { size 8x8 draw { p.draw(seed,at={0,0}) } })");
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
