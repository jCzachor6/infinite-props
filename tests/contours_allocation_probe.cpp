#include "support/contour_fixture.hpp"
#include "iprop/VmInternals.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>

namespace { bool armed=false,persistent=false;std::size_t allocations=0,failAt=0,live=0; }
void* operator new(std::size_t size) {
    if(armed && allocations++>=failAt){if(!persistent)armed=false;throw std::bad_alloc{};}
    if(void* p=std::malloc(size?size:1)){++live;return p;}throw std::bad_alloc{};
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {if(p){--live;std::free(p);}}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}

int main() {
    using namespace iprop;
    LoadedFile file; std::size_t faults=0;
    for(const bool mode:{false,true})for(const bool simplify:{false,true}) {
        bool success=false;
        for(std::size_t at=0;at<512;++at) {
            const auto baseline=live;
            {
                detail::Execution ex(file);ex.fuel=100000000;
                std::array<u8,81> pixels{};
                for(u32 y=0;y<9;++y)for(u32 x=0;x<9;++x)
                    pixels[y*9+x]=(x==0||y==0||x==8||y==8||(x>=3&&x<=5&&y>=3&&y<=5))?255:0;
                const auto mask=test_support::contourMask(ex,9,9,pixels);
                const auto sourceBytes=ex.resources.bytes;
                std::shared_ptr<const GeometryData> data;
                persistent=mode;allocations=0;failAt=at;armed=true;
                try { data=detail::maskContours(ex,mask,{0.5f,simplify?1000.f:0.f,0,0});armed=false;success=true; }
                catch(const std::bad_alloc&) {armed=false;++faults;}
                catch(...) {armed=false;return 1;}
                if(!success && (data || ex.resources.bytes!=sourceBytes))return 2;
                data.reset();if(ex.resources.bytes!=sourceBytes)return 3;
            }
            if(live!=baseline)return 4;
            if(success)break;
        }
        if(!success)return 5;
    }
    std::printf("Contours allocations: PASS %zu injected failures, nested holes/islands, simplify 0/1000, transient/persistent OOM, zero heap/quota leaks\n",faults);
}
