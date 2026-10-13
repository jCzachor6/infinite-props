#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Samplers.hpp"
#include "iprop/VmInternals.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "support/compiler_support.hpp"
#include <bit>

using namespace iprop;
using namespace compiler_test;
namespace {
std::vector<u8> samplers(const std::string& source, bool optimize = true) {
    auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}
void sameSamples(const std::vector<u8>& actual, const std::vector<u8>& reference, u32 seed) {
    const auto a = execute(actual, seed), b = execute(reference, seed);
    REQUIRE(a.canvas.pixels == b.canvas.pixels);
    REQUIRE(a.outputs.size() == b.outputs.size());
    for (std::size_t i = 0; i < a.outputs.size(); ++i) {
        REQUIRE(a.outputs[i].points.size() == b.outputs[i].points.size());
        for (std::size_t n = 0; n < a.outputs[i].points.size(); ++n) {
            CHECK(std::bit_cast<u32>(a.outputs[i].points[n].x) == std::bit_cast<u32>(b.outputs[i].points[n].x));
            CHECK(std::bit_cast<u32>(a.outputs[i].points[n].y) == std::bit_cast<u32>(b.outputs[i].points[n].y));
        }
    }
}
void rejectSampler(const std::string& body, const std::string& diagnostic) {
    try { (void)samplers("asset t {size 1x1 " + body + "}"); FAIL("invalid sampler accepted"); }
    catch (const ips::SourceError& error) {
        INFO(body << " -> " << error.what());
        CHECK(std::string(error.what()).find(diagnostic) != std::string::npos);
        CHECK(error.location.line > 0);
        CHECK(error.location.column > 0);
    }
}
std::string asset(const std::string& body, const std::string& helpers = {}) {
    return "asset t {size 16x16 output list point values output point state " + helpers +
        " draw{" + body + " state={seed&65535,seed>>16}}}";
}
struct DrawCase { const char* type; const char* range; const char* publish; };
const DrawCase cases[] = {
    {"i32", "16777217", "values={value&65535,value>>16}"},
    {"f32", "-0.0", "values={value,value}"},
    {"color", "#12345678", "values={value.r,value.g} values={value.b,value.a}"},
    {"vec2", "{3,4}", "values=value"},
    {"i32", "10 to 15", "values={value&65535,value>>16}"},
    {"i32", "-2147483648 to 0", "values={value&65535,value>>16}"},
    {"i32", "-2147483648 to 2147483647", "values={value&65535,value>>16}"},
    {"i32", "40 to 40", "values={value&65535,value>>16}"},
    {"f32", "-3 to 4.5", "values={value,value}"},
    {"f32", "-0.0 to 0.0", "values={value,value}"},
    {"f32", "(-1.1754943508222875e-38*0.5) to (1.1754943508222875e-38*0.5)", "values={value,value}"},
    {"color", "#12345678 to #fedcba98", "values={value.r,value.g} values={value.b,value.a} pixel(i%16,i/16,value)"},
    {"vec2", "{-3,4} to {5,-6}", "values=value"},
    {"i32", "10 to 30 with gaussian(20,3)", "values={value&65535,value>>16}"},
    {"f32", "0.0 to 1.0 with triangular(0,0.75,1)", "values={value,value}"},
    {"f32", "0.0 to 1.0 with beta(2,3)", "values={value,value}"},
    {"color", "#12345678 to #fedcba98 with gaussian(0.5,0.2)", "values={value.r,value.g} values={value.b,value.a} pixel(i%16,i/16,value)"},
    {"vec2", "{-3,4} to {5,-6} with beta(2,3)", "values=value"},
};
}

TEST_CASE("samplers replay scalar and batch draws through the existing range kernels", "[sampler]") {
    for (const auto& sample : cases) {
        INFO(sample.range);
        for (const int count : {0, 1, 3, 256}) {
            const auto number = std::to_string(count);
            const auto reference = samplers(asset("for i in 0.." + number + " {value=" + sample.range + " " + sample.publish + "}"), false);
            const auto helpers = "echo(s:sampler<" + std::string(sample.type) + ">)->sampler<" + sample.type + ">{return s}";
            for (const bool optimize : {false, true}) {
                const auto batch = samplers(asset("s=echo(sampler((" + std::string(sample.range) + "))) items=s.pick(n=" + number + ") "
                    "for i in 0..list.len(items){value=items[i] " + sample.publish + "}", helpers), optimize);
                const auto singles = samplers(asset("s=echo(sampler(" + std::string(sample.range) + ")) "
                    "for i in 0.." + number + "{value=s.pick() " + sample.publish + "}", helpers), optimize);
                for (const u32 seed : {0u, 1u, 26u, 0x80000000u, 0xffffffffu}) {
                    sameSamples(batch, reference, seed);
                    sameSamples(singles, reference, seed);
                }
            }
        }
    }
}

