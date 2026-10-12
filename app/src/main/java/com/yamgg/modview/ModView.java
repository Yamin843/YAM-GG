package com.yamgg.modview;

import android.app.Activity;
import android.content.Context;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.view.inputmethod.InputConnectionWrapper;
import android.view.inputmethod.InputConnection;
import android.widget.EditText;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class ModView extends GLSurfaceView implements GLSurfaceView.Renderer {

    private static final String TAG = "YAMGG";

    // Native methods — signatures must match the JNINativeMethod table in
    // JNI/NativeMethods.cpp exactly.
    private static native void    nativeOnSurfaceCreated();
    private static native void    nativeOnSurfaceChanged(int width, int height);
    private static native void    nativeOnDrawFrame(int width, int height);
    private static native void    nativeOnTouch(int action, float x, float y, int pointerId);
    private static native void    nativeOnKey(int keyCode, int action);
    private static native void    nativeOnChar(int codepoint);
    private static native void    nativeOnScroll(float dx, float dy);
    private static native boolean nativeWantCaptureMouse();
    private static native boolean nativeWantTextInput();
    private static native boolean nativeHitTest(float x, float y);
    private static native String  nativeGetPendingCmd();

    private static volatile ModView sInstance = null;
    private static volatile Activity sActivity = null;
    private static final Object sLock = new Object();
    private static EditText sHiddenInput = null;

    private boolean twoFingerScrollActive = false;
    private float   twoFingerLastY = 0f;
    private long    lastScrollSentMs = 0L;
    private float   smoothedDy = 0f;
    private boolean touchOwned = false;
    private boolean swipeActive = false;
    private float   swipeStartX = 0f;
    private float   swipeStartY = 0f;
    private float   swipeLastY = 0f;
    private volatile boolean keyboardShown = false;

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
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        Log.i(TAG, "ModView.onSurfaceCreated");
        try { nativeOnSurfaceCreated(); }
        catch (Throwable t) { Log.e(TAG, "onSurfaceCreated failed", t); }
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        Log.i(TAG, "ModView.onSurfaceChanged " + width + "x" + height);
        try { nativeOnSurfaceChanged(width, height); }
        catch (Throwable t) { Log.e(TAG, "onSurfaceChanged failed", t); }
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        try { nativeOnDrawFrame(getWidth(), getHeight()); }
        catch (Throwable t) { Log.e(TAG, "onDrawFrame failed", t); }
        syncSoftKeyboard();
    }

    private void syncSoftKeyboard() {
        try {
            final boolean want = nativeWantTextInput();
            if (want == keyboardShown) return;
            keyboardShown = want;
            final Activity activity = sActivity;
            if (activity == null) return;
            activity.runOnUiThread(new Runnable() {
                @Override public void run() {
                    try {
                        if (sHiddenInput == null) return;
                        InputMethodManager imm = (InputMethodManager)
                            activity.getSystemService(Context.INPUT_METHOD_SERVICE);
                        if (imm == null) return;
                        if (want) {
                            sHiddenInput.requestFocus();
                            sHiddenInput.postDelayed(new Runnable() {
                                @Override public void run() {
                                    try {
                                        sHiddenInput.requestFocus();
                                        imm.toggleSoftInput(
                                            InputMethodManager.SHOW_FORCED, 0);
                                    } catch (Throwable t) {}
                                }
                            }, 60);
                        } else {
                            imm.hideSoftInputFromWindow(
                                sHiddenInput.getWindowToken(), 0);
                        }
                    } catch (Throwable t) {
                        Log.e(TAG, "syncSoftKeyboard", t);
                    }
                }
            });
        } catch (Throwable t) {}
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int pointerCount = event.getPointerCount();

        // ── Two-finger → scroll ───────────────────────────────────
        if (pointerCount >= 2) {
            if (action == MotionEvent.ACTION_POINTER_DOWN) {
                if (!touchOwned) return false;
                try { nativeOnTouch(MotionEvent.ACTION_CANCEL, 0f, 0f, 0); }
                catch (Throwable t) {}
                twoFingerScrollActive = true;
                twoFingerLastY = (event.getY(0) + event.getY(1)) * 0.5f;
                lastScrollSentMs = 0L;
                smoothedDy = 0f;
                return true;
            }
            if (action == MotionEvent.ACTION_MOVE && twoFingerScrollActive) {
                float curY = (event.getY(0) + event.getY(1)) * 0.5f;
                float dy = curY - twoFingerLastY;
                twoFingerLastY = curY;
                smoothedDy = smoothedDy * 0.70f + dy * 0.30f;
                long now = System.currentTimeMillis();
                if (now - lastScrollSentMs >= 8L) {
                    lastScrollSentMs = now;
                    float scroll = -smoothedDy * 0.025f;
                    try { nativeOnScroll(0f, scroll); } catch (Throwable t) {}
                }
                return true;
            }
            if (action == MotionEvent.ACTION_POINTER_UP) {
                twoFingerScrollActive = false;
                smoothedDy = 0f;
                try { nativeOnTouch(MotionEvent.ACTION_CANCEL, 0f, 0f, 0); }
                catch (Throwable t) {}
                return touchOwned;
            }
            if (action == MotionEvent.ACTION_UP
                || action == MotionEvent.ACTION_CANCEL) {
                twoFingerScrollActive = false;
                smoothedDy = 0f;
                touchOwned = false;
                try { nativeOnTouch(action, 0f, 0f, 0); }
                catch (Throwable t) {}
                return true;
            }
            if (twoFingerScrollActive) return true;
        }

        // ── Single-finger (simple passthrough) ────────────────────
        if (action == MotionEvent.ACTION_DOWN) {
            twoFingerScrollActive = false;
            smoothedDy = 0f;
            float x = event.getX();
            float y = event.getY();
            boolean inside = false;
            try { inside = nativeHitTest(x, y); }
            catch (Throwable t) { inside = false; }
            touchOwned = inside;
            if (!touchOwned) return false;
        }

        if (!touchOwned) return false;

        try {
            int pointerIndex = event.getActionIndex();
            int pointerId = event.getPointerId(pointerIndex);
            float x = event.getX(pointerIndex);
            float y = event.getY(pointerIndex);
            nativeOnTouch(action, x, y, pointerId);
        } catch (Throwable t) {
            Log.e(TAG, "dispatchTouchEvent failed", t);
        }

        if (action == MotionEvent.ACTION_UP
            || action == MotionEvent.ACTION_CANCEL) {
            touchOwned = false;
        }
        return true;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        return true;
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        if (event.getAction() == MotionEvent.ACTION_SCROLL
            && (event.getSource()
                & android.view.InputDevice.SOURCE_CLASS_POINTER) != 0) {
            float v = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
            float h = event.getAxisValue(MotionEvent.AXIS_HSCROLL);
            try { nativeOnScroll(h, v); } catch (Throwable t) {}
            try { return nativeWantCaptureMouse(); } catch (Throwable t) {}
        }
        return super.onGenericMotionEvent(event);
    }

    private static boolean isConsumedKey(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_BACK:
            case KeyEvent.KEYCODE_ESCAPE:
            case KeyEvent.KEYCODE_ENTER:
            case KeyEvent.KEYCODE_TAB:
            case KeyEvent.KEYCODE_DEL:
            case KeyEvent.KEYCODE_FORWARD_DEL:
            case KeyEvent.KEYCODE_DPAD_UP:
            case KeyEvent.KEYCODE_DPAD_DOWN:
            case KeyEvent.KEYCODE_DPAD_LEFT:
            case KeyEvent.KEYCODE_DPAD_RIGHT:
                return true;
            default:
                return false;
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        try { nativeOnKey(keyCode, 0); } catch (Throwable t) {}
        return isConsumedKey(keyCode) || super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        try { nativeOnKey(keyCode, 1); } catch (Throwable t) {}
        return isConsumedKey(keyCode) || super.onKeyUp(keyCode, event);
    }

    public static ModView getInstance() { return sInstance; }
    public static Activity getActivity() { return sActivity; }

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
            @Override public void run() {
                try {
                    ViewGroup parent = (ViewGroup) view.getParent();
                    if (parent != null) parent.removeView(view);
                } catch (Throwable t) { Log.e(TAG, "detach failed", t); }
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

        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    ViewGroup decor = (ViewGroup) activity.getWindow().getDecorView();
                    if (decor == null) return;
                    for (int i = 0; i < decor.getChildCount(); i++) {
                        if (decor.getChildAt(i) instanceof ModView) return;
                    }

                    final ModView view = new ModView(activity);
                    synchronized (sLock) {
                        sInstance = view;
                        sActivity = activity;
                    }

                    ViewGroup.LayoutParams lp = new ViewGroup.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT);
                    decor.addView(view, lp);

                    try {
                        if (sHiddenInput == null) {
                            final EditText et = new EditText(activity) {
                                @Override
                                public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
                                    InputConnection ic = super.onCreateInputConnection(outAttrs);
                                    if (ic == null) return null;
                                    outAttrs.inputType = InputType.TYPE_CLASS_TEXT
                                        | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                                        | InputType.TYPE_TEXT_FLAG_MULTI_LINE;
                                    outAttrs.imeOptions |= EditorInfo.IME_FLAG_NO_EXTRACT_UI
                                        | EditorInfo.IME_FLAG_NO_FULLSCREEN
                                        | EditorInfo.IME_FLAG_NO_ENTER_ACTION;
                                    return new InputConnectionWrapper(ic, false) {
                                        @Override
                                        public boolean commitText(CharSequence text, int newCursor) {
                                            try {
                                                for (int i = 0; i < text.length(); i++) {
                                                    int cp = (int) text.charAt(i);
                                                    if (cp == '\n') {
                                                        nativeOnKey(KeyEvent.KEYCODE_ENTER, 0);
                                                        nativeOnKey(KeyEvent.KEYCODE_ENTER, 1);
                                                    } else {
                                                        nativeOnChar(cp);
                                                    }
                                                }
                                            } catch (Throwable t) {}
                                            return true;
                                        }
                                        @Override
                                        public boolean setComposingText(CharSequence text, int newCursor) {
                                            return commitText(text, newCursor);
                                        }
                                        @Override
                                        public boolean deleteSurroundingText(int before, int after) {
                                            try {
                                                for (int i = 0; i < before; i++) {
                                                    nativeOnKey(KeyEvent.KEYCODE_DEL, 0);
                                                    nativeOnKey(KeyEvent.KEYCODE_DEL, 1);
                                                }
                                            } catch (Throwable t) {}
                                            return true;
                                        }
                                        @Override
                                        public boolean sendKeyEvent(KeyEvent event) {
                                            int code = event.getKeyCode();
                                            if (code == KeyEvent.KEYCODE_ENTER
                                                || code == KeyEvent.KEYCODE_DEL
                                                || code == KeyEvent.KEYCODE_TAB
                                                || code == KeyEvent.KEYCODE_ESCAPE) {
                                                int act = event.getAction();
                                                nativeOnKey(code, act == KeyEvent.ACTION_DOWN ? 0 : 1);
                                                return true;
                                            }
                                            return super.sendKeyEvent(event);
                                        }
                                    };
                                }
                            };
                            et.setLayoutParams(new ViewGroup.LayoutParams(1, 1));
                            et.setAlpha(0.01f);
                            et.setBackground(null);
                            et.setCursorVisible(false);
                            et.setTextColor(0);
                            et.setPadding(0, 0, 0, 0);
                            et.setSingleLine(false);
                            et.setInputType(InputType.TYPE_CLASS_TEXT
                                | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                                | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
                            et.setImeOptions(EditorInfo.IME_ACTION_NONE
                                | EditorInfo.IME_FLAG_NO_EXTRACT_UI
                                | EditorInfo.IME_FLAG_NO_FULLSCREEN
                                | EditorInfo.IME_FLAG_NO_ENTER_ACTION);
                            et.setFocusable(true);
                            et.setFocusableInTouchMode(true);
                            decor.addView(et);
                            sHiddenInput = et;
                        }
                    } catch (Throwable t) {
                        Log.e(TAG, "hidden EditText setup failed", t);
                    }
                    Log.i(TAG, "ModView attached to DecorView");
                } catch (Throwable t) {
                    Log.e(TAG, "attach failed", t);
                }
            }
        });
    }
}
