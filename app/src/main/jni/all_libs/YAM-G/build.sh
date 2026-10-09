#!/system/bin/sh
set -e
ROOT=/storage/emulated/0/YAM-G
WRAP=$ROOT/wrapper
BUILD=$WRAP/build

mkdir -p "$BUILD"
cd "$BUILD"
cmake -G "Ninja" \
    -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DANDROID_STL=c++_shared \
    -DYAM_BUILD_EXAMPLES=ON \
    "$WRAP"
ninja
