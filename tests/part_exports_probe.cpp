// Test-only full result observations. This is not an installed CLI, binding
// or persistent generation-result format. Native additionally checks C API.
#include "iprop/VM.hpp"
#include "../backends/capi/iprop.h"
#include "../tools/json.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>

using namespace iprop;
using iprop::tools::Json;
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
Json bits(Vec2 p) {return Json::array({std::bit_cast<u32>(p.x),std::bit_cast<u32>(p.y)});}
Json bits(iprop_point_value p) {return bits(Vec2{p.x,p.y});}
Json bytesJson(const u8* data,std::size_t count) {
    auto result=Json::array();for(std::size_t i=0;i<count;++i)result.push_back(data[i]);return result;
}
struct Resource {
    const GeometryData* geometry=nullptr;const SurfaceBuffer* raster=nullptr;
    bool operator==(const Resource&) const=default;
};
struct Resources {
    std::vector<Resource> stores;
    u32 id(Resource source) const {
        const auto at=std::find(stores.begin(),stores.end(),source);
        require(at!=stores.end(),"unregistered C++ resource");return static_cast<u32>(at-stores.begin())+1;
    }
    explicit Resources(const GenerationResult& result) {
        const auto add=[&](Resource source){if(std::find(stores.begin(),stores.end(),source)==stores.end())stores.push_back(source);};
        for(const auto& slot:result.outputs)for(const auto& record:slot.partSnapshots()) {
            std::visit([&](const auto& source){
                if constexpr(std::is_same_v<std::decay_t<decltype(source)>,RegionSource>)add({source.geometry.get(),nullptr});
                else add({nullptr,source.buffer.get()});
            },record.part.data->source);
            if(record.part.data->texture)add({nullptr,record.part.data->texture->buffer.get()});
        }
    }
};
iprop_point_value point(Vec2 value){return {value.x,value.y};}
iprop_part_snapshot_ex partView(const OutputPart& record,u32 slot,const Resources& resources) {
    iprop_part_snapshot_ex v{};const auto& p=*record.part.data;
    v.output_index=slot;v.element_index=record.index;v.id=p.id;v.source_id=p.sourceId;v.region_index=IPROP_NO_REGION;
    std::visit([&](const auto& source){
        if constexpr(std::is_same_v<std::decay_t<decltype(source)>,RegionSource>) {
            v.shape_kind=IPROP_PART_SHAPE_REGION;v.shape_resource=resources.id({source.geometry.get(),nullptr});v.region_index=source.region;
        } else {
            v.shape_kind=std::is_same_v<std::decay_t<decltype(source)>,MaskValue>?IPROP_PART_SHAPE_MASK:IPROP_PART_SHAPE_LAYER;
            v.shape_resource=resources.id({nullptr,source.buffer.get()});v.source_d4=source.mapping.transform.kind();
            v.source_origin_x=source.mapping.origin.x;v.source_origin_y=source.mapping.origin.y;
        }
    },p.source);
    if(p.fill){v.appearance_kind=IPROP_PART_APPEARANCE_FILL;v.tint={p.fill->r,p.fill->g,p.fill->b,p.fill->a};}
    else if(p.texture){v.appearance_kind=IPROP_PART_APPEARANCE_TEXTURE;v.texture_resource=resources.id({nullptr,p.texture->buffer.get()});
        v.texture_d4=p.texture->mapping.transform.kind();v.texture_origin_x=p.texture->mapping.origin.x;v.texture_origin_y=p.texture->mapping.origin.y;}
    else if(std::holds_alternative<LayerValue>(p.source))v.appearance_kind=IPROP_PART_APPEARANCE_SOURCE_RGBA;
    v.d4=record.transform.kind();v.translation_x=record.translation.x;v.translation_y=record.translation.y;
    v.source_scale=point(record.sourceScale);v.target_scale=point(record.targetScale);
    v.source_anchor={static_cast<float>(p.original.centerX/p.original.scale.x),static_cast<float>(p.original.centerY/p.original.scale.y)};
    v.center=point(record.center);v.bounds_origin=point(record.boundsOrigin);v.bounds_size=point(record.boundsSize);
    return v;
}
Json partJson(const iprop_part_snapshot_ex& v) {
    return Json::object({{"slot",v.output_index},{"entry",v.element_index},{"id",v.id},{"source_id",v.source_id},
        {"shape",Json::array({v.shape_kind,v.shape_resource,v.region_index})},
        {"appearance",Json::array({v.appearance_kind,v.texture_resource,v.tint.r,v.tint.g,v.tint.b,v.tint.a})},
        {"source_mapping",Json::array({v.source_d4,v.source_origin_x,v.source_origin_y})},
        {"texture_mapping",Json::array({v.texture_d4,v.texture_origin_x,v.texture_origin_y})},
        {"transform",Json::array({v.d4,v.translation_x,v.translation_y})},
        {"source_scale",bits(v.source_scale)},{"target_scale",bits(v.target_scale)},{"anchor",bits(v.source_anchor)},
        {"center",bits(v.center)},{"bounds_origin",bits(v.bounds_origin)},{"bounds_size",bits(v.bounds_size)}});
}
Json values(const NamedOutput& slot) {
    auto result=Json::array();
    for(const auto p:slot.points)result.push_back(bits(Vec2{p.x,p.y}));
    for(const auto r:slot.rects)result.push_back(Json::array({std::bit_cast<u32>(r.x),std::bit_cast<u32>(r.y),std::bit_cast<u32>(r.w),std::bit_cast<u32>(r.h)}));
    for(const auto c:slot.circles)result.push_back(Json::array({std::bit_cast<u32>(c.x),std::bit_cast<u32>(c.y),std::bit_cast<u32>(c.r)}));
    return result;
}
Json resourceJson(const Resource& source) {
    if(source.raster) {
        const auto& r=*source.raster;const bool a8=r.format==SurfaceBuffer::Format::A8;
        const auto* bytes=a8?r.coverage.data():reinterpret_cast<const u8*>(r.raster.pixels.data());
        return Json::object({{"kind","raster"},{"format",a8?IPROP_PIXEL_FORMAT_A8:IPROP_PIXEL_FORMAT_RGBA8},
            {"size",Json::array({r.raster.width,r.raster.height})},{"scale",bits(Vec2{r.raster.scaleX,r.raster.scaleY})},
            {"stride",u64{r.raster.width}*(a8?1u:4u)},{"bytes",bytesJson(bytes,std::size_t{r.raster.width}*r.raster.height*(a8?1u:4u))}});
    }
    const auto& g=*source.geometry;auto rings=Json::array(),regions=Json::array();
    for(const auto& ring:g.rings) {
        auto vertices=Json::array();for(u32 k=0;k<ring.count;++k)vertices.push_back(bits(g.start(g.uses[ring.begin+k])));
        rings.push_back(Json::object({{"parent",ring.parent},{"hole",ring.hole},{"vertices",std::move(vertices)}}));
    }
    for(u32 i=0;i<g.regions.size();++i) {
        const auto& r=g.regions[i];auto holes=Json::array();for(const auto h:g.regionHoles(i))holes.push_back(h);
        regions.push_back(Json::object({{"outer",r.outer},{"holes",std::move(holes)},{"source_id",r.sourceId},
            {"area",std::bit_cast<u32>(r.area)},{"center",bits(r.center)},{"bounds_origin",bits(r.boundsOrigin)},{"bounds_size",bits(r.boundsSize)}}));
    }
    return Json::object({{"kind","geometry"},{"vertex_count",g.points.size()},{"edge_count",g.edges.size()},
        {"rings",std::move(rings)},{"regions",std::move(regions)}});
}

