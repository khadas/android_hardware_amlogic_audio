# Copyright 2005 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../../include \

LOCAL_SRC_FILES:= \
    load.c

LOCAL_MODULE:= loadconfig

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
    LOCAL_PROPRIETARY_MODULE := true
endif

LOCAL_SHARED_LIBRARIES := \
        libcutils \
        libc \
        libamaudioutils

LOCAL_CFLAGS += -Wno-error

include $(BUILD_EXECUTABLE)
