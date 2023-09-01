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

#define LOG_TAG "aml_ai_aq"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <pthread.h>
#include <math.h>
#include <utils/Log.h>
#include <system/audio.h>
#include <system/audio_effect.h>
#include <audio_utils/channels.h>
#include <audio_utils/format.h>

#include "aml_audio_resample_manager.h"
#include "aml_malloc_debug.h"

#include "iva.h"
#include "iva_scene.h"
#include "iva_scene_common.h"
#include "aml_ai_audio.h"
#include "aml_android_utils.h"
#include "aml_alsa_mixer.h"
#include "audio_post_process.h"

//IVA AI prebuilt library
static const char *AI_LIB_PATH = "/vendor/lib/libamlogic_ai_audio.so";

/*
IVA AI lib required condition:
  - input data format must be: 1ch/16bit/16000
  - once loop process data size: 25ms
*/
#define IVA_LIB_REQUIRED_FRAMES ((int)(16000 / 1000 * 25))
#define MAX_BUFFER_LIST_LENGTH 6
#define FLOAT_INT16_CONVERSION_FACTOR (32765.0)
//value rang: [0.0~1.0]
#define FLOAT_TO_INT16(f_val) ((int16_t)((float)f_val * FLOAT_INT16_CONVERSION_FACTOR))
#define INT16_TO_FLOAT(s_val) ((float)((float)s_val / FLOAT_INT16_CONVERSION_FACTOR))

#define ENABLE_ACR_DATA_DUMP 1

struct audio_config_base DEFAULT_TARGET_CONFIG = {
    .sample_rate = 16000,
    .channel_mask = AUDIO_CHANNEL_OUT_MONO,
    .format = AUDIO_FORMAT_PCM_16_BIT,
};

enum ai_aq_state_e {
    AI_AQ_STATE_UNINITIALIZED,
    AI_AQ_STATE_INITIALIZED,
    AI_AQ_STATE_ACTIVE,
};

struct ai_audio_libraries_context {
    //dlopen libamlogic_ai_audio
    void *dl_handle;
    //instance for libamlogic_ai_audio
    neu_iva_scene_class_handle_t iva_handle;
    //New instance for libamlogic_ai_audio
    neu_iva_scene_class_handle_t (*iva_init)(unsigned int mode, const char *model_path);
    //Control APIs based on iva_handle
    IVA_STATUS_E (*iva_process)(neu_iva_scene_class_handle_t iva_scene_handle, int16_t *samples, int sampleCount, neu_iva_scene_class_result_t *results);
    IVA_STATUS_E (*iva_deinit)(neu_iva_scene_class_handle_t iva_scene_handle, unsigned int mode);
    IVA_STATUS_E (*iva_set_param)(neu_iva_scene_class_handle_t iva_scene_class_handle, const neu_iva_scene_class_parameter_t *params);
    IVA_STATUS_E (*iva_get_param)(neu_iva_scene_class_handle_t iva_scene_class_handle, neu_iva_scene_class_parameter_t *params);
    IVA_STATUS_E (*iva_reset)(neu_iva_scene_class_handle_t iva_scene_handle);
};

enum buffer_status_e {
    BUFFER_IS_EMPTY=0,
    BUFFER_IS_PENDING,
    BUFFER_IS_BUSY,
};

typedef struct buffer_info {
    uint8_t *start;
    size_t bytes;
    size_t capcity;
    int status;
} buffer_info;

typedef struct queue_item {
    buffer_info *value;
} item_t;

struct simple_buffer_queue {
    //Real table size
    int size;
    int w;
    int r;
    item_t *table;
};

struct aml_ai_audio_module {
    struct audio_config_base source_config;
    struct audio_config_base target_config;
    //using process buffer if source config != target config
    void *process_buffer;
    uint32_t process_data_size;
    uint32_t process_buffer_capcity;
    uint32_t frameCount;
    //Using buffer queue manger buffer list
    struct simple_buffer_queue bufQueue;
    buffer_info bufInfoList[MAX_BUFFER_LIST_LENGTH];
    //Resample component
    aml_audio_resample_t *resample;
    bool do_resample;
    //module state
    int state;
    //AI libraries running status
    struct ai_audio_libraries_context aiLib;
    neu_iva_scene_class_result_t aiResult;
    struct aml_mixer_handle *mixer_ctrl;
    //update flush_time if ai->process success
    struct timespec flush_time;
    bool acr_result_available;
    //pthread APIs
    pthread_t iva_thread_id;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool req_thread_exit;
};

