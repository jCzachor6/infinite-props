#include <iprop.h>
#include <jni.h>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
struct Failure {
    iprop_error_ex error{};
    char message[8192]{};
    Failure() {
        iprop_error_ex_init(&error, sizeof(error));
        error.message_buffer = message;
        error.message_capacity = sizeof(message);
    }
    void check(iprop_status_ex status) { if (status != IPROP_EX_OK) throw *this; }
};

using Parameters = std::unique_ptr<iprop_parameters, decltype(&iprop_parameters_free)>;
struct Context {
    std::unique_ptr<iprop_program, decltype(&iprop_program_free)> program{nullptr, iprop_program_free};
    Parameters parameters{nullptr, iprop_parameters_free};
};
Context& context(jlong value) {
    if (value == 0) throw std::invalid_argument("Program is closed");
    return *reinterpret_cast<Context*>(static_cast<std::uintptr_t>(value));
}
iprop_result* result(jlong value) { return reinterpret_cast<iprop_result*>(static_cast<std::uintptr_t>(value)); }
jlong pointer(const void* value) { return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(value)); }

jbyteArray bytes(JNIEnv* env, const void* data, std::uint64_t size) {
    if (size > static_cast<std::uint64_t>(std::numeric_limits<jsize>::max())) throw std::length_error("JNI array is too large");
    auto array = env->NewByteArray(static_cast<jsize>(size));
    if (array && size) env->SetByteArrayRegion(array, 0, static_cast<jsize>(size), static_cast<const jbyte*>(data));
    return array;
}
jlongArray longs(JNIEnv* env, std::initializer_list<jlong> values) {
    auto array = env->NewLongArray(static_cast<jsize>(values.size()));
    if (array) env->SetLongArrayRegion(array, 0, static_cast<jsize>(values.size()), values.begin());
    return array;
}
jlong bits(float value) { return std::bit_cast<std::uint32_t>(value); }
jlong color(iprop_color_rgba8 c) { return (std::uint32_t(c.r) << 24) | (std::uint32_t(c.g) << 16) | (std::uint32_t(c.b) << 8) | c.a; }

// NewStringUTF uses modified UTF-8. Decode ordinary byte-counted UTF-8 through
// java.lang.String instead, preserving Unicode and embedded U+0000.
jstring string(JNIEnv* env, const char* data) {
    auto encoded = bytes(env, data, std::strlen(data));
    if (!encoded) return nullptr;
    auto klass = env->FindClass("java/lang/String");
    if (!klass) return nullptr;
    auto ctor = env->GetMethodID(klass, "<init>", "([BLjava/lang/String;)V");
    if (!ctor) return nullptr;
    auto encoding = env->NewStringUTF("UTF-8");
    if (!encoding) return nullptr;
    return static_cast<jstring>(env->NewObject(klass, ctor, encoded, encoding));
}

template<class R, class F> R guarded(JNIEnv* env, F body) {
    try { return body(); }
    catch (const Failure& failure) {
        const auto& e = failure.error;
        auto message = string(env, failure.message);
        if (!message) return R{};
        auto klass = env->FindClass("dev/infiniteprops/IpropException");
        if (!klass) return R{};
        auto ctor = env->GetMethodID(klass, "<init>", "(Ljava/lang/String;IIIJIIIIIZ)V");
        if (!ctor) return R{};
        auto exception = static_cast<jthrowable>(env->NewObject(klass, ctor, message,
            static_cast<jint>(e.status), static_cast<jint>(e.reason), static_cast<jint>(e.stage),
            static_cast<jlong>(e.byte_offset), static_cast<jint>(e.location_flags), static_cast<jint>(e.offset_space),
            static_cast<jint>(e.function_index), static_cast<jint>(e.pc), static_cast<jint>(e.debug_line),
            static_cast<jboolean>(e.message_truncated != 0)));
        if (exception) env->Throw(exception);
    }
    catch (const std::bad_alloc&) { auto klass = env->FindClass("java/lang/OutOfMemoryError"); if (klass) env->ThrowNew(klass, "Infinite Props JNI allocation failed"); }
    catch (const std::exception&) { auto klass = env->FindClass("java/lang/IllegalStateException"); if (klass) env->ThrowNew(klass, "Invalid Infinite Props JNI operation"); }
    return R{};
}

