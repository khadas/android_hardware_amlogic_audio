/*
 * hardware/amlogic/audio/audioeffect/audio_post_process.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 */

#define LOG_TAG "audio_hw_process_effect_postprocess"
//#define LOG_NDEBUG 0

#include <dlfcn.h>
#include <cutils/log.h>
#include <audio_utils/format.h>

#include "audio_post_process.h"
#include "Virtualx.h"
#include "aml_dec_api.h"
#include "aml_dts_dec_api.h"
#include "aml_effects_util.h"
#include "aml_ai_audio.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

//post-processing effects implemented by AML only support PCM32
#define EFFECT_PROCESSING_FORMAT (AUDIO_FORMAT_PCM_32_BIT)

/* path of virtualx effect license library */
#define VIRTUALX_LICENSE_LIB_PATH "/vendor/lib/soundfx/libvx.so"
#define DEBUG_ENABLE_DUMP_EFFECT_INFO 1

bool Check_VX_lib(void);
static int do_effect_process(struct aml_native_postprocess *native_postprocess, const effect_handle_t effect, void *in_buffer, size_t in_frames);

static struct effect_insert_seq_desc Effect_Insert_Seq_List[] = {
    {
        .type = EFFECT_TYPE_VIRTUAL_X,
        .seq = 0, /* insert on the head of Effect chain */
        .name = "VirtualX",
    },
    {
        .type = EFFECT_TYPE_TRUE_SURROUND_HD,
        .seq = -1, /* insert this effect by add sequeces */
        .name = "True Surround HD",
    },
    {
        .type = EFFECT_TYPE_HPEQ,
        .seq = -1,
        .name = "Hpeq",
    },
    {
        .type = EFFECT_TYPE_BALANCE,
        .seq = -1,
        .name = "Balance",
    },
    {
        .type = EFFECT_TYPE_TREBLEBASE,
        .seq = -1,
        .name = "TrebleBass",
    },
    {
        .type = EFFECT_TYPE_DBX,
        .seq = -1,
        .name = "DBX",
    },
    {
        .type = EFFECT_TYPE_DPE,
        .seq = -1,
        .name = "DPE",
    },
    {
        .type = EFFECT_TYPE_MS12_V2_DAP,
        .seq = -1,
        .name = "MS12v2 DAP",
    },
    {
        .type = EFFECT_TYPE_VIRTUAL_SURROUND,
        .seq = -1,
        .name = "Virtualsurround",
    },
};

struct effect_insert_seq_desc *find_effect_insert_desc_by_name(const char* name)
{
    for (int i= 0; i < ARRAY_SIZE(Effect_Insert_Seq_List); i++) {
        if (strcmp(name, Effect_Insert_Seq_List[i].name) == 0) {
            return &Effect_Insert_Seq_List[i];
        }
    }
    return NULL;
}

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

static void dump_effect_info_list(struct aml_native_postprocess *native_postprocess)
{
    ALOGD("--- Dump effect info list ---");
    for (int i = 0; i < native_postprocess->num_postprocessors; i++) {
        struct aml_post_effect_info *temp = &native_postprocess->postprocessors[i];
        ALOGD("\ti:%d effect:%s index:%d", i, (temp->idesc != NULL? temp->idesc->name : "customer"), temp->index);
    }
}

void update_effect_info_list(struct aml_native_postprocess *native_postprocess)
{
    int num_postprocessors = native_postprocess->num_postprocessors;
    struct aml_post_effect_info *newEffectInfo = &native_postprocess->postprocessors[num_postprocessors - 1];
    //If the effect is not dev by amlogic then insert it to the list tail
    if (newEffectInfo->idesc == NULL) {
        ALOGW("%s() warning, idesc = NULL not impl by AML, return!", __func__);
        return;
    }

    if (newEffectInfo->idesc->seq < 0 || num_postprocessors <= 1) {
        goto exit;
    }

    //adjust insert sequence
    int insert_index = -1;
    for (int i = 0; i < num_postprocessors; i++) {
        struct aml_post_effect_info *tempEffect;
        tempEffect =  &native_postprocess->postprocessors[i];
        if (!tempEffect->idesc) {
            insert_index = tempEffect->index;
            continue;
        }

        if (tempEffect->idesc && tempEffect->idesc->seq < 0) {
            insert_index = tempEffect->index;
            break;
        }

        if (tempEffect->idesc && newEffectInfo->idesc->seq < tempEffect->idesc->seq) {
            insert_index = tempEffect->index;
            break;
        }
    }

    //swap old effect with new effect by insert index
    if (insert_index >= 0 && insert_index < num_postprocessors) {
        struct aml_post_effect_info tailEffect = native_postprocess->postprocessors[num_postprocessors -1];
        for (int i = num_postprocessors - 1; i > insert_index; i--) {
            native_postprocess->postprocessors[i] = native_postprocess->postprocessors[i - 1];
        }
        native_postprocess->postprocessors[insert_index] = tailEffect;
        for (int i = 0; i < num_postprocessors; i++) {
            struct aml_post_effect_info *tempEffect =  &native_postprocess->postprocessors[i];
            tempEffect->index = i;
        }

        newEffectInfo = &native_postprocess->postprocessors[insert_index];
        ALOGD("%s() Adjust seq! effect:%s new_index:%d old_index:%d", __func__, newEffectInfo->idesc->name, newEffectInfo->index, num_postprocessors - 1);
    }

exit:
    //some special process
    if (newEffectInfo->idesc->type == EFFECT_TYPE_VIRTUAL_X) {
        native_postprocess->libvx_exist = Check_VX_lib();
    } else if (newEffectInfo->idesc->type == EFFECT_TYPE_TRUE_SURROUND_HD) {
        native_postprocess->AML_DTS_index = newEffectInfo->index;
    }
    ALOGD("%s() effect:%s handle:%p type:%d port_handle:%d index: %d", __func__,
        newEffectInfo->idesc->name, newEffectInfo->itfe, newEffectInfo->idesc->type, newEffectInfo->port, newEffectInfo->index);
}

