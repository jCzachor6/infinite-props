#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/VM.hpp"
#include "iprop/math/Distribution.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include <bit>
#include <cmath>
#include <limits>

using namespace iprop;
namespace {
std::vector<u8> compilePoints(const std::string& source, bool optimize = true) {
    auto program = ips::parse(source); ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}
GenerationResult generatePoints(const std::vector<u8>& bytes, u32 seed = 26, u32 variant = 0) {
    const auto file = loadFile(bytes); validate(file); VM vm(file, {seed, variant, 0, 0, 0, {}});
    vm.run(); return vm.result();
}
std::vector<u32> outputBits(const GenerationResult& result) {
    std::vector<u32> bits;
    for (const auto& output : result.outputs) for (const auto& p : output.points) {
        bits.push_back(std::bit_cast<u32>(p.x)); bits.push_back(std::bit_cast<u32>(p.y));
    }
    return bits;
}
std::string assetPoints(const std::string& declarations, const std::string& body, const std::string& helpers = {}) {
    return "asset t{size 4x4 variants 2 output list point p output point state " + declarations + helpers +
        " draw{" + body + " state={seed&65535,seed>>16}}}";
}
void rejectPoints(const std::string& source, const std::string& message) {
    try { (void)compilePoints(source); FAIL("invalid point description accepted"); }
    catch (const ips::SourceError& error) {
        INFO(error.what()); CHECK(std::string(error.what()).find(message) != std::string::npos);
        CHECK(error.location.line > 0); CHECK(error.location.column > 0);
    }
}
Distribution weighted(std::initializer_list<DistributionPoint> points) {
    auto data = std::make_shared<PointDistributionData>(); data->family = DistributionFamily::Weighted;
    data->points = points; REQUIRE(math::pointDistributionError(*data) == nullptr);
    Distribution d; d.family = data->family; d.points = std::move(data); return d;
}
u32 rawHash(u32 x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; return x ^ (x >> 16); }
void put32(std::vector<u8>& bytes, std::size_t at, u32 value) {
    for (int i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<u8>(value >> (8 * i));
}
}

TEST_CASE("weighted CDF boundaries clipping exact keys and extreme weights", "[point-distribution]") {
    const auto d = weighted({{i32{1},i32{1}},{i32{2},i32{0}},{i32{3},i32{1}},{i32{4},i32{2}}});
    for (const auto& [u, expected] : std::initializer_list<std::pair<double,double>>{
        {0.125,1}, {0.25,3}, {0.499999999,3}, {0.5,4}, {0.999999999,4}}) {
        const auto draw = math::truncatedDraw(d,1,4,u); CHECK(draw.status == math::DrawStatus::Ok); CHECK(draw.value == expected);
    }
    CHECK(math::truncatedDraw(d,2,3,0.9).value == 3);
    CHECK(math::truncatedDraw(d,1,1,0.9).value == 1);
    CHECK(math::truncatedDraw(d,2,2,0.1).status == math::DrawStatus::NoMass);
    CHECK(math::truncatedDraw(d,5,9,0.5).status == math::DrawStatus::NoMass);
    const auto large = weighted({{i32{16777217},1.f},{16777218.f,i32{1}},{i32{16777219},1.f}});
    CHECK(math::truncatedDraw(large,16777217,16777219,0.1).value == 16777217);
    CHECK(math::truncatedDraw(large,16777217,16777219,0.5).value == 16777218);
    CHECK(math::truncatedDraw(large,16777217,16777219,0.9).value == 16777219);
    const auto extremes = weighted({{i32{-2147483647},std::numeric_limits<f32>::denorm_min()},
        {i32{2147483647},std::numeric_limits<f32>::max()}});
    CHECK(math::truncatedDraw(extremes,-2147483647,-2147483647,0.9).value == -2147483647);
    CHECK(math::truncatedDraw(extremes,-2147483647,2147483647,0.01).value == 2147483647);
}

TEST_CASE("weighted draws match independent raw RNG thresholds and rounded atoms", "[point-distribution]") {
    const std::string source = assetPoints("", "d=weighted{1:50,2:20,3:20,4:10} for i in 0..64{x=1 to 4 with d p={x,0}}");
    for (const bool optimize : {false,true}) {
        const auto bytes = compilePoints(source,optimize);
        CHECK(loadFile(bytes).layout.header.versionMinor == 6);
        for (u32 seed : {0u,1u,7u,26u,42u,0xffffffffu}) {
            const auto result = generatePoints(bytes,seed);
            u32 stream = seed;
            for (const auto& point : result.outputs[0].points) {
                stream += 0x9e3779b9u;
                const double target = (static_cast<double>(rawHash(stream))+0.5)*0x1p-32*100.0;
                CHECK(point.x == (target < 50 ? 1.f : target < 70 ? 2.f : target < 90 ? 3.f : 4.f));
            }
            CHECK(result.outputs[1].points[0].x == static_cast<f32>(stream & 65535));
            CHECK(result.outputs[1].points[0].y == static_cast<f32>(stream >> 16));
        }
        const auto round = generatePoints(compilePoints(assetPoints("", "d=weighted{(-1.5):1,1.5:1} x=(-2) to 2 with d p={x,0}"),optimize),0);
        CHECK(round.outputs[0].points[0].x == -2.f);
    }
    const auto bytes = compilePoints(assetPoints("", "d=weighted{1:75,2:20,3:5} for i in 0..5000{p={1 to 3 with d,0}}"));
    const auto sample = generatePoints(bytes,73);
    std::array<int,3> counts{}; for (const auto& p : sample.outputs[0].points) ++counts[static_cast<int>(p.x)-1];
    CHECK(std::abs(counts[0]-3750) < 150); CHECK(std::abs(counts[1]-1000) < 120); CHECK(std::abs(counts[2]-250) < 75);
}

TEST_CASE("point descriptions snapshot effects and transfer through helpers lists and samplers", "[point-distribution]") {
    const auto helpers = "value(x:i32)->i32{pixel(x,0,#fff) return x+index(3)} echo(d:distribution)->distribution{return d}";
    const auto direct = assetPoints("", "lo=value(0) weight=value(1)+1 before=seed d=weighted{lo:weight,20:value(2)+1} p={seed==before,0} "
        "lo=100 weight=0 copy=echo(d) d=weighted{30:1} s=sampler(0 to 20 with copy) items=[copy,copy] x=s.pick() p={x,0 to 20 with items[1]}",helpers);
    const auto manual = assetPoints("", "lo=value(0) weight=value(1)+1 before=seed hiWeight=value(2)+1 "
        "p={0,0} d=weighted{lo:weight,20:hiWeight} p={0 to 20 with d,0 to 20 with d}",helpers);
    for (const bool optimize : {false,true}) for (const u32 seed : {0u,26u,0xffffffffu})
        CHECK(outputBits(generatePoints(compilePoints(direct,optimize),seed)) == outputBits(generatePoints(compilePoints(manual,false),seed)));
    const std::string declarations = "const distribution d=weighted{1:50,2:20,3:20,4:10} "
        "const list<distribution> ds=[d,d] const sampler<i32> s=sampler(1 to 4 with d) "
        "const list<sampler<i32>> ss=[s,s] palette specs=[1 to 4 with d,7]";
    const auto reference = compilePoints(assetPoints("", "d=weighted{1:50,2:20,3:20,4:10} p={1,0} for i in 0..3{p={1 to 4 with d,0}} p={1 to 4 with d,1 to 4 with d}"),false);
    for (const bool optimize : {false,true}) {
        const auto source = assetPoints(declarations,"before=seed items=ds descs=ss copy=echo(items[0]) p={seed==before,0} "
            "batch=descs[0].pick(3) for i in 0..3{p={batch[i],0}} p={specs[0].pick(),1 to 4 with copy}","echo(shape:distribution)->distribution{return shape}");
        const auto bytes = compilePoints(source,optimize);
        const auto file = loadFile(bytes); REQUIRE(file.collections.distributions.size() == 1);
        CHECK(file.collections.samplers[0].data->distribution.points == file.collections.distributions[0].points);
        CHECK(std::get<Distribution>(file.collections.lists[0].data->elements[0]).points == file.collections.distributions[0].points);
        for (const u32 seed : {0u,1u,26u,0xffffffffu}) CHECK(outputBits(generatePoints(bytes,seed)) == outputBits(generatePoints(reference,seed)));
        const auto restored = assemble(disassemble(bytes));
        CHECK(outputBits(generatePoints(bytes)) == outputBits(generatePoints(restored)));
        CHECK(outputBits(generatePoints(bytes)) == outputBits(generatePoints(compressFile(bytes))));
        CHECK(stripDebug(bytes) == stripDebug(compilePoints(ips::formatProgram(ips::parse(source)),optimize)));
    }
}

TEST_CASE("point literals reject invalid static data and trap on invalid runtime snapshots", "[point-distribution]") {
    for (const auto& [literal,message] : std::initializer_list<std::pair<const char*,const char*>>{
        {"weighted{}","between 1 and 256"},{"weighted{1:0}","positive total mass"},
        {"weighted{1:(-1)}","nonnegative"},{"weighted{1:1,1:2}","strictly increasing"},
        {"weighted{2:1,1:2}","strictly increasing"},{"weighted{1:(1.0/0.0)}","finite"},
        {"weighted{#fff:1}","numeric"},{"weighted{1:{1,2}}","numeric"}}) {
        rejectPoints(assetPoints("const distribution d="+std::string(literal),""),message);
        rejectPoints(assetPoints("","d="+std::string(literal)),message);
    }
    rejectPoints(assetPoints("const distribution d=weighted{seed:1}",""),"only asset constants");
    rejectPoints(assetPoints("const distribution d=weighted{1:unit()}",""),"RNG");
    rejectPoints(assetPoints("const distribution d=weighted{1:next} const i32 next=int(0 to 1 with d)",""),"random ranges");
    for (const bool optimize : {false,true}) {
        const auto invalid = compilePoints(assetPoints("","d=weighted{1:(v-1)}"),optimize);
        CHECK_THROWS_AS(generatePoints(invalid,0,0),Trap);
        CHECK_THROWS_AS(generatePoints(invalid,0,1),Trap);
        const auto valid = compilePoints(assetPoints("","d=weighted{1:v} s=sampler(10 to 20 with d) empty=s.pick(0) p={list.len(empty),0}"),optimize);
        CHECK_NOTHROW(generatePoints(valid,0,1));
        CHECK_THROWS_AS(generatePoints(compilePoints(assetPoints("","d=weighted{1:1} p={10 to 20 with d,0}"),optimize)),Trap);
        CHECK_THROWS_AS(generatePoints(compilePoints(assetPoints("","if(v==1){d=weighted{1:1}} p={1 to 1 with d,0}"),optimize),0,0),Trap);
    }
}

TEST_CASE("point builders support 256 pairs and constants share quota and fuel", "[point-distribution]") {
    std::string literal = "weighted{";
    for (int i=0;i<256;++i) { if(i)literal+=","; literal+=std::to_string(16777217+i)+":1"; } literal += "}";
    for (const bool optimize : {false,true}) {
        const auto source = assetPoints("const distribution d="+literal,"copy=d x=weighted{1:1} p={16777217 to 16777472 with copy,0}");
        CHECK_NOTHROW(generatePoints(compilePoints(source,optimize)));
        rejectPoints(assetPoints("", "d="+literal.substr(0,literal.size()-1)+",16777473:1}"),"between 1 and 256");
    }
    const auto file = loadFile(compilePoints(assetPoints("const distribution d=weighted{1:1,2:1} const sampler<i32> s=sampler(1 to 2 with d) "
        "const list<distribution> ds=[d,d]", "copy=d desc=s items=ds"),false));
    detail::Execution ex(file); ex.frames.push_back({}); ex.fuel=100; ex.frame().stream=26;
    REQUIRE(detail::stepPointDistributions(ex,{Opcode::ConstDistribution,0,0}));
    CHECK(ex.resources.bytes == ResourceArena::distributionBytes(2)); CHECK(ex.fuel == 98);
    REQUIRE(detail::stepPointDistributions(ex,{Opcode::ConstDistribution,0,0})); CHECK(ex.fuel == 98);
    REQUIRE(detail::stepSamplers(ex,{Opcode::ConstSampler,0,0}));
    CHECK(ex.resources.bytes == ResourceArena::distributionBytes(2)+ResourceArena::samplerBytes());
    REQUIRE(detail::stepCollections(ex,{Opcode::ConstList,0,0}));
    CHECK(ex.resources.bytes == ResourceArena::distributionBytes(2)+ResourceArena::samplerBytes()+ResourceArena::listBytes(2));
    CHECK(ex.frame().stream == 26);
    detail::Execution full(file); full.frames.push_back({}); full.fuel=1000; full.resources.charge(kMaxGenerationData-1);
    CHECK_THROWS_AS(detail::stepCollections(full,{Opcode::ConstList,0,0}),Trap);
    CHECK(full.resources.distributionConstants.empty()); CHECK(full.resources.constants.empty());
    full.push(i32{256}); CHECK_THROWS_AS(detail::stepPointDistributions(full,{Opcode::DistributionBegin,0,4}),Trap);
}

TEST_CASE("point distribution pools and unreachable opcodes reject corrupt and old files", "[point-distribution]") {
    const auto bytes = compilePoints(assetPoints("const distribution d=weighted{1:1,2:2}","p={1 to 2 with d,0}"),false);
    const auto file = loadFile(bytes); const auto offset = file.layout.find(SectionId::Distributions)->offset;
    for (const auto& [at,value] : std::initializer_list<std::pair<std::size_t,u32>>{
        {offset,65537},{offset+4,0},{offset+4,0x01000204},{offset+8,2},{offset+8,0x10000},
        {offset+12,2},{offset+16,0xffffffff},{offset+24,1}}) {
        auto bad=bytes; put32(bad,at,value); CHECK_THROWS_AS(loadFile(bad),FormatError);
    }
    for (u16 minor=1;minor<6;++minor) {auto bad=bytes;bad[6]=static_cast<u8>(minor);CHECK_THROWS_AS(loadFile(bad),FormatError);}
    for (const auto& instruction : {"const.distribution 0","distribution.begin 4","distribution.append 0","distribution.end"}) {
        const auto dead=assemble("meta: size=1x1\nfn draw ( i32, i32 ) -> ( ):\nreturn\nnop\nendfn\n");
        auto bad=dead; const auto at=loadFile(bad).code.entry().fileOffset+4;
        const auto opcode = instruction[0]=='c' ? Opcode::ConstDistribution : instruction==std::string("distribution.begin 4") ? Opcode::DistributionBegin :
            instruction==std::string("distribution.append 0") ? Opcode::DistributionAppend : Opcode::DistributionEnd;
        bad[at]=static_cast<u8>(opcode);
        for (u16 minor=1;minor<6;++minor) {bad[6]=static_cast<u8>(minor);CHECK_THROWS_AS(validate(loadFile(bad)),FormatError);}
    }
}

TEST_CASE("weighted remains contextual and formats point comments without changing effects", "[point-distribution]") {
    const auto source = assetPoints("", "weighted=3 density=4 p={weighted,density} x=weighted(2) d=weighted{1:2,/* mass */ 3:4,} p={x,1 to 3 with d}",
        "weighted(x:i32)->i32{return x+1} density(x:i32)->i32{return x+2}");
    const auto formatted = ips::formatProgram(ips::parse(source)); CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
    CHECK(formatted.find("/* mass */") != std::string::npos);
    for (const bool optimize : {false,true}) CHECK(stripDebug(compilePoints(source,optimize)) == stripDebug(compilePoints(formatted,optimize)));
}

TEST_CASE("point draws preserve caller and part streams and preflight processing fuel", "[point-distribution]") {
    const auto child = ips::parse("asset child{size 1x1 const distribution d=weighted{1:75,2:20,3:5} output point p draw{p={1 to 3 with d,1 to 3 with d}}}");
    const auto root = ips::parse("import child from \"child.ips\" "+assetPoints("const distribution shape=weighted{10:1,20:1}",
        "before=seed child.draw(seed,v,a,at={0,0}) p={seed==before,10 to 20 with shape}"));
    ips::checkAsset(child,{}); ips::checkAsset(root,{{"child",partInfoOf(child.asset)}});
    const auto reference = compilePoints(assetPoints("", "p={1,10 to 20 with weighted{10:1,20:1}}"),false);
    for (const bool optimize : {false,true}) for (const u32 seed : {0u,26u,0xffffffffu}) {
        const auto actual=outputBits(generatePoints(compileProgram(root,{{"child",&child}},optimize),seed));
        REQUIRE(actual.size()==6);
        CHECK(std::vector<u32>(actual.begin(),actual.begin()+4) == outputBits(generatePoints(reference,seed)));
        u32 stream=seed;
        for(std::size_t i=4;i<6;++i) {
            stream+=0x9e3779b9u;const double u=(static_cast<double>(rawHash(stream))+0.5)*0x1p-32;
            CHECK(actual[i]==std::bit_cast<u32>(u<0.75?1.f:u<0.95?2.f:3.f));
        }
    }
    const auto file=loadFile(reference);
    for (const u64 fuel : {u64{3},u64{4}}) {
        detail::Execution ex(file);ex.frames.push_back({});ex.fuel=fuel;ex.frame().stream=26;
        ex.push(i32{10});ex.push(i32{20});ex.push(weighted({{i32{10},i32{1}},{i32{20},i32{1}}}));
        if(fuel==3) CHECK_THROWS_AS(detail::stepSeeded(ex,{Opcode::RngIntWith,0,0}),Trap);
        else {CHECK(detail::stepSeeded(ex,{Opcode::RngIntWith,0,0}));CHECK(ex.fuel==0);CHECK(std::get<i32>(ex.pop())==20);}
        CHECK(ex.frame().stream==26u+0x9e3779b9u);
    }
    detail::Execution reversed(file);reversed.frames.push_back({});reversed.fuel=100;reversed.frame().stream=26;
    reversed.push(i32{20});reversed.push(i32{10});reversed.push(weighted({{i32{10},i32{1}}}));
    CHECK_THROWS_AS(detail::stepSeeded(reversed,{Opcode::RngIntWith,0,0}),Trap);CHECK(reversed.frame().stream==26);
    detail::Execution empty(file);empty.frames.push_back({});empty.fuel=100;empty.frame().stream=26;
    empty.push(i32{99});empty.push(i32{99});empty.push(weighted({{i32{10},i32{1}}}));
    CHECK_THROWS_AS(detail::stepSeeded(empty,{Opcode::RngIntWith,0,0}),Trap);
    CHECK(empty.frame().stream==26u+0x9e3779b9u);CHECK(empty.fuel==98);
}

TEST_CASE("mixed legacy and point distribution records retain transport and exact pool identities", "[point-distribution]") {
    const std::string declarations="const distribution points=weighted{lo:50,2:20,3:20,4:10} const i32 lo=1 "
        "const distribution same=weighted{1:50,2:20,3:20,4:10} const distribution typed=weighted{1.0:50,2:20,3:20,4:10} "
        "const list<distribution> descriptions=[gaussian(0,2),points,beta(2,3,0,5),same,typed]";
    const auto source=assetPoints(declarations,"items=descriptions for i in 0..5{p={1.0 to 4.0 with items[i],0}} "
        "s=sampler(1 to 4 with items[1]) empty=s.pick(0) p={list.len(empty),0}");
    const auto reference=compilePoints(assetPoints("","p={1.0 to 4.0 with gaussian(0,2),0} "
        "p={1.0 to 4.0 with weighted{1:50,2:20,3:20,4:10},0} p={1.0 to 4.0 with beta(2,3,0,5),0} "
        "p={1.0 to 4.0 with weighted{1:50,2:20,3:20,4:10},0} p={1.0 to 4.0 with weighted{1.0:50,2:20,3:20,4:10},0} p={0,0}"),false);
    for(const bool optimize:{false,true}) {
        const auto bytes=compilePoints(source,optimize);const auto file=loadFile(bytes);
        REQUIRE(file.collections.distributions.size()==2);REQUIRE(file.collections.lists.size()==1);
        const auto& list=file.collections.lists[0].data->elements;
        CHECK(std::get<Distribution>(list[1]).points==std::get<Distribution>(list[3]).points);
        CHECK(std::get<Distribution>(list[1]).points!=std::get<Distribution>(list[4]).points);
        const auto restored=assemble(disassemble(bytes));
        for(const u32 seed:{0u,7u,26u,0xffffffffu}) {
            CHECK(outputBits(generatePoints(bytes,seed))==outputBits(generatePoints(reference,seed)));
            CHECK(outputBits(generatePoints(restored,seed))==outputBits(generatePoints(reference,seed)));
        }
    }
}