void ts_wait_time_us(struct timespec *ts, uint32_t time_us);
bool is_ai_result_change(neu_iva_scene_class_result_t *new_val, neu_iva_scene_class_result_t *old_val);
int update_ai_process_result(struct aml_ai_audio_module *module, float score, int32_t label);
//iva libraries APIs
static int iva_libraries_open(struct ai_audio_libraries_context *aiLibContext, const char *lib_path);
static int iva_class_init(struct ai_audio_libraries_context *aiLibContext);
static int iva_class_deinit(struct ai_audio_libraries_context *aiLibContext);
static int iva_libraries_close(struct ai_audio_libraries_context *aiLibContext);
static int aml_ai_audio_dump_data(const char *path, const void *buf, size_t bytes);
//Buffer Queue APIs
int init_buf_queue(struct simple_buffer_queue* queue, int length);
void deinit_buf_queue(struct simple_buffer_queue* queue);
int pop_queue(struct simple_buffer_queue* queue, item_t* item);
int push_queue(struct simple_buffer_queue* queue, item_t item);
bool queue_is_empty(struct simple_buffer_queue* queue);
int queue_length(struct simple_buffer_queue* queue);
//Buffer Unit control APIs
buffer_info *get_free_buffer_l(struct aml_ai_audio_module *aiModule);
size_t fill_buffer_l(buffer_info *buffer, uint8_t *data, size_t bytes);
void empty_buffer_l(buffer_info *buffer);
bool is_buffer_filled_done_l(const buffer_info *buffer);


