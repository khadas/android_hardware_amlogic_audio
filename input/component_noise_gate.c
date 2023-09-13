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
#include <cutils/log.h>

#include "component_noise_gate.h"
#include "device_patch_mgr.h"
#include "aml_ng.h"

bool is_ng_enable(struct aml_audio_device *adev)
{
    struct component_noise_gate *noise_gate = get_noise_gate_instance(adev);
    return (noise_gate->aml_ng_enable != 0 ? true : false);
}

void *get_ng_handle(struct aml_audio_device *adev)
{
    struct component_noise_gate *noise_gate = get_noise_gate_instance(adev);
    return noise_gate->aml_ng_handle;
}

int noise_gate_process(struct aml_audio_device *adev, void *buffer, int samples)
{
    int ret = 0;
    void *handle = get_ng_handle(adev);
    ret = noise_evaluation(handle, buffer, samples);
    return ret;
}

void init_noise_gate_wrap(struct aml_audio_device *adev,
                            int aml_ng_enable,
                            float noise_level,
                            int attack_time,
                            int release_time)
{
    struct component_noise_gate *noise_gate = get_noise_gate_instance(adev);
    noise_gate->aml_ng_level = noise_level;
    noise_gate->aml_ng_enable = aml_ng_enable;
    noise_gate->aml_ng_attack_time = attack_time;
    noise_gate->aml_ng_release_time = release_time;
    ALOGI("%s() audio noise gate level: %fdB, attack_time = %dms, release_time = %dms", __func__,
          noise_level, attack_time, release_time);
}

void deinit_noise_gate_wrap(struct aml_audio_device *adev)
{
    struct component_noise_gate *noise_gate = get_noise_gate_instance(adev);
    if (noise_gate->aml_ng_handle)
    {
        release_noise_gate(noise_gate->aml_ng_handle);
        noise_gate->aml_ng_handle = NULL;
    }
}