#ifndef __EMSCRIPTEN__
void capi(const std::vector<u8>& bytes,const GenerationInputs& inputs,const GenerationResult& expected,
          const std::vector<u32>& owners,const Resources& resources,u64 budget,u64 remaining) {
    iprop_program* rawProgram=nullptr;require(iprop_program_load_ex(bytes.data(),bytes.size(),&rawProgram,nullptr)==IPROP_EX_OK,"C API load");
    std::unique_ptr<iprop_program,decltype(&iprop_program_free)> program(rawProgram,iprop_program_free);
    u32 count=0;require(iprop_program_output_count(program.get(),&count,nullptr)==IPROP_EX_OK&&count==expected.outputs.size(),"C API declaration count");
    for(u32 i=0;i<count;++i) {
        iprop_output_info_ex info{};iprop_output_info_ex_init(&info,sizeof(info));
        require(iprop_program_output_get(program.get(),i,&info,nullptr)==IPROP_EX_OK,"C API declaration");
        const auto& slot=expected.outputs[i];
        require(info.owner==owners[i]&&info.kind==static_cast<u32>(slot.kind)&&info.is_list==u32{slot.isList},"C API declaration tags/owner");
        require(std::string_view(info.name.data,info.name.byte_count)==slot.name,"C API declaration name");
    }
    iprop_generate_options_ex options{};iprop_generate_options_ex_init(&options,sizeof(options));
    options.seed=inputs.seed;options.size_x=inputs.sizeX;options.size_y=inputs.sizeY;
    iprop_result* rawResult=nullptr;require(iprop_generate_ex(program.get(),&options,nullptr,&rawResult,nullptr)==IPROP_EX_OK,"C API generate");
    std::unique_ptr<iprop_result,decltype(&iprop_result_free)> result(rawResult,iprop_result_free);program.reset();
    iprop_image_view_ex image{};iprop_image_view_ex_init(&image,sizeof(image));
    require(iprop_result_query_image(result.get(),&image,nullptr)==IPROP_EX_OK,"C API image");
    require(image.width==expected.image.width&&image.height==expected.image.height&&image.byte_count==expected.image.pixels.size()*4,"C API image size");
    require(!image.byte_count||std::memcmp(image.pixels,expected.image.pixels.data(),static_cast<std::size_t>(image.byte_count))==0,"C API complete RGBA");
    iprop_result_stats_ex stats{};iprop_result_stats_ex_init(&stats,sizeof(stats));
    require(iprop_result_query_stats(result.get(),&stats,nullptr)==IPROP_EX_OK&&stats.fuel_budget==budget&&stats.fuel_remaining==remaining,"C API fuel");
    require(iprop_result_output_count(result.get(),&count,nullptr)==IPROP_EX_OK&&count==expected.outputs.size(),"C API slot count");
    for(u32 i=0;i<count;++i) {
        const auto& slot=expected.outputs[i];iprop_output_slot_ex view{};iprop_output_slot_ex_init(&view,sizeof(view));
        require(iprop_result_output_slot(result.get(),i,&view,nullptr)==IPROP_EX_OK,"C API slot");
        require(view.output_index==i&&view.owner==owners[i]&&view.kind==static_cast<u32>(slot.kind)&&view.is_list==u32{slot.isList},"C API slot tags/owner");
        require(std::string_view(view.name.data,view.name.byte_count)==slot.name,"C API slot name");
        u32 found=UINT32_MAX;require(iprop_result_output_find(result.get(),owners[i],view.name,&found,nullptr)==IPROP_EX_OK&&found==i,"C API owner/name lookup");
        if(slot.kind==OutputKind::Part) {
            require(view.element_count==slot.partSnapshots().size(),"C API part count");
            for(u32 k=0;k<view.element_count;++k) {
                iprop_part_snapshot_ex actual{};iprop_part_snapshot_ex_init(&actual,sizeof(actual));
                require(iprop_result_output_part(result.get(),i,k,&actual,nullptr)==IPROP_EX_OK,"C API part");
                require(partJson(actual).dump()==partJson(partView(slot.partSnapshots()[k],i,resources)).dump(),"C API full snapshot bits");
            }
        } else {
            iprop_result_output_view_ex old{};iprop_result_output_view_ex_init(&old,sizeof(old));
            require(iprop_result_output_get(result.get(),i,&old,nullptr)==IPROP_EX_OK,"C API old slot in mixed result");
            auto actual=Json::array();for(u32 k=0;k<old.element_count;++k) {
                auto value=Json::array();for(u32 c=0;c<old.stride_floats;++c)value.push_back(std::bit_cast<u32>(old.values[k*old.stride_floats+c]));
                actual.push_back(std::move(value));
            }
            require(actual.dump()==values(slot).dump()&&view.element_count==old.element_count,"C API old values");
        }
    }
    require(iprop_result_resource_count(result.get(),&count,nullptr)==IPROP_EX_OK&&count==resources.stores.size(),"C API resource count");
    for(u32 i=0;i<count;++i) {
        iprop_resource_info_ex view{};iprop_resource_info_ex_init(&view,sizeof(view));
        require(iprop_result_resource_get(result.get(),i+1,&view,nullptr)==IPROP_EX_OK&&view.resource_id==i+1,"C API resource");
        const auto& source=resources.stores[i];
        if(source.raster) {
            const auto& r=*source.raster;const bool a8=r.format==SurfaceBuffer::Format::A8;
            const auto* data=a8?r.coverage.data():reinterpret_cast<const u8*>(r.raster.pixels.data());
            const auto size=std::size_t{r.raster.width}*r.raster.height*(a8?1u:4u);
            require(view.kind==IPROP_RESOURCE_RASTER&&view.pixel_format==(a8?IPROP_PIXEL_FORMAT_A8:IPROP_PIXEL_FORMAT_RGBA8)&&
                view.width==r.raster.width&&view.height==r.raster.height&&view.byte_count==size&&
                view.row_stride==u64{r.raster.width}*(a8?1u:4u)&&bits(view.scale).dump()==bits(Vec2{r.raster.scaleX,r.raster.scaleY}).dump(),"C API raster descriptor");
            require(!size||std::memcmp(view.data,data,size)==0,"C API borrowed raster bytes");
            std::vector<u8> copy(size);
            require(iprop_result_resource_copy_pixels(result.get(),i+1,copy.data(),size,nullptr)==IPROP_EX_OK&&(!size||std::memcmp(copy.data(),data,size)==0),"C API copied raster bytes");
        } else {
            const auto& g=*source.geometry;
            require(view.kind==IPROP_RESOURCE_GEOMETRY&&view.vertex_count==g.points.size()&&view.edge_count==g.edges.size()&&
                view.ring_count==g.rings.size()&&view.region_count==g.regions.size(),"C API geometry descriptor");
            for(u32 r=0;r<g.rings.size();++r) {
                const auto& ring=g.rings[r];iprop_ring_info_ex actual{};iprop_ring_info_ex_init(&actual,sizeof(actual));
                require(iprop_result_resource_ring(result.get(),i+1,r,&actual,nullptr)==IPROP_EX_OK&&actual.ring_index==r&&
                    actual.vertex_count==ring.count&&actual.parent_ring==ring.parent&&actual.is_hole==u32{ring.hole},"C API ring topology");
                std::vector<iprop_point_value> copy(ring.count);
                require(iprop_result_resource_ring_copy_vertices(result.get(),i+1,r,copy.data(),copy.size()*sizeof(copy[0]),nullptr)==IPROP_EX_OK,"C API ring vertices");
                for(u32 v=0;v<ring.count;++v)require(bits(copy[v]).dump()==bits(g.start(g.uses[ring.begin+v])).dump(),"C API vertex bits");
            }
            for(u32 r=0;r<g.regions.size();++r) {
                const auto& region=g.regions[r];iprop_region_info_ex actual{};iprop_region_info_ex_init(&actual,sizeof(actual));
                require(iprop_result_resource_region(result.get(),i+1,r,&actual,nullptr)==IPROP_EX_OK&&actual.region_index==r&&
                    actual.outer_ring==region.outer&&actual.hole_count==region.holesCount&&actual.source_id==region.sourceId&&
                    std::bit_cast<u32>(actual.area)==std::bit_cast<u32>(region.area)&&bits(actual.center).dump()==bits(region.center).dump()&&
                    bits(actual.bounds_origin).dump()==bits(region.boundsOrigin).dump()&&bits(actual.bounds_size).dump()==bits(region.boundsSize).dump(),"C API region bits/topology");
                for(u32 h=0;h<region.holesCount;++h) {
                    u32 hole=UINT32_MAX;require(iprop_result_resource_region_hole(result.get(),i+1,r,h,&hole,nullptr)==IPROP_EX_OK&&
                        hole==g.holes[region.holesBegin+h],"C API hole assignment");
                }
            }
        }
    }
}
#endif
} // namespace

