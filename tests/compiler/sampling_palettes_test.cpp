#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VmInternals.hpp"
#include "ips/AssetConstants.hpp"
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
std::vector<u8> compilePalettes(const std::string& source, bool optimize = true) {
    auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}
void sameResults(const std::vector<u8>& actual, const std::vector<u8>& reference, u32 seed) {
    const auto a = execute(actual, seed), b = execute(reference, seed);
    REQUIRE(a.canvas.pixels == b.canvas.pixels);
    REQUIRE(a.outputs.size() == b.outputs.size());
    for (std::size_t slot = 0; slot < a.outputs.size(); ++slot) {
        REQUIRE(a.outputs[slot].points.size() == b.outputs[slot].points.size());
        for (std::size_t i = 0; i < a.outputs[slot].points.size(); ++i) {
            CHECK(std::bit_cast<u32>(a.outputs[slot].points[i].x) == std::bit_cast<u32>(b.outputs[slot].points[i].x));
            CHECK(std::bit_cast<u32>(a.outputs[slot].points[i].y) == std::bit_cast<u32>(b.outputs[slot].points[i].y));
        }
    }
}
void rejectPalette(const std::string& declarations, const std::string& message) {
    try { (void)compilePalettes("asset t{size 1x1 " + declarations + "}"); FAIL("invalid declaration accepted"); }
    catch (const ips::SourceError& error) {
        INFO(declarations << " -> " << error.what());
        CHECK(std::string(error.what()).find(message) != std::string::npos);
        CHECK(error.location.line > 0); CHECK(error.location.column > 0);
    }
}
std::string asset(const std::string& declarations, const std::string& body, const std::string& helpers = {}) {
    return "asset t{size 16x16 output list point values output point state " + declarations + helpers +
        " draw{" + body + " state={seed&65535,seed>>16}}}";
}
struct DrawCase { const char* type; const char* expression; const char* publish; };
const DrawCase draws[] = {
    {"i32", "16777217", "values={value&65535,value>>16}"},
    {"f32", "-0.0", "values={value,value}"},
    {"color", "#12345678", "values={value.r,value.g} values={value.b,value.a}"},
    {"vec2", "{3,4}", "values=value"},
    {"i32", "10 to 15", "values={value&65535,value>>16}"},
    {"i32", "-2147483648 to 0", "values={value&65535,value>>16}"},
    {"i32", "-2147483648 to 2147483647", "values={value&65535,value>>16}"},
    {"i32", "40 to 40", "values={value,value}"},
    {"f32", "-3 to 4.5", "values={value,value}"},
    {"f32", "-0.0 to 0.0", "values={value,value}"},
    {"f32", "(-1.1754943508222875e-38*0.5) to (1.1754943508222875e-38*0.5)", "values={value,value}"},
    {"i32", "10 to 30 with gaussian(20,3)", "values={value,value}"},
    {"f32", "0.0 to 1.0 with triangular(0,0.75,1)", "values={value,value}"},
    {"f32", "0.0 to 1.0 with beta(2,3)", "values={value,value}"},
    {"color", "#12345678 to #fedcba98", "values={value.r,value.g} values={value.b,value.a} pixel(i%16,i/16,value)"},
    {"color", "#12345678 to #fedcba98 with gaussian(0.5,0.2)", "values={value.r,value.g} values={value.b,value.a} pixel(i%16,i/16,value)"},
    {"vec2", "{-3,4} to {5,-6}", "values=value"},
    {"vec2", "{-3,4} to {5,-6} with beta(2,3)", "values=value"},
};
void put16(std::vector<u8>& bytes, std::size_t at, u16 value) {
    bytes.at(at) = static_cast<u8>(value); bytes.at(at + 1) = static_cast<u8>(value >> 8);
}
void put32(std::vector<u8>& bytes, std::size_t at, u32 value) {
    for (int i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<u8>(value >> (i * 8));
}
}

