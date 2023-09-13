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


#ifndef TV_PRIVATE_OBJECT_H_
#define TV_PRIVATE_OBJECT_H_

#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>

struct aml_audio_device;

struct tv_private_object {
    bool mute_flag;
};


bool is_tv_mute(struct aml_audio_device *adev);
void enable_tv_mute(struct aml_audio_device *adev, bool enable);

int init_tv_object(struct aml_audio_device *adev);
int destroy_tv_object(struct aml_audio_device *adev);

#endif