/*
Data pipe:
    1) path1:
        - main thread:
          -inBuf -> format convert -> process buffer -> send to BufferQueue
        - iva thread
          - read buffer from BufferQueue -> iva process -> out Result
    2) path2:
        - inBuf -> outBuf
*/
int aml_ai_audio_process(void *handle, audio_buffer_t *inBuf, audio_buffer_t *outBuf)
{
    struct aml_ai_audio_module *aiModule;
    struct ai_audio_libraries_context *aiLibContext;
    uint32_t frameCount;
    void *processed_out_buffer = NULL;
    uint32_t processed_out_bytes = 0;
    int ret = 0;

    if (!handle || !inBuf || !outBuf) {
        ALOGV("%s() line:%d Warning! handle:%p inBuf:%p outBuf:%p", __func__,
                __LINE__, handle, inBuf, outBuf);
        return -EINVAL;
    }

    if (!inBuf->raw || !outBuf->raw ||
        inBuf->frameCount == 0 || outBuf->frameCount == 0) {
        return -EINVAL;
    }

    aiModule = (struct aml_ai_audio_module *)handle;
    aiLibContext = &aiModule->aiLib;

    if (aiModule->state != AI_AQ_STATE_ACTIVE) {
        return 0;
    }

    frameCount = inBuf->frameCount;
    uint32_t src_frame_size = audio_bytes_per_sample(aiModule->source_config.format) * \
                            audio_channel_count_from_out_mask(aiModule->source_config.channel_mask);
    uint32_t src_data_bytes = src_frame_size * frameCount;
    //fromat convert if needed
    if (aiModule->source_config.format != aiModule->target_config.format) {
        uint32_t dest_frame_size = audio_bytes_per_sample(aiModule->target_config.format) * \
                                audio_channel_count_from_out_mask(aiModule->target_config.channel_mask);
        int req_buf_size = dest_frame_size * frameCount;
        if (aiModule->process_buffer_capcity < req_buf_size) {
            void *tmp_addr = aml_audio_realloc(aiModule->process_buffer, req_buf_size);
            aiModule->process_buffer = tmp_addr;
            aiModule->process_buffer_capcity = req_buf_size;
            ALOGD("%s() realloc addr:%p bytes:%d for Format Convert",__func__, tmp_addr, req_buf_size);
        }

        memcpy_by_audio_format(aiModule->process_buffer, aiModule->target_config.format,
                                inBuf->raw, aiModule->source_config.format, frameCount);

        aiModule->process_data_size = req_buf_size;
        aiModule->frameCount = frameCount;
        processed_out_buffer = aiModule->process_buffer;
        processed_out_bytes = aiModule->process_data_size;
    } else {
        processed_out_buffer = inBuf->raw;
        processed_out_bytes = src_data_bytes;
    }

    //channel convert if needed
    int src_channels = audio_channel_count_from_out_mask(aiModule->source_config.channel_mask);
    int dest_channels = audio_channel_count_from_out_mask(aiModule->target_config.channel_mask);
    int dest_frame_size = audio_bytes_per_sample(aiModule->target_config.format) *\
                        audio_channel_count_from_out_mask(aiModule->target_config.channel_mask);
    if (src_channels != dest_channels) {
        uint32_t dest_sample_bytes = audio_bytes_per_sample(aiModule->target_config.format);
        uint32_t req_buf_size = dest_channels * src_data_bytes / src_channels + 16;
        if (aiModule->process_buffer_capcity < req_buf_size) {
            void *tmp_addr = aml_audio_realloc(aiModule->process_buffer, req_buf_size);
            aiModule->process_buffer = tmp_addr;
            aiModule->process_buffer_capcity = req_buf_size;
            ALOGD("%s() realloc addr:%p bytes:%d for Channel Convert",__func__, tmp_addr, req_buf_size);
        }

        aiModule->process_data_size = adjust_channels(processed_out_buffer, src_channels,
                        aiModule->process_buffer, dest_channels,
                        dest_sample_bytes, processed_out_bytes);

        aiModule->frameCount = frameCount;
        processed_out_buffer = aiModule->process_buffer;
        processed_out_bytes = aiModule->process_data_size;
    }

    //re-sample if needed
    if (aiModule->do_resample) {
        ret = aml_audio_resample_process(aiModule->resample, processed_out_buffer, processed_out_bytes);
        if (ret != 0) {
            processed_out_buffer = NULL;
            processed_out_bytes = 0;
            ALOGW("%s aml_audio_resample_process failed!", __func__);
        } else {
            processed_out_buffer = aiModule->resample->resample_buffer;
            processed_out_bytes = aiModule->resample->resample_size;
        }
    }

    if (!processed_out_buffer || processed_out_bytes <= 0) {
        return 0;
    }

    //Debug Tips: shouldn't happen
    if (processed_out_bytes > MAX_BUFFER_LIST_LENGTH * IVA_LIB_REQUIRED_FRAMES * dest_frame_size) {
        ALOGW("%s() Warning! source_size:%d > amount cache buffer size:%d", __func__,
            processed_out_bytes, MAX_BUFFER_LIST_LENGTH * IVA_LIB_REQUIRED_FRAMES * dest_frame_size);
    }

    uint8_t *data = processed_out_buffer;
    int remain = processed_out_bytes;
    int consumed = 0;
    while (remain > 0) {
        pthread_mutex_lock(&aiModule->mutex);
        buffer_info *freeBuffer = get_free_buffer_l(aiModule);
        if (!freeBuffer) {
            ALOGW("%s() Warning! No free buffer, remain:%d", __func__, remain);
            pthread_mutex_unlock(&aiModule->mutex);
            break;
        }

        int copy_bytes = fill_buffer_l(freeBuffer, data + consumed, remain);
        consumed += copy_bytes;
        remain -= copy_bytes;

        if (is_buffer_filled_done_l(freeBuffer)) {
            item_t item;
            item.value = freeBuffer;
            ret = push_queue(&aiModule->bufQueue, item);
            if (ret < 0) {
                ALOGW("%s() Warning! BufferQueue Full, remain_bytes:%d", __func__, remain);
            }
            //if new buffer prepared then wake up iva process
            //TODO: may wake up by interval
            pthread_cond_signal(&aiModule->cond);
        }
        pthread_mutex_unlock(&aiModule->mutex);
    }

#if 0
    //TODO: wake up iva at specified interval
    pthread_mutex_lock(&aiModule->mutex);
    if (!queue_is_empty(&aiModule->bufQueue)) {
        pthread_cond_signal(&aiModule->cond);
        ALOGV("->Wakeup AI process thread");
    }
    pthread_mutex_unlock(&aiModule->mutex);
#endif
    return 0;
}

