#include "support/fault_injection.hpp"
#include "support/geometry_fixture.hpp"
#include <cstdio>
#include <limits>
using namespace fault_injection;

int main(){
    using namespace iprop;
    LoadedFile file;
    std::size_t faults=0;
    for(const bool mode:{false,true})for(const int fixture:{0,1,2,3}){
        bool success=false;
        for(std::size_t at=0;at<128;++at){
            const auto baseline=live;
            {
                detail::Execution ex(file);ex.fuel=10000000;
                Value output=i32{0};
                arm(at,mode);
                try{
                    auto geometry=fixture==0?geometry_fixture::adjacent(ex):geometry_fixture::holes(ex);
                    if(fixture==2){
                        detail::GeometryBuilder b(ex,GeometryOrigin::Offset,geometry);
                        const auto ring=geometry_fixture::rectangle(b,{1,1},{2,2});b.region(ring,9);
                        geometry=std::move(b).finish();
                    }
                    if(fixture==3){
                        auto p=detail::makeResource<DynamicPartData,256>(ex.resources);
                        p->source=RegionSource{geometry,0};p->id=7;
                        auto ps=detail::makeResource<DynamicPartsData,128>(ex.resources,ex.resources);
                        for(int i=0;i<16;++i)ps->parts.push_back({p});
                        output=Parts2dValue{ps};
                    }else output=Regions2dValue{geometry};
                    armed=false;success=true;
                }catch(const std::bad_alloc&){armed=false;++faults;}
                catch(...){armed=false;return 1;}
                if(!success && (valueType(output)!=ValueType::I32 || ex.resources.bytes!=0))return 2;
                output=i32{0};
                if(ex.resources.bytes!=0)return 3;
            }
            if(live!=baseline)return 4;
            if(success)break;
        }
        if(!success)return 5;
    }
    {
        detail::Execution ex(file);ex.fuel=1000000;const auto data=geometry_fixture::holes(ex);
        const auto bytes=ex.resources.bytes,fuel=ex.fuel;
        allocations=0;failAt=0;persistent=true;armed=true;
        double sum=0;
        try{for(int i=0;i<10000;++i){sum+=data->regions[0].area+data->regionHoles(0)[0];(void)data->regionNeighbors(0);}}
        catch(...){armed=false;return 6;}
        armed=false;
        if(allocations!=0 || sum!=770000 || ex.resources.bytes!=bytes || ex.fuel!=fuel)return 7;
    }
    std::printf("geometry allocations: PASS %zu injected failures, four creation paths, transient/persistent OOM, zero live-allocation or quota leaks\n",faults);
}
