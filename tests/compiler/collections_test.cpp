#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "iprop/Compression.hpp"
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
std::vector<u8> collections(const std::string& source, bool optimize = true) {
    const auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}
void same(const std::vector<u8>& a, const std::vector<u8>& b, u32 seed) {
    const auto actual = execute(a, seed), expected = execute(b, seed);
    REQUIRE(actual.canvas.pixels == expected.canvas.pixels);
    REQUIRE(actual.outputs.size() == expected.outputs.size());
    for (std::size_t i = 0; i < actual.outputs.size(); ++i) {
        REQUIRE(actual.outputs[i].points.size() == expected.outputs[i].points.size());
        for (std::size_t n = 0; n < actual.outputs[i].points.size(); ++n) {
            CHECK(std::bit_cast<u32>(actual.outputs[i].points[n].x) == std::bit_cast<u32>(expected.outputs[i].points[n].x));
            CHECK(std::bit_cast<u32>(actual.outputs[i].points[n].y) == std::bit_cast<u32>(expected.outputs[i].points[n].y));
        }
    }
}
std::string repeated(unsigned n, const std::string& value) {
    std::string out = "[";
    for (unsigned i = 0; i < n; ++i) { if (i) out += ','; out += value; }
    return out + ']';
}
void reject(const std::string& source, const std::string& message) {
    try { (void)collections(source); FAIL("invalid collection accepted"); }
    catch (const ips::SourceError& error) {
        INFO(source << " -> " << error.what());
        CHECK(std::string(error.what()).find(message) != std::string::npos);
        CHECK(error.location.line >= 1);
        CHECK(error.location.column >= 1);
    }
}
void raw16(std::vector<u8>& bytes, std::size_t at, u16 value) {
    bytes.at(at) = static_cast<u8>(value); bytes.at(at + 1) = static_cast<u8>(value >> 8);
}
void raw32(std::vector<u8>& bytes, std::size_t at, u32 value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<u8>(value >> (8 * i));
}
}

TEST_CASE("lists share all current element types through helpers and returns") {
    const std::string source = R"(asset t {
      size 2x2 output point checks output point numbers output point shape output point state
      const list<i32> integers = [later, 16777218]
      const i32 later = 16777217
      const list<f32> fractions = [1, 0.5, -0.0]
      const list<color> colors = [#929ca8, #343a42]
      const list<vec2> vectors = [{3,4}, vec2.normalize({3,4})]
      const list<distribution> shapes = [gaussian(0.5,0.18), triangular(0,0.5,1), beta(2,2)]
      const i32 length = list.len(integers)
      const color steel = colors[0]
      const vec2 direction = vectors[1]
      const distribution typical = shapes[0]
      ints(items:list<i32>)->list<i32> { return items }
      floats(items:list<f32>)->list<f32> { return items }
      tints(items:list<color>)->list<color> { return items }
      points(items:list<vec2>)->list<vec2> { return items }
      distributions(items:list<distribution>)->list<distribution> { return items }
      draw {
        shared=ints(integers) shared=[] shared=[4,5]
        checks={ints(integers)[0]==16777217,length==2 && list.len(shared)==2}
        numbers={floats(fractions)[1],points(vectors)[1].x}
        shape={0.0 to 1.0 with distributions(shapes)[0],0 to 1 with typical}
        pixel(0,0,tints(colors)[0])
        state={seed&65535,seed>>16}
      }
    })";
    const auto reference = collections(R"(asset t {
      size 2x2 output point checks output point numbers output point shape output point state
      draw { checks={1,1} numbers={0.5,0.6} shape={0.0 to 1.0 with gaussian(0.5,0.18),0 to 1 with gaussian(0.5,0.18)}
        pixel(0,0,#929ca8) state={seed&65535,seed>>16} }
    })", false);
    for (const bool optimize : {false, true}) {
        const auto bytes = collections(source, optimize);
        const auto file = loadFile(bytes);
        CHECK(file.layout.header.versionMinor == 6);
        CHECK(file.collections.types.size() == 5);
        for (const auto seed : {0u, 1u, 26u, 0xffffffffu}) same(bytes, reference, seed);
        same(assemble(disassemble(bytes)), bytes, 26);
        same(compressFile(bytes), bytes, 26);
    }
}