static void *iva_lib_thread_loop(void *arg)
{
    struct aml_ai_audio_module *aiModule = (struct aml_ai_audio_module *)arg;
    struct ai_audio_libraries_context *aiLib = &aiModule->aiLib;
    struct simple_buffer_queue *bufferQueue = &aiModule->bufQueue;
    int ret = 0;
    ALOGI("+%s() enter", __func__);
    while (true) {
        pthread_mutex_lock(&aiModule->mutex);
        if (aiModule->req_thread_exit) {
            ALOGI("%s() exit!", __func__);
            pthread_mutex_unlock(&aiModule->mutex);
            break;
        }

        int len = queue_length(bufferQueue);
        if (len <= 0) {
            struct timespec ts;
            ts_wait_time_us(&ts, 50 * 1000);
            ALOGV("+%s() enter wait", __func__);
            pthread_cond_timedwait(&aiModule->cond, &aiModule->mutex, &ts);
            ALOGV("-%s() leave wait", __func__);
        }

        item_t item;
        ret = pop_queue(bufferQueue, &item);
        if (ret < 0) {
            pthread_mutex_unlock(&aiModule->mutex);
            continue;
        }
        pthread_mutex_unlock(&aiModule->mutex);

        buffer_info *filledBuffer = item.value;
        if (!is_buffer_filled_done_l(filledBuffer)) {
            ALOGW("%s() Warning, buffer status:%d not BUFFER_IS_BUSY", __func__, filledBuffer->status);
            continue;
        }

        int16_t *start = (int16_t*)filledBuffer->start;
        size_t frames = filledBuffer->bytes / 2;

        neu_iva_scene_class_result_t temp_result;
        ret = aiLib->iva_process(aiLib->iva_handle, start, frames, &temp_result);
        if (temp_result.inference_enable == 1) {
            aiModule->acr_result_available = true;
            clock_gettime(CLOCK_MONOTONIC, &aiModule->flush_time);
            if (is_ai_result_change(&temp_result, &aiModule->aiResult)) {
                aiModule->aiResult = temp_result;
                update_ai_process_result(aiModule, aiModule->aiResult.gradual_score, (int16_t)aiModule->aiResult.gradual_label);
            }
        }

#ifdef ENABLE_ACR_DATA_DUMP
        int enable_dump = aml_getprop_bool("vendor.media.audio_hal.ai.dump");
        if (enable_dump) {
            aml_ai_audio_dump_data("/data/audio/ai_in.pcm", filledBuffer->start, filledBuffer->bytes);
        }
#endif
        pthread_mutex_lock(&aiModule->mutex);
        empty_buffer_l(filledBuffer);
        pthread_mutex_unlock(&aiModule->mutex);
    }
    ALOGI("-%s() exit", __func__);
    return NULL;
}

bool is_ai_result_change(neu_iva_scene_class_result_t *new_val, neu_iva_scene_class_result_t *old_val)
{
    if (new_val->gradual_label != old_val->gradual_label) {
        return true;
    }
    if (fabsf(new_val->gradual_score - old_val->gradual_score) > 0.00001) {
        return true;
    }
    return false;
}

int aml_ai_audio_module_set_mixer_ctrl(void *handle, struct aml_mixer_handle *mixer_ctrl)
{
    struct aml_ai_audio_module *aiModule = (struct aml_ai_audio_module *)handle;
    aiModule->mixer_ctrl = mixer_ctrl;
    return 0;
}

int update_ai_process_result(struct aml_ai_audio_module *module, float score, int32_t label)
{
    if (!module->mixer_ctrl) {
        ALOGD("%s() fail, gradual_score = %.4f, gradual_label = %d", __func__,
                    module->aiResult.gradual_score, module->aiResult.gradual_label);
        return -1;
    }

    int16_t high = FLOAT_TO_INT16(score);
    int16_t low = label;
    int32_t set_value = (int32_t)(high << 16 | (low & 0xffff));
    int ret = aml_mixer_ctrl_set_int(module->mixer_ctrl, AML_MIXER_ID_AI_SOUND_MODE, set_value);
    ALOGI("%s() ret:%d set_value:%d gradual_label:%d gradual_score:%.4f", __func__, ret,
            set_value, (int16_t)(set_value & 0x0000FFFF), INT16_TO_FLOAT((int16_t)(set_value >> 16)));
    return ret;
}

//Get audio content recognition results
int get_aml_ai_process_result(void *handle, float *score, int32_t *label)
{
    struct aml_ai_audio_module *aiModule = (struct aml_ai_audio_module *)handle;
    int ret = 0;
    int set_value = 0;
    if (!handle) {
        *score = 0.0;
        *label = -1;
        ret = -EINVAL;
    }

    if (!aiModule->acr_result_available) {
        *score = 0.0;
        *label = -1;
        ret = -1;
    } else {
        *score = aiModule->aiResult.gradual_score;
        *label = aiModule->aiResult.gradual_label;
    }

    int16_t high = FLOAT_TO_INT16(*score);
    int16_t low = (int16_t)*label;
    set_value = (int32_t)(high << 16 | (low & 0xffff));
    ALOGV("%s() ret:%d set_value:%d gradual_label:%d gradual_score:%.4f", __func__, ret,
            set_value, (int16_t)(set_value & 0x0000FFFF), INT16_TO_FLOAT((int16_t)(set_value >> 16)));
    return set_value;
}

