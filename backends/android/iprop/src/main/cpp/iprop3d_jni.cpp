#include "JniSupport.hpp"
#include "iprop3d.h"

namespace {
template<class T> T* handle(jlong raw) {
    return reinterpret_cast<T*>(static_cast<std::uintptr_t>(raw));
}

template<class View, auto Query, class Owner, class... Index>
View query(Owner* owner, Index... index) {
    Failure f;
    View view{};
    view.struct_size = sizeof(view);
    f.check(Query(owner, index..., &view, &f.error));
    return view;
}

jfloatArray floats(JNIEnv* env, const float* data, std::size_t size) {
    if (size > INT32_MAX) throw std::length_error("JNI float array too large");
    auto count = static_cast<jsize>(size);
    auto array = env->NewFloatArray(count);
    if (array && count) env->SetFloatArrayRegion(array, 0, count, data);
    return array;
}

auto model(jlong raw) { return handle<iprop3d_model>(raw); }
auto result(jlong raw) { return handle<iprop3d_result>(raw); }
auto surface(jlong raw, jint i) { return query<iprop3d_surface_view, iprop3d_result_surface>(result(raw), i); }
auto texture(jlong raw, jint i) { return query<iprop3d_texture_view, iprop3d_result_texture>(result(raw), i); }
auto image(jlong raw, jint i, jint j) { return query<iprop_aux_image_view_ex, iprop3d_result_texture_image>(result(raw), i, j); }
auto output(jlong raw, jint i) { return query<iprop3d_output_view, iprop3d_result_output>(result(raw), i); }
auto skin(jlong raw, jint i) { return query<iprop3d_skin_program_info, iprop3d_model_skin_program>(model(raw), i); }
auto input(jlong raw, jint i) { return query<iprop_input_info_ex, iprop3d_model_input_get>(model(raw), i); }
}
#define MODEL_JNI(type, name) extern "C" JNIEXPORT type JNICALL Java_dev_infiniteprops_ModelNative_##name

MODEL_JNI(jlongArray, library)(JNIEnv* env, jobject) {
    return guarded<jlongArray>(env, [&] {
        Failure f;
        requireRuntime(f);
        iprop3d_library_info info{};
        info.struct_size = sizeof(info);
        f.check(iprop3d_library_query(&info, &f.error));
        if (info.api_major != 0 || info.api_minor != 3) throw std::invalid_argument("Expected model API 0.3");
        return longs(env, {info.api_major, info.api_minor, info.max_vertices, info.max_triangles,
            info.max_textures, info.max_texture_side, static_cast<jlong>(info.max_texels), static_cast<jlong>(iprop3d_texture_stamp())});
    });
}

MODEL_JNI(jlong, load)(JNIEnv* env, jobject, jbyteArray bytesIn) {
    return guarded<jlong>(env, [&] {
        auto size = env->GetArrayLength(bytesIn);
        std::vector<uint8_t> data(static_cast<std::size_t>(size) + 1);
        env->GetByteArrayRegion(bytesIn, 0, size, reinterpret_cast<jbyte*>(data.data()));
        if (env->ExceptionCheck()) return jlong{};
        iprop3d_model* out = nullptr;
        Failure f;
        f.check(iprop3d_model_load(data.data(), static_cast<uint64_t>(size), &out, &f.error));
        return pointer(out);
    });
}
MODEL_JNI(void, close)(JNIEnv*, jobject, jlong raw) { iprop3d_model_free(model(raw)); }
MODEL_JNI(void, releaseResult)(JNIEnv*, jobject, jlong raw) { iprop3d_result_free(result(raw)); }
MODEL_JNI(void, releaseParameters)(JNIEnv*, jobject, jlong raw) { iprop_parameters_free(handle<iprop_parameters>(raw)); }

MODEL_JNI(jlong, parameters)(JNIEnv* env, jobject, jlong raw) {
    return guarded<jlong>(env, [&] {
        Failure f;
        iprop_parameters* p = nullptr;
        f.check(iprop3d_model_parameters_create(model(raw), &p, &f.error));
        return pointer(p);
    });
}
MODEL_JNI(void, set)(JNIEnv* env, jobject, jlong raw, jbyteArray name, jint type, jint value) {
    guarded<int>(env, [&] { setNamed(env, handle<iprop_parameters>(raw), name, type, value); return 0; });
}

