#include <catch2/catch_test_macros.hpp>
#include "support/geometry_fixture.hpp"
#include "support/test_support.hpp"
#include "Compiler.hpp"
#include "SourceCompilation.hpp"
#include "iprop/VM.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ips/Palette.hpp"

#include <bit>
#include <cmath>
#include <limits>

using namespace iprop;
using namespace test_support;
namespace {
constexpr ValueType handles[]{ValueType::Diagram2d,ValueType::Contours2d,ValueType::Regions2d,
                              ValueType::Part2d,ValueType::Parts2d};
std::vector<u8> compile(const std::string& source, bool optimize = false) {
    const auto p = ips::parse(source); ips::checkAsset(p,{}); return compileProgram(p,{},optimize);
}
std::string relay(const std::string& type) {
    return "asset t{size 1x1 echo(v:"+type+")->"+type+"{"+type+
        " copy=v if(true){copy=v} for i in 0..2{copy=v} copy=choose(seed,copy,copy) return copy} "
        "unset(v:"+type+")->"+type+"{if(false){"+type+" copy=v}return copy}"
        "unused(v:"+type+"){if(false){"+type+" copy=v}discarded=copy}draw{}}";
}
bool sameHandle(const Value& a,const Value& b){
    return std::visit([&](const auto& left){
        using T=std::decay_t<decltype(left)>;
        if constexpr(requires{left.data;}){const auto* right=std::get_if<T>(&b);return right && left.data==right->data;}
        else return false;
    },a);
}
template<class F> void failsWithoutLeak(detail::Execution& ex, F action) {
    const auto before = ex.resources.bytes;
    CHECK_THROWS_AS(action(),Trap);
    CHECK(ex.resources.bytes == before);
    CHECK((ex.frames.empty() || ex.frame().stack.empty()));
}
}

TEST_CASE("geometry handles append descriptor codes and reject every forbidden container", "[geometry-v09]") {
    for (std::size_t i = 0; i < std::size(handles); ++i) {
        const auto type = handles[i]; const std::string name = valueTypeName(type);
        CHECK(static_cast<u8>(type) == 15+i);
        CHECK(type.isGeometry()); CHECK(type.isResource()); CHECK(type.isStorable()); CHECK(type.needsDescriptor());
        CHECK_FALSE(type.isScalar()); CHECK_FALSE(ValueType::list(type).isStorable());
        CHECK_FALSE(ValueType::sampler(type).isStorable()); CHECK_FALSE(ValueType::stepper(type).isStorable());
        CHECK_FALSE(ValueType{type.kind,ValueType::Kind::I32}.isStorable());
        for (const bool opt : {false,true}) {
            const auto source = relay(name); const auto bytes = compile(source,opt); const auto file = loadFile(bytes); validate(file);
            REQUIRE(std::find(file.collections.types.begin(),file.collections.types.end(),type) != file.collections.types.end());
            CHECK(disassemble(assemble(disassemble(bytes))) == disassemble(bytes));
            CHECK(stripDebug(decompressFile(compressFile(bytes))) == stripDebug(bytes));
            const auto formatted = ips::formatProgram(ips::parse(source));
            CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
            validate(loadFile(compile(formatted,opt)));
        }
        for (const auto constructor : {"list","sampler","stepper"}) {
            CHECK_THROWS(compile("asset t{size 1x1 f(v:"+std::string(constructor)+"<"+name+">){}draw{}}"));
            CHECK_THROWS(compile("asset t{size 1x1 draw{"+std::string(constructor)+"<"+name+"> v=[1]}}"));
            CHECK_THROWS(assemble("meta: size=1x1\ntypes: "+std::string(constructor)+"<"+name+">\nfn draw ( ) -> ( ):\nreturn\nendfn\n"));
        }
        CHECK_THROWS(compile("asset t{size 1x1 input "+name+" v=0 draw{}}"));
        CHECK_THROWS(compile("asset t{size 1x1 const "+name+" v=0 draw{}}"));
        CHECK_THROWS(compile("asset t{size 1x1 f(v:"+name+"){list=[v]}draw{}}"));
        CHECK_THROWS(compile("asset t{size 1x1 f(v:"+name+"){sampler(v)}draw{}}"));
        CHECK_THROWS(compile("asset t{size 1x1 f(v:"+name+"){stepper(v)}draw{}}"));
        const auto palette=ips::parse("asset t{size 1x1 palette P=[1]draw{}}");
        const std::array<ValueType,1> elements{type};
        CHECK_THROWS_AS(ips::paletteType(palette.asset.palettes[0],elements),ips::SourceError);
        CHECK_THROWS(compile("asset t{size 1x1 f(v:"+name+")->"+name+"{return 1}draw{}}"));
        CHECK_THROWS(compile("asset t{size 1x1 f(v:"+name+"){v=1}draw{}}"));
    }
}

