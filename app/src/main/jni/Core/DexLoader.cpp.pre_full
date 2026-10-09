#include "DexLoader.h"
#include "Runtime.h"
#include "../Generated/embedded_dex.h"

#include <android/log.h>
#include <cstring>
#include <vector>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {

DexLoader& DexLoader::instance() {
    static DexLoader inst;
    return inst;
}

bool DexLoader::loadEmbeddedDex(JNIEnv* env) {
    if (loaded_) return true;
    if (!env) return false;

    LOGI("DexLoader: entering InMemoryDexClassLoader path");

    jclass imdclClass = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    if (!imdclClass) {
        env->ExceptionClear();
        LOGE("InMemoryDexClassLoader not found (API < 26)");
        return false;
    }

    jclass byteBufferClass = env->FindClass("java/nio/ByteBuffer");
    if (!byteBufferClass) {
        env->ExceptionClear();
        LOGE("ByteBuffer not found");
        return false;
    }

    jmethodID wrapMethod = env->GetStaticMethodID(
            byteBufferClass, "wrap", "([B)Ljava/nio/ByteBuffer;");
    if (!wrapMethod) {
        env->ExceptionClear();
        LOGE("ByteBuffer.wrap not found");
        return false;
    }

    jbyteArray byteArray = env->NewByteArray(kDexSize);
    if (!byteArray) {
        env->ExceptionClear();
        LOGE("NewByteArray failed");
        return false;
    }

    env->SetByteArrayRegion(byteArray, 0, kDexSize,
                            reinterpret_cast<const jbyte*>(kDexBytes));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("SetByteArrayRegion failed");
        env->DeleteLocalRef(byteArray);
        return false;
    }

    jobject byteBuffer = env->CallStaticObjectMethod(byteBufferClass, wrapMethod, byteArray);
    env->DeleteLocalRef(byteArray);
    if (!byteBuffer) {
        env->ExceptionClear();
        LOGE("ByteBuffer.wrap returned null");
        return false;
    }

    jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
    if (!classLoaderClass) {
        env->ExceptionClear();
        LOGE("ClassLoader not found");
        env->DeleteLocalRef(byteBuffer);
        return false;
    }

    jmethodID getSystemCL = env->GetStaticMethodID(
            classLoaderClass, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
    jobject parentLoader = nullptr;
    if (getSystemCL) {
        parentLoader = env->CallStaticObjectMethod(classLoaderClass, getSystemCL);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        parentLoader = nullptr;
    }

    jmethodID ctor = env->GetMethodID(
            imdclClass,
            "<init>",
            "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    if (!ctor) {
        env->ExceptionClear();
        LOGE("InMemoryDexClassLoader ctor not found");
        env->DeleteLocalRef(byteBuffer);
        return false;
    }

    jobject loader = env->NewObject(imdclClass, ctor, byteBuffer, parentLoader);
    env->DeleteLocalRef(byteBuffer);
    if (env->ExceptionCheck() || !loader) {
        env->ExceptionClear();
        LOGE("InMemoryDexClassLoader construction failed");
        return false;
    }

    classLoaderObj_ = env->NewGlobalRef(loader);
    classLoaderClass_ = reinterpret_cast<jclass>(env->NewGlobalRef(classLoaderClass));
    env->DeleteLocalRef(loader);

    if (!classLoaderObj_) {
        LOGE("NewGlobalRef for class loader failed");
        return false;
    }

    LOGI("DexLoader: InMemoryDexClassLoader created successfully");
    loaded_ = true;
    return true;
}

jclass DexLoader::findClass(JNIEnv* env, const char* name) {
    if (!env || !name) return nullptr;

    if (classLoaderObj_ && classLoaderClass_) {
        jmethodID loadClass = env->GetMethodID(
                classLoaderClass_, "loadClass",
                "(Ljava/lang/String;)Ljava/lang/Class;");
        if (loadClass) {
            jstring jname = env->NewStringUTF(name);
            jclass cls = reinterpret_cast<jclass>(
                    env->CallObjectMethod(classLoaderObj_, loadClass, jname));
            env->DeleteLocalRef(jname);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                return nullptr;
            }
            return cls;
        }
    }

    return env->FindClass(name);
}

jobject DexLoader::classLoader(JNIEnv* env) {
    if (!env) return nullptr;
    return classLoaderObj_;
}

} // namespace yamgg