int aml_ai_audio_module_configure(void *handle, uint32_t sample_rate, int channels, audio_format_t format)
{
    struct aml_ai_audio_module *aiModule = (struct aml_ai_audio_module *)handle;
    int ret = 0;

    aiModule->source_config.sample_rate = sample_rate;
    aiModule->source_config.channel_mask = audio_channel_out_mask_from_count(channels);
    aiModule->source_config.format = format;
    //Currently, IVA using this input format data
    aiModule->target_config = DEFAULT_TARGET_CONFIG;

    if (aiModule->source_config.sample_rate != aiModule->target_config.sample_rate) {
        struct audio_resample_config resample_config = {
            .aformat = aiModule->target_config.format,
            .input_sr = aiModule->source_config.sample_rate,
            .output_sr = aiModule->target_config.sample_rate,
            .channels = audio_channel_count_from_out_mask(aiModule->target_config.channel_mask),
        };
        ret = aml_audio_resample_init(&aiModule->resample, AML_AUDIO_SIMPLE_RESAMPLE, &resample_config);
        if (ret != 0) {
            ALOGE("%s() create resample fail, ret=%d\n", __func__, ret);
        }
        aiModule->do_resample = true;
        ALOGD("%s() rsample_config aformat:0x%x in_rate:%d out_rate:%d ch:%x", __func__,
            resample_config.aformat, resample_config.input_sr, resample_config.output_sr, resample_config.channels);
    }

    aiModule->state = AI_AQ_STATE_INITIALIZED;
    return ret;
}

int aml_ai_audio_module_reset(void *handle)
{
    struct aml_ai_audio_module *aiModule = (struct aml_ai_audio_module *)handle;
    if (aiModule->state != AI_AQ_STATE_INITIALIZED || aiModule->state != AI_AQ_STATE_ACTIVE) {
        return -EINVAL;
    }
    //TODO
    return 0;
}

int aml_ai_audio_module_command(void *handle, int32_t cmdCode)
{
    if (!handle) {
        return -EINVAL;
    }

    struct aml_ai_audio_module *aiModule = (struct aml_ai_audio_module *)handle;

    ALOGD("%s: cmd = %u state:%d", __FUNCTION__, cmdCode, aiModule->state);

    switch (cmdCode) {
    case AIAQ_CMD_ENABLE:
        if (aiModule->state == AI_AQ_STATE_ACTIVE) {
            return 0;
        }
        if (aiModule->state != AI_AQ_STATE_INITIALIZED) {
            return -ENOSYS;
        }
        aiModule->state = AI_AQ_STATE_ACTIVE;
        break;
    case AIAQ_CMD_DISABLE:
        if (aiModule->state == AI_AQ_STATE_INITIALIZED) {
            return 0;
        }
        if (aiModule->state != AI_AQ_STATE_ACTIVE) {
            return -ENOSYS;
        }
        aiModule->state = AI_AQ_STATE_INITIALIZED;
        break;
    case AIAQ_CMD_RESET:
        break;
    default:
        ALOGE("%s: invalid command %d", __FUNCTION__, cmdCode);
        return -EINVAL;
    }
    return 0;
}

int aml_ai_audio_module_init(struct aml_ai_audio_module *aiModule)
{
    struct ai_audio_libraries_context *aiLibContext = &aiModule->aiLib;
    int ret;

    ret = iva_libraries_open(aiLibContext, AI_LIB_PATH);
    if (ret < 0) {
        ALOGE("%s() iva_libraries_open fail, return!",__func__);
        return ret;
    }

    ret = iva_class_init(aiLibContext);
    if (ret < 0) {
        ALOGE("%s() iva_libraries_init fail, return!",__func__);
        return ret;
    }

    init_buf_queue(&aiModule->bufQueue, MAX_BUFFER_LIST_LENGTH);

    //pre-allocate cache buffer for pading source data after resample
    int buffer_size =  IVA_LIB_REQUIRED_FRAMES * 2/*2ch,16bit*/;
    for (int i = 0; i< MAX_BUFFER_LIST_LENGTH; i++) {
        buffer_info *temp = &aiModule->bufInfoList[i];
        temp->start = (uint8_t*)aml_audio_malloc(buffer_size);
        temp->bytes = 0;
        temp->status = BUFFER_IS_EMPTY;
        temp->capcity = buffer_size;
    }

    //pthread env init
    pthread_condattr_t condattr;
    pthread_mutex_init(&aiModule->mutex, NULL);
    pthread_condattr_init(&condattr);
    pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
    pthread_cond_init(&aiModule->cond, &condattr);
    pthread_condattr_destroy(&condattr);
    int pthread_ret = pthread_create(&aiModule->iva_thread_id, NULL, iva_lib_thread_loop, aiModule);
    if (pthread_ret != 0) {
        ALOGE("pthread_create fail");
        return -1;
    }
    return ret;
}


