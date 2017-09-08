LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libTVaudio
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
	LOCAL_PROPRIETARY_MODULE := true
endif
LOCAL_SHARED_LIBRARIES := libcutils libutils libtinyalsa libdl \
    libmedia libbinder libstagefright libstagefright_foundation  libaaudio  liblog libaudioclient \
    libmedia_helper

ifneq (0, $(shell expr $(PLATFORM_VERSION) \>= 5.0))
LOCAL_SHARED_LIBRARIES += libsystemcontrolservice
else
LOCAL_SHARED_LIBRARIES += libsystemwriteservice
endif
LOCAL_C_INCLUDES := \
    $(TOP)/frameworks/native/services \
    $(TOP)/frameworks/native/include \
    $(TOP)/vendor/amlogic/frameworks/services \
    external/tinyalsa/include \
    frameworks/av/include/media/stagefright \
    frameworks/av/include/media \
    frameworks/native/include/media/openmax \
    frameworks/av/media/libeffects/lvm/lib/StereoWidening/lib \
    frameworks/av/media/libeffects/lvm/lib/StereoWidening/src \
    frameworks/av/media/libeffects/lvm/lib/Common/lib \
    frameworks/av/media/libeffects/lvm/lib/Common/src \
    $(LOCAL_PATH)/ \
    $(LOCAL_PATH)/audio \

LOCAL_SRC_FILES := \
    audio/aml_audio.c \
    audio/audio_effect_control.c \
    audio/android_out.cpp \
    audio/audio_amaudio.cpp \
    audio/audio_usb_check.cpp \
    audio/amaudio_main.cpp \
    audio/aml_shelf.c \
    ../audio_virtual_effect.c
    #audio/DDP_media_source.cpp \
    #audio/DTSHD_media_source.cpp 
LOCAL_STATIC_LIBRARIES += libmusicbundle

LOCAL_CFLAGS := -DANDROID_PLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)  -DUSE_SYS_WRITE_SERVICE=1

LOCAL_MODULE_TAGS := optional
LOCAL_PRELINK_MODULE := false

include $(BUILD_SHARED_LIBRARY)
