/*
 * hardware/amlogic/audio/TvAudio/aml_audio_ms12.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 */


#define LOG_TAG "libms12"
// #define LOG_NDEBUG 0


// fpr android O build, use cutils/log.h
//#include <cutils/log.h>
// for android P build, use log/log.h
#include <log/log.h>
#include <dolby_ms12.h>
#include <dolby_ms12_config_params.h>
#include <dolby_ms12_status.h>

#include "aml_audio_ms12.h"


#define DOLBY_SAMPLE_SIZE 4//2ch x 2bytes(16bits) = 4 bytes


int get_dolby_ms12_output_details(struct dolby_ms12_desc *ms12_desc)
{
    ms12_desc->dolby_ms12_init_argv = dolby_ms12_config_params_get_config_params(&ms12_desc->dolby_ms12_init_argc);
    ms12_desc->output_samplerate = dolby_ms12_config_params_get_dolby_config_output_samplerate();
    ms12_desc->output_channelmask = dolby_ms12_config_params_get_dolby_config_output_channelmask();
    ALOGD("%s() dolby_ms12_init_argv %p argc %d dolby ms12 output config %#x samplerate %d channelmask %#x\n",
          __func__, ms12_desc->dolby_ms12_init_argv, ms12_desc->dolby_ms12_init_argc, ms12_desc->output_config,
          ms12_desc->output_samplerate, ms12_desc->output_channelmask);
    return 0;
}

int get_dolby_ms12_init(struct dolby_ms12_desc *ms12_desc, char *dolby_ms12_path)
{
    int ret = 0;

    ALOGD("+%s()\n", __FUNCTION__);
    ret = get_libdolbyms12_handle(dolby_ms12_path);
    if (ret) {
        ALOGE("%s, fail to get ms12 handle\n", __FUNCTION__);
        return ret;
    }
    if (ms12_desc->dolby_ms12_init_argv) {
        ms12_desc->dolby_ms12_ptr = dolby_ms12_init(ms12_desc->dolby_ms12_init_argc, ms12_desc->dolby_ms12_init_argv);
        ms12_desc->dolby_ms12_out_max_size = get_dolby_ms12_output_max_size();
        ALOGV("%s() dolby ms12 init return %p dolby_ms12_out_max_size %d\n",
              __FUNCTION__, ms12_desc->dolby_ms12_ptr, ms12_desc->dolby_ms12_out_max_size);
        if (ms12_desc->dolby_ms12_ptr == NULL) {
            ALOGD("%s() dolby ms12 init fail!\n", __FUNCTION__);
            ms12_desc->dolby_ms12_enable = false;
        }
        else {
            ms12_desc->dolby_ms12_enable = true;
            ALOGD("%s() dolby ms12 output config %#x\n", __FUNCTION__, ms12_desc->output_config);
            if (ms12_desc->output_config & MS12_OUTPUT_MASK_DD)
                set_offload_playback_dolby_ms12_output_format(AUDIO_FORMAT_AC3);
            else if (ms12_desc->output_config & MS12_OUTPUT_MASK_DDP)
                set_offload_playback_dolby_ms12_output_format(AUDIO_FORMAT_E_AC3);
            else if (ms12_desc->output_config & MS12_OUTPUT_MASK_MAT)
                set_offload_playback_dolby_ms12_output_format(AUDIO_FORMAT_MAT);
            else
                set_offload_playback_dolby_ms12_output_format(AUDIO_FORMAT_PCM);
            ALOGD("%s() init DolbyMS12 success\n", __FUNCTION__);
        }
        ms12_desc->curDBGain = 0;
    }
    ALOGD("-%s() dolby_ms12_enable %d\n", __FUNCTION__, ms12_desc->dolby_ms12_enable);
    return 0;
}

int aml_ms12_config(struct dolby_ms12_desc *ms12_desc
                    , audio_format_t config_format
                    , audio_channel_mask_t config_channel_mask
                    , int config_sample_rate
                    , int output_config
                    , char *dolby_ms12_path)
{
    ALOGI("+%s() %d\n", __FUNCTION__, __LINE__);
    //int low_latency = 1;
    int low_latency = 0;
    ms12_desc->input_config_format = config_format;
    ms12_desc->config_channel_mask = config_channel_mask;
    ms12_desc->config_sample_rate = config_sample_rate;
    ms12_desc->output_config = output_config;
    ALOGI("%s() config input format %#x channle mask %#x samplerate %d output config %#x\n",
          __FUNCTION__, config_format, config_channel_mask, config_sample_rate, output_config);
    dolby_ms12_config_params_reset_config_params();
    if (get_audio_system_format() == AUDIO_FORMAT_PCM_16_BIT) {
        dolby_ms12_config_params_set_system_flag(true);
    }
    /*only enable app input in continuous mode*/
    if (get_audio_app_format() == AUDIO_FORMAT_PCM_16_BIT && is_dolby_ms12_continuous_mode()) {
        dolby_ms12_config_params_set_app_flag(true);
    }
    if ((get_audio_associate_format() == AUDIO_FORMAT_AC3) ||
        (get_audio_associate_format() == AUDIO_FORMAT_E_AC3) ||
        (get_audio_associate_format() == AUDIO_FORMAT_MAT) ||
        (get_audio_associate_format() == AUDIO_FORMAT_HE_AAC_V1) ||
        (get_audio_associate_format() == AUDIO_FORMAT_HE_AAC_V2)) {
        dolby_ms12_config_params_set_associate_flag(true);
    }

    dolby_ms12_config_params_set_audio_stream_out_params(
        2 //AUDIO_OUTPUT_FLAG_PRIMARY
        , ms12_desc->input_config_format
        , ms12_desc->config_channel_mask
        , ms12_desc->config_sample_rate
        , ms12_desc->output_config);
    get_dolby_ms12_output_details(ms12_desc);

    get_dolby_ms12_init(ms12_desc, dolby_ms12_path);
    dolby_ms12_set_sys_low_latency(low_latency);
    ALOGI("-%s() %d\n", __FUNCTION__, __LINE__);
    return 0;
}