TEST_CASE("sampler capture snapshots values once and handle transfer does not draw", "[sampler]") {
    const auto source = asset(R"(
      lo=10 hi=15 d=gaussian(12,2) before=seed
      sampler<i32> s=sampler(lo to hi with d)
      copied=identity(s) descriptions=echo([copied,sampler(40),sampler(20 to 30)])
      list<sampler<i32>> empty=[]
      lo=100 hi=150 d=gaussian(125,20)
      values={seed==before,list.len(descriptions)==3 && list.len(empty)==0}
      value=descriptions[0].pick() values={value,value}
      before=seed constants=descriptions[1].pick(256)
      values={constants[0],seed==before && constants[255]==40}
      copy=valuesOf(constants) values={copy[0],copy[255]}
    )", R"(
      identity(s:sampler<i32>)->sampler<i32>{return s}
      echo(items:list<sampler<i32>>)->list<sampler<i32>>{return items}
      valuesOf(items:list<i32>)->list<i32>{return items}
    )");
    const auto reference = samplers(asset(R"(
      values={1,1} value=10 to 15 with gaussian(12,2) values={value,value}
      values={40,1} values={40,40}
    )"), false);
    for (const bool optimize : {false,true}) {
        const auto bytes = samplers(source, optimize);
        for (const u32 seed : {0u,26u,0xffffffffu}) sameSamples(bytes,reference,seed);
        sameSamples(assemble(disassemble(bytes)),bytes,26);
        sameSamples(compressFile(bytes),bytes,26);
        const auto formatted = ips::formatProgram(ips::parse(source));
        CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
        sameSamples(samplers(formatted,optimize),bytes,26);
    }
}

TEST_CASE("sampler receivers and counts evaluate once in order before batch preflight", "[sampler]") {
    const std::string helpers = R"(
      description()->sampler<i32>{return sampler((10 to 15) to (20 to 30) with gaussian(float(15 to 18),2))}
      count()->i32{return 1 to 3}
    )";
    const auto source = asset("items=description().pick(count()) for i in 0..list.len(items){value=items[i] values={value,value}}",helpers);
    const auto reference = samplers(asset(R"(
      lo=10 to 15 hi=20 to 30 d=gaussian(float(15 to 18),2) n=1 to 3
      for i in 0..n {value=lo to hi with d values={value,value}}
    )"),false);
    for (const bool optimize : {false,true}) for (const u32 seed : {0u,1u,26u,0xffffffffu})
        sameSamples(samplers(source,optimize),reference,seed);
    // Explicit calls remain distinct and short circuit leaves the stream alone.
    const auto independent = asset(R"(
      s=sampler(10 to 15) skipped=0 && s.pick() alsoSkipped=1 || s.pick()
      values={skipped,alsoSkipped} value=pick(s.pick(),s.pick()) values={value,value}
    )");
    const auto eager = samplers(asset("values={0,1} value=pick(10 to 15,10 to 15) values={value,value}"),false);
    for (const bool optimize : {false,true}) for (const u32 seed : {0u,26u,0xffffffffu})
        sameSamples(samplers(independent,optimize),eager,seed);
}

TEST_CASE("sampler syntax types and known invalid arguments have located diagnostics", "[sampler]") {
    rejectSampler("draw{s=sampler(1 to 2) x=s.pick(-1)}", "between 0 and 256");
    rejectSampler("draw{s=sampler(1 to 2) x=s.pick(max(257,258))}", "between 0 and 256");
    rejectSampler("const i32 tooMany=257 draw{s=sampler(1) x=s.pick(tooMany)}", "between 0 and 256");
    rejectSampler("draw{s=sampler(1 to 2) x=s.pick(1.0)}", "count must be i32");
    rejectSampler("draw{s=sampler(4 to 2)}", "lower bound exceeds");
    rejectSampler("draw{s=sampler(1.0 to float(0))}", "lower bound exceeds");
    rejectSampler("draw{s=sampler(0.0 to sqrt(-1))}", "must be finite");
    rejectSampler("input f32 hi=1 draw{s=sampler(sqrt(-1) to hi)}", "must be finite");
    rejectSampler("draw{s=sampler({sqrt(-1),0} to {1,2})}", "must be finite");
    rejectSampler("draw{s=sampler(0.0 to 1.0 with gaussian(0.5,-1))}", "sigma");
    rejectSampler("draw{x=1 x.pick()}", "needs a sampler");
    rejectSampler("draw{x=sampler(gaussian(0.5,1))}", "needs i32, f32, color or vec2");
    rejectSampler("draw{x=sampler([1,2])}", "needs i32, f32, color or vec2");
    rejectSampler("f(x:sampler<list<i32>>){} draw{}", "supports only");
    rejectSampler("draw{s=sampler(1) s=sampler(1.0)}", "cannot assign sampler<f32>");
    rejectSampler("draw{rect(0,0,sampler(1),1,#abc)}", "must be f32");
    rejectSampler("draw{s=sampler(sampler(1))}", "needs i32, f32, color or vec2");
    CHECK_NOTHROW(samplers("asset t{size 1x1 const sampler<i32> s=sampler(1) draw{}}"));
    rejectSampler("sampler(x:i32)->i32{return x} draw{s=sampler(1 to 2)}", "conflicts with helper");
    CHECK_NOTHROW(samplers("asset t{size 1x1 sampler(x:i32)->i32{return x} draw{x=sampler(1)}}"));
    const auto captured = samplers(asset("s=sampler((1 to 2)+3) value=s.pick() values={value,value}"));
    sameSamples(captured,samplers(asset("value=(1 to 2)+3 values={value,value}")),26);
}

TEST_CASE("sampler arena sharing and batch preflight preserve RNG and publish no partial list", "[sampler]") {
    LoadedFile file;
    const auto type = ValueType::sampler(ValueType::I32);
    file.collections.types = {type};
    file.code.functions.push_back({});
    detail::Execution ex(file);
    ex.frames.push_back({}); ex.fuel = 10000; ex.frame().stream = 26;
    ex.push(i32{10}); ex.push(i32{15});
    REQUIRE(detail::stepSamplers(ex,{Opcode::SamplerRange,0,0}));
    const auto handle = std::get<SamplerValue>(ex.pop());
    CHECK(ex.frame().stream == 26);
    CHECK(ex.resources.bytes == ResourceArena::samplerBytes());
    Function helper; helper.params = {type}; helper.results = {type};
    file.code.functions.push_back(helper);
    ex.push(handle); ex.callFunction(1);
    CHECK(std::get<SamplerValue>(ex.frame().locals[0]).data == handle.data);
    ex.push(ex.frame().locals[0]); ex.leaveFrame();
    CHECK(std::get<SamplerValue>(ex.pop()).data == handle.data);
    CHECK(ex.resources.bytes == ResourceArena::samplerBytes());
    const auto trapped = [&](detail::Execution& execution, i32 count, SamplerValue receiver, ErrorReason reason) {
        execution.frame().stack.clear(); execution.push(receiver); execution.push(count);
        const auto state = execution.frame().stream;
        const auto buffers = execution.resources.lists.size();
        try { detail::stepSamplers(execution,{Opcode::SamplerPickList,0,0}); FAIL("invalid batch accepted"); }
        catch (const Trap& error) { CHECK(error.reason == reason); }
        CHECK(execution.frame().stream == state);
        CHECK(execution.resources.lists.size() == buffers);
        CHECK(execution.frame().stack.size() <= 1); // only an unconsumed receiver, never a partial result
    };
    trapped(ex,-1,handle,ErrorReason::NumericRange);
    trapped(ex,257,handle,ErrorReason::NumericRange);
    trapped(ex,0,{type,{}},ErrorReason::RuntimeViolation);
    trapped(ex,-1,{type,{}},ErrorReason::NumericRange); // size precedes handle validity
    ex.frame().stack.clear(); ex.push(handle); ex.push(i32{0});
    const auto fuel = ex.fuel;
    REQUIRE(detail::stepSamplers(ex,{Opcode::SamplerPickList,0,0}));
    CHECK(ex.frame().stream == 26); CHECK(ex.fuel == fuel);
    CHECK(std::get<ListValue>(ex.pop()).data->elements.empty());
    CHECK(ex.resources.bytes == ResourceArena::samplerBytes() + ResourceArena::listBytes(0));
    ex.resources.bytes = kMaxGenerationData;
    trapped(ex,1,handle,ErrorReason::OutOfMemory);
    ex.resources.bytes = 0; ex.fuel = 2;
    trapped(ex,3,handle,ErrorReason::FuelExhausted);
    // A later fuel trap during draws keeps all partial elements private.
    ex.fuel = 3;
    trapped(ex,3,handle,ErrorReason::FuelExhausted);
    ex.frame().stack.clear(); ex.fuel=4;ex.push(handle);ex.push(i32{3});
    const auto before=ex.frame().stream;
    CHECK_THROWS_AS(detail::stepSamplers(ex,{Opcode::SamplerPickList,0,0}),Trap);
    CHECK(ex.frame().stream==before+0x9e3779b9u);
    CHECK(ex.frame().stack.empty());
    CHECK(ex.resources.lists.size()==1); // only the earlier successful empty list
}

TEST_CASE("sampler captures validate dynamic bounds before allocating or drawing", "[sampler]") {
    LoadedFile file; file.code.functions.push_back({});
    file.collections.types = {ValueType::sampler(ValueType::I32),ValueType::sampler(ValueType::F32),ValueType::sampler(ValueType::Vec2)};
    for (const auto& bad : std::vector<std::pair<u16,std::vector<Value>>>{
        {0,{i32{4},i32{2}}}, {1,{0.f,std::bit_cast<f32>(0x7f800000u)}},
        {2,{Vec2{0,0},Vec2{std::bit_cast<f32>(0x7fc00000u),1}}}}) {
        detail::Execution ex(file); ex.frames.push_back({}); ex.fuel=100; ex.frame().stream=26;
        for (const auto& value : bad.second) ex.push(value);
        CHECK_THROWS_AS(detail::stepSamplers(ex,{Opcode::SamplerRange,0,bad.first}),Trap);
        CHECK(ex.frame().stream==26); CHECK(ex.resources.bytes==0); CHECK(ex.resources.samplers.empty());
    }
    detail::Execution ex(file); ex.frames.push_back({}); ex.fuel=100;
    ex.push(0.f); ex.push(1.f); ex.push(Distribution{});
    CHECK_THROWS_AS(detail::stepSamplers(ex,{Opcode::SamplerWith,0,1}),Trap);
    CHECK(ex.resources.bytes==0);
    // Capture outside support succeeds. Empty batches do not ask for mass.
    Distribution shape;shape.family=DistributionFamily::Triangular;shape.parameters={10,15,20,0};
    ex.push(0.f);ex.push(1.f);ex.push(shape);
    REQUIRE(detail::stepSamplers(ex,{Opcode::SamplerWith,0,1}));
    const auto handle=std::get<SamplerValue>(ex.pop());
    ex.push(handle);ex.push(i32{0});
    CHECK_NOTHROW(detail::stepSamplers(ex,{Opcode::SamplerPickList,0,1}));
    ex.pop();ex.push(handle);
    CHECK_THROWS_AS(detail::stepSamplers(ex,{Opcode::SamplerPick,0,1}),Trap);
}

TEST_CASE("sampler descriptors preserve full nested item identity and reject malformed references", "[sampler]") {
    const auto bytes=samplers(asset("items=[sampler(1),sampler(2 to 4)] value=items[0].pick() values={value,list.len(items)}"),false);
    const auto loaded=loadFile(bytes); REQUIRE_NOTHROW(validate(loaded));
    CHECK(loaded.collections.types.size()==2);
    const auto section=loaded.layout.find(SectionId::Types)->offset;
    const auto bad16=[](std::vector<u8>& data,std::size_t at,u16 value){data.at(at)=static_cast<u8>(value);data.at(at+1)=static_cast<u8>(value>>8);};
    for(const auto [at,value]:std::vector<std::pair<std::size_t,u16>>{
        {section+6,1}, {section+10,1}, {section+10,65535}}) {
        auto bad=bytes;bad16(bad,at,value);CHECK_THROWS_AS(loadFile(bad),FormatError);
    }
    auto bad=bytes;bad.at(section+5)=4;CHECK_THROWS_AS(loadFile(bad),FormatError); // sampler<distribution>
    bad=bytes;bad16(bad,6,5);CHECK_THROWS_AS(loadFile(bad),FormatError);
    CHECK_THROWS_AS(assemble("meta: size=1x1\ntypes: list<i32>\nfn draw ( i32, i32 ) -> ( ):\n return\n sampler.pick 0\nendfn\n"),std::exception);
    CHECK_THROWS_AS(assemble("meta: size=1x1\ntypes: sampler<i32>\nfn draw ( i32, i32 ) -> ( ):\n return\n list.len 0\nendfn\n"),std::exception);
    for (const auto& signature:ips::samplerPickSpec().overloads) {
        CHECK(signature.params[0].type.isSampler());
        CHECK(findOpcode(*signature.opcode)->effects.rng==RngEffect::AdvanceCurrent);
        CHECK(findOpcode(*signature.opcode)->minimumMinor==6);
        CHECK(signature.result==(signature.params.size()==1?signature.params[0].type.item():ValueType::list(signature.params[0].type.item())));
    }
}

TEST_CASE("all sampler list types share helper data and construct 256 descriptions without stack overflow", "[sampler]") {
    const std::vector<std::pair<std::string,std::string>> literals{
        {"i32","16777217"},{"f32","-0.0"},{"color","#12345678"},{"vec2","{3,4}"}};
    for(const auto& [type,literal]:literals)for(const bool optimize:{false,true}){
        const auto helpers="echo(items:list<sampler<"+type+">>)->list<sampler<"+type+">>{return items} "
            "empty()->list<sampler<"+type+">>{return []}";
        std::string descriptions="[";
        for(int i=0;i<256;++i){if(i)descriptions+=',';descriptions+="sampler("+literal+")";}
        descriptions+=']';
        const auto source=asset("before=seed items=echo("+descriptions+") shared=items items=[] "
            "value=shared[255].pick() values={list.len(shared),list.len(items)+list.len(empty())} values={seed==before,seed==before}",helpers);
        const auto bytes=samplers(source,optimize);
        sameSamples(bytes,samplers(asset("values={256,0} values={1,1}")),26);
        sameSamples(assemble(disassemble(bytes)),bytes,26);
    }
    rejectSampler("draw{items=[sampler(1),sampler(1.0)]}","list element needs sampler<i32>");
    CHECK_NOTHROW(samplers(asset("chosen=pick(sampler(1),sampler(2 to 4)) value=chosen.pick() values={value,value}")));
}

TEST_CASE("sampler helper draws preserve imported part stream isolation", "[sampler]") {
    const auto part=ips::parse("asset child{size 1x1 output point value draw{s=sampler(20 to 30) value={s.pick(),s.pick()}}}");
    const auto oldPart=ips::parse("asset child{size 1x1 output point value draw{value={20 to 30,20 to 30}}}");
    const auto root=ips::parse("import child from \"child.ips\" "+asset("before=seed spec=sampler(10 to 15) values={childDraw(spec),seed==before}",
        "childDraw(spec:sampler<i32>)->i32{child.draw(7,0,0,at={0,0}) return spec.pick()}"));
    const auto oldRoot=ips::parse("import child from \"child.ips\" "+asset("before=seed values={childDraw(),seed==before}",
        "childDraw()->i32{child.draw(7,0,0,at={0,0}) return 10 to 15}"));
    for(const bool optimize:{false,true}){
        const auto bytes=compileProgram(root,{{"child",&part}},optimize);
        const auto reference=compileProgram(oldRoot,{{"child",&oldPart}},false);
        for(const u32 seed:{0u,26u,0xffffffffu})sameSamples(bytes,reference,seed);
    }
}

TEST_CASE("invalid dynamic sampler batches retain actual receiver and count effects", "[sampler]") {
    const auto bytes=samplers(R"(asset t{size 1x1
      receiver()->sampler<i32>{lo=10 to 15 return sampler(lo to 30)}
      count()->i32{discarded=20 to 30 return -1}
      draw{items=receiver().pick(count())}
    })",false);
    const auto file=loadFile(bytes);detail::Execution ex(file);
    ex.frames.push_back({});ex.frame().stream=26;ex.fuel=ex.budget=2000000;
    ex.frame().locals.assign(file.code.functions[0].params.size()+file.code.functions[0].locals.size(),i32{0});
    try{
        while(!ex.frames.empty()){
            if(ex.frame().ip>=ex.fn().code.size()/4){ex.leaveFrame();continue;}
            const auto instruction=decodeInstruction(ex.fn().code.subspan(ex.frame().ip*4,4));
            ex.spend(1);
            const bool dispatched=detail::dispatch(ex,instruction);
            REQUIRE(dispatched);
        }
        FAIL("invalid batch completed");
    }catch(const Trap& error){
        CHECK(error.reason==ErrorReason::NumericRange);
        REQUIRE(ex.frames.size()==1);
        CHECK(ex.frame().stream==26u+2u*0x9e3779b9u);
        CHECK(ex.resources.samplers.size()==1);CHECK(ex.resources.lists.empty());
    }
    // The public generation failure releases resources and cannot return or resume.
    VM vm(file,{26,0,0,0,0,{}});
    CHECK_THROWS_AS(vm.run(),Trap);
    CHECK_THROWS_AS(vm.result(),std::logic_error);
    CHECK_THROWS_AS(vm.run(),std::logic_error);
}
