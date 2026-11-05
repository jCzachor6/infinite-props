#include "support/fault_injection.hpp"
#include "support/contour_fixture.hpp"
#include "iprop/VmInternals.hpp"
#include <cstdio>
using namespace fault_injection;

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
                arm(at,mode);
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
