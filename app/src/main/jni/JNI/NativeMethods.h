#ifndef YAMGG_JNI_NATIVEMETHODS_H
#define YAMGG_JNI_NATIVEMETHODS_H

#include <jni.h>

namespace yamgg {
namespace jni {

bool registerNativeMethods(JNIEnv* env, jclass modViewClass);
void onSurfaceCreated(JNIEnv* env, jclass clazz);
void onSurfaceChanged(JNIEnv* env, jclass clazz, jint width, jint height);
void onDrawFrame(JNIEnv* env, jclass clazz, jint width, jint height);
void onTouch(JNIEnv* env, jclass clazz, jint action, jfloat x, jfloat y, jint pointerId);
void onKey(JNIEnv* env, jclass clazz, jint keyCode, jint action);
jboolean wantCaptureMouse(JNIEnv* env, jclass clazz);
jboolean hitTest(JNIEnv* env, jclass clazz, jfloat x, jfloat y);
void onChar(JNIEnv* env, jclass clazz, jint codepoint);
jboolean wantTextInput(JNIEnv* env, jclass clazz);
void onScroll(JNIEnv* env, jclass clazz, jfloat dx, jfloat dy);

} // namespace jni
} // namespace yamgg

#endif
