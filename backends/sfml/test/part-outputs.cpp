// Complete production SFML adapter result, compared with independent C++/C
// test observations. No serializer is installed or added to the product API.
#include "../src/Host.hpp"
#include "../../../tools/json.hpp"
#include <bit>
#include <fstream>
#include <iostream>
#include <iterator>

using iprop::tools::Json;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Json point(iprop_point_value p) { return Json::array({std::bit_cast<std::uint32_t>(p.x), std::bit_cast<std::uint32_t>(p.y)}); }
Json bytes(const std::vector<std::uint8_t>& values) {
    auto array = Json::array(); for (const auto v : values) array.push_back(v); return array;
}
Json part(const iprop_part_snapshot_ex& p) {
    return Json::object({{"slot",p.output_index},{"entry",p.element_index},{"id",p.id},{"source_id",p.source_id},
        {"shape",Json::array({p.shape_kind,p.shape_resource,p.region_index})},
        {"appearance",Json::array({p.appearance_kind,p.texture_resource,p.tint.r,p.tint.g,p.tint.b,p.tint.a})},
        {"source_mapping",Json::array({p.source_d4,p.source_origin_x,p.source_origin_y})},
        {"texture_mapping",Json::array({p.texture_d4,p.texture_origin_x,p.texture_origin_y})},
        {"transform",Json::array({p.d4,p.translation_x,p.translation_y})},
        {"source_scale",point(p.source_scale)},{"target_scale",point(p.target_scale)},{"anchor",point(p.source_anchor)},
        {"center",point(p.center)},{"bounds_origin",point(p.bounds_origin)},{"bounds_size",point(p.bounds_size)}});
}
Json observe(const iprop::sfml_host::GeneratedResult& result) {
    auto outputs = Json::array(), resources = Json::array(); std::uint64_t elements = 0;
    for (std::size_t i = 0; i < result.outputs.size(); ++i) {
        const auto& slot = result.outputs[i]; require(slot.index == i,"slot index"); elements += slot.count;
        const auto stride = iprop::backend::outputComponents(slot.kind);
        auto values = Json::array(), parts = Json::array();
        if (slot.kind == IPROP_OUTPUT_KIND_PART) {
            require(slot.values.empty() && slot.parts.size() == slot.count,"part count");
            for (const auto& p : slot.parts) {
                require(p.shape_resource > 0 && p.shape_resource <= result.resources.size(),"shape resource ID");
                require(p.texture_resource <= result.resources.size(),"texture resource ID"); parts.push_back(part(p));
            }
        } else {
            require(stride != 0 && slot.values.size() == std::size_t{slot.count} * stride,"float count");
            for (std::size_t k = 0; k < slot.values.size(); k += stride) {
                auto value = Json::array(); for (std::uint32_t c = 0; c < stride; ++c) value.push_back(std::bit_cast<std::uint32_t>(slot.values[k+c]));
                values.push_back(std::move(value));
            }
        }
        outputs.push_back(Json::object({{"name",slot.name},{"owner",slot.owner},{"kind",slot.kind},{"list",slot.isList},
            {"values",std::move(values)},{"parts",std::move(parts)}}));
    }
    for (std::size_t i = 0; i < result.resources.size(); ++i) {
        const auto& resource = result.resources[i]; const auto& r = resource.info;
        require(r.resource_id == i + 1 && r.data == nullptr && r.byte_count == resource.pixels.size(),"owned resource storage");
        require(r.ring_count == resource.rings.size() && r.region_count == resource.regions.size(),"resource topology counts");
        if (r.kind == IPROP_RESOURCE_RASTER) {
            resources.push_back(Json::object({{"kind","raster"},{"format",r.pixel_format},{"size",Json::array({r.width,r.height})},
                {"scale",point(r.scale)},{"stride",r.row_stride},{"bytes",bytes(resource.pixels)}}));
        } else {
            require(r.kind == IPROP_RESOURCE_GEOMETRY,"resource kind"); auto rings=Json::array(),regions=Json::array();
            for (const auto& ring : resource.rings) {
                auto vertices=Json::array(); require(ring.info.vertex_count == ring.vertices.size(),"ring count");
                for (const auto p : ring.vertices) vertices.push_back(point(p));
                rings.push_back(Json::object({{"parent",ring.info.parent_ring},{"hole",ring.info.is_hole != 0},{"vertices",std::move(vertices)}}));
            }
            for (const auto& region : resource.regions) {
                auto holes=Json::array(); for (const auto h : region.holes) holes.push_back(h); const auto& v=region.info;
                require(v.hole_count == region.holes.size(),"hole count");
                regions.push_back(Json::object({{"outer",v.outer_ring},{"holes",std::move(holes)},{"source_id",v.source_id},
                    {"area",std::bit_cast<std::uint32_t>(v.area)},{"center",point(v.center)},
                    {"bounds_origin",point(v.bounds_origin)},{"bounds_size",point(v.bounds_size)}}));
            }
            resources.push_back(Json::object({{"kind","geometry"},{"vertex_count",r.vertex_count},{"edge_count",r.edge_count},
                {"rings",std::move(rings)},{"regions",std::move(regions)}}));
        }
    }
    require(elements == result.statistics.output_element_count,"statistics include typed elements");
    require(result.statistics.fuel_used == result.statistics.fuel_budget - result.statistics.fuel_remaining,"fuel statistics");
    return Json::object({{"schema",1},{"size",Json::array({result.width,result.height})},{"rgba",bytes(result.pixels)},
        {"outputs",std::move(outputs)},{"resources",std::move(resources)}});
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 6,"usage: program seed width height expected.json");
        const auto number = [](const char* text) { std::uint32_t v=0; const auto end=text+std::strlen(text);
            const auto parsed=std::from_chars(text,end,v); require(parsed.ec == std::errc{} && parsed.ptr == end,"invalid integer"); return v; };
        const auto seed=number(argv[2]), width=number(argv[3]), height=number(argv[4]);
        iprop::sfml_host::GeneratedResult owned;
        {
            using namespace iprop::sfml_host;
            Program program(loadProgram(readFile(argv[1])),iprop_program_free);
            Result result(generate(program.get(),seed,nullptr,width,height),iprop_result_free);
            owned=copyResult(result.get());
            Result later(generate(program.get(),seed ^ UINT32_MAX,nullptr,width,height),iprop_result_free);
            program.reset(); // Native result and copied result both outlive program.
        }
        std::ifstream file(argv[5],std::ios::binary); require(bool(file),"expected fixture missing");
        std::string expected(std::istreambuf_iterator<char>{file},{});
        while (!expected.empty() && (expected.back()=='\n' || expected.back()=='\r')) expected.pop_back();
        require(observe(owned).dump() == expected,"complete owned SFML result differs from C++/C fixture");
        std::cout << "SFML complete typed result and independent lifetime: PASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
