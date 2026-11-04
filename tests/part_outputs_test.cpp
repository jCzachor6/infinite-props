#include <catch2/catch_test_macros.hpp>
#include "SourceCompilation.hpp"
#include "iprop/DynamicParts.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "support/geometry_fixture.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include <map>
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>

using namespace iprop;
namespace {
using Sources = std::map<std::string, std::string>;
SourceCompilationResult compilation(const Sources& sources, bool optimize) {
    SourceCompilationOptions options; options.optimize=optimize; options.compress=false;
    return compileSource("root.ips", [&](std::string_view path) { return sources.at(std::string(path)); }, options);
}
std::vector<u8> compile(const std::string& source, bool optimize) {
    auto result=compilation({{"root.ips",source}},optimize);
    for(const auto& error:result.diagnostics) UNSCOPED_INFO(error.message);
    REQUIRE(result.succeeded()); return result.bytes;
}
GenerationResult generate(const std::vector<u8>& bytes,u32 seed=0,u32 width=0,u32 height=0) {
    const auto file=loadFile(bytes);validate(file);VM vm(file,{seed,width,height,{}});vm.run();return vm.takeResult();
}

const std::string scene=R"(asset t{size 8x4 output list part tiles output list part empty output list point trace
    save(p:part2d){tiles=p}
    draw{rs=regions(voronoi({0,0},{8,4},[{2,2},{6,2}])) p=part.create(rs,1)
        save(p) tiles=part.fill(part.place(p,{-2.25,3.5},90,0,0),#12345680)
        trace={part.id(p),part.source_id(p)}
    }})";
}

namespace {
struct ReferencePoint { double x,y; };
ReferencePoint edgeMap(unsigned kind,ReferencePoint p,ReferencePoint extent) {
    switch(kind) {
    case 0:return p;case 1:return {p.y,extent.x-p.x};case 2:return {extent.x-p.x,extent.y-p.y};
    case 3:return {extent.y-p.y,p.x};case 4:return {extent.x-p.x,p.y};case 5:return {p.x,extent.y-p.y};
    case 6:return {p.y,p.x};default:return {extent.y-p.y,extent.x-p.x};
    }
}
Vec2 permutation(unsigned kind,Vec2 v) {return kind==1||kind==3||kind>=6?Vec2{v.y,v.x}:v;}
std::string staticTransform(unsigned kind) {
    const std::array rotations{0,90,180,270,0,0,90,90};
    return ",rot="+std::to_string(rotations[kind])+",mirror_x="+std::to_string(kind==4||kind==7)+
        ",mirror_y="+std::to_string(kind==5||kind==6);
}
bool sameBits(float a,float b) {return std::bit_cast<u32>(a)==std::bit_cast<u32>(b);}
void checkPoint(Vec2 actual,ReferencePoint expected) {
    CHECK(sameBits(actual.x,static_cast<float>(expected.x)));CHECK(sameBits(actual.y,static_cast<float>(expected.y)));
}
}

