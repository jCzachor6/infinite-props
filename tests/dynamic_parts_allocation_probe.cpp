#include "support/geometry_fixture.hpp"
#include "iprop/DynamicParts.hpp"
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
    for(const bool mode:{false,true})for(u32 path=0;path<12;++path){
        bool success=false;
        for(std::size_t at=0;at<1024;++at){const auto baseline=live;
            {
                detail::Execution ex(file);ex.fuel=1000000000;ex.target=RenderTarget::create(16,16,{1,1},CompositionMode::SourceOver);
                const Regions2dValue source{geometry_fixture::holes(ex)};
                const auto surface=[&](bool rgba){const auto bytes=rgba?surfaceBytes(5,4):maskBytes(5,4);ex.allocateResource(bytes);
                    auto b=ex.resources.makeSurface(bytes);b->raster.width=5;b->raster.height=4;
                    b->format=rgba?SurfaceBuffer::Format::RGBA8:SurfaceBuffer::Format::A8;
                    if(rgba)b->raster.pixels.assign(20,Color{17,23,29,128});else b->coverage.assign(20,127);return b;};
                const LayerValue image{surface(true),{*D4Transform::fromKind(3),{-2,1}}};
                const MaskValue coverage{surface(false),{}};
                const auto original=path==6||path==8||path==11?detail::createPart(ex,image):detail::createPart(ex,source,0);
                const auto bytes=ex.resources.bytes;const auto count=ex.dynamicPartCreations;const auto center=original.data->center;
                Part2dValue p;Parts2dValue ps;MaskValue mask;
                persistent=mode;allocations=0;failAt=at;armed=true;
                try{
                    if(path==0)p=detail::createPart(ex,source,0);
                    if(path==1)ps=detail::partsFromRegions(ex,source);
                    if(path==2)p=detail::createPart(ex,image);
                    if(path==3)p=detail::createPart(ex,coverage);
                    if(path==4)p=detail::fillPart(ex,original,{31,37,41,128});
                    if(path==5)p=detail::texturePart(ex,original,image);
                    if(path==6)p=detail::placePart(ex,original,{-1.5f,3.5f},90,1,0);
                    if(path==7||path==8)mask=detail::maskPart(ex,original);
                    if(path==9){p=detail::texturePart(ex,original,image);detail::drawPart(ex,p);}
                    if(path==10){p=detail::createPart(ex,source,0);p=detail::texturePart(ex,p,image);
                        p=detail::placePart(ex,p,{-0.5f,4.5f},270,0,1);mask=detail::maskPart(ex,p);detail::drawPart(ex,p);}
                    if(path==11)detail::drawPart(ex,original);
                    armed=false;success=true;
                }catch(const std::bad_alloc&){armed=false;++faults;}
                catch(...){armed=false;return 1;}
                if(!success&&path<4&&ex.dynamicPartCreations!=count)return 2;
                if(path>=4&&path!=10&&ex.dynamicPartCreations!=count)return 3;
                if(original.data->fill||original.data->texture||original.data->transform.kind()!=0||original.data->center!=center)return 4;
                p={};ps={};mask={};if(ex.resources.bytes!=bytes)return 5;
            }
            if(live!=baseline)return 6;
            if(success)break;
        }if(!success)return 7;
    }
    std::printf("Dynamic part allocations: PASS %zu injected transient/persistent failures across 12 creation, bulk, immutable view, mask, render and combined paths; atomic creation IDs and no heap/quota leaks\n",faults);
}
