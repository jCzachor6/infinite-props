#include "iprop.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "generate_ex: line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int fixture(const char* dir, const char* name, iprop_program** program) {
    char path[1024]; FILE* f; long n; uint8_t* bytes;
    snprintf(path,sizeof(path),"%s/%s",dir,name);
    f=fopen(path,"rb");
    CHECK(f != NULL && fseek(f,0,SEEK_END)==0); n=ftell(f); CHECK(n>0 && fseek(f,0,SEEK_SET)==0);
    bytes=(uint8_t*)malloc((size_t)n); CHECK(bytes != NULL && fread(bytes,1,(size_t)n,f)==(size_t)n);
    CHECK(fclose(f)==0);
    CHECK(iprop_program_load_ex(bytes,(uint64_t)n,program,NULL)==IPROP_EX_OK);
    free(bytes); return 0;
}

static size_t floats(const iprop_output* output) {
    return output->count * (output->kind==IPROP_OUTPUT_POINT ? 2u : output->kind==IPROP_OUTPUT_RECT ? 4u : 3u);
}

/* Bitwise equality of image, every output slot and the fuel statistics. */
static int same(const iprop_result* a, const iprop_result* b) {
    iprop_image x,y; iprop_result_stats_ex sx,sy; uint32_t i;
    iprop_result_image(a,&x); iprop_result_image(b,&y);
    CHECK(x.width==y.width && x.height==y.height && x.output_count==y.output_count);
    CHECK(memcmp(x.pixels,y.pixels,(size_t)x.width*x.height*4)==0);
    for(i=0;i<x.output_count;++i) {
        const iprop_output *p=&x.outputs[i],*q=&y.outputs[i];
        CHECK(p->kind==q->kind && p->is_list==q->is_list && p->owner==q->owner && p->count==q->count);
        CHECK(strcmp(p->name,q->name)==0);
        CHECK(floats(p)==0 || memcmp(p->values,q->values,floats(p)*sizeof(float))==0);
    }
    CHECK(iprop_result_stats_ex_init(&sx,sizeof(sx))==IPROP_EX_OK && iprop_result_stats_ex_init(&sy,sizeof(sy))==IPROP_EX_OK);
    CHECK(iprop_result_query_stats(a,&sx,NULL)==IPROP_EX_OK && iprop_result_query_stats(b,&sy,NULL)==IPROP_EX_OK);
    CHECK(sx.fuel_budget==sy.fuel_budget && sx.fuel_used==sy.fuel_used && sx.output_element_count==sy.output_element_count);
    return 0;
}

static iprop_generate_options legacy_options(uint32_t seed, uint32_t variant, uint32_t size_x, uint32_t size_y) {
    iprop_generate_options o; memset(&o,0,sizeof(o));
    o.seed=seed; o.variant=variant; o.size_x=size_x; o.size_y=size_y; return o;
}

static iprop_generate_options_ex options_ex(uint32_t seed, uint32_t variant, uint32_t size_x, uint32_t size_y) {
    iprop_generate_options_ex o;
    (void)iprop_generate_options_ex_init(&o,sizeof(o));
    o.seed=seed; o.variant=variant; o.size_x=size_x; o.size_y=size_y; return o;
}

/* A rejected generation keeps the caller's previous output pointer. */
static int reject(const iprop_program* program, const iprop_generate_options_ex* options,
                  const iprop_parameters* parameters, iprop_error_ex* error,
                  iprop_status_ex status, iprop_error_reason reason, iprop_error_stage stage) {
    iprop_result* sentinel=(iprop_result*)(void*)&sentinel; iprop_result* out=sentinel;
    CHECK(iprop_generate_ex(program,options,parameters,&out,error)==status && out==sentinel);
    CHECK(error->status==status && error->reason==reason && error->stage==stage);
    return 0;
}

