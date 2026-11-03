#pragma once

#include "iprop/VmInternals.hpp"
#include <bit>

namespace iprop::detail {
// Sorting of bounded algorithm scratch, with a portable comparison bound and
// constant automatic workspace. ResourceVector accounts every capacity.
template<class T,u64 Slot,class Less>
void geometrySort(Execution& ex,ResourceVector<T,Slot>& values,Less less) {
    const auto n=static_cast<u64>(values.size());ex.spend(4*n*(1+std::bit_width(n)));
    const auto sift=[&](std::size_t root,std::size_t count) {
        while(root<count/2) {
            auto child=root*2+1;
            if(child+1<count && less(values[child],values[child+1]))++child;
            if(!less(values[root],values[child]))break;
            std::swap(values[root],values[child]);root=child;
        }
    };
    for(auto i=values.size()/2;i>0;--i)sift(i-1,values.size());
    for(auto i=values.size();i>1;--i){std::swap(values[0],values[i-1]);sift(0,i-1);}
}
} // namespace iprop::detail
