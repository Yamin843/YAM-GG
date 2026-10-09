LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := yamcpp

LOCAL_SRC_FILES := \
    src/yam_json.cpp \
    src/yam_core.cpp \
    src/yam_memory.cpp \
    src/yam_stalker.cpp \
    src/yam_hooks.cpp \
    src/yam_subsystems.cpp \
    src/yam_cmodule_registry.cpp \
    src/yam_java.cpp \
    src/yam_java_model.cpp \
    src/yam_events.cpp \
    src/yam_console.cpp

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \
    $(LOCAL_PATH)/../LIBYAMJS/include

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include

LOCAL_CPPFLAGS := \
    -std=c++17 -fexceptions -frtti -fPIC \
    -w -O2 -DNDEBUG

LOCAL_CPP_FEATURES := exceptions rtti

LOCAL_STATIC_LIBRARIES := yamgjs
LOCAL_LDLIBS := -llog -ldl -lm -lz -lc++_shared -lunwind

include $(BUILD_STATIC_LIBRARY)