TEST_CASE("geometry descriptors validate unused code and malformed wire records", "[geometry-v09]") {
    for (const auto type : handles) {
        const std::string name = valueTypeName(type);
        const auto bytes = assemble("meta: size=1x1\ntypes: "+name+"\nfn draw ( ) -> ( ):\nreturn\nendfn\n"
            "fn dead ( "+name+" ) -> ( "+name+" ):\nlocal.get 0\nreturn\nendfn\n");
        const auto file = loadFile(bytes); validate(file);
        const auto at = file.layout.find(SectionId::Types)->offset+4;
        for (const auto [delta,value] : {std::pair{0,20}, {1,0}, {2,1}, {3,1}}) {
            auto bad=bytes; bad[at+static_cast<std::size_t>(delta)]=static_cast<u8>(value); CHECK_THROWS_AS(loadFile(bad),FormatError);
        }
        for (const u8 constructor : {u8{5},u8{10},u8{14}}) {
            auto bad=bytes;bad[at]=constructor;bad[at+1]=static_cast<u8>(type);CHECK_THROWS_AS(loadFile(bad),FormatError);
        }
        // Distinct handles never substitute for one another on helper returns.
        for (const auto other : handles) if (other != type) {
            CHECK_THROWS(assemble("meta: size=1x1\ntypes: "+name+" "+valueTypeName(other)+
                "\nfn draw ( ) -> ( ):\nreturn\nendfn\nfn bad ( "+name+" ) -> ( "+valueTypeName(other)+
                " ):\nlocal.get 0\nreturn\nendfn\n"));
        }
    }
}

TEST_CASE("geometry names remain ordinary locals helper names and import aliases", "[geometry-v09]") {
    for (const std::string name : {"diagram2d","contours2d","regions2d","part2d","parts2d","diagram","regions","part","parts"}) {
        const auto source="asset t{size 1x1 "+name+"(v:i32)->i32{return v}draw{"+name+"=1 x="+name+"("+name+")}}";
        CHECK_NOTHROW(compile(source));
        const std::map<std::string,std::string> files{{"root.ips","import "+name+" from \"child.ips\" asset t{size 1x1 draw{"+name+".draw(seed,at={0,0})}}"},
            {"child.ips","asset t{size 1x1 draw{pixel(0,0,#fff)}}"}};
        const auto result=compileSource("root.ips",[&](std::string_view path){return files.at(std::string(path));});
        INFO(name);
        INFO((result.diagnostics.empty()?"no diagnostics":result.diagnostics[0].message));
        REQUIRE(result.succeeded());
    }
}

