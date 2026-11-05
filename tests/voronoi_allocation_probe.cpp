#include "support/fault_injection.hpp"
#include "iprop/Voronoi.hpp"
#include "iprop/VmInternals.hpp"
#include <cstdio>
using namespace fault_injection;

int main() {
    using namespace iprop;
    LoadedFile file;std::size_t faults=0;
    for(const bool mode:{false,true})for(const bool generated:{false,true}) {
        bool success=false;
        for(std::size_t at=0;at<512;++at) {
            const auto baseline=live;
            {
                detail::Execution ex(file);ex.fuel=100000000;
                std::shared_ptr<const GeometryData> data;
                arm(at,mode);
                try {
                    if(generated){detail::VoronoiOptions o;o.count=19;o.relax=4;o.anisotropy=4;o.direction={3,4};o.rowBias=1;o.rowJitter=1;
                        data=detail::generatedVoronoi(ex,{0,0},{31,17},o);}
                    else{const std::array<Vec2,4> sites{{{0,0},{8,0},{8,8},{0,8}}};data=detail::explicitVoronoi(ex,{0,0},{8,8},sites,4);}
                    armed=false;success=true;
                } catch(const std::bad_alloc&) {armed=false;++faults;}
                catch(...) {armed=false;return 1;}
                if(!success && (data || ex.resources.bytes))return 2;
                data.reset();if(ex.resources.bytes)return 3;
            }
            if(live!=baseline)return 4;
            if(success)break;
        }
        if(!success)return 5;
    }
    {
        detail::Execution ex(file);ex.fuel=1000000;ex.frames.emplace_back();ex.frame().stack.reserve(4);
        const auto data=detail::generatedVoronoi(ex,{0,0},{8,8},{});const auto bytes=ex.resources.bytes;
        allocations=0;failAt=0;persistent=true;armed=true;
        try {
            for(int i=0;i<1000;++i){ex.push(Diagram2dValue{data});detail::stepVoronoi(ex,{Opcode::DiagramCount,0,0});ex.popI32();
                ex.push(Diagram2dValue{data});ex.push(i32{0});detail::stepVoronoi(ex,{Opcode::DiagramSite,0,0});ex.popVec2();}
        } catch(...) {armed=false;return 6;}
        armed=false;if(allocations || ex.resources.bytes!=bytes)return 7;
    }
    std::printf("Voronoi allocations: PASS %zu injected failures, explicit/generated Lloyd, transient/persistent OOM, zero heap/quota leaks; count/site allocate nothing\n",faults);
}