int main(int argc,char** argv) {
    std::vector<u8> bytes;GenerationInputs inputs;
    try {
        require(argc==5,"usage: probe program seed width height");
        const auto integer=[](const char* s){u32 n=0;const auto end=s+std::strlen(s);const auto parsed=std::from_chars(s,end,n);
            require(parsed.ec==std::errc{}&&parsed.ptr==end,"invalid integer");return n;};
        inputs.seed=integer(argv[2]);inputs.sizeX=integer(argv[3]);inputs.sizeY=integer(argv[4]);
        std::ifstream stream(argv[1],std::ios::binary);require(bool(stream),"cannot read program");
        bytes.assign(std::istreambuf_iterator<char>(stream),{});
        GenerationResult result;std::vector<u32> owners;u64 budget=0,remaining=0;
        {
            const auto file=loadFile(bytes);validate(file);
            owners.insert(owners.end(),file.meta.outputs.size(),0);
            for(u32 i=0;i<file.code.parts.size();++i)owners.insert(owners.end(),file.code.parts[i].outputs.size(),i+1);
            VM vm(file,inputs);vm.run();budget=vm.fuelBudget();remaining=vm.fuel();result=vm.takeResult();
        }
        const Resources resources(result);
#ifndef __EMSCRIPTEN__
        capi(bytes,inputs,result,owners,resources,budget,remaining);
#endif
        auto outputs=Json::array(),stores=Json::array();
        for(u32 i=0;i<result.outputs.size();++i) {
            const auto& slot=result.outputs[i];auto parts=Json::array();
            for(const auto& entry:slot.partSnapshots())parts.push_back(partJson(partView(entry,i,resources)));
            outputs.push_back(Json::object({{"name",slot.name},{"owner",owners[i]},{"kind",static_cast<u32>(slot.kind)},
                {"list",slot.isList},{"values",values(slot)},{"parts",std::move(parts)}}));
        }
        for(const auto source:resources.stores)stores.push_back(resourceJson(source));
        const auto json=Json::object({{"schema",1},{"size",Json::array({result.image.width,result.image.height})},
            {"fuel",Json::array({budget,remaining})},{"rgba",bytesJson(reinterpret_cast<const u8*>(result.image.pixels.data()),result.image.pixels.size()*4)},
            {"outputs",std::move(outputs)},{"resources",std::move(stores)}});
        std::cout<<json.dump()<<'\n';return 0;
    } catch(const FormatError& error) {
#ifndef __EMSCRIPTEN__
        // Invalid programs and failed generations must agree through checked C
        // and preserve its caller-owned handle, with no partial result.
        auto* const sentinel=reinterpret_cast<iprop_program*>(std::uintptr_t{1});auto* program=sentinel;
        iprop_error_ex failure{};iprop_error_ex_init(&failure,sizeof(failure));
        const auto status=iprop_program_load_ex(bytes.data(),bytes.size(),&program,&failure);
        if(error.stage==ErrorStage::Execute) {
            if(status!=IPROP_EX_OK){std::cerr<<"C API unexpectedly rejected valid program\n";return 5;}
            iprop_generate_options_ex options{};iprop_generate_options_ex_init(&options,sizeof(options));
            options.seed=inputs.seed;options.size_x=inputs.sizeX;options.size_y=inputs.sizeY;
            auto* const previous=reinterpret_cast<iprop_result*>(std::uintptr_t{1});auto* result=previous;
            const auto generated=iprop_generate_ex(program,&options,nullptr,&result,&failure);iprop_program_free(program);
            if(generated!=IPROP_EX_E_TRAP||failure.reason!=static_cast<u32>(error.reason)||result!=previous){std::cerr<<"C API trap mismatch\n";return 5;}
        } else if(status!=IPROP_EX_E_FORMAT||program!=sentinel||failure.reason!=static_cast<u32>(error.reason)) {
            if(status==IPROP_EX_OK)iprop_program_free(program);std::cerr<<"C API validation mismatch\n";return 5;
        }
#endif
        std::cerr<<static_cast<u32>(error.reason)<<": "<<error.what()<<'\n';return error.stage==ErrorStage::Execute?4:3;
    } catch(const std::exception& error) {std::cerr<<"probe: "<<error.what()<<'\n';return 5;}
}