TEST_CASE("shared geometry records cache holes provenance adjacency and f32 metadata", "[geometry-v09]") {
    LoadedFile file; detail::Execution ex(file); ex.fuel=10000000;
    {
        const auto data=geometry_fixture::adjacent(ex); const auto bytes=ex.resources.bytes;
        CHECK(data->points.size()==6); CHECK(data->edges.size()==7); CHECK(data->uses.size()==8);
        CHECK(data->uses[1].edge==data->uses[7].edge); CHECK(data->uses[1].reversed!=data->uses[7].reversed);
        CHECK(data->edges[1].provenance.edge==1); CHECK(data->edges[1].provenance.source==0); CHECK(data->edges[1].provenance.neighbor==1);
        CHECK(data->regions[0].area==4); CHECK(data->regions[0].center==Vec2{1,1});
        CHECK(data->regions[1].center==Vec2{3,1}); CHECK(data->regions[1].boundsOrigin==Vec2{2,0});
        CHECK(data->regions[1].boundsSize==Vec2{2,2});
        CHECK(data->regionNeighbors(0)[0]==1);CHECK(data->regionNeighbors(1)[0]==0);
        const Diagram2dValue d{data}; const Contours2dValue c{data}; const Regions2dValue r{data};
        CHECK(d.data==c.data); CHECK(c.data==r.data); CHECK(ex.resources.bytes==bytes);
        const auto fuel=ex.fuel;
        CHECK(r.data->regions[1].area==4); CHECK(r.data->regionNeighbors(0)[0]==1);
        CHECK(ex.fuel==fuel); CHECK(ex.resources.bytes==bytes);
    }
    CHECK(ex.resources.bytes==0);
    {
        const auto data=geometry_fixture::holes(ex);
        REQUIRE(data->rings.size()==3); CHECK_FALSE(data->rings[0].hole); CHECK(data->rings[1].hole); CHECK_FALSE(data->rings[2].hole);
        CHECK(data->rings[1].parent==0);CHECK(data->rings[2].parent==1);CHECK(data->regionHoles(0)[0]==1);
        CHECK(data->regions[0].sourceId==9); CHECK(data->regions[0].area==76.f);
        CHECK(data->regions[0].center==Vec2{static_cast<f32>(97.0/19.0),static_cast<f32>(77.0/19.0)});
        CHECK(data->regions[1].area==1.f);CHECK(data->regions[1].center==Vec2{3,3});CHECK(data->regionNeighbors(0).empty());
    }
    CHECK(ex.resources.bytes==0);
}

