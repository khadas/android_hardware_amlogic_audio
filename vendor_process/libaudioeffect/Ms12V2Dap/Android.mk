LOCAL_PATH := $(call my-dir)
# MS12 DAP audio effect library
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_ARM_MODE := arm

LOCAL_MODULE := libms12v2dapwrapper

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libdl \
    libutils \
    libamaudioutils \
    android.hardware.audio@4.0 \
    android.hardware.audio.common@4.0 \
    android.hardware.audio.common@4.0-util \


LOCAL_C_INCLUDES := \
    $(LOCAL_PATH) \
    hardware/amlogic/audio/utils/ini/include \
    hardware/amlogic/audio/utils/include \
    hardware/libhardware/include \
    system/media/audio/include \
    vendor/amlogic/frameworks/av/libaudioeffect/Utility \
    system/core/libutils/include \

LOCAL_SRC_FILES := ms12v2_dap_wrapper.cpp

LOCAL_CFLAGS += -O2

LOCAL_LDLIBS   +=  -llog
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
LOCAL_PROPRIETARY_MODULE := true
endif

LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0 legacy_proprietary
LOCAL_LICENSE_CONDITIONS := notice proprietary by_exception_only
include $(BUILD_SHARED_LIBRARY)
