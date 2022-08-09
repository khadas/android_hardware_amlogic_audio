/*
 * hardware/amlogic/audio/audioeffect/audio_post_process.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 */

#define LOG_TAG "audio_post_process"
//#define LOG_NDEBUG 0

#include <dlfcn.h>
#include <cutils/log.h>

#include "audio_post_process.h"
#include "Virtualx.h"
#include "aml_dec_api.h"
#include "aml_dts_dec_api.h"
#include "aml_effects_util.h"

static int check_dts_config(struct aml_native_postprocess *native_postprocess) {
    int cur_channels = dca_get_out_ch_internal();

    if (native_postprocess->vx_force_stereo == 1)
        cur_channels = 2;

    if (cur_channels >= 6) {
        cur_channels = 6;
    } else {
        cur_channels = 2;
    }

    if (native_postprocess->effect_in_ch != cur_channels) {

        ALOGD("%s, reconfig VX pre_channels = %d, cur_channels = %d, vx_force_stereo = %d",
            __func__, native_postprocess->effect_in_ch,
            cur_channels, native_postprocess->vx_force_stereo);

        VirtualX_reset(native_postprocess);
        if (cur_channels == 6) {
            VirtualX_Channel_reconfig(native_postprocess, 6);
        } else {
            VirtualX_Channel_reconfig(native_postprocess, 2);
        }
        native_postprocess->effect_in_ch = cur_channels;
    }

    return 0;
}

size_t audio_post_process(struct aml_native_postprocess *native_postprocess, int16_t *in_buffer, size_t in_frames)
{
    int ret = 0, j = 0;
    audio_buffer_t in_buf;
    audio_buffer_t out_buf;
    int frames = in_frames;

    if (native_postprocess == NULL ||
        native_postprocess->num_postprocessors != native_postprocess->total_postprocessors) {
        return ret;
    }

    if (native_postprocess->libvx_exist) {
        check_dts_config(native_postprocess);
    }

    for (j = 0; j < native_postprocess->num_postprocessors; j++) {
        effect_handle_t effect = native_postprocess->postprocessors[j];
        if (!getEffectStatus(effect)) {
             continue;
        }
        if (effect && (*effect) && (*effect)->process && in_buffer) {
            if (native_postprocess->libvx_exist && native_postprocess->effect_in_ch == 6 && j == 0) {
                /* skip multi channel processing for dts streaming in VX */
                continue;
            } else {
                /* do 2 channel processing */
                in_buf.frameCount =  out_buf.frameCount = frames;
                in_buf.s16 = out_buf.s16 = in_buffer;
                if ((*effect)->process)
                    ret = (*effect)->process(effect, &in_buf, &out_buf);
            }
            frames = out_buf.frameCount;
        }
    }

    if (ret < 0) {
        ALOGE("postprocess failed\n");
    }
    return frames;
}

int audio_VX_post_process(struct aml_native_postprocess *native_postprocess, int16_t *in_buffer, size_t bytes)
{
    int ret = 0;
    audio_buffer_t in_buf;
    audio_buffer_t out_buf;

    effect_handle_t effect = native_postprocess->postprocessors[0];
    if (effect && (*effect) && (*effect)->process && in_buffer &&
        native_postprocess->libvx_exist && native_postprocess->effect_in_ch == 6) {
        /* do multi channel processing for dts streaming in VX */
        in_buf.frameCount = bytes/12;
        out_buf.frameCount = bytes/12;
        in_buf.s16 = out_buf.s16 = in_buffer;
        ret = (*effect)->process(effect, &in_buf, &out_buf);
        if (ret < 0) {
            ALOGE("postprocess failed\n");
        } else {
            ret = bytes/3;
        }
    }

    return ret;
}

