APP_ABI := arm64-v8a
APP_PLATFORM := android-24
APP_STL := c++_shared
APP_OPTIM := release
APP_THIN_ARCHIVE := true
APP_PIE := true
APP_ALLOW_MISSING_DEPS := true
APP_SUPPORT_FLEXIBLE_PAGE_SIZES := true
APP_CPPFLAGS := -std=c++17 -fexceptions -frtti -O2 -DNDEBUG -w
APP_CFLAGS := -O2 -DNDEBUG -w
NDK_TOOLCHAIN_VERSION := clang
