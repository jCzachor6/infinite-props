#include "iprop.h"
#include "check.h"

int main(int argc, char** argv) {
    uint8_t* bytes; size_t size;
    iprop_program* program=NULL;
    iprop_parameters* parameters=NULL;
    iprop_parameter_value_ex got,before,all_before[8],all_after[8];
    iprop_input_info_ex input;
    iprop_library_info_ex library;
    iprop_error_ex error; char message[96]; uint32_t id,index;
    const uint32_t f32_bits[]={UINT32_C(0x80000000),1,UINT32_C(0x007fffff),
        UINT32_C(0x7fc12345),UINT32_C(0xffc54321),UINT32_C(0x7f800001),
        UINT32_C(0xff800001),UINT32_C(0x7f800000),UINT32_C(0xff800000)};
    const uint32_t unsigned_values[]={0,UINT32_MAX,UINT32_C(0x80000000),UINT32_C(0x7fffffff)};
    const int32_t signed_values[]={INT32_MIN,INT32_MAX,-7,-1,0};
    const iprop_color_rgba8 color={1,2,3,4};
    CHECK(argc==2 && read_fixture(argv[1],&bytes,&size)==0);
    CHECK(iprop_program_load_ex(bytes,size,&program,NULL)==IPROP_EX_OK);
    CHECK(iprop_parameters_create(program,&parameters,NULL)==IPROP_EX_OK);
    CHECK(iprop_parameter_value_ex_init(&got,sizeof(got))==IPROP_EX_OK);
    CHECK(iprop_input_info_ex_init(&input,sizeof(input))==IPROP_EX_OK);
    CHECK(iprop_library_info_ex_init(&library,sizeof(library))==IPROP_EX_OK);
    CHECK(iprop_library_query(&library)==IPROP_EX_OK && (library.capabilities&IPROP_CAP_PARAMETERS_TYPED)!=0);
    CHECK(IPROP_INPUT_COLOR==0 && IPROP_INPUT_F32==1 && IPROP_INPUT_U32==2);
    CHECK(iprop_error_ex_init(&error,sizeof(error))==IPROP_EX_OK); error.message_buffer=message; error.message_capacity=sizeof(message);

    /* Both routes use the same resolved declaration and copy every binary32
     * bit. No source descriptor or inactive-field initialization is required. */
    for(index=0;index<sizeof(f32_bits)/sizeof(f32_bits[0]);++index) {
        float f; memcpy(&f,&f32_bits[index],4);
        CHECK(iprop_parameters_set_f32(parameters,1,f,&error)==IPROP_EX_OK);
        CHECK(iprop_parameters_get(parameters,1,&got,NULL)==IPROP_EX_OK && float_bits(&got.f32)==f32_bits[index]);
        CHECK(got.type==IPROP_INPUT_F32 && got.color.a==0 && got.u32==0);
        CHECK(iprop_parameters_set_f32_named(parameters,(iprop_utf8_view){"zero",4},f,&error)==IPROP_EX_OK);
        CHECK(iprop_parameters_get(parameters,1,&got,NULL)==IPROP_EX_OK && float_bits(&got.f32)==f32_bits[index]);
        CHECK(error.status==IPROP_EX_OK && error.reason==IPROP_REASON_NONE && error.message_required_bytes==0 && message[0]=='\0');
    }
    for(index=0;index<sizeof(unsigned_values)/sizeof(unsigned_values[0]);++index) {
        CHECK(iprop_parameters_set_u32(parameters,4,unsigned_values[index],&error)==IPROP_EX_OK);
        CHECK(iprop_parameters_set_u32_named(parameters,(iprop_utf8_view){"high",4},unsigned_values[index],&error)==IPROP_EX_OK);
        CHECK(iprop_parameters_get(parameters,4,&got,NULL)==IPROP_EX_OK && got.u32==unsigned_values[index] && got.type==IPROP_INPUT_U32);
    }
    for(index=0;index<sizeof(signed_values)/sizeof(signed_values[0]);++index) {
        uint32_t expected; memcpy(&expected,&signed_values[index],4);
        CHECK(iprop_parameters_set_i32(parameters,5,signed_values[index],&error)==IPROP_EX_OK);
        CHECK(iprop_parameters_get(parameters,5,&got,NULL)==IPROP_EX_OK && got.u32==expected);
        CHECK(iprop_parameters_set_i32_named(parameters,(iprop_utf8_view){"negative",8},signed_values[index],&error)==IPROP_EX_OK);
        CHECK(iprop_parameters_get(parameters,5,&got,NULL)==IPROP_EX_OK && got.u32==expected);
        /* Binary U32 carries both source spellings; the signed helper also
         * works on a source u32, without guessing its original signedness. */
        CHECK(iprop_parameters_set_i32(parameters,4,signed_values[index],NULL)==IPROP_EX_OK);
        CHECK(iprop_parameters_get(parameters,4,&got,NULL)==IPROP_EX_OK && got.u32==expected);
    }
    CHECK(iprop_parameters_set_color(parameters,0,color,&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_set_color_named(parameters,(iprop_utf8_view){"tint",4},color,&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_get(parameters,0,&got,NULL)==IPROP_EX_OK && memcmp(&got.color,&color,4)==0 && float_bits(&got.f32)==0 && got.u32==0);

    /* Every rejected request preserves all parameter values, with the same
     * typed reason as generic P.1; success clears a prior failure report. */
    for(id=0;id<8;++id) {
        CHECK(iprop_parameter_value_ex_init(&all_before[id],sizeof(got))==IPROP_EX_OK);
        CHECK(iprop_parameter_value_ex_init(&all_after[id],sizeof(got))==IPROP_EX_OK);
        CHECK(iprop_parameters_get(parameters,id,&all_before[id],NULL)==IPROP_EX_OK);
    }
#define MISMATCH(call) CHECK((call)==IPROP_EX_E_ARGUMENT && error.reason==IPROP_REASON_UNIFORM_TYPE && error.stage==IPROP_STAGE_ARGUMENTS)
    MISMATCH(iprop_parameters_set_f32(parameters,0,1.0f,&error));
    MISMATCH(iprop_parameters_set_u32(parameters,1,7,&error));
    MISMATCH(iprop_parameters_set_i32(parameters,1,-7,&error));
    MISMATCH(iprop_parameters_set_color(parameters,4,color,&error));
    MISMATCH(iprop_parameters_set_f32_named(parameters,(iprop_utf8_view){"tint",4},1.0f,&error));
    MISMATCH(iprop_parameters_set_u32_named(parameters,(iprop_utf8_view){"zero",4},7,&error));
    MISMATCH(iprop_parameters_set_i32_named(parameters,(iprop_utf8_view){"zero",4},-7,&error));
    MISMATCH(iprop_parameters_set_color_named(parameters,(iprop_utf8_view){"high",4},color,&error));
#undef MISMATCH
#define INVALID(call) CHECK((call)==IPROP_EX_E_ARGUMENT && error.reason==IPROP_REASON_INVALID_ARGUMENT && error.stage==IPROP_STAGE_ARGUMENTS)
    INVALID(iprop_parameters_set_f32(NULL,1,0.0f,&error));
    INVALID(iprop_parameters_set_u32(NULL,4,0,&error));
    INVALID(iprop_parameters_set_i32(NULL,5,0,&error));
    INVALID(iprop_parameters_set_color(NULL,0,color,&error));
    INVALID(iprop_parameters_set_f32_named(NULL,(iprop_utf8_view){"zero",4},0.0f,&error));
    INVALID(iprop_parameters_set_u32_named(NULL,(iprop_utf8_view){"high",4},0,&error));
    INVALID(iprop_parameters_set_i32_named(NULL,(iprop_utf8_view){"negative",8},0,&error));
    INVALID(iprop_parameters_set_color_named(NULL,(iprop_utf8_view){"tint",4},color,&error));
    INVALID(iprop_parameters_set_f32_named(parameters,(iprop_utf8_view){NULL,1},0.0f,&error));
    INVALID(iprop_parameters_set_u32_named(parameters,(iprop_utf8_view){"\xc0\xaf",2},0,&error));
#undef INVALID
    /* Well-formed IDs/names absent from the root inputs have their own cause. */
#define UNKNOWN(call) CHECK((call)==IPROP_EX_E_ARGUMENT && error.reason==IPROP_REASON_UNKNOWN_PARAMETER && error.stage==IPROP_STAGE_ARGUMENTS)
    UNKNOWN(iprop_parameters_set_f32(parameters,UINT32_MAX,0.0f,&error));
    UNKNOWN(iprop_parameters_set_u32(parameters,8,0,&error));
    UNKNOWN(iprop_parameters_set_i32(parameters,UINT32_MAX,0,&error));
    UNKNOWN(iprop_parameters_set_color(parameters,UINT32_MAX,color,&error));
    UNKNOWN(iprop_parameters_set_f32_named(parameters,(iprop_utf8_view){"unknown",7},0.0f,&error));
    UNKNOWN(iprop_parameters_set_u32_named(parameters,(iprop_utf8_view){"unknown",7},0,&error));
    UNKNOWN(iprop_parameters_set_i32_named(parameters,(iprop_utf8_view){"unknown",7},0,&error));
    UNKNOWN(iprop_parameters_set_color_named(parameters,(iprop_utf8_view){"unknown",7},color,&error));
    UNKNOWN(iprop_parameters_set_i32_named(parameters,(iprop_utf8_view){NULL,0},0,&error));
    UNKNOWN(iprop_parameters_set_color_named(parameters,(iprop_utf8_view){"",0},color,&error));
    UNKNOWN(iprop_parameters_get(parameters,8,&got,&error));
    UNKNOWN(iprop_parameters_get_named(parameters,(iprop_utf8_view){"Tint",4},&got,&error));
#undef UNKNOWN
    for(id=0;id<8;++id) CHECK(iprop_parameters_get(parameters,id,&all_after[id],NULL)==IPROP_EX_OK && memcmp(&all_after[id],&all_before[id],sizeof(got))==0);
    CHECK(iprop_parameters_set_color(parameters,0,color,&error)==IPROP_EX_OK && error.status==IPROP_EX_OK && error.location_flags==0 && message[0]=='\0');

    /* Exact input-name bytes are protected through generic named preflight. */
    { char name[]="zero"; iprop_error_ex saved; memcpy(&before,&all_before[1],sizeof(got));
      error.message_buffer=name; error.message_capacity=1; memcpy(&saved,&error,sizeof(error));
      CHECK(iprop_parameters_set_f32_named(parameters,(iprop_utf8_view){name,4},0.0f,&error)==IPROP_EX_E_ARGUMENT);
      CHECK(strcmp(name,"zero")==0 && memcmp(&error,&saved,sizeof(error))==0);
      CHECK(iprop_parameters_get(parameters,1,&got,NULL)==IPROP_EX_OK && memcmp(&got,&before,sizeof(got))==0);
      error.message_buffer=message; error.message_capacity=sizeof(message);
    }
    { uint32_t capacity; for(capacity=0;capacity<4;++capacity) {
        error.struct_size=capacity;
        CHECK(iprop_parameters_set_color(parameters,0,(iprop_color_rgba8){9,8,7,6},&error)==IPROP_EX_E_ARGUMENT);
        CHECK(iprop_parameters_get(parameters,0,&got,NULL)==IPROP_EX_OK && memcmp(&got.color,&color,4)==0);
      } error.struct_size=sizeof(error);
      error.message_buffer=NULL; error.message_capacity=1;
      CHECK(iprop_parameters_set_color(parameters,0,(iprop_color_rgba8){9,8,7,6},&error)==IPROP_EX_E_ARGUMENT);
      CHECK(iprop_parameters_get(parameters,0,&got,NULL)==IPROP_EX_OK && memcmp(&got.color,&color,4)==0);
      error.message_buffer=message; error.message_capacity=sizeof(message);
    }
    /* An actual binary name with UTF-8 and embedded NUL resolves only at the
     * exact byte count; source program lifetime does not constrain setters. */
    { uint32_t section,strings; size_t at=0; const char exact[]={'\xc3','\xa9','\0','x','\0'};
      iprop_program* named_program=NULL; iprop_parameters* named=NULL;
      CHECK(read_u32(bytes+8)==0);
      for(section=0;section<read_u32(bytes+12);++section) if(read_u32(bytes+20+section*12)==2) at=read_u32(bytes+24+section*12);
      CHECK(at!=0); strings=read_u32(bytes+at); at+=4;
      for(index=0;index<strings;++index) { uint32_t n=read_u32(bytes+at); at+=4;
        if(n==4 && memcmp(bytes+at,"tint",4)==0) memcpy(bytes+at,exact,4);
        at+=n;
      }
      CHECK(iprop_program_load_ex(bytes,size,&named_program,NULL)==IPROP_EX_OK);
      CHECK(iprop_parameters_create(named_program,&named,NULL)==IPROP_EX_OK); iprop_program_free(named_program);
      CHECK(iprop_parameters_set_color_named(named,(iprop_utf8_view){exact,4},color,&error)==IPROP_EX_OK);
      CHECK(iprop_parameters_get(named,0,&got,NULL)==IPROP_EX_OK && memcmp(&got.color,&color,4)==0);
      CHECK(iprop_parameters_set_color_named(named,(iprop_utf8_view){exact,2},(iprop_color_rgba8){9,8,7,6},&error)==IPROP_EX_E_ARGUMENT);
      CHECK(iprop_parameters_set_color_named(named,(iprop_utf8_view){exact,5},(iprop_color_rgba8){9,8,7,6},&error)==IPROP_EX_E_ARGUMENT);
      CHECK(iprop_parameters_get(named,0,&got,NULL)==IPROP_EX_OK && memcmp(&got.color,&color,4)==0);
      iprop_parameters_free(named);
    }
    iprop_program_free(program); free(bytes);
    CHECK(iprop_parameters_set_i32_named(parameters,(iprop_utf8_view){"minimum",7},INT32_MIN,&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_get(parameters,6,&got,NULL)==IPROP_EX_OK && got.u32==UINT32_C(0x80000000));
    CHECK(iprop_parameters_set_u32_named(parameters,(iprop_utf8_view){"maximum",7},UINT32_C(0x7fffffff),&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_reset(parameters,&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_get(parameters,1,&got,NULL)==IPROP_EX_OK && float_bits(&got.f32)==UINT32_C(0x80000000));
    CHECK(iprop_parameters_get(parameters,5,&got,NULL)==IPROP_EX_OK && got.u32==UINT32_C(0xfffffff9));
    iprop_parameters_free(parameters);
    puts("C99 typed parameter setters: exact f32/U32/i32/color values, strict ID/name/type validation, transactions/errors and retained lifetime: PASS");
    return 0;
}
