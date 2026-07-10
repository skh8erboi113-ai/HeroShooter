// app/build.gradle.kts
import com.android.build.api.dsl.Packaging

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace      = "com.heroshooter.engine"
    compileSdk     = rootProject.extra["sdkVersion"] as Int
    ndkVersion     = rootProject.extra["ndkVersion"] as String

    defaultConfig {
        applicationId  = "com.heroshooter.engine"
        minSdk         = rootProject.extra["minSdkVersion"] as Int
        targetSdk      = rootProject.extra["sdkVersion"] as Int
        versionCode    = 1
        versionName    = "0.1.0-alpha"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        // ── CMake configuration ──────────────────────────────────────────
        externalNativeBuild {
            cmake {
                // Pass version info and ABI into CMake
                arguments(
                    "-DANDROID_STL=c++_shared",         // Use shared libc++ for AGDK
                    "-DANDROID_ARM_NEON=TRUE",           // Enable NEON SIMD
                    "-DANDROID_TOOLCHAIN=clang",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DENGINE_VERSION_MAJOR=0",
                    "-DENGINE_VERSION_MINOR=1",
                    // Enable Vulkan validation layers only in debug
                    "-DENABLE_VULKAN_VALIDATION=\${if (buildType == \"debug\") \"ON\" else \"OFF\"}",
                )
                cppFlags(
                    "-std=c++20",
                    "-Wall", "-Wextra", "-Werror",
                    "-O3",
                    "-fno-rtti",
                    "-fno-exceptions",
                    "-march=armv8-a",               // Target ARMv8-A baseline
                    "-fvisibility=hidden",           // Hide symbols by default
                    "-ffunction-sections",           // Enable dead code elimination
                    "-fdata-sections",
                )
                targets("hero_shooter_engine")       // The .so target name
            }
        }

        // ── ABI Splits ───────────────────────────────────────────────────
        // Build separate APKs per ABI to minimize download size
        ndk {
            // Prioritize arm64-v8a (modern devices); keep armeabi-v7a for coverage
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }
    }

    // ── CMake source location ────────────────────────────────────────────
    externalNativeBuild {
        cmake {
            path    = file("src/main/cpp/CMakeLists.txt")
            version = rootProject.extra["cmakeVersion"] as String
        }
    }

    // ── ABI Splits (produces per-ABI APKs) ──────────────────────────────
    splits {
        abi {
            isEnable             = true
            reset()
            include("arm64-v8a", "armeabi-v7a")
            isUniversalApk       = false  // No fat APK
        }
    }

    buildTypes {
        debug {
            isDebuggable            = true
            isJniDebuggable         = true
            isMinifyEnabled         = false
            externalNativeBuild {
                cmake {
                    arguments(
                        "-DCMAKE_BUILD_TYPE=Debug",
                        "-DENABLE_VULKAN_VALIDATION=ON",
                    )
                    cppFlags("-O0", "-g3") // Override for debug symbols
                }
            }
        }
        release {
            isMinifyEnabled         = true
            isShrinkResources       = true
            isDebuggable            = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    buildFeatures {
        viewBinding    = true
        buildConfig    = true
        prefab         = true   // Required for AGDK AAR prefab packages
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
        freeCompilerArgs += listOf(
            "-opt-in=kotlin.RequiresOptIn",
            "-Xjvm-default=all",
        )
    }

    packaging {
        jniLibs {
            // Keep native libs uncompressed for direct mmap access (faster load)
            useLegacyPackaging = false
        }
        resources {
            excludes += listOf("META-INF/LICENSE", "META-INF/*.kotlin_module")
        }
    }
}

dependencies {
    // ── AndroidX Core ────────────────────────────────────────────────────
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.3")

    // ── AGDK: GameActivity (replaces NativeActivity) ─────────────────────
    // Prefab package — CMake will link against these via find_package()
    implementation("androidx.games:games-activity:3.0.3")

    // ── AGDK: Frame Pacing (Swappy) ───────────────────────────────────────
    implementation("androidx.games:games-frame-pacing:2.1.0")

    // ── AGDK: Memory Advice ───────────────────────────────────────────────
    implementation("androidx.games:games-memory-advice:2.0.0-beta01")

    // ── AGDK: Performance Tuner ───────────────────────────────────────────
    implementation("androidx.games:games-performance-tuner:2.0.0-beta01")

    // ── Oboe: Low-latency audio ───────────────────────────────────────────
    implementation("com.google.oboe:oboe:1.9.0")

    // ── Testing ───────────────────────────────────────────────────────────
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.6.1")
}