int aml_ai_audio_module_deinit(struct aml_ai_audio_module *aiModule)
{
    struct ai_audio_libraries_context *aiLibContext = &aiModule->aiLib;
    if (!aiLibContext->iva_handle) {
        return 0;
    }

    //make pthread exit and clean up
    aiModule->req_thread_exit = true;
    pthread_mutex_lock(&aiModule->mutex);
    pthread_cond_signal(&aiModule->cond);
    pthread_mutex_unlock(&aiModule->mutex);
    //wait iva thread exit
    pthread_join(aiModule->iva_thread_id, NULL);
    pthread_cond_destroy(&aiModule->cond);
    pthread_mutex_destroy(&aiModule->mutex);

    if (aiLibContext->iva_handle) {
        iva_class_deinit(aiLibContext);
    }
    if (aiLibContext->dl_handle) {
        iva_libraries_close(aiLibContext);
    }

    if (aiModule->resample) {
        aml_audio_resample_close(aiModule->resample);
        aiModule->resample = NULL;
    }

    deinit_buf_queue(&aiModule->bufQueue);

    // clear buffer
    for (int i = 0; i< MAX_BUFFER_LIST_LENGTH; i++) {
        buffer_info *bufInfo = &aiModule->bufInfoList[i];
        if (bufInfo->start) {
            free(bufInfo->start);
            bufInfo->start = NULL;
            bufInfo->bytes = 0;
        }
    }
    if (aiModule->process_buffer) {
        free(aiModule->process_buffer);
        aiModule->process_buffer = NULL;
    }
    return 0;
}

int aml_ai_audio_module_create(void **handle)
{
    struct aml_ai_audio_module *aiModule;
    int ret;

    aiModule = (struct aml_ai_audio_module *)aml_audio_calloc(1, sizeof(struct aml_ai_audio_module));
    if (!aiModule) {
        ALOGE("%s malloc error", __func__);
        return -ENOMEM;
    }

    ret = aml_ai_audio_module_init(aiModule);
    *handle = aiModule;
    aiModule->state = AI_AQ_STATE_UNINITIALIZED;
    return ret;
}

int aml_ai_audio_module_release(void *handle)
{
    if (!handle) {
        return -EINVAL;
    }

    struct aml_ai_audio_module *aiModule = (struct aml_ai_audio_module *)handle;
    int ret;
    ret = aml_ai_audio_module_deinit(aiModule);
    free(handle);
    return ret;
}

//############ IVA AI library APIs #############//
static int iva_libraries_open(struct ai_audio_libraries_context *aiLibContext, const char *lib_path)
{
    void* dl_handle = dlopen(lib_path, RTLD_NOW);
    if (!dl_handle) {
        ALOGE("%s() dlopen() %s error:%s", __func__, lib_path, dlerror());
        return -EINVAL;
    }

    aiLibContext->iva_init = dlsym(dl_handle, "neu_iva_scene_init");
    if (!aiLibContext->iva_init) {
        ALOGE("%s() dlsym() neu_iva_scene_init error:%d", __func__, errno);
        goto exit;
    }

    aiLibContext->iva_process = dlsym(dl_handle, "neu_iva_scene_class_process");
    if (!aiLibContext->iva_process) {
        ALOGE("%s() dlsym() neu_iva_scene_class_process error:%d", __func__, errno);
        goto exit;
    }

    aiLibContext->iva_deinit = dlsym(dl_handle, "neu_iva_scene_class_deinit");
    if (!aiLibContext->iva_deinit) {
        ALOGE("%s() dlsym() neu_iva_scene_class_deinit error:%d", __func__, errno);
        goto exit;
    }

    aiLibContext->iva_set_param = dlsym(dl_handle, "neu_iva_scene_class_param_set");
    if (!aiLibContext->iva_set_param) {
        ALOGE("%s() dlsym() neu_iva_scene_class_param_set error:%d", __func__, errno);
        goto exit;
    }

    aiLibContext->iva_get_param = dlsym(dl_handle, "neu_iva_scene_class_param_get");
    if (!aiLibContext->iva_get_param) {
        ALOGE("%s() dlsym() neu_iva_scene_class_param_get error:%d", __func__, errno);
        goto exit;
    }

    aiLibContext->iva_reset = dlsym(dl_handle, "neu_iva_scene_class_reset");
    if (!aiLibContext->iva_reset) {
        ALOGE("%s() dlsym() neu_iva_scene_class_reset error:%d", __func__, errno);
        goto exit;
    }

    aiLibContext->dl_handle = dl_handle;
    ALOGI("%s() parse iva library symbol SUCCESS!", __func__);
    return 0;
exit:
    aiLibContext->dl_handle = NULL;
    dlclose(dl_handle);
    return -EINVAL;
}