static int VirtualX_setparameter(struct aml_native_postprocess *native_postprocess, int param, int ch_num, int cmdCode)
{
    effect_handle_t effect = native_postprocess->postprocessors[0];
    int32_t replyData = 0;
    uint32_t replySize = sizeof(int32_t);
    uint32_t cmdSize = (int)(sizeof(effect_param_t) + sizeof(uint32_t) + sizeof(uint32_t));
    uint32_t buf32[sizeof(effect_param_t) / sizeof(uint32_t) + 2];
    effect_param_t *p = (effect_param_t *)buf32;

    p->psize = sizeof(uint32_t);
    p->vsize = sizeof(uint32_t);
    *(int32_t *)p->data = param;
    *((int32_t *)p->data + 1) = ch_num;

    if (effect && (*effect) && (*effect)->command) {
        (*effect)->command(effect, cmdCode, cmdSize, (void *)p, &replySize, &replyData);
    }

    return replyData;
}

void VirtualX_reset(struct aml_native_postprocess *native_postprocess)
{
     if (native_postprocess->libvx_exist) {
        VirtualX_setparameter(native_postprocess, 0, 0, EFFECT_CMD_RESET);
        ALOGI("VirtualX_reset!\n");
     }
     return;
}

void VirtualX_Channel_reconfig(struct aml_native_postprocess *native_postprocess, int ch_num)
{
    int ret = -1;

    if (native_postprocess->libvx_exist) {
        ret = VirtualX_setparameter(native_postprocess,
                                    DTS_PARAM_CHANNEL_NUM,
                                    ch_num, EFFECT_CMD_SET_PARAM);
        if (ret != ch_num) {
            ALOGE("Set VX input channel error: channel %d, ret = %d\n", ch_num, ret);
        }
    }

    return;
}

bool Check_VX_lib(void)
{
    void *h_libvx_handle = NULL;

    int fd = open(VIRTUALX_LICENSE_LIB_PATH, O_RDONLY);
    if (fd < 0) {
        ALOGD("%s, there isn't VX lib in (%s)", __func__, VIRTUALX_LICENSE_LIB_PATH);
        return false;
    } else {
        close(fd);
    }

    h_libvx_handle = dlopen(VIRTUALX_LICENSE_LIB_PATH, RTLD_NOW);
    if (!h_libvx_handle) {
        ALOGE("%s, fail to dlopen %s(%s)", __func__, VIRTUALX_LICENSE_LIB_PATH, dlerror());
        return false;
    } else {
        ALOGD("%s, success to dlopen %s", __func__, VIRTUALX_LICENSE_LIB_PATH);
        dlclose(h_libvx_handle);
        h_libvx_handle = NULL;
        /* VX effect lib is in system, set dts output as stream content */
        dca_set_out_ch_internal(0);
        return true;
    }
}

typedef enum {
    SRS_PARAM_MODE = 0,
    SRS_PARAM_DIALOG_CLARITY_MODE,
    SRS_PARAM_SURROUND_MODE,
    SRS_PARAM_VOLUME_MODE,
    SRS_PARAM_ENABLE,
    SRS_PARAM_TRUEBASS_ENABLE,
    SRS_PARAM_TRUEBASS_MODE,
    SRS_PARAM_TRUEBASS_SPKER_SIZE,
    SRS_PARAM_TRUEBASS_GAIN,
    SRS_PARAM_DIALOG_CLARITY_ENABLE,
    SRS_PARAM_DIALOG_CLARITY_GAIN,
    SRS_PARAM_DEFINITION_ENABLE,
    SRS_PARAM_DEFINITION_GAIN,
    SRS_PARAM_SURROUND_ENABLE,
    SRS_PARAM_SURROUND_GAIN,
    SRS_PARAM_INPUT_GAIN,
    SRS_PARAM_OUTPUT_GAIN,
    SRS_PARAM_OUTPUT_GAIN_COMP,
    SRS_PARAM_OUTPUT_GAIN_BYPASS
} SRSparams;

