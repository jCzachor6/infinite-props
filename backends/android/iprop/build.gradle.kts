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

androidComponents {
    onVariants(selector().all()) { variant ->
        variant.androidTest?.sources?.assets?.addStaticSourceDirectory("../../../build/binding-fixtures")
        variant.androidTest?.sources?.java?.addStaticSourceDirectory("../tests/shared")
    }
}

dependencies {
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
