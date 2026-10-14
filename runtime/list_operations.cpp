#include "iprop/ListOperations.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace iprop {
namespace {
template<class T> int compare(T a, T b) { return a < b ? -1 : b < a ? 1 : 0; }
int floatCompare(f32 a, f32 b, bool exact) {
    const auto x=std::bit_cast<u32>(a), y=std::bit_cast<u32>(b);
    // Description identity retains parameter bits, including signed zero.
    if(exact) return compare(x,y);
    const bool nx=(x&0x7fffffffu)>0x7f800000u, ny=(y&0x7fffffffu)>0x7f800000u;
    if(nx || ny) return compare(nx,ny); // all NaNs form one stable class, last
    return compare(a,b); // signed zeros compare equal
}
u64 distributionWidth(const Distribution& value) {
    if(value.family==DistributionFamily::None) throw std::invalid_argument("distribution handle is unassigned");
    if(value.family==DistributionFamily::Weighted || value.family==DistributionFamily::Density) {
        if(!value.points) throw std::invalid_argument("distribution handle is unassigned");
        if(value.points->points.size()>kMaxListElements) throw std::invalid_argument("distribution exceeds 256 points");
    }
    return 7u+(value.points?value.points->points.size()*4u:0u);
}
u64 scalarWidth(TypeDescriptor type) {
    return type==ValueType::Color?4u:type==ValueType::Vec2?2u:1u;
}
u64 elementWidth(const Value& value, TypeDescriptor expected) {
    if(valueType(value)!=expected) throw std::invalid_argument("list element type mismatch");
    if(expected==ValueType::Distribution) return distributionWidth(std::get<Distribution>(value));
    if(expected.isSampler()) {
        const auto& sampler=std::get<SamplerValue>(value);
        if(!sampler.data) throw std::invalid_argument("sampler handle is unassigned");
        const auto& data=*sampler.data;
        if(data.type!=expected || data.mode>SamplerMode::With || valueType(data.lower)!=expected.item() ||
            (data.mode!=SamplerMode::Constant && valueType(data.upper)!=expected.item()))
            throw std::invalid_argument("invalid sampler description");
        return 2u+scalarWidth(expected.item())*(data.mode==SamplerMode::Constant?1u:2u)+
            (data.mode==SamplerMode::With?distributionWidth(data.distribution):0u);
    }
    return scalarWidth(expected);
}
int numberCompare(const DistributionNumber& a, const DistributionNumber& b) {
    if(const auto kind=compare(a.index(),b.index())) return kind;
    if(const auto* i=std::get_if<i32>(&a)) return compare(*i,std::get<i32>(b));
    return floatCompare(std::get<f32>(a),std::get<f32>(b),true);
}
int distributionCompare(const Distribution& a, const Distribution& b) {
    if(const auto family=compare(a.family,b.family)) return family;
    for(std::size_t i=0;i<a.parameters.size();++i)
        if(const auto parameter=floatCompare(a.parameters[i],b.parameters[i],true)) return parameter;
    if(const auto present=compare(static_cast<bool>(a.points),static_cast<bool>(b.points))) return present;
    if(!a.points) return 0;
    if(const auto family=compare(a.points->family,b.points->family)) return family;
    if(const auto size=compare(a.points->points.size(),b.points->points.size())) return size;
    for(std::size_t i=0;i<a.points->points.size();++i) {
        const auto& x=a.points->points[i]; const auto& y=b.points->points[i];
        if(const auto key=numberCompare(x.key,y.key)) return key;
        if(const auto value=numberCompare(x.value,y.value)) return value;
    }
    return 0;
}
int elementCompare(const Value& a, const Value& b, TypeDescriptor type, bool exact=false) {
    if(type==ValueType::I32) return compare(std::get<i32>(a),std::get<i32>(b));
    if(type==ValueType::F32) return floatCompare(std::get<f32>(a),std::get<f32>(b),exact);
    if(type==ValueType::Color) {
        const auto x=std::get<Color>(a),y=std::get<Color>(b);
        const std::array left{x.r,x.g,x.b,x.a},right{y.r,y.g,y.b,y.a};
        for(std::size_t i=0;i<left.size();++i) if(const auto channel=compare(left[i],right[i])) return channel;
        return 0;
    }
    if(type==ValueType::Vec2) {
        const auto x=std::get<Vec2>(a),y=std::get<Vec2>(b);
        if(const auto component=floatCompare(x.x,y.x,exact)) return component;
        return floatCompare(x.y,y.y,exact);
    }
    if(type==ValueType::Distribution) return distributionCompare(std::get<Distribution>(a),std::get<Distribution>(b));
    const auto& x=*std::get<SamplerValue>(a).data;const auto& y=*std::get<SamplerValue>(b).data;
    if(const auto mode=compare(x.mode,y.mode)) return mode;
    if(const auto lower=elementCompare(x.lower,y.lower,type.item(),true)) return lower;
    if(x.mode!=SamplerMode::Constant)
        if(const auto upper=elementCompare(x.upper,y.upper,type.item(),true)) return upper;
    return x.mode==SamplerMode::With?distributionCompare(x.distribution,y.distribution):0;
}
}