TEST_CASE("empty lists require an element context and locals retain their list type") {
    const auto bytes = collections(R"(asset t { size 1x1 output point p
      const list<color> noColors=[]
      empty()->list<vec2> { return [] }
      count(items:list<distribution>)->i32 { return list.len(items) }
      draw { list<f32> numbers=[] numbers=[1,2] numbers=[]
        p={list.len(empty()),count([])+list.len(noColors)+list.len(numbers)} }
    })");
    const auto result = execute(bytes);
    CHECK(result.outputs[0].points[0].x == 0.f);
    CHECK(result.outputs[0].points[0].y == 0.f);
    reject("asset t {size 1x1 draw{items=[]}}", "empty list needs");
    reject("asset t {size 1x1 draw{list.len([])}}", "empty list needs");
    reject("asset t {size 1x1 draw{items=[1] items=[1.0]}}", "list element needs i32");
    reject("asset t {size 1x1 draw{items=[1] items=[#112233]}}", "list element needs i32");
    reject("asset t {size 1x1 draw{items=[1,#112233]}}", "list element needs i32");
    reject("asset t {size 1x1 draw{items=[[1]]}}", "list elements must be");
    reject("asset t {size 1x1 f(items:list<list<i32>>){} draw{}}", "nested lists");
    CHECK_NOTHROW(collections("asset t {size 1x1 f(items:list<sampler<i32>>){} draw{}}"));
    reject("asset t {size 1x1 draw{items=[1] items[0]=2}}", "list elements are immutable");
    reject("asset t {size 1x1 draw{items=[1] x=items[0.5]}}", "list index must be i32");
}

TEST_CASE("list construction evaluates elements once in order and supports 256 elements") {
    const std::string source = R"(asset t {size 1x1 output point p output point state
      echo(items:list<i32>)->list<i32>{return items}
      draw{items=echo([10 to 15,20 to 30]) p={items[0],items[1]} state={seed&65535,seed>>16}}
    })";
    const auto reference = collections("asset t {size 1x1 output point p output point state draw{first=10 to 15 second=20 to 30 p={first,second} state={seed&65535,seed>>16}}}", false);
    for (const bool optimize : {false,true}) {
        const auto bytes = collections(source,optimize);
        for (const auto seed : {0u,1u,26u,0xffffffffu}) same(bytes,reference,seed);
        for (const std::string value : {"seed", "{float(seed&255),1}", "gaussian(0.5,0.18)"}) {
            const auto large = collections("asset t {size 1x1 output point p draw{items="+repeated(256,value)+" p={list.len(items),list.len(items)}}}",optimize);
            CHECK(execute(large).outputs[0].points[0].x == 256.f);
        }
    }
    reject("asset t {size 1x1 draw{items="+repeated(257,"1")+"}}", "exceeds 256");
    reject("asset t {size 1x1 const list<i32> items="+repeated(257,"1")+" draw{}}", "exceeds 256");
}

