// Sweep real heap failures separately from Catch2, including persistent OOM
// during unwinding. Both output ownership and temporary row charges must unwind.
#include "iprop/VmInternals.hpp"
#include "../support/fault_injection.hpp"

#include <cstdio>

using namespace fault_injection;

int main() {
    using namespace iprop;
    LoadedFile file; file.code.functions.emplace_back();
    const auto source = std::make_shared<SurfaceBuffer>();
    source->raster.resize(3,19); source->raster.at(1,9)={255,80,40,128};
    const auto original = source->raster.pixels;
    for (const auto op : {Opcode::LayerBlur,Opcode::MaskBlur}) for (const f32 sigma : {0.f,1.f}) {
        bool reachedSuccess = false;
        for (std::size_t limit = 0; limit < 32; ++limit) {
            detail::Execution ex(file); ex.frames.emplace_back(); ex.fuel=1000000;
            ex.push(op==Opcode::LayerBlur ? Value(LayerValue{source,{}}) : Value(MaskValue{source,{}})); ex.push(sigma);
            arm(limit, true);
            bool succeeded = false;
            try { succeeded=detail::stepBlur(ex,{op,0,0}); armed=false; }
            catch (const std::bad_alloc&) { armed=false; }
            catch (...) { armed=false; return 1; }
            if (source->raster.pixels != original) return 2;
            if (succeeded) {
                if (ex.frame().stack.size()!=1) return 3;
                if (sigma==0 && (allocations!=0 || ex.resources.bytes!=0)) return 4;
                ex.frame().stack.clear();
                if (ex.resources.bytes!=0) return 5;
                reachedSuccess=true;
                break;
            }
            if (!ex.frame().stack.empty() || ex.resources.bytes!=0) return 6;
        }
        if (!reachedSuccess) return 7;
    }
    std::puts("blur allocations: PASS zero-allocation identity and persistent failure cleanup for layer/mask");
    return 0;
}