TEST_CASE("const samplers and palette captures replay existing single and batch kernels", "[sampling-palette]") {
    for (const auto& draw : draws) for (const int count : {0,1,3,256}) {
        INFO(draw.type << ": " << draw.expression << ", count " << count);
        const auto n = std::to_string(count);
        const auto reference = compilePalettes(asset("", "for i in 0.." + n + "{value=" + draw.expression + " " + draw.publish + "}"), false);
        const auto helpers = "echo(s:sampler<" + std::string(draw.type) + ">)->sampler<" + draw.type + ">{return s}";
        const std::string definitions[] = {
            "const sampler<" + std::string(draw.type) + "> description=sampler((" + draw.expression + "))",
            "palette specs=[sampler(" + std::string(draw.expression) + ")] const sampler<" + draw.type + "> description=specs[-1]",
            "const list<sampler<" + std::string(draw.type) + ">> specs=[sampler(" + draw.expression + ")] const sampler<" + draw.type + "> description=specs[0]"
        };
        for (const bool optimize : {false,true}) for (const auto& declaration : definitions) {
            const auto actual = compilePalettes(asset(declaration,
                "before=seed s=echo(description) copied=s empty=s.pick(0) values={seed==before,list.len(empty)} "
                "items=copied.pick(" + n + ") for i in 0..list.len(items){value=items[i] " + draw.publish + "}", helpers), optimize);
            const auto expected = compilePalettes(asset("", "values={1,0} for i in 0.." + n + "{value=" + draw.expression + " " + draw.publish + "}"), false);
            for (const u32 seed : {0u,1u,26u,0x80000000u,0xffffffffu}) sameResults(actual, expected, seed);
        }
        // The ordinary-range reference still compiles with pre-sampler lowering.
        CHECK_NOTHROW(validate(loadFile(reference)));
    }
}

TEST_CASE("range palettes lift constants and select once with legacy fallback", "[sampling-palette]") {
    const std::string declarations = R"(
      const sampler<i32> fixed=sampler(40)
      palette widths=[low to high,20 to 30 with shape,fixed]
      const distribution shape=gaussian(25,spread)
      const i32 high=15 const i32 low=10 const f32 spread=2
      const sampler<i32> selected=widths[-1]
      const list<sampler<i32>> copied=widths
      const i32 length=list.len(copied)
      const list<sampler<i32>> empty=[]
    )";
    for (const int index : {-2147483647,-1,0,1,2,3,2147483647}) for (const bool optimize : {false,true}) {
        const auto source = asset(declarations,
            "before=seed s=widths[" + std::to_string(index) + "] copy=s values={seed==before,length} "
            "items=copy.pick(3) for i in 0..3{values={items[i],items[i]}} "
            "before=seed constant=selected.pick(256) values={constant[255],seed==before} values={list.len(empty),0}");
        const auto chosen = index == 0 ? "10 to 15" : index == 1 ? "20 to 30 with gaussian(25,2)" : "40";
        const auto reference = asset("", "values={1,3} for i in 0..3{value=" + std::string(chosen) +
            " values={value,value}} values={40,1} values={0,0}");
        for (const u32 seed : {0u,1u,26u,0xffffffffu}) sameResults(compilePalettes(source,optimize),compilePalettes(reference,false),seed);
    }
    const auto effectful = compilePalettes(asset(declarations, "items=widths[select()].pick(count()) for i in 0..list.len(items){values={items[i],items[i]}}",
        "select()->i32{pixel(0,0,#f00) return index(5)-1} count()->i32{pixel(1,0,#0f0) return 1 to 3}"));
    const auto ordered = compilePalettes(asset("", "pixel(0,0,#f00) k=index(5)-1 pixel(1,0,#0f0) n=1 to 3 "
        "for i in 0..n{value=40 if(k==0){value=10 to 15} if(k==1){value=20 to 30 with gaussian(25,2)} values={value,value}}"),false);
    for (const u32 seed : {0u,1u,26u,0xffffffffu}) sameResults(effectful,ordered,seed);
}

TEST_CASE("sampling palettes widen numeric descriptions and allow vec2 values", "[sampling-palette]") {
    for (const bool optimize : {false,true}) {
        const auto actual = compilePalettes(asset(R"(
          const sampler<i32> integer=sampler(10 to 15)
          palette widths=[integer,20 to 30.5,40]
          palette points=[{3,4},{5,6}]
          palette directions=[{0,0} to {8,4} with beta(2,3),{3,4}]
        )", "value=widths[0].pick() values={value,value} value=widths[1].pick() values={value,value} "
            "values=points[-1] values=directions[0].pick() before=seed values=directions[-1].pick() values={seed==before,0}"),optimize);
        const auto expected = compilePalettes(asset("", "value=10.0 to 15.0 values={value,value} value=20.0 to 30.5 values={value,value} "
            "values={5,6} values={0,0} to {8,4} with beta(2,3) values={3,4} values={1,0}"),false);
        for (const u32 seed : {0u,1u,26u,0xffffffffu}) sameResults(actual,expected,seed);
    }
}

