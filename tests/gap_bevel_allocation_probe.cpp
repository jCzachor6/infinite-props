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
        for(std::size_t at=0;at<2048;++at){const auto baseline=live;
            {
                detail::Execution ex(file);ex.fuel=1000000000;ex.target=RenderTarget::create(1,1,{2,3});
                const auto source=path==5?geometry_fixture::fractional(ex):geometry_fixture::holes(ex);
                const auto bytes=ex.resources.bytes;std::shared_ptr<const GeometryData> result;LayerValue layer;
                persistent=mode;allocations=0;failAt=at;armed=true;
                try{
                    if(path==0)result=detail::gapRegions(ex,source,0.5f,1,7);
                    if(path==1)result=detail::gapRegions(ex,detail::gapRegions(ex,source,0.5f,1,7),0.5f,1,7);
                    if(path==2||path==5)layer=detail::bevelRegions(ex,*source,2,{-1,-1},{255,20,40,128},{10,30,50,64},0.75f);
                    if(path==3)layer=detail::bevelRegions(ex,*source,2,{-1,-1},{},{},0);
                    if(path==4)layer=detail::bevelRegions(ex,*detail::gapRegions(ex,source,0.5f,1,7),2,{-1,-1});
                    armed=false;success=true;
                }catch(const std::bad_alloc&){armed=false;++faults;}
                catch(...){armed=false;return 1;}
                if(!success&&(result||layer.buffer||ex.resources.bytes!=bytes))return 2;
                result.reset();layer={};if(ex.resources.bytes!=bytes)return 3;
            }
            if(live!=baseline)return 4;if(success)break;
        }if(!success)return 5;
    }
    std::printf("Gap/bevel allocations: PASS %zu injected transient/persistent failures across variable and repeated gap, bevel, zero-strength, combined pipeline and fractional off-target bounds; no heap/quota leaks\n",faults);
}
