package com.izzy2lost.a3do;

import android.content.Context;
import android.graphics.Insets;
import android.graphics.Rect;
import android.view.DisplayCutout;
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
 *
 * The cutout's safe inset is a whole strip along one edge, though, while the
 * camera is usually a small hole in the middle of it. So the bar insets and
 * the cutout's exact rectangles are also kept here, for the menus to avoid
 * only the hole (see Ui::launcher_area).
 */
public class A3DOSurface extends SDLSurface {

    /** Bar insets (left, top, right, bottom), then x, y, w, h per cutout,
     *  all in pixels. Read from native code through A3DOActivity. */
    static volatile int[] screenShape = new int[4];

    public A3DOSurface(Context context) {
        super(context);
    }

    @Override
    public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
        // Stored before SDL hears of the change, so the native side reads the
        // new shape when SDL's safe-area event reaches it.
        if (Build.VERSION.SDK_INT >= 30 /* Android 11 (R) */) {
            final Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
            final DisplayCutout cutout = insets.getDisplayCutout();
            final java.util.List<Rect> holes = cutout != null
                    ? cutout.getBoundingRects() : java.util.Collections.<Rect>emptyList();
            final int[] shape = new int[4 + holes.size() * 4];
            shape[0] = bars.left; shape[1] = bars.top;
            shape[2] = bars.right; shape[3] = bars.bottom;
            for (int i = 0; i < holes.size(); ++i) {
                final Rect r = holes.get(i);
                shape[4 + i * 4] = r.left;
                shape[5 + i * 4] = r.top;
                shape[6 + i * 4] = r.width();
                shape[7 + i * 4] = r.height();
            }
            screenShape = shape;
        }
        // SDL's own handling: it tracks the on-screen keyboard here.
        final WindowInsets result = super.onApplyWindowInsets(v, insets);
        if (Build.VERSION.SDK_INT >= 30 /* Android 11 (R) */) {
            final Insets safe = insets.getInsets(WindowInsets.Type.systemBars() |
                                                 WindowInsets.Type.displayCutout());
            SDLActivity.onNativeInsetsChanged(safe.left, safe.right, safe.top, safe.bottom);
        }
        return result;
    }
}
