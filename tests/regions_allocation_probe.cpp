#include "support/regions_fixture.hpp"
#include "support/geometry_fixture.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>

namespace{bool armed=false,persistent=false;std::size_t allocations=0,failAt=0,live=0;}
void* operator new(std::size_t n){if(armed&&allocations++>=failAt){if(!persistent)armed=false;throw std::bad_alloc{};}if(auto p=std::malloc(n?n:1)){++live;return p;}throw std::bad_alloc{};}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{if(p){--live;std::free(p);}}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t)noexcept{::operator delete(p);}

int main(){
    using namespace iprop;LoadedFile file;file.code.functions.emplace_back();std::size_t faults=0;
    for(const bool mode:{false,true})for(u32 path=0;path<6;++path){
        bool success=false;
        for(std::size_t at=0;at<1024;++at){const auto baseline=live;
            {
                detail::Execution ex(file);ex.fuel=100000000;ex.target=RenderTarget::create(1,1,{2,4});
                const auto source=path==5?geometry_fixture::adjacent(ex):geometry_fixture::holes(ex);
                const auto bytes=ex.resources.bytes;std::shared_ptr<const GeometryData> result;MaskValue mask;
                if(path>=4){ex.frames.emplace_back();ex.frames.back().stack.reserve(4);ex.push(Regions2dValue{source});ex.push(i32{path==4?1:0});}
                persistent=mode;allocations=0;failAt=at;armed=true;
                try{
                    if(path==0)result=detail::geometryRegions(ex,source,2);
                    if(path==1)result=detail::offsetRegions(ex,source,0.25f);
                    if(path==2)result=detail::offsetRegions(ex,source,-1.25f);
                    if(path==3)mask=detail::rasterRegions(ex,*source);
                    if(path>=4)detail::stepRegions(ex,{path==4?Opcode::RegionsPoints:Opcode::RegionsNeighbors,0,0});
                    armed=false;success=true;
                }catch(const std::bad_alloc&){armed=false;++faults;}
                catch(...){armed=false;return 1;}
                if(!success&&(result||mask.buffer||ex.resources.bytes!=bytes))return 2;
                result.reset();mask={};if(path<4&&ex.resources.bytes!=bytes)return 3;
            }
            if(live!=baseline)return 4;if(success)break;
        }if(!success)return 5;
    }
    // Repeated scalar reads and the short conversion allocate no topology.
    {detail::Execution ex(file);ex.fuel=100000000;const auto source=geometry_fixture::holes(ex);
    const auto bytes=ex.resources.bytes,fuel=ex.fuel;allocations=0;failAt=0;armed=true;
    double total=0;for(u32 i=0;i<10000;++i){const auto view=detail::geometryRegions(ex,source);total+=view->regions[0].area+view->regions[1].center.x;}
    armed=false;if(allocations||total!=790000||ex.resources.bytes!=bytes||ex.fuel!=fuel)return 6;}
    std::printf("Regions allocations: PASS %zu injected transient/persistent failures across filtering, both offsets, raster and two lists; no heap/quota leaks; scalar reads allocate zero\n",faults);
}
