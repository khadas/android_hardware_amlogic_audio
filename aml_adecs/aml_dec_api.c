/*
 * Copyright (C) 2021 Amlogic Corporation.
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

#define LOG_TAG "aml_dec_api"

#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <cutils/log.h>

#include "aml_dec_api.h"
#include "aml_ddp_dec_api.h"
#include "aml_dts_dec_api.h"
#include "aml_pcm_dec_api.h"
#include "aml_mpeg_dec_api.h"
#include "aml_aac_dec_api.h"
#include "aml_dra_dec_api.h"
#include "aml_dump_debug.h"
#include "aml_audio_report.h"
#include "aml_audio_sysfs.h"



#define AML_DEC_FRAGMENT_FRAMES     (512)
#define AML_DEC_MAX_FRAMES          (AML_DEC_FRAGMENT_FRAMES * 4)

static aml_dec_func_t * get_decoder_function(audio_format_t format)
{
    switch (format) {
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3: {
        return &aml_dcv_func;
    }
    case AUDIO_FORMAT_DOLBY_TRUEHD:
    case AUDIO_FORMAT_MAT:
        return NULL;
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_HD: {
        return &aml_dca_func;
    }
    case AUDIO_FORMAT_PCM_16_BIT:
    case AUDIO_FORMAT_PCM_32_BIT:
    case AUDIO_FORMAT_PCM_8_BIT:
    case AUDIO_FORMAT_PCM_8_24_BIT: {
        return &aml_pcm_func;
    }
    case AUDIO_FORMAT_MP3:
    case AUDIO_FORMAT_MP2: {
       return  &aml_mad_func;
    }
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_AAC_LATM: {
       return  &aml_faad_func;
    }
    default:
        if (format == AUDIO_FORMAT_DRA) {
            return  &aml_dra_func;
        }
        ALOGE("[%s:%d] doesn't support decoder format:%#x", __func__, __LINE__, format);
        return NULL;
    }

    return NULL;
}

int aml_decoder_init(aml_dec_t **ppaml_dec, audio_format_t format, aml_dec_config_t * dec_config)
{
    int ret = -1;
    aml_dec_func_t *dec_fun = NULL;
    dec_fun = get_decoder_function(format);
    aml_dec_t *aml_dec_handle = NULL;
    if (dec_fun == NULL) {
        ALOGE("%s got dec_fun as NULL!\n", __func__);
        return -1;
    }

    ALOGD("[%s:%d] dec_fun->f_init=%p, format:%#x", __func__, __LINE__, dec_fun->f_init, format);
    if (dec_fun->f_init) {
        ret = dec_fun->f_init(ppaml_dec, dec_config);
        if (ret < 0) {
            return -1;
        }
    } else {
        return -1;
    }

    aml_dec_handle = *ppaml_dec;
    aml_dec_handle->frame_cnt = 0;
    aml_dec_handle->format = format;
    aml_dec_handle->fragment_left_size = 0;
    aml_dec_handle->in_frame_pts = 0;

    if (get_debug_value(AML_DEBUG_AUDIOHAL_SYNCPTS)) {
        aml_dec_handle->debug_synced_frame_pts_flag = true;
    } else {
        aml_dec_handle->debug_synced_frame_pts_flag = false;
    }

    return ret;

ERROR:
    if (dec_fun->f_release && aml_dec_handle) {
        dec_fun->f_release(aml_dec_handle);
    }

    return -1;

}
int aml_decoder_release(aml_dec_t *aml_dec)
{
    int ret = -1;
    aml_dec_func_t *dec_fun = NULL;
    if (aml_dec == NULL) {
        ALOGE("%s aml_dec is NULL\n", __func__);
        return -1;
    }

    dec_fun = get_decoder_function(aml_dec->format);
    if (dec_fun == NULL) {
        return -1;
    }

    if (dec_fun->f_release) {
        dec_fun->f_release(aml_dec);
    } else {
        return -1;
    }

    return ret;


}
int aml_decoder_set_config(aml_dec_t *aml_dec, aml_dec_config_type_t config_type, aml_dec_config_t * dec_config)
{
    int ret = -1;
    aml_dec_func_t *dec_fun = NULL;
    if (aml_dec == NULL) {
        ALOGE("%s aml_dec is NULL\n", __func__);
        return -1;
    }
    dec_fun = get_decoder_function(aml_dec->format);
    if (dec_fun == NULL) {
        return -1;
    }

    if (dec_fun->f_config) {
        ret = dec_fun->f_config(aml_dec, config_type, dec_config);
    }

    return ret;
}

int aml_decoder_get_info(aml_dec_t *aml_dec, aml_dec_info_type_t info_type, aml_dec_info_t * dec_info)
{
    int ret = -1;
    aml_dec_func_t *dec_fun = NULL;
    if (aml_dec == NULL) {
        ALOGE("%s aml_dec is NULL\n", __func__);
        return -1;
    }
    dec_fun = get_decoder_function(aml_dec->format);
    if (dec_fun == NULL) {
        return -1;
    }

    if (dec_fun->f_info) {
        ret = dec_fun->f_info(aml_dec, info_type, dec_info);
    }

    return ret;
}

void get_audio_decoder_info (aml_dec_info_t dec_info, aml_dec_t *aml_dec) {

    char sysfs_buf[MAX_BUFF_LEN] = {0};
    memset(sysfs_buf, 0x00, MAX_BUFF_LEN);
    aml_decoder_get_info(aml_dec, AML_DEC_STREAM_INFO, &dec_info);
    sprintf(sysfs_buf, "bitrate %d", dec_info.dec_info.stream_bitrate);
    sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
    memset(sysfs_buf, 0x00, MAX_BUFF_LEN);
    sprintf(sysfs_buf, "ch_num %d", dec_info.dec_info.stream_ch);
    sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
    memset(sysfs_buf, 0x00, MAX_BUFF_LEN);
    sprintf(sysfs_buf, "samplerate %d", dec_info.dec_info.stream_sr);
    sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
    memset(sysfs_buf, 0x00, MAX_BUFF_LEN);
    sprintf(sysfs_buf, "decoded_frames %d", dec_info.dec_info.stream_decode_num);
    sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
    memset(sysfs_buf, 0x00, MAX_BUFF_LEN);
    sprintf(sysfs_buf, "decoded_err %d", dec_info.dec_info.stream_error_num);
    sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
    memset(sysfs_buf, 0x00, MAX_BUFF_LEN);
    sprintf(sysfs_buf, "decoded_drop %d", dec_info.dec_info.stream_drop_num);
    sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
    memset(sysfs_buf, 0x00, MAX_BUFF_LEN);
    UpdateDecodeInfo_ChannelConfiguration(sysfs_buf,dec_info.dec_info.stream_ch);
}
int aml_decoder_process(aml_dec_t *aml_dec, unsigned char*buffer, int bytes, int *used_bytes)
{
    int ret = -1;
    aml_dec_func_t *dec_fun = NULL;
    int fill_bytes = 0;
    int parser_raw = 0;
    int offset = 0;
    int n_bytes_spdifdec_consumed = 0;
    void *payload_addr = NULL;
    int32_t n_bytes_payload = 0;
    unsigned char *spdif_src = NULL;
    int spdif_offset = 0;
    int frame_size = 0;
    int fragment_size = 0;
    dec_data_info_t * dec_pcm_data = &aml_dec->dec_pcm_data;
    dec_data_info_t * dec_raw_data = &aml_dec->dec_raw_data;
    dec_data_info_t * raw_in_data  = &aml_dec->raw_in_data;

    *used_bytes = 0;
    if (aml_dec == NULL) {
        ALOGE("[%s:%d] aml_dec is null", __func__, __LINE__);
        return -1;
    }

    dec_fun = get_decoder_function(aml_dec->format);
    if (dec_fun == NULL) {
        ALOGW("[%s:%d] get_decoder_function format:%#x is null", __func__, __LINE__, aml_dec->format);
        return -1;
    }
    /*if we have fragment size output*/
    if (aml_dec->fragment_left_size > 0) {
        ALOGV("[%s:%d] fragment_left_size=%d ", __func__, __LINE__, aml_dec->fragment_left_size);
        frame_size = audio_bytes_per_sample(dec_pcm_data->data_format) * dec_pcm_data->data_ch;
        fragment_size = AML_DEC_FRAGMENT_FRAMES * frame_size;
        memmove(dec_pcm_data->buf, (unsigned char *)dec_pcm_data->buf + fragment_size, aml_dec->fragment_left_size);
        memmove(dec_raw_data->buf, (unsigned char *)dec_raw_data->buf + fragment_size, aml_dec->fragment_left_size);

        if (aml_dec->fragment_left_size >= fragment_size) {
            dec_pcm_data->data_len = fragment_size;
            dec_raw_data->data_len = fragment_size;
            aml_dec->fragment_left_size -= fragment_size;
        } else {
            dec_pcm_data->data_len = aml_dec->fragment_left_size;
            dec_raw_data->data_len = aml_dec->fragment_left_size;
            aml_dec->fragment_left_size = 0;
        }
        *used_bytes = 0;
        return 0;
    }

    dec_pcm_data->data_len = 0;
    dec_raw_data->data_len = 0;
    raw_in_data->data_len = 0;

    if (dec_fun->f_process) {
        ret = dec_fun->f_process(aml_dec, buffer, bytes);
    } else {
        ALOGE("[%s:%d] f_process is null", __func__, __LINE__);
        return -1;
    }
    if (get_audio_info_enable(DUMP_AUDIO_INFO_DECODE)) {
        aml_dec_info_t dec_info = {0};
        get_audio_decoder_info(dec_info, aml_dec);
        frame_size = audio_bytes_per_sample(dec_pcm_data->data_format) * dec_pcm_data->data_ch;
        /*one decoded frame length is too big, we need separate it*/
        if ((dec_pcm_data->data_len >= AML_DEC_MAX_FRAMES * frame_size) &&
            (dec_raw_data->data_format == AUDIO_FORMAT_IEC61937) &&
            (dec_raw_data->data_len == dec_pcm_data->data_len)) {
            fragment_size = AML_DEC_FRAGMENT_FRAMES * frame_size;
            aml_dec->fragment_left_size = dec_pcm_data->data_len - fragment_size;
            dec_pcm_data->data_len = fragment_size;
            dec_raw_data->data_len = fragment_size;
        }
    }

    if (ret >= 0 ) {
      *used_bytes = ret;
       return AML_DEC_RETURN_TYPE_OK;
    } else {
       *used_bytes = bytes;
       return ret;
    }

}

