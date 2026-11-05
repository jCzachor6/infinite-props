// Real heap-failure injection for standalone allocation probes, including
// persistent OOM while unwinding. Include from exactly one translation unit of
// a probe executable that stays outside Catch2, so framework allocations cannot
// conceal retained resources.
#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>

namespace fault_injection {
inline bool armed=false, persistent=false;
inline std::size_t allocations=0, failAt=0, live=0;
// Fail allocation number `at`; with `keep`, every later allocation fails too.
inline void arm(std::size_t at,bool keep){persistent=keep;allocations=0;failAt=at;armed=true;}
}

// These replacements intentionally pair malloc/free. GCC's inlining analysis
// otherwise diagnoses the replaced delete as a mismatched standard allocator.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void* operator new(std::size_t size) {
    using namespace fault_injection;
    if(armed && allocations++>=failAt){if(!persistent)armed=false;throw std::bad_alloc{};}
    if(void* p=std::malloc(size?size:1)){++live;return p;}throw std::bad_alloc{};
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept {if(p){--fault_injection::live;std::free(p);}}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
using namespace fault_injection;