int set_aml_dts_effect_param(struct aml_native_postprocess *native_postprocess, char *param)
{
    int32_t value = 0, replyData = -1;
    uint32_t replySize = sizeof(int32_t);
    effect_handle_t effect = native_postprocess->postprocessors[native_postprocess->AML_DTS_index];
    uint32_t cmdSize = (int)(sizeof(effect_param_t) + sizeof(uint32_t) + sizeof(uint32_t));
    uint32_t buf32[sizeof(effect_param_t) / sizeof(uint32_t) + 2];
    effect_param_t *p = (effect_param_t *)buf32;
    char *ptr = NULL;

    if (!effect || !(*effect) || !(*effect)->command)
        return replyData;

    p->psize = sizeof(uint32_t);
    p->vsize = sizeof(uint32_t);

    ptr = strstr(param, "-enable");
    if (ptr) {
        sscanf(ptr + 8, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, enable = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_ENABLE;
        *((int32_t *)p->data + 1) = value;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
    ptr = strstr(param, "-tb_enable");
    if (ptr) {
        sscanf(ptr + 11, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, True Bass enable = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_TRUEBASS_ENABLE;
        *((int32_t *)p->data + 1) = value;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
    ptr = strstr(param, "-ss");
    if (ptr) {
        sscanf(ptr + 4, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, True Bass speaker size = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_TRUEBASS_SPKER_SIZE;
        *((int32_t *)p->data + 1) = value;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
    ptr = strstr(param, "-tbm");
    if (ptr) {
        sscanf(ptr + 5, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, True Bass mode = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_TRUEBASS_MODE;
        *((int32_t *)p->data + 1) = value;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
    ptr = strstr(param, "-tbv");
    if (ptr) {
        sscanf(ptr + 5, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, True Bass Gain = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_TRUEBASS_GAIN;
        *((float *)p->data + 1) = (float) value/100;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
    ptr = strstr(param, "-dc_enable");
    if (ptr) {
        sscanf(ptr + 11, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, Dialog Clarity Enable = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_DIALOG_CLARITY_ENABLE;
        *((int32_t *)p->data + 1) = value;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
    ptr = strstr(param, "-dcv");
    if (ptr) {
        sscanf(ptr + 5, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, Dialog Clarity Gain = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_DIALOG_CLARITY_GAIN;
        *((float *)p->data + 1) = (float) value/100;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
    ptr = strstr(param, "-def_enable");
    if (ptr) {
        sscanf(ptr + 12, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, Definition Enable = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_DEFINITION_ENABLE;
        *((int32_t *)p->data + 1) = value;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
    ptr = strstr(param, "-defv");
    if (ptr) {
        sscanf(ptr + 6, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, Definition Gain = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_DEFINITION_GAIN;
        *((float *)p->data + 1) = (float) value/100;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
    ptr = strstr(param, "-sd_enable");
    if (ptr) {
        sscanf(ptr + 11, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, Surround Enable = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_SURROUND_ENABLE;
        *((int32_t *)p->data + 1) = value;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
    ptr = strstr(param, "-sdv");
    if (ptr) {
        sscanf(ptr + 5, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, Surround Gain = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_SURROUND_GAIN;
        *((float *)p->data + 1) = (float) value/100;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
    ptr = strstr(param, "-ig");
    if (ptr) {
        sscanf(ptr + 4, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, Input Gain = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_INPUT_GAIN;
        *((float *)p->data + 1) = (float) value/100;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
    ptr = strstr(param, "-og");
    if (ptr) {
        sscanf(ptr + 4, "%d", &value);
        ALOGI("%s() Set DTS Parameters:%s, Output Gain = %d", __func__, ptr, value);
        *(int32_t *)p->data = SRS_PARAM_OUTPUT_GAIN;
        *((float *)p->data + 1) = (float) value/100;
        (*effect)->command(effect, EFFECT_CMD_SET_PARAM, cmdSize, (void *)p, &replySize, &replyData);
        goto exit;
    }
exit:
    return replyData;
}

int get_aml_dts_effect_param(struct aml_native_postprocess *native_postprocess, char *param, const char *keys)
{
    effect_handle_t effect = native_postprocess->postprocessors[native_postprocess->AML_DTS_index];
    uint32_t cmdSize = (int)(sizeof(effect_param_t) + sizeof(uint32_t));
    uint32_t buf32[sizeof(effect_param_t) / sizeof(uint32_t) + 2];
    effect_param_t *p = (effect_param_t *)buf32;
    uint32_t replySize = (int)(sizeof(effect_param_t) + sizeof(uint32_t) + sizeof(uint32_t));
    float scale = 0;
    int value = 0;
    char *ptr = NULL;

    if (!effect || !(*effect) || !(*effect)->command)
        return -1;

    p->psize = sizeof(uint32_t);
    p->vsize = sizeof(uint32_t);

    ptr = strstr(keys, "aq_tuning_dts_ts_enable");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_ENABLE;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        sprintf(param, "aq_tuning_dts_ts_enable=%d", *((int32_t *)p->data + 1));
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }
    ptr = strstr(keys, "aq_tuning_dts_ts_tb_enable");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_TRUEBASS_ENABLE;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        sprintf(param, "aq_tuning_dts_ts_tb_enable=%d", *((int32_t *)p->data + 1));
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }
    ptr = strstr(keys, "aq_tuning_dts_ts_ss");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_TRUEBASS_SPKER_SIZE;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        sprintf(param, "aq_tuning_dts_ts_ss=%d", *((int32_t *)p->data + 1));
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }
    ptr = strstr(keys, "aq_tuning_dts_ts_tbm");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_TRUEBASS_MODE;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        sprintf(param, "aq_tuning_dts_ts_tbm=%d", *((int32_t *)p->data + 1));
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }
    ptr = strstr(keys, "aq_tuning_dts_ts_tbv");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_TRUEBASS_GAIN;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        scale = *((float *)p->data + 1);
        value = (int)round(scale * 100);
        sprintf(param, "aq_tuning_dts_ts_tbv=%d", value);
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }
    ptr = strstr(keys, "aq_tuning_dts_ts_dc_enable");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_DIALOG_CLARITY_ENABLE;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        sprintf(param, "aq_tuning_dts_ts_dc_enable=%d", *((int32_t *)p->data + 1));
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }
    ptr = strstr(keys, "aq_tuning_dts_ts_dcv");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_DIALOG_CLARITY_GAIN;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        scale = *((float *)p->data + 1);
        value = (int)round(scale * 100);
        sprintf(param, "aq_tuning_dts_ts_dcv=%d", value);
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }
    ptr = strstr(keys, "aq_tuning_dts_ts_def_enable");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_DEFINITION_ENABLE;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        sprintf(param, "aq_tuning_dts_ts_def_enable=%d", *((int32_t *)p->data + 1));
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }
    ptr = strstr(keys, "aq_tuning_dts_ts_defv");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_DEFINITION_GAIN;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        scale = *((float *)p->data + 1);
        value = (int)round(scale * 100);
        sprintf(param, "aq_tuning_dts_ts_defv=%d", value);
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }
    ptr = strstr(keys, "aq_tuning_dts_ts_sd_enable");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_SURROUND_ENABLE;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        sprintf(param, "aq_tuning_dts_ts_sd_enable=%d", *((int32_t *)p->data + 1));
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }
    ptr = strstr(keys, "aq_tuning_dts_ts_sdv");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_SURROUND_GAIN;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        scale = *((float *)p->data + 1);
        value = (int)round(scale * 100);
        sprintf(param, "aq_tuning_dts_ts_sdv=%d", value);
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }
    ptr = strstr(keys, "aq_tuning_dts_ts_ig");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_INPUT_GAIN;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        scale = *((float *)p->data + 1);
        value = (int)round(scale * 100);
        sprintf(param, "aq_tuning_dts_ts_ig=%d", value);
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }
    ptr = strstr(keys, "aq_tuning_dts_ts_og");
    if (ptr) {
        *(int32_t *)p->data = SRS_PARAM_OUTPUT_GAIN;
        (*effect)->command(effect, EFFECT_CMD_GET_PARAM, cmdSize, (void *)p, &replySize, (void *)p);
        scale = *((float *)p->data + 1);
        value = (int)round(scale * 100);
        sprintf(param, "aq_tuning_dts_ts_og=%d", value);
        ALOGV("%s() Get DTS Parameters: [%s]", __func__, param);
        goto exit;
    }

exit:
    return 0;
}

