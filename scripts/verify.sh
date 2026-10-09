#!/bin/bash
# التحقق المحلي قبل الـ push.
# ملاحظة: embedded_dex.h و libYAMGG.so لا يُفحَصان هنا
# لأنهما يُولَّدان في CI فقط. محلياً لا يوجد ndk-build ولا d8.

cd /storage/emulated/0/YAM-GG || exit 1

echo "=========================================="
echo "  التحقق المحلي — قبل الـ push"
echo "=========================================="
echo ""

ERRORS=0

ckf() {
    if [ -f "$1" ]; then
        echo "  ✓ $1"
    else
        echo "  ✗ $1"
        ERRORS=$((ERRORS+1))
    fi
}

ckd() {
    if [ -d "$1" ]; then
        echo "  ✓ $1/"
    else
        echo "  ✗ $1/"
        ERRORS=$((ERRORS+1))
    fi
}

echo "[1] Gradle:"
ckf "build.gradle"
ckf "settings.gradle"
ckf "gradle.properties"
ckf "app/build.gradle"
ckf "app/proguard-rules.pro"
echo ""

echo "[2] GitHub Actions:"
ckf ".github/workflows/build.yml"
echo ""

echo "[3] Java (ModView):"
ckf "app/src/main/java/com/yamgg/modview/ModView.java"
ckf "app/src/main/java/com/yamgg/modview/ModViewHelper.java"
echo ""

echo "[4] Native Core:"
ckf "app/src/main/jni/Main.cpp"
ckf "app/src/main/jni/Android.mk"
ckf "app/src/main/jni/Application.mk"
ckf "app/src/main/jni/Core/Runtime.h"
ckf "app/src/main/jni/Core/Runtime.cpp"
ckf "app/src/main/jni/Core/DexLoader.h"
ckf "app/src/main/jni/Core/DexLoader.cpp"
ckf "app/src/main/jni/Core/HookManager.h"
ckf "app/src/main/jni/Core/HookManager.cpp"
echo ""

echo "[5] UI:"
ckf "app/src/main/jni/UI/Renderer.h"
ckf "app/src/main/jni/UI/Renderer.cpp"
ckf "app/src/main/jni/UI/Theme.h"
ckf "app/src/main/jni/UI/Theme.cpp"
ckf "app/src/main/jni/UI/MainWindow.h"
ckf "app/src/main/jni/UI/MainWindow.cpp"
ckf "app/src/main/jni/UI/Tabs/JSConsole.h"
ckf "app/src/main/jni/UI/Tabs/JSConsole.cpp"
ckf "app/src/main/jni/UI/Tabs/FileBrowser.h"
ckf "app/src/main/jni/UI/Tabs/FileBrowser.cpp"
ckf "app/src/main/jni/UI/Widgets/Notification.h"
ckf "app/src/main/jni/UI/Widgets/Notification.cpp"
echo ""

echo "[6] Bridge + JNI:"
ckf "app/src/main/jni/Bridge/YamBridge.h"
ckf "app/src/main/jni/Bridge/YamBridge.cpp"
ckf "app/src/main/jni/JNI/NativeMethods.h"
ckf "app/src/main/jni/JNI/NativeMethods.cpp"
echo ""

echo "[7] Includes (محلياً):"
ckf "app/src/main/jni/Includes/Roboto-Regular.h"
ckf "app/src/main/jni/Includes/Utils.h"
ckf "app/src/main/jni/Includes/obfuscate.h"
echo ""

echo "[8] Generated (يجب أن يكون فارغاً محلياً — يُبنى في CI):"
ckd "app/src/main/jni/Generated"
if [ -f "app/src/main/jni/Generated/embedded_dex.h" ]; then
    echo "  ℹ️  embedded_dex.h موجود (من بناء سابق)"
    echo "     الحجم: $(du -h app/src/main/jni/Generated/embedded_dex.h | cut -f1)"
else
    echo "  ℹ️  embedded_dex.h غير موجود — طبيعي، سيُبنى في CI"
fi
echo ""

echo "[9] المكتبات:"
ckf "app/src/main/jni/all_libs/asmjit/Android.mk"
ckf "app/src/main/jni/all_libs/Dobby/Android.mk"
ckf "app/src/main/jni/all_libs/Dobby/prebuilt/arm64-v8a/libdobby.a"
ckf "app/src/main/jni/all_libs/Dobby/prebuilt/include/dobby.h"
ckf "app/src/main/jni/all_libs/KittyMemory/Android.mk"
ckf "app/src/main/jni/all_libs/KittyMemory/KittyMemory/Deps/Keystone/libs-android/arm64-v8a/libkeystone.a"
ckf "app/src/main/jni/all_libs/XDL/Android.mk"
ckf "app/src/main/jni/all_libs/XDL/xdl.c"
ckf "app/src/main/jni/all_libs/json/json.hpp"
ckf "app/src/main/jni/all_libs/YAM-G/LIBYAMJS/Android.mk"
ckf "app/src/main/jni/all_libs/YAM-G/LIBYAMJS/libyamjs.a"
ckf "app/src/main/jni/all_libs/YAM-G/wrapper/Android.mk"
ckf "app/src/main/jni/all_libs/YAM-G/wrapper/include/yam.hpp"
ckf "app/src/main/jni/all_libs/YAM-G/wrapper/src/yam_java.cpp"
ckf "app/src/main/jni/all_libs/imgui/imgui.cpp"
ckf "app/src/main/jni/all_libs/imgui/imgui.h"
ckf "app/src/main/jni/all_libs/imgui/imgui_internal.h"
ckf "app/src/main/jni/all_libs/imgui/backends/imgui_impl_opengl3.cpp"
ckf "app/src/main/jni/all_libs/imgui/backends/imgui_impl_opengl3.h"
ckf "app/src/main/jni/all_libs/imgui/backends/imgui_impl_opengl3_loader.h"
echo ""

echo "[10] Scripts CI:"
ckf "scripts/generate-dex.sh"
ckf "scripts/dex_to_header.py"
ckf "scripts/verify.sh"
echo ""

echo "[11] Manifest:"
ckf "app/src/main/AndroidManifest.xml"
echo ""

echo "=========================================="
if [ $ERRORS -eq 0 ]; then
    echo "  ✓ جاهز للـ push — $ERRORS أخطاء"
    echo ""
    echo "  المفقود المتوقع (يُبنى في CI):"
    echo "    - app/src/main/jni/Generated/embedded_dex.h"
    echo "    - libYAMGG.so"
    echo "    - app-release.apk"
else
    echo "  ✗ $ERRORS ملف مفقود — أصلحها قبل الـ push"
fi
echo "=========================================="

exit $ERRORS