TEST_CASE("constant descriptions validate the complete declaration graph without drawing", "[sampling-palette]") {
    for (const auto& [declaration,message] : std::vector<std::pair<std::string,std::string>>{
        {"const sampler<i32> bad=sampler(seed) draw{}","only asset constants"},
        {"input i32 width=1 const sampler<i32> bad=sampler(width) draw{}","only asset constants"},
        {"helper()->i32{return 1} const sampler<i32> bad=sampler(helper()) draw{}","user helper"},
        {"const sampler<i32> bad=sampler(index(5)) draw{}","RNG or generation effects"},
        {"const sampler<i32> bad=sampler((1 to 2)+3) draw{}","random ranges are not allowed"},
        {"const sampler<i32> s=sampler(1) const i32 bad=false && s.pick() draw{}",".pick has RNG"},
        {"const sampler<i32> bad=sampler(4 to 2) draw{}","lower bound exceeds"},
        {"const sampler<f32> bad=sampler(0.0 to sqrt(-1)) draw{}","must be finite"},
        {"const sampler<vec2> bad=sampler({sqrt(-1),0}) draw{}","must be finite"},
        {"const sampler<f32> bad=sampler(0.0 to 1.0 with gaussian(0.5,0)) draw{}","sigma"},
        {"const sampler<i32> first=second const sampler<i32> second=first draw{}","cyclic const dependency"},
        {"const sampler<i32> first=specs[0] palette specs=[first] draw{}","cyclic const dependency"},
        {"palette specs=[index(5),1 to 2] draw{}","RNG or generation effects"},
        {"palette specs=[1 to 2 with gaussian(0,0),40] draw{}","sigma"},
        {"palette specs=[4 to 2,40] draw{}","lower bound exceeds"},
        {"palette specs=[1 to 2,#fff] draw{}","one type"},
        {"palette specs=[] draw{}","at least one"},
        {"palette tones=[#123 to #456] draw{x=ramp(tones,0.5)}","ramp does not accept sampling"},
        {"const list<sampler<color>> tones=[sampler(#123)] draw{x=ramp(tones,0.5)}","ramp does not accept sampling"},
        {"palette points=[{1,2}] draw{x=ramp(points,0.5)}","ramp requires a color or f32"},
        {"palette colors=[#123,#456] draw{x=colors[0].pick()}",".pick needs a sampler"},
        {"const sampler<i32> s=sampler(1) draw{s=sampler(2)}","cannot assign to const"},
        {"const list<sampler<i32>> specs=[sampler(1),2] draw{}","list element needs sampler"},
        {"const list<sampler<i32>> specs=[sampler(1)] const sampler<i32> bad=specs[-1] draw{}","list index out of range"},
    }) rejectPalette(declaration,message);
    const auto noMass = "const sampler<f32> outside=sampler(0.0 to 1.0 with triangular(10,15,20)) palette specs=[outside,0.5]";
    CHECK_NOTHROW(execute(compilePalettes(asset(noMass,"s=specs[0] empty=s.pick(0) value=specs[-1].pick() values={value,list.len(empty)}"))));
    const auto file = loadFile(compilePalettes(asset(noMass,"value=specs[0].pick()")));
    VM vm(file,{}); CHECK_THROWS_AS(vm.run(),Trap);
    CHECK_THROWS_AS(vm.result(),std::logic_error); CHECK_THROWS_AS(vm.run(),std::logic_error);
}

