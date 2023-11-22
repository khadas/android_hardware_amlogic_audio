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

#define LOG_TAG "audio_hw_decoder_mpegh"
//#define LOG_NDEBUG 0

#include <cutils/log.h>
#include "aml_dec_api.h"
#include "audio_data_process.h"
#include "aml_malloc_debug.h"
#include "assert.h"

#define MPEGH_MAX_LENGTH (1024 * 64 * 2)

struct mpegh_dec_t {
    aml_dec_t  aml_dec;
    aml_mpegh_config_t mpegh_config;
    int rate_factor;
    int endian;
};

static int mpegh_decoder_init(aml_dec_t **ppaml_dec, aml_dec_config_t * dec_config)
{
    struct mpegh_dec_t *mpegh_dec;
    aml_dec_t  *aml_dec = NULL;
    aml_mpegh_config_t *mpegh_config = NULL;
    dec_data_info_t * dec_pcm_data = NULL;
    dec_data_info_t * dec_raw_data = NULL;
    dec_data_info_t * raw_in_data  = NULL;
    dec_data_info_t * ad_dec_pcm_data = NULL;
    int bitrate = 0;
    ALOGI("mpegh_decoder_init\n");

    mpegh_dec = aml_audio_calloc(1, sizeof(struct mpegh_dec_t));
    if (mpegh_dec == NULL) {
        ALOGE("%s calloc mpegh_dec error", __func__);
        goto error;
    }
    aml_dec = &mpegh_dec->aml_dec;

    bitrate = dec_config->mpegh_config.channel * dec_config->mpegh_config.samplerate * 16;
    mpegh_dec->rate_factor = bitrate > 6144000 ? 16 : 4;
    mpegh_dec->endian = 1;

    if (!dec_config->mpegh_config.is_iec61937) {
        dec_config->mpegh_config.channel = mpegh_dec->rate_factor == 16 ? 8 : 2;
        dec_config->mpegh_config.samplerate = 192000;
    }

    dec_pcm_data = &aml_dec->dec_pcm_data;
    dec_pcm_data->buf_size = MPEGH_MAX_LENGTH;
    dec_pcm_data->data_ch = dec_config->mpegh_config.channel;
    dec_pcm_data->data_sr = dec_config->mpegh_config.samplerate;
    dec_pcm_data->buf = (unsigned char*) aml_audio_calloc(1, dec_pcm_data->buf_size);
    if (dec_pcm_data->buf == NULL) {
        ALOGE("%s calloc pcm buffer error", __func__);
        goto error;
    }
    dec_pcm_data->data_format = AUDIO_FORMAT_PCM_16_BIT;

    dec_raw_data = &aml_dec->dec_raw_data;
    dec_raw_data->buf_size = MPEGH_MAX_LENGTH;
    dec_raw_data->data_ch = dec_config->mpegh_config.channel;
    dec_raw_data->data_sr = dec_config->mpegh_config.samplerate;
    dec_raw_data->data_format = AUDIO_FORMAT_IEC61937;
    dec_raw_data->sub_format = dec_config->mpegh_config.format;
    dec_raw_data->buf = (unsigned char*) aml_audio_calloc(1, dec_raw_data->buf_size);
    if (dec_raw_data->buf == NULL) {
        ALOGE("%s calloc dec raw buffer error", __func__);
        goto error;
    }

    raw_in_data = &aml_dec->raw_in_data;
    raw_in_data->buf_size = MPEGH_MAX_LENGTH;
    raw_in_data->data_ch = dec_config->mpegh_config.channel;
    raw_in_data->data_sr = dec_config->mpegh_config.samplerate;
    raw_in_data->data_format = AUDIO_FORMAT_IEC61937;
    raw_in_data->sub_format = dec_config->mpegh_config.format;
    raw_in_data->buf = (unsigned char*) aml_audio_calloc(1, raw_in_data->buf_size);
    if (raw_in_data->buf == NULL) {
        ALOGE("%s calloc raw in buffer error", __func__);
        goto error;
    }

    memcpy(&mpegh_dec->mpegh_config, &dec_config->mpegh_config, sizeof(aml_mpegh_config_t));

    *ppaml_dec = (aml_dec_t *)mpegh_dec;

    ALOGI("mpegh_decoder_init end\n");
    return 0;

error:
    if (mpegh_dec) {
        if (dec_pcm_data && dec_pcm_data->buf) {
            aml_audio_free(dec_pcm_data->buf);
        }
        if (dec_raw_data && dec_raw_data->buf) {
            aml_audio_free(dec_raw_data->buf);
        }
        if (raw_in_data && raw_in_data->buf) {
            aml_audio_free(raw_in_data->buf);
        }
        aml_audio_free(mpegh_dec);
    }
    *ppaml_dec = NULL;
    ALOGE("%s error", __func__);
    return -1;
}