TEST_CASE("list polygons preserve legacy rasterization and argument order") {
    const auto reference = collections(R"(asset t {size 32x24 output point state draw{
      poly(#915f34af,3,4,18,2,25,15,8,20) state={seed&65535,seed>>16}
    }})",false);
    const std::string source = R"(asset t {size 32x24 output point state
      const list<vec2> outline=[{3,4},{18,2},{25,15},{8,20}]
      paintShape(points:list<vec2>,tint:color){poly(color=tint,points=points)}
      draw{paintShape(outline,#915f34af) state={seed&65535,seed>>16}}}
    )";
    for (const bool optimize : {false,true}) {
        const auto bytes=collections(source,optimize);
        for (const auto seed : {0u,26u,0xffffffffu}) same(bytes,reference,seed);
        const auto ordered = collections(R"(asset t {size 4x4 output point state
          pointValue()->vec2{return {0.0 to 3.0,0.0 to 3.0}}
          tintValue()->color{return #000000 to #ffffff}
          draw{poly(color=tintValue(),points=[pointValue(),pointValue(),pointValue()]) state={seed&65535,seed>>16}}})",optimize);
        const auto sequential=collections(R"(asset t {size 4x4 output point state
          pointValue()->vec2{return {0.0 to 3.0,0.0 to 3.0}}
          tintValue()->color{return #000000 to #ffffff}
          draw{first=pointValue() second=pointValue() third=pointValue() tint=tintValue()
            poly(tint,first.x,first.y,second.x,second.y,third.x,third.y) state={seed&65535,seed>>16}}})",false);
        for (const auto seed : {0u,26u,0xffffffffu}) same(ordered,sequential,seed);
    }
    const auto tooFew=loadFile(collections("asset t {size 1x1 draw{poly([{0,0},{1,1}],#ffffff)}}"));
    VM vm(tooFew,{});
    REQUIRE_THROWS_AS(vm.run(),Trap);
    CHECK_THROWS_AS(vm.takeResult(),std::logic_error);
    CHECK_THROWS_AS(vm.result(),std::logic_error);
    CHECK_THROWS_AS(vm.run(),std::logic_error);
}

TEST_CASE("legacy palettes retain fallback and large-palette use without first-class limits") {
    for (const bool optimize : {false,true}) {
        const auto bytes=collections(R"(asset t {size 1x1 palette tones=[#000000,#ffffff]
          output point p
          colorAt(items:list<color>)->color{return items[1]}
          draw{items=tones p={tones[-1].r,tones[999].r} pixel(0,0,colorAt(items))}
        })",optimize);
        const auto result=execute(bytes);
        CHECK(result.outputs[0].points[0].x==255.f);
        CHECK(result.outputs[0].points[0].y==255.f);
        CHECK(result.canvas.at(0,0)==Color{255,255,255,255});
    }
    const auto prefix="asset t {size 1x1 palette big="+repeated(257,"1.0")+" output point p ";
    const auto legacy=collections(prefix+"draw{p={big[-1],ramp(big,0.5)}}}");
    CHECK(loadFile(legacy).layout.header.versionMinor==4);
    CHECK(execute(legacy).outputs[0].points[0].x==1.f);
    reject(prefix+"draw{items=big}}", "exceeds 256 elements when used as a list");
}

TEST_CASE("list indexing traps and const list dependencies remain compile-time checked") {
    for (const auto index : {-1,1,2147483647}) {
        const auto file=loadFile(collections("asset t {size 1x1 draw{items=[1] x=items["+std::to_string(index)+"]}}"));
        VM vm(file,{});
        try {vm.run(); FAIL("invalid index accepted");}
        catch(const Trap& error){CHECK(error.reason==ErrorReason::IndexRange); CHECK(std::string(error.what())=="list index out of range");}
    }
    const auto unassigned=loadFile(collections("asset t {size 1x1 draw{if(false){items=[1]} n=list.len(items)}}"));
    VM vm(unassigned,{});
    CHECK_THROWS_AS(vm.run(),Trap);
    reject("asset t {size 1x1 const list<i32> items=[seed] draw{}}", "only asset constants are allowed");
    reject("asset t {size 1x1 const list<i32> items=[1 to 2] draw{}}", "random ranges are not allowed");
    reject("asset t {size 1x1 const list<i32> first=[second[0]] const list<i32> second=[first[0]] draw{}}", "cyclic const dependency");
    reject("asset t {size 1x1 const list<i32> items=[1] const i32 invalid=items[1] draw{}}", "list index out of range");
    reject("asset t {size 1x1 const list<distribution> items=[gaussian(0,0)] draw{}}", "gaussian");
    const auto folded=collections("asset t {size 1x1 const list<i32> items=[1,2] const i32 n=list.len(items) draw{pixel(n,0,#ffffff)}}");
    CHECK(loadFile(folded).layout.header.versionMinor==4);
}

