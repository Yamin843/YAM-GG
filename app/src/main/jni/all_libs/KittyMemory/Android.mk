LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := keystone
LOCAL_SRC_FILES := KittyMemory/Deps/Keystone/libs-android/arm64-v8a/libkeystone.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/KittyMemory/Deps/Keystone/includes
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := kitty_memory

LOCAL_CPPFLAGS := -w -std=c++17 -fexceptions -frtti -O2 -DNDEBUG
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/KittyMemory \
    $(LOCAL_PATH)/KittyMemory/Deps/Keystone/includes

LOCAL_SRC_FILES := \
    KittyMemory/KittyMemory.cpp \
    KittyMemory/MemoryPatch.cpp \
    KittyMemory/MemoryBackup.cpp \
    KittyMemory/KittyUtils.cpp \
    KittyMemory/KittyScanner.cpp \
    KittyMemory/KittyPtrValidator.cpp \
    KittyMemory/KittyIOFile.cpp \
    KittyMemory/KittyAsm.cpp

LOCAL_STATIC_LIBRARIES := keystone

include $(BUILD_STATIC_LIBRARY)
