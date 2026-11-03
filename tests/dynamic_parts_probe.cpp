#include "support/geometry_fixture.hpp"
#include "support/geometry_json.hpp"
#include "iprop/DynamicParts.hpp"
#include <bit>
#include <cstdio>
#include <cstdlib>

using namespace iprop;
namespace {
u64 integer(const char* text){return std::strtoull(text,nullptr,10);}
f32 number(const char* text){return std::bit_cast<f32>(static_cast<u32>(integer(text)));}
i32 signedInteger(const char* text){return static_cast<i32>(std::strtoll(text,nullptr,10));}
std::shared_ptr<SurfaceBuffer> surface(detail::Execution& ex,u32 w,u32 h,Vec2 scale,bool rgba){
    const auto bytes=rgba?surfaceBytes(w,h):maskBytes(w,h);ex.allocateResource(bytes);auto b=ex.resources.makeSurface(bytes);
    b->format=rgba?SurfaceBuffer::Format::RGBA8:SurfaceBuffer::Format::A8;
    b->raster.width=w;b->raster.height=h;b->raster.scaleX=scale.x;b->raster.scaleY=scale.y;
    if(rgba)b->raster.pixels.resize(static_cast<std::size_t>(w)*h);else b->coverage.resize(static_cast<std::size_t>(w)*h);
    return b;
}
Color sourcePixel(u32 x,u32 y){
    const auto i=y*5+x;const u8 alpha=i==1?1:i==8?127:i==11?128:i==12?254:i==19?255:0;
    return {static_cast<u8>(17+x*43+y*11),static_cast<u8>(29+x*13+y*37),static_cast<u8>(53+x*31+y*17),alpha};
}
void partJson(const DynamicPartData& p){
    std::printf("{\"id\":%d,\"sourceId\":%d,\"center\":",p.id,p.sourceId);test_support::jsonPoint(p.center);
    std::printf(",\"origin\":");test_support::jsonPoint(p.boundsOrigin);std::printf(",\"size\":");test_support::jsonPoint(p.boundsSize);
    std::printf(",\"d4\":%u,\"translation\":[%d,%d],\"appearance\":\"%s\",\"originalFrame\":[",p.transform.kind(),p.translation.x,p.translation.y,
        p.fill?"fill":p.texture?"texture":"source");
    bool comma=false;for(const double value:{p.original.centerX,p.original.centerY,p.original.left,p.original.top,p.original.right,p.original.bottom}){
        if(comma)std::printf(",");comma=true;std::printf("\"%016llx\"",static_cast<unsigned long long>(std::bit_cast<u64>(value)));}
    std::printf("],\"sourceScale\":");test_support::jsonPoint(p.original.scale);std::printf("}");
}
void maskJson(const MaskValue& mask){
    if(!mask.buffer){std::printf("null");return;}
    const auto& b=*mask.buffer;std::printf("{\"format\":\"%s\",\"size\":[%u,%u],\"origin\":[%d,%d],\"d4\":%u,\"scale\":",
        b.format==SurfaceBuffer::Format::A8?"A8":"RGBA8",b.raster.width,b.raster.height,mask.mapping.origin.x,mask.mapping.origin.y,mask.mapping.transform.kind());
    test_support::jsonPoint({b.raster.scaleX,b.raster.scaleY});std::printf(",\"pixels\":[");
    for(std::size_t i=0;i<b.coverage.size();++i){if(i)std::printf(",");std::printf("%u",b.coverage[i]);}std::printf("]}");
}
}
int main(int argc,char** argv){
    if(argc!=20)return 1;
    LoadedFile file;file.code.functions.emplace_back();detail::Execution ex(file);ex.fuel=1000000000;
    const auto kind=integer(argv[1]),appearance=integer(argv[7]),clip=integer(argv[13]),mode=integer(argv[14]),background=integer(argv[15]);
    const auto maskMode=integer(argv[16]),place=integer(argv[19]);
    const u64 budget=integer(argv[17]),occupied=integer(argv[18]);
    const auto originalD4=D4Transform::fromKind(static_cast<u8>(integer(argv[10])));if(!originalD4)return 2;
    const Vec2 rawScale{number(argv[8]),number(argv[9])};
    const Vec2 captureScale=kind<2?rawScale:originalD4->mapExtent(rawScale);
    const auto transform=D4Transform::fromParameters(signedInteger(argv[2]),signedInteger(argv[3]),signedInteger(argv[4]));
    const bool explicitPlace=place!=0&&place!=4;
    const auto finalScale=place==3?captureScale:place==4?D4Transform::fromKind(1)->mapExtent(captureScale):
        explicitPlace&&transform?transform->mapExtent(captureScale):captureScale;
    const auto textureScale=kind<2?(explicitPlace&&transform?transform->mapExtent(finalScale):finalScale):captureScale;
    try{
        ex.target=RenderTarget::create(32,32,captureScale,mode?CompositionMode::SourceOver:CompositionMode::Legacy);
        {
            std::shared_ptr<const GeometryData> geometry;
            std::shared_ptr<SurfaceBuffer> pixels;
            if(kind<2)geometry=kind==0?geometry_fixture::holes(ex):geometry_fixture::fractional(ex);
            else{
                pixels=surface(ex,5,4,rawScale,kind!=2);
                for(u32 y=0;y<4;++y)for(u32 x=0;x<5;++x){const auto c=sourcePixel(x,y);
                    if(kind==2)pixels->coverage[y*5+x]=c.a;else pixels->raster.at(x,y)=c;}
            }
            const auto textureD4=*D4Transform::fromKind(6);
            auto paint=surface(ex,7,6,textureD4.mapExtent(textureScale),true);
            for(u32 y=0;y<6;++y)for(u32 x=0;x<7;++x)paint->raster.at(x,y)={static_cast<u8>(11+x*23),static_cast<u8>(19+y*29),
                static_cast<u8>(31+x*17+y*13),static_cast<u8>(x*37+y*61)};
            LayerValue texture{paint,{textureD4,{-4,-2}}};
            if(clip){auto b=surface(ex,32,32,finalScale,false);
                for(u32 y=0;y<32;++y)for(u32 x=0;x<32;++x)b->coverage[y*32+x]=clip==2?0:clip==1?128:static_cast<u8>((x*19+y*31)%256);
                ex.target.clips.push_back({std::move(b),{}});
            }
            if(background)for(auto& c:ex.canvas().pixels)c={201,4,172,static_cast<u8>(background==1?128:0)};
            const auto sourceBytes=ex.resources.bytes;ex.fuel=budget;ex.resources.bytes+=occupied;
            {
                const SurfaceMapping sourceMapping{*originalD4,{signedInteger(argv[11]),signedInteger(argv[12])}};
                Part2dValue original=kind<2?detail::createPart(ex,Regions2dValue{geometry},0):
                    kind==3?detail::createPart(ex,LayerValue{pixels,sourceMapping}):detail::createPart(ex,MaskValue{pixels,sourceMapping});
                auto view=original;
                if(appearance==1||appearance==3||appearance==5||appearance==6)
                    view=detail::fillPart(ex,view,appearance==3?Color{7,11,13,0}:appearance==6?Color{13,29,47,255}:Color{17,89,203,128});
                if(appearance==2||appearance==4||appearance==5)view=detail::texturePart(ex,view,texture);
                if(appearance==4)view=detail::fillPart(ex,view,{41,97,153,64});
                if(explicitPlace){
                    if(place==2)view=detail::placePart(ex,view,{7.25f,9.5f},0,0,0);
                    ex.canvas().scaleX=finalScale.x;ex.canvas().scaleY=finalScale.y;
                    view=detail::placePart(ex,view,{number(argv[5]),number(argv[6])},signedInteger(argv[2]),signedInteger(argv[3]),signedInteger(argv[4]));
                }
                if(place==4){ex.canvas().scaleX=finalScale.x;ex.canvas().scaleY=finalScale.y;}
                MaskValue mask;
                if(maskMode!=2)mask=detail::maskPart(ex,view);
                if(maskMode!=1)detail::drawPart(ex,view);
                const auto fuel=budget-ex.fuel,bytes=ex.resources.bytes-sourceBytes-occupied;
                std::printf("{\"original\":");partJson(*original.data);std::printf(",\"view\":");partJson(*view.data);
                std::printf(",\"mask\":");maskJson(mask);std::printf(",\"canvas\":[");bool comma=false;
                for(const auto c:ex.canvas().pixels)for(const auto channel:{c.r,c.g,c.b,c.a}){
                    if(comma)std::printf(",");comma=true;std::printf("%u",channel);}
                std::printf("],\"sourceBytes\":%llu,\"bytes\":%llu,\"fuel\":%llu,\"creations\":%u",
                    static_cast<unsigned long long>(sourceBytes),static_cast<unsigned long long>(bytes),static_cast<unsigned long long>(fuel),ex.dynamicPartCreations);
            }
        }
        ex.target.clips.clear();ex.resources.bytes-=occupied;std::printf(",\"released\":%llu}\n",static_cast<unsigned long long>(ex.resources.bytes));
    }catch(const FormatError& e){std::fprintf(stderr,"%d:%s\n",static_cast<int>(e.reason),e.what());return 4;}
}
