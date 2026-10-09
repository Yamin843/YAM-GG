package com.yamgg.modview;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.os.Bundle;
import android.util.Log;

public class ModViewHelper {

    private static final String TAG = "YAMGG";
    private static boolean sInstalled = false;

    public static void install(final ClassLoader loader) {
        if (sInstalled) return;
        sInstalled = true;
        Log.i(TAG, "install() called");
    }

    public static void onActivityResumed(Activity activity) {
        try {
            Log.i(TAG, "onActivityResumed: " + activity);
            ModView.attach(activity);
        } catch (Throwable t) {
            Log.e(TAG, "onActivityResumed", t);
        }
    }

    public static void onActivityPaused(Activity activity) {
        Log.i(TAG, "onActivityPaused: " + activity);
    }

    public static void onActivityDestroyed(Activity activity) {
        Log.i(TAG, "onActivityDestroyed: " + activity);
        try {
            if (ModView.getActivity() == activity) {
                ModView.detach();
            }
        } catch (Throwable t) {
            Log.e(TAG, "onActivityDestroyed", t);
        }
    }
}
