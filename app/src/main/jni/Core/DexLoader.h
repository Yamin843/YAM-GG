#ifndef YAMGG_CORE_DEXLOADER_H
#define YAMGG_CORE_DEXLOADER_H

#include <jni.h>
#include <string>
#include <mutex>

namespace yamgg {

class DexLoader {
public:
    static DexLoader& instance();

    bool loadEmbeddedDex(JNIEnv* env);
    bool isLoaded() const { return loaded_; }
    jclass findClass(JNIEnv* env, const char* name);
    jobject classLoader(JNIEnv* env);
    void detach(JNIEnv* env);

private:
    DexLoader() = default;
    ~DexLoader() = default;
    DexLoader(const DexLoader&) = delete;
    DexLoader& operator=(const DexLoader&) = delete;

    bool loaded_{false};
    jclass classLoaderClass_{nullptr};
    jobject classLoaderObj_{nullptr};
    mutable std::mutex mu_;
};

#define YAMGG_DEXLOADER yamgg::DexLoader::instance()

} // namespace yamgg

#endif