TEST_CASE("nested part exports compose actual pixel edges for every D4 and source grid", "[part-outputs-v09]") {
    const std::string leaf=R"(asset t{size 7x5 origin 0.5,1.25 output list part geometry output list part rasters
        save(p:part2d){geometry=p}
        draw{g=part.create(regions(voronoi({-1.5,-0.5},{6,4},[{1.5,1.5}])),0)
        p=part.place(g,{-0.25,3.5},90*(seed&3),(seed>>2)&1,0)
        save(g) save(p) save(part.fill(p,#12345680))
        layer image(alpha=surface.source_over){rect(0,0,7,5,#aabbcc80)}
        save(part.texture(g,image)) rasters=part.create(image)}})";
    for(const bool optimize:{false,true})for(unsigned outer=0;outer<8;++outer)for(unsigned inner=0;inner<8;++inner) {
        const Sources sources{
            {"root.ips","import unused from \"unused.ips\" import middle from \"middle.ips\" import tail from \"tail.ips\" "
             "asset t{size 24x20 output list part root_empty draw{middle.draw(seed,at={5.25,-2.5}"+staticTransform(outer)+")}}"},
            {"unused.ips","asset t{size 1x1 draw{}}"},
            {"tail.ips","asset t{size 1x1 output list part never draw{}}"},
            {"middle.ips","import leaf from \"leaf.ips\" asset t{size 9x7 origin 1.25,-0.5 output list part middle_empty "
             "draw{leaf.draw(seed,at={-1.5,4.25}"+staticTransform(inner)+")}}"},
            {"leaf.ips",leaf}};
        const auto compiled=compilation(sources,optimize);
        for(const auto& error:compiled.diagnostics)UNSCOPED_INFO(error.message);
        REQUIRE(compiled.succeeded());
        for(const auto size:{RasterPoint{24,20},RasterPoint{48,60},RasterPoint{18,25},RasterPoint{30,30}})
        for(u32 seed=0;seed<8;++seed) {
            INFO("outer="<<outer<<" inner="<<inner<<" seed="<<seed<<" size="<<size.x<<","<<size.y<<" opt="<<optimize);
            auto result=generate(compiled.bytes,seed,static_cast<u32>(size.x),static_cast<u32>(size.y));
            REQUIRE(result.outputs.size()==5);CHECK(result.outputs[0].partSnapshots().empty());
            CHECK(result.outputs[1].partSnapshots().empty());CHECK(result.outputs[4].partSnapshots().empty());
            const auto parts=result.outputs[2].partSnapshots(),rasters=result.outputs[3].partSnapshots();
            REQUIRE(parts.size()==4);REQUIRE(rasters.size()==1);
            CHECK(std::all_of(result.image.pixels.begin(),result.image.pixels.end(),[](Color c){return c==Color{};}));
            const Vec2 rootScale{static_cast<float>(size.x)/24.f,static_cast<float>(size.y)/20.f};
            const auto middleScale=permutation(outer,rootScale),leafScale=permutation(inner,middleScale);
            const ReferencePoint middleExtent{std::max(1.f,std::round(9.f*middleScale.x)),std::max(1.f,std::round(7.f*middleScale.y))};
            const ReferencePoint leafExtent{std::max(1.f,std::round(7.f*leafScale.x)),std::max(1.f,std::round(5.f*leafScale.y))};
            const ReferencePoint middleOrigin{std::round(5.25f-1.25f*rootScale.x),std::round(-2.5f-(-0.5f)*rootScale.y)};
            const ReferencePoint leafOrigin{std::round(-1.5f-0.5f*middleScale.x),std::round(4.25f-1.25f*middleScale.y)};
            const auto toRoot=[&](ReferencePoint p) {
                p=edgeMap(inner,p,leafExtent);p.x+=leafOrigin.x;p.y+=leafOrigin.y;
                p=edgeMap(outer,p,middleExtent);return ReferencePoint{p.x+middleOrigin.x,p.y+middleOrigin.y};
            };
            for(u32 i=0;i<5;++i) {
                const auto& exported=i==4?rasters[0]:parts[i];
                const unsigned dynamic=i==1||i==2?std::array<unsigned,8>{0,1,2,3,4,7,5,6}[seed]:0u;
                const Vec2 sourceScale=permutation(dynamic,leafScale);
                ReferencePoint anchor{1.5*sourceScale.x,1.5*sourceScale.y};
                ReferencePoint lo{-1.5*sourceScale.x,-0.5*sourceScale.y},hi{4.5*sourceScale.x,3.5*sourceScale.y};
                if(i==4) {
                    const auto& buffer=*std::get<LayerValue>(exported.part.data->source).buffer;
                    u32 left=buffer.raster.width,top=buffer.raster.height,right=0,bottom=0;
                    for(u32 y=0;y<buffer.raster.height;++y)for(u32 x=0;x<buffer.raster.width;++x)
                        if(buffer.raster.at(x,y).a){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}
                    REQUIRE(left<right);lo={double(left),double(top)};hi={double(right),double(bottom)};
                    anchor={(lo.x+hi.x)*0.5,(lo.y+hi.y)*0.5};
                }
                ReferencePoint translation{};
                if(i==1||i==2) {
                    const auto a=edgeMap(dynamic,anchor,{});
                    translation={std::floor(-0.25*leafScale.x-a.x+0.5),std::floor(3.5*leafScale.y-a.y+0.5)};
                }
                const auto mapped=[&](ReferencePoint p) {
                    p=edgeMap(dynamic,p,{});p.x+=translation.x;p.y+=translation.y;return toRoot(p);
                };
                const auto zero=mapped({0,0}),basisX=mapped({1,0}),basisY=mapped({0,1});
                unsigned composed=8;
                for(unsigned k=0;k<8;++k) {
                    const auto x=edgeMap(k,{1,0},{}),y=edgeMap(k,{0,1},{});
                    if(x.x==basisX.x-zero.x&&x.y==basisX.y-zero.y&&y.x==basisY.x-zero.x&&y.y==basisY.y-zero.y)composed=k;
                }
                CHECK(exported.transform.kind()==composed);
                CHECK(exported.translation==RasterPoint{static_cast<i32>(zero.x),static_cast<i32>(zero.y)});
                CHECK(exported.sourceScale==sourceScale);CHECK(exported.targetScale==rootScale);
                const auto center=mapped(anchor);checkPoint(exported.center,{center.x/rootScale.x,center.y/rootScale.y});
                double xMin=1e30,yMin=1e30,xMax=-1e30,yMax=-1e30;
                for(const auto corner:{lo,ReferencePoint{hi.x,lo.y},hi,ReferencePoint{lo.x,hi.y}}) {
                    const auto p=mapped(corner);xMin=std::min(xMin,p.x);yMin=std::min(yMin,p.y);xMax=std::max(xMax,p.x);yMax=std::max(yMax,p.y);
                }
                checkPoint(exported.boundsOrigin,{xMin/rootScale.x,yMin/rootScale.y});
                checkPoint(exported.boundsSize,{(xMax-xMin)/rootScale.x,(yMax-yMin)/rootScale.y});
                CHECK(exported.part.data->id==(i==4?1:0));CHECK(exported.index==(i==4?0:i));
            }
            CHECK(parts[2].part.data->fill==Color{18,52,86,128});
            CHECK(parts[3].part.data->texture->buffer==std::get<LayerValue>(rasters[0].part.data->source).buffer);
        }
    }
}

TEST_CASE("rendering and snapshot order stay independent including empty clips", "[part-outputs-v09]") {
    const auto source=[](bool draw,bool output) {
        return "asset t{size 4x3 output list part pieces draw{layer image(alpha=surface.source_over){pixel(1,1,#12345680)}"
            "p=part.create(image) q=part.fill(part.place(p,{2.5,1.5},0,0,0),#ffeecc80)"
            "layer rendered(alpha=surface.source_over){"+std::string(output?"pieces=q ":"")+
            (draw?"part.draw(p) ":"")+(output?"pieces=p ":"")+(draw?"part.draw(q) part.draw(p) ":"")+
            (output?"pieces=q ":"")+"}surface.paint(rendered,blend=surface.source_over)}}";
    };
    for(const bool optimize:{false,true}) {
        const auto render=generate(compile(source(true,false),optimize));
        const auto output=generate(compile(source(false,true),optimize));
        const auto both=generate(compile(source(true,true),optimize));
        CHECK(render.outputs[0].partSnapshots().empty());
        CHECK(render.image.pixels==both.image.pixels);
        CHECK(std::any_of(render.image.pixels.begin(),render.image.pixels.end(),[](Color c){return c.a>0;}));
        CHECK(std::all_of(output.image.pixels.begin(),output.image.pixels.end(),[](Color c){return c==Color{};}));
        for(const auto* result:{&output,&both}) {
            const auto entries=result->outputs[0].partSnapshots();REQUIRE(entries.size()==3);
            CHECK(entries[0].part.data==entries[2].part.data);CHECK(entries[0].part.data->id==0);
            CHECK(entries[0].center==Vec2{2.5f,1.5f});CHECK(entries[1].center==Vec2{1.5f,1.5f});
            CHECK(entries[0].part.data->fill==Color{255,238,204,128});CHECK_FALSE(entries[1].part.data->fill);
        }
        const auto clipped=generate(compile(R"(asset t{size 4x3 output list part pieces draw{
            layer image(alpha=surface.source_over){pixel(1,1,#12345680)} p=part.create(mask.from(image))
            clip(mask.subtract(mask.from(image),mask.from(image))){pieces=p}}})",optimize));
        const auto entries=clipped.outputs[0].partSnapshots();REQUIRE(entries.size()==1);
        CHECK(entries[0].center==Vec2{1.5f,1.5f});CHECK_FALSE(entries[0].part.data->fill);
        const auto& mask=std::get<MaskValue>(entries[0].part.data->source);
        CHECK(mask.buffer->raster.at(1,1).a==128);
    }
}

TEST_CASE("static output slots retain earlier calls and share creation and append counters", "[part-outputs-v09]") {
    for(const bool optimize:{false,true}) {
        const Sources sources{{"root.ips",R"(import child from "child.ips" asset t{size 8x8 output list part root
            draw{p=part.create(regions(voronoi({0,0},{2,2},[{1,1}])),0)root=p
            child.draw(0,at={2,3})root=part.fill(p,#abc)
            child.draw(0,at={-5,8})child.draw(1,at={30,30})
            root=part.create(regions(voronoi({0,0},{2,2},[{1,1}])),0)}})"},
            {"child.ips",R"(asset t{size 2x2 output list part pieces draw{
            p=part.create(regions(voronoi({0,0},{2,2},[{1,1}])),0)if(seed==0){pieces=p}}})"}};
        const auto compiled=compilation(sources,optimize);REQUIRE(compiled.succeeded());
        const auto first=generate(compiled.bytes);const auto again=generate(compiled.bytes,71);
        REQUIRE(first.outputs.size()==2);const auto root=first.outputs[0].partSnapshots(),child=first.outputs[1].partSnapshots();
        REQUIRE(root.size()==3);REQUIRE(child.size()==2);
        CHECK(root[0].part.data->id==0);CHECK(root[1].part.data->id==0);CHECK(root[2].part.data->id==4);
        CHECK(child[0].part.data->id==1);CHECK(child[1].part.data->id==2);
        CHECK(child[0].center==Vec2{3,4});CHECK(child[1].center==Vec2{-4,9});
        CHECK(child[0].index==0);CHECK(child[1].index==1);
        CHECK(first.outputs[1].parts!=again.outputs[1].parts);CHECK(child[0].center==Vec2{3,4});
        CHECK_FALSE(compilation({{"root.ips","import child from \"child.ips\" asset t{size 8x8 draw{p=child.outputs.pieces}}"},
                                 {"child.ips",sources.at("child.ips")}},optimize).succeeded());
        for(const bool overflow:{false,true}) {
            auto limit=sources;
            limit["root.ips"]="import child from \"child.ips\" asset t{size 8x8 output list part root draw{"
                "p=part.create(regions(voronoi({0,0},{2,2},[{1,1}])),0)for i in 0..255{root=p}"
                "child.draw(0,at={0,0})"+std::string(overflow?"root=p":"")+"}}";
            const auto bytes=compilation(limit,optimize);REQUIRE(bytes.succeeded());
            if(overflow)CHECK_THROWS(generate(bytes.bytes));
            else {const auto result=generate(bytes.bytes);CHECK(result.outputs[0].partSnapshots().size()==255);CHECK(result.outputs[1].partSnapshots().size()==1);}
        }
    }
}

TEST_CASE("list part output compiles formats and roundtrips as a typed append", "[part-outputs-v09]") {
    for(const bool optimize:{false,true}) {
        const auto bytes=compile(scene,optimize);
        const auto file=loadFile(bytes);validate(file);
        CHECK(file.meta.outputs[0].kind==OutputKind::Part);CHECK(file.meta.outputs[0].isList);
        CHECK(file.functionOwners.size()==file.code.functions.size());CHECK(file.resourceExports.empty());
        const auto assembly=disassemble(bytes);
        CHECK(assembly.find("out.list.part")!=std::string::npos);
        CHECK(disassemble(assemble(assembly))==assembly);
        auto inferred=assembly;
        for(std::size_t at=0;(at=inferred.find("owner: ",at))!=std::string::npos;)
            inferred.erase(at,inferred.find('\n',at)-at+1);
        CHECK(disassemble(assemble(inferred))==assembly); // root/helper owners infer without redundant declarations
        const auto formatted=ips::formatProgram(ips::parse(scene));
        CHECK(ips::formatProgram(ips::parse(formatted))==formatted);
        CHECK(disassemble(compile(formatted,optimize))==assembly);
        auto result=generate(bytes);REQUIRE(result.outputs.size()==3);
        const auto records=result.outputs[0].partSnapshots();REQUIRE(records.size()==2);
        CHECK(records[0].index==0);CHECK(records[1].index==1);
        CHECK(records[0].part.data->id==0);CHECK(records[1].part.data->id==0);
        CHECK(records[0].part.data->sourceId==1);CHECK_FALSE(records[0].part.data->fill);
        CHECK(records[1].part.data->fill==Color{18,52,86,128});
        CHECK(records[0].transform.kind()==0);CHECK(records[1].transform.kind()==1);
        CHECK(std::get<RegionSource>(records[0].part.data->source).geometry==
              std::get<RegionSource>(records[1].part.data->source).geometry);
        CHECK(result.outputs[1].partSnapshots().empty());CHECK(result.outputs[2].points.size()==1);
        for(const auto pixel:result.image.pixels) CHECK(pixel.a==0);
        const auto copy=result;CHECK(copy.outputs[0].parts==result.outputs[0].parts);
        const auto again=generate(bytes);CHECK(again.outputs[0].parts!=result.outputs[0].parts);
        CHECK(records[1].part.data->fill==Color{18,52,86,128});
    }
    for(const auto bad:{
        "asset t{size 1x1 output part p draw{}}",
        "asset t{size 1x1 output list part p draw{p=1}}",
        "asset t{size 1x1 output list part p draw{q=p}}",
        "asset t{size 1x1 output list part p output point p draw{}}"})
        CHECK_FALSE(compilation({{"root.ips",bad}},false).succeeded());
    CHECK_THROWS(assemble("size: 1x1\noutput: part p\nfunc draw\nreturn\nendfunc\n"));
}

TEST_CASE("part exports retain holes rasters and immutable views without copying resources", "[part-outputs-v09]") {
    std::shared_ptr<const PartOutputData> retained;
    std::weak_ptr<const GeometryData> geometryWeak;
    {
        LoadedFile file;file.code.functions.emplace_back();detail::Execution ex(file);
        ex.target=RenderTarget::create(16,16,{1,1});ex.frames.emplace_back();ex.fuel=1000000;
        ex.outputSlots.emplace_back();ex.outputSlots.back().kind=OutputKind::Part;ex.outputSlots.back().isList=true;
        ex.slotOwner.push_back(detail::kNoPartIndex);
        const Regions2dValue regions{geometry_fixture::holes(ex)};geometryWeak=regions.data;
        const auto part=detail::createPart(ex,regions,0);const auto before=ex.resources.bytes;
        ex.push(part);REQUIRE(detail::stepOutputs(ex,{Opcode::OutListPart,0,0}));
        CHECK(ex.resources.bytes==before+128+32+128);
        ex.push(part);REQUIRE(detail::stepOutputs(ex,{Opcode::OutListPart,0,0}));
        CHECK(ex.resources.bytes==before+128+32+256);
        retained=ex.outputSlots[0].parts;
        CHECK(retained->records[0].part.data==part.data);
        const auto& source=std::get<RegionSource>(retained->records[0].part.data->source);
        CHECK(source.geometry==regions.data);CHECK(source.geometry->regionHoles(source.region).size()==1);
        CHECK(source.geometry->rings[1].hole);
        CHECK_THROWS(detail::stepOutputs(ex,{Opcode::OutListPart,1,0}));
        CHECK_THROWS(detail::stepOutputs(ex,{Opcode::OutListPart,0,1}));
        ex.push(Part2dValue{});CHECK_THROWS(detail::stepOutputs(ex,{Opcode::OutListPart,0,0}));
        CHECK(ex.exportedParts==2);
    }
    REQUIRE_FALSE(geometryWeak.expired());
    CHECK(retained->records[0].part.data->sourceId==9);
    CHECK(std::get<RegionSource>(retained->records[0].part.data->source).geometry->rings[1].count==4);
    retained.reset();CHECK(geometryWeak.expired());
}

TEST_CASE("all part slots share the 256 snapshot cap independently of creation IDs", "[part-outputs-v09]") {
    for(const bool optimize:{false,true}) {
        const auto source=[](int last) {return "asset t{size 2x2 output list part a output list part b draw{"
            "p=part.create(regions(voronoi({0,0},{2,2},[{1,1}])),0)"
            "for i in 0..128{a=p b=p}"+std::string(last?"a=p":"")+"}}";};
        const auto good=generate(compile(source(0),optimize));
        for(const auto& slot:good.outputs) {
            REQUIRE(slot.partSnapshots().size()==128);
            for(u32 i=0;i<128;++i) {CHECK(slot.partSnapshots()[i].index==i);CHECK(slot.partSnapshots()[i].part.data->id==0);}
        }
        const auto file=loadFile(compile(source(1),optimize));validate(file);VM vm(file,{});
        CHECK_THROWS(vm.run());CHECK_THROWS(vm.result());CHECK_THROWS(vm.takeResult());
    }
}

TEST_CASE("part export bytecode validates kinds flags ownership and dead instructions", "[part-outputs-v09]") {
    const auto bytes=compile(scene,false);const auto file=loadFile(bytes);
    const auto invalid=[&](std::size_t at,u8 value) {auto changed=bytes;changed.at(at)=value;CHECK_THROWS(validate(loadFile(changed)));};
    for(const auto& fn:file.code.functions) for(std::size_t pc=0;pc<fn.code.size();pc+=4)
        if(fn.code[pc]==static_cast<u8>(Opcode::OutListPart)) {
            invalid(fn.fileOffset+pc+1,1);invalid(fn.fileOffset+pc+2,255);
            invalid(fn.fileOffset+pc,static_cast<u8>(Opcode::OutListPoint));
        }
    // The output entry follows fixed meta bytes, zero inputs, output count.
    const auto meta=file.layout.find(SectionId::Meta)->offset;
    invalid(meta+36+1,0);invalid(meta+36,255);
    const auto exports=file.layout.find(SectionId::ResourceExports)->offset;
    invalid(exports+8,0); // root entry cannot claim static-part owner zero
    auto noOwners=loadFile(bytes);noOwners.functionOwners.clear();CHECK_THROWS(validate(noOwners));
    const Sources foreign{{"root.ips","import child from \"child.ips\" asset t{size 4x4 output list part root draw{child.draw(seed,at={0,0})}}"},
        {"child.ips","asset t{size 2x2 output list part own draw{own=part.create(regions(voronoi({0,0},{2,2},[{1,1}])),0)}}"}};
    auto result=compilation(foreign,false);
    for(const auto& error:result.diagnostics) UNSCOPED_INFO(error.message);
    REQUIRE(result.succeeded());auto parsed=loadFile(result.bytes);validate(parsed);
    for(const auto& fn:parsed.code.functions) for(std::size_t pc=0;pc<fn.code.size();pc+=4)
        if(fn.code[pc]==static_cast<u8>(Opcode::OutListPart)) {
            auto changed=result.bytes;changed[fn.fileOffset+pc+2]=0;changed[fn.fileOffset+pc+3]=0;
            CHECK_THROWS(validate(loadFile(changed)));
        }
}