MODEL_JNI(jlong, generate)(JNIEnv* env, jobject, jlong raw, jlong parameters, jint seed, jint lod,
                          jlong work, jlong timeout, jlong token, jlongArray records) {
    return guarded<jlong>(env, [&] {
        auto length = env->GetArrayLength(records);
        if (length % 4 != 0) throw std::invalid_argument("Invalid known texture record");
        std::vector<jlong> words(static_cast<std::size_t>(length));
        if (length) env->GetLongArrayRegion(records, 0, length, words.data());
        if (env->ExceptionCheck()) return jlong{};
        std::vector<iprop3d_known_texture> known;
        for (std::size_t i = 0; i < words.size(); i += 4) {
            known.push_back({static_cast<uint64_t>(words[i]), static_cast<uint64_t>(words[i+1]),
                static_cast<uint64_t>(words[i+2]), static_cast<uint32_t>(words[i+3]), 0});
        }
        iprop3d_generate_options opts{};
        opts.struct_size = sizeof(opts);
        opts.seed = static_cast<uint32_t>(seed);
        opts.lod = static_cast<uint32_t>(lod);
        opts.known_textures = known.data();
        opts.known_texture_count = static_cast<uint32_t>(known.size());
        iprop_execution_limits_ex limits{};
        limits.struct_size = sizeof(limits);
        limits.work_limit = static_cast<uint64_t>(work);
        limits.timeout_ns = static_cast<uint64_t>(timeout);
        limits.cancellation = handle<iprop_cancellation>(token);
        Failure f;
        iprop3d_result* out = nullptr;
        f.check(iprop3d_generate(model(raw), &opts, handle<iprop_parameters>(parameters), &limits, &out, &f.error));
        return pointer(out);
    });
}

MODEL_JNI(jlongArray, metadata)(JNIEnv* env, jobject, jlong raw) {
    return guarded<jlongArray>(env, [&] {
        auto v = query<iprop3d_model_info, iprop3d_model_query>(model(raw));
        return longs(env, {bits(v.size_x), bits(v.size_y), bits(v.size_z), bits(v.units_per_meter),
            static_cast<jlong>(v.fuel), v.container_flags, v.input_count, v.skin_program_count});
    });
}

MODEL_JNI(jlongArray, inputInfo)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jlongArray>(env, [&] {
        auto v = input(raw, i);
        return longs(env, {v.type, v.type == 0 ? color(v.color_default) : v.type == 1 ? bits(v.f32_default) : v.u32_default});
    });
}

MODEL_JNI(jlongArray, skinInfo)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jlongArray>(env, [&] {
        auto v = skin(raw, i);
        return longs(env, {v.width, v.height});
    });
}

MODEL_JNI(jlongArray, statistics)(JNIEnv* env, jobject, jlong raw) {
    return guarded<jlongArray>(env, [&] {
        auto v = query<iprop3d_result_info, iprop3d_result_query>(result(raw));
        return longs(env, {bits(v.size[0]), bits(v.size[1]), bits(v.size[2]), bits(v.units_per_meter),
            bits(v.bounds_min[0]), bits(v.bounds_min[1]), bits(v.bounds_min[2]), bits(v.bounds_max[0]),
            bits(v.bounds_max[1]), bits(v.bounds_max[2]), v.vertex_count, v.triangle_count,
            static_cast<jlong>(v.fuel_budget), static_cast<jlong>(v.fuel_used), static_cast<jlong>(v.work_budget), static_cast<jlong>(v.work_used),
            v.surface_count, v.texture_count, v.output_count});
    });
}

MODEL_JNI(jlongArray, surfaceInfo)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jlongArray>(env, [&] {
        auto v = surface(raw, i);
        return longs(env, {v.texture_index, bits(v.roughness), bits(v.metallic), bits(v.emission),
            v.alpha_mode, bits(v.alpha_cutoff), v.two_sided, v.crisp});
    });
}