TEST_CASE("geometry locals helpers dynamic records and last references share one arena", "[geometry-v09]") {
    for (const bool optimize : {false,true}) for (const auto type : handles) {
        auto file=loadFile(compile(relay(valueTypeName(type)),optimize));
        detail::Execution ex(file); ex.fuel=100000;
        ex.frames.emplace_back(); ex.frame().locals={i32{0},i32{0}};ex.frame().stream=26;
        std::weak_ptr<const GeometryData> weak;
        {
            auto geometry=geometry_fixture::adjacent(ex); weak=geometry;
            Value value;
            if(type==ValueType::Diagram2d)value=Diagram2dValue{geometry};
            if(type==ValueType::Contours2d)value=Contours2dValue{geometry};
            if(type==ValueType::Regions2d)value=Regions2dValue{geometry};
            if(type==ValueType::Part2d || type==ValueType::Parts2d) {
                auto part=detail::makeResource<DynamicPartData,256>(ex.resources);part->source=RegionSource{geometry,0};part->id=17;part->sourceId=0;
                if(type==ValueType::Part2d)value=Part2dValue{part};
                else {auto parts=detail::makeResource<DynamicPartsData,128>(ex.resources,ex.resources);parts->parts.push_back({part});value=Parts2dValue{parts};}
            }
            const auto bytes=ex.resources.bytes; CHECK(valueType(value)==type);
            ex.push(value);ex.callFunction(1);CHECK(valueType(ex.frame().locals[0])==type);
            ex.push(ex.frame().locals[0]);
            // The optimizer may remove the copy local entirely. Its actual
            // bytecode is executed below; raw local writes use the full table.
            if(!optimize){
                CHECK(detail::stepStack(ex,{Opcode::LocalSet,0,1}));
                CHECK(detail::stepStack(ex,{Opcode::LocalGet,0,1}));
            }
            CHECK(detail::stepStack(ex,{Opcode::Dup,0,0}));
            CHECK(detail::stepStack(ex,{Opcode::Drop,0,0}));
            ex.leaveFrame();CHECK(valueType(ex.peek())==type);CHECK(ex.frame().stream==26);CHECK(ex.resources.bytes==bytes);
            CHECK(sameHandle(ex.peek(),value));
            // Execute the compiled control-flow body as well as the individual
            // stack operations; both optimized and ordinary IPS code use it.
            ex.callFunction(1);
            while(ex.frames.size()>1){
                const auto& fn=ex.fn();
                if(ex.frame().ip==fn.code.size()/4){ex.leaveFrame();continue;}
                const auto ins=decodeInstruction(fn.code.subspan(ex.frame().ip*4,4));
                REQUIRE(detail::dispatch(ex,ins));
            }
            CHECK(valueType(ex.peek())==type);CHECK(ex.resources.bytes==bytes);CHECK(ex.frame().stream==26);
            CHECK(sameHandle(ex.peek(),value));
            for(const u16 function:{u16{2},u16{3}}){
                ex.push(value);ex.callFunction(function);
                try{
                    while(ex.frames.size()>1){
                        const auto& fn=ex.fn();
                        if(ex.frame().ip==fn.code.size()/4){ex.leaveFrame();continue;}
                        const bool handled=detail::dispatch(ex,decodeInstruction(fn.code.subspan(ex.frame().ip*4,4)));
                        REQUIRE(handled);
                    }
                    FAIL("unexecuted geometry initializer did not trap");
                }catch(const Trap& error){CHECK(std::string(error.what())==std::string(valueTypeName(type))+" is unassigned");}
                ex.frames.resize(1);CHECK(ex.resources.bytes==bytes);
            }
            geometry.reset();CHECK_FALSE(weak.expired());
        }
        CHECK_FALSE(weak.expired());ex.frame().stack.clear();CHECK(weak.expired());CHECK(ex.resources.bytes==0);
    }
}

TEST_CASE("unset geometry reads trap and failed VM exposes no result", "[geometry-v09]") {
    for (const auto type : handles) {
        const auto name=std::string(valueTypeName(type));
        const auto bytes=assemble("meta: size=1x1\ntypes: "+name+"\nfn draw ( ) -> ( ):\nlocals: "+name+"\nlocal.get 0\ndrop\nreturn\nendfn\n");
        const auto file=loadFile(bytes);validate(file);VM vm(file,{});
        try{vm.run();FAIL("unset geometry accepted");}catch(const Trap& error){CHECK(std::string(error.what())==name+" is unassigned");CHECK(error.reason==ErrorReason::RuntimeViolation);}
        CHECK_THROWS_AS(vm.result(),std::logic_error);CHECK_THROWS_AS(vm.takeResult(),std::logic_error);CHECK(vm.debugStack().empty());
        CHECK_THROWS(compile("asset t{size 1x1 draw{x=unknown}}"));
    }
}

