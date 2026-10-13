#include "test_support.h"
#include "source_over_reference.h"

#include <stdlib.h>
#include <string.h>

void check_generation(const char* program_dir) {
    char err[512];

    /* Straight RGBA8 part inheritance through the unchanged C host layout. */
    {
        char path[1024]; snprintf(path,sizeof(path),"%s/source-over-panels.iprop",program_dir);
        size_t length=0; uint8_t* bytes=read_file(path,&length);
        iprop_program* program=NULL; iprop_result* result=NULL;
        iprop_generate_options options={0}; options.seed=26;
        const uint32_t expected[]={0x41f00000,0x41a00000,0x41d00000,0,0x41f00000,0x41a00000};
        CHECK(bytes!=NULL,"cannot read source-over fixture");
        if(bytes!=NULL){CHECK(iprop_program_load(bytes,length,&program,err,sizeof(err))==IPROP_OK,"source-over load: %s",err);free(bytes);}
        if(program!=NULL){CHECK(iprop_generate(program,&options,&result,err,sizeof(err))==IPROP_OK,"source-over generation: %s",err);iprop_program_free(program);}
        if(result!=NULL){
            iprop_image image; iprop_result_image(result,&image);
            CHECK(image.width==32 && image.height==24 && image.output_count==3,"source-over image/output metadata");
            for(uint32_t i=0;i<image.output_count && i<3;++i){
                const iprop_output* output=&image.outputs[i];
                CHECK(output->kind==IPROP_OUTPUT_POINT && output->owner==(i==2 ? 1u : 0u) && !output->is_list && output->count==1,"source-over output metadata");
                if(output->count==1){uint32_t bits[2];memcpy(bits,output->values,sizeof(bits));
                    CHECK(bits[0]==expected[i*2] && bits[1]==expected[i*2+1],"source-over output bits");}
            }
            if(image.width==32 && image.height==24)for(uint32_t y=0;y<24;++y)for(uint32_t x=0;x<32;++x){
                uint8_t color[4];source_over_panel_pixel(x,y,color);
                CHECK(memcmp(image.pixels+(y*32+x)*4,color,4)==0,"source-over pixel %u,%u",x,y);
            }
            iprop_result_free(result);
        }
    }

    /* Preserved stage-7 direct-render oracle for the captured panel. */
    {
        char path[1024]; snprintf(path,sizeof(path),"%s/captured-panels.iprop",program_dir);
        size_t length=0; uint8_t* bytes=read_file(path,&length);
        iprop_program* program=NULL; iprop_result* result=NULL;
        iprop_generate_options options={0}; options.seed=26;
        const uint32_t expected[]={0x41e00000,0x41900000,0x46f3a600,0x471e3700};
        CHECK(bytes!=NULL,"cannot read captured panel fixture");
        if(bytes!=NULL){CHECK(iprop_program_load(bytes,length,&program,err,sizeof(err))==IPROP_OK,"captured panel load: %s",err);free(bytes);}
        if(program!=NULL){CHECK(iprop_generate(program,&options,&result,err,sizeof(err))==IPROP_OK,"captured panel generation: %s",err);iprop_program_free(program);}
        if(result!=NULL){
            iprop_image image; iprop_result_image(result,&image);
            CHECK(image.width==32 && image.height==24 && image.output_count==2,"captured panel image/output metadata");
            for(uint32_t i=0;i<image.output_count && i<2;++i){
                const iprop_output* output=&image.outputs[i];
                CHECK(output->kind==IPROP_OUTPUT_POINT && output->owner==0 && !output->is_list && output->count==1,"captured panel output metadata");
                if(output->count==1){uint32_t bits[2];memcpy(bits,output->values,sizeof(bits));
                    CHECK(bits[0]==expected[i*2] && bits[1]==expected[i*2+1],"captured panel bits at %u",i);}
            }
            if(image.width==32 && image.height==24)for(uint32_t y=0;y<24;++y)for(uint32_t x=0;x<32;++x){
                uint8_t color[4]={36,50,66,255};
                if(x>=2 && x<30 && y>=3 && y<21){color[0]=52;color[1]=58;color[2]=66;}
                if(x>=4 && x<28 && y>=5 && y<19){color[0]=146;color[1]=156;color[2]=168;}
                if((x==4 && y==5)||(x==27 && y==18)){color[0]=192;color[1]=144;color[2]=96;}
                CHECK(memcmp(image.pixels+(y*32+x)*4,color,4)==0,"captured panel pixel %u,%u",x,y);
            }
            iprop_result_free(result);
        }
    }

    /* Independent raw-RNG and binary64 integral pins for density columns. */
    {
        char path[1024]; snprintf(path,sizeof(path),"%s/point-distributions.iprop",program_dir);
        size_t length=0; uint8_t* bytes=read_file(path,&length);
        iprop_program* program=NULL; iprop_result* result=NULL;
        iprop_generate_options options={0}; options.seed=26;
        const uint32_t expected[]={0x40000000,0x40000000,0x41a085d5,0x41b6b498,0x4766fe00,0x46f1ba00};
        CHECK(bytes!=NULL,"cannot read density fixture");
        if(bytes!=NULL){CHECK(iprop_program_load(bytes,length,&program,err,sizeof(err))==IPROP_OK,"density load: %s",err);free(bytes);}
        if(program!=NULL){CHECK(iprop_generate(program,&options,&result,err,sizeof(err))==IPROP_OK,"density generation: %s",err);iprop_program_free(program);}
        if(result!=NULL){
            iprop_image image; iprop_result_image(result,&image);
            CHECK(image.width==32 && image.height==32 && image.output_count==3,"density image/output metadata");
            for(uint32_t i=0;i<image.output_count && i<3;++i){
                const iprop_output* output=&image.outputs[i];
                CHECK(output->kind==IPROP_OUTPUT_POINT && output->owner==0 && !output->is_list && output->count==1,"density output metadata");
                if(output->count==1){uint32_t bits[2];memcpy(bits,output->values,sizeof(bits));
                    CHECK(bits[0]==expected[i*2] && bits[1]==expected[i*2+1],"density bits at %u",i);}
            }
            if(image.width==32 && image.height==32)for(uint32_t y=0;y<32;++y)for(uint32_t x=0;x<32;++x){
                uint8_t color[4]={0,0,0,0};
                if(((x>=2 && x<10)||(x>=12 && x<20)) && y>=8 && y<28){color[0]=142;color[1]=156;color[2]=112;color[3]=255;}
                if(x>=22 && x<30 && y>=5 && y<28){color[0]=142;color[1]=156;color[2]=112;color[3]=255;}
                CHECK(memcmp(image.pixels+(y*32+x)*4,color,4)==0,"density pixel %u,%u",x,y);
            }
            iprop_result_free(result);
        }
    }

    /* Independent raw-RNG thresholds select material 2 and one rust layer. */
    {
        char path[1024]; snprintf(path,sizeof(path),"%s/weighted.iprop",program_dir);
        size_t length=0; uint8_t* bytes=read_file(path,&length);
        iprop_program* program=NULL; iprop_result* result=NULL;
        iprop_generate_options options={0}; options.seed=26;
        const uint32_t expected[]={0x40000000,0x40000000,0x3f800000,0x3f800000,0x47738c00,0x4671b800};
        CHECK(bytes!=NULL,"cannot read weighted fixture");
        if(bytes!=NULL){CHECK(iprop_program_load(bytes,length,&program,err,sizeof(err))==IPROP_OK,"weighted load: %s",err);free(bytes);}
        if(program!=NULL){CHECK(iprop_generate(program,&options,&result,err,sizeof(err))==IPROP_OK,"weighted generation: %s",err);iprop_program_free(program);}
        if(result!=NULL){
            iprop_image image; iprop_result_image(result,&image);
            CHECK(image.width==32 && image.height==24 && image.output_count==3,"weighted image/output metadata");
            for(uint32_t i=0;i<image.output_count && i<3;++i){
                const iprop_output* output=&image.outputs[i];
                CHECK(output->kind==IPROP_OUTPUT_POINT && output->owner==0 && !output->is_list && output->count==1,"weighted output metadata");
                if(output->count==1){uint32_t bits[2];memcpy(bits,output->values,sizeof(bits));
                    CHECK(bits[0]==expected[i*2] && bits[1]==expected[i*2+1],"weighted bits at %u",i);}
            }
            if(image.width==32 && image.height==24)for(uint32_t y=0;y<24;++y)for(uint32_t x=0;x<32;++x){
                uint8_t color[4]={0,0,0,0};
                if(x>=2 && x<30 && y>=2 && y<22){color[0]=142;color[1]=156;color[2]=112;color[3]=255;}
                if(x>=4 && x<8 && y>=6 && y<18){color[0]=52;color[1]=42;color[2]=34;color[3]=255;}
                CHECK(memcmp(image.pixels+(y*32+x)*4,color,4)==0,"weighted pixel %u,%u",x,y);
            }
            iprop_result_free(result);
        }
    }

    /* Preserved stage-4 runtime pins for constant samplers and palettes. */
    {
        char path[1024]; snprintf(path,sizeof(path),"%s/sampling-palettes.iprop",program_dir);
        size_t length=0; uint8_t* bytes=read_file(path,&length);
        iprop_program* program=NULL; iprop_result* result=NULL;
        iprop_generate_options options={0}; options.seed=26;
        const uint32_t expected[]={0x40400000,0x42200000,0x41aa69ec,0x40000000,
            0x41200000,0x41600000,0x3f2ac48e,0,0x475a7000,0x47354c00};
        CHECK(bytes!=NULL,"cannot read sampling palette fixture");
        if(bytes!=NULL){CHECK(iprop_program_load(bytes,length,&program,err,sizeof(err))==IPROP_OK,"palette load: %s",err);free(bytes);}
        if(program!=NULL){CHECK(iprop_generate(program,&options,&result,err,sizeof(err))==IPROP_OK,"palette generation: %s",err);iprop_program_free(program);}
        if(result!=NULL){
            iprop_image image; iprop_result_image(result,&image);
            CHECK(image.width==32 && image.height==24 && image.output_count==5,"palette image/output metadata");
            for(uint32_t i=0;i<image.output_count && i<5;++i){
                const iprop_output* output=&image.outputs[i];
                CHECK(output->kind==IPROP_OUTPUT_POINT && output->owner==0 && !output->is_list && output->count==1,"palette output metadata");
                if(output->count==1){uint32_t bits[2];memcpy(bits,output->values,sizeof(bits));
                    CHECK(bits[0]==expected[i*2] && bits[1]==expected[i*2+1],"palette bits at %u",i);}
            }
            if(image.width==32 && image.height==24)for(uint32_t y=0;y<24;++y)for(uint32_t x=0;x<32;++x){
                uint8_t color[4]={0,0,0,0};
                if(x>=2 && ((y>=2 && y<6 && x<12)||(y>=8 && y<12 && x<13)||(y>=14 && y<18 && x<16))){
                    color[0]=168;color[1]=173;color[2]=179;color[3]=255;}
                CHECK(memcmp(image.pixels+(y*32+x)*4,color,4)==0,"palette pixel %u,%u",x,y);
            }
            iprop_result_free(result);
        }
    }

    /* Independent stage-3 ordinary-range pins, before sampler support. */
    {
        char path[1024]; snprintf(path,sizeof(path),"%s/samplers.iprop",program_dir);
        size_t length=0; uint8_t* bytes=read_file(path,&length);
        iprop_program* program=NULL; iprop_result* result=NULL;
        iprop_generate_options options={0};options.seed=26;
        const uint32_t expected[]={0x40400000,0x42200000,0x416ef976,0x401a259f,
            0x41500000,0x41500000,0x46c16e00,0x45b8a800};
        CHECK(bytes!=NULL,"cannot read sampler fixture");
        if(bytes!=NULL){CHECK(iprop_program_load(bytes,length,&program,err,sizeof(err))==IPROP_OK,"sampler load: %s",err);free(bytes);}
        if(program!=NULL){CHECK(iprop_generate(program,&options,&result,err,sizeof(err))==IPROP_OK,"sampler generation: %s",err);iprop_program_free(program);}
        if(result!=NULL){
            iprop_image image;iprop_result_image(result,&image);
            CHECK(image.width==32 && image.height==24 && image.output_count==4,"sampler image/output metadata");
            for(uint32_t i=0;i<image.output_count && i<4;++i){
                const iprop_output* output=&image.outputs[i];
                CHECK(output->kind==IPROP_OUTPUT_POINT && output->owner==0 && !output->is_list && output->count==1,"sampler output metadata");
                if(output->count==1){uint32_t bits[2];memcpy(bits,output->values,sizeof(bits));
                    CHECK(bits[0]==expected[i*2] && bits[1]==expected[i*2+1],"sampler bits at %u",i);}
            }
            if(image.width==32 && image.height==24)for(uint32_t y=0;y<24;++y)for(uint32_t x=0;x<32;++x){
                const uint8_t* actual=image.pixels+(y*32+x)*4;
                uint8_t color[4]={0,0,0,0};
                if(x>=2 && x<15 && ((y>=2 && y<6)||(y>=8 && y<12)||(y>=14 && y<18))){
                    color[0]=134;color[1]=144;color[2]=156;color[3]=255;}
                CHECK(memcmp(actual,color,4)==0,"sampler pixel %u,%u",x,y);
            }
            iprop_result_free(result);
        }
    }

    /* Independent stage-2 ordinary polygon/RNG pins, before list support. */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/collections.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        iprop_program* program = NULL;
        iprop_result* result = NULL;
        iprop_generate_options options = {0}; options.seed = 26;
        const uint32_t expected[] = {0x40800000,0x41980000,0x41900000,0x40000000,
            0x41200000,0x41f00000,0x46da8a00,0x475aa600};
        const uint32_t rows[] = {0,0,0x3c000,0x7ff80,0x7fff8,0xffff8,0xffff0,0x1ffff0,
            0x1ffff0,0x3fffe0,0x7fffe0,0x7fffe0,0xffffc0,0xffffc0,0x1ffffc0,0x7fff80,
            0xfff80,0xff80,0x1f00,0x300,0,0,0,0};
        CHECK(bytes != NULL, "cannot read collection fixture");
        if (bytes != NULL) {
            CHECK(iprop_program_load(bytes,length,&program,err,sizeof(err)) == IPROP_OK,
                  "collection load: %s",err); free(bytes);
        }
        if (program != NULL) {
            CHECK(iprop_generate(program,&options,&result,err,sizeof(err)) == IPROP_OK,
                  "collection generation: %s",err); iprop_program_free(program);
        }
        if (result != NULL) {
            iprop_image image; iprop_result_image(result,&image);
            CHECK(image.width == 32 && image.height == 24 && image.output_count == 4,
                  "collection image/output metadata");
            for (uint32_t i = 0; i < image.output_count && i < 4; ++i) {
                const iprop_output* output = &image.outputs[i];
                CHECK(output->kind == IPROP_OUTPUT_POINT && output->owner == 0 &&
                      !output->is_list && output->count == 1,"collection output metadata");
                if (output->count == 1) {
                    uint32_t bits[2]; memcpy(bits,output->values,sizeof(bits));
                    CHECK(bits[0] == expected[i*2] && bits[1] == expected[i*2+1],"collection bits at %u",i);
                }
            }
            if (image.width == 32 && image.height == 24) for (uint32_t y = 0; y < 24; ++y)
                for (uint32_t x = 0; x < 32; ++x) {
                    const uint8_t* actual = image.pixels + (y*32+x)*4;
                    uint8_t color[4] = {0,0,0,0};
                    if ((rows[y] >> x) & 1) { color[0]=146; color[1]=156; color[2]=168; color[3]=255; }
                    if (x == 3 && y == 4) { color[0]=52; color[1]=58; color[2]=66; color[3]=255; }
                    CHECK(memcmp(actual,color,4) == 0,"collection polygon pixel %u,%u",x,y);
                }
            iprop_result_free(result);
        }
    }

    /* Pins from the preserved stage-1 runtime, using ordinary constructors. */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/asset-constants.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        iprop_program* program = NULL;
        iprop_result* result = NULL;
        iprop_generate_options options = {0};
        options.seed = 26;
        const uint32_t expected[] = {UINT32_C(0x3f800000), UINT32_C(0x3f800000),
            UINT32_C(0x3f19999a), UINT32_C(0x3f4ccccd), 0, 0,
            UINT32_C(0x3f116e13), UINT32_C(0x3ee83a45), 0, UINT32_C(0x3efbda76),
            UINT32_C(0x4766fe00), UINT32_C(0x46f1ba00)};
        CHECK(bytes != NULL, "cannot read asset constant fixture");
        if (bytes != NULL) {
            CHECK(iprop_program_load(bytes, length, &program, err, sizeof(err)) == IPROP_OK,
                  "asset constant load: %s", err);
            free(bytes);
        }
        if (program != NULL) {
            CHECK(iprop_generate(program, &options, &result, err, sizeof(err)) == IPROP_OK,
                  "asset constant generation: %s", err);
            iprop_program_free(program);
        }
        if (result != NULL) {
            iprop_image image;
            iprop_result_image(result, &image);
            CHECK(image.width == 2 && image.height == 2 && image.output_count == 6,
                  "asset constant image/output metadata");
            for (uint32_t i = 0; i < image.output_count; ++i) {
                const iprop_output* output = &image.outputs[i];
                CHECK(output->kind == IPROP_OUTPUT_POINT && output->owner == 0 &&
                      !output->is_list && output->count == 1, "asset constant output metadata");
                if (i < 6 && output->count == 1) {
                    uint32_t bits[2];
                    memcpy(bits, output->values, sizeof(bits));
                    CHECK(bits[0] == expected[i * 2] && bits[1] == expected[i * 2 + 1],
                          "asset constant output bits at slot %u", i);
                }
            }
            const uint8_t pixels[] = {146,156,168,255,139,149,161,255,0,0,0,0,0,0,0,0};
            CHECK(memcmp(image.pixels, pixels, sizeof(pixels)) == 0, "asset constant pixels");
            iprop_result_free(result);
        }
    }

    /* Format 0.6 math still uses the frozen host geometry and input layouts. */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/math-bounds.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        iprop_program* program = NULL;
        iprop_result* result = NULL;
        iprop_generate_options options = {0};
        options.seed = 26;
        const uint32_t expected[] = {UINT32_C(0x3f800000), UINT32_C(0x3f800000),
            UINT32_C(0x3f19999a), UINT32_C(0x3f4ccccd), 0, 0,
            UINT32_C(0x40000000), UINT32_C(0x40000000)};
        CHECK(bytes != NULL, "cannot read format 0.6 math fixture");
        if (bytes != NULL) {
            CHECK(iprop_program_load(bytes, length, &program, err, sizeof(err)) == IPROP_OK,
                  "format 0.6 math load: %s", err);
            free(bytes);
        }
        if (program != NULL) {
            CHECK(iprop_generate(program, &options, &result, err, sizeof(err)) == IPROP_OK,
                  "format 0.6 math generation: %s", err);
            iprop_program_free(program);
        }
        if (result != NULL) {
            iprop_image image;
            iprop_result_image(result, &image);
            CHECK(image.output_count == 4, "format 0.6 host output count");
            for (uint32_t i = 0; i < image.output_count; ++i) {
                const iprop_output* output = &image.outputs[i];
                CHECK(output->kind == IPROP_OUTPUT_POINT && output->owner == 0 &&
                      !output->is_list && output->count == 1, "format 0.6 host output metadata");
                if (i < 4 && output->count == 1) {
                    uint32_t bits[2];
                    memcpy(bits, output->values, sizeof(bits));
                    CHECK(bits[0] == expected[i * 2] && bits[1] == expected[i * 2 + 1],
                          "format 0.6 math output bits at slot %u", i);
                }
            }
            CHECK(image.pixels[0] == 0x20 && image.pixels[1] == 0xc0 && image.pixels[2] == 0x80,
                  "format 0.6 math pixel");
            iprop_result_free(result);
        }
    }

    /* The uniform overrides: the fixture's three inputs drive three
       diagonal pixels — the defaults, all three overrides, an unknown
       name (ignored) and a type mismatch (a trap). */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/uniforms.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        CHECK(bytes != NULL, "cannot read %s", path);
        if (bytes != NULL) {
            iprop_program* program = NULL;
            CHECK(iprop_program_load(bytes, length, &program, err,
                                     sizeof(err)) == IPROP_OK,
                  "load %s: %s", path, err);
            if (program != NULL) {
                iprop_generate_options options;
                memset(&options, 0, sizeof(options));
                iprop_result* result = NULL;
                iprop_image image;

                CHECK(iprop_generate(program, &options, &result, err,
                                     sizeof(err)) == IPROP_OK,
                      "uniform defaults: %s", err);
                if (result != NULL) {
                    iprop_result_image(result, &image);
                    CHECK(image.width == 8 && image.height == 8,
                          "fixture size %ux%u", image.width, image.height);
                    CHECK(image.pixels[0] == 0x33 && image.pixels[1] == 0x55 &&
                              image.pixels[2] == 0xaa &&
                              image.pixels[3] == 0xff,
                          "the default colour pixel");
                    iprop_result_free(result);
                }

                iprop_uniform uniforms[3];
                memset(uniforms, 0, sizeof(uniforms));
                uniforms[0].name = "base";
                uniforms[0].type = IPROP_UNIFORM_COLOR;
                uniforms[0].r = 0xff;
                uniforms[0].g = 0xff;
                uniforms[0].b = 0xff;
                uniforms[0].a = 0xff;
                uniforms[1].name = "fx";
                uniforms[1].type = IPROP_UNIFORM_F32;
                uniforms[1].f = 10.f;
                uniforms[2].name = "ux";
                uniforms[2].type = IPROP_UNIFORM_U32;
                uniforms[2].u = 7;
                options.uniforms = uniforms;
                options.uniform_count = 3;
                result = NULL;
                CHECK(iprop_generate(program, &options, &result, err,
                                     sizeof(err)) == IPROP_OK,
                      "uniform overrides: %s", err);
                if (result != NULL) {
                    iprop_result_image(result, &image);
                    CHECK(image.pixels[0] == 0xff && image.pixels[1] == 0xff &&
                              image.pixels[2] == 0xff &&
                              image.pixels[3] == 0xff,
                          "the overridden colour pixel");
                    /* shade(base, 10) on (1,1) — byte offset 36. */
                    CHECK(image.pixels[36] == 0x3d &&
                              image.pixels[37] == 0x5f &&
                              image.pixels[38] == 0xb4,
                          "the f32 override: shade +10");
                    /* shade(base, 7) on (2,2) — byte offset 72. */
                    CHECK(image.pixels[72] == 0x3a &&
                              image.pixels[73] == 0x5c &&
                              image.pixels[74] == 0xb1,
                          "the u32 override: shade +7");
                    iprop_result_free(result);
                }

                options.uniform_count = 1;
                uniforms[0].name = "not_an_input";
                uniforms[0].type = IPROP_UNIFORM_U32;
                uniforms[0].u = 9;
                result = NULL;
                CHECK(iprop_generate(program, &options, &result, err,
                                     sizeof(err)) == IPROP_OK,
                      "unknown uniform: %s", err);
                if (result != NULL) {
                    iprop_result_image(result, &image);
                    CHECK(image.pixels[0] == 0x33 && image.pixels[1] == 0x55 &&
                              image.pixels[2] == 0xaa,
                          "the unknown uniform must leave the default in "
                          "place");
                    iprop_result_free(result);
                }

                uniforms[0].name = "base";
                uniforms[0].type = IPROP_UNIFORM_F32;
                uniforms[0].f = 1.f;
                result = NULL;
                CHECK(iprop_generate(program, &options, &result, err,
                                     sizeof(err)) == IPROP_E_TRAP,
                      "the type mismatch must trap, got %s", err);
                CHECK(strstr(err, "type mismatch") != NULL,
                      "the trap message must mention the mismatch: %s", err);

                iprop_program_free(program);
            }
            free(bytes);
        }
    }

    /* Duplicate names in the root and two parts retain distinct owners.
       A second generation and freeing both it and the program must leave
       the first result's pixels, names, values and borrowed pointers valid. */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/output-owners.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        iprop_program* program = NULL;
        iprop_result* result = NULL;
        iprop_image first = {0};
        uint8_t first_pixels[8 * 8 * 4] = {0};
        int have_pixels = 0;
        iprop_generate_options options = {0};
        CHECK(bytes != NULL, "cannot read owner fixture");
        if (bytes != NULL) {
            CHECK(iprop_program_load(bytes, length, &program, err, sizeof(err)) == IPROP_OK,
                  "owner fixture load: %s", err);
            free(bytes);
        }
        if (program != NULL) {
            CHECK(iprop_generate(program, &options, &result, err, sizeof(err)) == IPROP_OK,
                  "owner fixture generation: %s", err);
            if (result != NULL) {
                iprop_result_image(result, &first);
                have_pixels = first.width == 8 && first.height == 8 && first.pixels != NULL;
                CHECK(have_pixels, "owner fixture first image");
                if (have_pixels) memcpy(first_pixels, first.pixels, sizeof(first_pixels));
            }
            iprop_result* second = NULL;
            options.seed = 17;
            CHECK(iprop_generate(program, &options, &second, err, sizeof(err)) == IPROP_OK,
                  "second owner fixture generation: %s", err);
            CHECK(second != NULL && second != result, "independent result handles");
            iprop_result_free(second);
            iprop_program_free(program);
        }
        if (result != NULL) {
            iprop_image image;
            iprop_result_image(result, &image);
            CHECK(image.pixels == first.pixels && image.outputs == first.outputs,
                  "first borrowed views remain stable");
            if (have_pixels) CHECK(memcmp(image.pixels, first_pixels, sizeof(first_pixels)) == 0,
                                   "first image remains unchanged");
            CHECK(image.output_count == 9, "owner fixture slot count");
            const char* names[] = {"anchor", "collider", "zone"};
            for (uint32_t i = 0; i < image.output_count; ++i) {
                const iprop_output* slot = &image.outputs[i];
                CHECK(slot->owner == i / 3, "slot %u owner %u", i, slot->owner);
                CHECK(strcmp(slot->name, names[i % 3]) == 0, "slot %u name", i);
                CHECK(slot->kind == (iprop_output_kind)(i % 3), "slot %u kind", i);
                CHECK(slot->count == 1 && slot->values != NULL, "slot %u values", i);
                if (slot->values != NULL) {
                    CHECK(slot->values[0] == (i < 3 ? 4.f : i < 6 ? 1.f : 2.f),
                          "slot %u value", i);
                }
            }
            iprop_result_free(result);
        }
    }

    /* Compiled 0.2 source: palettes, typed helpers, loop control, math,
       empty lists, variants/archetypes, size and uniform overrides. */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/language.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        iprop_program* program = NULL;
        CHECK(bytes != NULL, "cannot read language fixture");
        if (bytes != NULL) {
            CHECK(iprop_program_load(bytes, length, &program, err, sizeof(err)) == IPROP_OK,
                  "language fixture load: %s", err);
            free(bytes);
        }
        if (program != NULL) {
            const uint8_t colors[][3] = {{0x11,0x22,0x33}, {0x44,0x55,0x66}, {0x77,0x88,0x99}};
            for (uint32_t v = 0; v < 3; ++v) {
                for (uint32_t a = 0; a < 2; ++a) {
                    iprop_generate_options options = {0};
                    options.seed = 26;
                    options.variant = v;
                    options.archetype = a;
                    /* A non-square size scales raster coordinates (2x, 3x)
                       while structured outputs remain in asset units. */
                    options.size_x = 16;
                    options.size_y = 24;
                    iprop_uniform base = {0};
                    base.name = "base";
                    base.type = IPROP_UNIFORM_COLOR;
                    base.r = 0xee;
                    base.g = 0xdd;
                    base.b = 0xcc;
                    base.a = 0xff;
                    options.uniforms = &base;
                    options.uniform_count = 1;
                    iprop_result* result = NULL;
                    CHECK(iprop_generate(program, &options, &result, err, sizeof(err)) == IPROP_OK,
                          "language generation v%u a%u: %s", v, a, err);
                    if (result == NULL) continue;
                    iprop_image image;
                    iprop_result_image(result, &image);
                    CHECK(image.width == 16 && image.height == 24, "language size override");
                    CHECK(image.pixels[0] == 0xee && image.pixels[1] == 0xdd &&
                          image.pixels[2] == 0xcc && image.pixels[3] == 0xff,
                          "language uniform override");
                    const size_t pixel = ((size_t)image.width * 3 + (a + 1) * 2) * 4;
                    CHECK(memcmp(image.pixels + pixel, colors[v], 3) == 0 &&
                          image.pixels[pixel + 3] == 0xff, "language palette pixel");
                    CHECK(image.output_count == 4, "language output count");
                    check_point(&image, 0, (float)(v + 1), (float)a + .25f);
                    if (image.output_count == 4) {
                        uint32_t angle_bits = 0;
                        memcpy(&angle_bits, image.outputs[1].values, sizeof(angle_bits));
                        CHECK(angle_bits == UINT32_C(0x3f8db70d) && image.outputs[1].values[1] == 1.f,
                              "language math pins");
                        const iprop_output* list = &image.outputs[2];
                        CHECK(list->is_list && list->count == 2, "language loop list count");
                        if (list->count == 2) {
                            CHECK(list->values[0] == 0.f && list->values[1] == (float)a &&
                                  list->values[2] == 2.f && list->values[3] == (float)a,
                                  "language continue/break values");
                        }
                        CHECK(image.outputs[3].is_list && image.outputs[3].count == 0,
                              "language empty circle list");
                        for (uint32_t i = 0; i < 4; ++i) {
                            CHECK(image.outputs[i].owner == 0, "language root owner");
                        }
                    }
                    iprop_result_free(result);
                }
            }
            iprop_program_free(program);
        }
    }

    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/plant.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        iprop_program* program = NULL;
        iprop_result* result = NULL;
        iprop_generate_options options = {0};
        options.seed = 26;
        CHECK(bytes != NULL, "cannot read compiled plant");
        if (bytes != NULL) {
            CHECK(iprop_program_load(bytes, length, &program, err, sizeof(err)) == IPROP_OK,
                  "compiled plant load: %s", err);
            free(bytes);
        }
        if (program != NULL) {
            CHECK(iprop_generate(program, &options, &result, err, sizeof(err)) == IPROP_OK,
                  "compiled plant generation: %s", err);
            iprop_program_free(program);
        }
        if (result != NULL) {
            iprop_image image;
            iprop_result_image(result, &image);
            CHECK(image.width == 64 && image.height == 64, "compiled plant size");
            iprop_result_free(result);
        }
    }
}
