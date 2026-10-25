# Android / Kotlin

A Kotlin API, JNI bridge and Android Bitmap adapter packaged as an AAR.
The JNI bridge statically links the runtime and C API; generation works
offline with compiled `.iprop` assets. See the
[shared contract](../../docs/native-bindings.md).

## Build

Use JDK 17+, Gradle 9.4.1, Android SDK platform 36, Build Tools 36.0.0,
NDK 28.2.13676358 and Android CMake 3.31.6. AGP 9.2.1 supplies Kotlin.
From a source checkout:

```sh
gradle -p backends/android :iprop:assembleRelease
```

Set `ANDROID_HOME` to the SDK, or set `sdk.dir` in an untracked
`backends/android/local.properties`. The AAR is
`backends/android/iprop/build/outputs/aar/iprop-release.aar`. It includes
arm64-v8a, armeabi-v7a and x86_64 JNI libraries, static libc++, 16 KiB page
alignment and R8 keep rules for JNI names/error construction. Minimum API: 23.
Native source lives in the full checkout; when using SDK-installed wrapper
sources, pass `-PipropSource=/path/to/infinite-props`.

Add the AAR as a local dependency to your app. Also add Kotlin stdlib if your
app does not already use Kotlin; a local AAR has no Maven dependency metadata.
Put compiled assets under the app's `src/main/assets/` directory.

## Use

```kotlin
import dev.infiniteprops.*

val bytes = context.assets.open("stone.iprop").use { it.readBytes() }
val image = Program(bytes).use { program ->
    program.generate(seed = 7u, inputs = mapOf(
        "tint" to InputValue.RGBA(Color(138, 146, 153))
    ))
}
val bitmap = image.toBitmap()
val collider = image.output("collider", owner = 0u)
```

Use `InputValue.F32`, `.U32`, or `.I32` for numeric inputs. Program calls and
close serialize; independent programs can execute concurrently. `use` closes
the native handle; results own their arrays. The caller owns returned Bitmaps.
`toBitmap()` preserves all RGBA bytes without premultiplication, including RGB
under zero alpha. For drawing through Android Canvas/View use the explicit
`toPremultipliedBitmap()` conversion; its alpha conversion is lossy. Raw `rgba`
retains the runtime's legacy alpha tag.

## Verify

Prepare shared fixtures and the complete [pixel corpus](../../docs/native-bindings.md#required-pixel-equality-gate), then run the real JNI bridge on a host JVM:

```sh
python3 backends/android/tests/run-host.py --kotlinc /path/to/kotlinc --java-home /path/to/jdk
```

This enables CheckJNI and tests actual native calls, float bits, names,
outputs, errors, ownership and concurrency. Device/emulator tests additionally
exercise every corpus case through the packaged Android libraries and compare
the complete Bitmap buffer byte-for-byte:

```sh
gradle -p backends/android :iprop:connectedDebugAndroidTest
```

No device is needed for `assembleRelease` or `assembleDebugAndroidTest`.