TEST_CASE("collection serialization preserves raw values and rejects malformed descriptors and pool data") {
    const auto bytes=collections(R"(asset t {size 1x1 output point p
      const list<f32> values=[-0.0,1.1754943508222875e-38*0.5,1.0/0.0,0.0/0.0]
      const list<distribution> shapes=[beta(2,2)]
      draw{p={values[0],values[1]} d=shapes[0]}
    })",false);
    const auto file=loadFile(bytes);
    REQUIRE(file.collections.lists.size()==2);
    CHECK(std::bit_cast<u32>(std::get<f32>(file.collections.lists[0].data->elements[0]))==0x80000000);
    CHECK(std::bit_cast<u32>(std::get<f32>(file.collections.lists[0].data->elements[1]))==0x00400000);
    const auto roundtrip=loadFile(assemble(disassemble(bytes)));
    Writer a,b;
    for(const auto& list:file.collections.lists)writeListElements(a,*list.data);
    for(const auto& list:roundtrip.collections.lists)writeListElements(b,*list.data);
    CHECK(a.data()==b.data());
    const auto typeSection=file.layout.find(SectionId::Types)->offset;
    const auto listSection=file.layout.find(SectionId::Collections)->offset;
    for(const auto [at,value]:std::vector<std::pair<std::size_t,u8>>{
        {typeSection+4,255},{typeSection+5,5},{typeSection+6,1}}){
        auto bad=bytes;bad.at(at)=value;CHECK_THROWS_AS(loadFile(bad),FormatError);
    }
    auto bad=bytes;raw16(bad,listSection+4,65535);CHECK_THROWS_AS(loadFile(bad),FormatError);
    bad=bytes;raw16(bad,listSection+6,257);CHECK_THROWS_AS(loadFile(bad),FormatError);
    bad=bytes;raw32(bad,typeSection,65537);CHECK_THROWS_AS(loadFile(bad),FormatError);
    bad=bytes;raw16(bad,6,5);CHECK_THROWS_AS(loadFile(bad),FormatError);
    CHECK_THROWS_AS(assemble("meta: size=1x1\ntypes: list<i32>\nfn draw ( i32, i32 ) -> ( ):\n return\n list.get 65535\nendfn\n"),std::exception);
    CHECK_THROWS_AS(assemble("meta: size=1x1\ntypes: list<i32>\nfn draw ( i32, i32 ) -> ( ):\n const.i32 1\n list.begin 0\n dup\n drop\n list.end 0\n drop\n return\nendfn\n"),std::exception);
}

TEST_CASE("list builders share frozen storage and charge memory and fuel before allocation") {
    LoadedFile file;
    file.layout.header.versionMinor=6;
    file.code.functions.push_back({});
    file.collections.types.push_back(ValueType::list(ValueType::I32));
    detail::Execution ex(file);
    ex.frames.push_back({});ex.fuel=100;
    ex.push(i32{2});REQUIRE(detail::stepCollections(ex,{Opcode::ListBegin,0,0}));
    const auto allocated=ex.resources.bytes;
    const auto mutableData=std::get<ListBuilder>(ex.peek()).data;
    ex.push(i32{10});REQUIRE(detail::stepCollections(ex,{Opcode::ListAppend,0,0}));
    ex.push(i32{20});REQUIRE(detail::stepCollections(ex,{Opcode::ListAppend,0,0}));
    REQUIRE(detail::stepCollections(ex,{Opcode::ListEnd,0,0}));
    const auto frozen=std::get<ListValue>(ex.peek());
    CHECK(frozen.data.get()==&mutableData->list);
    const auto copy=frozen;
    CHECK(copy.data.get()==frozen.data.get());CHECK(ex.resources.bytes==allocated);
    CHECK(ex.fuel==98);
    Function helper;
    helper.params={ValueType::list(ValueType::I32)};
    helper.results=helper.params;
    file.code.functions.push_back(helper);
    ex.frame().stack.clear();ex.push(frozen);
    ex.callFunction(1);
    CHECK(std::get<ListValue>(ex.frame().locals[0]).data.get()==frozen.data.get());
    ex.push(ex.frame().locals[0]);ex.leaveFrame();
    CHECK(std::get<ListValue>(ex.peek()).data.get()==frozen.data.get());
    CHECK(ex.resources.bytes==allocated);
    ex.push(i32{0});REQUIRE(detail::stepCollections(ex,{Opcode::ListBegin,0,0}));
    REQUIRE(detail::stepCollections(ex,{Opcode::ListEnd,0,0}));
    CHECK(ex.resources.bytes==allocated+ResourceArena::listBytes(0));
    detail::Execution limited(file);limited.frames.push_back({});limited.fuel=1;limited.push(i32{2});
    CHECK_THROWS_AS(detail::stepCollections(limited,{Opcode::ListBegin,0,0}),Trap);
    CHECK(limited.resources.bytes==0);
    limited.fuel=100;limited.resources.bytes=kMaxGenerationData;limited.push(i32{2});
    CHECK_THROWS_AS(detail::stepCollections(limited,{Opcode::ListBegin,0,0}),Trap);
    CHECK(limited.resources.lists.empty());
}

