/**************************************************************************
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
 **************************************************************************/

/**************************************************************************
 *
 *  Filename:      a2dp_hal.c
 *
 *  Description:   Implements hal for bluedroid a2dp audio device
 *
 **************************************************************************/

#define LOG_TAG "a2dp_hal"

#include <audio_utils/format.h>
#include <audio_utils/primitives.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <system/audio.h>

#include "a2dp_hal.h"
#include "a2dp_hw.h"
#include "aml_android_utils.h"
#include "aml_audio_timer.h"
#include "audio_hw_utils.h"
#include "audio_virtual_buf.h"

#define DEBUG_LOG_MASK_A2DP                         (0x1000)
#define VIR_BUFF_NS                                 (80 * NSEC_PER_MSEC)

static uint64_t total_input_ns = 0;

static size_t a2dp_out_get_buffer_size(const struct audio_stream* stream) {
    struct aml_stream_out* aml_out = (struct aml_stream_out*)stream;
    struct aml_audio_device *adev = aml_out->dev;
    aml_a2dp_hal *a2dp_hal = adev->a2dp_hal;
    // the AudioFlinger mixer buffer size.
    return a2dp_hal->buffer_sz / AUDIO_STREAM_OUTPUT_BUFFER_PERIODS;
}

static int a2dp_out_set_parameters(struct audio_stream *stream, const char* kvpairs) {
    struct aml_stream_out* aml_out = (struct aml_stream_out*)stream;
    struct aml_audio_device *adev = aml_out->dev;
    aml_a2dp_hal *a2dp_hal = adev->a2dp_hal;
    struct str_parms *parms;
    char value[32];
    int ret;

    AM_LOGI("a2dp_out_set_parameters %s", kvpairs);
    parms = str_parms_create_str (kvpairs);
    /*ret = str_parms_get_str (parms, "closing", value, sizeof (value) );
    if (ret >= 0) {
        pthread_mutex_lock(&a2dp_hal->mutex);
        if (strncmp(value, "true", 4) == 0)
            a2dp_hal->state = AUDIO_A2DP_STATE_STOPPING;
        pthread_mutex_unlock(&a2dp_hal->mutex);
    }*/
    ret = str_parms_get_str (parms, "A2dpSuspended", value, sizeof (value) );
    if (ret >= 0) {
        pthread_mutex_lock(&a2dp_hal->mutex);
        if (strncmp(value, "true", 4) == 0) {
            if (a2dp_hal->state == AUDIO_A2DP_STATE_STARTED)
                suspend_audio_datapath(a2dp_hal, false);
        } else {
            //if (a2dp_hal->state == AUDIO_A2DP_STATE_SUSPENDED)
                a2dp_hal->state = AUDIO_A2DP_STATE_STANDBY;
        }
        pthread_mutex_unlock(&a2dp_hal->mutex);
    }
    return 0;
}

