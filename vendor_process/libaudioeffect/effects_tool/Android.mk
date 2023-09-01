LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_MODULE_TAGS := optional

LOCAL_MODULE    := AudioEffectTool

LOCAL_SYSTEM_EXT_MODULE := true
LOCAL_SRC_FILES := main.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libaudioclient \
    libmedia \
    libmedia_helper \
    libmediaplayerservice \

# libaudiofoundation support only R and above
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 30 && echo OK), OK)
LOCAL_SHARED_LIBRARIES += \
    libaudiofoundation
endif

# framework-permission-aidl-cpp support only S and above
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 31 && echo OK), OK)
LOCAL_CFLAGS += -DUSE_IDENTITY_CREATE_AUDIOEFFECT
LOCAL_SHARED_LIBRARIES += \
    framework-permission-aidl-cpp
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0
LOCAL_LICENSE_CONDITIONS := notice
endif

LOCAL_CFLAGS +=-Wno-gnu-variable-sized-type-not-at-end
#-Wgnu-variable-sized-type-not-at-end
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../VirtualX

include $(BUILD_EXECUTABLE)