TEST_CASE("geometry quotas fuel scratch rollback and retained ownership are bounded", "[geometry-v09]") {
    LoadedFile file; detail::Execution ex(file); ex.fuel=10000000;
    failsWithoutLeak(ex,[&]{ex.fuel=0;geometry_fixture::adjacent(ex);});CHECK_FALSE(ex.resources.surfaceLifetime);ex.fuel=10000000;
    for (const u64 available : {0u,511u,512u,700u,1000u}) {
        ex.resources.bytes=kMaxGenerationData-available;
        failsWithoutLeak(ex,[&]{geometry_fixture::adjacent(ex);});
    }
    ex.resources.bytes=0;
    u64 cost=0, retained=0;
    {const auto before=ex.fuel;auto data=geometry_fixture::adjacent(ex);cost=before-ex.fuel;retained=ex.resources.bytes;}
    ex.fuel=cost;CHECK_NOTHROW(geometry_fixture::adjacent(ex));CHECK(ex.fuel==0);CHECK(ex.resources.bytes==0);
    ex.fuel=cost-1;failsWithoutLeak(ex,[&]{geometry_fixture::adjacent(ex);});ex.fuel=10000000;
    // Find the exact memory boundary, which includes transient reallocations
    // and validation scratch rather than just the retained result.
    u64 minimum=0;
    for(u64 available=retained;available<retained+4096;++available){
        ex.resources.bytes=kMaxGenerationData-available;
        try{auto data=geometry_fixture::adjacent(ex);minimum=available;break;}catch(const Trap& error){CHECK(error.reason==ErrorReason::OutOfMemory);CHECK(ex.resources.bytes==kMaxGenerationData-available);}
    }
    REQUIRE(minimum>retained);CHECK(ex.resources.bytes==kMaxGenerationData-minimum);
    ex.resources.bytes=kMaxGenerationData-(minimum-1);failsWithoutLeak(ex,[&]{geometry_fixture::adjacent(ex);});ex.resources.bytes=0;
    // Capacity, growth peak and integer-overflow defense of the common allocator.
    {
        detail::ResourceVector<Vec2,8> scratch{detail::ResourceAllocator<Vec2,8>(ex.resources)};
        const std::array<std::size_t,5> capacities{1,2,4,4,8};
        for(const auto capacity:capacities){scratch.push_back({1,2});CHECK(scratch.capacity()==capacity);}
        scratch=detail::ResourceVector<Vec2,8>{detail::ResourceAllocator<Vec2,8>(ex.resources)};CHECK(ex.resources.bytes==0);
        scratch.push_back({7,9});scratch.push_back(scratch.front());CHECK(scratch[1]==Vec2{7,9});
        scratch.reserve(100);CHECK(ex.resources.bytes==32+8*scratch.capacity());
        scratch.resize(1);CHECK(ex.resources.bytes==32+8*scratch.capacity());
        scratch.clear();CHECK(ex.resources.bytes==832);
        CHECK_THROWS_AS(scratch.get_allocator().allocate(std::numeric_limits<std::size_t>::max()),Trap);
        CHECK(ex.resources.bytes==832);
    }
    CHECK(ex.resources.bytes==0);
    std::shared_ptr<const GeometryData> retainedData;
    {detail::Execution inner(file);inner.fuel=100000;retainedData=geometry_fixture::holes(inner);inner.resources.clear();CHECK(inner.resources.bytes==0);}
    CHECK(retainedData->regions[0].area==76);retainedData.reset();CHECK(ex.resources.bytes==0);
}

