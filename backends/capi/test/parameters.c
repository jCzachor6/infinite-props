#include "iprop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "parameters: line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static uint32_t bits(const float* f) { uint32_t value; memcpy(&value, f, 4); return value; }
static uint32_t read_u32(const uint8_t* b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static void write_u32(uint8_t* b, uint32_t v) { b[0]=(uint8_t)v; b[1]=(uint8_t)(v>>8); b[2]=(uint8_t)(v>>16); b[3]=(uint8_t)(v>>24); }
static int fixture(const char* path, uint8_t** bytes, size_t* size) {
    FILE* f=fopen(path,"rb"); long n;
    CHECK(f != NULL && fseek(f,0,SEEK_END)==0); n=ftell(f); CHECK(n>0 && fseek(f,0,SEEK_SET)==0);
    *bytes=(uint8_t*)malloc((size_t)n); CHECK(*bytes != NULL && fread(*bytes,1,(size_t)n,f)==(size_t)n);
    CHECK(fclose(f)==0); *size=(size_t)n; return 0;
}
int main(int argc, char** argv) {
    uint8_t *bytes,*empty_bytes; size_t size,empty_size;
    iprop_program *program=NULL,*empty=NULL;
    iprop_parameters *parameters=NULL,*clone=NULL,*empty_parameters=NULL;
    iprop_parameter_value_ex value,got,before;
    iprop_input_info_ex input;
    iprop_library_info_ex library;
    iprop_error_ex error;
    char message[96]; uint32_t count,id,capacity,type;
    const uint32_t integer_defaults[]={UINT32_MAX,UINT32_C(0xfffffff9),UINT32_C(0x80000000),UINT32_C(0x7fffffff)};
    CHECK(argc==3 && fixture(argv[1],&bytes,&size)==0 && fixture(argv[2],&empty_bytes,&empty_size)==0);
    CHECK(iprop_error_ex_init(&error,sizeof(error))==IPROP_EX_OK); error.message_buffer=message; error.message_capacity=sizeof(message);
    CHECK(iprop_program_load_ex(bytes,size,&program,&error)==IPROP_EX_OK);
    CHECK(iprop_program_load_ex(empty_bytes,empty_size,&empty,&error)==IPROP_EX_OK); free(empty_bytes);
    CHECK(iprop_library_info_ex_init(&library,sizeof(library))==IPROP_EX_OK);
    CHECK(iprop_library_query_ex(&library,&error)==IPROP_EX_OK && (library.capabilities&IPROP_CAP_PARAMETERS)!=0);
    CHECK(iprop_parameters_create(program,&parameters,&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_create(empty,&empty_parameters,&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_reset(empty_parameters,&error)==IPROP_EX_OK);
    CHECK(iprop_parameter_value_ex_init(&value,sizeof(value))==IPROP_EX_OK);
    CHECK(iprop_parameter_value_ex_init(&got,sizeof(got))==IPROP_EX_OK);
    CHECK(iprop_input_info_ex_init(&input,sizeof(input))==IPROP_EX_OK);
    CHECK(iprop_program_input_count(program,&count,&error)==IPROP_EX_OK && count==8);
    for(id=0;id<count;++id) {
        CHECK(iprop_program_input_get(program,id,&input,&error)==IPROP_EX_OK);
        CHECK(iprop_parameters_get_named(parameters,input.name,&got,&error)==IPROP_EX_OK && got.type==input.type);
        if(input.type==IPROP_INPUT_COLOR) CHECK(memcmp(&got.color,&input.color_default,4)==0 && bits(&got.f32)==0 && got.u32==0);
        if(input.type==IPROP_INPUT_F32) CHECK(bits(&got.f32)==bits(&input.f32_default) && got.u32==0 && got.color.a==0);
        if(input.type==IPROP_INPUT_U32) CHECK(got.u32==input.u32_default && bits(&got.f32)==0 && got.color.a==0);
        CHECK(iprop_parameters_get(parameters,id,&value,NULL)==IPROP_EX_OK && memcmp(&got,&value,sizeof(value))==0);
    }
    for(id=4;id<8;++id) CHECK(iprop_parameters_get(parameters,id,&got,NULL)==IPROP_EX_OK && got.type==IPROP_INPUT_U32 && got.u32==integer_defaults[id-4]);
    CHECK(iprop_parameters_get(parameters,1,&got,&error)==IPROP_EX_OK && bits(&got.f32)==UINT32_C(0x80000000));

    /* Exact setter allocations omit all inactive trailing fields. Too short
     * values fail without changing the previous value. */
    for(type=0;type<3;++type) for(capacity=0;capacity<=sizeof(value)+16;++capacity) {
        const size_t allocation=capacity<4?4:capacity;
        unsigned char* storage=(unsigned char*)malloc(allocation);
        const uint32_t selected=type==0?0:type==1?1:4;
        const uint32_t required=type==0?16:type==1?20:24;
        const uint32_t raw=type==1?UINT32_C(0x7fc12345):UINT32_MAX;
        CHECK(storage != NULL); memset(storage,0xa5,allocation); memcpy(storage,&capacity,4);
        if(capacity>=8) memset(storage+4,0,4);
        if(capacity>=12) memcpy(storage+offsetof(iprop_parameter_value_ex,type),&type,4);
        if(capacity>=required) {
            if(type==0) memcpy(storage+offsetof(iprop_parameter_value_ex,color),"\x01\x02\x03\x04",4);
            else memcpy(storage+(type==1?offsetof(iprop_parameter_value_ex,f32):offsetof(iprop_parameter_value_ex,u32)),&raw,4);
        }
        CHECK(iprop_parameter_value_ex_init(&before,sizeof(before))==IPROP_EX_OK);
        CHECK(iprop_parameters_get(parameters,selected,&before,NULL)==IPROP_EX_OK);
        CHECK(iprop_parameters_set(parameters,selected,(iprop_parameter_value_ex*)storage,&error)==(capacity<required?IPROP_EX_E_ARGUMENT:IPROP_EX_OK));
        CHECK(iprop_parameters_get(parameters,selected,&got,NULL)==IPROP_EX_OK);
        if(capacity<required) CHECK(memcmp(&got,&before,sizeof(got))==0);
        else if(type==0) CHECK(got.color.r==1 && got.color.g==2 && got.color.b==3 && got.color.a==4);
        else if(type==1) CHECK(bits(&got.f32)==raw && got.color.a==0 && got.u32==0);
        else CHECK(got.u32==raw && bits(&got.f32)==0 && got.color.a==0);
        free(storage);
    }
    /* All exact getter and initializer prefixes preserve partial fields/tail. */
    CHECK(iprop_parameters_get(parameters,1,&got,&error)==IPROP_EX_OK);
    for(capacity=0;capacity<=sizeof(value)+16;++capacity) {
        const size_t allocation=capacity<4?4:capacity;
        unsigned char* storage=(unsigned char*)malloc(allocation);
        unsigned char saved[sizeof(value)+16]; size_t byte,field;
        const size_t offsets[]={offsetof(iprop_parameter_value_ex,type),offsetof(iprop_parameter_value_ex,color),offsetof(iprop_parameter_value_ex,f32),offsetof(iprop_parameter_value_ex,u32)};
        CHECK(storage != NULL); memset(storage,0xa5,allocation); memcpy(storage,&capacity,4);
        if(capacity>=8) memset(storage+4,0,4);
        memcpy(saved,storage,allocation);
        CHECK(iprop_parameters_get(parameters,UINT32_MAX,(iprop_parameter_value_ex*)storage,&error)==IPROP_EX_E_ARGUMENT && memcmp(storage,saved,allocation)==0);
        CHECK(iprop_parameters_get(parameters,1,(iprop_parameter_value_ex*)storage,&error)==(capacity<4?IPROP_EX_E_ARGUMENT:IPROP_EX_OK));
        for(byte=0;byte<allocation;++byte) {
            unsigned char want=saved[byte];
            for(field=0;field<4;++field) if(offsets[field]+4<=capacity && byte>=offsets[field] && byte<offsets[field]+4) want=((const unsigned char*)&got)[byte];
            CHECK(storage[byte]==want);
        }
        memset(storage,0xa5,allocation);
        CHECK(iprop_parameter_value_ex_init((iprop_parameter_value_ex*)storage,capacity)==(capacity<4?IPROP_EX_E_ARGUMENT:IPROP_EX_OK));
        for(byte=0;byte<allocation;++byte) {
            unsigned char want=0xa5;
            if(capacity>=4 && byte<4) want=((const unsigned char*)&capacity)[byte];
            if(capacity>=8 && byte>=4 && byte<8) want=0;
            for(field=0;field<4;++field) if(offsets[field]+4<=capacity && byte>=offsets[field] && byte<offsets[field]+4) want=0;
            CHECK(storage[byte]==want);
        }
        free(storage);
    }
    CHECK(iprop_parameter_value_ex_init(&value,sizeof(value))==IPROP_EX_OK); value.type=IPROP_INPUT_F32;
    { const uint32_t raw=UINT32_C(0x80000000); memcpy(&value.f32,&raw,4); }
    CHECK(iprop_parameters_set_named(parameters,(iprop_utf8_view){"zero",4},&value,&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_clone(parameters,&clone,&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_reset(parameters,&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_get(clone,0,&got,NULL)==IPROP_EX_OK && got.color.r==1);
    CHECK(iprop_parameters_get(parameters,0,&got,NULL)==IPROP_EX_OK && got.color.r==0x12);
    value.type=IPROP_INPUT_U32; value.u32=7;
    CHECK(iprop_parameters_set(parameters,1,&value,&error)==IPROP_EX_E_ARGUMENT && error.reason==IPROP_REASON_UNIFORM_TYPE && error.stage==IPROP_STAGE_ARGUMENTS);
    value.type=UINT32_MAX;
    CHECK(iprop_parameters_set(parameters,1,&value,&error)==IPROP_EX_E_ARGUMENT && error.reason==IPROP_REASON_INVALID_ARGUMENT);
    value.type=IPROP_INPUT_F32; value.flags=1;
    CHECK(iprop_parameters_set(parameters,1,&value,&error)==IPROP_EX_E_ARGUMENT); value.flags=0;
    got.flags=1; memcpy(&before,&got,sizeof(got));
    CHECK(iprop_parameters_get(parameters,1,&got,&error)==IPROP_EX_E_ARGUMENT && memcmp(&got,&before,sizeof(got))==0); got.flags=0;
    CHECK(iprop_parameters_create(NULL,&clone,&error)==IPROP_EX_E_ARGUMENT && clone!=NULL);
    CHECK(iprop_parameters_create(program,NULL,&error)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_parameters_clone(NULL,&clone,&error)==IPROP_EX_E_ARGUMENT && clone!=NULL);
    CHECK(iprop_parameters_clone(parameters,NULL,&error)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_parameters_get(NULL,0,&got,&error)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_parameters_get(parameters,0,NULL,&error)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_parameters_set(NULL,0,&value,&error)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_parameters_set(parameters,0,NULL,&error)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_parameters_reset(NULL,&error)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_parameters_get(empty_parameters,0,&got,&error)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_parameters_get_named(parameters,(iprop_utf8_view){NULL,1},&got,&error)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_parameters_get_named(parameters,(iprop_utf8_view){NULL,0},&got,&error)==IPROP_EX_E_ARGUMENT);
    CHECK(iprop_parameters_set_named(parameters,(iprop_utf8_view){"unknown",7},&value,&error)==IPROP_EX_E_ARGUMENT);

    /* Error writes must preserve value/name inputs as well as query outputs. */
    { iprop_error_ex saved; char name[]="zero"; char old_name[sizeof(name)];
      memcpy(old_name,name,sizeof(name)); memcpy(&before,&value,sizeof(value));
      error.message_buffer=(char*)&value; error.message_capacity=1; memcpy(&saved,&error,sizeof(error));
      CHECK(iprop_parameters_set(parameters,1,&value,&error)==IPROP_EX_E_ARGUMENT);
      CHECK(memcmp(&value,&before,sizeof(value))==0 && memcmp(&error,&saved,sizeof(error))==0);
      CHECK(iprop_parameters_set(parameters,1,&value,(iprop_error_ex*)&value)==IPROP_EX_E_ARGUMENT && memcmp(&value,&before,sizeof(value))==0);
      error.message_buffer=name; memcpy(&saved,&error,sizeof(error));
      CHECK(iprop_parameters_get_named(parameters,(iprop_utf8_view){name,4},&got,&error)==IPROP_EX_E_ARGUMENT);
      CHECK(iprop_parameters_set_named(parameters,(iprop_utf8_view){name,4},&value,&error)==IPROP_EX_E_ARGUMENT);
      CHECK(memcmp(name,old_name,sizeof(name))==0 && memcmp(&error,&saved,sizeof(error))==0);
      error.message_buffer=(char*)parameters;
      CHECK(iprop_parameters_reset(parameters,&error)==IPROP_EX_E_ARGUMENT);
      error.message_buffer=message; error.message_capacity=sizeof(message);
      CHECK(iprop_parameters_get_named(parameters,(iprop_utf8_view){(const char*)&got,4},&got,&error)==IPROP_EX_E_ARGUMENT);
      error.message_buffer=NULL; error.message_capacity=1;
      CHECK(iprop_parameters_reset(parameters,&error)==IPROP_EX_E_ARGUMENT);
      CHECK(iprop_parameters_clone(parameters,&clone,&error)==IPROP_EX_E_ARGUMENT);
      error.message_buffer=message; error.message_capacity=sizeof(message);
    }
    /* Readable name/value inputs may share storage; inactive color is ignored. */
    memcpy(&value.color,"zero",4);
    CHECK(iprop_parameters_set_named(parameters,(iprop_utf8_view){(const char*)&value.color,4},&value,&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_get(parameters,1,&got,NULL)==IPROP_EX_OK && bits(&got.f32)==UINT32_C(0x80000000) && got.color.a==0);
    /* Real executable names, including embedded NUL and an actual empty name. */
    { uint8_t* modified=(uint8_t*)malloc(size); size_t at=0,name_at=0; uint32_t section,strings,const_entry=0;
      const char exact[]={(char)0xc3,(char)0xa9,'\0','x','\0'};
      const char invalid[]={(char)0xc0,(char)0xaf};
      iprop_program* binary=NULL; iprop_parameters* named=NULL;
      iprop_parameter_value_ex named_value;
      CHECK(modified!=NULL && read_u32(bytes+8)==0); memcpy(modified,bytes,size);
      for(section=0;section<read_u32(modified+12);++section)
        if(read_u32(modified+20+section*12)==2) { const_entry=20+section*12; at=read_u32(modified+const_entry+4); }
      CHECK(at!=0 && at+4<=size); strings=read_u32(modified+at); at+=4;
      for(id=0;id<strings;++id) { uint32_t n;
        CHECK(at+4<=size); n=read_u32(modified+at); at+=4; CHECK(n<=size-at);
        if(n==4 && memcmp(modified+at,"tint",4)==0) { CHECK(name_at==0); name_at=at; memcpy(modified+at,exact,4); }
        at+=n;
      }
      CHECK(name_at!=0 && iprop_program_load_ex(modified,size,&binary,&error)==IPROP_EX_OK);
      CHECK(iprop_parameters_create(binary,&named,&error)==IPROP_EX_OK); iprop_program_free(binary); binary=NULL;
      CHECK(iprop_parameters_get_named(named,(iprop_utf8_view){exact,4},&got,&error)==IPROP_EX_OK && got.color.r==0x12);
      memcpy(&before,&got,sizeof(got));
      CHECK(iprop_parameters_get_named(named,(iprop_utf8_view){exact,2},&got,&error)==IPROP_EX_E_ARGUMENT && memcmp(&got,&before,sizeof(got))==0);
      CHECK(iprop_parameters_get_named(named,(iprop_utf8_view){exact,3},&got,&error)==IPROP_EX_E_ARGUMENT);
      CHECK(iprop_parameters_get_named(named,(iprop_utf8_view){exact,5},&got,&error)==IPROP_EX_E_ARGUMENT);
      CHECK(iprop_parameters_get_named(named,(iprop_utf8_view){invalid,2},&got,&error)==IPROP_EX_E_ARGUMENT);
      CHECK(iprop_parameter_value_ex_init(&named_value,sizeof(named_value))==IPROP_EX_OK);
      named_value.type=IPROP_INPUT_COLOR; named_value.color=(iprop_color_rgba8){9,8,7,6};
      CHECK(iprop_parameters_set_named(named,(iprop_utf8_view){exact,4},&named_value,&error)==IPROP_EX_OK);
      named_value.color.r=11;
      CHECK(iprop_parameters_set_named(named,(iprop_utf8_view){exact,4},&named_value,&error)==IPROP_EX_OK);
      CHECK(iprop_parameters_get(named,0,&got,&error)==IPROP_EX_OK && got.color.r==11);
      CHECK(iprop_parameters_set_named(named,(iprop_utf8_view){invalid,2},&named_value,&error)==IPROP_EX_E_ARGUMENT);
      CHECK(iprop_parameters_get(named,0,&got,&error)==IPROP_EX_OK && got.color.r==11);
      iprop_parameters_free(named); named=NULL;
      write_u32(modified+name_at-4,0); memmove(modified+name_at,modified+name_at+4,size-name_at-4);
      write_u32(modified+const_entry+8,read_u32(modified+const_entry+8)-4);
      for(section=0;section<read_u32(modified+12);++section) {
        const size_t entry=20+section*12;
        if(read_u32(modified+entry+4)>name_at) write_u32(modified+entry+4,read_u32(modified+entry+4)-4);
      }
      CHECK(iprop_program_load_ex(modified,size-4,&binary,&error)==IPROP_EX_OK);
      CHECK(iprop_parameters_create(binary,&named,&error)==IPROP_EX_OK); iprop_program_free(binary); free(modified);
      CHECK(iprop_parameters_get_named(named,(iprop_utf8_view){NULL,0},&got,&error)==IPROP_EX_OK && got.color.r==0x12);
      CHECK(iprop_parameters_set_named(named,(iprop_utf8_view){NULL,0},&named_value,&error)==IPROP_EX_OK);
      CHECK(iprop_parameters_get_named(named,(iprop_utf8_view){"nonempty",0},&got,&error)==IPROP_EX_OK && got.color.r==11);
      iprop_parameters_free(named);
    }
    /* Retained data supports exact lookup after the original handle is freed. */
    iprop_program_free(program); program=NULL; memset(bytes,0,size); free(bytes);
    CHECK(iprop_parameters_get_named(parameters,(iprop_utf8_view){"zero",4},&got,&error)==IPROP_EX_OK && bits(&got.f32)==UINT32_C(0x80000000));
    CHECK(error.status==IPROP_EX_OK && message[0]=='\0');
    CHECK(iprop_parameters_set_named(clone,(iprop_utf8_view){"zero",4},&value,NULL)==IPROP_EX_OK);
    iprop_parameters_free(parameters);
    CHECK(iprop_parameters_get_named(clone,(iprop_utf8_view){"tint",4},&got,NULL)==IPROP_EX_OK && got.color.r==1);
    CHECK(iprop_parameters_reset(clone,&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_get(clone,0,&got,NULL)==IPROP_EX_OK && got.color.r==0x12);
    iprop_parameters_free(clone); iprop_parameters_free(empty_parameters); iprop_parameters_free(NULL); iprop_program_free(empty);
    puts("C99 retained parameters: exact defaults/setter prefixes/getter bounds, clone/reset, strict types/names, alias transactions and lifetime: PASS");
    return 0;
}