int aml_add_audio_effect(struct aml_native_postprocess *native_postprocess, effect_handle_t effect, audio_port_handle_t port_handle)
{
    int status = 0;
    effect_descriptor_t desc;

    pthread_mutex_lock(&native_postprocess->lock);

    if (native_postprocess->num_postprocessors >= MAX_POSTPROCESSORS) {
        status = -ENOSYS;
        ALOGW("%s() warning, num_postprocessors > MAX_POSTPROCESSORS", __func__);
        pthread_mutex_unlock(&native_postprocess->lock);
        return status;
    }

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0) {
        goto exit;
    }

    /* save audio effect handle in audio hal. if it is saved, skip this. */
    for (int i = 0; i < native_postprocess->num_postprocessors; i++) {
        if (native_postprocess->postprocessors[i].itfe == effect) {
            status = 0;
            ALOGW("%s() Warning, effect:%s already added!", __func__, desc.name);
            goto exit;
        }
    }

    const struct effect_insert_seq_desc *insert_desc = find_effect_insert_desc_by_name(desc.name);
    if (insert_desc == NULL) {
        ALOGW("%s() warning, Not find Effect:%s in Amlgic effect list!", __func__, desc.name);
    }

    native_postprocess->postprocessors[native_postprocess->num_postprocessors].itfe = effect;
    native_postprocess->postprocessors[native_postprocess->num_postprocessors].idesc = insert_desc;
    native_postprocess->postprocessors[native_postprocess->num_postprocessors].index = native_postprocess->num_postprocessors;
    native_postprocess->postprocessors[native_postprocess->num_postprocessors].port = port_handle;
    native_postprocess->num_postprocessors++;

    update_effect_info_list(native_postprocess);

exit:
    pthread_mutex_unlock(&native_postprocess->lock);
#ifdef DEBUG_ENABLE_DUMP_EFFECT_INFO
    dump_effect_info_list(native_postprocess);
#endif
    ALOGI("%s() ret:%d effect:%s handle:%p port:%d num_postprocessors:%d", __FUNCTION__, status,
        (status ==0? desc.name : "null"), effect, port_handle, native_postprocess->num_postprocessors);
    return status;
}

int aml_remove_audio_effect(struct aml_native_postprocess *native_postprocess, effect_handle_t effect, audio_port_handle_t port_handle __unused)
{
    int status = -EINVAL;
    bool found = false;
    effect_descriptor_t desc;
    struct aml_post_effect_info *rmEffectInfo = NULL;

    pthread_mutex_lock(&native_postprocess->lock);

    if (native_postprocess->num_postprocessors <= 0) {
        status = -ENOSYS;
        goto exit;
    }

    for (int i = 0; i < native_postprocess->num_postprocessors; i++) {
        if (found) {
            native_postprocess->postprocessors[i - 1].itfe = native_postprocess->postprocessors[i].itfe;
            native_postprocess->postprocessors[i - 1].index = native_postprocess->postprocessors[i].index;
            native_postprocess->postprocessors[i - 1].idesc = native_postprocess->postprocessors[i].idesc;
            native_postprocess->postprocessors[i - 1].port = native_postprocess->postprocessors[i].port;
            continue;
        }

        if (native_postprocess->postprocessors[i].itfe == effect) {
            native_postprocess->postprocessors[i].itfe = NULL;
            native_postprocess->postprocessors[i].index = -1;
            native_postprocess->postprocessors[i].idesc = NULL;
            native_postprocess->postprocessors[i - 1].port = -1;
            status = 0;
            found = true;
        }
    }

    if (status != 0) {
        goto exit;
    }

    native_postprocess->num_postprocessors--;
    status = (*effect)->get_descriptor(effect, &desc);
    //update exist effect array offset
    for (int i = 0; i < native_postprocess->num_postprocessors; i++) {
        struct aml_post_effect_info * temp = &native_postprocess->postprocessors[i];
        temp->index = i;
    }

exit:
    pthread_mutex_unlock(&native_postprocess->lock);
#ifdef DEBUG_ENABLE_DUMP_EFFECT_INFO
    dump_effect_info_list(native_postprocess);
#endif
    ALOGI("%s() ret:%d effect:%s handle:%p port:%d num_postprocessors:%d", __FUNCTION__, status,
        (status == 0 ? desc.name : "null"), effect, port_handle, native_postprocess->num_postprocessors);
    return status;
}

