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

#define LOG_TAG "audio_hw_input_dtv"

#include <stdio.h>
#include <errno.h>
#include <cutils/log.h>

#include "dtv_private_object.h"
#include "device_patch_mgr.h"

#ifdef ENABLE_DVB_PATCH
#include "dmx_audio_es.h"
#endif


int get_dtv_aformat(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->dtv_aformat;
}

void set_dtv_aformat(struct aml_audio_device *adev, int format)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->dtv_aformat = format;
}

uint32_t get_dtv_i2s_clock(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->dtv_i2s_clock;
}

void set_dtv_i2s_clock(struct aml_audio_device *adev, uint32_t i2s_clock)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->dtv_i2s_clock = i2s_clock;
}

uint32_t get_dtv_spdif_clock(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->dtv_spdif_clock;
}

void set_dtv_spdif_clock(struct aml_audio_device *adev, uint32_t spdif_clock)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->dtv_spdif_clock = spdif_clock;
}

uint32_t get_dtv_droppcm_size(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->dtv_droppcm_size;
}

void set_dtv_droppcm_size(struct aml_audio_device *adev, uint32_t drop_size)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->dtv_droppcm_size = drop_size;
}

struct aml_dtv_audio_instances * get_dtv_audio_instance(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->aml_dtv_audio_instances;
}

int get_dtv_sound_mode(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->dtv_sound_mode;
}

void set_dtv_sound_mode(struct aml_audio_device *adev, int sound_mode)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->dtv_sound_mode = sound_mode;
}

float get_dtv_volume(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->dtv_volume;
}

void set_dtv_volume(struct aml_audio_device *adev, float volume)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->dtv_volume = volume;
}

void enable_dtv_multi_demux(struct aml_audio_device *adev, int enable)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->is_multi_demux = enable;
}

bool is_dtv_multi_demux(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->is_multi_demux;
}

void enable_dtv_discontinue_mute(struct aml_audio_device *adev, bool enable)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->discontinue_mute_flag = enable;
}

bool is_dtv_discontinue_mute(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->discontinue_mute_flag;
}

void enable_dtv_audio_discontinue(struct aml_audio_device *adev, bool enable)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->audio_discontinue = enable;
}

bool is_dtv_audio_discontinue(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->audio_discontinue;
}

int get_dtv_no_underrun_count(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->no_underrun_count;
}

void set_dtv_no_underrun_count(struct aml_audio_device *adev, int count)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->no_underrun_count = count;
}

int inc_dtv_no_underrun_count(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    int temp = dtv_obj->no_underrun_count++;
    return temp;
}

int get_dtv_no_underrun_max(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->no_underrun_max;
}

void set_dtv_no_underrun_max(struct aml_audio_device *adev, int max)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->no_underrun_max = max;
}

void enable_dtv_underrun_mute(struct aml_audio_device *adev, bool enable)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->underrun_mute_flag = enable;
}

bool is_dtv_underrun_mute(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->underrun_mute_flag;
}

void enable_dtv_start_mute_flag(struct aml_audio_device *adev, bool enable)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->start_mute_flag = enable;
}

bool is_dtv_start_mute(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->start_mute_flag;
}

int get_dtv_start_mute_count(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->start_mute_count;
}

void set_dtv_start_mute_count(struct aml_audio_device *adev, int mute_count)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->start_mute_count = mute_count;
}

int get_dtv_start_mute_max(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->start_mute_max;
}

void set_dtv_start_mute_max(struct aml_audio_device *adev, int max)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->start_mute_max = max;
}

void enable_dtv_ad_start(struct aml_audio_device *adev, bool enable)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->ad_start_enable = enable;
}

bool is_dtv_ad_start(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->ad_start_enable;
}

void enable_dtv_insert_mute(struct aml_audio_device *adev, bool enable)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->insert_mute_flag = enable;
}

bool is_dtv_insert_mute(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->insert_mute_flag;
}

void acquire_dtv_mutex_lock(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    pthread_mutex_lock(&dtv_obj->dtv_lock);
}

void release_dtv_mutex_lock(struct aml_audio_device *adev)
{
   struct dtv_private_object *dtv_obj = get_dtv_object(adev);
   pthread_mutex_unlock(&dtv_obj->dtv_lock);
}


static bool is_multi_demux()
{
    if (access("/sys/module/dvb_demux/",F_OK) == 0 ||
        access("/sys/module/amlogic_dvb_demux/",F_OK) == 0) {
        ALOGI("use AmHwMultiDemux mode\n");
        return true;
    }
    ALOGI("use AmHwDemux mode\n");
    return false;
}

int init_dtv_object(struct aml_audio_device *adev)
{
    int ret = 0;
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    if (!dtv_obj) {
        ALOGE("%s() Error, dtv_obj = NULL, return!", __func__);
        return -EINVAL;
    }

    enable_dtv_multi_demux(adev, is_multi_demux());

#if ENABLE_DVB_PATCH
    dtv_obj->aml_dtv_audio_instances = aml_audio_calloc(1, sizeof(aml_dtv_audio_instances_t));
    if (dtv_obj->aml_dtv_audio_instances == NULL) {
        ALOGE("malloc aml_dtv_audio_instances failed");
        ret = -ENOMEM;
        return ret;
    } else {
        aml_dtv_audio_instances_t *dtv_audio_instances = (aml_dtv_audio_instances_t *)dtv_obj->aml_dtv_audio_instances;
        for (int index = 0; index < DVB_DEMUX_SUPPORT_MAX_NUM; index ++) {
            aml_dtvsync_t *dtvsync =  &dtv_audio_instances->dtvsync[index];
            pthread_mutex_init(&dtvsync->ms_lock, NULL);
        }
    }

    pthread_mutex_init(&dtv_obj->dtv_lock, NULL);
#endif

    dtv_obj->dtv_sound_mode = 0;
    /* dtv_volume init , range [0, 1]*/
    dtv_obj->dtv_volume = 1.0;
    dtv_obj->insert_mute_flag = false;
    return ret;
}

int destroy_dtv_object(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    if (!dtv_obj) {
        ALOGW("%s() Warning, dtv_obj = NULL!", __func__);
        return -EINVAL;
    }

    //free aml_dtv_audio_instances
    if (dtv_obj->aml_dtv_audio_instances) {
#if ENABLE_DVB_PATCH
        aml_dtv_audio_instances_t *dtv_audio_instances = (aml_dtv_audio_instances_t *)dtv_obj->aml_dtv_audio_instances;
        for (int index = 0; index < DVB_DEMUX_SUPPORT_MAX_NUM; index ++) {
            aml_dtvsync_t *dtvsync =  &dtv_audio_instances->dtvsync[index];
            pthread_mutex_destroy(&dtvsync->ms_lock);
        }
        aml_audio_free(dtv_obj->aml_dtv_audio_instances);
        pthread_mutex_destroy(&dtv_obj->dtv_lock);
        dtv_obj->aml_dtv_audio_instances = NULL;
#endif
    }

    //free dtv_obj
    free(dtv_obj);
    return 0;
}