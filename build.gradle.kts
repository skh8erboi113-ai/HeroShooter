// build.gradle.kts (root)
plugins {
    // Android Gradle Plugin - latest stable
    id("com.android.application") version "8.3.2" apply false
    id("com.android.library")     version "8.3.2" apply false
    id("org.jetbrains.kotlin.android") version "1.9.24" apply false
}

// Shared version catalog accessible across all modules
extra["sdkVersion"]     = 35
extra["minSdkVersion"]  = 26
extra["ndkVersion"]     = "27.1.12297006" // Latest stable NDK (r27b)
extra["cmakeVersion"]   = "3.28.3+"