/*
Data path:
  case 1: src_format != proc_format
    in_buffer [src_format] -> {pre_process_stereo() [proc_format] } -> itfe.process() [proc_format] -> {post_process_stereo [src_format]}
  case 2: src_format == proc_format
    in_buffer [src_format] -> itfe.process() [proc_format]
Note: return value must be: in_frames
*/
size_t audio_post_process(struct aml_native_postprocess *native_postprocess, void *in_buffer, size_t in_frames)
{
    int ret = 0, j = 0;
    audio_buffer_t in_buf;
    audio_buffer_t out_buf;
    int frames = in_frames;
    bool ai_process_done = false;


    pthread_mutex_lock(&native_postprocess->lock);
    if (native_postprocess->num_postprocessors == 0) {
        pthread_mutex_unlock(&native_postprocess->lock);
        return frames;
    }

    if (native_postprocess->libvx_exist) {
        check_dts_config(native_postprocess);
    }

    if (native_postprocess->libvx_exist && native_postprocess->effect_in_ch == 6) {
        ai_process_done = false;
    } else {
        if (native_postprocess->ai_handle) {
            in_buf.frameCount =  out_buf.frameCount = frames;
            in_buf.s16 = out_buf.s16 = in_buffer;
            ret = aml_ai_audio_process(native_postprocess->ai_handle, &in_buf, &out_buf);
            ai_process_done = true;
        }
    }

    for (j = 0; j < native_postprocess->num_postprocessors; j++) {
        effect_handle_t effect = native_postprocess->postprocessors[j].itfe;
        const struct aml_post_effect_info *effectInfo = &native_postprocess->postprocessors[j];
        if (effect && (*effect) && (*effect)->process && in_buffer) {
            if ((native_postprocess->libvx_exist && native_postprocess->effect_in_ch == 6 && j == 0) ||
                  (((native_postprocess->effect_ctrl.effect_mode == EFFECT_MODE_AUTO) && !(native_postprocess->effect_ctrl.is_dts)) && j == 0)) {
                /* skip multi channel processing for dts streaming in VX */
                continue;
            } else {
                /* do 2 channel processing */
                in_buf.frameCount =  out_buf.frameCount = frames;
                in_buf.raw = out_buf.raw = in_buffer;
                if (effectInfo->idesc == NULL) {
                    /* Effect designed by customer without pre/post process */
                    ret = (*effect)->process(effect, &in_buf, &out_buf);
                } else {
                    ret = do_effect_process(native_postprocess, effect, in_buffer, in_frames);
                }
            }
            frames = out_buf.frameCount;
        }
    }

    if (ret < 0) {
        ALOGE("postprocess failed! ret:%d", ret);
    }

    pthread_mutex_unlock(&native_postprocess->lock);
    return frames;
}