bool ListSelection::unchanged(const ListValue& source) const {
    if(count!=source.data->elements.size()) return false;
    for(std::size_t i=0;i<count;++i) if(indices[i]!=i) return false;
    return true;
}
u64 listSelectionWork(const ListValue& source, bool sort) {
    if(!source.data) throw std::invalid_argument("list handle is unassigned");
    if(!source.type.isList() || !source.type.isStorable() || source.data->type!=source.type)
        throw std::invalid_argument("invalid list descriptor");
    const auto n=source.data->elements.size();
    if(n>kMaxListElements) throw std::invalid_argument("list exceeds 256 elements");
    if(sort && !isSortableList(source.type)) throw std::invalid_argument("list.sort does not support this element type");
    u64 width=1;
    for(const auto& value:source.data->elements) width=std::max(width,elementWidth(value,source.type.item()));
    u64 comparisons=n*(n?n-1u:0u)/2u;
    if(sort) {u64 passes=0;for(std::size_t run=1;run<n;run*=2)++passes;comparisons=n*passes;}
    return 2u*n+width*comparisons;
}
ListSelection selectList(const ListValue& source, bool sort) {
    (void)listSelectionWork(source,sort);
    ListSelection result;const auto n=source.data->elements.size();const auto type=source.type.item();
    if(!sort) {
        for(u16 i=0;i<n;++i) {
            bool duplicate=false;
            for(std::size_t j=0;j<result.count;++j)
                if(elementCompare(source.data->elements[i],source.data->elements[result.indices[j]],type)==0) {duplicate=true;break;}
            if(!duplicate) result.indices[result.count++]=i;
        }
        return result;
    }
    result.count=n;for(u16 i=0;i<n;++i)result.indices[i]=i;
    std::array<u16,kMaxListElements> temporary{};
    // Fixed merge passes, stable equal-value ordering, no library sort or heap scratch.
    for(std::size_t run=1;run<n;run*=2) {
        for(std::size_t start=0;start<n;start+=run*2) {
            const auto middle=std::min(start+run,n),end=std::min(start+run*2,n);
            auto left=start,right=middle;
            for(auto at=start;at<end;++at) {
                const bool takeLeft=left<middle && (right==end || elementCompare(
                    source.data->elements[result.indices[left]],source.data->elements[result.indices[right]],type)<=0);
                temporary[at]=result.indices[takeLeft?left++:right++];
            }
        }
        result.indices=temporary;
    }
    return result;
}
ListValue materializeListSelection(const ListValue& source, const ListSelection& selection) {
    if(selection.unchanged(source)) return source;
    auto data=std::make_shared<ListData>();data->type=source.type;data->elements.reserve(selection.count);
    for(std::size_t i=0;i<selection.count;++i)data->elements.push_back(source.data->elements[selection.indices[i]]);
    return {source.type,std::move(data)};
}
} // namespace iprop
