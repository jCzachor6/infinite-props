// No public constructor opcodes exist until stage 05. This executable links the
// actual geometry core into both architectures and exposes hand-authored shapes
// for an independent, exact-bit JS gate, without adding a test-only product API.
#include "support/geometry_fixture.hpp"
#include <bit>
#include <cstdio>

using namespace iprop;
namespace {
void values(const GeometryData& data){
    std::printf("\"points\":%zu,\"edges\":%zu,\"uses\":%zu,\"rings\":%zu,\"regions\":[",data.points.size(),data.edges.size(),data.uses.size(),data.rings.size());
    for(u32 i=0;i<data.regions.size();++i){
        const auto& r=data.regions[i];
        if(i)std::printf(",");
        std::printf("{\"id\":%d,\"bits\":[%u,%u,%u,%u,%u,%u,%u],\"holes\":[",r.sourceId,
            std::bit_cast<u32>(r.area),std::bit_cast<u32>(r.center.x),std::bit_cast<u32>(r.center.y),
            std::bit_cast<u32>(r.boundsOrigin.x),std::bit_cast<u32>(r.boundsOrigin.y),
            std::bit_cast<u32>(r.boundsSize.x),std::bit_cast<u32>(r.boundsSize.y));
        bool comma=false;for(const auto hole:data.regionHoles(i)){if(comma)std::printf(",");std::printf("%u",hole);comma=true;}
        std::printf("],\"neighbors\":[");comma=false;
        for(const auto n:data.regionNeighbors(i)){if(comma)std::printf(",");std::printf("%d",n);comma=true;}
        std::printf("]}");
    }
    std::printf("],\"topology\":[");
    for(u32 i=0;i<data.rings.size();++i){
        if(i)std::printf(",");
        const auto& r=data.rings[i];
        std::printf("{\"parent\":%d,\"hole\":%s,\"vertices\":[",r.parent==kNoGeometryIndex?-1:static_cast<i32>(r.parent),r.hole?"true":"false");
        for(u32 j=0;j<r.count;++j){if(j)std::printf(",");const auto u=data.uses[r.begin+j];const auto& e=data.edges[u.edge];std::printf("%u",u.reversed?e.second:e.first);}
        std::printf("]}");
    }
    std::printf("],\"edge_provenance\":[");
    for(u32 i=0;i<data.edges.size();++i){
        if(i)std::printf(",");
        const auto& e=data.edges[i];const auto& p=e.provenance;
        std::printf("[%u,%u,%u,%d,%d,%u]",e.first,e.second,p.edge,p.source,p.neighbor,static_cast<u32>(p.origin));
    }
    std::printf("]");
}
}
int main(){
    LoadedFile file;file.code.functions.resize(2);detail::Execution ex(file);ex.fuel=10000000;
    std::printf("{\"adjacent\":{");
    {const auto data=geometry_fixture::adjacent(ex);values(*data);std::printf(",\"bytes\":%llu,\"fuel\":%llu}",
        static_cast<unsigned long long>(ex.resources.bytes),static_cast<unsigned long long>(10000000-ex.fuel));}
    if(ex.resources.bytes!=0)return 1;
    ex.fuel=10000000;std::printf(",\"holes\":{");
    {const auto data=geometry_fixture::holes(ex);values(*data);std::printf(",\"bytes\":%llu,\"fuel\":%llu}",
        static_cast<unsigned long long>(ex.resources.bytes),static_cast<unsigned long long>(10000000-ex.fuel));}
    if(ex.resources.bytes!=0)return 2;
    ex.fuel=10000000;std::printf(",\"fractional\":{");
    {const auto data=geometry_fixture::fractional(ex);values(*data);std::printf(",\"bytes\":%llu,\"fuel\":%llu}",
        static_cast<unsigned long long>(ex.resources.bytes),static_cast<unsigned long long>(10000000-ex.fuel));}
    if(ex.resources.bytes!=0)return 3;
    std::printf(",\"released_bytes\":%llu,\"limits\":{",static_cast<unsigned long long>(ex.resources.bytes));
    using Factory=std::shared_ptr<const GeometryData>(*)(detail::Execution&);
    const std::array<Factory,3> factories{geometry_fixture::adjacent,geometry_fixture::holes,geometry_fixture::fractional};
    const std::array<const char*,3> names{"adjacent","holes","fractional"};
    const std::array<u64,3> fuel{218,395,77};
    for(std::size_t i=0;i<factories.size();++i){
        u64 low=0,high=4096;
        while(low<high){
            const auto available=(low+high)/2;
            detail::Execution limited(file);limited.fuel=100000;limited.resources.bytes=kMaxGenerationData-available;
            bool success=false;
            try{const auto data=factories[i](limited);success=true;}
            catch(const Trap& error){if(error.reason!=ErrorReason::OutOfMemory)return 4;}
            if(limited.resources.bytes!=kMaxGenerationData-available)return 5;
            if(success)high=available;else low=available+1;
        }
        for(const bool enough:{false,true}){
            detail::Execution limited(file);limited.fuel=fuel[i]-(enough?0:1);
            bool success=false;
            try{const auto data=factories[i](limited);success=true;}
            catch(const Trap& error){if(error.reason!=ErrorReason::FuelExhausted)return 6;}
            if(success!=enough || limited.resources.bytes!=0)return 7;
        }
        if(i)std::printf(",");
        std::printf("\"%s\":{\"peak_bytes\":%llu,\"fuel_boundary\":true}",names[i],static_cast<unsigned long long>(low));
    }
    std::printf("},\"handles\":{\"types\":[");
    ex.frames.emplace_back();ex.frame().stream=26;ex.fuel=100000;
    {
        const auto geometry=geometry_fixture::adjacent(ex);
        const auto part=detail::makeResource<DynamicPartData,256>(ex.resources);
        part->source=RegionSource{geometry,0};part->id=7;part->sourceId=0;
        const auto parts=detail::makeResource<DynamicPartsData,128>(ex.resources,ex.resources);
        parts->parts.push_back({part});
        const std::array<Value,5> handles{Diagram2dValue{geometry},Contours2dValue{geometry},Regions2dValue{geometry},Part2dValue{part},Parts2dValue{parts}};
        const auto bytes=ex.resources.bytes;
        for(std::size_t i=0;i<handles.size();++i){
            const auto type=valueType(handles[i]);file.code.functions[1].params={type};file.code.functions[1].results={type};
            ex.push(handles[i]);ex.callFunction(1);ex.push(ex.frame().locals[0]);ex.leaveFrame();
            const auto returned=ex.pop();
            const bool shared=std::visit([&](const auto& h){
                using T=std::decay_t<decltype(h)>;
                if constexpr(requires{h.data;}){const auto* other=std::get_if<T>(&returned);return other && h.data==other->data;}
                else return false;
            },handles[i]);
            if(!shared || ex.resources.bytes!=bytes || ex.frame().stream!=26)return 8;
            if(i)std::printf(",");
            std::printf("\"%s\"",valueTypeName(type));
        }
        std::printf("],\"bytes\":%llu,\"shared\":true,\"helper_stream\":26",static_cast<unsigned long long>(bytes));
    }
    if(ex.resources.bytes!=0)return 9;
    std::printf(",\"released_bytes\":0}}\n");
}
