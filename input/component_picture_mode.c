/*
 * Copyright (C) 2017 Amlogic Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_tv_patch"

#include <stdio.h>
#include <sys/types.h>

#include "device_patch_mgr.h"
#include "component_picture_mode.h"
#include "device_patch_mgr.h"

void set_dev_pic_mode(struct aml_audio_device *adev, picture_mode_t pic_mode)
{
    struct component_picture_mode *instance = get_pic_mode_instance(adev);
    instance->pic_mode = pic_mode;
}

picture_mode_t get_dev_pic_mode(struct aml_audio_device *adev)
{
    struct component_picture_mode *instance = get_pic_mode_instance(adev);
    return instance->pic_mode;
}

void get_pic_mode_config(struct aml_audio_device *adev, bool *reconfig_in, bool *reconfig_out, bool *reconfig_ms12)
{
    struct component_picture_mode *instance = get_pic_mode_instance(adev);
    if (reconfig_in)
    {
        *reconfig_in = instance->mode_reconfig_in;
    }
    if (reconfig_out)
    {
        *reconfig_out = instance->mode_reconfig_out;
    }
    if (reconfig_ms12)
    {
        *reconfig_ms12 = instance->mode_reconfig_ms12;
    }
}

void reconfig_dev_pic_mode_in(struct aml_audio_device *adev, bool reconfig)
{
    struct component_picture_mode *instance = get_pic_mode_instance(adev);
    instance->mode_reconfig_in = reconfig;
}

void reconfig_dev_pic_mode_out(struct aml_audio_device *adev, bool reconfig)
{
    struct component_picture_mode *instance = get_pic_mode_instance(adev);
    instance->mode_reconfig_out = reconfig;
}

void reconfig_dev_pic_mode_ms12(struct aml_audio_device *adev, bool reconfig)
{
    struct component_picture_mode *instance = get_pic_mode_instance(adev);
    instance->mode_reconfig_ms12 = reconfig;
}
