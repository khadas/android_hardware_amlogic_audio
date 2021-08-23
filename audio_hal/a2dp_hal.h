/******************************************************************************
 *
 *  Copyright 2009-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/*****************************************************************************
 *
 *  Filename:      audio_a2dp_hw.h
 *
 *  Description:
 *
 *****************************************************************************/

#ifndef AUDIO_A2DP_HW_H
#define AUDIO_A2DP_HW_H

#include <stdint.h>
#include <pthread.h>
#include "audio_hw.h"
#include "aml_audio_resample_manager.h"
#include "a2dp_hw.h"


typedef struct {
    pthread_mutex_t mutex;
    int ctrl_fd;
    int audio_fd;
    size_t buffer_sz;
    struct audio_config a2dp_config;
    btav_a2dp_codec_config_t capability;
    a2dp_state_t state;
    aml_audio_resample_t *resample;
    void * vir_buf_handle;
    uint64_t last_write_time;
    char * buff_conv_format;
    size_t buff_size_conv_format;
    uint64_t mute_time;
} aml_a2dp_hal;


/*****************************************************************************
 *  Functions
 *****************************************************************************/

int a2dp_out_open(struct aml_audio_device *adev);
int a2dp_out_close(struct aml_audio_device *adev);
int a2dp_out_standby(struct aml_audio_device *adev);
ssize_t a2dp_out_write(struct aml_audio_device *adev, audio_config_base_t *config, const void* buffer, size_t bytes);
uint32_t a2dp_out_get_latency(struct aml_audio_device *adev);
int a2dp_hal_dump(struct aml_audio_device *adev, int fd);

#endif /* A2DP_AUDIO_HW_H */