TEST_CASE("geometry rejects invalid topology limits and data before publishing", "[geometry-v09]") {
    LoadedFile file;detail::Execution ex(file);ex.fuel=100000000;
    for(const auto p:{Vec2{NAN,0},Vec2{0,INFINITY}}) failsWithoutLeak(ex,[&]{detail::GeometryBuilder b(ex,GeometryOrigin::Contours);b.point(p);});
    failsWithoutLeak(ex,[&]{detail::GeometryBuilder b(ex,GeometryOrigin::Offset);});
    failsWithoutLeak(ex,[&]{detail::GeometryBuilder b(ex,static_cast<GeometryOrigin>(3));});
    failsWithoutLeak(ex,[&]{detail::GeometryBuilder b(ex,GeometryOrigin::Contours);for(u32 i=0;i<=256;++i)b.site({static_cast<f32>(i),0});});
    failsWithoutLeak(ex,[&]{detail::GeometryBuilder b(ex,GeometryOrigin::Contours);for(u32 i=0;i<=65536;++i)b.point({static_cast<f32>(i),0});});
    failsWithoutLeak(ex,[&]{detail::GeometryBuilder b(ex,GeometryOrigin::Contours);const std::vector<GeometryEdgeUse> edges(2049);b.ring(edges);});
    for(const int fault:{0,1,2,3,4,5,6})failsWithoutLeak(ex,[&]{
        detail::GeometryBuilder b(ex,GeometryOrigin::Contours);
        const auto outer=geometry_fixture::rectangle(b,{0,0},{10,8});
        if(fault==0)b.edge(0,1); // duplicate/unused canonical edge
        if(fault==1)b.edge(1,0);
        if(fault==2){std::array<GeometryEdgeUse,3> bad{{{0,false},{1,false},{3,true}}};b.ring(bad);}
        if(fault==3){b.region(outer,0);b.region(outer,1);}
        if(fault==4){const std::array<i32,1> neighbors{0};b.region(outer,0,{},neighbors);}
        if(fault==5){const auto hole=geometry_fixture::rectangle(b,{2,2},{4,4},outer,true);(void)hole;b.region(outer,0);}
        if(fault==6){const auto bad=geometry_fixture::rectangle(b,{0,0},{2,2},kNoGeometryIndex,true);b.region(bad,0);}
        (void)std::move(b).finish();
    });
}

TEST_CASE("geometry admits exact site region ring and aggregate vertex limits", "[geometry-v09]") {
    LoadedFile file;detail::Execution ex(file);ex.fuel=100000000;
    for(const auto origin:{GeometryOrigin::Diagram,GeometryOrigin::Contours}){
        detail::GeometryBuilder b(ex,origin);const auto empty=std::move(b).finish();
        CHECK(empty->points.empty());CHECK(empty->regions.empty());CHECK(empty->rings.empty());CHECK(ex.resources.bytes==512);
        CHECK_THROWS_AS(std::move(b).finish(),std::logic_error);
    }
    CHECK(ex.resources.bytes==0);
    {
        detail::GeometryBuilder b(ex,GeometryOrigin::Diagram);
        for(u32 i=0;i<256;++i){
            const auto x=static_cast<f32>(i*2);b.site({x+0.5f,0.5f});
            const auto ring=geometry_fixture::rectangle(b,{x,0},{x+1,1});b.region(ring,static_cast<i32>(i));
        }
        const auto data=std::move(b).finish();CHECK(data->sites.size()==256);CHECK(data->regions.size()==256);
        CHECK(data->regions.back().sourceId==255);CHECK(data->regions.back().area==1);
    }
    CHECK(ex.resources.bytes==0);
    failsWithoutLeak(ex,[&]{detail::GeometryBuilder b(ex,GeometryOrigin::Contours);
        for(u32 i=0;i<257;++i){const auto ring=geometry_fixture::rectangle(b,{static_cast<f32>(i*2),0},{static_cast<f32>(i*2+1),1});b.region(ring,0);}});
    {
        detail::GeometryBuilder b(ex,GeometryOrigin::Contours);
        // 32 disjoint rectangles, each with 512 vertices on each side.
        // Every coordinate is exactly representable; no trig or generator.
        for(u32 shape=0;shape<32;++shape){
            const auto x=static_cast<f32>(shape*1024);const u32 first=shape*2048;
            for(u32 i=0;i<512;++i)b.point({x+static_cast<f32>(i),0});
            for(u32 i=0;i<512;++i)b.point({x+512,static_cast<f32>(i)});
            for(u32 i=0;i<512;++i)b.point({x+512-static_cast<f32>(i),512});
            for(u32 i=0;i<512;++i)b.point({x,512-static_cast<f32>(i)});
            std::array<GeometryEdgeUse,2048> uses;
            for(u32 i=0;i<2048;++i){
                const u32 a=first+i,c=first+(i+1)%2048;
                uses[i]={b.edge(std::min(a,c),std::max(a,c)),a>c};
            }
            b.region(b.ring(uses),static_cast<i32>(shape));
        }
        const std::array<GeometryEdgeUse,3> excess{{{0,false},{1,false},{2,false}}};
        CHECK_THROWS_AS(b.ring(excess),Trap);
        const auto data=std::move(b).finish();CHECK(data->points.size()==65536);CHECK(data->uses.size()==65536);
        CHECK(data->rings.size()==32);CHECK(data->rings[0].count==2048);CHECK(data->regions[31].area==262144.f);
    }
    CHECK(ex.resources.bytes==0);
}

