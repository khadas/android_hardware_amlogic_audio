LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

#LOCAL_CFLAGS += -DAML_CONFIG_SUPPORT_READ_ONLY

LOCAL_C_INCLUDES +=                           \
    hardware/libhardware/include              \
    $(TOPDIR)system/core/include              \
    $(TOPDIR)external/tinyalsa/include        \
    $(TOPDIR)system/media/audio_utils/include \
    $(TOPDIR)frameworks/av/include            \
    $(TOPDIR)system/media/audio/include       \
    $(TOPDIR)frameworks/native/libs/binder/include   \
    $(TOPDIR)frameworks/av/media/libaudiohal/include \
    $(LOCAL_PATH)/include                     \
    $(LOCAL_PATH)/ini/include

LOCAL_SRC_FILES  +=               \
    aml_buffer_provider.c         \
    aml_dump_debug.c              \
    aml_ringbuffer.c              \
    aml_alsa_mixer.c              \
    aml_android_hidl_utils.cpp    \
    aml_android_utils.c           \
    aml_data_utils.c              \
    aml_configs/aml_conf_loader.c \
    aml_configs/aml_conf_parser.c \
    aml_audio_mixer.c             \
    SPDIFEncoderAD.cpp            \
    spdifenc_wrap.cpp             \
    aml_volume_utils.c            \
    ini/ini.cpp                   \
    ini/IniParser.cpp             \
    alsa_device_parser.c          \
    aml_hw_mixer.c                \
    audio_data_process.c          \
    aml_malloc_debug.c

LOCAL_MODULE := libamaudioutils

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
    LOCAL_PROPRIETARY_MODULE := true
endif

LOCAL_SHARED_LIBRARIES += \
    libc                  \
    libcutils             \
    libutils              \
    liblog                \
    libtinyalsa           \
    libaudioutils         \
    libdroidaudiospdif    \
    libamlaudiohal

LOCAL_STATIC_LIBRARIES += \
    android.hardware.audio@4.0 \
    android.hardware.audio.common@4.0

LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS += -DBUILD_IN_ANDROID -Werror -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-sign-compare

include $(BUILD_SHARED_LIBRARY)