int aml_ms12_lib_preload(char *dolby_ms12_path) {
    int ret = 0;
    void * dolby_ms12_ptr = NULL;
    ALOGD("+%s()\n", __FUNCTION__);
    ret = get_libdolbyms12_handle(dolby_ms12_path);
    if (ret == 0) {
        dolby_ms12_ptr = dolby_ms12_init(1, NULL);
        if (dolby_ms12_ptr) {
            dolby_ms12_release(dolby_ms12_ptr);
        }
    }
    ALOGD("-%s()\n", __FUNCTION__);
    return 0;
}

int aml_ms12_lib_release() {
    release_libdolbyms12_handle();
    dolby_ms12_self_cleanup();
    ALOGD("-%s()\n", __FUNCTION__);
    return 0;
}


int aml_ms12_cleanup(struct dolby_ms12_desc *ms12_desc)
{
    dolby_ms12_status_self_cleanup();
    dolby_ms12_release(ms12_desc->dolby_ms12_ptr);
    //dolby_ms12_self_cleanup();
    ms12_desc->dolby_ms12_ptr = NULL;
    ALOGI("%s", __func__);
    return 0;
}

int aml_ms12_update_runtime_params(struct dolby_ms12_desc *ms12_desc, char *cmd)
{
    ALOGV("+%s()\n", __FUNCTION__);
    int ret = -1;
    int mutex_result = 0;
    if (ms12_desc->dolby_ms12_init_argv) {
        dolby_ms12_config_params_reset_config_params();
        if (get_audio_system_format() == AUDIO_FORMAT_PCM_16_BIT) {
            dolby_ms12_config_params_set_system_flag(true);
        }
        if (get_audio_app_format() == AUDIO_FORMAT_PCM_16_BIT && is_dolby_ms12_continuous_mode()) {
            dolby_ms12_config_params_set_app_flag(true);
        }
        if ((get_audio_associate_format() == AUDIO_FORMAT_AC3) || (get_audio_associate_format() == AUDIO_FORMAT_E_AC3)) {
            dolby_ms12_config_params_set_associate_flag(true);
        }
        ms12_desc->dolby_ms12_init_argv = dolby_ms12_config_params_update_runtime_config_params(&ms12_desc->dolby_ms12_init_argc, cmd);
        /*dolby_ms12_ptr should be in lock mode*/
        mutex_result = pthread_mutex_trylock(&ms12_desc->lock);
        if (ms12_desc->dolby_ms12_ptr) {
            ret = dolby_ms12_update_runtime_params(ms12_desc->dolby_ms12_ptr, ms12_desc->dolby_ms12_init_argc, ms12_desc->dolby_ms12_init_argv);
        }
        if (mutex_result == 0) {
            /*we success lock it, now unlock it*/
            pthread_mutex_unlock(&ms12_desc->lock);
        }
    }
    ALOGV("-%s() ret %d\n", __FUNCTION__, ret);
    return ret;
}

int aml_ms12_update_runtime_params_lite(struct dolby_ms12_desc *ms12_desc)
{
    ALOGI("+%s()\n", __FUNCTION__);
    int ret = -1;
    if (ms12_desc->dolby_ms12_init_argv) {
        dolby_ms12_config_params_reset_config_params();
        ms12_desc->dolby_ms12_init_argv = dolby_ms12_config_params_get_runtime_config_params_lite(&ms12_desc->dolby_ms12_init_argc);
        if (ms12_desc->dolby_ms12_ptr) {
            // we still need to use this interface, it calls ms12 with lock, then gain can set correctly each time
            ret = dolby_ms12_update_runtime_params(ms12_desc->dolby_ms12_ptr, ms12_desc->dolby_ms12_init_argc, ms12_desc->dolby_ms12_init_argv);
            // for thing like gain control, if continously setting volume in short time, we need no lock setting, or sound will break;
            //ret = dolby_ms12_update_runtime_params_nolock(ms12_desc->dolby_ms12_ptr, ms12_desc->dolby_ms12_init_argc, ms12_desc->dolby_ms12_init_argv);
        }
    }
    ALOGI("-%s() ret %d\n", __FUNCTION__, ret);
    return ret;
}

