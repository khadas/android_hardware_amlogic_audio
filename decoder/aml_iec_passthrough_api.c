/*
 * Copyright (C) 2022 Amlogic Corporation.
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

#define LOG_TAG "audio_hw_decoder_iecpassthrough"
//#define LOG_NDEBUG 0

#include <cutils/log.h>
#include "aml_dec_api.h"
#include "audio_data_process.h"
#include "aml_malloc_debug.h"

#define IEC_MAX_LENGTH (1024 * 64 * 2)

static int iec_passthrough_init(aml_dec_t **ppaml_dec, aml_dec_config_t * dec_config)
{
    aml_dec_t  *aml_dec = NULL;
    dec_data_info_t * dec_pcm_data = NULL;
    dec_data_info_t * dec_raw_data = NULL;
    dec_data_info_t * raw_in_data  = NULL;
    dec_data_info_t * ad_dec_pcm_data = NULL;

    ALOGI("iec_passthrough_init\n");
    aml_dec = aml_audio_calloc(1, sizeof(aml_dec_t));
    if (aml_dec == NULL) {
        ALOGE("%s calloc aml_dec error", __func__);
        goto error;
    }

    dec_pcm_data = &aml_dec->dec_pcm_data;
    dec_pcm_data->buf_size = IEC_MAX_LENGTH;
    dec_pcm_data->data_ch = dec_config->iec_config.channel;
    dec_pcm_data->data_sr = dec_config->iec_config.samplerate;
    dec_pcm_data->buf = (unsigned char*) aml_audio_calloc(1, dec_pcm_data->buf_size);
    if (dec_pcm_data->buf == NULL) {
        ALOGE("%s calloc pcm buffer error", __func__);
        goto error;
    }

    dec_pcm_data->data_format = AUDIO_FORMAT_PCM_16_BIT;
    dec_raw_data = &aml_dec->dec_raw_data;
    dec_raw_data->buf_size = IEC_MAX_LENGTH;
    dec_raw_data->data_ch = dec_config->iec_config.channel;
    dec_raw_data->data_sr = dec_config->iec_config.samplerate;
    dec_raw_data->data_format = AUDIO_FORMAT_IEC61937;
    dec_raw_data->sub_format = dec_config->iec_config.format;
    dec_raw_data->buf = (unsigned char*) aml_audio_calloc(1, dec_raw_data->buf_size);
    if (dec_raw_data->buf == NULL) {
        ALOGE("%s calloc dec raw buffer error", __func__);
        goto error;
    }

    raw_in_data  = &aml_dec->raw_in_data;
    raw_in_data->buf_size = IEC_MAX_LENGTH;
    raw_in_data->data_ch = dec_config->iec_config.channel;
    raw_in_data->data_sr = dec_config->iec_config.samplerate;
    raw_in_data->data_format = AUDIO_FORMAT_IEC61937;
    raw_in_data->sub_format = dec_config->iec_config.format;
    raw_in_data->buf = (unsigned char*) aml_audio_calloc(1, raw_in_data->buf_size);
    if (raw_in_data->buf == NULL) {
        ALOGE("%s calloc raw in buffer error", __func__);
        goto error;
    }

    if (dec_config->iec_config.is_iec61937) {
        aml_dec->format = AUDIO_FORMAT_IEC61937;
    }
    *ppaml_dec = (aml_dec_t *)aml_dec;
    return 0;

error:
    if (aml_dec) {
        if (dec_pcm_data && dec_pcm_data->buf) {
            aml_audio_free(dec_pcm_data->buf);
        }
        if (dec_raw_data && dec_raw_data->buf) {
            aml_audio_free(dec_raw_data->buf);
        }
        if (raw_in_data && raw_in_data->buf) {
            aml_audio_free(raw_in_data->buf);
        }
        aml_audio_free(aml_dec);
    }
    *ppaml_dec = NULL;
    ALOGE("%s error", __func__);
    return -1;
}

static int iec_passthrough_release(aml_dec_t * aml_dec)
{
    if (aml_dec) {
        if (aml_dec->dec_pcm_data.buf) {
            aml_audio_free(aml_dec->dec_pcm_data.buf);
        }

        if (aml_dec->dec_raw_data.buf) {
            aml_audio_free(aml_dec->dec_raw_data.buf);
        }

        if (aml_dec->raw_in_data.buf) {
            aml_audio_free(aml_dec->raw_in_data.buf);
        }
        aml_audio_free(aml_dec);
        aml_dec = NULL;
    }
    return 0;
}

static int iec_passthrough_process(aml_dec_t *aml_dec, unsigned char *buffer, int bytes)
{
    if (aml_dec == NULL) {
        ALOGE("%s aml_dec is NULL", __func__);
        return -1;
    }

    dec_data_info_t * dec_pcm_data = &aml_dec->dec_pcm_data;
    dec_data_info_t * dec_raw_data = &aml_dec->dec_raw_data;
    dec_data_info_t * raw_in_data  = &aml_dec->raw_in_data;

    dec_pcm_data->data_len = 0;
    dec_raw_data->data_len = 0;
    raw_in_data->data_len = 0;

    dec_pcm_data->data_len = bytes;
    memset(dec_pcm_data->buf, 0, sizeof(char)*dec_pcm_data->data_len);

    dec_raw_data->data_len = bytes;
    memcpy(dec_raw_data->buf, buffer, sizeof(char)*bytes);

    raw_in_data->data_len = bytes;
    memcpy(raw_in_data->buf, buffer, sizeof(char)*bytes);
    ALOGV("iec_passthrough_process \n");
    return bytes;
}

static int iec_passthrough_getinfo(aml_dec_t *aml_dec __unused, aml_dec_info_type_t info_type __unused, aml_dec_info_t * dec_info __unused)
{
    int ret = 0;
    ALOGV("iec_passthrough_getinfo \n");
    return ret;
}

int iec_passthrough_config(aml_dec_t * aml_dec __unused, aml_dec_config_type_t config_type __unused, aml_dec_config_t * dec_config __unused)
{
    int ret = 0;
    ALOGV("iec_passthrough_config \n");
    return ret;
}

aml_dec_func_t aml_iec_func = {
    .f_init                 = iec_passthrough_init,
    .f_release              = iec_passthrough_release,
    .f_process              = iec_passthrough_process,
    .f_config               = iec_passthrough_config,
    .f_info                 = iec_passthrough_getinfo,
};
