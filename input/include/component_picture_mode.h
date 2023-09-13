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

#ifndef COMPONENT_PICTURE_MODE_H_
#define COMPONENT_PICTURE_MODE_H_

#include <sys/types.h>

/* Base on user settings */
typedef enum picture_mode
{
    PQ_STANDARD = 0,
    PQ_MOVIE,
    PQ_DYNAMIC,
    PQ_NATURAL,
    PQ_GAME,
    PQ_PC,
    PQ_CUSTOM,
    PQ_MODE_MAX
} picture_mode_t;

struct component_picture_mode
{
    picture_mode_t pic_mode;
    bool mode_reconfig_in;
    bool mode_reconfig_out;
    bool mode_reconfig_ms12;
};


void set_dev_pic_mode(struct aml_audio_device *adev, picture_mode_t pic_mode);

picture_mode_t get_dev_pic_mode(struct aml_audio_device *adev);

void get_pic_mode_config(struct aml_audio_device *adev, bool *reconfig_in, bool *reconfig_out, bool *reconfig_ms12);

void reconfig_dev_pic_mode_in(struct aml_audio_device *adev, bool reconfig);
void reconfig_dev_pic_mode_out(struct aml_audio_device *adev, bool reconfig);
void reconfig_dev_pic_mode_ms12(struct aml_audio_device *adev, bool reconfig);

#endif
