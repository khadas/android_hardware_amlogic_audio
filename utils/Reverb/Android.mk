LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libamlreverb
LOCAL_ARM_MODE := arm

LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES:= \
    EffectReverb.c

LOCAL_CFLAGS += -Wall -Werror

LOCAL_LDFLAGS_arm += $(LOCAL_PATH)/libreverb.a

LOCAL_SHARED_LIBRARIES := \
     libaudioutils \
     libcutils \
     libdl \
     liblog

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/include \

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
    LOCAL_PROPRIETARY_MODULE := true
endif

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