static int iva_class_init(struct ai_audio_libraries_context *aiLibContext)
{
    neu_iva_scene_class_handle_t iva_scene_handle;
    iva_scene_handle = aiLibContext->iva_init(NEU_IVA_SCENE_CLASS_TYPE, NULL);
    if (iva_scene_handle == NULL) {
        ALOGE("%s() The iva scene init fail !",__func__);
        return -1;
    }

    aiLibContext->iva_handle = iva_scene_handle;
    return 0;
}

static int iva_class_deinit(struct ai_audio_libraries_context *aiLibContext)
{
    if (!aiLibContext->iva_handle) {
        return -EINVAL;
    }
    aiLibContext->iva_deinit(aiLibContext->iva_handle, NEU_IVA_SCENE_CLASS_TYPE);
    return 0;
}

static int iva_libraries_close(struct ai_audio_libraries_context *aiLibContext)
{
    if (!aiLibContext->dl_handle) {
        dlclose(aiLibContext->dl_handle);
        aiLibContext->dl_handle = NULL;
    }
    return 0;
}

int aml_ai_audio_dump_data(const char *path, const void *buf, size_t bytes)
{
    if (!path) {
        return -1;
    }

    FILE *fp = fopen(path, "a+");
    if (fp) {
        int flen = fwrite((char *)buf, 1, bytes, fp);
        fclose(fp);
        return 0;
    }
    ALOGE("fail to open path=%s, errno=%s",  path, strerror(errno));
    return -1;
}

//############ buffer control APIs #############//
/*
Function:
    Get one empty buffer from cach buffer list:
Return:
    1st pri:
        get the buffer that was partially filled with data
    2nd pri:
        got whole empty buffer obj
*/
buffer_info *get_free_buffer_l(struct aml_ai_audio_module *aiModule)
{
    buffer_info *buf_list = aiModule->bufInfoList;
    int pending_index = -1;
    int empty_index = -1;
    for (int i = 0; i < MAX_BUFFER_LIST_LENGTH; i++) {
        int status = buf_list[i].status;
        if (status == BUFFER_IS_PENDING) {
            pending_index = i;
        } else if (status == BUFFER_IS_EMPTY) {
            empty_index = i;
        }
    }
    if (pending_index >= 0) {
        return &buf_list[pending_index];
    } else if (empty_index >= 0) {
        return &buf_list[empty_index];
    }
    return NULL;
}

/*
Function: fill one buffer util data size = .capcity
return:
    bytes copied from source buffer (data)
*/
size_t fill_buffer_l(buffer_info *buffer, uint8_t *data, size_t bytes)
{
    int request_bytes = buffer->capcity - buffer->bytes;
    if (request_bytes <= 0 || buffer->status == BUFFER_IS_BUSY || bytes == 0) {
        ALOGE("%s() Error! capcity:%zu pading:%zu request:%d in_bytes:%zu status:%d",__func__,
            buffer->capcity, buffer->bytes, request_bytes, bytes, buffer->status);
        return bytes;
    }

    int copy_bytes = (request_bytes <= bytes ? request_bytes : bytes);
    memcpy(buffer->start + buffer->bytes, data, copy_bytes);
    buffer->bytes += copy_bytes;
    if (buffer->bytes >= buffer->capcity) {
        buffer->status = BUFFER_IS_BUSY;
    } else if(buffer->bytes > 0) {
        buffer->status = BUFFER_IS_PENDING;
    } else {
        buffer->status = BUFFER_IS_EMPTY;
    }
    return copy_bytes;
}