TEST_CASE("palette helper transfer uses checked lists and preserves short circuit and part streams", "[sampling-palette]") {
    const auto declarations = "palette specs=[10 to 15,40]";
    const auto helpers = "echo(items:list<sampler<i32>>)->list<sampler<i32>>{return items} selected(items:list<sampler<i32>>,i:i32)->sampler<i32>{return items[i]}";
    const auto actual = compilePalettes(asset(declarations,"before=seed items=echo(specs) s=selected(items,0) values={seed==before,0} "
        "ignored=false && s.pick()>0 for i in 0..3{values={s.pick(),s.pick()}}",helpers));
    const auto expected = compilePalettes(asset("","values={1,0} for i in 0..3{values={10 to 15,10 to 15}}"),false);
    for (const u32 seed : {0u,1u,26u,0xffffffffu}) sameResults(actual,expected,seed);
    for (const int index : {-1,2}) {
        const auto file = loadFile(compilePalettes(asset(declarations,"value=selected(specs,"+std::to_string(index)+").pick()",helpers)));
        VM vm(file,{}); CHECK_THROWS_AS(vm.run(),Trap);
    }
    const auto child = ips::parse("asset child{size 1x1 palette specs=[10 to 15,40] const sampler<i32> s=specs[0] output point p draw{p={s.pick(),s.pick()}}}");
    const auto root = ips::parse("import child from \"child.ips\" asset t{size 1x1 const sampler<i32> s=sampler(20 to 30) output point p output point state draw{before=seed child.draw(seed,at={0,0}) p={seed==before,s.pick()} state={seed&65535,seed>>16}}}");
    ips::checkAsset(child,{}); ips::checkAsset(root,{{"child",partInfoOf(child.asset)}});
    const auto bytes = compileProgram(root,{{"child",&child}});
    const auto reference = compilePalettes("asset t{size 1x1 output point p output point state draw{p={1,20 to 30} state={seed&65535,seed>>16}}}",false);
    for (const u32 seed : {0u,1u,26u,0xffffffffu}) {
        const auto a=execute(bytes,seed),b=execute(reference,seed);
        CHECK(a.outputs[0].points[0].x==1.f);
        CHECK(std::bit_cast<u32>(a.outputs[0].points[0].y)==std::bit_cast<u32>(b.outputs[0].points[0].y));
        CHECK(a.outputs[1].points[0].x==b.outputs[1].points[0].x); CHECK(a.outputs[1].points[0].y==b.outputs[1].points[0].y);
    }
}

TEST_CASE("constant sampler pools preserve bits sharing and deterministic transport", "[sampling-palette]") {
    const std::string source = R"(asset t{size 1x1 output point p
      const sampler<f32> minus=sampler(-0.0)
      const sampler<f32> plus=sampler(0.0)
      const sampler<f32> small=sampler(1.1754943508222875e-38*0.5)
      const sampler<f32> range=sampler(0.0 to 1.0 with beta(2,3))
      const list<sampler<f32>> specs=[minus,minus,plus,small,range]
      const list<sampler<f32>> empty=[]
      draw{items=specs copy=items s=minus t=items[0] u=range p={s.pick(),items[3].pick()} n=list.len(empty)}
    })";
    for (const bool optimize : {false,true}) {
        const auto bytes = compilePalettes(source,optimize);
        const auto file = loadFile(bytes);
        REQUIRE(file.collections.samplers.size()==4); REQUIRE(file.collections.lists.size()==2);
        const auto& elements=file.collections.lists[0].data->elements;
        CHECK(std::get<SamplerValue>(elements[0]).data==std::get<SamplerValue>(elements[1]).data);
        CHECK(std::bit_cast<u32>(std::get<f32>(std::get<SamplerValue>(elements[0]).data->lower))==0x80000000);
        CHECK(std::bit_cast<u32>(std::get<f32>(std::get<SamplerValue>(elements[2]).data->lower))==0);
        CHECK(std::bit_cast<u32>(std::get<f32>(std::get<SamplerValue>(elements[3]).data->lower))==0x00400000);
        CHECK(disassemble(bytes).find("const.sampler")!=std::string::npos);
        const auto restored=assemble(disassemble(bytes));
        const auto formatted=compilePalettes(ips::formatProgram(ips::parse(source)),optimize);
        CHECK(stripDebug(bytes)==stripDebug(formatted));
        for (const u32 seed : {0u,26u,0xffffffffu}) {
            sameResults(bytes,restored,seed); sameResults(bytes,compressFile(bytes),seed);
        }
    }
}

