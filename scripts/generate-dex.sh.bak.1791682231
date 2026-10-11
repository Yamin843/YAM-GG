#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
JAVA_SRC_DIR="$PROJECT_DIR/app/src/main/java"
OUT_DIR="$PROJECT_DIR/app/src/main/jni/Generated"
BUILD_DIR="$PROJECT_DIR/build/dex-build"

mkdir -p "$OUT_DIR"
mkdir -p "$BUILD_DIR/classes"

if [ -z "$ANDROID_HOME" ]; then
    if [ -n "$ANDROID_SDK_ROOT" ]; then
        ANDROID_HOME="$ANDROID_SDK_ROOT"
    else
        echo "ERROR: ANDROID_HOME not set"
        exit 1
    fi
fi

BUILD_TOOLS=$(ls -1 "$ANDROID_HOME/build-tools/" | sort -V | tail -n1)
PLATFORM=$(ls -1 "$ANDROID_HOME/platforms/" | sort -V | tail -n1)
ANDROID_JAR="$ANDROID_HOME/platforms/$PLATFORM/android.jar"
D8="$ANDROID_HOME/build-tools/$BUILD_TOOLS/d8"

echo "Build tools: $BUILD_TOOLS"
echo "Platform: $PLATFORM"
echo "Android JAR: $ANDROID_JAR"
echo "D8: $D8"

if [ ! -f "$ANDROID_JAR" ]; then
    echo "ERROR: android.jar not found: $ANDROID_JAR"
    exit 1
fi

if [ ! -x "$D8" ]; then
    D8="$ANDROID_HOME/build-tools/$BUILD_TOOLS/d8.bat"
fi

echo "Compiling Java sources..."
rm -rf "$BUILD_DIR/classes"
mkdir -p "$BUILD_DIR/classes"

find "$JAVA_SRC_DIR/com/yamgg/modview" -name "*.java" > "$BUILD_DIR/sources.txt"

javac -source 8 -target 8 \
    -bootclasspath "$ANDROID_JAR" \
    -classpath "$ANDROID_JAR" \
    -d "$BUILD_DIR/classes" \
    @"$BUILD_DIR/sources.txt" 2>&1 | head -100

echo "Compiled classes:"
find "$BUILD_DIR/classes" -name "*.class"

echo "Converting to DEX..."
mkdir -p "$BUILD_DIR/dex"
rm -rf "$BUILD_DIR/dex/"*

CLASS_FILES=$(find "$BUILD_DIR/classes" -name "*.class")

"$D8" \
    --output "$BUILD_DIR/dex/" \
    --lib "$ANDROID_JAR" \
    --min-api 24 \
    --no-desugaring \
    $CLASS_FILES

if [ ! -f "$BUILD_DIR/dex/classes.dex" ]; then
    echo "ERROR: classes.dex not produced"
    exit 1
fi

echo "DEX size: $(stat -c%s "$BUILD_DIR/dex/classes.dex") bytes"

echo "Generating C header..."
python3 "$SCRIPT_DIR/dex_to_header.py" \
    "$BUILD_DIR/dex/classes.dex" \
    "$OUT_DIR/embedded_dex.h"

echo "Generated header:"
wc -l "$OUT_DIR/embedded_dex.h"
head -10 "$OUT_DIR/embedded_dex.h"

echo "DEX generation complete"