TEST_CASE("geometry dependencies share raster quota and unwind overwritten and branched handles", "[geometry-v09]") {
    LoadedFile file;file.code.functions.emplace_back();detail::Execution ex(file);ex.fuel=10000000;ex.frames.emplace_back();
    std::weak_ptr<const GeometryData> weak;
    {
        auto root=geometry_fixture::holes(ex);const auto rootBytes=ex.resources.bytes;
        auto current=root;
        for(int i=0;i<1000;++i){
            detail::GeometryBuilder b(ex,GeometryOrigin::Offset,current);
            b.region(geometry_fixture::rectangle(b,{0,0},{1,1}),9);
            current=std::move(b).finish();CHECK(current->source==root);
        }
        CHECK(ex.resources.bytes==rootBytes+current->points.capacity()*8+current->edges.capacity()*32+
            current->uses.capacity()*8+current->rings.capacity()*48+current->regions.capacity()*64+512+5*32);
        // Reserve the existing raster tariff, then retain it in a part.
        ex.allocateResource(maskBytes(2,2));auto pixels=ex.resources.makeSurface(maskBytes(2,2));
        pixels->format=SurfaceBuffer::Format::A8;pixels->raster.width=2;pixels->raster.height=2;pixels->coverage={255,0,128,255};
        const auto both=ex.resources.bytes;
        auto part=detail::makeResource<DynamicPartData,256>(ex.resources);part->source=MaskValue{pixels,{}};
        const Part2dValue p{part};pixels.reset();CHECK(ex.resources.bytes==both+256);CHECK(std::get<MaskValue>(p.data->source).buffer->coverage[2]==128);
        weak=current;ex.push(Regions2dValue{current});current.reset();root.reset();
    }
    CHECK_FALSE(weak.expired());
    ex.frame().labels.push_back({0,0,0,0});CHECK(detail::stepControl(ex,{Opcode::Br,0,0}));
    CHECK(weak.expired());CHECK(ex.resources.bytes==0);
    {
        auto first=geometry_fixture::adjacent(ex);weak=first;ex.frame().locals={Regions2dValue{first}};first.reset();
        auto second=geometry_fixture::holes(ex);ex.push(Regions2dValue{second});CHECK(detail::stepStack(ex,{Opcode::LocalSet,0,0}));
        CHECK(weak.expired());ex.frame().locals.clear();second.reset();
    }
    CHECK(ex.resources.bytes==0);
    auto allocator=detail::ResourceAllocator<Vec2,8>(ex.resources);ex.resources.clear();
    CHECK_THROWS_AS(allocator.allocate(1),Trap);CHECK(ex.resources.bytes==0);
}

TEST_CASE("actual geometry workspace allocation fills the shared 64 MiB quota", "[geometry-v09]") {
    LoadedFile file;detail::Execution ex(file);ex.fuel=1000000;
    {
        detail::ResourceVector<Vec2,8> workspace{detail::ResourceAllocator<Vec2,8>(ex.resources)};
        workspace.reserve((kMaxGenerationData-32)/8);
        CHECK(ex.resources.bytes==kMaxGenerationData);
        failsWithoutLeak(ex,[&]{geometry_fixture::adjacent(ex);});
    }
    CHECK(ex.resources.bytes==0);
    CHECK_NOTHROW(geometry_fixture::adjacent(ex));CHECK(ex.resources.bytes==0);
}
