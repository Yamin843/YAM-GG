package com.yamgg.modview;

import android.app.Activity;
import android.content.Context;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class ModView extends GLSurfaceView implements GLSurfaceView.Renderer {

    private static final String TAG = "YAMGG";

    private static native void nativeOnSurfaceCreated();
    private static native void nativeOnSurfaceChanged(int width, int height);
    private static native void nativeOnDrawFrame(int width, int height);
    private static native void nativeOnTouch(int action, float x, float y, int pointerId);

    private static volatile ModView sInstance = null;
    private static volatile Activity sActivity = null;
    private static final Object sLock = new Object();

    public ModView(Context context) {
        super(context);
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setZOrderOnTop(true);
        setRenderer(this);
        setRenderMode(RENDERMODE_CONTINUOUSLY);
        setPreserveEGLContextOnPause(true);
        setFocusable(true);
        setFocusableInTouchMode(true);
        setClickable(true);
        setLongClickable(true);
        setHapticFeedbackEnabled(false);
        setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
        );
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        Log.i(TAG, "ModView.onSurfaceCreated");
        try {
            nativeOnSurfaceCreated();
        } catch (Throwable t) {
            Log.e(TAG, "onSurfaceCreated failed", t);
        }
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        Log.i(TAG, "ModView.onSurfaceChanged " + width + "x" + height);
        try {
            nativeOnSurfaceChanged(width, height);
        } catch (Throwable t) {
            Log.e(TAG, "onSurfaceChanged failed", t);
        }
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        try {
            nativeOnDrawFrame(getWidth(), getHeight());
        } catch (Throwable t) {
            Log.e(TAG, "onDrawFrame failed", t);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        try {
            int action = event.getActionMasked();
            int pointerIndex = event.getActionIndex();
            int pointerId = event.getPointerId(pointerIndex);
            float x = event.getX(pointerIndex);
            float y = event.getY(pointerIndex);
            nativeOnTouch(action, x, y, pointerId);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "onTouchEvent failed", t);
            return false;
        }
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        return true;
    }

    public static ModView getInstance() {
        return sInstance;
    }

    public static Activity getActivity() {
        return sActivity;
    }

    public static void detach() {
        final ModView view;
        final Activity act;
        synchronized (sLock) {
            view = sInstance;
            act = sActivity;
            sInstance = null;
            sActivity = null;
        }
        if (view == null || act == null) return;
        act.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    ViewGroup parent = (ViewGroup) view.getParent();
                    if (parent != null) parent.removeView(view);
                } catch (Throwable t) {
                    Log.e(TAG, "detach failed", t);
                }
            }
        });
    }

    public static void attach(final Activity activity) {
        if (activity == null) return;

        synchronized (sLock) {
            if (sInstance != null && sInstance.getParent() != null) {
                sActivity = activity;
                return;
            }
        }

        final ModView view = new ModView(activity);
        synchronized (sLock) {
            sInstance = view;
            sActivity = activity;
        }

        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    ViewGroup decor = (ViewGroup) activity.getWindow().getDecorView();
                    if (decor == null) return;

                    for (int i = 0; i < decor.getChildCount(); i++) {
                        if (decor.getChildAt(i) instanceof ModView) {
                            return;
                        }
                    }

                    ViewGroup.LayoutParams lp = new ViewGroup.LayoutParams(
                            ViewGroup.LayoutParams.MATCH_PARENT,
                            ViewGroup.LayoutParams.MATCH_PARENT);
                    decor.addView(view, lp);
                    Log.i(TAG, "ModView attached to DecorView");
                } catch (Throwable t) {
                    Log.e(TAG, "attach failed", t);
                }
            }
        });
    }
}
