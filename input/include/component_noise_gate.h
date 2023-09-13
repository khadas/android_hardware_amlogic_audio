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

#ifndef COMPONENT_NOISE_GATE_H_
#define COMPONENT_NOISE_GATE_H_

struct aml_audio_device;

//amlogic soft ware noise gate fot analog TV source
struct component_noise_gate
{
    void *aml_ng_handle;
    int aml_ng_enable;
    float aml_ng_level;
    int aml_ng_attack_time;
    int aml_ng_release_time;
};

bool is_ng_enable(struct aml_audio_device *adev);

void *get_ng_handle(struct aml_audio_device *adev);

int noise_gate_process(struct aml_audio_device *adev, void *buffer, int samples);

void init_noise_gate_wrap(struct aml_audio_device *adev,
                    int aml_ng_enable,
                    float noise_level,
                    int attack_time,
                    int release_time);

void deinit_noise_gate_wrap(struct aml_audio_device *adev);

#endif
