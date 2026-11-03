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
GenerationResult generate(const std::vector<u8>& bytes) {
    const auto file=loadFile(bytes);validate(file);VM vm(file,{});vm.run();return vm.takeResult();
}
const std::string scene=R"(asset t{size 8x4 output list part tiles output list part empty output list point trace
    save(p:part2d){tiles=p}
    draw{rs=regions(voronoi({0,0},{8,4},[{2,2},{6,2}])) p=part.create(rs,1)
        save(p) tiles=part.fill(part.place(p,{-2.25,3.5},90,0,0),#12345680)
        trace={part.id(p),part.source_id(p)}
    }})";
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
