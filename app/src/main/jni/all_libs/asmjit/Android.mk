LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := asmjit

LOCAL_SRC_FILES := \
    core/archtraits.cpp \
    core/assembler.cpp \
    core/builder.cpp \
    core/codeholder.cpp \
    core/codewriter.cpp \
    core/compiler.cpp \
    core/constpool.cpp \
    core/cpuinfo.cpp \
    core/emithelper.cpp \
    core/emitter.cpp \
    core/emitterutils.cpp \
    core/environment.cpp \
    core/errorhandler.cpp \
    core/formatter.cpp \
    core/func.cpp \
    core/funcargscontext.cpp \
    core/globals.cpp \
    core/inst.cpp \
    core/instdb.cpp \
    core/jitallocator.cpp \
    core/jitruntime.cpp \
    core/logger.cpp \
    core/operand.cpp \
    core/osutils.cpp \
    core/ralocal.cpp \
    core/rapass.cpp \
    core/rastack.cpp \
    core/string.cpp \
    core/support.cpp \
    core/target.cpp \
    core/type.cpp \
    core/virtmem.cpp \
    core/zone.cpp \
    core/zonehash.cpp \
    core/zonelist.cpp \
    core/zonestack.cpp \
    core/zonetree.cpp \
    core/zonevector.cpp \
    arm/armformatter.cpp \
    arm/a64assembler.cpp \
    arm/a64builder.cpp \
    arm/a64compiler.cpp \
    arm/a64emithelper.cpp \
    arm/a64formatter.cpp \
    arm/a64func.cpp \
    arm/a64instapi.cpp \
    arm/a64instdb.cpp \
    arm/a64operand.cpp \
    arm/a64rapass.cpp

LOCAL_C_INCLUDES := $(LOCAL_PATH)

LOCAL_CFLAGS := -w -std=c++17 -fno-exceptions -fno-rtti -march=armv8-a
LOCAL_CPPFLAGS := -w -std=c++17 -fno-exceptions -fno-rtti -march=armv8-a

LOCAL_CFLAGS += -DASMJIT_STATIC
LOCAL_CFLAGS += -DASMJIT_NO_X86
LOCAL_CFLAGS += -DASMJIT_NO_FOREIGN
LOCAL_CFLAGS += -DASMJIT_NO_DEPRECATED
LOCAL_CFLAGS += -DASMJIT_NO_TEXT
LOCAL_CFLAGS += -DASMJIT_NO_LOGGING
LOCAL_CFLAGS += -DASMJIT_NO_INTROSPECTION
LOCAL_CFLAGS += -DASMJIT_NO_VALIDATION

include $(BUILD_STATIC_LIBRARY)
