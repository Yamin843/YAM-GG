# ---------------------------------------------------------------------------
# YAM-GG / Application.mk
# ---------------------------------------------------------------------------
# CRITICAL: c++_static is MANDATORY here.
#   - Our library is injected into foreign processes (Unity/Unreal games).
#   - Those processes do NOT ship libc++_shared.so.
#   - If libYAMGG.so has a NEEDED entry for libc++_shared.so, dlopen fails
#     at injection time and nothing loads.
#   - With c++_static, all libc++ code is embedded in libYAMGG.so.
#   - Combined with -Wl,--exclude-libs,ALL in Android.mk, our static libc++
#     symbols are hidden from the dynamic symbol table, so they cannot
#     collide with a libc++_shared the target app may already have loaded.
# ---------------------------------------------------------------------------

APP_ABI := arm64-v8a
APP_PLATFORM := android-24
APP_STL := c++_static
APP_OPTIM := release
APP_THIN_ARCHIVE := true
APP_PIE := true
APP_ALLOW_MISSING_DEPS := true
APP_CPPFLAGS := -std=c++17 -fexceptions -frtti -O2 -DNDEBUG -w
APP_CFLAGS := -O2 -DNDEBUG -w
NDK_TOOLCHAIN_VERSION := clang
