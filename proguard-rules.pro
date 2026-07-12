# app/proguard-rules.pro

# ── Keep all GameActivity entry points ────────────────────────────────────────
-keep public class com.heroshooter.engine.** { *; }

# ── Keep AGDK GameActivity ────────────────────────────────────────────────────
-keep class androidx.games.** { *; }
-keep class com.google.androidgamesdk.** { *; }

# ── Keep Oboe callbacks ───────────────────────────────────────────────────────
-keep class com.google.oboe.** { *; }

# ── Native method signatures (JNI) ────────────────────────────────────────────
-keepclasseswithmembernames class * {
    native <methods>;
}

# ── Android Activity lifecycle ────────────────────────────────────────────────
-keep public class * extends android.app.Activity
-keep public class * extends androidx.games.app.GameActivity

# ── Suppress warnings for pre-verified APIs ───────────────────────────────────
-dontwarn androidx.games.**
-dontwarn com.google.androidgamesdk.**
