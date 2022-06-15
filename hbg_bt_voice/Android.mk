LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(BOARD_ENABLE_HBG), true)

LOCAL_SRC_FILES += \
        hbg_blehid_mic.c \
        ringBuffer.c

LOCAL_C_INCLUDES +=                      \
    hardware/libhardware/include \
    $(TOPDIR)system/core/include        \
    $(TOPDIR)system/media/audio/include \
    $(LOCAL_PATH)/../utils/include

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libhbgdecode \
    libamaudioutils

LOCAL_MODULE := libhbg

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
    LOCAL_PROPRIETARY_MODULE := true
endif

LOCAL_CFLAGS := -Werror -Wall
LOCAL_MODULE_TAGS := optional
LOCAL_LICENSE_KINDS := legacy_by_exception_only legacy_proprietary
LOCAL_LICENSE_CONDITIONS := by_exception_only proprietary
LOCAL_NOTICE_FILE := $(LOCAL_PATH)/../LICENSE
include $(BUILD_SHARED_LIBRARY)

endif
