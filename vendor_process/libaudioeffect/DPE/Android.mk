LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_ARM_MODE := arm

LOCAL_MODULE := libdpe

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libdl \
    libutils \
    libamaudioutils

LOCAL_HEADER_LIBRARIES := \
    libeigen \
    libaudioeffects

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH) \
    hardware/amlogic/audio/utils/ini/include \
    hardware/libhardware/include/hardware \
    hardware/libhardware/include \
    system/media/audio/include

LOCAL_SRC_FILES += EffectDynamicsProcessing.cpp
LOCAL_SRC_FILES += dsp/DPBase.cpp
LOCAL_SRC_FILES += dsp/DPFrequency.cpp

LOCAL_CFLAGS += -O2
LOCAL_CFLAGS += -DANDROID_PLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)
LOCAL_LDLIBS   +=  -llog
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
LOCAL_PROPRIETARY_MODULE := true
endif

LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0
LOCAL_LICENSE_CONDITIONS := notice

include $(BUILD_SHARED_LIBRARY)