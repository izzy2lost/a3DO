package com.izzy2lost.a3do;

import android.content.Context;
import android.graphics.Insets;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/**
 * SDL's surface, reporting the safe area the way edge-to-edge means it.
 *
 * SDL counts the system gesture zones as unsafe along with the bars and the
 * display cutout. The back-gesture zones run the full height of both sides
 * and stay put even while the app is immersive with the bars hidden, so every
 * screen ended up with an empty band down each side: the library never
 * reached the edges, and the touch controls sat further in than they needed
 * to. Gesture zones say where a drag might be taken by the system, not where
 * content must not be drawn.
 *
 * What content keeps clear of is the safe drawing area: system bars (zero
 * while hidden) and the display cutout. The native side lays the UI out from
 * that and keeps drawing its backgrounds and the game picture under all of it.
 */
public class A3DOSurface extends SDLSurface {

    public A3DOSurface(Context context) {
        super(context);
    }

    @Override
    public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
        // SDL's own handling first: it tracks the on-screen keyboard here.
        final WindowInsets result = super.onApplyWindowInsets(v, insets);
        if (Build.VERSION.SDK_INT >= 30 /* Android 11 (R) */) {
            final Insets safe = insets.getInsets(WindowInsets.Type.systemBars() |
                                                 WindowInsets.Type.displayCutout());
            SDLActivity.onNativeInsetsChanged(safe.left, safe.right, safe.top, safe.bottom);
        }
        return result;
    }
}