void empty_buffer_l(buffer_info *buffer)
{
    buffer->bytes = 0;
    buffer->status = BUFFER_IS_EMPTY;
}

bool is_buffer_filled_done_l(const buffer_info *buffer)
{
    return (buffer->status == BUFFER_IS_BUSY ? true : false);
}

//############ simple buffer queue control APIs #############//
int init_buf_queue(struct simple_buffer_queue* queue, int length)
{
    if (length == 0) {
        ALOGW("%s() Invalid len:%d", __func__, length);
        return -EINVAL;
    }

    int table_size = (length + 1) * sizeof(item_t);
    queue->table =(item_t*) malloc(table_size);
    queue->size = length + 1;
    queue->w = queue->r = 0;
    return 0;
}

void deinit_buf_queue(struct simple_buffer_queue* queue)
{
    if (!queue->table) {
        free(queue->table);
        queue->table = NULL;
    }
}

void queue_reset(struct simple_buffer_queue* queue)
{
    queue->w = queue->r = 0;
}

int queue_length(struct simple_buffer_queue* queue)
{
    int item_count = 0;
    if (queue->w >= queue->r) {
        item_count = queue->w - queue->r;
    } else {
        item_count =  queue->size + queue->w - queue->r;
    }
    return item_count;
}

bool queue_is_full(struct simple_buffer_queue* queue)
{
    int empty_size = queue->size - queue_length(queue) - 1;
    return (empty_size > 0 ? false : true);
}

bool queue_is_empty(struct simple_buffer_queue* queue)
{
    return (queue->w == queue->r ? true : false);
}

int push_queue(struct simple_buffer_queue* queue, item_t item)
{
    if (queue_is_full(queue)) {
        return -1;
    }

    queue->table[queue->w] = item;
    if (++queue->w >= queue->size) {
        queue->w = 0;
    }
    return 0;
}

int pop_queue(struct simple_buffer_queue* queue, item_t* item)
{
    if (queue_is_empty(queue)) {
        return -1;
    }

    *item = queue->table[queue->r];
    if (++queue->r >= queue->size) {
        queue->r = 0;
    }
    return 0;
}

/*
Function: add AI module handle to post processor
*/
int aml_add_ai_audio_handle(struct aml_native_postprocess *native_postprocess, void *handle)
{
    if (handle == NULL) {
        return -EINVAL;
    }

    native_postprocess->ai_handle = handle;
    return 0;
}

/*
Function: remove AI module handle from post processor
*/
int aml_remove_ai_audio_handle(struct aml_native_postprocess *native_postprocess, void *handle)
{
    if (handle == NULL) {
        return -EINVAL;
    }

    native_postprocess->ai_handle = NULL;
    return 0;
}

int aml_open_ai_audio_module(struct aml_native_postprocess *native_postprocess, struct aml_mixer_handle* alsa_mixer)
{
    void *ai_handle;
    int ret;
    ALOGD("%s()",__func__);
    ret = aml_ai_audio_module_create(&ai_handle);
    if (ret < 0) {
        ALOGE("%s() create ai fail", __func__);
        goto exit;
    }

    aml_ai_audio_module_set_mixer_ctrl(ai_handle, alsa_mixer);

    ret = aml_ai_audio_module_configure(ai_handle, 48000, 2/*Channel*/, AUDIO_FORMAT_PCM_16_BIT);
    if (ret < 0) {
        ALOGE("%s() configure ai fail", __func__);
        goto exit;
    }

    ret = aml_add_ai_audio_handle(native_postprocess, ai_handle);
    if (ret < 0) {
        ALOGE("%s() add ai handle fail", __func__);
        goto exit;
    }

    ret = aml_ai_audio_module_command(ai_handle, AIAQ_CMD_ENABLE);
    return ret;

exit:
    aml_ai_audio_module_release(ai_handle);
    return ret;
}

int aml_close_ai_audio_module(struct aml_native_postprocess *native_postprocess)
{
    void *ai_handle = native_postprocess->ai_handle;
    if (ai_handle) {
        aml_ai_audio_module_command(ai_handle, AIAQ_CMD_DISABLE);
        aml_remove_ai_audio_handle(native_postprocess, ai_handle);
        aml_ai_audio_module_release(ai_handle);
        native_postprocess->ai_handle = NULL;
    }
    return 0;
}

