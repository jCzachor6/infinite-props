#include "iprop.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "parameter overrides: line %d: %s\n", __LINE__, #c); return 1; } } while (0)
#define INPUTS 8
#define NO_INDEX UINT32_MAX

static uint32_t bits(const float* value) { uint32_t result; memcpy(&result, value, 4); return result; }
static int fixture(const char* path, uint8_t** bytes, size_t* size) {
    FILE* f=fopen(path,"rb"); long n;
    CHECK(f != NULL && fseek(f,0,SEEK_END)==0); n=ftell(f); CHECK(n>0 && fseek(f,0,SEEK_SET)==0);
    *bytes=(uint8_t*)malloc((size_t)n); CHECK(*bytes != NULL && fread(*bytes,1,(size_t)n,f)==(size_t)n);
    CHECK(fclose(f)==0); *size=(size_t)n; return 0;
}

/* Unselected selectors and inactive values hold unreadable garbage: a name
 * count far beyond its two-byte literal, or a nonexistent input ID. */
static iprop_parameter_override_ex by_id(uint32_t id, iprop_input_type type, uint32_t raw) {
    iprop_parameter_override_ex o; const uint32_t garbage=UINT32_C(0xa5a5a5a5);
    (void)iprop_parameter_override_ex_init(&o,sizeof(o));
    o.target=IPROP_PARAMETER_TARGET_ID; o.input_id=id; o.name.data="x"; o.name.byte_count=1000; o.type=type;
    memcpy(&o.color,&garbage,4); memcpy(&o.f32,&garbage,4); o.u32=garbage;
    if(type==IPROP_INPUT_COLOR) memcpy(&o.color,&raw,4); else if(type==IPROP_INPUT_F32) memcpy(&o.f32,&raw,4); else o.u32=raw;
    return o;
}
static iprop_parameter_override_ex by_name(const char* name, iprop_input_type type, uint32_t raw) {
    iprop_parameter_override_ex o=by_id(UINT32_MAX,type,raw);
    o.target=IPROP_PARAMETER_TARGET_NAME; o.name.data=name; o.name.byte_count=(uint32_t)strlen(name);
    return o;
}
static int snapshot(const iprop_parameters* parameters, iprop_parameter_value_ex out[INPUTS]) {
    uint32_t id;
    for(id=0;id<INPUTS;++id) {
        CHECK(iprop_parameter_value_ex_init(&out[id],sizeof(out[id]))==IPROP_EX_OK);
        CHECK(iprop_parameters_get(parameters,id,&out[id],NULL)==IPROP_EX_OK);
    }
    return 0;
}
/* A rejected list reports one stable cause, names the failing element when it
 * has one, and leaves every value unchanged, including earlier valid entries. */
static int reject(iprop_parameters* parameters, const void* list, uint32_t count, uint32_t stride,
                  iprop_error_ex* error, iprop_error_reason reason, uint32_t index) {
    iprop_parameter_value_ex before[INPUTS],after[INPUTS]; char prefix[48];
    CHECK(snapshot(parameters,before)==0);
    CHECK(iprop_parameters_set_overrides(parameters,(const iprop_parameter_override_ex*)list,count,stride,error)==IPROP_EX_E_ARGUMENT);
    CHECK(error->status==IPROP_EX_E_ARGUMENT && error->reason==reason && error->stage==IPROP_STAGE_ARGUMENTS);
    CHECK(error->location_flags==0 && error->message_required_bytes==strlen(error->message_buffer));
    snprintf(prefix,sizeof(prefix),"parameter override %u: ",(unsigned)index);
    CHECK((strncmp(error->message_buffer,prefix,strlen(prefix))==0)==(index!=NO_INDEX));
    CHECK(snapshot(parameters,after)==0 && memcmp(before,after,sizeof(before))==0);
    return 0;
}