MODEL_JNI(jlongArray, textureInfo)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jlongArray>(env, [&] {
        auto v = texture(raw, i);
        return longs(env, {static_cast<jlong>(v.key), static_cast<jlong>(v.fuel), static_cast<jlong>(v.work), v.image_kinds,
            v.asset_width, v.asset_height, v.width, v.height,
            v.repeats, v.reused, static_cast<jlong>(v.row_stride), v.pixel_format,
            v.alpha_mode, v.origin, v.image_count});
    });
}

MODEL_JNI(jlongArray, imageInfo)(JNIEnv* env, jobject, jlong raw, jint i, jint j) {
    return guarded<jlongArray>(env, [&] {
        auto v = image(raw, i, j);
        return longs(env, {v.kind, v.source, v.width, v.height,
            static_cast<jlong>(v.row_stride), v.pixel_format, v.alpha_mode, v.origin});
    });
}

MODEL_JNI(jlongArray, outputInfo)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jlongArray>(env, [&] {
        auto v = output(raw, i);
        return longs(env, {v.kind, v.list, v.value_count, v.floats_per_value});
    });
}

MODEL_JNI(jbyteArray, skinName)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jbyteArray>(env, [&] {
        auto name = skin(raw, i).name;
        return bytes(env, name.data, name.byte_count);
    });
}

MODEL_JNI(jbyteArray, inputName)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jbyteArray>(env, [&] {
        auto name = input(raw, i).name;
        return bytes(env, name.data, name.byte_count);
    });
}

MODEL_JNI(jbyteArray, textureName)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jbyteArray>(env, [&] {
        auto name = texture(raw, i).name;
        return bytes(env, name.data, name.byte_count);
    });
}

MODEL_JNI(jbyteArray, texturePixels)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jbyteArray>(env, [&] {
        auto v = texture(raw, i);
        return bytes(env, v.pixels, v.byte_count);
    });
}

MODEL_JNI(jbyteArray, imageName)(JNIEnv* env, jobject, jlong raw, jint i, jint j) {
    return guarded<jbyteArray>(env, [&] {
        auto name = image(raw, i, j).name;
        return bytes(env, name.data, name.byte_count);
    });
}

MODEL_JNI(jbyteArray, imagePixels)(JNIEnv* env, jobject, jlong raw, jint i, jint j) {
    return guarded<jbyteArray>(env, [&] {
        auto v = image(raw, i, j);
        return bytes(env, v.pixels, v.byte_count);
    });
}

MODEL_JNI(jbyteArray, outputName)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jbyteArray>(env, [&] {
        auto name = output(raw, i).name;
        return bytes(env, name.data, name.byte_count);
    });
}

MODEL_JNI(jfloatArray, positions)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jfloatArray>(env, [&] {
        auto s = surface(raw, i);
        return floats(env, s.positions, 3 * s.vertex_count);
    });
}

MODEL_JNI(jfloatArray, normals)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jfloatArray>(env, [&] {
        auto s = surface(raw, i);
        return floats(env, s.normals, 3 * s.vertex_count);
    });
}

MODEL_JNI(jfloatArray, tangents)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jfloatArray>(env, [&] {
        auto s = surface(raw, i);
        return floats(env, s.tangents, 4 * s.vertex_count);
    });
}

MODEL_JNI(jfloatArray, uvs)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jfloatArray>(env, [&] {
        auto s = surface(raw, i);
        return floats(env, s.uvs, 2 * s.vertex_count);
    });
}

MODEL_JNI(jintArray, indices)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jintArray>(env, [&] {
        auto s = surface(raw, i);
        auto count = static_cast<jsize>(s.index_count);
        auto a = env->NewIntArray(count);
        if (a && count) env->SetIntArrayRegion(a, 0, count, reinterpret_cast<const jint*>(s.indices));
        return a;
    });
}

MODEL_JNI(jfloatArray, outputValues)(JNIEnv* env, jobject, jlong raw, jint i) {
    return guarded<jfloatArray>(env, [&] {
        auto v = output(raw, i);
        return floats(env, v.values, static_cast<std::size_t>(v.value_count)*v.floats_per_value);
    });
}
