# Copyright 2005 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
    hardware/$(PLATFORM_NAME)/audio/utils/include

LOCAL_SRC_FILES:= \
    test.c

LOCAL_MODULE:= testamlconf

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
    LOCAL_PROPRIETARY_MODULE := true
endif

LOCAL_SHARED_LIBRARIES := \
        libcutils \
        libc \
        libamaudioutils

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
    hardware/$(PLATFORM_NAME)/audio/utils/include

LOCAL_CFLAGS := -DBUILDHOSTEXE

LOCAL_SRC_FILES:= \
    test_data_utils.c \
    ../aml_data_utils.c

LOCAL_MODULE:= testdatautils

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
    LOCAL_PROPRIETARY_MODULE := true
endif

LOCAL_SHARED_LIBRARIES := \
	libamaudioutils

include $(BUILD_HOST_EXECUTABLE)