int audio_VX_post_process(struct aml_native_postprocess *native_postprocess, int16_t *in_buffer, size_t bytes)
{
    int ret = 0;
    audio_buffer_t in_buf;
    audio_buffer_t out_buf;

    effect_handle_t effect = native_postprocess->postprocessors[0].itfe;
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

/*
Function: bit convert to ::proc_format before enter effect_process
Description:
    effect chain fixed support 32bit process, if input data is not 32 bit do bit convert
Data Path:
    in_buffer -> pre_process -> pre_out (temp_proc_buffer)
*/
static int pre_process_stereo(struct aml_native_postprocess *post_handle, void *in, size_t in_frames, void **out, size_t *out_frames)
{
    audio_format_t in_format = post_handle->src_format;
    audio_format_t out_format = post_handle->proc_format;
    if (in_format != out_format) {
        size_t request_buffer_size = in_frames * 2 /*channels*/ * audio_bytes_per_sample(out_format);
        if (request_buffer_size > post_handle->temp_proc_capacity) {
            void *addr = aml_audio_realloc(post_handle->temp_proc_buffer, request_buffer_size);
            post_handle->temp_proc_buffer = addr;
            post_handle->temp_proc_capacity = request_buffer_size;
            ALOGE_IF(addr == NULL, "%s() line:%d Fatal error, No memory!", __func__, __LINE__);
        }
        memcpy_by_audio_format(post_handle->temp_proc_buffer, out_format, in, in_format, in_frames * 2 /*channels*/);
        *out = post_handle->temp_proc_buffer;
        *out_frames = in_frames;
    } else {
        *out = in;
        *out_frames = in_frames;
    }
    return 0;
}

/*
Function: bit convert to ::src_format after effect_process
Data Path:
   in -> post_process -> post_out (out)
*/
static int post_process_stereo(struct aml_native_postprocess *post_handle, void *in, size_t in_frames, void *out, size_t *out_frames)
{
    //no pre_process
    if (in == out) {
        *out_frames = in_frames;
        return 0;
    }

    audio_format_t in_format = post_handle->proc_format;
    audio_format_t out_format = post_handle->src_format;
    if (in_format != out_format) {
        size_t samples = in_frames * 2 /*channels*/;
        memcpy_by_audio_format(out, out_format, in, in_format, samples);
    }
    *out_frames = in_frames;
    return 0;
}

static int do_effect_process(struct aml_native_postprocess *native_postprocess, const effect_handle_t effect, void *in_buffer, size_t in_frames)
{
    audio_buffer_t in_buf;
    audio_buffer_t out_buf;
    int ret = 0;

    if (native_postprocess->src_format == native_postprocess->proc_format) {
        in_buf.frameCount = out_buf.frameCount = in_frames;
        in_buf.s32 = out_buf.s32 = (int32_t*)in_buffer;
        //do effect process
        ret = (*effect)->process(effect, &in_buf, &out_buf);
    }
    else
    {
        void *pre_out_buffer = NULL;
        size_t pre_out_frames = 0;
        pre_process_stereo(native_postprocess, in_buffer, in_frames, &pre_out_buffer, &pre_out_frames);

        //do effect process
        if (pre_out_frames != 0) {
            in_buf.frameCount = out_buf.frameCount = pre_out_frames;
            in_buf.s32 = out_buf.s32 = (int32_t*)pre_out_buffer;
            ret = (*effect)->process(effect, &in_buf, &out_buf);

            void *post_out_buf = in_buffer;
            size_t post_out_frames = in_frames;
            post_process_stereo(native_postprocess, out_buf.raw, out_buf.frameCount, post_out_buf, &post_out_frames);
            if (post_out_frames != in_frames) {
                ALOGW("%s() Warning! post_out_frames:%zu != in_frames:%zu", __func__, post_out_frames, in_frames);
            }
        }
    }

    return ret;
}

static int VirtualX_setparameter(struct aml_native_postprocess *native_postprocess, int param, int ch_num, int cmdCode)
{
    effect_handle_t effect = native_postprocess->postprocessors[0].itfe;
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
                                    PARAM_CHANNEL_NUM,
                                    ch_num, EFFECT_CMD_SET_PARAM);
        if (ret != ch_num) {
            ALOGE("Set VX input channel error: channel %d, ret = %d\n", ch_num, ret);
            /* if VX can't set input mode, force dts decoder stereo output */
            if (ret == 2) {
                dca_set_out_ch_internal(2);
            }
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
    effect_handle_t effect = native_postprocess->postprocessors[native_postprocess->AML_DTS_index].itfe;
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
    effect_handle_t effect = native_postprocess->postprocessors[native_postprocess->AML_DTS_index].itfe;
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


bool is_vendor_support_libvx(struct aml_native_postprocess *native_postprocess)
{
    return native_postprocess->libvx_exist;
}

int init_vendor_post_process(struct aml_native_postprocess *native_postprocess, audio_format_t src_format)
{
    if (!native_postprocess) {
        ALOGW("%s() Warning, native_postprocess = NULL!", __func__);
        return -EINVAL;
    }

    memset(native_postprocess, 0, sizeof(struct aml_native_postprocess));
    pthread_mutex_init(&native_postprocess->lock, NULL);
    native_postprocess->libvx_exist = Check_VX_lib();
    native_postprocess->src_format = src_format;
    native_postprocess->proc_format = EFFECT_PROCESSING_FORMAT;

    ALOGI("%s() source_format:0x%x proc_format:0x%x", __func__, native_postprocess->src_format, native_postprocess->proc_format);
    return 0;
}

void destroy_vendor_post_process(struct aml_native_postprocess *native_postprocess)
{
    if (!native_postprocess) {
        ALOGW("%s() Warning, native_postprocess = NULL!", __func__);
        return;
    }

    if (native_postprocess->temp_proc_buffer != NULL) {
        aml_audio_free(native_postprocess->temp_proc_buffer);
        native_postprocess->temp_proc_buffer = NULL;
    }

    pthread_mutex_destroy(&native_postprocess->lock);
}
