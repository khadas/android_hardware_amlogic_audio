LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= libvirtualsurround

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libdl \
    libutils \
    libaudioutils \
    libamaudioutils

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \
    hardware/amlogic/audio/utils/ini/include \
    hardware/libhardware/include/hardware \
    hardware/libhardware/include \
    system/media/audio/include \
    system/media/audio_utils/include \

LOCAL_SRC_FILES += Virtualsurround.cpp

LOCAL_LDFLAGS_arm  += $(LOCAL_PATH)/libmusicbundle.a
LOCAL_LDFLAGS_arm64  += $(LOCAL_PATH)/libmusicbundle64.a
LOCAL_MULTILIB := both
LOCAL_PRELINK_MODULE := false

LOCAL_LDLIBS   +=  -llog
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
LOCAL_PROPRIETARY_MODULE := true
endif

LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0 legacy_proprietary
LOCAL_LICENSE_CONDITIONS := notice proprietary by_exception_only
include $(BUILD_SHARED_LIBRARY)