static char *a2dp_out_get_parameters(const struct audio_stream* stream, const char* keys) {
    struct aml_stream_out* aml_out = (struct aml_stream_out*)stream;
    struct aml_audio_device *adev = aml_out->dev;
    aml_a2dp_hal *a2dp_hal = adev->a2dp_hal;
    char cap[1024];
    int size = 0;
    btav_a2dp_codec_config_t codec_config;
    btav_a2dp_codec_config_t codec_capability;

    AM_LOGI("a2dp_out_get_parameters %s,out %p\n", keys, aml_out);
    if (a2dp_get_output_audio_config(a2dp_hal, &codec_config, &codec_capability) < 0) {
        AM_LOGE("a2dp_out_get_parameters: keys=%s, a2dp_get_output_audio_config error", keys);
        return strdup("");
    }
    memset(cap, 0, 1024);
    if (strstr(keys, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        bool first = true;
        size += sprintf(cap + size, "%s=", AUDIO_PARAMETER_STREAM_SUP_FORMATS);
        if (codec_capability.bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16) {
            if (!first)
                size += sprintf(cap + size, "%s", "|");
            else
                first = false;
            size += sprintf(cap + size, "%s", "AUDIO_FORMAT_PCM_16_BIT");
        }
        if (codec_capability.bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24) {
            if (!first)
                size += sprintf(cap + size, "%s", "|");
            else
                first = false;
            size += sprintf(cap + size, "%s", "AUDIO_FORMAT_PCM_24_BIT_PACKED");
        }
        if (codec_capability.bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32) {
            if (!first)
                size += sprintf(cap + size, "%s", "|");
            else
                first = false;
            size += sprintf(cap + size, "%s", "AUDIO_FORMAT_PCM_32_BIT");
        }
    }else if (strstr(keys, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES)) {
        bool first = true;
        size += sprintf(cap + size, "%s=", AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES);
        if (codec_capability.sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_44100) {
            if (!first)
                size += sprintf(cap + size, "%s", "|");
            else
                first = false;
            size += sprintf(cap + size, "%s", "44100");
        }
        if (codec_capability.sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_48000) {
            if (!first)
                size += sprintf(cap + size, "%s", "|");
            else
                first = false;
            size += sprintf(cap + size, "%s", "48000");
        }
        if (codec_capability.sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_88200) {
            if (!first)
                size += sprintf(cap + size, "%s", "|");
            else
                first = false;
            size += sprintf(cap + size, "%s", "88200");
        }
        if (codec_capability.sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_96000) {
            if (!first)
                size += sprintf(cap + size, "%s", "|");
            else
                first = false;
            size += sprintf(cap + size, "%s", "96000");
        }
        if (codec_capability.sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_176400) {
            if (!first)
                size += sprintf(cap + size, "%s", "|");
            else
                first = false;
            size += sprintf(cap + size, "%s", "176400");
        }
        if (codec_capability.sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_192000) {
            if (!first)
                size += sprintf(cap + size, "%s", "|");
            else
                first = false;
            size += sprintf(cap + size, "%s", "192000");
        }
    } else if (strstr(keys, AUDIO_PARAMETER_STREAM_SUP_CHANNELS)) {
        bool first = true;
        size += sprintf(cap + size, "%s=", AUDIO_PARAMETER_STREAM_SUP_CHANNELS);
        if (codec_capability.channel_mode & BTAV_A2DP_CODEC_CHANNEL_MODE_MONO) {
            if (!first)
                size += sprintf(cap + size, "%s", "|");
            else
                first = false;
            size += sprintf(cap + size, "%s", "AUDIO_CHANNEL_OUT_MONO");
        }
        if (codec_capability.channel_mode & BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO) {
            if (!first)
                size += sprintf(cap + size, "%s", "|");
            else
                first = false;
            size += sprintf(cap + size, "%s", "AUDIO_CHANNEL_OUT_STEREO");
        }
    }
    return strdup(cap);
}

uint32_t a2dp_out_get_latency(struct aml_audio_device *adev) {
    (void *)adev;
    return 200;
}

static int a2dp_out_standby_l(aml_a2dp_hal *a2dp_hal) {

    int ret = 0;
    AM_LOGD("state:%s", a2dpStatus2String(a2dp_hal->state));
    // Do nothing in SUSPENDED state.
    if (a2dp_hal->state != AUDIO_A2DP_STATE_SUSPENDED) {
        ret = suspend_audio_datapath(a2dp_hal, true);
    }
    if (a2dp_hal->vir_buf_handle != NULL) {
        audio_virtual_buf_close(&a2dp_hal->vir_buf_handle);
    }
    return ret;
}

int a2dp_out_standby(struct aml_audio_device *adev) {
    aml_a2dp_hal *a2dp_hal = adev->a2dp_hal;
    if (a2dp_hal == NULL) {
        AM_LOGW("a2dp_hal is released");
        return -1;
    }
    pthread_mutex_lock(&a2dp_hal->mutex);
    int ret = a2dp_out_standby_l(a2dp_hal);
    pthread_mutex_unlock(&a2dp_hal->mutex);
    return ret;
}

static void process_time(struct aml_audio_device *adev, size_t sent) {
    aml_a2dp_hal *a2dp_hal = adev->a2dp_hal;

    if (adev->is_TV && adev->audio_patching) {
        size_t frame_size = audio_channel_count_from_out_mask(a2dp_hal->a2dp_config.channel_mask) *
                audio_bytes_per_sample(a2dp_hal->a2dp_config.format);
        uint64_t input_ns = 0;
        static uint64_t begin_ns = 0;
        uint64_t process_ns = 0;
        input_ns = (sent * NSEC_PER_SEC) / (frame_size * a2dp_hal->a2dp_config.sample_rate);
        if (adev->debug_flag) {
            if (total_input_ns == 0)
                begin_ns = aml_audio_get_systime_ns();
            process_ns = aml_audio_get_systime_ns() - begin_ns;
            AM_LOGD("skt_write: process_ns %lld input_ns %lld, diff: %lldms (%lld), cur write=%llu ms",
                process_ns, total_input_ns, (((int64_t)total_input_ns - process_ns) / NSEC_PER_MSEC),
                ((int64_t)total_input_ns - process_ns), input_ns / NSEC_PER_MSEC);
            total_input_ns += input_ns;
        }

        if (a2dp_hal->vir_buf_handle == NULL) {
            audio_virtual_buf_open(&a2dp_hal->vir_buf_handle, "a2dp", VIR_BUFF_NS, VIR_BUFF_NS, 0);
            audio_virtual_buf_process(a2dp_hal->vir_buf_handle, VIR_BUFF_NS - input_ns / 2);
        }
        audio_virtual_buf_process(a2dp_hal->vir_buf_handle, input_ns);
    }
}

static bool a2dp_state_process(struct aml_audio_device *adev, size_t bytes) {
    aml_a2dp_hal        *a2dp_hal = adev->a2dp_hal;
    bool                prepared = true;
    uint64_t            pre_time_us = a2dp_hal->last_write_time;


    uint64_t cur_write_time_us = aml_audio_get_systime();
    uint64_t write_delta_time_us = cur_write_time_us - pre_time_us;
    a2dp_hal->last_write_time = cur_write_time_us;
    if (adev->debug_flag & DEBUG_LOG_MASK_A2DP) {
        AM_LOGD("bytes:%d, gap:%llu ms", bytes, write_delta_time_us / USEC_PER_MSEC);
    }
    if (adev->audio_patch && a2dp_hal->state == AUDIO_A2DP_STATE_STARTED) {
        if (write_delta_time_us > 128000) {
            AM_LOGI("tv_mute:%d, gap:%lld ms, start standby", adev->tv_mute, write_delta_time_us / USEC_PER_MSEC);
            a2dp_out_standby_l(a2dp_hal);
            prepared = false;
        }
    }

    if (a2dp_hal->state == AUDIO_A2DP_STATE_SUSPENDED || a2dp_hal->state == AUDIO_A2DP_STATE_STOPPING) {
        AM_LOGI("not ready state:%s", a2dpStatus2String(a2dp_hal->state));
        prepared = false;
    }

    /* only allow autostarting if we are in stopped or standby */
    if (a2dp_hal->state == AUDIO_A2DP_STATE_STOPPED || a2dp_hal->state == AUDIO_A2DP_STATE_STANDBY) {
        total_input_ns = 0;
        if (start_audio_datapath(a2dp_hal) < 0) {
            prepared = false;
        } else {
            prepared = true;
            AM_LOGI("start a2dp success");
        }
    } else if (a2dp_hal->state != AUDIO_A2DP_STATE_STARTED) {
        AM_LOGE("not in stopped or standby");
        prepared = false;
    }
    return prepared;
}

static ssize_t a2dp_in_data_process(aml_a2dp_hal *hal, audio_config_base_t *config, const void *buffer, size_t bytes) {
    size_t frames = 0;
    if (config->channel_mask == AUDIO_CHANNEL_OUT_7POINT1 && config->format == AUDIO_FORMAT_PCM_32_BIT) {
        int16_t *tmp_buffer = (int16_t *)buffer;
        int32_t *tmp_buffer_8ch = (int32_t *)buffer;
        frames = bytes / (4 * 8);
        for (int i=0; i<frames; i++) {
            tmp_buffer[2 * i]       = (tmp_buffer_8ch[8 *  i] >> 16);
            tmp_buffer[2 * i + 1]   = (tmp_buffer_8ch[8 * i + 1] >> 16);
        }
    } else if (config->channel_mask == AUDIO_CHANNEL_OUT_STEREO && config->format == AUDIO_FORMAT_PCM_16_BIT) {
        frames = bytes / (2 * 2);
    } else {
        AM_LOGW("not support param, channel_cnt:%d, format:%#x",
            audio_channel_count_from_out_mask(config->channel_mask), config->format);
        return -1;
    }
    config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    config->format = AUDIO_FORMAT_PCM_16_BIT;

    const int64_t cur_write_time_us = aml_audio_get_systime();
    if (hal->mute_time > 0) {
        if (hal->mute_time > cur_write_time_us) {
            memset((void*)buffer, 0, bytes);
        } else {
            hal->mute_time = 0;
        }
    }
    return frames;
}

static ssize_t a2dp_data_resample_process(aml_a2dp_hal *hal, audio_config_base_t *config,
    const void *buffer, size_t in_frames, const void **output_buffer) {
    int out_frames = in_frames;
    if (config->sample_rate != hal->a2dp_config.sample_rate) {
        size_t in_frame_size = audio_channel_count_from_out_mask(config->channel_mask) * audio_bytes_per_sample(config->format);
        /* The resampled frames may be large than the theoretical value.
         * So, there is an extra 32 bytes allocated to prevent overflows.
         */
        if (hal->resample != NULL && hal->resample->resample_config.input_sr != config->sample_rate) {
            free(hal->resample);
            hal->resample = NULL;
        }
        if (hal->resample == NULL) {
            audio_resample_config_t stResamplerConfig;
            stResamplerConfig.aformat   = config->format;
            stResamplerConfig.channels  = audio_channel_count_from_out_mask(config->channel_mask);
            stResamplerConfig.input_sr  = config->sample_rate;
            stResamplerConfig.output_sr = hal->a2dp_config.sample_rate;
            int ret = aml_audio_resample_init(&hal->resample, AML_AUDIO_SIMPLE_RESAMPLE, &stResamplerConfig);
            R_CHECK_RET(ret, "Resampler is failed initialization !!!");
        }
        aml_audio_resample_process(hal->resample, (void *)buffer, in_frames * in_frame_size);
        *output_buffer = hal->resample->resample_buffer;
        int out_size = hal->resample->resample_size;
        out_frames = out_size / 4;
        AM_LOGV("in_size:%d, out_size:%d, inframe:%d, cal_out_f:%d, real_out_f:%d",
            in_frames *in_frame_size, out_size,
            in_frames, (in_frames * hal->resample->resample_config.output_sr) / hal->resample->resample_config.input_sr, out_frames);
    }
    return out_frames;
}

static ssize_t a2dp_out_data_process(aml_a2dp_hal *hal, audio_config_base_t *config,
    const void *buffer, size_t in_frames, const void **output_buffer) {
    size_t in_ch = audio_channel_count_from_out_mask(config->channel_mask);
    size_t in_frame_size = in_ch * audio_bytes_per_sample(config->format);
    size_t out_ch = audio_channel_count_from_out_mask(hal->a2dp_config.channel_mask);
    ssize_t out_size = in_frames * out_ch * audio_bytes_per_sample(hal->a2dp_config.format);

    if (config->format == hal->a2dp_config.format && in_ch == out_ch) {
        *output_buffer = buffer;
        return out_size;
    }

    if (hal->buff_size_conv_format < out_size || hal->buff_conv_format == NULL) {
        if (hal->buff_conv_format) {
            free(hal->buff_conv_format);
            hal->buff_conv_format = NULL;
        }
        hal->buff_conv_format = aml_audio_calloc(1, out_size);
        if (hal->buff_conv_format == NULL) {
            AM_LOGE("realloc hal->buff fail, out_size:%d", out_size);
            *output_buffer = buffer;
            return 0;
        }
    }

    if (in_ch != 2 || config->format != AUDIO_FORMAT_PCM_16_BIT ||
        (out_ch != 1 && out_ch != 2)) {
        AM_LOGW("not support cfg ch:%d, a2dp ch:%d format:%#x", in_ch, out_ch, config->format);
        return 0;
    }

    if (out_ch == 1) {
        downmix_to_mono_i16_from_stereo_i16((int16_t *)hal->buff_conv_format, buffer, in_frames);
    }

    memcpy_by_audio_format(hal->buff_conv_format, hal->a2dp_config.format, buffer, config->format, in_frames * in_ch);
    *output_buffer = hal->buff_conv_format;
    return out_size;
}


ssize_t a2dp_out_write(struct aml_audio_device *adev, audio_config_base_t *config, const void *buffer, size_t bytes) {
    aml_a2dp_hal *a2dp_hal = adev->a2dp_hal;
    int sent = -1;
    const void *wr_buff = NULL;
    size_t in_frames = 0;
    size_t out_frames = 0;
    size_t wr_size = 0;

    if (a2dp_hal == NULL) {
        AM_LOGW("a2dp_hal is released");
        return bytes;
    }
    pthread_mutex_lock(&a2dp_hal->mutex);

    bool prepared = a2dp_state_process(adev, bytes);
    if (!prepared) {
        goto finish;
    }

    in_frames = a2dp_in_data_process(a2dp_hal, config, buffer, bytes);
    if (in_frames < 0) {
        goto finish;
    }

    out_frames = a2dp_data_resample_process(a2dp_hal, config, buffer, in_frames, &wr_buff);
    if (out_frames < 0) {
        goto finish;
    }

    wr_size = a2dp_out_data_process(a2dp_hal, config, wr_buff, out_frames, &wr_buff);
    if (wr_size == 0) {
        goto finish;
    }

    if (aml_getprop_bool("vendor.media.audiohal.a2dpdump")) {
        aml_audio_dump_audio_bitstreams("/data/audio/a2dp.pcm",
            wr_buff, wr_size);
    }

    size_t writed_bytes = 0;
    while (writed_bytes < wr_size) {
        size_t need_write = (wr_size - writed_bytes > a2dp_hal->buffer_sz)? a2dp_hal->buffer_sz : (wr_size - writed_bytes);
        sent = skt_write(a2dp_hal, (char *)wr_buff + writed_bytes, need_write);
        AM_LOGV("need_write:%zu, actual sent:%d, writed_bytes:%zu, total_size:%zu", need_write, sent, writed_bytes, wr_size);
        if (sent < 0) {
            break;
        } else if (sent > 0) {
            process_time(adev, sent);
        }
        writed_bytes += sent;
    }

finish:
    if (adev->debug_flag & DEBUG_LOG_MASK_A2DP) {
        AM_LOGD("in:%zu, in_frames:%zu, out:%zu, out_frames:%zu", in_frames, bytes, out_frames, wr_size);
    }
    pthread_mutex_unlock(&a2dp_hal->mutex);
    if (sent == -1) {
        // If send didn't work a2dp_hal, sleep to emulate write delay.
        const int us_delay = (out_frames * USEC_PER_SEC) / a2dp_hal->a2dp_config.sample_rate;
        AM_LOGW("frames:%zu, emulate a2dp write delay (%ld ms)", out_frames, us_delay / USEC_PER_MSEC);
        usleep(us_delay);
    }
    return bytes;
}

int a2dp_out_open(struct aml_audio_device *adev) {
    aml_a2dp_hal *a2dp_hal = NULL;
    int ret = 0;

    if (adev->a2dp_hal != NULL) {
        AM_LOGD("a2dp_out_open already exist");
        return 0;
    }
    a2dp_hal = (aml_a2dp_hal*)aml_audio_calloc(1, sizeof(aml_a2dp_hal));
    if (!a2dp_hal) {
        AM_LOGE("a2dp_out_open aml_a2dp_hal realloc error");
        return -ENOMEM;
    }
    adev->a2dp_hal = a2dp_hal;
    pthread_mutex_init(&a2dp_hal->mutex, NULL);
    pthread_mutex_lock(&a2dp_hal->mutex);

    total_input_ns = 0;
    /* initialize a2dp specifics */
    a2dp_stream_common_init(a2dp_hal);
    a2dp_hal->vir_buf_handle = NULL;

    // Make sure we always have the feeding parameters configured
    if (a2dp_read_output_audio_config(a2dp_hal, &a2dp_hal->capability) < 0) {
        AM_LOGE("a2dp_read_output_audio_config failed");
        ret = -1;
    }
    a2dp_write_output_audio_config(a2dp_hal, &a2dp_hal->capability);
    a2dp_read_output_audio_config(a2dp_hal, &a2dp_hal->capability);

    AM_LOGI("channel:%d, fmt:%#x, rate:%d, buf_sz:%zu", audio_channel_count_from_out_mask(a2dp_hal->a2dp_config.channel_mask),
        a2dp_hal->a2dp_config.format, a2dp_hal->a2dp_config.sample_rate, a2dp_hal->buffer_sz);

    adev->sink_format = AUDIO_FORMAT_PCM_16_BIT;
    adev->optical_format = AUDIO_FORMAT_PCM_16_BIT;
    a2dp_hal->buff_conv_format = NULL;
    a2dp_hal->buff_size_conv_format = 0;
    a2dp_hal->mute_time = aml_audio_get_systime(); + USEC_PER_SEC; // mute for 1s
    pthread_mutex_unlock(&a2dp_hal->mutex);
    return 0;
}

int a2dp_out_close(struct aml_audio_device *adev) {
    aml_a2dp_hal *a2dp_hal = adev->a2dp_hal;
    pthread_mutex_lock(&a2dp_hal->mutex);
    if (a2dp_hal == NULL)
        return -1;

    AM_LOGD("state:%s", a2dpStatus2String(a2dp_hal->state));
    if ((a2dp_hal->state == AUDIO_A2DP_STATE_STARTED) || (a2dp_hal->state == AUDIO_A2DP_STATE_STOPPING)) {
        stop_audio_datapath(a2dp_hal);
    }

    if (a2dp_hal->buff_conv_format)
        aml_audio_free(a2dp_hal->buff_conv_format);
    if (a2dp_hal->vir_buf_handle != NULL) {
        audio_virtual_buf_close(&a2dp_hal->vir_buf_handle);
    }

    a2dp_stream_common_destroy(a2dp_hal);
    total_input_ns = 0;
    adev->a2dp_hal = NULL;
    pthread_mutex_unlock(&a2dp_hal->mutex);
    aml_audio_free(a2dp_hal);
    return 0;
}

int a2dp_hal_dump(struct aml_audio_device *adev, int fd) {
    aml_a2dp_hal *hal = (aml_a2dp_hal *)adev->a2dp_hal;
    if (hal) {
        dprintf(fd, "-------------[AM_HAL][A2DP]-------------\n");
        dprintf(fd, "-[AML_HAL]      out_rate       : %10d | out_ch             :%10d\n", hal->a2dp_config.sample_rate, audio_channel_count_from_out_mask(hal->a2dp_config.channel_mask));
        dprintf(fd, "-[AML_HAL]      out_format     : %#10x | cur_state          :%10s\n", hal->a2dp_config.format, a2dpStatus2String(hal->state));
        aml_audio_resample_t *resample = hal->resample;
        if (resample) {
            audio_resample_config_t *config = &resample->resample_config;
            dprintf(fd, "-[AML_HAL] resample in_sr      : %10d | out_sr             :%10d\n", config->input_sr, config->output_sr);
            dprintf(fd, "-[AML_HAL] resample ch         : %10d | type               :%10d\n", config->channels, resample->resample_type);
        }
        a2dp_hw_dump(hal, fd);
    }
    return 0;
}

