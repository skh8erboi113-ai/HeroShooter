// app/src/main/java/com/heroshooter/engine/MainActivity.kt
package com.heroshooter.engine

import android.os.Bundle
import android.view.WindowManager
import androidx.games.app.GameActivity    // AGDK GameActivity

/**
 * MainActivity extends GameActivity (AGDK), not AppCompatActivity.
 *
 * GameActivity handles:
 *   • JNI bootstrapping (loads the .so and calls android_main on a thread)
 *   • ANativeWindow lifecycle forwarding
 *   • Input event routing to the native input system
 *   • Window insets (cutouts, navigation bars) reporting to native code
 *
 * We do minimal work here — the real logic lives in C++ GameEngine.
 */
class MainActivity : GameActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Keep screen on during gameplay
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // Request full-screen immersive mode (hide system bars)
        // Android 11+: use WindowInsetsController
        window.insetsController?.let { controller ->
            controller.hide(
                android.view.WindowInsets.Type.systemBars() or
                android.view.WindowInsets.Type.navigationBars()
            )
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        // Re-apply immersive mode when focus is regained (e.g., after dialog)
        if (hasFocus) {
            window.insetsController?.hide(
                android.view.WindowInsets.Type.systemBars()
            )
        }
    }
}