int main(int argc, char** argv) {
    uint8_t* bytes; size_t size;
    iprop_program* program=NULL;
    iprop_parameters *parameters=NULL,*clone=NULL;
    iprop_parameter_value_ex defaults[INPUTS],got[INPUTS];
    iprop_parameter_override_ex list[6],single;
    iprop_library_info_ex library;
    iprop_error_ex error,saved; char message[128];
    const uint32_t nan=UINT32_C(0x7fc12345),negative_zero=UINT32_C(0x80000000),one=UINT32_C(0x3f800000);
    const iprop_color_rgba8 color={1,2,3,4}; uint32_t raw_color,id;
    const size_t f32_end=offsetof(iprop_parameter_override_ex,f32)+4;
    const size_t color_end=offsetof(iprop_parameter_override_ex,color)+4;
    memcpy(&raw_color,&color,4);
    CHECK(argc==2 && fixture(argv[1],&bytes,&size)==0);
    CHECK(iprop_program_load_ex(bytes,size,&program,NULL)==IPROP_EX_OK);
    CHECK(iprop_parameters_create(program,&parameters,NULL)==IPROP_EX_OK);
    CHECK(iprop_library_info_ex_init(&library,sizeof(library))==IPROP_EX_OK);
    CHECK(iprop_library_query(&library)==IPROP_EX_OK && (library.capabilities&IPROP_CAP_PARAMETER_OVERRIDES)!=0);
    CHECK(IPROP_PARAMETER_TARGET_ID==1 && IPROP_PARAMETER_TARGET_NAME==2);
    CHECK(IPROP_REASON_UNKNOWN_PARAMETER==4005 && IPROP_REASON_DUPLICATE_PARAMETER==4006 && IPROP_REASON_FOREIGN_PARAMETERS==4007);
    CHECK(iprop_error_ex_init(&error,sizeof(error))==IPROP_EX_OK); error.message_buffer=message; error.message_capacity=sizeof(message);
    CHECK(snapshot(parameters,defaults)==0);

    /* Initialization honors the caller capacity and never writes a partial field. */
    { unsigned char storage[sizeof(iprop_parameter_override_ex)+8],expected[sizeof(storage)];
      const size_t name=offsetof(iprop_parameter_override_ex,name);
      const struct { size_t offset,width; } fields[]={
          {offsetof(iprop_parameter_override_ex,flags),4},{offsetof(iprop_parameter_override_ex,target),4},
          {offsetof(iprop_parameter_override_ex,input_id),4},{name+offsetof(iprop_utf8_view,data),sizeof(const char*)},
          {name+offsetof(iprop_utf8_view,byte_count),4},{offsetof(iprop_parameter_override_ex,type),4},
          {offsetof(iprop_parameter_override_ex,color),4},{offsetof(iprop_parameter_override_ex,f32),4},
          {offsetof(iprop_parameter_override_ex,u32),4}};
      uint32_t capacity; size_t n;
      CHECK(iprop_parameter_override_ex_init(NULL,sizeof(single))==IPROP_EX_E_ARGUMENT);
      for(capacity=0;capacity<=sizeof(storage);++capacity) {
          memset(storage,0xcd,sizeof(storage)); memset(expected,0xcd,sizeof(expected));
          CHECK(iprop_parameter_override_ex_init((iprop_parameter_override_ex*)(void*)storage,capacity)==
                (capacity<4?IPROP_EX_E_ARGUMENT:IPROP_EX_OK));
          if(capacity>=4) {
              memcpy(expected,&capacity,4);
              for(n=0;n<sizeof(fields)/sizeof(fields[0]);++n)
                  if(fields[n].offset+fields[n].width<=capacity) memset(expected+fields[n].offset,0,fields[n].width);
          }
          CHECK(memcmp(storage,expected,sizeof(storage))==0);
      }
    }

    /* A mixed ID/name list sets all three binary types exactly, including NaN
     * payload, negative zero, all RGBA channels and a signed i32 bit pattern. */
    list[0]=by_id(1,IPROP_INPUT_F32,nan);
    list[1]=by_name("high",IPROP_INPUT_U32,7);
    list[2]=by_name("tint",IPROP_INPUT_COLOR,raw_color);
    list[3]=by_id(5,IPROP_INPUT_U32,UINT32_MAX);
    list[4]=by_name("fraction",IPROP_INPUT_F32,negative_zero);
    CHECK(iprop_parameters_set_overrides(parameters,list,5,sizeof(list[0]),&error)==IPROP_EX_OK);
    CHECK(error.status==IPROP_EX_OK && error.reason==IPROP_REASON_NONE && error.message_required_bytes==0 && message[0]=='\0');
    CHECK(snapshot(parameters,got)==0);
    CHECK(got[1].type==IPROP_INPUT_F32 && bits(&got[1].f32)==nan && got[1].u32==0);
    CHECK(got[4].u32==7 && got[5].u32==UINT32_MAX && bits(&got[2].f32)==negative_zero);
    CHECK(memcmp(&got[0].color,&color,4)==0 && bits(&got[0].f32)==0);
    for(id=3;id<INPUTS;id+=3) CHECK(memcmp(&got[id],&defaults[id],sizeof(got[id]))==0);
    CHECK(memcmp(&got[7],&defaults[7],sizeof(got[7]))==0);

    /* Count zero reads nothing; separate lists and setters update repeatedly. */
    CHECK(iprop_parameters_set_overrides(parameters,NULL,0,0,&error)==IPROP_EX_OK);
    CHECK(iprop_parameters_set_overrides(parameters,list,0,1,&error)==IPROP_EX_OK);
    CHECK(snapshot(parameters,defaults)==0 && memcmp(defaults,got,sizeof(got))==0);
    single=by_name("zero",IPROP_INPUT_F32,one);
    CHECK(iprop_parameters_set_overrides(parameters,&single,1,sizeof(single),&error)==IPROP_EX_OK);
    CHECK(snapshot(parameters,got)==0 && bits(&got[1].f32)==one);
    single=by_id(1,IPROP_INPUT_F32,negative_zero);
    CHECK(iprop_parameters_set_overrides(parameters,&single,1,sizeof(single),&error)==IPROP_EX_OK);
    CHECK(snapshot(parameters,got)==0 && bits(&got[1].f32)==negative_zero);
    { iprop_parameter_value_ex value; float two=2.0f;
      CHECK(iprop_parameter_value_ex_init(&value,sizeof(value))==IPROP_EX_OK); value.type=IPROP_INPUT_F32; value.f32=two;
      CHECK(iprop_parameters_set(parameters,1,&value,&error)==IPROP_EX_OK);
      CHECK(iprop_parameters_set(parameters,1,&value,&error)==IPROP_EX_OK);
      CHECK(snapshot(parameters,got)==0 && bits(&got[1].f32)==bits(&two)); }
    CHECK(iprop_parameters_set_overrides(parameters,&single,1,sizeof(single),&error)==IPROP_EX_OK);
    CHECK(snapshot(parameters,got)==0 && bits(&got[1].f32)==negative_zero);

    /* One list cannot name an input twice, by ID, by name or mixed; the
     * duplicate is reported before its value type is considered. */
    list[0]=by_id(1,IPROP_INPUT_F32,one); list[1]=by_id(1,IPROP_INPUT_F32,one);
    CHECK(reject(parameters,list,2,sizeof(list[0]),&error,IPROP_REASON_DUPLICATE_PARAMETER,1)==0);
    list[1]=by_name("zero",IPROP_INPUT_F32,one);
    CHECK(reject(parameters,list,2,sizeof(list[0]),&error,IPROP_REASON_DUPLICATE_PARAMETER,1)==0);
    list[0]=by_name("high",IPROP_INPUT_U32,1); list[1]=by_id(2,IPROP_INPUT_F32,one); list[2]=by_name("high",IPROP_INPUT_U32,2);
    CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_DUPLICATE_PARAMETER,2)==0);
    list[2]=by_name("high",IPROP_INPUT_F32,one);
    CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_DUPLICATE_PARAMETER,2)==0);

    /* Unknown well-formed targets, malformed views and type errors fail after
     * earlier valid elements without applying any of them. */
    list[0]=by_id(1,IPROP_INPUT_F32,one); list[1]=by_name("high",IPROP_INPUT_U32,1);
    { const uint32_t unknown_ids[]={INPUTS,UINT32_MAX}; const char* unknown_names[]={"Zero","zero ","","high\0"};
      const uint32_t unknown_counts[]={4,5,0,5}; size_t n;
      for(n=0;n<2;++n) { list[2]=by_id(unknown_ids[n],IPROP_INPUT_U32,1);
          CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_UNKNOWN_PARAMETER,2)==0); }
      for(n=0;n<4;++n) { list[2]=by_name("x",IPROP_INPUT_U32,1); list[2].name.data=unknown_names[n]; list[2].name.byte_count=unknown_counts[n];
          CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_UNKNOWN_PARAMETER,2)==0); }
      list[2].name.data=NULL; list[2].name.byte_count=0;
      CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_UNKNOWN_PARAMETER,2)==0);
      list[2].name.byte_count=1;
      CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_INVALID_ARGUMENT,2)==0);
      list[2].name.data="\xc0\xaf"; list[2].name.byte_count=2;
      CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_INVALID_ARGUMENT,2)==0);
    }
    list[2]=by_id(0,IPROP_INPUT_F32,one);
    CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_UNIFORM_TYPE,2)==0);
    list[2]=by_name("negative",IPROP_INPUT_COLOR,raw_color);
    CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_UNIFORM_TYPE,2)==0);
    { const uint32_t invalid_types[]={3,UINT32_MAX}; const uint32_t invalid_targets[]={0,3,UINT32_MAX}; size_t n;
      for(n=0;n<2;++n) { list[2]=by_id(4,IPROP_INPUT_U32,1); list[2].type=invalid_types[n];
          CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_INVALID_ARGUMENT,2)==0); }
      for(n=0;n<3;++n) { list[2]=by_id(4,IPROP_INPUT_U32,1); list[2].target=invalid_targets[n];
          CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_INVALID_ARGUMENT,2)==0); }
    }
    list[2]=by_id(4,IPROP_INPUT_U32,1); list[2].flags=1;
    CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_INVALID_ARGUMENT,2)==0);
    /* Every incomplete required prefix and every impossible element size. */
    { const struct { iprop_parameter_target target; iprop_input_type type; size_t end; } shapes[]={
          {IPROP_PARAMETER_TARGET_ID,IPROP_INPUT_U32,offsetof(iprop_parameter_override_ex,target)+3},
          {IPROP_PARAMETER_TARGET_ID,IPROP_INPUT_U32,offsetof(iprop_parameter_override_ex,input_id)+3},
          {IPROP_PARAMETER_TARGET_NAME,IPROP_INPUT_U32,offsetof(iprop_parameter_override_ex,name)+offsetof(iprop_utf8_view,byte_count)+3},
          {IPROP_PARAMETER_TARGET_ID,IPROP_INPUT_U32,offsetof(iprop_parameter_override_ex,type)+3},
          {IPROP_PARAMETER_TARGET_ID,IPROP_INPUT_COLOR,offsetof(iprop_parameter_override_ex,color)+3},
          {IPROP_PARAMETER_TARGET_ID,IPROP_INPUT_F32,offsetof(iprop_parameter_override_ex,f32)+3},
          {IPROP_PARAMETER_TARGET_ID,IPROP_INPUT_U32,offsetof(iprop_parameter_override_ex,u32)+3}};
      size_t n; uint32_t small;
      for(n=0;n<sizeof(shapes)/sizeof(shapes[0]);++n) {
          list[2]=shapes[n].target==IPROP_PARAMETER_TARGET_NAME ? by_name("tint",shapes[n].type,raw_color) :
              by_id(shapes[n].type==IPROP_INPUT_COLOR?0:shapes[n].type==IPROP_INPUT_F32?2:4,shapes[n].type,raw_color);
          list[2].struct_size=(uint32_t)shapes[n].end;
          CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_INVALID_ARGUMENT,2)==0);
      }
      for(small=0;small<4;++small) { list[2]=by_id(4,IPROP_INPUT_U32,1); list[2].struct_size=small;
          CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_INVALID_ARGUMENT,2)==0); }
      list[2]=by_id(4,IPROP_INPUT_U32,1); list[2].struct_size=sizeof(list[0])+1;
      CHECK(reject(parameters,list,3,sizeof(list[0]),&error,IPROP_REASON_INVALID_ARGUMENT,2)==0);
      for(small=0;small<4;++small)
          CHECK(reject(parameters,list,1,small,&error,IPROP_REASON_INVALID_ARGUMENT,NO_INDEX)==0);
    }
    CHECK(reject(parameters,NULL,1,sizeof(list[0]),&error,IPROP_REASON_INVALID_ARGUMENT,NO_INDEX)==0);
    CHECK(iprop_parameters_set_overrides(NULL,list,1,sizeof(list[0]),&error)==IPROP_EX_E_ARGUMENT && error.reason==IPROP_REASON_INVALID_ARGUMENT);
    { char tiny[8]; error.message_buffer=tiny; error.message_capacity=sizeof(tiny);
      list[2]=by_id(INPUTS,IPROP_INPUT_U32,1);
      CHECK(iprop_parameters_set_overrides(parameters,list,3,sizeof(list[0]),&error)==IPROP_EX_E_ARGUMENT);
      CHECK(error.reason==IPROP_REASON_UNKNOWN_PARAMETER && error.message_truncated==1 && strcmp(tiny,"paramet")==0);
      error.message_buffer=message; error.message_capacity=sizeof(message); }

    /* Prefix-sized elements in exact allocations: inactive and absent fields
     * are never read (ASan), and a larger future element is read by prefix. */
    { const uint32_t f32_ids[]={1,2,3}; const uint32_t f32_values[]={one,nan,negative_zero}; size_t n;
      unsigned char* packed=(unsigned char*)malloc(3*f32_end);
      CHECK(packed!=NULL);
      for(n=0;n<3;++n) { single=by_id(f32_ids[n],IPROP_INPUT_F32,f32_values[n]); single.struct_size=(uint32_t)f32_end;
          memcpy(packed+n*f32_end,&single,f32_end); }
      CHECK(iprop_parameters_set_overrides(parameters,(const iprop_parameter_override_ex*)(void*)packed,3,(uint32_t)f32_end,&error)==IPROP_EX_OK);
      CHECK(snapshot(parameters,got)==0 && bits(&got[1].f32)==one && bits(&got[2].f32)==nan && bits(&got[3].f32)==negative_zero);
      free(packed);
      packed=(unsigned char*)malloc(color_end); CHECK(packed!=NULL);
      single=by_name("tint",IPROP_INPUT_COLOR,UINT32_C(0x08070605)); single.struct_size=(uint32_t)color_end;
      memcpy(packed,&single,color_end);
      CHECK(iprop_parameters_set_overrides(parameters,(const iprop_parameter_override_ex*)(void*)packed,1,(uint32_t)color_end,&error)==IPROP_EX_OK);
      { iprop_color_rgba8 expected; const uint32_t raw=UINT32_C(0x08070605); memcpy(&expected,&raw,4);
        CHECK(snapshot(parameters,got)==0 && memcmp(&got[0].color,&expected,4)==0); }
      free(packed);
    }
    { struct future { iprop_parameter_override_ex known; uint64_t extension[3]; } wide[2];
      memset(wide,0xee,sizeof(wide));
      wide[0].known=by_id(6,IPROP_INPUT_U32,UINT32_C(0x12345678)); wide[1].known=by_name("maximum",IPROP_INPUT_U32,0);
      wide[0].known.struct_size=wide[1].known.struct_size=(uint32_t)sizeof(wide[0]);
      CHECK(iprop_parameters_set_overrides(parameters,&wide[0].known,2,(uint32_t)sizeof(wide[0]),&error)==IPROP_EX_OK);
      CHECK(snapshot(parameters,got)==0 && got[6].u32==UINT32_C(0x12345678) && got[7].u32==0);
    }

    /* Aliases fail before error binding: no error, list, name or value writes. */
    { uint64_t aligned[(sizeof(iprop_parameter_override_ex)*2+sizeof(iprop_error_ex))/8+1];
      unsigned char* storage=(unsigned char*)(void*)aligned;
      iprop_parameter_override_ex pair[2]; unsigned char storage_before[sizeof(aligned)];
      char name[]="zero"; iprop_parameter_value_ex before[INPUTS];
      pair[0]=by_id(4,IPROP_INPUT_U32,3); pair[1]=by_name("zero",IPROP_INPUT_F32,one);
      pair[1].name.data=name;
      memset(storage,0,sizeof(aligned)); memcpy(storage,pair,sizeof(pair));
      memcpy(storage_before,storage,sizeof(aligned));
      CHECK(snapshot(parameters,before)==0);
      CHECK(iprop_parameters_set_overrides(parameters,(const iprop_parameter_override_ex*)(void*)storage,2,sizeof(pair[0]),
                                           (iprop_error_ex*)(void*)(storage+sizeof(iprop_parameter_override_ex)))==IPROP_EX_E_ARGUMENT);
      CHECK(memcmp(storage,storage_before,sizeof(aligned))==0);
      error.message_buffer=name; error.message_capacity=sizeof(name); memcpy(&saved,&error,sizeof(error));
      CHECK(iprop_parameters_set_overrides(parameters,pair,2,sizeof(pair[0]),&error)==IPROP_EX_E_ARGUMENT);
      CHECK(strcmp(name,"zero")==0 && memcmp(&error,&saved,sizeof(error))==0);
      error.message_buffer=(char*)(void*)&pair[1]; error.message_capacity=4; memcpy(&saved,&error,sizeof(error));
      CHECK(iprop_parameters_set_overrides(parameters,pair,2,sizeof(pair[0]),&error)==IPROP_EX_E_ARGUMENT);
      CHECK(memcmp(&error,&saved,sizeof(error))==0 && pair[1].name.data==name && pair[0].u32==3);
      CHECK(snapshot(parameters,got)==0 && memcmp(before,got,sizeof(got))==0);
      error.message_buffer=message; error.message_capacity=sizeof(message);
      /* The same storage succeeds once error text cannot land in it. */
      CHECK(iprop_parameters_set_overrides(parameters,pair,2,sizeof(pair[0]),&error)==IPROP_EX_OK);
      CHECK(snapshot(parameters,got)==0 && got[4].u32==3 && bits(&got[1].f32)==one);
    }

    /* A malformed capacity cannot hide readable name fields from alias
     * preflight, including a late name after an earlier semantic failure. */
    { struct { iprop_parameter_override_ex pair[2]; unsigned char tail[8]; } wide;
      unsigned char before_list[sizeof(wide)]; iprop_parameter_value_ex before[INPUTS];
      char name[]="zero"; uint32_t first_bad;
      memset(&wide,0xa5,sizeof(wide));
      wide.pair[0]=by_id(4,IPROP_INPUT_U32,3); wide.pair[1]=by_name(name,IPROP_INPUT_F32,one);
      wide.pair[1].struct_size=sizeof(wide.pair[1])+sizeof(wide.tail);
      CHECK(snapshot(parameters,before)==0);
      for(first_bad=0;first_bad<2;++first_bad) {
        wide.pair[0].flags=first_bad; memcpy(before_list,&wide,sizeof(wide));
        error.message_buffer=name; error.message_capacity=sizeof(name); saved=error;
        CHECK(iprop_parameters_set_overrides(parameters,wide.pair,2,sizeof(wide.pair[0]),&error)==IPROP_EX_E_ARGUMENT);
        CHECK(strcmp(name,"zero")==0 && memcmp(&error,&saved,sizeof(error))==0 && memcmp(&wide,before_list,sizeof(wide))==0);
        CHECK(snapshot(parameters,got)==0 && memcmp(before,got,sizeof(got))==0);
      }
      /* Only a complete name prefix is actually present in this row. */
      { const uint32_t stride=offsetof(iprop_parameter_override_ex,name)+offsetof(iprop_utf8_view,byte_count)+4;
        const uint32_t claimed=stride+4;
        unsigned char* row=(unsigned char*)malloc(claimed);
        CHECK(row!=NULL); memcpy(row,&wide.pair[1],stride); memcpy(row,&claimed,4); saved=error;
        CHECK(iprop_parameters_set_overrides(parameters,(const iprop_parameter_override_ex*)(void*)row,1,stride,&error)==IPROP_EX_E_ARGUMENT);
        CHECK(strcmp(name,"zero")==0 && memcmp(&error,&saved,sizeof(error))==0);
        CHECK(snapshot(parameters,got)==0 && memcmp(before,got,sizeof(got))==0); free(row);
      }
      error.message_buffer=message; error.message_capacity=sizeof(message);
      /* Even a short-stride list span is protected before argument reporting. */
      saved=error;
      CHECK(iprop_parameters_set_overrides(parameters,(const iprop_parameter_override_ex*)(const void*)&error,1,3,&error)==IPROP_EX_E_ARGUMENT);
      CHECK(memcmp(&error,&saved,sizeof(error))==0 && snapshot(parameters,got)==0 && memcmp(before,got,sizeof(got))==0);
    }

    /* Lists resolve through retained data after program_free and mutate only
     * their own handle; the program's other parameter handles are unaffected. */
    CHECK(iprop_parameters_clone(parameters,&clone,NULL)==IPROP_EX_OK);
    iprop_program_free(program); free(bytes);
    CHECK(snapshot(parameters,defaults)==0);
    single=by_name("minimum",IPROP_INPUT_U32,42);
    CHECK(iprop_parameters_set_overrides(clone,&single,1,sizeof(single),&error)==IPROP_EX_OK);
    CHECK(snapshot(clone,got)==0 && got[6].u32==42);
    CHECK(snapshot(parameters,got)==0 && memcmp(defaults,got,sizeof(got))==0);
    iprop_parameters_free(clone); iprop_parameters_free(parameters);
    puts("C99 parameter overrides: exact mixed ID/name lists, duplicate/unknown/type/descriptor rejection before mutation, repeated updates, prefixes/stride and aliases: PASS");
    return 0;
}
