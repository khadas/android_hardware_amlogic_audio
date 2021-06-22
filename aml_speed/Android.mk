# Copyright (C) 2014 The Android Open Source Project
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

##################################
# Audio bluetooth RC HAL
##################################

LOCAL_PATH := $(call my-dir)

# The default audio HAL module, which is a stub, that is loaded if no other
# device specific modules are present. The exact load order can be seen in
# libhardware/hardware.c
#
# The format of the name is audio.<type>.<hardware/etc>.so where the only
# required type is 'primary'. Other possibilites are 'a2dp', 'usb', etc.


include $(CLEAR_VARS)

LOCAL_MODULE := libamlspeed
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
    LOCAL_PROPRIETARY_MODULE := true
endif

LOCAL_SRC_FILES := \
    aml_audio_speed_manager.c \
    audio_sonic_speed_api.c \
    sonic_speed_wrapper.c

LOCAL_C_INCLUDES += \
        hardware/amlogic/audio/aml_speed/include \
        hardware/amlogic/audio/utils/include \
        system/media/audio/include \
        hardware/amlogic/LibAudio/sonic_ext

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    liblog \
    libutils \
    libamaudioutils \
    libsonic_ext

LOCAL_MODULE_TAGS := optional
#LOCAL_MODULE_CLASS := STATIC_LIBRARIES

LOCAL_CFLAGS += -Wno-error=date-time

#include $(BUILD_STATIC_LIBRARY)
include $(BUILD_SHARED_LIBRARY)