int main(int argc, char** argv) {
    iprop_program *uniforms=NULL,*portrait=NULL,*owners=NULL,*trap=NULL,*portrait_again=NULL;
    iprop_parameters *values=NULL,*defaults=NULL,*foreign=NULL;
    iprop_result *legacy=NULL,*checked=NULL,*previous=NULL;
    iprop_library_info_ex library;
    iprop_error_ex error; char message[128];
    iprop_generate_options_ex request;
    uint32_t seed,variant,index;
    CHECK(argc==2);
    CHECK(fixture(argv[1],"uniforms.iprop",&uniforms)==0 && fixture(argv[1],"portrait.iprop",&portrait)==0);
    CHECK(fixture(argv[1],"output-owners.iprop",&owners)==0 && fixture(argv[1],"error-trap.iprop",&trap)==0);
    CHECK(fixture(argv[1],"portrait.iprop",&portrait_again)==0);
    CHECK(iprop_library_info_ex_init(&library,sizeof(library))==IPROP_EX_OK);
    CHECK(iprop_library_query(&library)==IPROP_EX_OK && (library.capabilities&IPROP_CAP_GENERATE_EX)!=0);
    CHECK(iprop_error_ex_init(&error,sizeof(error))==IPROP_EX_OK); error.message_buffer=message; error.message_capacity=sizeof(message);

    /* Defaults: NULL parameters, a fresh default set and the legacy entry give
     * the same pixels, output slots and fuel, across seeds, variants, sizes. */
    CHECK(iprop_parameters_create(portrait,&defaults,NULL)==IPROP_EX_OK);
    { const uint32_t seeds[]={0,7,26}, variants[]={0,3,7}, sizes[][2]={{0,0},{32,48},{1,0}};
      size_t s;
      for(seed=0;seed<3;++seed) for(variant=0;variant<3;++variant) for(s=0;s<3;++s) {
          iprop_generate_options old=legacy_options(seeds[seed],variants[variant],sizes[s][0],sizes[s][1]);
          request=options_ex(seeds[seed],variants[variant],sizes[s][0],sizes[s][1]);
          CHECK(iprop_generate(portrait,&old,&legacy,NULL,0)==IPROP_OK);
          CHECK(iprop_generate_ex(portrait,&request,NULL,&checked,&error)==IPROP_EX_OK);
          CHECK(error.status==IPROP_EX_OK && message[0]=='\0' && same(legacy,checked)==0);
          iprop_result_free(checked); checked=NULL;
          CHECK(iprop_generate_ex(portrait,&request,defaults,&checked,&error)==IPROP_EX_OK && same(legacy,checked)==0);
          iprop_result_free(checked); iprop_result_free(legacy); checked=legacy=NULL;
      }
    }
    { iprop_generate_options old=legacy_options(5,0,0,0); request=options_ex(5,0,0,0);
      CHECK(iprop_generate(owners,&old,&legacy,NULL,0)==IPROP_OK);
      CHECK(iprop_generate_ex(owners,&request,NULL,&checked,&error)==IPROP_EX_OK && same(legacy,checked)==0);
      iprop_result_free(checked); iprop_result_free(legacy); checked=legacy=NULL; }

    /* A four-byte options prefix requests the source defaults; absent fields
     * are zero even when the caller's storage beyond the prefix is garbage. */
    { iprop_generate_options old=legacy_options(0,0,0,0); iprop_generate_options old7=legacy_options(7,0,0,0);
      CHECK(iprop_generate(portrait,&old,&legacy,NULL,0)==IPROP_OK);
      request=options_ex(UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX); request.flags=UINT32_MAX;
      request.struct_size=4;
      CHECK(iprop_generate_ex(portrait,&request,NULL,&checked,&error)==IPROP_EX_OK && same(legacy,checked)==0);
      iprop_result_free(checked); iprop_result_free(legacy); checked=legacy=NULL;
      CHECK(iprop_generate(portrait,&old7,&legacy,NULL,0)==IPROP_OK);
      request=options_ex(7,UINT32_MAX,UINT32_MAX,UINT32_MAX); request.struct_size=offsetof(iprop_generate_options_ex,variant);
      CHECK(iprop_generate_ex(portrait,&request,NULL,&checked,&error)==IPROP_EX_OK && same(legacy,checked)==0);
      iprop_result_free(checked); iprop_result_free(legacy); checked=legacy=NULL; }

    /* Validated parameter values equal the legacy named overrides. The legacy
     * entry keeps its own policy: unknown names ignored, first duplicate wins. */
    CHECK(iprop_parameters_create(uniforms,&values,NULL)==IPROP_EX_OK);
    { iprop_uniform named[5]; iprop_generate_options old=legacy_options(3,0,0,0);
      iprop_parameter_override_ex list[3]; float ten=10.0f;
      memset(named,0,sizeof(named));
      named[0].name="base"; named[0].type=IPROP_UNIFORM_COLOR; named[0].r=named[0].g=named[0].b=named[0].a=0xff;
      named[1].name="fx"; named[1].type=IPROP_UNIFORM_F32; named[1].f=ten;
      named[2].name="ux"; named[2].type=IPROP_UNIFORM_U32; named[2].u=7;
      named[3].name="ux"; named[3].type=IPROP_UNIFORM_U32; named[3].u=3;
      named[4].name="unknown"; named[4].type=IPROP_UNIFORM_U32; named[4].u=1;
      old.uniforms=named; old.uniform_count=5;
      for(index=0;index<3;++index) CHECK(iprop_parameter_override_ex_init(&list[index],sizeof(list[index]))==IPROP_EX_OK);
      list[0].target=IPROP_PARAMETER_TARGET_NAME; list[0].name.data="base"; list[0].name.byte_count=4;
      list[0].type=IPROP_INPUT_COLOR; list[0].color.r=list[0].color.g=list[0].color.b=list[0].color.a=0xff;
      list[1].target=IPROP_PARAMETER_TARGET_ID; list[1].input_id=1; list[1].type=IPROP_INPUT_F32; list[1].f32=ten;
      list[2].target=IPROP_PARAMETER_TARGET_NAME; list[2].name.data="ux"; list[2].name.byte_count=2;
      list[2].type=IPROP_INPUT_U32; list[2].u32=7;
      CHECK(iprop_parameters_set_overrides(values,list,3,sizeof(list[0]),NULL)==IPROP_EX_OK);
      CHECK(iprop_generate(uniforms,&old,&legacy,NULL,0)==IPROP_OK);
      request=options_ex(3,0,0,0);
      CHECK(iprop_generate_ex(uniforms,&request,values,&checked,&error)==IPROP_EX_OK && same(legacy,checked)==0);
      { iprop_image image; iprop_result_image(checked,&image); CHECK(image.pixels[0]==0xff && image.pixels[3]==0xff); }
      iprop_result_free(checked); checked=NULL;
      /* The default set differs: overrides really came from the handle. */
      CHECK(iprop_parameters_create(uniforms,&foreign,NULL)==IPROP_EX_OK);
      CHECK(iprop_generate_ex(uniforms,&request,foreign,&checked,&error)==IPROP_EX_OK);
      { iprop_image a,b; iprop_result_image(legacy,&a); iprop_result_image(checked,&b);
        CHECK(memcmp(a.pixels,b.pixels,(size_t)a.width*a.height*4)!=0); }
      iprop_result_free(checked); iprop_parameters_free(foreign); checked=NULL; foreign=NULL;

      /* Generation reads the set without changing it; the result is then
       * independent of later mutation, frees and generations. */
      { iprop_parameter_value_ex before[3],after[3]; iprop_image image; uint8_t pixels[8*8*4];
        for(index=0;index<3;++index) {
            CHECK(iprop_parameter_value_ex_init(&before[index],sizeof(before[index]))==IPROP_EX_OK);
            CHECK(iprop_parameter_value_ex_init(&after[index],sizeof(after[index]))==IPROP_EX_OK);
            CHECK(iprop_parameters_get(values,index,&before[index],NULL)==IPROP_EX_OK);
        }
        CHECK(iprop_generate_ex(uniforms,&request,values,&checked,&error)==IPROP_EX_OK);
        for(index=0;index<3;++index) {
            CHECK(iprop_parameters_get(values,index,&after[index],NULL)==IPROP_EX_OK);
            CHECK(memcmp(&before[index],&after[index],sizeof(before[index]))==0);
        }
        iprop_result_image(checked,&image); CHECK(image.width*image.height*4==sizeof(pixels));
        memcpy(pixels,image.pixels,sizeof(pixels));
        CHECK(iprop_parameters_reset(values,NULL)==IPROP_EX_OK);
        /* A success stores a new handle without freeing the previous pointer. */
        previous=checked;
        CHECK(iprop_generate_ex(uniforms,&request,values,&checked,&error)==IPROP_EX_OK && checked!=previous);
        iprop_result_image(previous,&image); CHECK(memcmp(image.pixels,pixels,sizeof(pixels))==0);
        iprop_result_image(checked,&image); CHECK(memcmp(image.pixels,pixels,sizeof(pixels))!=0);
        iprop_parameters_free(values); values=NULL;
        iprop_result_image(previous,&image); CHECK(memcmp(image.pixels,pixels,sizeof(pixels))==0);
        iprop_result_free(checked); iprop_result_free(previous); checked=previous=NULL;
      }
      iprop_result_free(legacy); legacy=NULL;
    }

    /* Argument failures happen before the VM with stable reasons; the legacy
     * entry still reports its VM-setup traps for the same ranges. */
    CHECK(iprop_parameters_create(portrait_again,&foreign,NULL)==IPROP_EX_OK);
    request=options_ex(0,0,0,0);
    CHECK(reject(NULL,&request,NULL,&error,IPROP_EX_E_ARGUMENT,IPROP_REASON_INVALID_ARGUMENT,IPROP_STAGE_ARGUMENTS)==0);
    CHECK(reject(portrait,NULL,NULL,&error,IPROP_EX_E_ARGUMENT,IPROP_REASON_INVALID_ARGUMENT,IPROP_STAGE_ARGUMENTS)==0);
    CHECK(iprop_generate_ex(portrait,&request,NULL,NULL,&error)==IPROP_EX_E_ARGUMENT && error.reason==IPROP_REASON_INVALID_ARGUMENT);
    for(index=0;index<4;++index) { request.struct_size=index;
        CHECK(reject(portrait,&request,NULL,&error,IPROP_EX_E_ARGUMENT,IPROP_REASON_INVALID_ARGUMENT,IPROP_STAGE_ARGUMENTS)==0); }
    request=options_ex(0,0,0,0); request.flags=1;
    CHECK(reject(portrait,&request,NULL,&error,IPROP_EX_E_ARGUMENT,IPROP_REASON_INVALID_ARGUMENT,IPROP_STAGE_ARGUMENTS)==0);
    request=options_ex(0,0,0,0);
    CHECK(reject(portrait,&request,foreign,&error,IPROP_EX_E_ARGUMENT,IPROP_REASON_FOREIGN_PARAMETERS,IPROP_STAGE_ARGUMENTS)==0);
    CHECK(iprop_generate_ex(portrait_again,&request,foreign,&checked,&error)==IPROP_EX_OK);
    iprop_result_free(checked); checked=NULL;
    request=options_ex(0,8,0,0);
    CHECK(reject(portrait,&request,defaults,&error,IPROP_EX_E_ARGUMENT,IPROP_REASON_VARIANT_RANGE,IPROP_STAGE_ARGUMENTS)==0);
    CHECK(strcmp(message,"variant index out of range")==0 && error.location_flags==0);
    { iprop_generate_options old=legacy_options(0,8,0,0); char text[64];
      CHECK(iprop_generate(portrait,&old,&legacy,text,sizeof(text))==IPROP_E_TRAP && legacy==NULL); }
    request=options_ex(0,0,0,0); request.archetype=1;
    CHECK(reject(portrait,&request,NULL,&error,IPROP_EX_E_ARGUMENT,IPROP_REASON_ARCHETYPE_RANGE,IPROP_STAGE_ARGUMENTS)==0);
    request=options_ex(0,0,16385,0);
    CHECK(reject(portrait,&request,NULL,&error,IPROP_EX_E_ARGUMENT,IPROP_REASON_CANVAS_SIZE,IPROP_STAGE_ARGUMENTS)==0);
    /* A real VM trap is E_TRAP at EXECUTE with its function/PC location. */
    request=options_ex(0,0,0,0);
    CHECK(reject(trap,&request,NULL,&error,IPROP_EX_E_TRAP,IPROP_REASON_DIVIDE_BY_ZERO,IPROP_STAGE_EXECUTE)==0);
    CHECK((error.location_flags&(IPROP_LOCATION_FUNCTION|IPROP_LOCATION_PC))==(IPROP_LOCATION_FUNCTION|IPROP_LOCATION_PC));
    CHECK(error.function_index==0 && error.message_required_bytes==strlen(message));
    /* Success clears the previous failure report. */
    CHECK(iprop_generate_ex(portrait,&request,NULL,&checked,&error)==IPROP_EX_OK);
    CHECK(error.status==IPROP_EX_OK && error.reason==IPROP_REASON_NONE && error.location_flags==0 && message[0]=='\0');
    iprop_result_free(checked); checked=NULL;

    /* Error storage inside the output pointer or options, or an output inside
     * the options, is rejected before any write. */
    { struct { iprop_generate_options_ex options; iprop_result* out; } block; iprop_error_ex saved;
      iprop_result* sentinel=(iprop_result*)(void*)&block; unsigned char before[sizeof(block)];
      block.options=options_ex(0,0,0,0); block.out=sentinel; memcpy(before,&block,sizeof(block));
      CHECK(iprop_generate_ex(portrait,&block.options,NULL,&block.out,(iprop_error_ex*)(void*)&block.out)==IPROP_EX_E_ARGUMENT);
      CHECK(memcmp(&block,before,sizeof(block))==0);
      error.message_buffer=(char*)(void*)&block.options.seed; error.message_capacity=4; memcpy(&saved,&error,sizeof(error));
      CHECK(iprop_generate_ex(portrait,&block.options,NULL,&block.out,&error)==IPROP_EX_E_ARGUMENT);
      CHECK(memcmp(&block,before,sizeof(block))==0 && memcmp(&error,&saved,sizeof(error))==0);
      error.message_buffer=message; error.message_capacity=sizeof(message);
      CHECK(iprop_generate_ex(portrait,&block.options,NULL,(iprop_result**)(void*)&block.options.seed,&error)==IPROP_EX_E_ARGUMENT);
      CHECK(memcmp(&block,before,sizeof(block))==0);
    }

    /* Results survive the program, the parameter set and later generations. */
    request=options_ex(26,7,0,0);
    CHECK(iprop_generate_ex(portrait,&request,defaults,&checked,&error)==IPROP_EX_OK);
    { iprop_generate_options old=legacy_options(26,7,0,0);
      CHECK(iprop_generate(portrait_again,&old,&legacy,NULL,0)==IPROP_OK); }
    iprop_parameters_free(defaults); defaults=NULL; iprop_program_free(portrait); portrait=NULL;
    CHECK(iprop_generate_ex(portrait_again,&request,NULL,&previous,&error)==IPROP_EX_OK);
    CHECK(same(legacy,checked)==0 && same(previous,checked)==0);
    iprop_result_free(previous); iprop_result_free(checked); iprop_result_free(legacy);
    iprop_parameters_free(foreign);
    iprop_program_free(uniforms); iprop_program_free(owners); iprop_program_free(trap); iprop_program_free(portrait_again);
    puts("C99 generate_ex: legacy-identical defaults/sizes/uniforms, read-only parameters by ID, pre-VM argument reasons, execute traps, publication, aliases and independent results: PASS");
    return 0;
}