static int mpegh_decoder_release(aml_dec_t * aml_dec)
{
    if (aml_dec) {
        struct mpegh_dec_t *mpegh_dec = (struct mpegh_dec_t *)aml_dec;
        ALOGI("mpegh_decoder_release\n");

        if (aml_dec->dec_pcm_data.buf) {
            aml_audio_free(aml_dec->dec_pcm_data.buf);
        }

        if (aml_dec->dec_raw_data.buf) {
            aml_audio_free(aml_dec->dec_raw_data.buf);
        }

        if (aml_dec->raw_in_data.buf) {
            aml_audio_free(aml_dec->raw_in_data.buf);
        }
        aml_audio_free(mpegh_dec);
        aml_dec = NULL;
    }
    return 0;
}

static int mpegh_decoder_process(aml_dec_t* aml_dec, unsigned char* buffer, int bytes)
{
    if (aml_dec == NULL) {
        ALOGE("%s aml_dec is NULL", __func__);
        return -1;
    }

    struct mpegh_dec_t *mpegh_dec = (struct mpegh_dec_t *)aml_dec;

    dec_data_info_t * dec_pcm_data = &aml_dec->dec_pcm_data;
    dec_data_info_t * dec_raw_data = &aml_dec->dec_raw_data;
    dec_data_info_t * raw_in_data  = &aml_dec->raw_in_data;

    dec_pcm_data->data_len = 0;
    dec_raw_data->data_len = 0;
    raw_in_data->data_len = 0;

    if (mpegh_dec->mpegh_config.is_iec61937) {
        dec_pcm_data->data_len = bytes;
        memset(dec_pcm_data->buf, 0, sizeof(char)*bytes);

        dec_raw_data->data_len = bytes;
        memcpy(dec_raw_data->buf, buffer, sizeof(char)*bytes);

        raw_in_data->data_len = bytes;
        memcpy(raw_in_data->buf, buffer, sizeof(char)*bytes);
    } else {
        ALOGE("mpegh es not support! \n");
    }

    ALOGV("mpegh_decoder_process \n");
    return bytes;
}

static int mpegh_decoder_getinfo(aml_dec_t *aml_dec __unused, aml_dec_info_type_t info_type __unused, aml_dec_info_t * dec_info __unused)
{
    int ret = 0;
    ALOGV("mpegh_decoder_getinfo \n");
    return ret;
}

int mpegh_decoder_config(aml_dec_t * aml_dec __unused, aml_dec_config_type_t config_type __unused, aml_dec_config_t * dec_config __unused)
{
    int ret = 0;
    ALOGV("mpegh_decoder_config \n");
    return ret;
}

aml_dec_func_t aml_mpegh_func = {
    .f_init                 = mpegh_decoder_init,
    .f_release              = mpegh_decoder_release,
    .f_process              = mpegh_decoder_process,
    .f_config               = mpegh_decoder_config,
    .f_info                 = mpegh_decoder_getinfo,
};

