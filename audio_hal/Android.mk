# Copyright (C) 2011 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ifeq ($(strip $(BOARD_ALSA_AUDIO)),tiny)

    LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libnano
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0 SPDX-license-identifier-BSD
LOCAL_LICENSE_CONDITIONS := notice
LOCAL_SRC_FILES_arm := ../bt_voice/nano/32/libnano.so
LOCAL_SRC_FILES_arm64 := ../bt_voice/nano/64/libnano.so
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := nanosic
LOCAL_MODULE_SUFFIX := .so
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_TARGET_ARCH:= arm arm64
LOCAL_MULTILIB := both
LOCAL_SHARED_LIBRARIES := libcutils liblog libutils

include $(BUILD_PREBUILT)

# The default audio HAL module, which is a stub, that is loaded if no other
# device specific modules are present. The exact load order can be seen in
# libhardware/hardware.c
#
# The format of the name is audio.<type>.<hardware/etc>.so where the only
# required type is 'primary'. Other possibilities are 'a2dp', 'usb', etc.
	include $(CLEAR_VARS)

    LOCAL_MODULE := audio.primary.amlogic
    LOCAL_MULTILIB := both
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
        LOCAL_PROPRIETARY_MODULE := true
    endif
    LOCAL_MODULE_RELATIVE_PATH := hw
    LOCAL_SRC_FILES := \
        audio_hw.c \
        audio_hw_resource_mgr.c \
        ../input/tv_patch.c \
        ../input/tv_patch_avsync.c \
        ../input/tv_patch_ctrl.c \
        ../input/tv_patch_format_parser.c \
        ../input/hdmirx_utils.c  \
        ../input/component_picture_mode.c \
        ../input/component_noise_gate.c \
        ../input/device_patch.c \
        ../input/tv_private_object.c \
        ../input/dtv_private_object.c \
        ../input/device_patch_mgr.c \
        aml_hfp.c \
        audio_hw_utils.c \
        audio_hwsync.c \
        audio_hw_profile.c \
        alsa_manager.c \
        a2dp_hw.cpp \
        a2dp_hal.cpp \
        audio_bt_sco.c \
        aml_audio_stream.c \
        alsa_config_parameters.c \
        spdif_encoder_api.c \
        ../vendor_process/audio_post_process.c \
        ../vendor_process/aml_ai_audio.c \
        dolby_lib_api.c \
        amlAudioMixer.c \
        hw_avsync.c \
        hw_avsync_callbacks.c \
        audio_port.c \
        sub_mixing_factory.c \
        aml_audio_timer.c \
        audio_virtual_buf.c \
        aml_audio_ease.c \
        aml_mmap_audio.c \
        aml_audio_ms12_bypass.c \
        aml_audio_delay.c \
        aml_audio_spdifout.c \
        aml_audio_ms12_sync.c \
        ../aml_aq_hw/audio_eq_drc_compensation.c \
        ../aml_aq_hw/audio_eq_drc_parser.c \
        ../aml_aq_hw/ini/dictionary.c \
        ../aml_aq_hw/ini/iniparser.c \
        aml_audio_dev2mix_process.c \
        earc_utils.c \
        aml_vad_wakeup.cpp \
        aml_audio_ms12_render.c \
        aml_audio_nonms12_render.c \
        karaoke_manager.c \
        audio_kara.c \
        audio_usb_hal.c \
        aml_config_parser.c \
        aml_config_data.c \
        aml_audio_scaletempo.c \
        aml_audio_output.c \
        aml_async_write.c

    LOCAL_C_INCLUDES += \
        hardware/amlogic/audio/aml_speed/include \
        system/media/audio_utils/include \
        system/media/audio_effects/include \
        system/media/audio_route/include \
        system/memory/libion/kernel-headers/linux \
        system/core/libion/include \
        system/core/include \
        system/libfmq/include \
        system/media/alsa_utils/include \
        hardware/libhardware/include \
        $(LOCAL_PATH)/../utils \
        $(LOCAL_PATH)/../utils/include \
        $(LOCAL_PATH)/../utils/ini/include \
        $(LOCAL_PATH)/../rcaudio \
        $(LOCAL_PATH)/../utils/tinyalsa/include \
        $(LOCAL_PATH)/../aml_aq_hw \
        $(LOCAL_PATH)/../aml_aq_hw/ini \
        $(LOCAL_PATH)/../utils/cJSON \
        hardware/amlogic/audio/aml_adecs/include \
        hardware/amlogic/audio/aml_resampler/include \
        hardware/amlogic/audio/aml_parser/include \
        hardware/amlogic/audio/aml_speed/include \
        hardware/amlogic/audio/vendor_process/libaudioeffect/VirtualX \
        hardware/amlogic/audio/vendor_process/libaudioeffect/aiaq/include \
        hardware/amlogic/audio/vendor_process/include \
        hardware/amlogic/audio/input/include \
        hardware/amlogic/audio/decoder/include \


    LOCAL_LDFLAGS_arm += $(LOCAL_PATH)/../aml_aq_hw/lib_aml_ng.a
    LOCAL_LDFLAGS_arm += $(LOCAL_PATH)/../aml_aq_hw/Amlogic_EQ_Param_Generator.a
    LOCAL_LDFLAGS_arm += $(LOCAL_PATH)/../aml_aq_hw/Amlogic_DRC_Param_Generator.a
    LOCAL_LDFLAGS_arm64 += $(LOCAL_PATH)/../aml_aq_hw/Amlogic_EQ_Param_Generator64.a
    LOCAL_LDFLAGS_arm64 += $(LOCAL_PATH)/../aml_aq_hw/Amlogic_DRC_Param_Generator64.a
    LOCAL_LDFLAGS_arm64 += $(LOCAL_PATH)/../aml_aq_hw/lib_aml_ng64.a

    LOCAL_SHARED_LIBRARIES := \
        liblog libcutils libamltinyalsa \
        libaudioutils libdl libaudioroute libutils \
        libdroidaudiospdif libamaudioutils libamlaudiorc \
        libnano \
        libion \
        libamladecs \
        libamlresampler \
        libamlparser \
        libamlspeed \
        libalsautils

    LOCAL_SHARED_LIBRARIES += \
        android.hardware.bluetooth.audio@2.0 \
        android.hardware.bluetooth.audio@2.0-impl \
        libbluetooth_audio_session \
        libbase \
        libfmq

    LOCAL_SRC_FILES += \
        audio_tsync_wrap.c \
        audio_hwsync_wrap.c \
        audio_mediasync_wrap.c \


    LOCAL_C_INCLUDES += \
        vendor/amlogic/common/mediahal_sdk/include \