TEST_CASE("list parameters sharing a palette name retain their own values and bounds", "[sampling-palette]") {
    const auto declarations="palette widths=[10 to 15,40]";
    const auto helpers="copy(widths:list<sampler<i32>>)->list<sampler<i32>>{return widths} "
        "selected(widths:list<sampler<i32>>,i:i32)->sampler<i32>{return widths[i]}";
    for (const bool optimize : {false,true}) {
        const auto actual=compilePalettes(asset(declarations,"before=seed items=copy([sampler(7)]) "
            "values={selected(items,0).pick(),widths[-1].pick()} values={seed==before,list.len(items)}",helpers),optimize);
        const auto expected=compilePalettes(asset("","values={7,40} values={1,1}"),false);
        for (const u32 seed : {0u,1u,26u,0xffffffffu}) sameResults(actual,expected,seed);
        for (const int index : {-1,2}) {
            const auto file=loadFile(compilePalettes(asset(declarations,
                "value=selected(widths,"+std::to_string(index)+").pick()",helpers),optimize));
            VM vm(file,{});
            try {vm.run(); FAIL("parameter inherited palette fallback");}
            catch (const Trap& error) {CHECK(error.reason==ErrorReason::IndexRange);}
        }
    }
}

TEST_CASE("forward palette chains are typed and evaluated in dependency order", "[sampling-palette]") {
    std::string declarations="const sampler<i32> description=p0[0] ";
    for (int i=0;i<1499;++i) declarations+="palette p"+std::to_string(i)+"=[p"+std::to_string(i+1)+"[0]] ";
    declarations+="palette p1499=[10 to 15 with gaussian(12,2)]";
    for (const bool optimize : {false,true}) {
        const auto actual=compilePalettes(asset(declarations,"before=seed s=description values={seed==before,0} "
            "value=s.pick() values={value,value}"),optimize);
        CHECK(loadFile(actual).collections.samplers.size()==1);
        const auto expected=compilePalettes(asset("","values={1,0} value=10 to 15 with gaussian(12,2) values={value,value}"),false);
        for (const u32 seed : {0u,26u,0xffffffffu}) sameResults(actual,expected,seed);
    }
    rejectPalette("palette first=[second[0]] palette second=[first[0]] draw{}","cyclic const dependency");
}

TEST_CASE("constant description loading charges each buffer once and preflights quota", "[sampling-palette]") {
    const auto bytes=compilePalettes("asset t{size 1x1 const sampler<i32> s=sampler(10 to 15) const list<sampler<i32>> specs=[s,s,s] draw{x=s items=specs}}",false);
    const auto file=loadFile(bytes);
    REQUIRE(file.collections.samplers.size()==1); REQUIRE(file.collections.lists.size()==1);
    detail::Execution ex(file); ex.frames.push_back({}); ex.frame().stream=26; ex.fuel=100;
    REQUIRE(detail::stepSamplers(ex,{Opcode::ConstSampler,0,0}));
    const auto sampler=std::get<SamplerValue>(ex.pop());
    CHECK(ex.resources.bytes==256); CHECK(ex.frame().stream==26);
    REQUIRE(detail::stepCollections(ex,{Opcode::ConstList,0,0}));
    const auto list=std::get<ListValue>(ex.pop());
    CHECK(std::get<SamplerValue>(list.data->elements[0]).data==sampler.data);
    CHECK(ex.resources.bytes==256+ResourceArena::listBytes(3)); CHECK(ex.fuel==97);
    for (int i=0;i<100;++i) {
        REQUIRE(detail::stepSamplers(ex,{Opcode::ConstSampler,0,0})); ex.pop();
        REQUIRE(detail::stepCollections(ex,{Opcode::ConstList,0,0}));
        CHECK(std::get<ListValue>(ex.pop()).data==list.data);
    }
    CHECK(ex.resources.bytes==256+ResourceArena::listBytes(3)); CHECK(ex.fuel==97); CHECK(ex.frame().stream==26);
    for (const bool quota : {false,true}) {
        detail::Execution fail(file); fail.frames.push_back({}); fail.frame().stream=26; fail.fuel=quota?100:2;
        fail.resources.bytes=quota?kMaxGenerationData-ResourceArena::listBytes(3):0;
        const auto before=fail.resources.bytes;
        const auto attempt = [&] { return detail::stepCollections(fail,{Opcode::ConstList,0,0}); };
        CHECK_THROWS_AS(attempt(),Trap);
        CHECK(fail.resources.bytes==before); CHECK(fail.resources.constants.empty());
        CHECK(fail.resources.descriptionConstants.empty()); CHECK(fail.frame().stream==26); CHECK(fail.frame().stack.empty());
    }
}