iprop_result_output_view_ex output(jlong value, jint index) {
    Failure f;
    iprop_result_output_view_ex view{};
    iprop_result_output_view_ex_init(&view, sizeof(view));
    f.check(iprop_result_output_get(result(value), static_cast<std::uint32_t>(index), &view, &f.error));
    return view;
}
iprop_input_info_ex input(jlong value, jint index) {
    Failure f;
    iprop_input_info_ex info{};
    iprop_input_info_ex_init(&info, sizeof(info));
    f.check(iprop_program_input_get(context(value).program.get(), static_cast<std::uint32_t>(index), &info, &f.error));
    return info;
}
iprop_image_view_ex image(jlong value) {
    Failure f;
    iprop_image_view_ex view{};
    iprop_image_view_ex_init(&view, sizeof(view));
    f.check(iprop_result_query_image(result(value), &view, &f.error));
    return view;
}
} // namespace

#define JNI_METHOD(type, name) extern "C" JNIEXPORT type JNICALL Java_dev_infiniteprops_Native_##name

JNI_METHOD(jlong, load)(JNIEnv* env, jobject, jbyteArray data) {
    return guarded<jlong>(env, [&] {
        Failure f;
        iprop_library_info_ex library{};
        iprop_library_info_ex_init(&library, sizeof(library));
        f.check(iprop_library_query_ex(&library, &f.error));
        if (library.api_major != 0 || library.api_minor < 4 || library.abi_major != 0 || library.abi_minor != 5)
            throw std::invalid_argument("C API 0.4 / ABI 0.5 required");
        auto owner = std::make_unique<Context>();
        const auto count = env->GetArrayLength(data);
        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(count) + 1);
        env->GetByteArrayRegion(data, 0, count, reinterpret_cast<jbyte*>(buffer.data()));
        if (env->ExceptionCheck()) return jlong{};
        iprop_program* program = nullptr;
        f.check(iprop_program_load_ex(buffer.data(), static_cast<std::uint64_t>(count), &program, &f.error));
        owner->program.reset(program);
        return pointer(owner.release());
    });
}
JNI_METHOD(void, close)(JNIEnv*, jobject, jlong value) { delete reinterpret_cast<Context*>(static_cast<std::uintptr_t>(value)); }
JNI_METHOD(jlongArray, metadata)(JNIEnv* env, jobject, jlong value) {
    return guarded<jlongArray>(env, [&] {
        Failure f; iprop_program_info_ex m{}; iprop_program_info_ex_init(&m, sizeof(m));
        f.check(iprop_program_query(context(value).program.get(), &m, &f.error));
        return longs(env, {m.size_x, m.size_y, bits(m.origin_x), bits(m.origin_y), static_cast<jlong>(m.fuel),
            m.has_emission, color(m.emission_color), bits(m.emission_offset_x), bits(m.emission_offset_y), bits(m.emission_radius), m.container_flags});
    });
}
JNI_METHOD(jint, inputCount)(JNIEnv* env, jobject, jlong value) {
    return guarded<jint>(env, [&] { Failure f; std::uint32_t count = 0;
        f.check(iprop_program_input_count(context(value).program.get(), &count, &f.error)); return static_cast<jint>(count); });
}
JNI_METHOD(jlongArray, inputInfo)(JNIEnv* env, jobject, jlong value, jint index) {
    return guarded<jlongArray>(env, [&] { auto v = input(value, index);
        return longs(env, {v.type, v.type == 0 ? color(v.color_default) : v.type == 1 ? bits(v.f32_default) : jlong(v.u32_default)}); });
}
JNI_METHOD(jbyteArray, inputName)(JNIEnv* env, jobject, jlong value, jint index) {
    return guarded<jbyteArray>(env, [&] { auto v = input(value, index); return bytes(env, v.name.data, v.name.byte_count); });
}
JNI_METHOD(void, begin)(JNIEnv* env, jobject, jlong value) {
    guarded<int>(env, [&] { Failure f; auto& c = context(value); iprop_parameters* p = nullptr;
        f.check(iprop_parameters_create(c.program.get(), &p, &f.error)); c.parameters.reset(p); return 0; });
}
JNI_METHOD(void, end)(JNIEnv* env, jobject, jlong value) { guarded<int>(env, [&] { context(value).parameters.reset(); return 0; }); }
JNI_METHOD(void, set)(JNIEnv* env, jobject, jlong value, jbyteArray name, jint type, jint raw) {
    guarded<int>(env, [&] {
        Failure f; const auto count = env->GetArrayLength(name);
        std::vector<char> buffer(static_cast<std::size_t>(count));
        if (count) env->GetByteArrayRegion(name, 0, count, reinterpret_cast<jbyte*>(buffer.data()));
        if (env->ExceptionCheck()) return 0;
        iprop_utf8_view view{buffer.data(), static_cast<std::uint32_t>(count)};
        auto* p = context(value).parameters.get();
        const auto u = static_cast<std::uint32_t>(raw);
        if (type == 0) f.check(iprop_parameters_set_color_named(p, view,
            {std::uint8_t(u >> 24), std::uint8_t(u >> 16), std::uint8_t(u >> 8), std::uint8_t(u)}, &f.error));
        else if (type == 1) f.check(iprop_parameters_set_f32_named(p, view, std::bit_cast<float>(u), &f.error));
        else f.check(iprop_parameters_set_u32_named(p, view, u, &f.error));
        return 0;
    });
}
JNI_METHOD(jlong, generate)(JNIEnv* env, jobject, jlong value, jint seed, jint width, jint height) {
    return guarded<jlong>(env, [&] { Failure f; auto& c = context(value);
        iprop_generate_options_ex options{}; iprop_generate_options_ex_init(&options, sizeof(options));
        options.seed = static_cast<std::uint32_t>(seed); options.size_x = static_cast<std::uint32_t>(width); options.size_y = static_cast<std::uint32_t>(height);
        iprop_result* generated = nullptr;
        f.check(iprop_generate_ex(c.program.get(), &options, c.parameters.get(), &generated, &f.error)); return pointer(generated); });
}
JNI_METHOD(void, freeResult)(JNIEnv*, jobject, jlong value) { iprop_result_free(result(value)); }
JNI_METHOD(jlongArray, imageInfo)(JNIEnv* env, jobject, jlong value) {
    return guarded<jlongArray>(env, [&] { auto v = image(value); return longs(env, {v.width, v.height,
        static_cast<jlong>(v.row_stride), v.pixel_format, v.alpha_mode, v.origin}); });
}
JNI_METHOD(jbyteArray, pixels)(JNIEnv* env, jobject, jlong value) {
    return guarded<jbyteArray>(env, [&] { auto v = image(value); return bytes(env, v.pixels, v.byte_count); });
}
JNI_METHOD(jlongArray, statistics)(JNIEnv* env, jobject, jlong value) {
    return guarded<jlongArray>(env, [&] { Failure f; iprop_result_stats_ex s{}; iprop_result_stats_ex_init(&s, sizeof(s));
        f.check(iprop_result_query_stats(result(value), &s, &f.error)); return longs(env,
            {static_cast<jlong>(s.fuel_budget), static_cast<jlong>(s.fuel_remaining), static_cast<jlong>(s.fuel_used),
             static_cast<jlong>(s.image_byte_count), static_cast<jlong>(s.output_element_count)}); });
}
JNI_METHOD(jint, outputCount)(JNIEnv* env, jobject, jlong value) {
    return guarded<jint>(env, [&] { Failure f; std::uint32_t count = 0;
        f.check(iprop_result_output_count(result(value), &count, &f.error)); return static_cast<jint>(count); });
}
JNI_METHOD(jlongArray, outputInfo)(JNIEnv* env, jobject, jlong value, jint index) {
    return guarded<jlongArray>(env, [&] { auto v = output(value, index); return longs(env, {v.owner, v.kind, v.is_list, v.element_count}); });
}
JNI_METHOD(jbyteArray, outputName)(JNIEnv* env, jobject, jlong value, jint index) {
    return guarded<jbyteArray>(env, [&] { auto v = output(value, index); return bytes(env, v.name.data, v.name.byte_count); });
}
JNI_METHOD(jintArray, outputValues)(JNIEnv* env, jobject, jlong value, jint index) {
    return guarded<jintArray>(env, [&] {
        auto v = output(value, index);
        const auto count = static_cast<std::size_t>(v.element_count) * v.stride_floats;
        if (count > static_cast<std::size_t>(std::numeric_limits<jsize>::max())) throw std::length_error("Output is too large");
        std::vector<jint> packed(count);
        if (count) std::memcpy(packed.data(), v.values, count * sizeof(float));
        auto array = env->NewIntArray(static_cast<jsize>(count));
        if (array && count) env->SetIntArrayRegion(array, 0, static_cast<jsize>(count), packed.data());
        return array;
    });
}