#For audio content recognize
LOCAL_CFLAGS += -DENABLE_AML_ACR

ifneq ($(BOARD_DISABLE_DVB_AUDIO), true)
        LOCAL_CFLAGS += -DENABLE_DVB_PATCH
        LOCAL_SRC_FILES += ../input/dtv_patch.c \
                           ../input/dtv_patch_utils.c \
                           ../input/dtv_patch_dtvsync.c \
                           ../input/dtv_patch_hal_avsync.c \

        LOCAL_C_INCLUDES += \
                $(LOCAL_PATH)/../../LibAudio/amadec/include \
                vendor/amlogic/common/prebuilt/dvb/include/am_adp \
                hardware/amlogic/audio/dtv_audio_utils/sync \
                hardware/amlogic/audio/dtv_audio_utils/audio_read_api \

        LOCAL_SHARED_LIBRARIES += \
                libamadec \
                libam_adp \
                libdvbaudioutils
endif

LOCAL_CFLAGS += -DANDROID_PLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)
ifdef PLATFORM_SDK_EXTENSION_VERSION
    LOCAL_CFLAGS += -DANDROID_PLATFORM_SDK_EXTENSION_VERSION=$(PLATFORM_SDK_EXTENSION_VERSION)
else
    LOCAL_CFLAGS += -DANDROID_PLATFORM_SDK_EXTENSION_VERSION=1
