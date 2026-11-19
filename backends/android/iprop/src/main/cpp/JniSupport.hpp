#pragma once
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

inline jlong pointer(const void* value) { return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(value)); }

inline jbyteArray bytes(JNIEnv* env, const void* data, std::uint64_t size) {
    if (size > static_cast<std::uint64_t>(std::numeric_limits<jsize>::max())) throw std::length_error("JNI array is too large");
    auto array = env->NewByteArray(static_cast<jsize>(size));
    if (array && size) env->SetByteArrayRegion(array, 0, static_cast<jsize>(size), static_cast<const jbyte*>(data));
    return array;
}
inline jlongArray longs(JNIEnv* env, std::initializer_list<jlong> values) {
    auto array = env->NewLongArray(static_cast<jsize>(values.size()));
    if (array) env->SetLongArrayRegion(array, 0, static_cast<jsize>(values.size()), values.begin());
    return array;
}
inline jlong bits(float value) { return std::bit_cast<std::uint32_t>(value); }
inline jlong color(iprop_color_rgba8 c) { return (std::uint32_t(c.r) << 24) | (std::uint32_t(c.g) << 16) | (std::uint32_t(c.b) << 8) | c.a; }

// NewStringUTF uses modified UTF-8. Decode ordinary byte-counted UTF-8 through
// java.lang.String instead, preserving Unicode and embedded U+0000.
inline jstring string(JNIEnv* env, const char* data) {
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

inline void setNamed(JNIEnv* env, iprop_parameters* p, jbyteArray name, jint type, jint raw) {
    Failure f; const auto count = env->GetArrayLength(name);
    std::vector<char> buffer(static_cast<std::size_t>(count));
    if (count) env->GetByteArrayRegion(name, 0, count, reinterpret_cast<jbyte*>(buffer.data()));
    if (env->ExceptionCheck()) return;
    iprop_utf8_view view{buffer.data(), static_cast<std::uint32_t>(count)};
    const auto u = static_cast<std::uint32_t>(raw);
    if (type == 0) f.check(iprop_parameters_set_color_named(p, view,
        {std::uint8_t(u >> 24), std::uint8_t(u >> 16), std::uint8_t(u >> 8), std::uint8_t(u)}, &f.error));
    else if (type == 1) f.check(iprop_parameters_set_f32_named(p, view, std::bit_cast<float>(u), &f.error));
    else f.check(iprop_parameters_set_u32_named(p, view, u, &f.error));
}
inline void requireRuntime(Failure& f) {
    iprop_library_info_ex library{};
    iprop_library_info_ex_init(&library, sizeof(library));
    f.check(iprop_library_query_ex(&library, &f.error));
    const std::uint64_t required = IPROP_CAP_PART_OUTPUTS | IPROP_CAP_RESULT_RESOURCES | IPROP_CAP_EXECUTION_LIMITS |
        IPROP_CAP_AUX_IMAGES | IPROP_CAP_DERIVED_NORMAL;
    if (library.api_major != 0 || library.api_minor < 7 || library.abi_major != 0 || library.abi_minor != 5 ||
        (library.capabilities & required) != required)
        throw std::invalid_argument("C API 0.7 / ABI 0.5 with typed results, execution limits and auxiliary images required");
}
} // namespace
