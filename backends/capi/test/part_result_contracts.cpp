#include "iprop.h"
#include "../Handles.hpp"
#include "check.h"

#include <array>
#include <new>
#include <vector>

namespace {
bool armed=false;
std::size_t allocations=0, checks=0;
}
void* operator new(std::size_t size) {
    if(armed){++allocations;throw std::bad_alloc{};}
    if(void* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* value) noexcept{std::free(value);}
void operator delete[](void* value) noexcept{std::free(value);}
void operator delete(void* value,std::size_t) noexcept{std::free(value);}
void operator delete[](void* value,std::size_t) noexcept{std::free(value);}

namespace {
struct Field{std::size_t offset,size;};
#define F(T,f) Field{offsetof(T,f),sizeof(T::f)}
const std::array slotFields{F(iprop_output_slot_ex,output_index),F(iprop_output_slot_ex,owner),F(iprop_output_slot_ex,kind),
    F(iprop_output_slot_ex,is_list),F(iprop_output_slot_ex,element_count),
    Field{offsetof(iprop_output_slot_ex,name)+offsetof(iprop_utf8_view,data),sizeof(const char*)},
    Field{offsetof(iprop_output_slot_ex,name)+offsetof(iprop_utf8_view,byte_count),sizeof(uint32_t)}};
const std::array partFields{F(iprop_part_snapshot_ex,output_index),F(iprop_part_snapshot_ex,element_index),
    F(iprop_part_snapshot_ex,id),F(iprop_part_snapshot_ex,source_id),F(iprop_part_snapshot_ex,shape_kind),
    F(iprop_part_snapshot_ex,appearance_kind),F(iprop_part_snapshot_ex,shape_resource),F(iprop_part_snapshot_ex,region_index),
    F(iprop_part_snapshot_ex,texture_resource),F(iprop_part_snapshot_ex,tint),F(iprop_part_snapshot_ex,d4),
    F(iprop_part_snapshot_ex,translation_x),F(iprop_part_snapshot_ex,translation_y),F(iprop_part_snapshot_ex,source_scale),
    F(iprop_part_snapshot_ex,target_scale),F(iprop_part_snapshot_ex,source_anchor),F(iprop_part_snapshot_ex,center),
    F(iprop_part_snapshot_ex,bounds_origin),F(iprop_part_snapshot_ex,bounds_size),F(iprop_part_snapshot_ex,source_d4),
    F(iprop_part_snapshot_ex,source_origin_x),F(iprop_part_snapshot_ex,source_origin_y),F(iprop_part_snapshot_ex,texture_d4),
    F(iprop_part_snapshot_ex,texture_origin_x),F(iprop_part_snapshot_ex,texture_origin_y)};
const std::array resourceFields{F(iprop_resource_info_ex,resource_id),F(iprop_resource_info_ex,kind),
    F(iprop_resource_info_ex,pixel_format),F(iprop_resource_info_ex,width),F(iprop_resource_info_ex,height),
    F(iprop_resource_info_ex,row_stride),F(iprop_resource_info_ex,byte_count),F(iprop_resource_info_ex,data),
    F(iprop_resource_info_ex,scale),F(iprop_resource_info_ex,vertex_count),F(iprop_resource_info_ex,edge_count),
    F(iprop_resource_info_ex,ring_count),F(iprop_resource_info_ex,region_count)};
const std::array ringFields{F(iprop_ring_info_ex,ring_index),F(iprop_ring_info_ex,vertex_count),
    F(iprop_ring_info_ex,parent_ring),F(iprop_ring_info_ex,is_hole)};
const std::array regionFields{F(iprop_region_info_ex,region_index),F(iprop_region_info_ex,outer_ring),
    F(iprop_region_info_ex,hole_count),F(iprop_region_info_ex,source_id),F(iprop_region_info_ex,area),
    F(iprop_region_info_ex,center),F(iprop_region_info_ex,bounds_origin),F(iprop_region_info_ex,bounds_size)};
#undef F

template<class T,class Init,class Query,std::size_t N>
int boundaries(Init init,Query query,const std::array<Field,N>& fields) {
    T full;std::memset(&full,0xa5,sizeof(full));CHECK(init(&full,sizeof(full))==IPROP_EX_OK);
    auto* noError=static_cast<iprop_error_ex*>(nullptr);
    CHECK(query(&full,noError)==IPROP_EX_OK);
    for(uint32_t capacity=0;capacity<sizeof(T)+17;++capacity) {
        // Offset by one deliberately: descriptors use byte-safe field access.
        std::array<uint8_t,sizeof(T)+32> data,expected;
        data.fill(0xa5);expected=data;auto* out=reinterpret_cast<T*>(data.data()+1);
        const auto apply=[&](Field f,const void* value){if(f.offset+f.size<=capacity)std::memcpy(expected.data()+1+f.offset,value,f.size);};
        armed=true;const auto initialized=init(out,capacity);armed=false;
        if(capacity<4)CHECK(initialized==IPROP_EX_E_ARGUMENT);
        else {
            CHECK(initialized==IPROP_EX_OK);apply({0,4},&capacity);
            const T zero{};apply({offsetof(T,flags),4},&zero.flags);
            for(const auto f:fields)apply(f,reinterpret_cast<const uint8_t*>(&zero)+f.offset);
        }
        CHECK(data==expected);++checks;
        std::memcpy(data.data()+1,&capacity,sizeof(capacity));
        const auto before=data;expected=before;
        if(capacity>=4)for(const auto f:fields)apply(f,reinterpret_cast<const uint8_t*>(&full)+f.offset);
        armed=true;const auto queried=query(out,noError);armed=false;
        CHECK(queried==(capacity<4?IPROP_EX_E_ARGUMENT:IPROP_EX_OK));CHECK(data==expected);++checks;
        if(capacity>=8) {
            const uint32_t flags=UINT32_MAX;std::memcpy(data.data()+1+offsetof(T,flags),&flags,4);expected=data;
            armed=true;CHECK(query(out,noError)==IPROP_EX_E_ARGUMENT);armed=false;CHECK(data==expected);++checks;
        }
    }
    CHECK(init(nullptr,4)==IPROP_EX_E_ARGUMENT);CHECK(query(static_cast<T*>(nullptr),noError)==IPROP_EX_E_ARGUMENT);
    std::array<uint8_t,(sizeof(T)>sizeof(iprop_error_ex)?sizeof(T):sizeof(iprop_error_ex))+16> alias{};
    auto* out=reinterpret_cast<T*>(alias.data());CHECK(init(out,sizeof(T))==IPROP_EX_OK);
    const auto before=alias;
    armed=true;CHECK(query(out,reinterpret_cast<iprop_error_ex*>(out))==IPROP_EX_E_ARGUMENT);armed=false;
    CHECK(alias==before);++checks;
    return 0;
}

template<class T,class Query> int failure(T& out,iprop_status_ex expected,Query query) {
    const auto before=out;iprop_error_ex error{};
    CHECK(iprop_error_ex_init(&error,sizeof(error))==IPROP_EX_OK);
    armed=true;const auto status=query(&out,&error);armed=false;
    CHECK(status==expected && error.status==expected);CHECK(std::memcmp(&before,&out,sizeof(out))==0);++checks;
    return 0;
}

int load(const char* path,iprop_program** program,iprop_result** result) {
    uint8_t* bytes;std::size_t size;CHECK(read_fixture(path,&bytes,&size)==0);
    CHECK(iprop_program_load_ex(bytes,size,program,nullptr)==IPROP_EX_OK);std::free(bytes);
    iprop_generate_options_ex options{};CHECK(iprop_generate_options_ex_init(&options,sizeof(options))==IPROP_EX_OK);
    auto* const previous=reinterpret_cast<iprop_result*>(std::uintptr_t{1});auto* candidate=previous;
    armed=true;const auto status=iprop_generate_ex(*program,&options,nullptr,&candidate,nullptr);armed=false;
    CHECK(status==IPROP_EX_E_UNSUPPORTED && candidate==previous);++checks;
    options.flags=IPROP_GENERATE_PART_OUTPUTS;
    CHECK(iprop_generate_ex(*program,&options,nullptr,result,nullptr)==IPROP_EX_OK);
    return 0;
}
} // namespace

int main(int argc,char** argv) {
    CHECK(argc==4);iprop_program* program=nullptr;iprop_result* result=nullptr;
    CHECK(load(argv[1],&program,&result)==0);
    const auto slotQuery=[&](auto* out,auto* error){return iprop_result_output_slot(result,0,out,error);};
    const auto partQuery=[&](auto* out,auto* error){return iprop_result_output_part(result,0,1,out,error);};
    CHECK(boundaries<iprop_output_slot_ex>(iprop_output_slot_ex_init,slotQuery,slotFields)==0);
    CHECK(boundaries<iprop_part_snapshot_ex>(iprop_part_snapshot_ex_init,partQuery,partFields)==0);
    CHECK(boundaries<iprop_resource_info_ex>(iprop_resource_info_ex_init,
        [&](auto* out,auto* error){return iprop_result_resource_get(result,2,out,error);},resourceFields)==0);
    CHECK(boundaries<iprop_ring_info_ex>(iprop_ring_info_ex_init,
        [&](auto* out,auto* error){return iprop_result_resource_ring(result,1,1,out,error);},ringFields)==0);
    CHECK(boundaries<iprop_region_info_ex>(iprop_region_info_ex_init,
        [&](auto* out,auto* error){return iprop_result_resource_region(result,1,0,out,error);},regionFields)==0);
    iprop_part_snapshot_ex part{};CHECK(iprop_part_snapshot_ex_init(&part,sizeof(part))==IPROP_EX_OK);
    for(const uint32_t bad:{3u,4u,UINT32_MAX})CHECK(failure(part,IPROP_EX_E_ARGUMENT,
        [&](auto* out,auto* error){return iprop_result_output_part(result,0,bad,out,error);})==0);
    for(const uint32_t bad:{2u,3u,4u,UINT32_MAX})CHECK(failure(part,IPROP_EX_E_ARGUMENT,
        [&](auto* out,auto* error){return iprop_result_output_part(result,bad,0,out,error);})==0);
    CHECK(failure(part,IPROP_EX_E_ARGUMENT,[&](auto* out,auto* error){return iprop_result_output_part(nullptr,0,0,out,error);})==0);
    iprop_resource_info_ex resource{};CHECK(iprop_resource_info_ex_init(&resource,sizeof(resource))==IPROP_EX_OK);
    for(const uint32_t bad:{0u,4u,UINT32_MAX})CHECK(failure(resource,IPROP_EX_E_ARGUMENT,
        [&](auto* out,auto* error){return iprop_result_resource_get(result,bad,out,error);})==0);
    iprop_ring_info_ex ring{};CHECK(iprop_ring_info_ex_init(&ring,sizeof(ring))==IPROP_EX_OK);
    CHECK(failure(ring,IPROP_EX_E_ARGUMENT,[&](auto* out,auto* error){return iprop_result_resource_ring(result,2,0,out,error);})==0);
    CHECK(failure(ring,IPROP_EX_E_ARGUMENT,[&](auto* out,auto* error){return iprop_result_resource_ring(result,1,2,out,error);})==0);
    iprop_region_info_ex region{};CHECK(iprop_region_info_ex_init(&region,sizeof(region))==IPROP_EX_OK);
    CHECK(failure(region,IPROP_EX_E_ARGUMENT,[&](auto* out,auto* error){return iprop_result_resource_region(result,1,1,out,error);})==0);
    uint32_t hole=123;
    CHECK(failure(hole,IPROP_EX_E_ARGUMENT,[&](auto* out,auto* error){return iprop_result_resource_region_hole(result,1,0,1,out,error);})==0);

    CHECK(iprop_result_resource_get(result,2,&resource,nullptr)==IPROP_EX_OK);
    std::array<uint8_t,272> copy{};const auto bytes=resource.byte_count;CHECK(bytes==256);
    for(uint64_t capacity=0;capacity<=bytes+16;++capacity) {
        copy.fill(0xa5);auto expected=copy;
        if(capacity>=bytes)std::memcpy(expected.data(),resource.data,static_cast<std::size_t>(bytes));
        armed=true;const auto status=iprop_result_resource_copy_pixels(result,2,copy.data(),capacity,nullptr);armed=false;
        CHECK(status==(capacity<bytes?IPROP_EX_E_ARGUMENT:IPROP_EX_OK));CHECK(copy==expected);++checks;
    }
    const auto pixelBefore=std::vector<uint8_t>(resource.data,resource.data+bytes);
    CHECK(iprop_result_resource_copy_pixels(result,2,const_cast<uint8_t*>(resource.data),bytes,nullptr)==IPROP_EX_E_ARGUMENT);
    CHECK(std::memcmp(resource.data,pixelBefore.data(),static_cast<std::size_t>(bytes))==0);
    iprop_resource_info_ex a8{};CHECK(iprop_resource_info_ex_init(&a8,sizeof(a8))==IPROP_EX_OK);
    CHECK(iprop_result_resource_get(result,3,&a8,nullptr)==IPROP_EX_OK && a8.byte_count==64);
    copy.fill(0xa5);const auto a8Before=copy;
    CHECK(iprop_result_resource_copy_pixels(result,3,copy.data(),63,nullptr)==IPROP_EX_E_ARGUMENT && copy==a8Before);
    CHECK(iprop_result_resource_copy_pixels(result,3,copy.data(),64,nullptr)==IPROP_EX_OK);
    CHECK(std::memcmp(copy.data(),a8.data,64)==0 && copy[64]==0xa5);
    iprop_point_value oldPoint{17,23};
    CHECK(failure(oldPoint,IPROP_EX_E_UNSUPPORTED,[&](auto* out,auto* error){
        return iprop_result_output_point(result,2,0,out,sizeof(*out),error);})==0);
    CHECK(iprop_result_resource_copy_pixels(result,2,nullptr,bytes,nullptr)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_resource_copy_pixels(result,1,copy.data(),copy.size(),nullptr)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_resource_copy_pixels(result,2,copy.data(),bytes-1,nullptr)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_resource_ring(result,1,1,&ring,nullptr)==IPROP_EX_OK);
    std::vector<uint8_t> vertices(std::size_t{ring.vertex_count}*sizeof(iprop_point_value)+17,0xa5),complete=vertices;
    const auto vertexBytes=uint64_t{ring.vertex_count}*sizeof(iprop_point_value);
    CHECK(iprop_result_resource_ring_copy_vertices(result,1,1,reinterpret_cast<iprop_point_value*>(complete.data()+1),vertexBytes,nullptr)==IPROP_EX_OK);
    for(uint64_t capacity=0;capacity<=vertexBytes+8;++capacity) {
        std::fill(vertices.begin(),vertices.end(),0xa5);const auto before=vertices;
        armed=true;const auto status=iprop_result_resource_ring_copy_vertices(result,1,1,
            reinterpret_cast<iprop_point_value*>(vertices.data()+1),capacity,nullptr);armed=false;
        CHECK(status==(capacity<vertexBytes?IPROP_EX_E_ARGUMENT:IPROP_EX_OK));CHECK(vertices==(capacity<vertexBytes?before:complete));++checks;
    }
    const auto& geometry=std::get<iprop::RegionSource>(result->partSlots[0].data->records[0].part.data->source).geometry;
    CHECK(iprop_result_resource_ring_copy_vertices(result,1,1,
        reinterpret_cast<iprop_point_value*>(const_cast<iprop::Vec2*>(geometry->points.data())),vertexBytes,nullptr)==IPROP_EX_E_ARGUMENT);
    iprop_error_ex error{};CHECK(iprop_error_ex_init(&error,sizeof(error))==IPROP_EX_OK);
    error.message_buffer=reinterpret_cast<char*>(copy.data());error.message_capacity=copy.size();const auto copyBefore=copy;
    CHECK(iprop_result_resource_copy_pixels(result,2,copy.data(),bytes,&error)==IPROP_EX_E_ARGUMENT && copy==copyBefore);
    error.message_buffer=reinterpret_cast<char*>(const_cast<uint8_t*>(resource.data));error.message_capacity=static_cast<uint32_t>(bytes);
    CHECK(iprop_result_resource_copy_pixels(result,2,copy.data(),bytes,&error)==IPROP_EX_E_ARGUMENT);
    CHECK(std::memcmp(resource.data,pixelBefore.data(),static_cast<std::size_t>(bytes))==0);
    std::weak_ptr<const iprop::GeometryData> geometryWeak=geometry;
    std::weak_ptr<const iprop::SurfaceBuffer> rgbaWeak=std::get<iprop::LayerValue>(result->partSlots[1].data->records[0].part.data->source).buffer;
    std::weak_ptr<const iprop::PartOutputData> tableWeak=result->partSlots[0].data;
    const auto* originalPixels=std::get<iprop::LayerValue>(result->partSlots[1].data->records[0].part.data->source).buffer->raster.pixels.data();
    CHECK(resource.data==reinterpret_cast<const uint8_t*>(originalPixels));
    iprop_program_free(program);CHECK(!geometryWeak.expired() && !rgbaWeak.expired());
    iprop_result_free(result);CHECK(geometryWeak.expired() && rgbaWeak.expired() && tableWeak.expired());

    CHECK(load(argv[2],&program,&result)==0);uint32_t count=0;
    armed=true;CHECK(iprop_result_resource_count(result,&count,nullptr)==IPROP_EX_OK && count==512);armed=false;
    for(uint32_t i=0;i<256;++i) {
        armed=true;CHECK(iprop_result_output_part(result,0,i,&part,nullptr)==IPROP_EX_OK);armed=false;
        CHECK(part.id==static_cast<int32_t>(i) && part.element_index==i);
        CHECK(part.shape_resource==2*i+1 && part.texture_resource==2*i+2);
        ++checks;
    }
    CHECK(iprop_result_resource_get(result,512,&resource,nullptr)==IPROP_EX_OK && resource.data[0]==18);
    CHECK(failure(resource,IPROP_EX_E_ARGUMENT,[&](auto* out,auto* error){return iprop_result_resource_get(result,513,out,error);})==0);
    iprop_result_free(result);iprop_program_free(program);
    CHECK(load(argv[3],&program,&result)==0);
    CHECK(iprop_result_resource_count(result,&count,nullptr)==IPROP_EX_OK && count==0);
    CHECK(iprop_result_check_legacy(result,nullptr)==IPROP_EX_E_UNSUPPORTED);
    CHECK(failure(part,IPROP_EX_E_ARGUMENT,[&](auto* out,auto* error){return iprop_result_output_part(result,0,0,out,error);})==0);
    iprop_result_free(result);iprop_program_free(program);
    CHECK(allocations==0);
    std::printf("Part result contracts: PASS (%zu boundary/error/resource checks, zero query allocations)\n",checks);
    return 0;
}