endif

#/*[SEI-2018-12-18] add for HBG remote audio support { */
ifeq ($(BOARD_ENABLE_HBG), true)
    LOCAL_SHARED_LIBRARIES += libhbg
endif
#/*[SEI-2018-12-18] add for HBG remote audio support } */

    LOCAL_MODULE_TAGS := optional
    LOCAL_CFLAGS += -Werror
ifneq ($(TARGET_BUILD_VARIANT),user)
    LOCAL_CFLAGS += -DDEBUG_VOLUME_CONTROL
endif

ifeq ($(BOARD_ENABLE_HBG), true)
LOCAL_CFLAGS += -DENABLE_HBG_PATCH
endif

ifeq ($(strip $(TARGET_WITH_TV_AUDIO_MODE)),true)
$(info "---------tv audio mode, compiler configured 8 channels output by default--------")
LOCAL_CFLAGS += -DTV_AUDIO_OUTPUT
else
$(info "---------ott audio mode, compiler configure 2 channels output by default--------")
#LOCAL_CFLAGS += -DUSB_KARAOKE
endif
    #LOCAL_CFLAGS += -Wall -Wunknown-pragmas

#add dolby ms12support
    LOCAL_CFLAGS += -DDOLBY_MS12_ENABLE
    LOCAL_CFLAGS += -DREPLACE_OUTPUT_BUFFER_WITH_CALLBACK

#by default, we compile V2,V1 is not used now. TBD
ifneq ($(TARGET_BUILD_DOLBY_MS12_V1), true)
    LOCAL_SRC_FILES += audio_hw_ms12_common.c
    LOCAL_SRC_FILES += audio_hw_ms12_v2.c
    LOCAL_CFLAGS += -DMS12_V24_ENABLE
    LOCAL_C_INCLUDES += hardware/amlogic/audio/decoder/libms12_v24/include
    LOCAL_SHARED_LIBRARIES += libms12api_v24
else
    LOCAL_SRC_FILES += audio_hw_ms12_common.c
    LOCAL_SRC_FILES += audio_hw_ms12.c
    LOCAL_C_INCLUDES += hardware/amlogic/audio/decoder/libms12_v1/include
    LOCAL_SHARED_LIBRARIES += libms12api
endif

#For atom project
ifeq ($(strip $(TARGET_BOOTLOADER_BOARD_NAME)), atom)
    LOCAL_CFLAGS += -DIS_ATOM_PROJECT
    LOCAL_SRC_FILES += \
        ../vendor_process/audio_aec_process.cpp
    LOCAL_C_INCLUDES += \
        $(TOPDIR)vendor/harman/atom/google_aec \
        $(TOPDIR)vendor/harman/atom/harman_api
    LOCAL_SHARED_LIBRARIES += \
        libgoogle_aec libharman_api
endif

#For ATV Far Field AEC
#ifeq ($(BOARD_ENABLE_FAR_FIELD_AEC), true)
    LOCAL_CFLAGS += -DENABLE_AEC_APP
    LOCAL_SRC_FILES += \
        ../vendor_process/audio_aec.c \
        ../vendor_process/fifo_wrapper.cpp
#endif

# AML_ASYNC_WRITE_COMPRESS_ENABLE = true
ifeq ($(AML_ASYNC_WRITE_COMPRESS_ENABLE), true)
    LOCAL_CFLAGS += -DAML_ASYNC_WRITE_COMPRESS_ENABLE
    LOCAL_C_INCLUDES += external/zlib
    LOCAL_SHARED_LIBRARIES += libz
endif

    LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0 SPDX-license-identifier-BSD
    LOCAL_LICENSE_CONDITIONS := notice
    include $(BUILD_SHARED_LIBRARY)

endif # BOARD_ALSA_AUDIO

include $(call all-makefiles-under,$(LOCAL_PATH))