TEST_CASE("list polygon raster fuel and part canvas quota use the shared resource contract") {
    LoadedFile file;file.layout.header.versionMinor=6;file.code.functions.push_back({});
    const std::vector<Vec2> points{{3,4},{18,2},{25,15},{8,20}};
    auto data=std::make_shared<ListData>();data->type=ValueType::list(ValueType::Vec2);
    for(const auto point:points)data->elements.emplace_back(point);
    for(const auto scale:std::vector<Vec2>{{1,1},{0.5f,2.f}}) {
        detail::Execution legacy(file), list(file);
        for(auto* ex:{&legacy,&list}) {
            ex->frames.push_back({});ex->canvas.resize(32,24);
            ex->canvas.scaleX=scale.x;ex->canvas.scaleY=scale.y;ex->fuel=100000;
        }
        for(const auto point:points){legacy.push(point.x);legacy.push(point.y);}
        legacy.push(Color{146,156,168,255});
        list.push(ListValue{data->type,data});list.push(Color{146,156,168,255});
        REQUIRE(detail::stepCanvas(legacy,{Opcode::Poly,0,4}));
        REQUIRE(detail::stepCanvas(list,{Opcode::PolyList,0,0}));
        CHECK(legacy.canvas.pixels==list.canvas.pixels);CHECK(legacy.fuel==list.fuel);
    }
    Function part;part.params={ValueType::I32,ValueType::I32};file.code.functions.push_back(part);
    Part declaration;declaration.functionIndex=1;declaration.sizeX=1024;declaration.sizeY=1024;
    file.code.parts.push_back(declaration);
    detail::Execution ex(file);ex.frames.push_back({});ex.canvas.resize(1,1);ex.fuel=10000000;
    ex.resources.bytes=kMaxGenerationData-1024u*1024u*4u+1u;
    for(const auto value:{0,0,0})ex.push(i32{value});ex.push(0.f);ex.push(0.f);
    try{ex.callPart(0,0);FAIL("part canvas bypassed the arena quota");}
    catch(const Trap& error){CHECK(error.reason==ErrorReason::OutOfMemory);}
    CHECK(ex.partCalls.empty());CHECK(ex.frames.size()==1);CHECK(ex.canvas.width==1);
}

TEST_CASE("list formatting preserves comments, declared types and canonical call order") {
    const std::string source=R"(asset t {size 8x8
      const list<vec2> outline=[{1,1}, /* corner */ {6,1},{1,6},]
      empty()->list<vec2>{return []}
      draw{list<color> colors=[] poly(color=#ffffff,points=outline) points=empty()}
    })";
    const auto program=ips::parse(source);
    const auto printed=ips::formatProgram(program);
    CHECK(printed.find("/* corner */")!=std::string::npos);
    CHECK(printed.find("list<color> colors = []")!=std::string::npos);
    CHECK(printed==ips::formatProgram(ips::parse(printed)));
    same(collections(source),collections(printed),26);
}
