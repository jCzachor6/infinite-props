plugins { id("com.android.library") }

android {
    namespace = "dev.infiniteprops"
    compileSdk = 36
    ndkVersion = "28.2.13676358"
    defaultConfig {
        minSdk = 23
        consumerProguardFiles("consumer-rules.pro")
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
        externalNativeBuild { cmake {
            arguments += "-DANDROID_STL=c++_static"
            providers.gradleProperty("ipropSource").orNull?.let { arguments += "-DIPROP_SOURCE=$it" }
            targets += "iprop_jni"
        } }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.31.6" } }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
}

abstract class BindingTestAssets : DefaultTask() {
    @get:InputDirectory abstract val fixtures: DirectoryProperty
    @get:InputDirectory abstract val corpus: DirectoryProperty
    @get:OutputDirectory abstract val output: DirectoryProperty
    @get:javax.inject.Inject abstract val files: org.gradle.api.file.FileSystemOperations
    @TaskAction fun prepare() {
        files.sync { from(fixtures); from(corpus); into(output) }
    }
}
val bindingTestAssets = tasks.register<BindingTestAssets>("prepareBindingTestAssets") {
    fixtures.set(layout.projectDirectory.dir("../../../build/binding-fixtures"))
    corpus.set(layout.projectDirectory.dir("../../../build/pixel-corpus"))
    output.set(layout.buildDirectory.dir("generated/binding-test-assets"))
}

androidComponents {
    onVariants(selector().all()) { variant ->
        variant.androidTest?.sources?.assets?.addGeneratedSourceDirectory(bindingTestAssets) { it.output }
        variant.androidTest?.sources?.java?.addStaticSourceDirectory("../tests/shared")
    }
}

dependencies {
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
