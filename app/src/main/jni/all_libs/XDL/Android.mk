LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := xdl

LOCAL_SRC_FILES := \
    xdl.c \
    xdl_iterate.c \
    xdl_linker.c \
    xdl_lzma.c \
    xdl_util.c

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH) \
    $(LOCAL_PATH)/include

LOCAL_EXPORT_C_INCLUDES := \
    $(LOCAL_PATH) \
    $(LOCAL_PATH)/include

LOCAL_CFLAGS := -w -O2 -std=c17
LOCAL_LDLIBS := -llog

include $(BUILD_STATIC_LIBRARY)
