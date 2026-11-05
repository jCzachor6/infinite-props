#include "iprop.h"
#include "check.h"

int main(int argc,char** argv) {
    uint8_t* bytes;size_t length;iprop_program* program=NULL;iprop_result* result=NULL;iprop_result* second=NULL;
    iprop_generate_options_ex options;iprop_error_ex error;iprop_output_slot_ex slot;
    iprop_part_snapshot_ex plain,filled,textured,rgba,mask,a8;
    iprop_resource_info_ex geometry,pixels,coverage;iprop_region_info_ex region;iprop_ring_info_ex ring;
    iprop_point_value* vertices;uint8_t* copied;uint32_t count,hole,index;iprop_point_value trace;
    char message[256];
    CHECK(argc==2);CHECK(read_fixture(argv[1],&bytes,&length)==0);
    CHECK(iprop_error_ex_init(&error,sizeof(error))==IPROP_EX_OK);
    error.message_buffer=message;error.message_capacity=sizeof(message);
    CHECK(iprop_program_load(bytes,length,&program,message,sizeof(message))==IPROP_E_FORMAT);
    CHECK(program==NULL && strstr(message,"unsupported")!=NULL);
    CHECK(iprop_program_load_ex(bytes,length,&program,&error)==IPROP_EX_OK);
    CHECK(iprop_generate_options_ex_init(&options,sizeof(options))==IPROP_EX_OK);
    {
        iprop_result* preserved=(iprop_result*)(uintptr_t)1;
        CHECK(iprop_generate_ex(program,&options,NULL,&preserved,&error)==IPROP_EX_E_UNSUPPORTED);
        CHECK(preserved==(iprop_result*)(uintptr_t)1 && error.reason==IPROP_REASON_UNSUPPORTED_FEATURE);
        options.struct_size=4;
        CHECK(iprop_generate_ex(program,&options,NULL,&preserved,&error)==IPROP_EX_E_UNSUPPORTED && preserved==(iprop_result*)(uintptr_t)1);
        options.struct_size=7;options.flags=IPROP_GENERATE_PART_OUTPUTS;
        CHECK(iprop_generate_ex(program,&options,NULL,&preserved,&error)==IPROP_EX_E_UNSUPPORTED && preserved==(iprop_result*)(uintptr_t)1);
        options.struct_size=sizeof(options);
    }
    options.flags=IPROP_GENERATE_PART_OUTPUTS;
    {
        iprop_result* prefix_result=NULL;options.struct_size=8;
        CHECK(iprop_generate_ex(program,&options,NULL,&prefix_result,&error)==IPROP_EX_OK);
        iprop_result_free(prefix_result);options.struct_size=sizeof(options);
    }
    {
        iprop_generate_options legacy={0};iprop_result* preserved=(iprop_result*)(uintptr_t)1;
        CHECK(iprop_generate(program,&legacy,&preserved,message,sizeof(message))==IPROP_E_FORMAT);
        CHECK(preserved==(iprop_result*)(uintptr_t)1 && strstr(message,"unsupported")!=NULL);
    }
    {
        iprop_status_ex status=iprop_generate_ex(program,&options,NULL,&result,&error);
        if(status!=IPROP_EX_OK)fprintf(stderr,"generation status %u, reason %u: %s\n",status,error.reason,message);
        CHECK(status==IPROP_EX_OK);
    }
    CHECK(iprop_result_output_count(result,&count,&error)==IPROP_EX_OK && count==4);
    CHECK(iprop_output_slot_ex_init(&slot,sizeof(slot))==IPROP_EX_OK);
    CHECK(iprop_result_output_slot(result,0,&slot,&error)==IPROP_EX_OK);
    CHECK(slot.kind==IPROP_OUTPUT_KIND_PART && slot.is_list==1 && slot.element_count==3 && slot.owner==0);
    CHECK(slot.name.byte_count==8 && memcmp(slot.name.data,"geometry",8)==0);
    CHECK(iprop_result_output_slot(result,2,&slot,&error)==IPROP_EX_OK && slot.element_count==0);
    CHECK(iprop_result_output_find(result,0,(iprop_utf8_view){"empty",5},&index,&error)==IPROP_EX_OK && index==2);
    CHECK(iprop_part_snapshot_ex_init(&plain,sizeof(plain))==IPROP_EX_OK);
    filled=textured=rgba=mask=a8=plain;
    CHECK(iprop_result_output_part(result,0,0,&plain,&error)==IPROP_EX_OK);
    CHECK(iprop_result_output_part(result,0,1,&filled,&error)==IPROP_EX_OK);
    CHECK(iprop_result_output_part(result,0,2,&textured,&error)==IPROP_EX_OK);
    CHECK(plain.id==0 && filled.id==0 && textured.id==0 && filled.element_index==1);
    CHECK(plain.shape_kind==IPROP_PART_SHAPE_REGION && plain.appearance_kind==IPROP_PART_APPEARANCE_NONE);
    CHECK(filled.appearance_kind==IPROP_PART_APPEARANCE_FILL && filled.tint.r==18 && filled.tint.g==52 && filled.tint.b==86 && filled.tint.a==128);
    CHECK(filled.d4==7 && filled.translation_x!=0 && filled.translation_y!=0);
    CHECK(textured.appearance_kind==IPROP_PART_APPEARANCE_TEXTURE && textured.texture_resource!=0);
    CHECK(plain.shape_resource==filled.shape_resource && filled.shape_resource==textured.shape_resource);
    CHECK(iprop_result_output_part(result,1,0,&rgba,&error)==IPROP_EX_OK);
    CHECK(iprop_result_output_part(result,1,1,&mask,&error)==IPROP_EX_OK);
    CHECK(iprop_result_output_part(result,1,2,&a8,&error)==IPROP_EX_OK);
    CHECK(rgba.id==1 && mask.id==2 && a8.id==3 && rgba.source_id==-1 && rgba.region_index==IPROP_NO_REGION);
    CHECK(rgba.shape_kind==IPROP_PART_SHAPE_LAYER && rgba.appearance_kind==IPROP_PART_APPEARANCE_SOURCE_RGBA);
    CHECK(mask.shape_kind==IPROP_PART_SHAPE_MASK && mask.appearance_kind==IPROP_PART_APPEARANCE_NONE);
    CHECK(rgba.shape_resource==mask.shape_resource && rgba.shape_resource==textured.texture_resource);
    CHECK(iprop_result_resource_count(result,&count,&error)==IPROP_EX_OK && count==3);
    CHECK(iprop_resource_info_ex_init(&geometry,sizeof(geometry))==IPROP_EX_OK);pixels=coverage=geometry;
    CHECK(iprop_result_resource_get(result,plain.shape_resource,&geometry,&error)==IPROP_EX_OK);
    CHECK(geometry.kind==IPROP_RESOURCE_GEOMETRY && geometry.region_count==1 && geometry.ring_count==2 && geometry.data==NULL);
    CHECK(iprop_region_info_ex_init(&region,sizeof(region))==IPROP_EX_OK);
    CHECK(iprop_result_resource_region(result,geometry.resource_id,plain.region_index,&region,&error)==IPROP_EX_OK);
    CHECK(region.hole_count==1 && region.source_id==plain.source_id && region.area>0);
    CHECK(iprop_result_resource_region_hole(result,geometry.resource_id,plain.region_index,0,&hole,&error)==IPROP_EX_OK);
    CHECK(iprop_ring_info_ex_init(&ring,sizeof(ring))==IPROP_EX_OK);
    CHECK(iprop_result_resource_ring(result,geometry.resource_id,hole,&ring,&error)==IPROP_EX_OK);
    CHECK(ring.is_hole==1 && ring.parent_ring==region.outer_ring && ring.vertex_count>=4);
    vertices=(iprop_point_value*)malloc((size_t)ring.vertex_count*sizeof(*vertices));CHECK(vertices!=NULL);
    CHECK(iprop_result_resource_ring_copy_vertices(result,geometry.resource_id,hole,vertices,(uint64_t)ring.vertex_count*sizeof(*vertices),&error)==IPROP_EX_OK);
    CHECK(iprop_result_resource_get(result,rgba.shape_resource,&pixels,&error)==IPROP_EX_OK);
    CHECK(pixels.kind==IPROP_RESOURCE_RASTER && pixels.pixel_format==IPROP_PIXEL_FORMAT_RGBA8 && pixels.width==8 && pixels.height==8 && pixels.row_stride==32 && pixels.byte_count==256);
    CHECK(pixels.data[3]==1 && pixels.data[7]==128);
    copied=(uint8_t*)malloc((size_t)pixels.byte_count);CHECK(copied!=NULL);
    CHECK(iprop_result_resource_copy_pixels(result,pixels.resource_id,copied,pixels.byte_count,&error)==IPROP_EX_OK);
    CHECK(memcmp(copied,pixels.data,(size_t)pixels.byte_count)==0);
    CHECK(iprop_result_resource_get(result,a8.shape_resource,&coverage,&error)==IPROP_EX_OK);
    CHECK(coverage.pixel_format==IPROP_PIXEL_FORMAT_A8 && coverage.byte_count==64 && coverage.row_stride==8);
    CHECK(coverage.data[0]==254 && coverage.data[1]==127 && coverage.data[2]==255);
    CHECK(iprop_result_output_point(result,3,0,&trace,sizeof(trace),&error)==IPROP_EX_OK && trace.x==0);
    CHECK(iprop_result_check_legacy(result,&error)==IPROP_EX_E_UNSUPPORTED && error.reason==IPROP_REASON_UNSUPPORTED_FEATURE);
    {
        iprop_image legacy,before;iprop_result_output_view_ex old,old_before;
        memset(&legacy,0xa5,sizeof(legacy));before=legacy;iprop_result_image(result,&legacy);
        CHECK(memcmp(&legacy,&before,sizeof(legacy))==0);
        CHECK(iprop_result_output_view_ex_init(&old,sizeof(old))==IPROP_EX_OK);old_before=old;
        CHECK(iprop_result_output_get(result,2,&old,&error)==IPROP_EX_E_UNSUPPORTED);
        CHECK(memcmp(&old,&old_before,sizeof(old))==0);
    }
    options.seed=71;CHECK(iprop_generate_ex(program,&options,NULL,&second,&error)==IPROP_EX_OK);
    iprop_result_free(second);iprop_program_free(program);free(bytes);
    CHECK(pixels.data[3]==1 && coverage.data[0]==254);
    CHECK(iprop_result_output_part(result,0,1,&filled,&error)==IPROP_EX_OK && filled.tint.a==128);
    {
        iprop_image_view_ex image;CHECK(iprop_image_view_ex_init(&image,sizeof(image))==IPROP_EX_OK);
        CHECK(iprop_result_query_image(result,&image,&error)==IPROP_EX_OK && image.byte_count==256);
        for(index=0;index<image.byte_count;++index)CHECK(image.pixels[index]==0);
    }
    iprop_result_free(result);
    CHECK(copied[3]==1 && copied[7]==128 && vertices[0].x>=0);
    free(vertices);free(copied);
    puts("C99 typed parts, shared geometry/alpha, copies and independent result lifetime: PASS");
    return 0;
}
