#include "../Operations.hpp"
#include "../../../tests/support/geometry_fixture.hpp"
#include "iprop/DynamicParts.hpp"
#include "Compiler.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "check.h"

#include <limits>
#include <new>

namespace {
bool armed=false,persistent=false;
std::size_t allocations=0,failAt=0,live=0,faults=0;
void append(iprop::detail::Execution& ex,const iprop::Part2dValue& part) {
    ex.push(part);(void)iprop::detail::stepOutputs(ex,{iprop::Opcode::OutListPart,0,0});
}
}
void* operator new(std::size_t size) {
    if(armed&&allocations++>=failAt){if(!persistent)armed=false;throw std::bad_alloc{};}
    if(void* value=std::malloc(size?size:1)){++live;return value;}throw std::bad_alloc{};
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* value) noexcept{if(value){--live;std::free(value);}}
void operator delete[](void* value) noexcept{::operator delete(value);}
void operator delete(void* value,std::size_t) noexcept{::operator delete(value);}
void operator delete[](void* value,std::size_t) noexcept{::operator delete(value);}

int main() {
    using namespace iprop;
    const auto ast=ips::parse(R"(asset t{size 2x2 output list part pieces
        draw{layer image(alpha=surface.source_over){pixel(0,0,#12345680)}
        p=part.create(image) pieces=p pieces=part.fill(p,#aabbcc01)}})");
    ips::checkAsset(ast,{});const auto bytes=compileProgram(ast,{});
    const auto file=loadFile(bytes);validate(file);
    iprop_program* program=nullptr;CHECK(iprop_program_load_ex(bytes.data(),bytes.size(),&program,nullptr)==IPROP_EX_OK);
    iprop_generate_options_ex options{};CHECK(iprop_generate_options_ex_init(&options,sizeof(options))==IPROP_EX_OK);

    for(const bool mode:{false,true})for(unsigned path=0;path<5;++path) {
        bool success=false;
        for(std::size_t at=0;at<8192;++at) {
            const auto baseline=live;
            {
                if(path<2) {
                    LoadedFile internal;internal.code.functions.emplace_back();detail::Execution ex(internal);
                    ex.target=RenderTarget::create(16,16,{1,1});ex.frames.emplace_back();ex.fuel=1000000;
                    ex.outputSlots.emplace_back();ex.outputSlots[0].kind=OutputKind::Part;ex.outputSlots[0].isList=true;
                    ex.slotOwner.push_back(detail::kNoPartIndex);
                    const auto part=detail::createPart(ex,Regions2dValue{geometry_fixture::holes(ex)},0);
                    if(path==1)append(ex,part);
                    const auto priorBytes=ex.resources.bytes;const auto priorCount=ex.exportedParts;
                    ex.push(part); // stack growth is outside the append allocation under test
                    allocations=0;failAt=at;persistent=mode;armed=true;
                    try{(void)detail::stepOutputs(ex,{Opcode::OutListPart,0,0});armed=false;success=true;}
                    catch(const std::bad_alloc&){armed=false;++faults;}
                    catch(...){armed=false;return 2;}
                    if(!success) {
                        CHECK(ex.resources.bytes==priorBytes && ex.exportedParts==priorCount);
                        CHECK(path==0?!ex.outputSlots[0].parts:ex.outputSlots[0].parts->records.size()==1);
                    }
                    CHECK(ex.dynamicPartCreations==1 && part.data->id==0 && !part.data->fill);
                } else if(path==2) {
                    VM vm(file,{});vm.run();const auto previous=vm.result().outputs[0].parts;
                    const auto* pixels=vm.canvas().pixels.data();
                    allocations=0;failAt=at;persistent=mode;armed=true;
                    try{auto result=vm.takeResult();armed=false;success=true;CHECK(result.outputs[0].parts==previous);}
                    catch(const std::bad_alloc&){armed=false;++faults;}
                    catch(...){armed=false;return 3;}
                    if(!success)CHECK(vm.canvas().pixels.data()==pixels && vm.result().outputs[0].parts==previous);
                } else if(path==3) {
                    VM vm(file,{});vm.run();auto generation=vm.takeResult();iprop_result packed;
                    allocations=0;failAt=at;persistent=mode;armed=true;
                    try{capi::packResult(packed,std::move(generation),file);armed=false;success=true;}
                    catch(const std::bad_alloc&){armed=false;++faults;}
                    catch(...){armed=false;return 4;}
                } else {
                    auto* const sentinel=reinterpret_cast<iprop_result*>(std::uintptr_t{1});auto* output=sentinel;
                    allocations=0;failAt=at;persistent=mode;armed=true;
                    const auto status=iprop_generate_ex(program,&options,nullptr,&output,nullptr);armed=false;
                    if(status==IPROP_EX_OK){success=true;iprop_result_free(output);}
                    else {CHECK(status==IPROP_EX_E_OOM && output==sentinel);++faults;}
                }
            }
            CHECK(live==baseline);
            if(success)break;
        }
        CHECK(success);
    }
    for(unsigned growing:{0u,1u})for(unsigned shortBy:{0u,1u}) {
        LoadedFile internal;internal.code.functions.emplace_back();detail::Execution ex(internal);
        ex.target=RenderTarget::create(16,16,{1,1});ex.frames.emplace_back();ex.fuel=1000000;
        ex.outputSlots.emplace_back();ex.outputSlots[0].kind=OutputKind::Part;ex.outputSlots[0].isList=true;
        ex.slotOwner.push_back(detail::kNoPartIndex);
        const auto p=detail::createPart(ex,Regions2dValue{geometry_fixture::holes(ex)},0);
        if(growing)append(ex,p);
        // Both the first table and old+new capacity during growth need 288
        // additional logical bytes at peak. One less must leave the slot intact.
        ex.resources.bytes=kMaxGenerationData-(288-shortBy);const auto before=ex.resources.bytes;
        bool failed=false;try{append(ex,p);}catch(const Trap& error){failed=true;CHECK(error.reason==ErrorReason::OutOfMemory);}
        CHECK(failed==(shortBy!=0));CHECK(ex.exportedParts==growing+(shortBy?0u:1u));
        if(shortBy)CHECK(ex.resources.bytes==before);
        ex.outputSlots.clear();ex.resources.clear();
    }
    iprop_program_free(program);
    std::printf("Part output allocation failures: PASS (%zu transient/persistent faults across 5 append/grow/transfer/pack/generate paths; 4 exact quota boundaries; no heap/quota leaks)\n",faults);
    return 0;
}