TEST_CASE("sampler pools reject malformed constants references aux and older formats", "[sampling-palette]") {
    const auto bytes=stripDebug(compilePalettes("asset t{size 1x1 const sampler<f32> s=sampler(0.0 to 1.0 with beta(2,3)) const list<sampler<f32>> specs=[s] draw{items=specs t=s}}",false));
    const auto file=loadFile(bytes);
    const auto pool=file.layout.find(SectionId::Samplers)->offset;
    const auto lists=file.layout.find(SectionId::Collections)->offset;
    for (const auto& [at,value] : std::vector<std::pair<std::size_t,u8>>{{pool+6,3},{pool+7,1},{pool+16,0}}) {
        auto bad=bytes; bad.at(at)=value; CHECK_THROWS_AS(loadFile(bad),FormatError);
    }
    for (const auto at : {pool+4,lists+8}) {auto bad=bytes;put16(bad,at,65535);CHECK_THROWS_AS(loadFile(bad),FormatError);}
    auto bad=bytes;put32(bad,pool,65537);CHECK_THROWS_AS(loadFile(bad),FormatError);
    bad=bytes;put32(bad,pool+8,0x7f800000);CHECK_THROWS_AS(loadFile(bad),FormatError);
    bad=bytes;put32(bad,pool+8,0x40000000);CHECK_THROWS_AS(loadFile(bad),FormatError);
    bad=bytes;put32(bad,pool+17,0);CHECK_THROWS_AS(loadFile(bad),FormatError); // beta alpha
    for (u16 minor=1;minor<6;++minor) {bad=bytes;put16(bad,6,minor);CHECK_THROWS_AS(loadFile(bad),FormatError);}
    for (const auto& function : file.code.functions) for (std::size_t i=0;i<function.code.size()/4;++i)
        if (decodeInstruction(function.code.subspan(i*4,4)).opcode==Opcode::ConstSampler) {
            const auto at=function.fileOffset+i*4;
            bad=bytes;bad.at(at+1)=1;CHECK_THROWS_AS(validate(loadFile(bad)),FormatError);
            bad=bytes;put16(bad,at+2,65535);CHECK_THROWS_AS(validate(loadFile(bad)),FormatError);
        }
    CHECK_THROWS_AS(assemble("meta: size=1x1\nfn draw ( ) -> ( ):\nreturn\nconst.sampler 0\nendfn\n"),std::exception);
    const auto mismatch=stripDebug(compilePalettes("asset t{size 1x1 const sampler<i32> intSpec=sampler(1) "
        "const sampler<f32> floatSpec=sampler(1.0) const list<sampler<i32>> specs=[intSpec] draw{items=specs s=floatSpec}}",false));
    const auto mismatchFile=loadFile(mismatch);
    REQUIRE(mismatchFile.collections.samplers.size()==2);
    bad=mismatch;put16(bad,mismatchFile.layout.find(SectionId::Collections)->offset+8,1);
    CHECK_THROWS_AS(loadFile(bad),FormatError);
}

TEST_CASE("sampling palette and constant list limits preserve large legacy palettes", "[sampling-palette]") {
    const auto repeated = [](int count,const std::string& value) {
        std::string result="["; for (int i=0;i<count;++i) {if(i)result+=",";result+=value;} return result+"]";
    };
    for (const auto& draw : draws) {
        const auto values=repeated(256,"sampler("+std::string(draw.expression)+")");
        CHECK_NOTHROW(execute(compilePalettes(asset("palette specs="+values,"before=seed s=specs[255] empty=s.pick(0) values={seed==before,list.len(empty)}"))));
    }
    rejectPalette("palette specs="+repeated(257,"1 to 2")+" draw{}","sampling palette");
    rejectPalette("const list<sampler<i32>> specs="+repeated(257,"sampler(1)")+" draw{}","exceeds 256");
    const auto large="palette values="+repeated(257,"1.0");
    const auto legacy=compilePalettes("asset t{size 1x1 "+large+" output point p draw{p={values[-1],ramp(values,0.5)}}}");
    CHECK(execute(legacy).outputs[0].points[0].x==1.f);
    rejectPalette(large+" draw{items=values}","exceeds 256 elements when used as a list");
}
