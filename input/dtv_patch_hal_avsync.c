/*
 * Copyright (C) 2019 Amlogic Corporation.
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
 *
 * Function:
 * this file is created for starting play avsync
 */

#define LOG_TAG "aml_audio_hal_avsync"

#include <cutils/atomic.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <errno.h>
#include <fcntl.h>
#include <hardware/hardware.h>
#include <inttypes.h>
#include <linux/ioctl.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <system/audio.h>
#include <time.h>
#include <utils/Timers.h>
#if ANDROID_PLATFORM_SDK_VERSION >= 25 // 8.0
#include <system/audio-base.h>
#endif
#include <hardware/audio.h>
#include <aml_android_utils.h>
#include <aml_data_utils.h>
#include "aml_audio_stream.h"
#include "aml_audio_timer.h"
#include "aml_data_utils.h"
#include "audio_hw.h"
#include "dtv_patch.h"
#include "audio_hw_profile.h"
#include "audio_hw_utils.h"
#include "dtv_patch_out.h"
#include "aml_audio_resampler.h"
#include "audio_hw_ms12.h"
#include "dolby_lib_api.h"
#include "audio_dtv_ad.h"
#include "alsa_device_parser.h"
#include "dtv_patch_hal_avsync.h"
#include <audio_dtv_sync.h>
#include "aml_audio_report.h"
#include "aml_audio_sysfs.h"
static struct timespec start_time;

static int decoder_audio_mode(void)
{
    int ret, mode = 0;
    char buff[PROPERTY_VALUE_MAX];
    ret = aml_sysfs_get_str(TSYNC_AUDIO_MODE, buff, sizeof(buff));
    if (ret > 0) {
        sscanf(buff, "%d", &mode);
    }
    return mode;
}

static uint32_t decoder_get_latency(void)
{
    uint32_t latency = 0;
    char buff[PROPERTY_VALUE_MAX];
    int ret;

    ret = aml_sysfs_get_str(TSYNC_PCR_LANTCY, buff, sizeof(buff));
    if (ret > 0) {
        ret = sscanf(buff, "%u\n", &latency);
    }
    return (uint32_t)latency;
}

static void decoder_stop_audio(struct aml_audio_patch *patch)
{
    char buff[PROPERTY_VALUE_MAX];

    if (patch == NULL ||
        patch->tsync_mode == TSYNC_MODE_PCRMASTER) {
        return;
    }
    sprintf(buff, "AUDIO_STOP");
    if (sysfs_set_sysfs_str(TSYNC_EVENT, buff) == -1) {
        ALOGE("set AUDIO_STOP failed \n");
    }
}

static void decoder_set_latency(unsigned int latency)
{
    char tempbuf[PROPERTY_VALUE_MAX];
    uint32_t decoder_latency;

    decoder_latency = decoder_get_latency();
    if (latency == decoder_latency) {
        return;
    }
    memset(tempbuf, 0, PROPERTY_VALUE_MAX);
    sprintf(tempbuf, "%d", latency);
    ALOGI("%s: %u->%u, diff %d ms, now %d ms\n", __func__, decoder_latency, latency,
        (int)(latency - decoder_latency) / 90, (int)latency / 90);
    if (aml_sysfs_set_str(TSYNC_PCR_LANTCY, tempbuf) == -1) {
        ALOGE("set pcr lantcy failed %s\n", tempbuf);
    }
    return;
}

void decoder_set_pcrscr(unsigned int pcrscr)
{
    char tempbuf[PROPERTY_VALUE_MAX] = {0};
    uint32_t pcrpts = 0;

    /*[SE][BUG][SWPL-21122][chengshun] need add 0x, avoid driver get error*/
    sprintf(tempbuf, "0x%x", pcrscr);
    get_sysfs_uint(TSYNC_PCRSCR, &pcrpts);
    if (aml_sysfs_set_str(TSYNC_PCRSCR, tempbuf) == -1) {
        ALOGE("%s, set pcrscr failed %s\n", __func__, tempbuf);
    }
    ALOGI("%s, pcrscr %x -> %x, diff %d ms", __func__, pcrpts, pcrscr, (int)(pcrscr - pcrpts) / 90);
    return;
}

static void decoder_update_apts(uint32_t apts)
{
    char tempbuf[PROPERTY_VALUE_MAX] = {0};
    int ret = 0;

    sprintf(tempbuf, "%u", apts);
    ret = sysfs_set_sysfs_str(TSYNC_APTS, tempbuf);
    if (ret < 0) {
        ALOGE("%s, update apt failed\n", __func__);
    }
    return;
}

static int decoder_pcr_init_mode(void)
{
    int ret, mode = 0;
    char buff[PROPERTY_VALUE_MAX];

    ret = aml_sysfs_get_str(TSYNC_PCR_INITED, buff, sizeof(buff));
    if (ret > 0) {
        ret = sscanf(buff, "%d", &mode);
    }
    return mode;
}

static int decoder_dmxpcr_valid(void)
{
    int ret, mode = 0;
    char buff[PROPERTY_VALUE_MAX];

    ret = aml_sysfs_get_str(TSYNC_DMXPCR_VALID, buff, sizeof(buff));
    if (ret > 0) {
        ret = sscanf(buff, "%d", &mode);
    }
    return mode;
}

static int decoder_dmxpcr_mode(void)
{
    int ret, mode = 0;
    char buff[PROPERTY_VALUE_MAX];

    ret = aml_sysfs_get_str(TSYNC_PCR_MODE, buff, sizeof(buff));
    if (ret > 0) {
        ret = sscanf(buff, "%d", &mode);
    }
    return mode;
}

static void decoder_set_video_delay(int delay_ms)
{
    char tempbuf[PROPERTY_VALUE_MAX] = {0};

    if (delay_ms < -100 || delay_ms > 500) {
        ALOGE("set_video_delay out of range[-100 - 500] %d\n", delay_ms);
        return;
    }
    sprintf(tempbuf, "%d", delay_ms);
    if (aml_sysfs_set_str(TSYNC_VPTS_ADJ, tempbuf) == -1) {
        ALOGE("set_video_delay %s\n", tempbuf);
    }
    return;
}

static int decoder_get_video_delay(void)
{
    char tempbuf[PROPERTY_VALUE_MAX];
    int delay_ms = 0, ret;

    ret = aml_sysfs_get_str(TSYNC_VPTS_ADJ, tempbuf, sizeof(tempbuf));
    if (ret > 0) {
        ret = sscanf(tempbuf, "%d\n", &delay_ms);
    }
    if (ret <= 0) {
        delay_ms = 0;
    }
    return delay_ms;
}

static int decoder_get_video_state(void)
{
    char buff[PROPERTY_VALUE_MAX];
    int ret = 0, state = 2;

    ret = aml_sysfs_get_str(TSYNC_VIDEO_STATE, buff, sizeof(buff));
    if (ret > 0) {
        ret = sscanf(buff, "%d\n", &state);
    }
    return state;
}

static int decoder_get_audio_state(void)
{
    char buff[PROPERTY_VALUE_MAX];
    int ret = 0, state = 2;

    ret = aml_sysfs_get_str(TSYNC_AUDIO_STATE, buff, sizeof(buff));
    if (ret > 0) {
        ret = sscanf(buff, "%d\n", &state);
    }
    return state;
}

static int decoder_get_tsync_mode(void)
{
    char tsync_mode_str[PROPERTY_VALUE_MAX];
    char tempbuf[PROPERTY_VALUE_MAX];
    int tsync_mode = TSYNC_MODE_PCRMASTER;

    if (aml_sysfs_get_str(DTV_DECODER_TSYNC_MODE, tempbuf, sizeof(tempbuf)) <= 0) {
        ALOGI("dtv_get_tsync_mode error");
        return tsync_mode;
    }
    if (sscanf(tempbuf, "%d: %s", &tsync_mode, tsync_mode_str) < 1) {
        ALOGI("dtv_get_tsync_mode error.");
        return tsync_mode;
    }
    if (tsync_mode > TSYNC_MODE_PCRMASTER || tsync_mode < TSYNC_MODE_VMASTER) {
        tsync_mode = TSYNC_MODE_PCRMASTER;
    }
    return tsync_mode;
}

unsigned long decoder_apts_lookup(unsigned int offset)
{
    unsigned int pts = 0;
    int ret;
    char buff[PROPERTY_VALUE_MAX] = {0};

    snprintf(buff, PROPERTY_VALUE_MAX, "%d", offset);
    aml_sysfs_set_str(DTV_DECODER_PTS_LOOKUP_PATH, buff);
    ret = aml_sysfs_get_str(DTV_DECODER_PTS_LOOKUP_PATH, buff, sizeof(buff));

    if (ret > 0) {
        ret = sscanf(buff, "0x%x\n", &pts);
    }
    if (pts == (unsigned int) - 1) {
        pts = 0;
    }
    return (unsigned long)pts;
}

static bool dtv_avsync_valid_diff_pts32(uint32_t pts1, uint32_t pts2)
{
    return (VALID_PTS32(pts1) && VALID_PTS32(pts2)) ?
        (abs((int)(pts1 - pts2)) < AUDIO_PTS_DISCONTINUE_THRESHOLD ? true : false) : false;
}

static uint32_t dtv_avsync_get_time_interval(struct timespec *ts_start)
{
    struct timespec cur_ts;
    int64_t interval_us;

    clock_gettime(CLOCK_MONOTONIC, &cur_ts);
    interval_us = calc_time_interval_us(ts_start, &cur_ts);
    return (uint32_t)(interval_us / 1000);
}

/* set demux pcr latency */
static int dtv_avsync_set_dmx_latency(struct aml_audio_patch* patch, int latency)
{
    int ret, diff = 0;
    char buff[32];
    int audio_latency = latency;

    ret = aml_sysfs_get_str(TSYNC_APTS_DIFF, buff, sizeof(buff));
    if (ret > 0) {
        ret = sscanf(buff, "%d\n", &diff);
    }
    if (diff >= DECODER_PTS_DEFAULT_LATENCY) {
        decoder_set_latency(DECODER_PTS_DEFAULT_LATENCY);
    } else {
        decoder_set_latency(audio_latency);
    }
    audio_latency = decoder_get_latency();
    ALOGV("%s, diff %d, latency %d mode %d\n", __FUNCTION__, diff, audio_latency, patch->sync_para.pcr_init_mode);
    return decoder_get_latency();
}

/* record whether the pcrscr is stuttered */
static int dtv_avsync_record_pcrscr(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    bool discontinued = false;
    uint32_t mode = 0, cur_pcr = -1;
    int ret, cost_ms;

    cost_ms = dtv_avsync_get_time_interval(&patch->sync_para.pcrpts_jumped_record);
#ifdef PROCESS_TSYNC_DISCONTINUE
    if (cost_ms > TIME_UNIT90K / 90) {
        ret = get_sysfs_uint(TSYNC_PCR_DISCONTINUE, &mode);
        if (ret == 0 && (mode & PCR_DISCONTINUE)) {
            discontinued = true;
        }
    }
    ret = 0;
    if (discontinued) {
        clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.pcrpts_jumped_record);
        get_sysfs_uint(TSYNC_PCRSCR, &cur_pcr);
        if (VALID_PTS32(cur_pcr) && cur_pcr) {
            if (cur_pcr > patch->sync_para.last_pcrpts) {
                ret = PTS_FF_JUMPED;
            } else {
                ret = PTS_FB_JUMPED;
            }
        }
    }
#else
    if (cost_ms > TIME_UNIT90K / 90) {
        if (VALID_PTS32(patch->sync_para.cur_pcrpts) && patch->sync_para.cur_pcrpts &&
            VALID_PTS32(patch->sync_para.last_pcrpts) && patch->sync_para.last_pcrpts) {
            if (!dtv_avsync_valid_diff_pts32(patch->sync_para.cur_pcrpts, patch->sync_para.last_pcrpts)) {
                discontinued = true;
            }
        }
    }
    ret = 0;
    if (discontinued) {
        clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.pcrpts_jumped_record);
        if (patch->sync_para.cur_pcrpts > patch->sync_para.last_pcrpts) {
           ret = PTS_FF_JUMPED;
        } else {
            ret = PTS_FB_JUMPED;
        }
        cur_pcr = patch->sync_para.cur_pcrpts;
    }
#endif
    if (ret) {
        ALOGI("%s, PCR_DISCONTINUE -> %s, cur_pcr %x, last_pcr %x", __func__, ret == PTS_FF_JUMPED ?
            "PTS_FF_JUMPED" : "PTS_FB_JUMPED", cur_pcr, patch->sync_para.last_pcrpts);
    }
    return ret;
}

/* record audio out_pts */
static void dtv_avsync_record_out_apts(struct audio_stream_out *stream, uint32_t apts)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    int diff_apts_ms = 0;
    int threshold_ms, level;

    if (!patch || patch->tsync_mode != TSYNC_MODE_PCRMASTER) {
        return;
    }
    threshold_ms = property_get_int32(PROPERTY_AUDIO_JUMPED_THRESHOLD_PROPERTY, DTV_AUDIO_JUMPED_DEFAULT_THRESHOLD);
    if (VALID_PTS32(patch->sync_para.last_lookup_apts)) {
        if (threshold_ms <= 0) {
            threshold_ms = DTV_AUDIO_JUMPED_DEFAULT_THRESHOLD;
        }
        if (patch->sync_para.last_lookup_apts > apts) {
            if ((patch->sync_para.last_lookup_apts - apts) / 90 > (uint32_t)threshold_ms) {
                patch->sync_para.out_apts_jumped = PTS_FB_JUMPED;
                clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.outapts_jumped_record);
                ALOGI("%s, audio_jumped -> PTS_FB_JUMPED, last_pts %x,cur_pts %x\n",
                    __FUNCTION__, patch->sync_para.last_lookup_apts, apts);
            }
        } else if (patch->sync_para.last_lookup_apts < apts) {
            level = 0;
            if (patch->sync_para.last_validpts_duration == 0) {
                if (patch->cur_outapts > patch->sync_para.last_lookup_apts) {
                    level = patch->cur_outapts - patch->sync_para.last_lookup_apts;
                }
            } else {
                level = patch->sync_para.last_validpts_duration;
            }
            diff_apts_ms = (int)(apts - patch->sync_para.last_lookup_apts) / 90 - level / 90;
            if (diff_apts_ms > threshold_ms) {
                patch->sync_para.out_apts_jumped = PTS_FF_JUMPED;
                clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.outapts_jumped_record);
                ALOGI("%s, audio_jumped -> PTS_FF_JUMPED, diff %d ms, last_pts %x,cur_pts %x, dur %x, level %d\n",
                    __FUNCTION__, diff_apts_ms, patch->sync_para.last_lookup_apts,
                    apts, patch->sync_para.last_validpts_duration, level);
            }
        } else {
            return;
        }
    }
    patch->sync_para.last_lookup_apts = apts;
}

/* input latency by format for ms12 case */
static int dtv_get_ms12_input_latency(struct audio_stream_out *stream, audio_format_t input_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    char buf[PROPERTY_VALUE_MAX];
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;

    switch (input_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        prop_name = DTV_AVSYNC_MS12_PCM_LATENCY_PROPERTY;
        latency_ms = DTV_AVSYNC_MS12_PCM_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC3: {
        prop_name = DTV_AVSYNC_MS12_DD_LATENCY_PROPERTY;
        latency_ms = DTV_AVSYNC_MS12_DD_LATENCY;
        break;
    }
    case AUDIO_FORMAT_E_AC3: {
        prop_name = DTV_AVSYNC_MS12_DDP_LATENCY_PROPERTY;
        latency_ms = DTV_AVSYNC_MS12_DDP_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC4: {
        prop_name = DTV_AVSYNC_MS12_AC4_LATENCY_PROPERTY;
        latency_ms = DTV_AVSYNC_MS12_AC4_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_HE_AAC_V1:
    case AUDIO_FORMAT_HE_AAC_V2: {
        prop_name = DTV_AVSYNC_MS12_AAC_LATENCY_PROPERTY;
        latency_ms = DTV_AVSYNC_MS12_AAC_LATENCY;
        break;
    }
    default:
        break;
    }
    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }
    return latency_ms;
}

/* outport latency for ms12 case */
static int dtv_get_ms12_port_latency(struct audio_stream_out *stream, enum OUT_PORT port, audio_format_t output_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    char buf[PROPERTY_VALUE_MAX];
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;

    switch (port) {
        case OUTPORT_HDMI_ARC:
        {
            if (output_format == AUDIO_FORMAT_AC3) {
                if (aml_dev->ms12.is_bypass_ms12) {
                    latency_ms = DTV_AVSYNC_MS12_HDMI_ARC_OUT_PT_DD_LATENCY;
                    prop_name = DTV_AVSYNC_MS12_HDMI_ARC_OUT_PT_DD_LATENCY_PROPERTY;
                } else {
                    latency_ms = DTV_AVSYNC_MS12_HDMI_ARC_OUT_DD_LATENCY;
                    prop_name = DTV_AVSYNC_MS12_HDMI_ARC_OUT_DD_LATENCY_PROPERTY;
                }
            }
            else if (output_format == AUDIO_FORMAT_E_AC3) {
                if (aml_dev->ms12.is_bypass_ms12) {
                    latency_ms = DTV_AVSYNC_MS12_HDMI_ARC_OUT_PT_DDP_LATENCY;
                    prop_name = DTV_AVSYNC_MS12_HDMI_ARC_OUT_PT_DDP_LATENCY_PROPERTY;
                } else {
                    latency_ms = DTV_AVSYNC_MS12_HDMI_ARC_OUT_DDP_LATENCY;
                    prop_name = DTV_AVSYNC_MS12_HDMI_ARC_OUT_DDP_LATENCY_PROPERTY;
                }
            }
            else if (output_format == AUDIO_FORMAT_MAT) {
                latency_ms = DTV_AVSYNC_MS12_HDMI_ARC_OUT_MAT_LATENCY;
                prop_name = DTV_AVSYNC_MS12_HDMI_ARC_OUT_MAT_LATENCY_PROPERTY;
            } else {
                latency_ms = DTV_AVSYNC_MS12_HDMI_ARC_OUT_PCM_LATENCY;
                prop_name = DTV_AVSYNC_MS12_HDMI_ARC_OUT_PCM_LATENCY_PROPERTY;
            }
            break;
        }
        case OUTPORT_HDMI:
        {
            if (output_format == AUDIO_FORMAT_AC3) {
                latency_ms = 0;
            }
            else if (output_format == AUDIO_FORMAT_E_AC3) {
                latency_ms = 0;
            }
            else if (output_format == AUDIO_FORMAT_MAT) {
                latency_ms = 0;
            }
            else {
                latency_ms = 0;
            }
            break;
        }
        case OUTPORT_SPEAKER:
        case OUTPORT_AUX_LINE:
        {
            if (aml_dev->is_TV) {
                latency_ms = DTV_AVSYNC_MS12_TV_SPEAKER_LATENCY;
                prop_name = DTV_AVSYNC_MS12_TV_SPEAKER_LATENCY_PROPERTY;
            } else {
                latency_ms = 0;
            }
            break;
        }
        case OUTPORT_A2DP:
        {
            if (aml_dev->is_TV) {
                if (aml_dev->dev2mix_patch) {
                    latency_ms = DTV_AVSYNC_MS12_TV_MIX_A2DP_LATENCY;
                    prop_name = DTV_AVSYNC_MS12_TV_MIX_A2DP_LATENCY_PROPERTY;
                } else {
                    latency_ms = 0;
                }
            } else {
                latency_ms = 0;
            }
            break;
        }
        default :
            break;
    }
    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }
    return latency_ms;
}

/* offset latency for ms12 case */
static int dtv_get_ms12_offset_latency(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    int latency_ms = 0;
    int input_latency_ms = 0;
    int output_latency_ms = 0;
    int port_latency_ms = 0;

    input_latency_ms = dtv_get_ms12_input_latency(stream, aml_out->hal_internal_format);
    port_latency_ms = dtv_get_ms12_port_latency(stream, get_output_by_devices(aml_dev->cur_out_devices), aml_dev->ms12.optical_format);
    latency_ms = input_latency_ms + output_latency_ms + port_latency_ms;
    ALOGV("%s total latency %d(ms) hal_fmt %x(%d ms) opt_fmt %x(%d ms) port %d(%d ms)", __func__, latency_ms,
        aml_out->hal_internal_format, input_latency_ms, aml_dev->ms12.optical_format, output_latency_ms,
        aml_dev->cur_out_devices, port_latency_ms);
    return latency_ms;
}

/* input latency by format for nonms12 case */
static int dtv_get_nonms12_input_latency(struct audio_stream_out *stream, audio_format_t input_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    char buf[PROPERTY_VALUE_MAX];
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;

    switch (input_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        prop_name = DTV_AVSYNC_NONMS12_PCM_LATENCY_PROPERTY;
        latency_ms = DTV_AVSYNC_NONMS12_PCM_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC3: {
        prop_name = DTV_AVSYNC_NONMS12_DD_LATENCY_PROPERTY;
        latency_ms = DTV_AVSYNC_NONMS12_DD_LATENCY;
        break;
    }

    case AUDIO_FORMAT_E_AC3: {
        prop_name = DTV_AVSYNC_NONMS12_DDP_LATENCY_PROPERTY;
        latency_ms = DTV_AVSYNC_NONMS12_DDP_LATENCY;
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}

/* outport latency for nonms12 case */
static int dtv_get_nonms12_port_latency(struct audio_stream_out * stream, enum OUT_PORT port, audio_format_t output_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    char buf[PROPERTY_VALUE_MAX];
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;

    switch (port) {
        case OUTPORT_HDMI_ARC:
        {
            if (output_format == AUDIO_FORMAT_AC3) {
                latency_ms = DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_DD_LATENCY;
                prop_name = DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_DD_LATENCY_PROPERTY;
            }
            else if (output_format == AUDIO_FORMAT_E_AC3) {
                latency_ms = DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_DDP_LATENCY;
                prop_name = DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_DDP_LATENCY_PROPERTY;
            }
            else {
                latency_ms = DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_PCM_LATENCY;
                prop_name = DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_PCM_LATENCY_PROPERTY;
            }
            break;
        }
        case OUTPORT_HDMI:
        {
            if (output_format == AUDIO_FORMAT_AC3) {
                latency_ms = 0;
            }
            else if (output_format == AUDIO_FORMAT_E_AC3) {
                latency_ms = 0;
            }
            else {
                latency_ms = 0;
            }
            break;
        }
        case OUTPORT_SPEAKER:
        case OUTPORT_AUX_LINE:
        {
            latency_ms = DTV_AVSYNC_NONMS12_TV_SPEAKER_LATENCY;
            prop_name = DTV_AVSYNC_NONMS12_TV_SPEAKER_LATENCY_PROPERTY;
            break;
        }
        default :
            break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }
    return latency_ms;
}

/* offset latency for nonms12 case */
static int dtv_get_nonms12_offset_latency(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    int latency_ms = 0;
    int input_latency_ms = 0;
    int output_latency_ms = 0;
    int port_latency_ms = 0;

    input_latency_ms = dtv_get_nonms12_input_latency(stream, aml_out->hal_internal_format);
    port_latency_ms = dtv_get_nonms12_port_latency(stream, get_output_by_devices(aml_dev->cur_out_devices), aml_dev->sink_format);
    latency_ms = input_latency_ms + output_latency_ms + port_latency_ms;
    ALOGV("%s total latency %d(ms) hal_fmt %x(%d ms) opt_fmt %x(%d ms) port %d(%d ms)", __func__, latency_ms,
        aml_out->hal_internal_format, input_latency_ms, aml_dev->sink_format, output_latency_ms,
        aml_dev->cur_out_devices, port_latency_ms);
    return latency_ms;
}

/* get apts latency for avsync, uint: pts*/
int dtv_avsync_get_apts_latency(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    int32_t video_delay = 0;
    int32_t alsa_delay = 0;
    int32_t tuning_ms = 0;

    if (aml_dev->dolby_lib_type == eDolbyMS12Lib) {
        tuning_ms = dtv_get_ms12_offset_latency(stream);
    } else {
        tuning_ms = dtv_get_nonms12_offset_latency(stream);
    }
    tuning_ms += aml_getprop_int(PROPERTY_LOCAL_PASSTHROUGH_LATENCY);
    if (aml_dev->is_TV) {
        video_delay = 0;
    }
    patch->sync_para.out_apts_offset = (tuning_ms + alsa_delay + video_delay) * 90;
    return patch->sync_para.out_apts_offset;
}

/* get master frame size */
static int dtv_audio_get_framesize(struct audio_stream_out *stream, audio_format_t output_format)

{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    int frame_size = AUDIO_DEFAULT_PCM_FRAME_SIZE;

    if (aml_dev->dolby_lib_type == eDolbyMS12Lib || output_format == AUDIO_FORMAT_PCM_16_BIT) {
        if (aml_dev->is_TV) {
            frame_size = AUDIO_TV_PCM_FRAME_SIZE;
        } else {
            frame_size = AUDIO_DEFAULT_PCM_FRAME_SIZE;
        }
    } else {
        switch (aml_dev->sink_format) {
            case AUDIO_FORMAT_E_AC3:
                frame_size = AUDIO_EAC3_FRAME_SIZE;
                break;
            case AUDIO_FORMAT_AC3:
                frame_size = AUDIO_AC3_FRAME_SIZE;
                break;
            default:
                if (aml_dev->is_TV) {
                    frame_size = AUDIO_TV_PCM_FRAME_SIZE;
                } else {
                    frame_size = AUDIO_DEFAULT_PCM_FRAME_SIZE;
                }
                break;
        }
    }
    return frame_size;
}

/* process audio drop policy */
static void dtv_audio_drop_process(struct audio_stream_out *stream,audio_format_t output_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    int frame_size = dtv_audio_get_framesize(stream, output_format), debug_flag = 0;
    int pts_diff, used_ms, cached_apts, target_diff, timeout_ms = 0;
    uint32_t target_apts;

    get_sysfs_uint(TSYNC_CHECKIN_APTS, &patch->sync_para.cur_checkin_apts);
    target_apts = patch->sync_para.cur_checkin_apts - patch->sync_para.tsync_latency - patch->sync_para.default_acache_offset;
    cached_apts = patch->sync_para.cur_checkin_apts - patch->cur_outapts;
    pts_diff = target_apts - patch->cur_outapts;
    if (patch->sync_para.audio_drop_sum == 0 && pts_diff > 0) {
        patch->need_drop_size = (pts_diff / 90) * 48 * frame_size;
        patch->sync_para.audio_drop_sum = patch->need_drop_size;
        clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.audio_tune_record);
        ALOGI("%s, start, pts_diff %d ms, target_drop %d ms", __func__,
            patch->sync_para.cur_pts_diff / 90, pts_diff / 90);
        debug_flag = 1;
    }
    used_ms = dtv_avsync_get_time_interval(&patch->sync_para.audio_tune_record);
    timeout_ms = property_get_int32(PROPERTY_DTV_AUDIO_DROP_TIMEOUTMS,
        DTV_AUDIO_DROP_TIMEOUT_THRESHOLD);
    if (pts_diff < 0 || patch->sync_para.cur_checkin_apts < patch->cur_outapts) {
        ALOGI("%s, apts jumped, finished", __func__);
        patch->need_drop_size = 0;
    } else if (pts_diff / 90 > DTV_AUDIO_DROP_DEFAULT_THRESHOLD &&
        patch->need_drop_size / frame_size / 48 < DTV_AUDIO_DROP_DEFAULT_THRESHOLD &&
        patch->sync_para.cur_pts_diff > 3 * patch->sync_para.avsync_retune_threshold) {
        patch->sync_para.audio_drop_sum -= patch->need_drop_size;
        patch->need_drop_size = pts_diff / 90 * 48 * frame_size;
        patch->sync_para.audio_drop_sum += patch->need_drop_size;
        ALOGI("%s, recalc drop_size -> %u, left %d", __func__, patch->sync_para.audio_drop_sum, patch->need_drop_size);
        debug_flag = 1;
    }
    if (timeout_ms > 0 && used_ms > timeout_ms) {
        ALOGI("%s, drop timeout, finished, left need_drop %d", __func__, patch->need_drop_size);
        patch->need_drop_size = 0;
    }
    if (patch->tsync_pcr_debug || debug_flag || patch->need_drop_size == 0) {
        ALOGI("%s, pts_diff %d ms, cached %d ms, need_drop %d ms, used %d ms, avail %d bytes", __func__,
            patch->sync_para.cur_pts_diff / 90, cached_apts / 90, (patch->sync_para.audio_drop_sum -
            patch->need_drop_size) / (48 * frame_size), used_ms, get_buffer_read_space(&patch->aml_ringbuffer));
    }
    if (patch->need_drop_size == 0) {
        pts_diff = patch->sync_para.cur_pcrpts - patch->cur_outapts;
        ALOGI("%s, finished, cost[%d] ms", __func__, used_ms);
        patch->sync_para.audio_drop_sum = 0;
    }
}

/* process audio insert policy */
bool dtv_audio_insert_mute(struct audio_stream_out *stream, int time_ms)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    struct timespec cur_time;
    int cost_ms = 0;

    clock_gettime(CLOCK_MONOTONIC, &cur_time);
    patch->pcm_inserting = true;
    if (eDolbyDcvLib == aml_dev->dolby_lib_type) {
        //insert data from aml_dtvsync_insertpcm api
        aml_dtvsync_insertpcm(stream, AUDIO_FORMAT_PCM_16_BIT, time_ms, false);
    } else {
        //aml_dtvsync_insertpcm(stream, AUDIO_FORMAT_PCM_16_BIT, time_ms, true);
        usleep(time_ms * 1000);
    }
    cost_ms = dtv_avsync_get_time_interval(&cur_time);
    patch->pcm_inserting = false;
    ALOGI("%s, mute [%d] ms, cost [%d] ms", __func__, time_ms, cost_ms);
    return true;
}

/* process audio drop and hold policy */
static bool dtv_drop_hold_process(struct audio_stream_out *stream, bool discontinued, audio_format_t output_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    bool disable_drop = false, disable_hold = false, ret = true, dmxpcr_valid;
    int cached_apts, pts_diff, cost_ms, target_acache;

    pts_diff = patch->sync_para.cur_pcrpts - (patch->cur_outapts - patch->sync_para.out_apts_offset);
    cached_apts = patch->sync_para.cur_checkin_apts - patch->cur_outapts;
    dmxpcr_valid = (decoder_dmxpcr_mode() && (patch->sync_para.pcr_init_mode == PCR_INIT_DMXPCR));
    disable_drop = property_get_bool(PROPERTY_DTV_AUDIO_DROP_DISABLE, false);
    disable_hold = property_get_bool(PROPERTY_DTV_AUDIO_HOLD_DISABLE, false);
    target_acache = patch->sync_para.tsync_latency + patch->sync_para.default_acache_offset;
    if (abs(pts_diff) < AUDIO_PTS_DISCONTINUE_THRESHOLD &&
        dtv_avsync_valid_diff_pts32(patch->sync_para.cur_checkin_apts, patch->cur_outapts)) {
        if (patch->tsync_pcr_debug) {
            ALOGI("%s, pts_diff %d ms, cached[%d->%d]", __func__, pts_diff / 90, cached_apts / 90, target_acache / 90);
        }
        if (target_acache > cached_apts && patch->ac3_pcm_dropping == false) {
            clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.audio_tune_record);
            do {
                cost_ms = dtv_avsync_get_time_interval(&patch->sync_para.audio_tune_record);
                get_sysfs_uint(TSYNC_CHECKIN_APTS, &patch->sync_para.cur_checkin_apts);
                get_sysfs_uint(TSYNC_PCRSCR, &patch->sync_para.cur_pcrpts);
                cached_apts = (int)(patch->sync_para.cur_checkin_apts - patch->cur_outapts);
                pts_diff = patch->sync_para.cur_pcrpts - (patch->cur_outapts - patch->sync_para.out_apts_offset);
                if (patch->tsync_pcr_debug) {
                    ALOGI("%s, cur[apts %x,pcr %x,diff %d ms], acached[%d %d] ms", __func__, patch->cur_outapts,
                    patch->sync_para.cur_pcrpts, pts_diff / 90, cached_apts / 90, target_acache / 90);
                }
                if (disable_hold && discontinued) {
                    ALOGI("%s, disable_hold, checkin apts %x, out_apts %x, pa-diff %d ms", __func__,
                        patch->sync_para.cur_checkin_apts, patch->cur_outapts, pts_diff / 90);
                    break;
                }
                if (patch->sync_para.cur_checkin_apts + TIME_UNIT90K < patch->cur_outapts ||
                    !dtv_avsync_valid_diff_pts32(patch->sync_para.cur_checkin_apts, patch->cur_outapts)) {
                    ALOGI("%s, abnormal, checkin apts %x, out_apts %x, pa-diff %d ms", __func__,
                        patch->sync_para.cur_checkin_apts, patch->cur_outapts, pts_diff / 90);
                    break;
                }
                if (pts_diff >= 0 && (cached_apts > target_acache || (!discontinued &&
                    cached_apts > patch->sync_para.tsync_latency))) {
                    ALOGI("%s, finished, diff %d ms, apts_cached %d ms", __func__, pts_diff / 90, cached_apts / 90);
                    break;
                } else if (pts_diff < 0 && cached_apts > target_acache) {
                    if (cached_apts > patch->sync_para.tsync_latency * 4 / 3 ||
                        cached_apts > DECODER_PTS_MAX_LATENCY) {
                        ALOGI("%s, finished, diff %d ms, apts_cached %d ms, tsync %d ms",
                            __func__, pts_diff / 90, cached_apts / 90, patch->sync_para.tsync_latency / 90);
                        break;
                    }
                }
                if (cost_ms > DEFAULT_AV_THRESHOLD) {
                    ALOGI("%s, timeout, diff %d ms, apts_cached %d ms", __func__, pts_diff / 90, cached_apts / 90);
                    break;
                }
                dtv_audio_insert_mute(stream, 32);
            } while (!patch->output_thread_exit);
            ALOGI("%s, hold finished, target: %d -> %d", __func__, cached_apts / 90, target_acache / 90);
        } else if (patch->ac3_pcm_dropping || (patch->ac3_pcm_dropping == false &&
            cached_apts > target_acache + DTV_AUDIO_DROP_DEFAULT_THRESHOLD * 90)) {
            if (!patch->ac3_pcm_dropping) {
                patch->ac3_pcm_dropping = true;
                patch->sync_para.audio_drop_sum = 0;
                ALOGI("%s, drop start, target: %d -> %d", __func__, cached_apts / 90, target_acache / 90);
            }
            if (disable_drop && discontinued) {
                patch->ac3_pcm_dropping = false;
                patch->sync_para.audio_drop_sum = 0;
                ALOGI("%s,disable_drop, target: %d -> %d", __func__, cached_apts / 90, target_acache / 90);
            } else if (pts_diff > DTV_PTS_CORRECTION_THRESHOLD * 2) {
                dtv_audio_drop_process(stream, output_format);
                ret = false;
            } else {
                patch->sync_para.audio_drop_sum = 0;
                ALOGI("%s, pts_diff %d ms, resumed.", __func__, pts_diff / 90);
            }
            if (patch->sync_para.audio_drop_sum == 0) {
                ret = true;
                patch->ac3_pcm_dropping = false;
                ALOGI("%s, drop finished, target: %d -> %d", __func__, cached_apts / 90, target_acache / 90);
            }
        } else {
            patch->sync_para.audio_drop_sum = 0;
            if (patch->ac3_pcm_dropping) {
                patch->ac3_pcm_dropping = 0;
            }
            ALOGI("%s, resumed, pts_diff %d ms, cached[%d,%d]", __func__, pts_diff / 90, cached_apts / 90, target_acache / 90);
        }
    }
    return ret;
}

/* re-align output after apts stuttered */
static bool dtv_process_apts_discontinue(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    int target_acache, cached_apts, pts_diff, cost_ms;
    bool ret = true, disable_hold = false;

    disable_hold = property_get_bool(PROPERTY_DTV_AUDIO_HOLD_DISABLE, false);
    do {
        get_sysfs_uint(TSYNC_LAST_CHECKIN_APTS, &patch->sync_para.cur_checkin_apts);
        get_sysfs_uint(TSYNC_PCRSCR, &patch->sync_para.cur_pcrpts);
        cached_apts = patch->sync_para.cur_checkin_apts - patch->cur_outapts;
        pts_diff = patch->sync_para.cur_pcrpts - (patch->cur_outapts - patch->sync_para.out_apts_offset);
        cost_ms = dtv_avsync_get_time_interval(&patch->sync_para.outapts_jumped_record);
        target_acache = patch->sync_para.tsync_latency + patch->sync_para.default_acache_offset;
        if (!dtv_avsync_valid_diff_pts32(patch->sync_para.cur_checkin_apts, patch->cur_outapts) ||
            patch->sync_para.cur_checkin_apts + TIME_UNIT90K < patch->cur_outapts) {
            ALOGI("%s, checkin_apts jumped, diff %d ms, out_apts %x, checkin_apts %x", __func__,
                cached_apts / 90, patch->cur_outapts, patch->sync_para.cur_checkin_apts);
            ret = false;
            break;
        }
        if (patch->sync_para.pcrscr_jumped == PTS_NORMAL) {
            patch->sync_para.pcrscr_jumped = dtv_avsync_record_pcrscr(stream);
        }
        if (patch->sync_para.pcrscr_jumped) {
            ALOGI("%s, pcrscr_jumped, cur_diff %d ms", __func__, pts_diff / 90);
            break;
        }
        if (!patch->sync_para.pcrscr_jumped && pts_diff < DEFAULT_AV_THRESHOLD * 90 &&
            pts_diff > 0 && cached_apts > target_acache) {
            ALOGI("%s, out_apts resumed, cur[apts %x,pcr %x], cur_diff %d ms, acached[%d %d] ms", __func__,
                patch->cur_outapts, patch->sync_para.cur_pcrpts, pts_diff / 90, cached_apts / 90, target_acache / 90);
            break;
        }
        if (cached_apts > (target_acache + DTV_AUDIO_DROP_DEFAULT_THRESHOLD * 90)) {
            ALOGI("%s, cached resumed, cur_apts %x, checkin_apts %x, cur_pcr %x, acached[%d %d] ms", __func__,
                patch->cur_outapts, patch->sync_para.cur_checkin_apts, patch->sync_para.cur_pcrpts,
                cached_apts / 90, target_acache / 90);
            break;
        }
        if (disable_hold) {
            ALOGI("%s, disable_hold, diff %d ms, apts_cached %d ms", __func__, pts_diff / 90, cached_apts / 90);
            break;
        }
        if (patch->tsync_pcr_debug) {
            ALOGI("%s, cur[apts %x:%x,pcr %x,pa_diff %d ms], acached[%d %d] ms", __func__, patch->cur_outapts,
                patch->sync_para.cur_checkin_apts, patch->sync_para.cur_pcrpts, pts_diff / 90,
                cached_apts / 90, target_acache / 90);
        }
        dtv_audio_insert_mute(stream, 32);
    } while (cost_ms < DEFAULT_AV_THRESHOLD && !patch->output_thread_exit);
    if (cost_ms >= DEFAULT_AV_THRESHOLD) {
        ALOGI("%s, out_apts_jumped timeout, cur_diff %d ms", __func__, pts_diff / 90);
    }
    return ret;
}

/* slowsync adjust pcrpts and output_apts gaps */
static bool dtv_process_apts_tuning(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    char tempbuf[PROPERTY_VALUE_MAX] = {0};
    uint32_t t1 = 0, t2 = 0, out_apts;
    bool ret = false, dmxpcr_valid;
    int diff= 0;

    dmxpcr_valid = (decoder_dmxpcr_mode() && (patch->sync_para.pcr_init_mode == PCR_INIT_DMXPCR));
    out_apts = patch->cur_outapts - patch->sync_para.out_apts_offset;
#ifdef PROCESS_TSYNC_DISCONTINUE
    if (dmxpcr_valid) {
        t1 = decoder_get_latency();
    } else {
        get_sysfs_uint(TSYNC_PCRSCR, &t1);
    }
    sprintf(tempbuf, "AUDIO_TSTAMP_DISCONTINUITY:0x%lx", (unsigned long)out_apts);
    if (sysfs_set_sysfs_str(TSYNC_EVENT, tempbuf) == -1) {
        ALOGI("unable to open file %s,err: %s", TSYNC_EVENT, strerror(errno));
    }
    if (dmxpcr_valid) {
        t2 = decoder_get_latency();
    } else {
        get_sysfs_uint(TSYNC_PCRSCR, &t2);
    }
#else
    if (dtv_avsync_get_time_interval(&patch->sync_para.pcrpts_tune_record) < 100) {
        return ret;
    }
    clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.pcrpts_tune_record);
    get_sysfs_uint(TSYNC_PCRSCR, &patch->sync_para.cur_pcrpts);
    if (patch->sync_para.cur_pcrpts > out_apts) {
        diff = -DTV_PTS_CORRECTION_THRESHOLD / 6;
    } else {
        diff = DTV_PTS_CORRECTION_THRESHOLD / 6;
    }
    if (dmxpcr_valid) {
        t1 = decoder_get_latency();
        if ((diff < 0 && (int)t1 < TIME_UNIT90K + diff) ||
            (diff > 0 && (int)t1 > diff)) {
            decoder_set_latency(t1 - diff);
        }
        t2 = decoder_get_latency();
    } else {
        t1 = patch->sync_para.cur_pcrpts;
        decoder_set_pcrscr(t1 + diff);
        get_sysfs_uint(TSYNC_PCRSCR, &t2);
    }
#endif
    if (t1 != t2) {
        ret = true;
        if (patch->tsync_pcr_debug) {
            ALOGI("%s, AUDIO_TSTAMP_DISCONTINUITY %x -> %x, diff %d ms",
                __func__, t1, t2, (int)(t2 - t1) / 90);
        }
    }
    return ret;
}

/* tuning audio clock */
static bool dtv_process_clock_tuning(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    int cached_apts, target_acache, pts_diff, direct = DIRECT_NORMAL;
    bool ret = false, dmxpcr_valid, force_tuning = false;
    uint32_t t1 = 0, out_apts;

    dmxpcr_valid = (decoder_dmxpcr_mode() && (patch->sync_para.pcr_init_mode == PCR_INIT_DMXPCR));
    out_apts = patch->cur_outapts - patch->sync_para.out_apts_offset;
    cached_apts = patch->sync_para.cur_checkin_apts - patch->cur_outapts;
    pts_diff = (patch->sync_para.cur_pts_diff + patch->sync_para.last_pts_diff) / 2;
    target_acache = patch->sync_para.tsync_latency + patch->sync_para.default_acache_offset;
    if (property_get_int32(PROPERTY_DTV_RESAMPLE_DISABLE, 1)) {
        return false;
    }
    if (patch->dtv_audio_tune != AUDIO_RUNNING || patch->sync_para.avsync_status == AVSYNC_FREE ||
        abs(pts_diff) > 3 * patch->sync_para.avsync_retune_threshold) {
        direct = DIRECT_NORMAL;
        force_tuning = true;
    }
    if (!force_tuning && dmxpcr_valid) {
        if (patch->pll_state == DIRECT_SPEED) {
            if (cached_apts > target_acache) {
                direct = DIRECT_SPEED;
            }
        } else if (patch->pll_state == DIRECT_SLOW) {
            if (cached_apts < target_acache) {
                direct = DIRECT_SLOW;
            }
        } else if (patch->pll_state == DIRECT_NORMAL) {
            if (cached_apts > target_acache + DECODER_PTS_MIN_LATENCY) {
                direct = DIRECT_SPEED;
            } else if (cached_apts < patch->sync_para.tsync_latency / 2) {
                direct = DIRECT_SLOW;
            }
        }
        if (!patch->sync_para.avsync_retune && abs(pts_diff) > DTV_PTS_CORRECTION_THRESHOLD &&
            abs(patch->sync_para.cur_pts_diff) > DTV_PTS_CORRECTION_THRESHOLD &&
            abs(patch->sync_para.last_pts_diff) > DTV_PTS_CORRECTION_THRESHOLD) {
            if (direct == DIRECT_SPEED && pts_diff < 0) {
                dtv_process_apts_tuning(stream);
            } else if (direct == DIRECT_SLOW && pts_diff > 0) {
                dtv_process_apts_tuning(stream);
            }
        }
        if ((patch->tsync_pcr_debug > 0 && direct != DIRECT_NORMAL) || direct != patch->pll_state) {
            ALOGI("%s, direct %d, cached [%d -> %d] ms, pts_diff [%d,%d,%d] ms\n",
                __func__, direct, cached_apts / 90, patch->sync_para.tsync_latency / 90, pts_diff / 90,
                patch->sync_para.cur_pts_diff / 90, patch->sync_para.last_pts_diff / 90);
        }
    }
    if (direct != DIRECT_NORMAL || direct != patch->pll_state) {
        //dtv_adjust_output_clock(stream, direct, DEFAULT_DTV_ADJUST_CLOCK);
    }
    return direct != DIRECT_NORMAL;
}

/* amaster process */
void dtv_process_amaster_sync(struct audio_stream_out *stream, uint32_t pcrpts)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    char tempbuf[PROPERTY_VALUE_MAX];
    uint32_t last_pcrpts, last_apts, apts;
    int pcrpts_diff, last_pts_diff, cur_pts_diff;

    if (!patch) {
        return;
    }
    apts = patch->cur_outapts - patch->sync_para.out_apts_offset;
    last_apts = patch->last_apts;
    last_pcrpts = patch->last_pcrpts;
    patch->last_pcrpts = pcrpts;
    patch->last_apts = apts;
    last_pts_diff = last_pcrpts - last_apts;
    cur_pts_diff = pcrpts - apts;
    pcrpts_diff = pcrpts - last_pcrpts;
    if (last_apts == 0 || last_pcrpts == 0 ||
        patch->dtv_has_video == 0 || patch->dtv_audio_mode == 1) {
        return;
    }
    if (abs(last_pts_diff - cur_pts_diff) > DTV_PTS_CORRECTION_THRESHOLD) {
        return;
    }
    if (abs(cur_pts_diff) <= SYSTIME_CORRECTION_THRESHOLD) {
        // now the av is syncd ,so do nothing;
    } else if (abs(cur_pts_diff) < AUDIO_PTS_DISCONTINUE_THRESHOLD) {
        sprintf(tempbuf, "%u", (unsigned int)apts);
        if (patch->tsync_pcr_debug) {
            ALOGI("reset the apts to %x pcrpts %x pts_diff %d ms\n", apts, pcrpts, cur_pts_diff / 90);
        }
        sysfs_set_sysfs_str(TSYNC_APTS, tempbuf);
    } else {
        ALOGI("AUDIO_TSTAMP_DISCONTINUITY, the apts %x, pcrpts %x pts_diff %d \n",
            apts, pcrpts, cur_pts_diff);
        sprintf(tempbuf, "AUDIO_TSTAMP_DISCONTINUITY:0x%lx", (unsigned long)apts);
        if (sysfs_set_sysfs_str(TSYNC_EVENT, tempbuf) == -1) {
            ALOGI("unable to open file %s,err: %s", TSYNC_EVENT, strerror(errno));
        }
    }
}

/* process start_mute flag */
void dtv_process_start_mute(struct audio_stream_out *stream, uint32_t start_ms)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    uint32_t firstvpts = 0, pcrpts = -1;

    if (aml_dev->start_mute_flag) {
        get_sysfs_uint(TSYNC_FIRST_VPTS, &firstvpts);
        get_sysfs_uint(TSYNC_PCRSCR, &pcrpts);
        if (patch->tsync_mode != TSYNC_MODE_PCRMASTER) {
            ALOGI("non-pcrmaster, clear start_mute_flag 0\n");
            aml_dev->start_mute_flag = 0;
        }
        if (patch->dtv_has_video && patch->show_first_frame == 0) {
            if (access("/sys/module/aml_media/", F_OK) == 0) {
                patch->show_first_frame = get_sysfs_int(VIDEO_FIRST_FRAME_SHOW_2);
            } else {
                patch->show_first_frame = get_sysfs_int(VIDEO_FIRST_FRAME_SHOW);
            }
            if (patch->tsync_pcr_debug) {
                ALOGI("%s: first_frame %d, firstvpts %x, pcrpts %x",
                    __func__, patch->show_first_frame, firstvpts, pcrpts);
            }
            if (start_ms > DTV_AUDIO_START_MUTE_MAX_HTRESHOLD) {
                patch->show_first_frame = -1;
                ALOGI("%s: show_first_frame timeout", __func__);
            }
        }
        if ((firstvpts != 0 && pcrpts + 100 * 90 > firstvpts) || patch->show_first_frame ||
            start_ms > DTV_AUDIO_START_MUTE_MAX_HTRESHOLD || patch->dtv_audio_mode || patch->dtv_has_video == 0) {
            if (patch->dtv_audio_tune ==  AUDIO_RUNNING &&
                abs(patch->sync_para.cur_pts_diff) < DTV_AUDIO_DROP_DEFAULT_THRESHOLD * 90) {
                ALOGI("clear start_mute_flag 0,cur_diff=%d\n", patch->sync_para.cur_pts_diff);
                aml_dev->start_mute_flag = 0;
            } else if (start_ms > DTV_AUDIO_START_MUTE_MAX_HTRESHOLD) {
                ALOGI("timeout force clear start_mute_flag 0\n");
                aml_dev->start_mute_flag = 0;
            } else if (getprop_bool("vendor.media.audio.syncshow")) {
                ALOGI("need sync show, clear start_mute_flag\n");
                aml_dev->start_mute_flag = 0;
            } else if (patch->dtv_audio_mode) {
                ALOGI("audio free, clear start_mute_flag 0\n");
                aml_dev->start_mute_flag = 0;
            } else if (patch->dtv_has_video == 0) {
                ALOGI("dtv_has_video, clear start_mute_flag 0\n");
                aml_dev->start_mute_flag = 0;
            }
        }
    }
}

int dtv_get_frame_duration(struct audio_stream_out *stream, size_t bytes, audio_format_t output_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    int out_frames = bytes / 4, cur_frames = 0, duration_pts = 0;
    int sample_rate = 48000;

    if ((output_format == AUDIO_FORMAT_AC3) || (output_format == AUDIO_FORMAT_E_AC3) ||
        (output_format == AUDIO_FORMAT_MAT) || (output_format == AUDIO_FORMAT_DTS)) {
        // todo
        return -1;
    } else {
        cur_frames = bytes / dtv_audio_get_framesize(stream, output_format);
    }
    duration_pts = cur_frames * TIME_UNIT90K / sample_rate;
    if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
        /* in audio continuous mode, if all input was consumed or output was paused, skip it */
        if (aml_dev->continuous_audio_mode) {
            /*if ((dolby_ms12_get_consumed_sum(stream) & 0xffffffff) == patch->dtv_pcm_written ||
                aml_dev->ms12.is_continuous_paused) {
                duration_pts = 0;
            }*/
        }
    }
    return duration_pts;
}

/* avsync status reset */
void dtv_avsync_param_reset(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;

    patch->dtv_audio_tune = AUDIO_FREE;
    patch->first_apts_lookup_over = 0;
    patch->pcm_inserting = false;
    patch->last_apts = -1;
    patch->last_pcrpts = -1;
    patch->need_drop_size = 0;
    patch->sync_para.last_lookup_apts = -1;
    patch->sync_para.cur_checkin_vpts = -1;
    patch->sync_para.cur_tsync_apts = -1;
    patch->sync_para.cur_demux_pcr = -1;
    patch->sync_para.cur_out_vpts = -1;
    patch->sync_para.cur_pcrpts = -1;
    patch->sync_para.last_pcrpts = -1;
    aml_dev->start_mute_flag = 0;
    aml_dev->audio_discontinue = 0;
    aml_dev->discontinue_mute_flag = 0;
    patch->dtv_default_spdif_clock = 0;
    patch->spdif_step_clk = 0;
    patch->spdif_format_set = 0;
    if (patch->pll_state != DIRECT_NORMAL) {
        //dtv_adjust_output_clock(stream, DIRECT_NORMAL, DEFAULT_DTV_ADJUST_CLOCK);
        patch->pll_state = DIRECT_NORMAL;
    }
    decoder_stop_audio(patch);
    if (patch->tsync_pcr_debug) {
        ALOGI("%s, reset!", __func__);
    }
}

/* avsync parameter initialization */
void dtv_avsync_param_init(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;

    patch->dtv_audio_mode = decoder_audio_mode();
    patch->tsync_pcr_debug = get_tsync_pcr_debug();
    patch->dtv_audio_tune = AUDIO_FREE;
    patch->first_apts_lookup_over = 0;
    patch->need_drop_size = 0;
    patch->show_first_frame = 0;
    patch->last_apts = 0;
    patch->last_pcrpts = 0;
    patch->pcm_inserting = false;
    patch->pll_state = DIRECT_NORMAL;
    patch->ac3_pcm_dropping = false;
    patch->sync_para.avsync_duration = 0;
    patch->sync_para.last_validpts_duration = 0;
    patch->sync_para.validpts_cnt_record = 0;
    patch->sync_para.last_lookup_apts = -1;
    patch->sync_para.cur_checkin_vpts = -1;
    patch->sync_para.cur_tsync_apts = -1;
    patch->sync_para.cur_demux_pcr = -1;
    patch->sync_para.cur_out_vpts = -1;
    patch->sync_para.cur_pcrpts = -1;
    patch->sync_para.last_pcrpts = -1;
    patch->sync_para.cur_pts_diff = 0;
    patch->sync_para.last_pts_diff = 0;
    patch->sync_para.avsync_status = 0;
    patch->sync_para.avsync_retune = 0;
    patch->sync_para.pcr_init_mode = 0;
    patch->sync_para.dmxpcr_valid = 0;
    patch->sync_para.dmxpcr_latency = 0;
    patch->sync_para.tuned_pcrpts = 0;
    patch->sync_para.out_apts_jumped = 0;
    patch->sync_para.dmx_apts_jumped = 0;
    patch->sync_para.pcrscr_jumped = 0;
    patch->sync_para.out_apts_offset = 0;
    patch->sync_para.default_acache_offset = 0;
    patch->sync_para.audio_drop_sum = 0;
    patch->sync_para.audio_pause_resumed = 0;
    patch->sync_para.out_synced_frame_count = 0;
    patch->sync_para.last_synced_frame_pts = -1;
    patch->sync_para.tsync_latency = DEMUX_PCR_APTS_LATENCY;
    patch->sync_para.max_apts_cache = 90 * property_get_int32(
        PROPERTY_AUDIO_MAX_CACHE_THRESHOLD, DTV_AUDIO_CACHE_LATENCY_THRESHOLD);
    patch->sync_para.avsync_retune_threshold = 90 * property_get_int32(
        PROPERTY_AUDIO_RETUNE_THRESHOLD_PROPERTY, DTV_AUDIO_RETUNE_DEFAULT_THRESHOLD);
    if (patch->sync_para.avsync_retune_threshold < DTV_PTS_CORRECTION_THRESHOLD * 2) {
        patch->sync_para.avsync_retune_threshold = 90 * DTV_AUDIO_RETUNE_DEFAULT_THRESHOLD;
    }
    patch->sync_para.default_acache_offset = 90 * property_get_int32("vendor.media.audio.dtv.cache.threshold", 0);
    patch->sync_para.show_first_nosync = property_get_int32("vendor.media.video.show_first_frame_nosync", 1);
    clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.first_apts_record);
    clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.start_output_record);
    clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.apts_discontinue_record);
    clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.audio_tune_record);
    clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.pcrpts_tune_record);
    clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.main_loop_record);
    clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.outapts_jumped_record);
    clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.pcrpts_jumped_record);
    if (patch->dtv_audio_mode) {
        ALOGI("%s, dtv audio_mode is freerun", __func__);
    }
    ALOGI("%s, dolby_lib_type %d", __func__, aml_dev->dolby_lib_type);
}

/* avsync pause/resume process */
void dtv_avsync_pause_process(struct audio_stream_out *stream, int cmd)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    char buff[PROPERTY_VALUE_MAX];

    if (patch->dtv_decoder_state == AUDIO_DTV_PATCH_DECODER_STATE_RUNNING &&
        cmd == AUDIO_DTV_PATCH_CMD_PAUSE) {
        patch->sync_para.audio_pause_resumed = 0;
        ALOGI("%s, AUDIO_PAUSE", __FUNCTION__);
        sprintf(buff, "AUDIO_PAUSE");
        if (sysfs_set_sysfs_str(TSYNC_EVENT, buff) == -1) {
            ALOGE("set AUDIO_PAUSE failed \n");
        }
    } else if (patch->dtv_decoder_state == AUDIO_DTV_PATCH_DECODER_STATE_PAUSE &&
        cmd == AUDIO_DTV_PATCH_CMD_RESUME) {
        patch->sync_para.audio_pause_resumed = 1;
        ALOGI("%s, AUDIO_RESUME", __FUNCTION__);
        sprintf(buff, "AUDIO_RESUME");
        if (sysfs_set_sysfs_str(TSYNC_EVENT, buff) == -1) {
            ALOGE("set AUDIO_RESUME failed \n");
        }
    }
}

/* align av out_pts when startup or pts stuttered */
bool dtv_avsync_lookup_process(struct aml_audio_patch *patch, struct aml_audio_device *aml_dev)
{
    ring_buffer_t *ringbuffer = &(patch->aml_ringbuffer);
    bool av_valid = true, aligned = false, dmxpcr_valid = false;
    uint32_t first_checkinapts = 0xffffffff;
    uint32_t last_checkinapts = 0xffffffff;
    uint32_t last_checkinvpts = 0xffffffff;
    uint32_t first_checkinvpts = 0xffffffff;
    uint32_t cur_vpts = 0xffffffff;
    uint32_t first_vpts = 0xffffffff;
    uint32_t demux_pcr = 0xffffffff;
    uint32_t pcrpts = 0xffffffff;
    int acached_ms = 0, vcached_ms = 0;
    int avail, timeout_ms = 0;
    int pa_diff, pv_diff, av_diff;
    int cost_ms, start_cached_ms;

    if (!patch || !aml_dev || aml_dev->patch_src != SRC_DTV) {
        return true;
    }

    patch->tsync_mode = decoder_get_tsync_mode();
    avail = get_buffer_read_space(ringbuffer);
    get_sysfs_uint(TSYNC_FIRSTCHECKIN_APTS, &first_checkinapts);
    get_sysfs_uint(TSYNC_FIRSTCHECKIN_VPTS, &first_checkinvpts);
    get_sysfs_uint(TSYNC_FIRST_VPTS, &first_vpts);
    get_sysfs_uint(TSYNC_CHECKIN_APTS, &last_checkinapts);
    get_sysfs_uint(TSYNC_CHECKIN_VPTS, &last_checkinvpts);
    get_sysfs_uint(TSYNC_PCRSCR, &pcrpts);
    get_sysfs_uint(TSYNC_VPTS, &cur_vpts);

    // amaster mode;
    if (patch->tsync_mode != TSYNC_MODE_PCRMASTER) {
        start_cached_ms = DEMUX_PCR_APTS_LATENCY;
        cost_ms = dtv_avsync_get_time_interval(&patch->sync_para.start_output_record);
        if (VALID_PTS32(last_checkinapts) && last_checkinapts) {
            acached_ms = (int)(last_checkinapts - first_checkinapts) / 90;
        }
        if ((!patch->dtv_has_video && acached_ms > 0) || acached_ms > start_cached_ms || cost_ms > DEFAULT_AV_THRESHOLD) {
            ALOGI("%s, finished", __func__);
            aligned = true;
        }
        return aligned;
    }

    // pcrmaster mode;
    patch->sync_para.pcr_init_mode = decoder_pcr_init_mode();
    dmxpcr_valid = (decoder_dmxpcr_mode() && (patch->sync_para.pcr_init_mode == PCR_INIT_DMXPCR));
    patch->sync_para.tsync_latency = get_sysfs_int(TSYNC_REF_LATENCY);
    if (patch->sync_para.pcr_init_mode != PCR_INIT_DMXPCR) {
        patch->sync_para.tsync_latency += DECODER_PTS_MIN_LATENCY;
    }
    if (patch->sync_para.tsync_latency > patch->sync_para.max_apts_cache) {
        patch->sync_para.tsync_latency = patch->sync_para.max_apts_cache;
    }
    if (patch->dtv_audio_tune != AUDIO_BREAK) {
        cost_ms = dtv_avsync_get_time_interval(&patch->sync_para.start_output_record);
    } else {
        cost_ms = dtv_avsync_get_time_interval(&patch->sync_para.apts_discontinue_record);
    }
    if (patch->dtv_has_video == 0 || patch->dtv_audio_mode) {
        //no video or freerun case;
        if (avail > 0 && VALID_PTS32(last_checkinapts) && last_checkinapts) {
            acached_ms = (int)(last_checkinapts - first_checkinapts) / 90;
            if (sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_PRE_START") == -1) {
                ALOGI("unable to open file %s,err: %s", TSYNC_EVENT, strerror(errno));
            }
            aligned = true;
        }
    } else if (patch->dtv_audio_tune != AUDIO_BREAK) {
        // audio start normally
        if (VALID_PTS32(last_checkinapts) && VALID_PTS32(last_checkinvpts)) {
            av_valid = abs((int)(last_checkinapts - last_checkinvpts)) < AUDIO_PTS_DISCONTINUE_THRESHOLD * 2;
        }
        acached_ms = (int)(last_checkinapts - first_checkinapts) / 90;
        if (VALID_PTS32(last_checkinapts) && acached_ms > DEFAULT_AV_THRESHOLD) {
            ALOGI("%s, audio cached %d ms, timeout", __func__, acached_ms);
            aligned = true;
        } else if (patch->sync_para.pcr_init_mode == PCR_INIT_NONE) {
            if (VALID_PTS32(last_checkinapts) && VALID_PTS32(last_checkinvpts)) {
                if (sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_PRE_START") == -1) {
                    ALOGI("unable to open file %s,err: %s", TSYNC_EVENT, strerror(errno));
                }
            }
        } else if (patch->sync_para.pcr_init_mode == PCR_INIT_DMXPCR) {
            if (dmxpcr_valid) {
                patch->sync_para.dmxpcr_latency = dtv_avsync_set_dmx_latency(patch, patch->sync_para.tsync_latency);
            } else {
                patch->sync_para.dmxpcr_latency = patch->sync_para.tsync_latency;
            }
            if (VALID_PTS32(pcrpts) && pcrpts && VALID_PTS32(last_checkinapts) && last_checkinapts && avail > 0 && av_valid) {
                if (pcrpts >= first_checkinapts || first_checkinapts - pcrpts > AUDIO_PTS_DISCONTINUE_THRESHOLD) {
                    aligned = true;
                }
            } else if (!av_valid && acached_ms > patch->sync_para.tsync_latency / 3) {
                aligned = true;
            }
        } else {
            if (VALID_PTS32(pcrpts) && pcrpts && VALID_PTS32(last_checkinapts) && avail > 0) {
                if (pcrpts >= first_checkinapts || first_checkinapts - pcrpts > AUDIO_PTS_DISCONTINUE_THRESHOLD) {
                    aligned = true;
                }
            } else if (!av_valid) {
                aligned = true;
            }
        }
        if (cost_ms > DEFAULT_AV_THRESHOLD && !aligned) {
            aligned = true;
            ALOGI("%s, timeout, force starting", __func__);
        }
        if (aligned && av_valid) {
            ALOGI("%s, av aligned", __func__);
            patch->sync_para.avsync_status = AVSYNC_START;
            if (decoder_firstcheckin_avasync(patch)) {
                patch->dtv_disable_tune_latency = 1;
            }
        }
    } else {
        // audio discontinuity
        first_checkinapts = patch->sync_para.discontinue_apts;
        acached_ms = (int)(last_checkinapts - first_checkinapts) / 90;
        if (dmxpcr_valid) {
            if (decoder_get_latency() > patch->sync_para.dmxpcr_latency) {
               decoder_set_latency(patch->sync_para.dmxpcr_latency + patch->sync_para.out_apts_offset);
            } else {
               decoder_set_latency(patch->sync_para.dmxpcr_latency);
            }
            aligned = true;
            if ((first_checkinapts != 0xffffffff) && (pcrpts != 0xffffffff)) {
                if (pcrpts == 0 && first_checkinapts != 0 && last_checkinapts != 0) {
                    aligned = false;
                } else if (first_checkinapts > pcrpts) {
                    if (first_checkinapts - pcrpts < AUDIO_PTS_DISCONTINUE_THRESHOLD) {
                        aligned = false;
                    }
                }
            }
        } else {
            if (pcrpts > last_checkinapts && pcrpts - last_checkinapts > AUDIO_PTS_DISCONTINUE_THRESHOLD) {
                aligned = true;
            } else if ((pcrpts > last_checkinapts && pcrpts - last_checkinapts < AUDIO_PTS_DISCONTINUE_THRESHOLD) ||
                (last_checkinapts > pcrpts && (int)(last_checkinapts - pcrpts) < patch->sync_para.tsync_latency)) {
                pcrpts = last_checkinapts - patch->sync_para.tsync_latency - patch->sync_para.out_apts_offset;
                decoder_set_pcrscr(pcrpts);
                aligned = true;
            } else if (pcrpts > first_checkinapts) {
                aligned = true;
            }
        }
        if (acached_ms >= patch->sync_para.max_apts_cache / 90) {
            aligned = true;
        }
        if (cost_ms > DEFAULT_AV_THRESHOLD && !aligned) {
            aligned = true;
            ALOGI("%s, timeout, force starting", __func__);
        }
        if (aligned) {
            ALOGI("%s, av aligned", __func__);
            patch->sync_para.avsync_status = AVSYNC_RESTART;
        }
        if (aml_dev->dolby_lib_type == eDolbyMS12Lib && aligned) {
            //dtv_dolby_ms12_pause_resume(patch->stream_out, AUDIO_DTV_PATCH_CMD_RESUME);
        }
    }
    if (patch->tsync_pcr_debug || aligned) {
        pa_diff = (int)(first_checkinapts - pcrpts) / 90;
        pv_diff = (int)(first_checkinvpts - pcrpts) / 90;
        av_diff = (int)(first_checkinapts - first_checkinvpts) / 90;
        if (VALID_PTS32(last_checkinapts) && VALID_PTS32(last_checkinvpts)) {
            ALOGI("%s, av available, pcrpts %x, first_apts %x,last_apts=%x,first_vpts=%x(has_video:%d, out_vpts %x),"
                " last_vpts=%x, first-diff:pa-pv-av:[%d,%d,%d], cost_ms %d ms, mode %d %d, avail %d bytes, acached %d ms",\
                __func__, pcrpts, first_checkinapts, last_checkinapts, first_checkinvpts, patch->dtv_has_video,\
                cur_vpts, last_checkinvpts, pa_diff, pv_diff, av_diff, cost_ms, patch->sync_para.pcr_init_mode,
                decoder_dmxpcr_mode(), avail, acached_ms);
        } else if (VALID_PTS32(last_checkinapts)) {
            ALOGI("%s, audio available, pcrpts %x, first_apts %x,last_apts=%x,(has_video:%d),"
                " first-diff:pa:[%d], cost_ms %d ms, mode %d %d, avail %d bytes %d ms",\
                __func__, pcrpts, first_checkinapts, last_checkinapts, patch->dtv_has_video,\
                pa_diff, cost_ms, patch->sync_para.pcr_init_mode, decoder_dmxpcr_mode(), avail, acached_ms);
        } else if (VALID_PTS32(last_checkinvpts)) {
            ALOGI("%s, video available, pcrpts %x,first_vpts=%x(has_video:%d, out_vpts %x),"
                " last_vpts=%x, first-diff:pv:[%d], cost_ms %d ms, mode %d %d, avail %d",\
                __func__, pcrpts, first_checkinvpts, patch->dtv_has_video, cur_vpts, last_checkinvpts,\
                pv_diff, cost_ms, patch->sync_para.pcr_init_mode, decoder_dmxpcr_mode(), avail);
        } else {
            ALOGI("%s, av not available, pcrpts %x,(has_video:%d), cost_ms %d ms, mode %d %d, avail %d",\
                __func__, pcrpts,  patch->dtv_has_video, cost_ms, patch->sync_para.pcr_init_mode,
                decoder_dmxpcr_mode(), avail);
        }
    }
    if (aligned) {
        patch->sync_para.validpts_cnt_record = 0;
        patch->sync_para.last_pts_diff = 0;
        ALOGI("--%s, starting output, cost %d ms", __func__, cost_ms);
    }
    return aligned;
}

/* dtv avsync main loop */
void dtv_avsync_main_loop(struct audio_stream_out *stream, audio_format_t output_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)aml_dev->audio_patch;
    bool need_retuned = false, dmxpcr_valid = false, av_valid = true;
    uint32_t cost_ms = 0, start_ms, t1, t2;
    char tempbuf[PROPERTY_VALUE_MAX] = {0};
    int cached_apts = 0, target_cache, ret;
    int avail, pts_diff, max_gap = 100;

    if (!patch || aml_dev->patch_src != SRC_DTV || patch->output_thread_exit) {
        return;
    }
    start_ms = dtv_avsync_get_time_interval(&patch->sync_para.start_output_record);
    if (patch->tsync_mode != TSYNC_MODE_PCRMASTER) {
        /* amaster process */
        dtv_process_amaster_sync(stream, patch->sync_para.cur_pcrpts);
        return;
    }
    if (patch->dtv_audio_mode == 1 || patch->dtv_has_video == 0) {
        return;
    }
    if (patch->sync_para.pcr_init_mode == PCR_INIT_NONE) {
        return;
    }
    if (!VALID_PTS32(patch->last_apts) || !patch->last_apts || !VALID_PTS32(patch->last_pcrpts) || !patch->last_pcrpts) {
        return;
    }
    decoder_update_apts(patch->cur_outapts);
    get_sysfs_uint(TSYNC_CHECKIN_APTS, &patch->sync_para.cur_checkin_apts);
    get_sysfs_uint(TSYNC_CHECKIN_VPTS, &patch->sync_para.cur_checkin_vpts);
    get_sysfs_uint(TSYNC_APTS, &patch->sync_para.cur_tsync_apts);
    get_sysfs_uint(TSYNC_PCRSCR, &patch->sync_para.cur_pcrpts);
    get_sysfs_uint(TSYNC_VPTS, &patch->sync_para.cur_out_vpts);
    dmxpcr_valid = (decoder_dmxpcr_mode() && (patch->sync_para.pcr_init_mode == PCR_INIT_DMXPCR));
    patch->sync_para.cur_pts_diff = patch->last_pcrpts - patch->last_apts;
    cached_apts = patch->sync_para.cur_checkin_apts - patch->cur_outapts;
    pts_diff = patch->sync_para.cur_pts_diff;
    if (!VALID_PTS32(patch->sync_para.cur_checkin_vpts)) {
        return;
    }
    if (patch->dtv_audio_tune == AUDIO_LOOKUP || patch->dtv_audio_tune == AUDIO_DROP) {
        if (patch->dtv_audio_tune == AUDIO_LOOKUP && abs(patch->sync_para.last_pts_diff - patch->sync_para.cur_pts_diff) <
            DTV_PTS_CORRECTION_THRESHOLD * 2 && patch->sync_para.validpts_cnt_record > 1 && patch->sync_para.last_pts_diff) {
            patch->dtv_apts_lookup = (patch->sync_para.last_pts_diff + patch->sync_para.cur_pts_diff) / 2;
            patch->dtv_audio_tune = AUDIO_DROP;
            patch->ac3_pcm_dropping = false;
            ALOGI("%s, audio_lookup %d ms, -> AUDIO_DROP", __func__, patch->dtv_apts_lookup / 90);
        }
        if (patch->dtv_audio_tune == AUDIO_DROP) {
            if (dtv_drop_hold_process(stream, false, output_format)) {
                patch->dtv_audio_tune = AUDIO_LATENCY;
                ALOGI("%s, AUDIO_DROP -> AUDIO_LATENCY", __func__);
            }
        }
        if (patch->sync_para.pcrscr_jumped == PTS_NORMAL) {
            patch->sync_para.pcrscr_jumped = dtv_avsync_record_pcrscr(stream);
            if (patch->sync_para.pcrscr_jumped) {
                patch->sync_para.pcrscr_jumped = PTS_NORMAL;
                ALOGI("%s, tune state %d, reset pcrscr_jumped", __func__, patch->dtv_audio_tune);
            }
        }
        if (patch->sync_para.out_apts_jumped) {
            patch->sync_para.out_apts_jumped = PTS_NORMAL;
            ALOGI("%s, tune state %d, reset apts_jumped", __func__, patch->dtv_audio_tune);
        }
        if (patch->dtv_audio_tune == AUDIO_LATENCY) {
            get_sysfs_uint(TSYNC_PCRSCR, &patch->sync_para.cur_pcrpts);
            pts_diff = patch->sync_para.cur_pcrpts - (patch->cur_outapts - patch->sync_para.out_apts_offset);
            if (patch->sync_para.avsync_status == AVSYNC_START) {
                if (abs(pts_diff) > DTV_AUDIO_DROP_DEFAULT_THRESHOLD * 90) {
                    t2 = abs(pts_diff) - DTV_AUDIO_DROP_DEFAULT_THRESHOLD * 90;
                    if (dmxpcr_valid) {
                        t1 = decoder_get_latency();
                        if (pts_diff > 0) {
                            decoder_set_latency(t1 + t2);
                        } else {
                            decoder_set_latency(t1 - t2);
                        }
                        t2 = decoder_get_latency();
                    } else {
                        t1 = patch->sync_para.cur_pcrpts;
                        if (pts_diff > 0) {
                            patch->sync_para.cur_pcrpts -= t2;
                        } else {
                            patch->sync_para.cur_pcrpts += t2;
                        }
                        t2 = patch->sync_para.cur_pcrpts;
                        decoder_set_pcrscr(t2);
                    }
                    ALOGI("%s, avsync_start, dmxpcr_valid %d, pcrscr jumped %d ms",
                        __func__, dmxpcr_valid, (int)(t1 - t2) / 90);
                    get_sysfs_uint(TSYNC_PCRSCR, &patch->sync_para.cur_pcrpts);
                }
            } else if (patch->sync_para.avsync_status == AVSYNC_RESTART) {
                if (abs(pts_diff) > patch->sync_para.avsync_retune_threshold * 10) {
                    if (!dmxpcr_valid) {
                        t1 = patch->sync_para.cur_pcrpts;
                        patch->sync_para.cur_pcrpts = (patch->cur_outapts - patch->sync_para.out_apts_offset);
                        if (pts_diff > 0) {
                            patch->sync_para.cur_pcrpts -= TIME_UNIT90K / 2;
                        } else {
                            patch->sync_para.cur_pcrpts += TIME_UNIT90K / 2;
                        }
                        t2 = patch->sync_para.cur_pcrpts;
                        decoder_set_pcrscr(t2);
                        ALOGI("%s, avsync_restart, dmxpcr_valid %d, pcrscr jumped %d ms",
                            __func__, dmxpcr_valid, (int)(t1 - t2) / 90);
                    } else {
                        ALOGI("%s, avsync_restart, dmxpcr_valid %d, pts_diff %d ms, latency %d ms",
                            __func__, dmxpcr_valid, pts_diff / 90, (int)decoder_get_latency() / 90);
                    }
                }
            }
            if (patch->ac3_pcm_dropping) {
                patch->sync_para.audio_drop_sum = 0;
                patch->ac3_pcm_dropping = false;
            }
            if (patch->sync_para.avsync_status == AVSYNC_START) {
                ALOGI("%s patch_fmt %x, hal_fmt %x sink_fmt %x port %x, offset %d ms", __func__,
                    patch->aformat, aml_out->hal_internal_format, aml_dev->sink_format,
                    aml_dev->cur_out_devices, patch->sync_para.out_apts_offset / 90);
            }
            patch->sync_para.avsync_status = AVSYNC_RUNNING;
            patch->dtv_audio_tune = AUDIO_RUNNING;
        }
        if (patch->tsync_pcr_debug) {
            ALOGI("%s, tune state %d, diff %d ms, acached %d ms",
                __func__, patch->dtv_audio_tune, pts_diff / 90, cached_apts / 90);
        }
        goto exit;
    }
    if (patch->dtv_audio_tune == AUDIO_RUNNING && patch->sync_para.avsync_status) {
        target_cache = patch->sync_para.tsync_latency + patch->sync_para.default_acache_offset;
        if (patch->sync_para.cur_checkin_apts > patch->cur_outapts) {
            cached_apts = patch->sync_para.cur_checkin_apts - patch->cur_outapts;
        }
        if (patch->sync_para.pcrscr_jumped == PTS_NORMAL) {
            patch->sync_para.pcrscr_jumped = dtv_avsync_record_pcrscr(stream);
            if (patch->sync_para.pcrscr_jumped) {
                if (patch->ac3_pcm_dropping) {
                    patch->ac3_pcm_dropping = false;
                }
                get_sysfs_uint(TSYNC_PCRSCR, &patch->sync_para.cur_pcrpts);
            }
        }
        if (patch->sync_para.out_apts_jumped || patch->sync_para.pcrscr_jumped ||
            patch->sync_para.audio_pause_resumed) {
            if (patch->sync_para.avsync_retune) {
                patch->sync_para.avsync_retune = 0;
            }
            need_retuned = true;
        }
        // audio pause resumed;
        if (patch->sync_para.audio_pause_resumed) {
            pts_diff = patch->sync_para.cur_pcrpts - (patch->cur_outapts - patch->sync_para.out_apts_offset);
            if (patch->tsync_pcr_debug) {
                ALOGI("%s, audio_resume process, cur_pcr %x, apts %x, diff %d ms, acached %d ms", __func__,
                    patch->sync_para.cur_pcrpts, patch->cur_outapts, pts_diff / 90, cached_apts / 90);
            }
            if (DIRECT_NORMAL != patch->pll_state) {
                //dtv_adjust_output_clock(stream, DIRECT_NORMAL, DEFAULT_DTV_ADJUST_CLOCK);
            }
            if (patch->sync_para.validpts_cnt_record > 0) {
                ALOGI("%s, pause resumed, cur_pcr %x, apts %x, diff %d ms, acached %d ms", __func__,
                    patch->sync_para.cur_pcrpts, patch->cur_outapts, pts_diff / 90, cached_apts / 90);
                patch->sync_para.audio_pause_resumed = 0;
                patch->ac3_pcm_dropping = false;
                patch->sync_para.out_apts_jumped = 0;
                patch->sync_para.pcrscr_jumped = 0;
            } else {
                goto exit;
            }
        }
        // apts stuttered;
        if (patch->sync_para.out_apts_jumped) {
            ALOGI("%s, apts_jumped, cur_diff %d ms", __func__, patch->sync_para.cur_pts_diff / 90);
            if (dtv_process_apts_discontinue(stream)) {
                patch->sync_para.out_apts_jumped = 0;
                cached_apts = patch->sync_para.cur_checkin_apts - patch->cur_outapts;
                ALOGI("%s, out_apts_jumped finished, cached_apts %d ms", __func__, cached_apts / 90);
            }
            if (patch->sync_para.out_apts_jumped) {
                cost_ms = dtv_avsync_get_time_interval(&patch->sync_para.outapts_jumped_record);
                if (cost_ms > DEFAULT_AV_THRESHOLD) {
                    ALOGI("%s, timeout clean out_apts_jumped %s -> PTS_NORMAL", __func__,
                        patch->sync_para.out_apts_jumped == PTS_FF_JUMPED ? "PTS_FF_JUMPED" : "PTS_FB_JUMPED");
                    patch->sync_para.out_apts_jumped = 0;
                }
            }
            get_sysfs_uint(TSYNC_PCRSCR, &patch->sync_para.cur_pcrpts);
        }
        // pcrscr stuttered;
        if (patch->sync_para.pcrscr_jumped) {
            pts_diff = patch->sync_para.cur_pcrpts - (patch->cur_outapts - patch->sync_para.out_apts_offset);
            cached_apts = patch->sync_para.cur_checkin_apts - patch->cur_outapts;
            if (abs(pts_diff) < AUDIO_PTS_DISCONTINUE_THRESHOLD &&
                dtv_avsync_valid_diff_pts32(patch->sync_para.cur_checkin_apts, patch->cur_outapts)) {
                if (patch->tsync_pcr_debug) {
                    ALOGI("%s, process_discontinue, pts_diff %d ms, cached[%d->%d]", __func__,
                        pts_diff / 90, cached_apts / 90, target_cache / 90);
                }
                if (dtv_drop_hold_process(stream, true, output_format)) {
                    if (patch->sync_para.pcrscr_jumped) {
                        patch->sync_para.pcrscr_jumped = 0;
                    }
                    ALOGI("%s, process pcrscr_jumped finished", __func__);
                }
            }
            if (patch->sync_para.pcrscr_jumped) {
                cost_ms = dtv_avsync_get_time_interval(&patch->sync_para.pcrpts_jumped_record);
                if (cost_ms > DEFAULT_AV_THRESHOLD) {
                    ALOGI("%s, timeout clean pcrscr_jumped %s -> PTS_NORMAL", __func__,
                        patch->sync_para.pcrscr_jumped == PTS_FF_JUMPED ? "PTS_FF_JUMPED" : "PTS_FB_JUMPED");
                    patch->sync_para.pcrscr_jumped = 0;
                    if (patch->ac3_pcm_dropping) {
                        patch->ac3_pcm_dropping = 0;
                        patch->sync_para.audio_drop_sum = 0;
                        ALOGI("%s, timeout, clean ac3_pcm_dropping", __func__);
                    }
                }
            }
            get_sysfs_uint(TSYNC_PCRSCR, &patch->sync_para.cur_pcrpts);
        }
        /* apts process */
        cost_ms = dtv_avsync_get_time_interval(&patch->sync_para.main_loop_record);
        if (need_retuned) {
            get_sysfs_uint(TSYNC_LAST_CHECKIN_APTS, &patch->sync_para.cur_checkin_apts);
            get_sysfs_uint(TSYNC_PCRSCR, &patch->sync_para.cur_pcrpts);
            pts_diff = patch->sync_para.cur_pcrpts - (patch->cur_outapts - patch->sync_para.out_apts_offset);
            patch->sync_para.cur_pts_diff = pts_diff;
            need_retuned = false;
        } else {
            pts_diff = (patch->sync_para.cur_pts_diff + patch->sync_para.last_pts_diff) / 2;
        }
        if (patch->sync_para.pcrscr_jumped == 0 && patch->sync_para.out_apts_jumped == 0 &&
            dtv_avsync_valid_diff_pts32(patch->sync_para.cur_checkin_apts, patch->cur_outapts)) {
            if ((abs(pts_diff) > patch->sync_para.avsync_retune_threshold || patch->sync_para.avsync_retune) &&
                (abs(pts_diff) < AUDIO_PTS_DISCONTINUE_THRESHOLD)) {
                cached_apts = patch->sync_para.cur_checkin_apts - patch->cur_outapts;
                if (cost_ms > (uint32_t)max_gap && cached_apts < patch->sync_para.tsync_latency / 3) {
                    ALOGI("%s, acached[%d,%d] ms, diff[%d] ms, cost[%d] ms, hold_process", __func__,
                        cached_apts / 90, target_cache / 90, pts_diff / 90, cost_ms);
                    dtv_drop_hold_process(stream, true, output_format);
                    patch->sync_para.avsync_retune = 0;
                } else if (patch->sync_para.avsync_retune == 0) {
                    if (pts_diff > 0) {
                        patch->sync_para.avsync_retune = PTS_FF_JUMPED;
                    } else {
                        patch->sync_para.avsync_retune = PTS_FB_JUMPED;
                    }
                    ALOGI("%s, acached[%d,%d] ms, diff[cur %d,last %d] ms, cost[%d] ms", __func__, cached_apts / 90,
                        target_cache / 90, pts_diff / 90, patch->sync_para.last_pts_diff / 90, cost_ms);
                    clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.audio_tune_record);
                } else {
                    cost_ms = dtv_avsync_get_time_interval(&patch->sync_para.audio_tune_record);
                    cached_apts = patch->sync_para.cur_checkin_apts - patch->cur_outapts;
                    if (!need_retuned) {
                        if (patch->sync_para.avsync_retune == PTS_FF_JUMPED) {
                            if (pts_diff < 0 || patch->sync_para.cur_pts_diff < 0) {
                                patch->sync_para.avsync_retune = 0;
                            }
                        } else if (patch->sync_para.avsync_retune == PTS_FB_JUMPED) {
                            if (pts_diff > 0 || patch->sync_para.cur_pts_diff > 0) {
                                patch->sync_para.avsync_retune = 0;
                            }
                        }
                    }
                    need_retuned = true;
                    if (pts_diff < 0) {
                        if (cached_apts < patch->sync_para.tsync_latency / 3) {
                            need_retuned = false;
                        }
                    } else if (patch->sync_para.avsync_retune == 0) {
                        need_retuned = false;
                    }
                    if (patch->tsync_pcr_debug) {
                        ALOGI("%s, pts_diff %d ms, acached %d ms", __func__,
                            pts_diff / 90, cached_apts / 90);
                    }
                    if (cost_ms > (uint32_t)max_gap && need_retuned) {
                        ret = dtv_process_apts_tuning(stream);
                        if (ret) {
                            clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.audio_tune_record);
                        }
                    }
                    if ((abs(pts_diff) > patch->sync_para.avsync_retune_threshold && cost_ms > TIME_UNIT90K / 90) ||
                        (abs(pts_diff) > patch->sync_para.avsync_retune_threshold * 10)) {
                        patch->sync_para.avsync_retune = 0;
                        ALOGI("%s, retuned, cur_diff %d ms, acached[%d,%d]ms, cost %d ms", __func__,
                            pts_diff / 90, cached_apts / 90, target_cache / 90, cost_ms);
                        need_retuned = true;
                    } else if (dmxpcr_valid && cached_apts / 90 > patch->sync_para.max_apts_cache) {
                        patch->sync_para.avsync_retune = 0;
                        ALOGI("%s, retuned dmxpcr mode, cur_diff %d ms, acached[%d,%d]ms, cost %d ms", __func__,
                            pts_diff / 90, cached_apts / 90, target_cache / 90, cost_ms);
                        need_retuned = true;
                    } else {
                        need_retuned = false;
                    }
                }
            } else {
                patch->sync_para.avsync_retune = 0;
            }
        }
    }
    if (need_retuned) {
        ALOGI("%s, AUDIO_RUNNING -> AUDIO_BREAK", __func__);
        patch->dtv_audio_tune = AUDIO_BREAK;
        patch->dtv_disable_tune_latency = 0;
        patch->sync_para.avsync_status = 0;
        patch->sync_para.avsync_retune = 0;
        patch->sync_para.pcrscr_jumped = PTS_NORMAL;
        patch->sync_para.out_apts_jumped = PTS_NORMAL;
        patch->sync_para.discontinue_apts = patch->cur_outapts;
        patch->sync_para.last_pts_diff = 0;
        clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.apts_discontinue_record);
    }
exit:
    patch->sync_para.last_pts_diff = patch->sync_para.cur_pts_diff;
    patch->sync_para.last_pcrpts = patch->sync_para.cur_pcrpts;
    clock_gettime(CLOCK_MONOTONIC, &patch->sync_para.main_loop_record);
}

int get_tsync_pcr_debug(void)
{
    char tempbuf[128] = {0};
    int debug = 0, ret;
    ret = aml_sysfs_get_str(TSYNC_PCR_DEBUG, tempbuf, sizeof(tempbuf));
    if (ret > 0) {
        ret = sscanf(tempbuf, "%d\n", &debug);
    }
    if (ret > 0 && debug > 0) {
        return debug;
    } else {
        debug = 0;
    }
    return debug;
}

void set_video_delay(int delay_ms)
{
    char tempbuf[128] = {0};

    if (delay_ms < -100 || delay_ms > 500) {
        ALOGE("set_video_delay out of range[-100 - 500] %d\n", delay_ms);
        return;
    }
    sprintf(tempbuf, "%d", delay_ms);
    if (aml_sysfs_set_str(TSYNC_VPTS_ADJ, tempbuf) == -1) {
        ALOGE("set_video_delay %s\n", tempbuf);
    }
    return;
}
 int dtv_set_audio_latency(int apts_diff,struct aml_audio_patch* patch)
{
    int ret = 0, diff = 0;
    char buff[32] = {'\0'};

    /*[SE][BUG][SWPL-14828][chengshun.wang] add property
     * to set start latency
     */
    int audio_latency = DEMUX_PCR_APTS_LATENCY;
    int delay_ms = property_get_int32("vendor.media.audio.latencyms", 300);
    if (delay_ms * 90 > audio_latency) {
        audio_latency = delay_ms * 90;
    }

    if (apts_diff == 0) {
        ret = aml_sysfs_get_str(TSYNC_APTS_DIFF, buff, sizeof(buff));
        if (ret > 0) {
            ret = sscanf(buff, "%d\n", &diff);
            if (ret < 0) {
                return 0;
            }
        }
        if (diff > DECODER_PTS_DEFAULT_LATENCY) {
            diff = DECODER_PTS_DEFAULT_LATENCY;
        }
        apts_diff = diff;
    }
    ALOGI("dtv_set_audio_latency: audio_latency=%d, apts_diff=%d", audio_latency, apts_diff);

    if (patch->dtv_disable_tune_latency) {
        return apts_diff;
    }

    if (apts_diff < audio_latency && apts_diff > 0) {
        decoder_set_latency(audio_latency - apts_diff);
    } else {
        decoder_set_latency(audio_latency);
    }
    return apts_diff;
}

 bool dtv_firstapts_lookup_over(struct aml_audio_patch *patch, struct aml_audio_device *aml_dev, bool a_discontinue, int *apts_diff)
{
    char buff[32];
    int ret;
    unsigned int first_checkinapts = 0xffffffff;
    unsigned int last_checkinapts = 0xffffffff;
    unsigned int last_checkinvpts = 0xffffffff;
    unsigned int first_checkinvpts = 0xffffffff;
    unsigned int cur_vpts = 0xffffffff;
    unsigned int first_vpts = 0xffffffff;
    unsigned int demux_pcr = 0xffffffff;
    unsigned int pcr_inited = 0;
    int first_checkin_av_diff = 0;
    int first_out_av_diff = 0;

    if (!patch || !aml_dev) {
        return true;
    }

    if (dtv_get_tsync_mode() == TSYNC_MODE_PCRMASTER && get_dtv_pcr_sync_mode() == 0) {
        ret = get_sysfs_uint(TSYNC_PCR_INITED, &pcr_inited);
        if (ret == 0 && pcr_inited != 0) {
            ALOGI("pcr_already inited=0x%x\n", pcr_inited);
        } else {
            ALOGI("ret = %d, pcr_inited=%x\n",ret, pcr_inited);
            return false;
        }
    }
    patch->tsync_mode = dtv_get_tsync_mode();
    get_sysfs_uint(TSYNC_PCRSCR, &demux_pcr);

    if (a_discontinue) {
        get_sysfs_uint(TSYNC_LAST_DISCONTINUE_CHECKIN_APTS, &first_checkinapts);
    } else {
        get_sysfs_uint(TSYNC_FIRSTCHECKIN_APTS, &first_checkinapts);
    }

    if (get_tsync_pcr_debug()) {
        get_sysfs_uint(TSYNC_FIRSTCHECKIN_VPTS, &first_checkinvpts);
        get_sysfs_uint(TSYNC_FIRST_VPTS, &first_vpts);
        get_sysfs_uint(TSYNC_LAST_CHECKIN_APTS, &last_checkinapts);
        get_sysfs_uint(TSYNC_LAST_CHECKIN_VPTS, &last_checkinvpts);
        first_checkin_av_diff = (int)(first_checkinapts - first_checkinvpts) / 90;
        first_out_av_diff = (int)(first_checkinapts - first_vpts) / 90;
        ALOGI("demux_pcr %x first_checkinapts %x,last_checkinapts=%x,first_checkinvpts=%x,first_vpts:0x%x(has_video:%d),"
               " last_checkinvpts=%x, discontinue %d, apts_diff=%d, first_checkin_av_diff: %d ms, first_out_av_diff: %d ms",\
               demux_pcr, first_checkinapts, last_checkinapts, first_checkinvpts, first_vpts, patch->dtv_has_video,\
               last_checkinvpts, a_discontinue, *apts_diff, first_checkin_av_diff, first_out_av_diff);
    }

    if (dtv_get_tsync_mode() == TSYNC_MODE_AMASTER) {
       unsigned int videostarted = 0;
       struct timespec curtime;
       int costtime_ms = 0;
       int timeout = property_get_int32("vendor.media.audio.timecostms", 3000);

       clock_gettime(CLOCK_MONOTONIC, &curtime);
       costtime_ms = calc_time_interval_us(&start_time, &curtime) / 1000;
       get_sysfs_uint(TSYNC_VIDEO_STARTED, &videostarted);
       ALOGI("videostarted:%d , costtime:%d.", videostarted, costtime_ms);

       if (patch->dtv_has_video && videostarted == 0 && costtime_ms < timeout) {
           ALOGI("videostarted is 0.");
           return false;
       } else
           return true;
    }

    if ((first_checkinapts != 0xffffffff) && (demux_pcr != 0xffffffff)) {
        if (demux_pcr == 0 && first_checkinapts != 0 && last_checkinapts != 0) {
            ALOGI("demux pcr not set, wait, tsync_mode=%d, use_tsdemux_pcr=%d\n", dtv_get_tsync_mode(), get_dtv_pcr_sync_mode());
            return false;
        }
        if (first_checkinapts > demux_pcr) {
            unsigned diff = first_checkinapts - demux_pcr;
            if (diff < AUDIO_PTS_DISCONTINUE_THRESHOLD &&
                dtv_get_tsync_mode() == TSYNC_MODE_PCRMASTER) {
                //not return false in AMASTER mode
                return false;
            }
        } else {
            unsigned diff = demux_pcr - first_checkinapts;
            aml_dev->dtv_droppcm_size = diff * 48 * 2 * 2 / 90;
            ALOGI("now must drop size %d\n", aml_dev->dtv_droppcm_size);
        }
    }
    get_sysfs_uint(TSYNC_FIRSTCHECKIN_VPTS, &first_checkinvpts);
    get_sysfs_uint(TSYNC_FIRST_VPTS, &first_vpts);
    get_sysfs_uint(TSYNC_LAST_CHECKIN_APTS, &last_checkinapts);
    get_sysfs_uint(TSYNC_LAST_CHECKIN_VPTS, &last_checkinvpts);
    get_sysfs_uint(TSYNC_VPTS, &cur_vpts);
    first_checkin_av_diff = (int)(first_checkinapts - first_checkinvpts) / 90;
    first_out_av_diff = (int)(first_checkinapts - first_vpts) / 90;
    ALOGI("++ demux_pcr %x first_checkinapts %x,last_checkinapts=%x,first_checkinvpts=%x,first_vpts:0x%x(has_video:%d),"
          " last_checkinvpts=%x,cur_vpts %x,discontinue %d,apts_diff=%d,first_checkin_av_diff: %d ms,first_out_av_diff: %d ms",\
          demux_pcr, first_checkinapts, last_checkinapts, first_checkinvpts, first_vpts, patch->dtv_has_video,\
          last_checkinvpts, cur_vpts, a_discontinue, *apts_diff, first_checkin_av_diff, first_out_av_diff);

    return true;
}

unsigned long dtv_hal_get_pts(struct audio_stream_out *stream, unsigned int latency)

{
    struct aml_stream_out *stream_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = stream_out->dev;
    struct aml_audio_patch *patch = aml_dev->audio_patch;
    unsigned long val;
    unsigned long pts = 0;
    unsigned long long frame_nums;
    unsigned long delay_pts;
    unsigned int checkin_firstapts = 0;
    char value[PROPERTY_VALUE_MAX];
    uint32_t offset = 0;

    if (aml_dev->is_multi_demux && !property_get_bool("vendor.dtv.use_tsync_check",false)) {
        if (aml_audio_swcheck_lookup_apts(0,patch->decoder_offset,&pts) == -1) {
            pts = 0;
        }
    } else {
         pts = decoder_apts_lookup((unsigned int)patch->decoder_offset + patch->input_skipped_bytes);
    }
    if (patch->dtv_first_apts_flag == 0) {
       get_sysfs_uint(DTV_DECODER_CHECKIN_FIRSTAPTS_PATH, &checkin_firstapts);
        pts = checkin_firstapts;
        ALOGI("pts = 0,so get checkin_firstapts:0x%lx", pts);
        patch->last_valid_pts = pts;
        patch->outlen_after_last_validpts = 0;
        patch->sync_para.last_validpts_duration = 0;
        ALOGI("first apts looked=0x%lx\n", pts);
        dtv_avsync_record_out_apts(stream, pts);
        patch->sync_para.validpts_cnt_record++;
        return pts;
    }

    if (pts == 0 || pts == patch->last_valid_pts) {
        if (patch->last_valid_pts) {
            pts = patch->last_valid_pts;
        }
        if (patch->sync_para.last_validpts_duration > 0) {
            pts = patch->last_valid_pts + patch->sync_para.last_validpts_duration;
        } else {
            frame_nums = (patch->outlen_after_last_validpts / (DEFAULT_DATA_WIDTH * DEFAULT_CHANNELS));
            pts += (frame_nums * 90 / DEFAULT_SAMPLERATE);
        }
        if (aml_dev->debug_flag || patch->tsync_pcr_debug > 1) {
            offset = patch->decoder_offset + patch->input_skipped_bytes;
            ALOGI("get_pts:%lx,offset:%x,last_valid_pts %lx, outlen %d, dur %d, lan %d",
                pts, offset, patch->last_valid_pts, patch->outlen_after_last_validpts,
                patch->sync_para.last_validpts_duration, latency);
        }
    } else {
        dtv_avsync_record_out_apts(stream, pts);
        patch->sync_para.validpts_cnt_record++;
        patch->sync_para.last_validpts_duration = 0;
        patch->last_valid_pts = pts;
        patch->outlen_after_last_validpts = 0;
        if (aml_dev->debug_flag || patch->tsync_pcr_debug > 1) {
            offset = patch->decoder_offset + patch->input_skipped_bytes;
            ALOGI("====get_pts:%lx offset:%x lan %d\n",
                pts, offset, latency);
        }
    }
    val = pts - latency * 90;
    patch->cur_outapts = val;
    return val;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *)stream;
    snd_pcm_sframes_t frames = out_get_latency_frames(stream);
    return (frames * 1000) / out->config.rate;
}

static unsigned int compare_clock(unsigned int clock1, unsigned int clock2, unsigned int factor)
{
    if (clock1 == clock2) {
        return true;
    }
    if (clock1 > clock2) {
        if (clock1 < clock2 + 60 * factor) {
            return true;
        }
    }
    if (clock1 < clock2) {
        if (clock2 < clock1 + 60 * factor) {
            return true;
        }
    }
    return false;
}

unsigned int dtv_get_i2s_output_clock(struct aml_audio_patch* patch) {
    struct audio_hw_device *adev = patch->dev;
    struct aml_audio_device * aml_dev = (struct aml_audio_device*)adev;
    struct aml_mixer_handle * handle = &(aml_dev->alsa_mixer);
    return aml_mixer_ctrl_get_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL);
}

void dtv_adjust_i2s_output_clock(struct aml_audio_patch* patch, int direct, int step)
{
    struct audio_hw_device *adev = patch->dev;
    struct aml_audio_device * aml_dev = (struct aml_audio_device*)adev;
    struct aml_mixer_handle * handle = &(aml_dev->alsa_mixer);
    int output_clock = 0;
    unsigned int i2s_current_clock = 0;
    i2s_current_clock = aml_mixer_ctrl_get_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL);
    if (i2s_current_clock > DEFAULT_I2S_OUTPUT_CLOCK * 4 ||
        i2s_current_clock == 0 || step <= 0 || step > DEFAULT_DTV_OUTPUT_CLOCK) {
        return;
    }
    if (get_tsync_pcr_debug())
        ALOGI("current:%d, default:%d\n", i2s_current_clock, patch->dtv_default_i2s_clock);
    if (direct == DIRECT_SPEED) {
        if (i2s_current_clock >= patch->dtv_default_i2s_clock) {
            if (i2s_current_clock - patch->dtv_default_i2s_clock >=
                (patch->dtv_default_i2s_clock * DEFAULT_DTV_ADJUST_CLOCK_THRESHOLD / 100)) {
                ALOGI("already > i2s_step_clk 1M,no need speed adjust\n");
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + step;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
        } else {
            int value = patch->dtv_default_i2s_clock - i2s_current_clock;
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + value;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
        }
    } else if (direct == DIRECT_SLOW) {
        if (i2s_current_clock <= patch->dtv_default_i2s_clock) {
            if (patch->dtv_default_i2s_clock - i2s_current_clock >
                (patch->dtv_default_i2s_clock * DEFAULT_DTV_ADJUST_CLOCK_THRESHOLD / 100)) {
                ALOGI("already < 1M no need adjust slow, return\n");
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - step;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
        } else {
            int value = i2s_current_clock - patch->dtv_default_i2s_clock;
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - value;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - step;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
        }
    } else {
        if (compare_clock(i2s_current_clock, patch->dtv_default_i2s_clock, 1)) {
            return ;
        }
        if (i2s_current_clock > patch->dtv_default_i2s_clock) {
            int value = i2s_current_clock - patch->dtv_default_i2s_clock;
            if (value < 60) {
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - value;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
        } else if (i2s_current_clock < patch->dtv_default_i2s_clock) {
            int value = patch->dtv_default_i2s_clock - i2s_current_clock;
            if (value < 60) {
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + value;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
        }
    }
    return;
}

void dtv_adjust_earc_output_clock(struct aml_audio_patch* patch, int direct, int step)
{
    struct audio_hw_device *adev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) adev;
    struct aml_mixer_handle * handle = &(aml_dev->alsa_mixer);
    int output_clock, i, compare_factor = 1;
    unsigned int earc_current_clock = 0;
    unsigned int earc_default_clock = 0;
    eMixerCtrlID mixerID = AML_MIXER_ID_CHANGE_EARC_PLL;
    int device_index = alsa_device_update_pcm_index(PORT_EARC, PLAYBACK);

    if (device_index == -1) {
        return;
    }

    earc_current_clock = aml_mixer_ctrl_get_int(handle, mixerID);

    int audio_type = aml_mixer_ctrl_get_int(handle, AML_MIXER_ID_EARC_TX_AUDIO_TYPE);

    if (audio_type == AML_AUDIO_CODING_TYPE_STEREO_LPCM ||
        audio_type == AML_AUDIO_CODING_TYPE_AC3 ||
        audio_type == AML_AUDIO_CODING_TYPE_AC3_LAYOUT_B ||
        audio_type == AML_AUDIO_CODING_TYPE_DTS) {
        patch->dtv_default_arc_clock = DEFAULT_EARC_OUTPUT_CLOCK;
        compare_factor = 5;
    } else if (audio_type == AML_AUDIO_CODING_TYPE_EAC3){
        patch->dtv_default_arc_clock = DEFAULT_EARC_OUTPUT_CLOCK * 4;
        compare_factor = 5 * 4;
        step *= 4;
    } else if (audio_type == AML_AUDIO_CODING_TYPE_MLP ||
        audio_type == AML_AUDIO_CODING_TYPE_DTS_HD ||
        audio_type == AML_AUDIO_CODING_TYPE_DTS_HD_MA) {
         patch->dtv_default_arc_clock = DEFAULT_EARC_OUTPUT_CLOCK * 4 * 4;
         compare_factor = 5 * 4 * 4;
         step *= 16;
    }
    if (aml_audio_get_debug_flag())
        ALOGI("dtv_adjust_earc_output_clock direct %d step %d spdif_current_clock %u",direct, step, earc_current_clock);
    if (earc_current_clock > DEFAULT_EARC_OUTPUT_CLOCK * 4 * 4 ||
        earc_current_clock == 0 || step <= 0 || step > DEFAULT_DTV_OUTPUT_CLOCK) {
        return;
    }
    if (direct == DIRECT_SPEED) {
        if (compare_clock(earc_current_clock, patch->dtv_default_arc_clock, compare_factor)) {
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + step / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            if (aml_audio_get_debug_flag())
                ALOGI("arc_clock 1 set %d to %d",earc_current_clock,aml_mixer_ctrl_get_int(handle, mixerID));
        } else if (earc_current_clock < patch->dtv_default_arc_clock) {
            int value = patch->dtv_default_arc_clock - earc_current_clock;
            if (value > DEFAULT_DTV_OUTPUT_CLOCK) {
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + value / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + step / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
           if (aml_audio_get_debug_flag())
                ALOGI("arc_clock 2 set %d to %d",earc_current_clock,aml_mixer_ctrl_get_int(handle, mixerID));

        } else {
            if (aml_audio_get_debug_flag())
                ALOGI("arc_SPEED clk %d,default %d",earc_current_clock,patch->dtv_default_arc_clock);
            return ;
        }
    } else if (direct == DIRECT_SLOW) {
        if (compare_clock(earc_current_clock, patch->dtv_default_arc_clock, compare_factor)) {
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - step / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            if (aml_audio_get_debug_flag())
                ALOGI("arc_clock 3 set %d to %d",earc_current_clock,aml_mixer_ctrl_get_int(handle, mixerID));
        } else if (earc_current_clock > patch->dtv_default_arc_clock) {
            int value = earc_current_clock - patch->dtv_default_arc_clock;
            if (value > DEFAULT_DTV_OUTPUT_CLOCK) {
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - value / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - step / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            if (aml_audio_get_debug_flag())
                ALOGI("arc_clock 4 set %d to %d",earc_current_clock,aml_mixer_ctrl_get_int(handle, mixerID));
        } else {
            if (aml_audio_get_debug_flag())
                ALOGI("arc_SLOW clk %d,default %d",earc_current_clock,patch->dtv_default_arc_clock);
            return ;
        }
    } else {
        if (compare_clock(earc_current_clock, patch->dtv_default_arc_clock, compare_factor)) {
            return ;
        }
        if (earc_current_clock > patch->dtv_default_arc_clock) {
            int value = earc_current_clock - patch->dtv_default_arc_clock;
            if (value < 60 || value > DEFAULT_DTV_OUTPUT_CLOCK) {
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - value / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            if (aml_audio_get_debug_flag())
                ALOGI("arc_clock 5 set %d to %d",earc_current_clock,aml_mixer_ctrl_get_int(handle, mixerID));
        } else if (earc_current_clock < patch->dtv_default_arc_clock) {
            int value = patch->dtv_default_arc_clock - earc_current_clock;
            if (value < 60 || value > DEFAULT_DTV_OUTPUT_CLOCK) {
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + value / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            if (aml_audio_get_debug_flag())
                ALOGI("arc_clock 6 set %d to %d",earc_current_clock,aml_mixer_ctrl_get_int(handle, mixerID));
        } else {
            return ;
        }
    }
}

void dtv_adjust_spdif_output_clock(struct aml_audio_patch* patch, int direct, int step, bool spdifb)
{
    struct audio_hw_device *adev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) adev;
    struct aml_mixer_handle * handle = &(aml_dev->alsa_mixer);
    int output_clock, i;
    unsigned int spdif_current_clock = 0;

    if (aml_dev->bHDMIARCon && spdifb) {
        dtv_adjust_earc_output_clock(patch, direct, patch->arc_step_clk / patch->i2s_div_factor);
        return;
    }

    eMixerCtrlID mixerID = spdifb ? AML_MIXER_ID_CHANGE_SPDIFB_PLL : AML_MIXER_ID_CHANGE_SPDIF_PLL;
    spdif_current_clock = aml_mixer_ctrl_get_int(handle, mixerID);
    ALOGI("dtv_adjust_spdif_output_clock direct %d step %d spdifb %d spdif_current_clock %u",direct, step, spdifb, spdif_current_clock);
    if (spdif_current_clock > DEFAULT_SPDIF_PLL_DDP_CLOCK * 4 ||
        spdif_current_clock == 0 || step <= 0 || step > DEFAULT_DTV_OUTPUT_CLOCK) {
        return;
    }
    if (direct == DIRECT_SPEED) {
        if (compare_clock(spdif_current_clock, patch->dtv_default_spdif_clock, 1)) {
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + step / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            if (aml_audio_get_debug_flag())
                ALOGI("spdif_clock 1 set %d to %d",spdif_current_clock,aml_mixer_ctrl_get_int(handle, AML_MIXER_ID_CHANGE_SPDIF_PLL));
        } else if (spdif_current_clock < patch->dtv_default_spdif_clock) {
            int value = patch->dtv_default_spdif_clock - spdif_current_clock;
            if (value > DEFAULT_DTV_OUTPUT_CLOCK) {
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + value / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + step / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            if (aml_audio_get_debug_flag())
                ALOGI("spdif_clock 2 set %d to %d",spdif_current_clock,aml_mixer_ctrl_get_int(handle, AML_MIXER_ID_CHANGE_SPDIF_PLL));

        } else {
            if (aml_audio_get_debug_flag())
                ALOGI("spdif_SPEED clk %d,default %d",spdif_current_clock,patch->dtv_default_spdif_clock);
            return ;
        }
    } else if (direct == DIRECT_SLOW) {
        if (compare_clock(spdif_current_clock, patch->dtv_default_spdif_clock, 1)) {
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - step / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            if (aml_audio_get_debug_flag())
                ALOGI("spdif_clock 3 set %d to %d",spdif_current_clock,aml_mixer_ctrl_get_int(handle, AML_MIXER_ID_CHANGE_SPDIF_PLL));
        } else if (spdif_current_clock > patch->dtv_default_spdif_clock) {
            int value = spdif_current_clock - patch->dtv_default_spdif_clock;
            if (value > DEFAULT_DTV_OUTPUT_CLOCK) {
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - value / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - step / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            if (aml_audio_get_debug_flag())
                ALOGI("spdif_clock 4 set %d to %d",spdif_current_clock,aml_mixer_ctrl_get_int(handle, AML_MIXER_ID_CHANGE_SPDIF_PLL));
        } else {
            if (aml_audio_get_debug_flag())
                ALOGI("spdif_SLOW clk %d,default %d",spdif_current_clock,patch->dtv_default_spdif_clock);
            return ;
        }
    } else {
        if (compare_clock(spdif_current_clock, patch->dtv_default_spdif_clock, 1)) {
            return ;
        }
        if (spdif_current_clock > patch->dtv_default_spdif_clock) {
            int value = spdif_current_clock - patch->dtv_default_spdif_clock;
            if (value < 60 || value > DEFAULT_DTV_OUTPUT_CLOCK) {
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - value / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            if (aml_audio_get_debug_flag())
                ALOGI("spdif_clock 5 set %d to %d",spdif_current_clock,aml_mixer_ctrl_get_int(handle, AML_MIXER_ID_CHANGE_SPDIF_PLL));
        } else if (spdif_current_clock < patch->dtv_default_spdif_clock) {
            int value = patch->dtv_default_spdif_clock - spdif_current_clock;
            if (value < 60 || value > DEFAULT_DTV_OUTPUT_CLOCK) {
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + value / DEFAULT_SPDIF_ADJUST_TIMES;
            for (i = 0; i < DEFAULT_SPDIF_ADJUST_TIMES; i++) {
                aml_mixer_ctrl_set_int(handle, mixerID, output_clock);
            }
            if (aml_audio_get_debug_flag())
                ALOGI("spdif_clock 6 set %d to %d",spdif_current_clock,aml_mixer_ctrl_get_int(handle, AML_MIXER_ID_CHANGE_SPDIF_PLL));
        } else {
            return ;
        }
    }
}

void dtv_adjust_output_clock(struct aml_audio_patch * patch, int direct, int step, bool dual)
{
    struct audio_hw_device *adev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) adev;
    bool spdif_b = dual;
    if (aml_audio_get_debug_flag())
        ALOGI("dtv_adjust_output_clock not set,%" PRIx64 ",%x",patch->decoder_offset,patch->dtv_pcm_readed);
    if (!aml_dev || step <= 0 || patch->dtv_audio_mode) {
        return;
    }
    if (patch->decoder_offset < 512 * 2 * 10 &&
        ((patch->aformat == AUDIO_FORMAT_AC3) ||
         (patch->aformat == AUDIO_FORMAT_E_AC3))) {
        return;
    }
    if (patch->dtv_default_spdif_clock > DEFAULT_I2S_OUTPUT_CLOCK * 4 ||
        patch->dtv_default_spdif_clock == 0) {
        return;
    }
    patch->pll_state = direct;
    if (direct == DIRECT_SPEED) {
        clock_gettime(CLOCK_MONOTONIC, &(patch->speed_time));
    } else if (direct == DIRECT_SLOW) {
        clock_gettime(CLOCK_MONOTONIC, &(patch->slow_time));
    }
    if (patch->spdif_format_set == 0) {
        if (patch->dtv_default_i2s_clock > DEFAULT_SPDIF_PLL_DDP_CLOCK * 4 ||
            patch->dtv_default_i2s_clock == 0) {
            return;
        }
        ALOGV("i2s_step_clk:%d, i2s_div_factor:%d.", patch->i2s_step_clk, patch->i2s_div_factor);
        dtv_adjust_i2s_output_clock(patch, direct, patch->i2s_step_clk / patch->i2s_div_factor);
    } else if (!aml_dev->bHDMIARCon) {
        if (patch->dtv_default_i2s_clock > DEFAULT_SPDIF_PLL_DDP_CLOCK * 4 ||
            patch->dtv_default_i2s_clock == 0) {
            return;
        }
        ALOGV("i2s_step_clk:%d, spdif_step_clk:%d, i2s_div_factor:%d.", patch->i2s_step_clk, patch->spdif_step_clk, patch->i2s_div_factor);
        dtv_adjust_i2s_output_clock(patch, direct, patch->i2s_step_clk / patch->i2s_div_factor);
        dtv_adjust_spdif_output_clock(patch, direct, patch->spdif_step_clk / patch->i2s_div_factor, dual);
    } else {
        dtv_adjust_spdif_output_clock(patch, direct, patch->spdif_step_clk / 4, dual);
    }
}

static void dtv_adjust_output_clock_continue(struct aml_audio_patch * patch, int direct)
{
    struct timespec current_time;
    int time_cost = 0;
    static int last_div = 0;
    int adjust_interval = 0;
    patch->i2s_div_factor = property_get_int32(PROPERTY_AUDIO_TUNING_CLOCK_FACTOR, DEFAULT_TUNING_CLOCK_FACTOR);
    adjust_interval = property_get_int32("vendor.media.audio.hal.adjtime", 1000);
    if (last_div != patch->i2s_div_factor) {
        ALOGI("new_div=%d, adjust_interval=%d ms,spdif_format_set=%d\n",
            patch->i2s_div_factor, adjust_interval, patch->spdif_format_set);
        last_div = patch->i2s_div_factor;
    }

    if (patch->pll_state == DIRECT_NORMAL || patch->pll_state != direct) {
        ALOGI("pll_state=%d, direct=%d no need continue\n", patch->pll_state, direct);
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    if (direct == DIRECT_SPEED) {
        time_cost = calc_time_interval_us(&patch->speed_time, &current_time)/1000;
    } else if (direct == DIRECT_SLOW) {
        time_cost = calc_time_interval_us(&patch->slow_time, &current_time)/1000;
    }
    if (time_cost > adjust_interval && patch->spdif_format_set == 0) {
        ALOGI("over %d ms continue to adjust the clock\n", time_cost);
        dtv_adjust_output_clock(patch, direct, DEFAULT_DTV_ADJUST_CLOCK, false);
    }
    return;
}

static unsigned int dtv_calc_pcrpts_latency(struct aml_audio_patch *patch, unsigned int pcrpts)
{
    struct audio_hw_device *adev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) adev;
    if (aml_dev->bHDMIARCon == 0 || aml_dev->digital_audio_format == PCM) {
        return pcrpts;
    } else {
        return pcrpts + DEFAULT_ARC_DELAY_MS * 90;
    }
}

static void dtv_av_pts_info(struct aml_audio_patch *patch, unsigned int apts, unsigned int pcrpts)
{
    unsigned int cur_vpts = 0;
    unsigned int demux_vpts = 0;
    unsigned int demux_apts = 0;
    unsigned int demux_pcr = 0;
    unsigned int firstvpts = 0;
    char buf[4096] = {0};
    int video_display_frame_count = 0;
    int video_receive_frame_count = 0;
    int64_t av_diff_ms = 0;
    int64_t ap_diff_ms = 0;
    struct timespec now_time;
    int time_cost_ms = 0, avail = 0;
    struct timespec debug_current_time;
    int debug_time_costms = 0;
    int checkin_apts_interval = 0;
    int checkin_vpts_interval = 0;
    int demux_pcr_interval = 0;
    int out_apts_interval = 0;
    int out_vpts_interval = 0;

    get_sysfs_uint(TSYNC_VPTS, &cur_vpts);
    get_sysfs_uint(TSYNC_CHECKIN_APTS, &demux_apts);
    get_sysfs_uint(TSYNC_CHECKIN_VPTS, &demux_vpts);
    get_sysfs_uint(TSYNC_DEMUX_PCR, &demux_pcr);
    get_sysfs_uint(TSYNC_FIRST_VPTS, &firstvpts);
    av_diff_ms = (apts - cur_vpts) / 90;
    ap_diff_ms = (pcrpts - apts) / 90;
    avail = get_buffer_read_space(&(patch->aml_ringbuffer));

    if (patch->sync_para.pcr_init_mode == PCR_INIT_DMXPCR && decoder_dmxpcr_mode()) {
        ALOGI("dtv_av_info: pa-pv-av-diff:[%d,%d,%d],apts:%x(%x,cache:%dms),vpts:%x(%x,cache:%dms),pcrpts:%x,"
              "[%d,%d,%d],size:%d,latency:%d",\
               (int)(pcrpts - apts) / 90, (int)(pcrpts - cur_vpts) / 90, (int)(apts - cur_vpts)/90, apts, demux_apts,
               (int)(demux_apts - apts) / 90, cur_vpts, demux_vpts, (int)(demux_vpts - cur_vpts) / 90, pcrpts,
               (int)(demux_apts - pcrpts) / 90, (int)(demux_vpts - pcrpts) / 90, (int)(demux_vpts - demux_apts) / 90,
               avail, (int)decoder_get_latency() / 90);
    } else {
        ALOGI("dtv_av_info: pa-pv-av-diff:[%d,%d,%d],apts:%x(%x,cache:%dms),vpts:%x(%x,cache:%dms),pcrpts:%x,"
              "[%d,%d,%d],size:%d, mode %d\n",\
               (int)(pcrpts - apts) / 90, (int)(pcrpts - cur_vpts) / 90, (int)(apts - cur_vpts)/90, apts, demux_apts,
               (int)(demux_apts - apts) / 90,cur_vpts, demux_vpts, (int)(demux_vpts - cur_vpts) / 90, pcrpts,
               (int)(demux_apts - pcrpts) / 90, (int)(demux_vpts - pcrpts) / 90, (int)(demux_vpts - demux_apts) / 90,
               avail, patch->sync_para.pcr_init_mode);
    }
    return;
    if (patch->debug_para.debug_last_checkin_apts == 0 && patch->debug_para.debug_last_out_apts == 0
        && patch->debug_para.debug_last_checkin_vpts == 0 && patch->debug_para.debug_last_out_vpts == 0
        && patch->debug_para.debug_last_demux_pcr == 0) {
          patch->debug_para.debug_last_checkin_apts = demux_apts;
          patch->debug_para.debug_last_out_apts = apts;
          patch->debug_para.debug_last_checkin_vpts = demux_vpts;
          patch->debug_para.debug_last_out_vpts = cur_vpts;
          patch->debug_para.debug_last_demux_pcr = demux_pcr;
    }

    clock_gettime(CLOCK_MONOTONIC, &debug_current_time);
    debug_time_costms = calc_time_interval_us(&patch->debug_para.debug_system_time, &debug_current_time) / 1000;
    if (debug_time_costms >= patch->debug_para.debug_time_interval) {
        checkin_apts_interval = (int)(demux_apts - patch->debug_para.debug_last_checkin_apts) / 90;
        checkin_vpts_interval = (int)(demux_vpts - patch->debug_para.debug_last_checkin_vpts) / 90;
        demux_pcr_interval = (int)(demux_pcr - patch->debug_para.debug_last_demux_pcr) / 90;
        out_apts_interval = (int)(apts - patch->debug_para.debug_last_out_apts) / 90;
        out_vpts_interval = (int)(cur_vpts - patch->debug_para.debug_last_out_vpts) / 90;
        ALOGI("audio_hal_debug system_time_interval: %d ms, demux_pcr_interval: %d ms,"
              " apts interval:[in:%d, out:%d], vpts interval:[in:%d, out:%d].",\
               debug_time_costms, demux_pcr_interval, checkin_apts_interval,
               out_apts_interval, checkin_vpts_interval, out_vpts_interval);
        patch->debug_para.debug_last_checkin_apts = demux_apts;
        patch->debug_para.debug_last_out_apts = apts;
        patch->debug_para.debug_last_checkin_vpts = demux_vpts;
        patch->debug_para.debug_last_out_vpts = cur_vpts;
        patch->debug_para.debug_last_demux_pcr = demux_pcr;
        clock_gettime(CLOCK_MONOTONIC, &patch->debug_para.debug_system_time);
    }

    if ((av_diff_ms > 150 && ap_diff_ms > 0 && ap_diff_ms < 100 &&
        patch->dtv_log_retry_cnt++ > 1000) || (patch->tsync_pcr_debug > 1 && patch->dtv_log_retry_cnt++ > 4)) {
        patch->dtv_log_retry_cnt = 0;

        clock_gettime(CLOCK_MONOTONIC, &now_time);
        time_cost_ms = calc_time_interval_us(&patch->last_debug_record, &now_time) / 1000;
        clock_gettime(CLOCK_MONOTONIC, &patch->last_debug_record);
        // add calc to judge audio,video or pcr error.
        if (patch->last_apts_record != 0) {
            ALOGI("dtv_av_info, apts:0x%x, last_record=0x%x,apts diff:%" PRId64 " ms, time_const=%d ms\n",
                apts, patch->last_apts_record, (int64_t)(apts - patch->last_apts_record) / 90, time_cost_ms);
        }
        if (patch->last_pcrpts_record != 0) {
            ALOGI("dtv_av_info, pcrpts:0x%x, last_record=0x%x,pcr diff:%" PRId64 " ms, time_const=%d ms\n",
                pcrpts, patch->last_pcrpts_record, (int64_t)(pcrpts - patch->last_pcrpts_record) / 90, time_cost_ms);
        }
        if (patch->last_vpts_record != 0) {
            ALOGI("dtv_av_info, cur_vpts:0x%x, last_record=0x%x,vpts diff:%" PRId64 " ms, time_const=%d ms\n",
                cur_vpts, patch->last_vpts_record, (int64_t)(cur_vpts - patch->last_vpts_record) / 90, time_cost_ms);
        }
        patch->last_apts_record = apts;
        patch->last_pcrpts_record = pcrpts;
        patch->last_vpts_record = cur_vpts;

        sysfs_get_sysfs_str("/sys/class/amstream/bufs", buf, sizeof(buf));
        ALOGI("dtv_av_info, amstream bufs=%s\n", buf);
        memset(buf, 0, sizeof(buf));
        sysfs_get_sysfs_str("/sys/class/ppmgr/ppmgr_vframe_states", buf, sizeof(buf));
        ALOGI("dtv_av_info, ppmgr states=%s\n", buf);
        memset(buf, 0, sizeof(buf));
        sysfs_get_sysfs_str("/sys/class/deinterlace/di0/provider_vframe_status", buf, sizeof(buf));
        ALOGI("dtv_av_info, di states=%s\n", buf);
        memset(buf, 0, sizeof(buf));
        sysfs_get_sysfs_str("/sys/class/video/vframe_states", buf, sizeof(buf));
        ALOGI("dtv_av_info, video states=%s\n", buf);
    }

}

static int dtv_calc_abuf_level(struct aml_audio_patch *patch, struct aml_stream_out *stream_out)
{
    if (!patch) {
        return 0;
    }
    struct audio_hw_device *dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    ring_buffer_t *ringbuffer = &(patch->aml_ringbuffer);
    int main_avail = 0, min_buf_size;
    main_avail = get_buffer_read_space(ringbuffer);
    if ((patch->aformat == AUDIO_FORMAT_AC3) ||
        (patch->aformat == AUDIO_FORMAT_E_AC3)) {
        min_buf_size = stream_out->ddp_frame_size;
    } else if (patch->aformat == AUDIO_FORMAT_DTS) {
        min_buf_size = 1024;
    } else {
        min_buf_size = 32 * 48 * 4 * 2;
    }
    if (main_avail > min_buf_size) {
        return 1;
    }
    return 0;
}

void dtv_do_drop_pcm(int avail, struct aml_audio_patch *patch)
{
    ring_buffer_t *ringbuffer = &(patch->aml_ringbuffer);
    int drop_threshold = property_get_int32(PROPERTY_AUDIO_DROP_THRESHOLD, DEFAULT_AUDIO_DROP_THRESHOLD_MS);
    int least_cachetime = property_get_int32(PROPERTY_AUDIO_LEAST_CACHE, DEFAULT_AUDIO_LEAST_CACHE_MS);
    int least_size;
    int ap_diff_ms = patch->dtv_apts_lookup / 90;
    int drop_size = 48 * 4 * ap_diff_ms;
    int pts_latency = decoder_get_latency();
    int t1 = 0, t2 = 0;
    struct timespec before_write;
    struct timespec after_write;
    int wait_ms;
    least_size = least_cachetime * 48 * 4;
    ALOGI("AUDIO_DROP avail:%d,,dropsize:%d, pts_latency:%d.",  avail, drop_size, pts_latency);

    if (ap_diff_ms > drop_threshold) {
        int real_drop_size = (avail >= drop_size + least_size)?drop_size:avail - least_size;
        if (real_drop_size < 0)
            real_drop_size = 0;
        ALOGI("Drop data size: %d, avail: %d, need drop size: %d\n", real_drop_size, avail, drop_size);
        if (patch->out_buf_size > 0)
            t1 = real_drop_size / patch->out_buf_size;
        for (t2 = 0; t2 < t1; t2++) {
            ring_buffer_read(&(patch->aml_ringbuffer), (unsigned char *)patch->out_buf, patch->out_buf_size);
        }
        patch->dtv_apts_lookup = 0;
    }
    avail = get_buffer_read_space(ringbuffer);

    clock_gettime(CLOCK_MONOTONIC, &before_write);
    while (avail < least_size) {
        usleep(5 * 1000);
        avail = get_buffer_read_space(ringbuffer);
        clock_gettime(CLOCK_MONOTONIC, &after_write);
        wait_ms = calc_time_interval_us(&before_write, &after_write)/1000;
        if (wait_ms > 1000) {
            ALOGI("Warning wait_ms over 1s, break\n");
            break;
        }
    }

    if (patch->dtv_apts_lookup == 0) {
        patch->last_apts = 0;
        patch->last_pcrpts = 0;
    }
    patch->dtv_audio_tune = AUDIO_LATENCY;
    ALOGI("[%s,%d] dtv_audio_tune AUDIO_DROP-> AUDIO_LATENCY\n", __FUNCTION__, __LINE__);
}

void dtv_do_insert_zero_pcm(struct aml_audio_patch *patch,
                            struct audio_stream_out *stream_out)
{
    int t1, t2;
    int insert_size = 0;
    memset(patch->out_buf, 0, patch->out_buf_size);
    if (abs(patch->dtv_apts_lookup) / 90 > 1000) {
        t1 = 1000 * 192;
    } else {
        t1 =  192 * abs(patch->dtv_apts_lookup) / 90;
    }
    t2 = t1 / patch->out_buf_size;
    t1 = t1 & ~3;
    insert_size = t1;
    ALOGI("insert_zero_pcm: ++drop %d,lookup %d,diff %d ms,t2=%d,patch->out_buf_size=%zu\n",
         t1, patch->dtv_apts_lookup, t1 / 192, t2, patch->out_buf_size);
    /*[SE][BUG][SWPL-21122] when insert 0, need check write len,
         * and avoid dtv patch write together*/
    unsigned int cur_pcr = 0;
    struct timespec before_write;
    struct timespec after_write;
    clock_gettime(CLOCK_MONOTONIC, &before_write);
    patch->pcm_inserting = true;
    do {
        unsigned int cur_pts = patch->last_apts;
        get_sysfs_uint(TSYNC_PCRSCR, (unsigned int *) & (cur_pcr));
        int ap_diff = cur_pts - cur_pcr;
        ALOGI("insert_zero_pcm: cur_pts=0x%x, cur_pcr=0x%x,ap_diff=%d\n", cur_pts, cur_pcr, ap_diff);
        if (ap_diff < 90*10) {
            ALOGI("insert_zero_pcm: write mute pcm enough,break\n");
            patch->dtv_apts_lookup = 0;
            break;
        }
        memset(patch->out_buf, 0, patch->out_buf_size);
        int ret = ring_buffer_write(&(patch->aml_ringbuffer), (unsigned char *)patch->out_buf, patch->out_buf_size, 0);
        t1 -= ret;
        int buff_len = ring_buffer_read(&(patch->aml_ringbuffer), (unsigned char *)patch->out_buf, patch->out_buf_size);
        int write_len = out_write_new(stream_out, patch->out_buf, buff_len);
        patch->dtv_pcm_readed += write_len;
        clock_gettime(CLOCK_MONOTONIC, &after_write);
        int write_used_ms = calc_time_interval_us(&before_write, &after_write)/1000;
        ALOGI("insert_zero_pcm: write_used_ms = %d\n", write_used_ms);
        if (write_used_ms > 1000) {
            ALOGI("Warning write cost over 1s, break\n");
            break;
        }
        ALOGI("insert_zero_pcm: ++drop t1=%d, ret = %d", t1, ret);
    } while (t1 > 0);
    patch->pcm_inserting = false;
    patch->dtv_apts_lookup += ((insert_size - t1) * 90) / 192;
    ALOGI("after insert size:%d, dtv_apts_lookup=%d\n",
            insert_size - t1, patch->dtv_apts_lookup);
    if (-DTV_PTS_CORRECTION_THRESHOLD <= patch->dtv_apts_lookup) {
        ALOGI("only need insert 30ms, break\n");
        patch->dtv_apts_lookup = 0;
    }
}

/*when skip amadec ,do not insert data from ringbuffer ,insert data from aml_dtvsync_insertpcm api*/
void dtv_do_insert_zero_pcm_v2(struct aml_audio_patch *patch,
                            struct audio_stream_out *stream_out)
{
    // to do
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream_out;
    struct audio_hw_device *adev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) adev;
    int insert_time_ms =0;
    bool is_ms12 = false;
    ALOGV("stream_out %p", stream_out);
    if (abs(patch->dtv_apts_lookup) / 90 > 1000) {
        insert_time_ms = 1000;
    } else {
        if (abs(patch->dtv_apts_lookup) / 90 > 60) {
            insert_time_ms =  abs(patch->dtv_apts_lookup) / 90;
        } else {
            patch->pcm_inserting = false;
            return;
        }
    }
    do {

        if (patch->output_thread_exit == 1) {
            ALOGI("input exit, break now\n");
            break;
        }

        aml_dtvsync_insertpcm(stream_out, AUDIO_FORMAT_PCM_16_BIT, 8, is_ms12);

        insert_time_ms -= 8;

    } while (insert_time_ms  > 0);
    patch->pcm_inserting = false;
}
/*when skip amadec ,do not need drop data from ringbuffer ,skip data from alsa*/
void dtv_do_drop_pcm_v2(struct aml_audio_patch *patch,
            struct audio_stream_out *stream_out)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream_out;
    struct aml_audio_device *adev = aml_out->dev;
    size_t frame_size = 0;
    switch (adev->sink_format) {
    case AUDIO_FORMAT_E_AC3:
        if (eDolbyDcvLib == adev->dolby_lib_type)
            frame_size = AUDIO_AC3_FRAME_SIZE;
        else
            frame_size = AUDIO_EAC3_FRAME_SIZE;
        break;
    case AUDIO_FORMAT_AC3:
        frame_size = AUDIO_AC3_FRAME_SIZE;
        break;
    default:
        frame_size = (aml_out->is_tv_platform == true) ? AUDIO_TV_PCM_FRAME_SIZE : AUDIO_DEFAULT_PCM_FRAME_SIZE;
        break;
    }
    if (patch->dtv_apts_lookup > AUDIO_PTS_DISCONTINUE_THRESHOLD) {
        ALOGI("dtv_apts_lookup = 0x%x > 5s,force set 5s \n", patch->dtv_apts_lookup);
        patch->dtv_apts_lookup = AUDIO_PTS_DISCONTINUE_THRESHOLD;
    }
    aml_out->need_drop_size = (patch->dtv_apts_lookup / 90) * 48 * frame_size;
    aml_out->need_drop_size &= ~(frame_size - 1);
    ALOGI("dtv_do_drop need_drop_size=%d,frame_size=%zu\n",
        aml_out->need_drop_size, frame_size);
}

void dtv_do_process_pcm(int avail, struct aml_audio_patch *patch,
                            struct audio_stream_out *stream_out)
{
    if (!patch) {
        ALOGE("%s(), patch is NULL", __func__);
        return ;
    }
    struct audio_hw_device *adev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) adev;
    struct aml_stream_out *out = (struct aml_stream_out *)stream_out;
    if (!patch->dev || !stream_out) {
        return;
    }
    if (patch->dtv_apts_lookup > 0) {
        if (!audio_is_linear_pcm(patch->aformat))  {
            dtv_do_drop_pcm_v2(patch, stream_out);
        } else {
            dtv_do_drop_pcm(avail, patch);
        }
    } else if (patch->dtv_apts_lookup < 0) {
        if (!audio_is_linear_pcm(patch->aformat))  {
            dtv_do_insert_zero_pcm_v2(patch, stream_out);
        } else {
            dtv_do_insert_zero_pcm(patch, stream_out);
        }

        if (patch->dtv_apts_lookup == 0) {
            patch->last_apts = 0;
            patch->last_pcrpts = 0;
            ALOGI("[%s,%d] dtv_audio_tune AUDIO_DROP-> AUDIO_LATENCY\n",
                  __FUNCTION__, __LINE__);
            patch->dtv_audio_tune = AUDIO_LATENCY;
        } else {
            patch->dtv_audio_tune = AUDIO_LOOKUP;
        }
    }
}

static int dtv_audio_tune_check(struct aml_audio_patch *patch, int cur_pts_diff, int last_pts_diff, unsigned int apts)
{
    char tempbuf[128] = {0};
    int origin_pts_diff = 0;
    struct audio_hw_device *adev = NULL;
    if (!patch) {
        ALOGE("%s(), patch is NULL", __func__);
        return -1;
    }
    adev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) adev;
    if (!patch->dev || aml_dev->dev2mix_patch == 1) {
        patch->dtv_audio_tune = AUDIO_RUNNING;
        return 1;
    }
    if (get_audio_discontinue() || patch->dtv_has_video == 0 ||
        patch->dtv_audio_mode == 1) {
        dtv_adjust_output_clock(patch, DIRECT_NORMAL, DEFAULT_DTV_ADJUST_CLOCK, false);
        patch->last_apts = 0;
        patch->last_pcrpts = 0;
        return 1;
    }

    if (abs(cur_pts_diff) >= patch->a_discontinue_threshold) {
        sprintf(tempbuf, "AUDIO_TSTAMP_DISCONTINUITY:0x%lx",
                (unsigned long)apts);
        dtv_adjust_output_clock(patch, DIRECT_NORMAL, DEFAULT_DTV_ADJUST_CLOCK, false);
        if (sysfs_set_sysfs_str(TSYNC_EVENT, tempbuf) == -1) {
            ALOGI("unable to open file %s,err: %s", TSYNC_EVENT, strerror(errno));
        }
        return 1;
    }
    if (patch->dtv_audio_tune != AUDIO_RUNNING) {
        if (dtv_avsync_audio_freerun(patch)) {
            patch->dtv_audio_tune = AUDIO_RUNNING;
            return 1;
        }
    }
    if (patch->dtv_audio_tune == AUDIO_LOOKUP && patch->video_invalid != true) {
        if (abs(last_pts_diff - cur_pts_diff) < DTV_PTS_CORRECTION_THRESHOLD) {
            patch->dtv_apts_lookup = (last_pts_diff + cur_pts_diff) / 2;
            patch->dtv_audio_tune = AUDIO_DROP;
            ALOGI("dtv_audio_tune audio_lookup %d", patch->dtv_apts_lookup);
        }
        return 1;
    } else if (patch->dtv_audio_tune == AUDIO_LATENCY) {
        if (abs(last_pts_diff - cur_pts_diff) < DTV_PTS_CORRECTION_THRESHOLD) {
            int pts_diff = (last_pts_diff + cur_pts_diff) / 2;
            int pts_latency = decoder_get_latency();
            if (patch->dtv_disable_tune_latency) {
                patch->dtv_disable_tune_latency = 0;
                ALOGV("dtv_audio_tune latency disabled,pts_diff %d", pts_diff / 90);
            } else if (get_dtv_pcr_sync_mode() == 1) {
                if (pts_diff > DTV_AUDIO_DROP_DEFAULT_THRESHOLD * 90) {
                    pts_diff = DTV_AUDIO_DROP_DEFAULT_THRESHOLD * 90;
                } else if (pts_diff < -DTV_AUDIO_DROP_DEFAULT_THRESHOLD * 90) {
                    pts_diff = -DTV_AUDIO_DROP_DEFAULT_THRESHOLD * 90;
                }
                ALOGI("dtv_audio_tune audio_latency %d,pts_diff %d", (int)pts_latency / 90, pts_diff / 90);
                pts_latency += pts_diff;
                if (pts_latency >= AUDIO_PCR_LATENCY_MAX * 90) {
                    pts_latency = AUDIO_PCR_LATENCY_MAX * 90;
                } else if (pts_latency < 0) {
                    if (abs(pts_diff) < DTV_PTS_CORRECTION_THRESHOLD) {
                        pts_latency += abs(pts_diff);
                    } else {
                        pts_latency = 0;
                    }
                }
                decoder_set_latency(pts_latency);
            } else {
                uint pcrpts = 0;
                origin_pts_diff = pts_diff;
                get_sysfs_uint(TSYNC_PCRSCR, &pcrpts);
                ALOGI("dtv_audio_tune audio_latency pts_diff %d, pcrsrc %x", pts_diff / 90, pcrpts);
                if (pts_diff > patch->sync_para.pcr_adjust_max) {
                    pts_diff = patch->sync_para.pcr_adjust_max;
                } else if (pts_diff < -patch->sync_para.pcr_adjust_max) {
                    pts_diff = -patch->sync_para.pcr_adjust_max;
                }
                pcrpts -= pts_diff;
                decoder_set_pcrscr(pcrpts);
                ALOGI("dtv_audio_tune audio_latency end, pcrsrc %x, diff:%d, origin:%d",
                        pcrpts, pts_diff, origin_pts_diff);
            }
            ALOGI("dtv_audio_tune AUDIO_LATENCY -> AUDIO_RUNNING,cur_diff:%d\n",
                    patch->sync_para.cur_pts_diff);
            patch->dtv_audio_tune = AUDIO_RUNNING;
            clean_dtv_patch_pts(patch);
        }
        return 1;
    } else if (patch->dtv_audio_tune != AUDIO_RUNNING) {
        return 1;
    }

    return 0;
}

static void do_pll1_by_pts(unsigned int pcrpts, struct aml_audio_patch *patch,
                           unsigned int apts, struct aml_stream_out *stream_out)
{
    unsigned int last_pcrpts, last_apts;
    int pcrpts_diff, last_pts_diff, cur_pts_diff;
    struct audio_hw_device *adev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) adev;
    struct aml_mixer_handle * handle = &(aml_dev->alsa_mixer);

    if (patch->tsync_pcr_debug || aml_dev->debug_flag) {
        dtv_av_pts_info(patch, apts, pcrpts);
    }

    last_apts = patch->last_apts;
    last_pcrpts = patch->last_pcrpts;
    patch->last_pcrpts = pcrpts;
    patch->last_apts = apts;
    last_pts_diff = last_pcrpts - last_apts;
    cur_pts_diff = pcrpts - apts;
    patch->sync_para.cur_pts_diff = cur_pts_diff;
    pcrpts_diff = pcrpts - last_pcrpts;
    if (last_apts == 0 && last_pcrpts == 0) {
        return;
    }
    if (patch->pll_state == DIRECT_NORMAL) {
        if (abs(cur_pts_diff) <= DTV_PTS_CORRECTION_THRESHOLD * 3 ||
            abs(last_pts_diff + cur_pts_diff) / 2 <= DTV_PTS_CORRECTION_THRESHOLD * 3
            || abs(last_pts_diff) <= DTV_PTS_CORRECTION_THRESHOLD * 3) {
            return;
        } else {
            if (pcrpts > apts) {
                if (dtv_calc_abuf_level(patch, stream_out)) {
                    dtv_adjust_output_clock(patch, DIRECT_SPEED, DEFAULT_DTV_ADJUST_CLOCK, false);
                } else {
                    dtv_adjust_output_clock(patch, DIRECT_NORMAL, DEFAULT_DTV_ADJUST_CLOCK, false);
                }
            } else {
                dtv_adjust_output_clock(patch, DIRECT_SLOW, DEFAULT_DTV_ADJUST_CLOCK, false);
            }
            return;
        }
    } else if (patch->pll_state == DIRECT_SPEED) {
        if (!dtv_calc_abuf_level(patch, stream_out)) {
            dtv_adjust_output_clock(patch, DIRECT_NORMAL, DEFAULT_DTV_ADJUST_CLOCK, false);
        }
        if (cur_pts_diff < 0 && ((last_pts_diff + cur_pts_diff) < 0 ||
                                 abs(last_pts_diff) < DTV_PTS_CORRECTION_THRESHOLD)) {
            dtv_adjust_output_clock(patch, DIRECT_NORMAL, DEFAULT_DTV_ADJUST_CLOCK, false);
        }
    if (cur_pts_diff > 0)
            dtv_adjust_output_clock_continue(patch, DIRECT_SPEED);
    } else if (patch->pll_state == DIRECT_SLOW) {
        if (cur_pts_diff > 0 && ((last_pts_diff + cur_pts_diff) > 0 || abs(last_pts_diff) < DTV_PTS_CORRECTION_THRESHOLD)) {
            dtv_adjust_output_clock(patch, DIRECT_NORMAL, DEFAULT_DTV_ADJUST_CLOCK, false);
        }
    if (cur_pts_diff < 0)
            dtv_adjust_output_clock_continue(patch, DIRECT_SLOW);
    }
}

static void do_pll2_by_pts(unsigned int pcrpts, struct aml_audio_patch *patch,
                           unsigned int apts, struct aml_stream_out *stream_out)
{
    unsigned int last_pcrpts, last_apts;
    unsigned int cur_vpts = 0;
    int pcrpts_diff, last_pts_diff, cur_pts_diff;
    struct audio_hw_device *adev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) adev;
    struct aml_mixer_handle * handle = &(aml_dev->alsa_mixer);
    int ret = 0;

    if (patch->tsync_pcr_debug || aml_dev->debug_flag) {
        dtv_av_pts_info(patch, apts, pcrpts);
    }

    last_apts = patch->last_apts;
    last_pcrpts = patch->last_pcrpts;
    patch->last_pcrpts = pcrpts;
    patch->last_apts = apts;
    last_pts_diff = last_pcrpts - last_apts;
    cur_pts_diff = pcrpts - apts;
    patch->sync_para.cur_pts_diff = cur_pts_diff;
    pcrpts_diff = pcrpts - last_pcrpts;
    if (last_apts == 0 && last_pcrpts == 0) {
        return;
    }
    if (patch->pll_state == DIRECT_NORMAL) {
        if (abs(cur_pts_diff) <= DTV_PTS_CORRECTION_THRESHOLD ||
            abs(last_pts_diff + cur_pts_diff) / 2 <= DTV_PTS_CORRECTION_THRESHOLD
            || abs(last_pts_diff) <= DTV_PTS_CORRECTION_THRESHOLD) {
            return;
        } else {
            if (pcrpts > apts) {
                if (dtv_calc_abuf_level(patch, stream_out)) {
                    dtv_adjust_output_clock(patch, DIRECT_SPEED, DEFAULT_DTV_ADJUST_CLOCK, false);
                } else {
                    dtv_adjust_output_clock(patch, DIRECT_NORMAL, DEFAULT_DTV_ADJUST_CLOCK, false);
                }
            } else {
                dtv_adjust_output_clock(patch, DIRECT_SLOW, DEFAULT_DTV_ADJUST_CLOCK, false);
            }
            return;
        }
    } else if (patch->pll_state == DIRECT_SPEED) {
        if (!dtv_calc_abuf_level(patch, stream_out)) {
            dtv_adjust_output_clock(patch, DIRECT_NORMAL, DEFAULT_DTV_ADJUST_CLOCK, false);
        }
        if (cur_pts_diff < 0 && ((last_pts_diff + cur_pts_diff) < 0 ||
                                 abs(last_pts_diff) < DTV_PTS_CORRECTION_THRESHOLD)) {
            dtv_adjust_output_clock(patch, DIRECT_NORMAL, DEFAULT_DTV_ADJUST_CLOCK, false);
        }
    if (cur_pts_diff > 0)
            dtv_adjust_output_clock_continue(patch, DIRECT_SPEED);
    } else if (patch->pll_state == DIRECT_SLOW) {
        if (cur_pts_diff > 0 && ((last_pts_diff + cur_pts_diff) > 0 || abs(last_pts_diff) < DTV_PTS_CORRECTION_THRESHOLD)) {
            dtv_adjust_output_clock(patch, DIRECT_NORMAL, DEFAULT_DTV_ADJUST_CLOCK, false);
        }
    if (cur_pts_diff < 0)
            dtv_adjust_output_clock_continue(patch, DIRECT_SLOW);
    }
}

void process_ac3_sync(struct aml_audio_patch *patch, unsigned long pts, struct aml_stream_out *stream_out)
{
    int channel_count = 2;
    int bytewidth = 2;
    int symbol = 48;
    char tempbuf[128] = {0};
    unsigned int pcrpts = 0;
    unsigned int pts_diff,last_checkin_apts = 0;
    unsigned long cur_out_pts;
    struct audio_hw_device *adev = NULL;
    if (!patch) {
        ALOGE("%s(), patch is NULL", __func__);
        return ;
    }
    adev = patch->dev;
    struct aml_audio_device * aml_dev = (struct aml_audio_device*)adev;

    get_sysfs_uint(TSYNC_PCRSCR, &pcrpts);
    if (patch->dtv_first_apts_flag == 0) {
        get_sysfs_uint(TSYNC_LAST_CHECKIN_APTS, &last_checkin_apts);
        sprintf(tempbuf, "AUDIO_START:0x%x", (unsigned int)pts);
        ALOGI("[audiohal_kpi] dtv set tsync -> %s, cache audio:%dms,pcr:0x%x",
                tempbuf, (int)(last_checkin_apts - pts)/90, pcrpts);
        if (sysfs_set_sysfs_str(TSYNC_EVENT, tempbuf) == -1) {
            ALOGE("set AUDIO_START failed \n");
        }
        patch->dtv_first_apts_flag = 1;

        if (patch->dtv_has_video) {
            aml_dev->start_mute_flag = 1;
            aml_dev->start_mute_count = 0;
            ALOGI("set start_mute_flag 1.");
        }
        clock_gettime(CLOCK_MONOTONIC, &patch->debug_para.debug_system_time);
    } else {
        cur_out_pts = pts;
        if (!patch->dev || !stream_out) {
            return;
        }
        if (pts == 0) {
            return;
        }
        cur_out_pts -= dtv_avsync_get_apts_latency((struct audio_stream_out *)stream_out);
        do_pll2_by_pts(pcrpts, patch, cur_out_pts, stream_out);
    }
}

void process_pts_sync(unsigned int pcm_latency, struct aml_audio_patch *patch,
                      unsigned int rbuf_level, struct aml_stream_out *stream_out)
{
    int channel_count = 2;
    int bytewidth = 2;
    int symbol = 48;
    char tempbuf[128] = {0};
    unsigned int pcrpts = 0, apts  = 0, last_checkin_apts = 0;
    unsigned int calc_len = 0;
    unsigned long pts = 0, lookup_pts;
    unsigned long cache_pts = 0;
    unsigned long cur_out_pts = 0;
    unsigned int checkin_firstapts = 0;
    struct audio_hw_device *adev = NULL;
    if (!patch) {
        ALOGE("%s(), patch is NULL", __func__);
        return ;
    }
    adev = patch->dev;
    struct aml_audio_device * aml_dev = (struct aml_audio_device*)adev;


    get_sysfs_uint(DTV_DECODER_CHECKIN_FIRSTAPTS_PATH, &checkin_firstapts);
    pts = lookup_pts = dtv_patch_get_pts();
    if (pts == patch->last_valid_pts) {
        ALOGI("dtv_patch_get_pts pts  -> %lx", pts);
    }
    if (patch->dtv_first_apts_flag == 0) {
        if (pts == 0) {
            pts = checkin_firstapts;
            ALOGI("pts = 0,so get checkin_firstapts:0x%lx", pts);
        }

        get_sysfs_uint(TSYNC_LAST_CHECKIN_APTS, &last_checkin_apts);
        sprintf(tempbuf, "AUDIO_START:0x%x", (unsigned int)pts);
        ALOGI("[audiohal_kpi]dtv set tsync -> %s, audio cache:%dms",
                tempbuf, (int)(last_checkin_apts - pts)/90);
        if (sysfs_set_sysfs_str(TSYNC_EVENT, tempbuf) == -1) {
            ALOGE("set AUDIO_START failed \n");
        }
        if (patch->dtv_has_video) {
            aml_dev->start_mute_flag = 1;
            aml_dev->start_mute_count = 0;
            ALOGI("set start_mute_flag 1.");
        }

        patch->dtv_pcm_readed = 0;
        patch->dtv_first_apts_flag = 1;
        patch->last_valid_pts = pts;
        patch->cur_outapts = pts;
        patch->sync_para.last_validpts_duration = 0;
        dtv_avsync_record_out_apts((struct audio_stream_out*)stream_out, pts);
        patch->sync_para.validpts_cnt_record++;
        clock_gettime(CLOCK_MONOTONIC, &patch->debug_para.debug_system_time);

    } else {
        unsigned int pts_diff;
        if (pts != (unsigned long) - 1) {
            calc_len = (unsigned int)rbuf_level;
            cache_pts = (calc_len * 90) / (symbol * channel_count * bytewidth);
            if (pts > cache_pts) {
                cur_out_pts = pts - cache_pts;
            } else {
                return;
            }
            if (cur_out_pts > pcm_latency * 90) {
                cur_out_pts = cur_out_pts - pcm_latency * 90;
            } else {
                return;
            }
            patch->last_valid_pts = cur_out_pts;
            dtv_avsync_record_out_apts((struct audio_stream_out*)stream_out, pts);
            patch->sync_para.validpts_cnt_record++;
            patch->sync_para.last_validpts_duration = 0;
            patch->dtv_pcm_readed = 0;
            if (aml_dev->debug_flag || patch->tsync_pcr_debug > 1) {
                ALOGI("====get_pts:%lx, latency %d, level %d",
                    pts, (int)pcm_latency, (int)rbuf_level);
            }
        } else {
            pts = patch->last_valid_pts;
            calc_len = patch->dtv_pcm_readed;
            cache_pts = (calc_len * 90) / (symbol * channel_count * bytewidth);
            if (patch->sync_para.last_validpts_duration > 0) {
                cur_out_pts = pts + patch->sync_para.last_validpts_duration;
            } else {
                cur_out_pts = pts + cache_pts;
            }
            if (aml_dev->debug_flag || patch->tsync_pcr_debug > 1) {
                ALOGI("get_pts:%lx,last_valid_pts %lx,dur %d,cache_pts %lx,latency %d,level %d",
                    cur_out_pts, patch->last_valid_pts, patch->sync_para.last_validpts_duration,
                    cache_pts, (int)pcm_latency, (int)rbuf_level);
            }
        }
        if (!patch->dev || !stream_out) {
            return;
        }
        patch->cur_outapts = cur_out_pts;
        get_sysfs_uint(TSYNC_PCRSCR, &pcrpts);
        cur_out_pts -= dtv_avsync_get_apts_latency((struct audio_stream_out *)stream_out);
        do_pll1_by_pts(pcrpts, patch, cur_out_pts, stream_out);
    }
}

bool dtv_avsync_audio_freerun(struct aml_audio_patch* patch)
{
    unsigned int demux_apts = 0, cur_pcr = 0;

    get_sysfs_uint(TSYNC_CHECKIN_APTS, &demux_apts);
    if (patch->last_pcrpts) {
        cur_pcr = patch->last_pcrpts;
    } else {
        get_sysfs_uint(TSYNC_PCRSCR, &cur_pcr);
    }
    if (demux_apts && cur_pcr && (int)demux_apts != -1 && (int)cur_pcr != -1) {
        if (demux_apts < cur_pcr - DTV_PTS_CORRECTION_THRESHOLD * 5 ||
            demux_apts > cur_pcr + patch->a_discontinue_threshold) {
            return true;
        }
    }
    if (patch->dtv_audio_mode == 1) {
        return true;
    }
    return false;
}

/* +[SE] [BUG][SWPL-21070] startplay strategy choose*/
void dtv_avsync_get_ptsinfo(struct aml_audio_patch* patch)
{
    unsigned int cur_vpts = 0, cur_pcr = 0;
    unsigned int firstvpts = 0, checkin_firstapts = 0;
    get_sysfs_uint(TSYNC_VPTS, &cur_vpts);
    get_sysfs_uint(TSYNC_FIRST_VPTS, &firstvpts);
    get_sysfs_uint(TSYNC_PCRSCR, &cur_pcr);
    get_sysfs_uint(DTV_DECODER_CHECKIN_FIRSTAPTS_PATH, &checkin_firstapts);

    patch->startplay_vpts = cur_vpts;
    patch->startplay_pcrpts = cur_pcr;
    patch->startplay_apts_lookup = patch->last_valid_pts;
    patch->startplay_firstvpts = firstvpts;
    patch->startplay_first_checkinapts = checkin_firstapts;
}

/* +[SE] [BUG][SWPL-21070] startplay strategy choose*/
void dtv_out_avpts_equal(struct aml_audio_patch* patch)
{
    struct audio_hw_device *dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    if (patch->startplay_pcrpts >= patch->startplay_firstvpts) {
            aml_dev->start_mute_flag = 0;
            patch->startplay_avsync_flag = 0;
            ALOGI("%s avsync startplay strategy mode 0 --\n", __FUNCTION__);
    }
}

/* +[SE] [BUG][SWPL-21070] startplay strategy choose*/
void dtv_out_apts_biggerthan_vpts(struct aml_audio_patch* patch)
{
    unsigned int demux_pcr = 0;
    get_sysfs_uint(TSYNC_DEMUX_PCR, &demux_pcr);

    struct audio_hw_device *dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    int strategy_mode = property_get_int32("vendor.media.audio.strategy.aptsbigger", 1);

    if (get_tsync_pcr_debug()) {
        ALOGI("avsync startplay pcrpts = 0x%lx, demux_pcr = 0x%x --\n",
                        patch->startplay_pcrpts, demux_pcr);
    }

    if (patch->startplay_avsync_flag) {
        if (strategy_mode == STRATEGY_A_ZERO_V_NOSHOW) {
            if (patch->startplay_pcrpts >= patch->startplay_firstvpts) {
                //when pcr bigger than vpts,output normal
            }
        }
        if (patch->startplay_pcrpts >= patch->startplay_first_checkinapts) {
            aml_dev->start_mute_flag = 0;
            patch->startplay_avsync_flag = 0;
            ALOGI("%s avsync startplay strategy mode = %d --\n", __FUNCTION__, strategy_mode);
        }
    }
}

/* +[SE] [BUG][SWPL-21070] startplay strategy choose*/
void dtv_out_vpts_biggerthan_apts(struct aml_audio_patch* patch)
{
    struct audio_hw_device *dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    int strategy_mode = property_get_int32("vendor.media.audio.strategy.vptsbigger", 4);

    if (patch->startplay_avsync_flag) {
        if ((strategy_mode >= STRATEGY_A_NORMAL_V_SHOW_BLOCK) &&
            (strategy_mode <= STRATEGY_A_MUTE_V_SHOW_BLOCK)) {
            if (strategy_mode == STRATEGY_A_NORMAL_V_SHOW_BLOCK) {
                aml_dev->start_mute_flag = 0;
            } else if (strategy_mode == STRATEGY_A_NORMAL_V_NOSHOW) {
                //if (patch->startplay_pcrpts >= patch->startplay_first_checkinapts) {
                    //aml_dev->start_mute_flag = 1;
                //}
            } else if (strategy_mode == STRATEGY_A_MUTE_V_SHOW_BLOCK) {
                if (patch->startplay_pcrpts >= patch->startplay_first_checkinapts) {
                    aml_dev->start_mute_flag = 1;
                }
            }
            if (patch->startplay_pcrpts >= patch->startplay_firstvpts) {
                aml_dev->start_mute_flag = 0;
                patch->startplay_avsync_flag = 0;
                ALOGI("%s avsync startplay strategy mode = %d --\n", __FUNCTION__, strategy_mode);
            }
        }
        if ((strategy_mode == STRATEGY_A_DROP_V_SHOW_BLOCK) ||
            (strategy_mode == STRATEGY_A_DROP_V_NOSHOW)) {
            if (patch->startplay_apts_lookup >= patch->startplay_firstvpts) {
                decoder_set_latency(DEMUX_PCR_APTS_LATENCY);
                aml_dev->start_mute_flag = 0;
                patch->startplay_avsync_flag = 0;
                ALOGI("%s avsync startplay strategy mode = %d --\n", __FUNCTION__, strategy_mode);
            }
        }
    }
}

/* +[SE] [BUG][SWPL-21070] startplay strategy choose*/
void dtv_avsync_startplay_strategy(struct aml_audio_patch *patch)
{
    int strategy_mode = 0;
    unsigned long avdiff;
    struct audio_hw_device *dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    ring_buffer_t *ringbuffer = &(patch->aml_ringbuffer);
    int avail = get_buffer_read_space(ringbuffer);

    if ((dtv_get_tsync_mode() == 2) && (patch->startplay_avsync_flag)) {
        ALOGI(" start play under pcrmaster \n");
    } else {
        return;
    }

    int default_time= DEFAULT_SYSTEM_TIME;
    int avdiff_time_adjust = property_get_int32("vendor.media.audio.avdifftime", 3);
    int startplay_strategy_aptsbigger = property_get_int32("vendor.media.audio.strategy.aptsbigger", 1);
    int startplay_strategy_vptsbigger = property_get_int32("vendor.media.audio.strategy.vptsbigger", 3);

    dtv_avsync_get_ptsinfo(patch);
    if (patch->startplay_firstvpts >patch->startplay_first_checkinapts)
        avdiff = patch->startplay_firstvpts - patch->startplay_first_checkinapts;
    else
        avdiff = patch->startplay_first_checkinapts -patch->startplay_firstvpts;
    if (get_tsync_pcr_debug()) {
        ALOGI("avsync startplay firstvpts = 0x%lx,first_checkinapts = 0x%lx, avdiff = 0x%lx --\n",
                        patch->startplay_firstvpts, patch->startplay_first_checkinapts, avdiff);
        ALOGI("avsync startplay pcrpts = 0x%lx, apts = 0x%lx, vpts = 0x%x --\n",
                        patch->startplay_pcrpts, patch->startplay_apts_lookup, patch->startplay_vpts);
    }
    /*------------------------------------------------------------
    this function is designed for av alternately distributed situation
    startplay have below situation
    -------------------------------------------------------------*/
    if (avdiff < avdiff_time_adjust * default_time) {  //Evenly distributed data
        if (patch->startplay_first_checkinapts == patch->startplay_firstvpts) {  //strategy 0
            strategy_mode = 0;
        } else if (patch->startplay_first_checkinapts > patch->startplay_firstvpts) {  //strategy 1 2
            strategy_mode = startplay_strategy_aptsbigger;
        } else if (patch->startplay_first_checkinapts < patch->startplay_firstvpts) {  //strategy 3 4 5 6 7
            strategy_mode = startplay_strategy_vptsbigger;
        }
        //ALOGI("avsync startplay strategy mode = %d --\n", strategy_mode);
    }
    #if 0
    /*------------------------------------------------------------
    this function is designed for only video situation
    -------------------------------------------------------------*/
    else if () {  //only video

    }
    /*------------------------------------------------------------
    this function is designed for only audio situation
    -------------------------------------------------------------*/
    else if () {  //only audio

    }
     /*------------------------------------------------------------
    this function is designed for av not alternately distributed situation
    -------------------------------------------------------------*/
    else {  //Uneven data distribution

    }
    #endif
    switch (strategy_mode) {
    case STRATEGY_AVOUT_NORMAL:
        dtv_out_avpts_equal(patch);
        break;
    case STRATEGY_A_ZERO_V_NOSHOW:
    case STRATEGY_A_NORMAL_V_SHOW_QUICK:
        dtv_out_apts_biggerthan_vpts(patch);
        break;
    case STRATEGY_A_NORMAL_V_SHOW_BLOCK:
    case STRATEGY_A_NORMAL_V_NOSHOW:
    case STRATEGY_A_MUTE_V_SHOW_BLOCK:
    case STRATEGY_A_DROP_V_SHOW_BLOCK:
    case STRATEGY_A_DROP_V_NOSHOW:
        dtv_out_vpts_biggerthan_apts(patch);
        break;
    default:
        ALOGI(" startplay strategy do not contained\n");
        break;
    }
}

void dtv_avsync_process(struct audio_stream_out *stream, size_t bytes, audio_format_t output_format)
{
    struct aml_stream_out *stream_out = (struct aml_stream_out *) stream;
    if (stream_out  == NULL) {
        ALOGE("dtv_avsync_process stream_out is NULL");
        return ;
    }
    struct aml_audio_device *aml_dev = stream_out->dev;
    struct aml_audio_patch *patch = aml_dev->audio_patch;
    ring_buffer_t *ringbuffer = &(patch->aml_ringbuffer);
    int ret = 0, duration, pcm_latency = 0;
    int audio_output_delay = 0;
    uint32_t start_ms = 0;
    unsigned long pts;

    if (!patch || aml_dev->patch_src != SRC_DTV || patch->first_apts_lookup_over == 0 || patch->pcm_inserting) {
        return;
    }
    start_ms = dtv_avsync_get_time_interval(&patch->sync_para.start_output_record);
    patch->dtv_audio_mode = decoder_audio_mode();
    patch->tsync_pcr_debug = get_tsync_pcr_debug();
    if (patch->dtv_decoder_state != AUDIO_DTV_PATCH_DECODER_STATE_RUNNING) {
        if (patch->sync_para.validpts_cnt_record) {
            patch->sync_para.validpts_cnt_record = 0;
        }
        return;
    }
    duration = dtv_get_frame_duration(stream, bytes, output_format);
    if (duration > 0) {
        patch->sync_para.last_validpts_duration += duration;
        patch->sync_para.avsync_duration += duration;
        if (patch->sync_para.avsync_duration < DTV_PTS_CORRECTION_THRESHOLD) {
            return;
        }
    }

    if ((eDolbyDcvLib == aml_dev->dolby_lib_type &&
            is_dolby_ddp_support_compression_format(patch->aformat)) ||
            (eDolbyMS12Lib == aml_dev->dolby_lib_type &&
            is_dolby_ms12_support_compression_format(patch->aformat)) || patch->skip_amadec_flag) {
        if (stream_out != NULL) {
            pcm_latency = out_get_latency(&(stream_out->stream)) + audio_output_delay;

            if (pcm_latency < 0)
                pcm_latency = 0;
            pts = dtv_hal_get_pts(stream, pcm_latency);
            process_ac3_sync(patch, pts, stream_out);
        }
    } else if (patch->aformat ==  AUDIO_FORMAT_DTS || patch->aformat == AUDIO_FORMAT_DTS_HD) {
        if (stream_out != NULL) {
            ringbuffer = &(patch->aml_ringbuffer);
            pcm_latency = out_get_latency(stream) + audio_output_delay;
            if (pcm_latency < 0)
                pcm_latency = 0;
            pts = dtv_hal_get_pts(stream, pcm_latency);
            process_ac3_sync(patch, pts, stream_out);
        }
    } else {
        if (stream_out != NULL) {
            pcm_latency = out_get_latency(stream) + audio_output_delay;
            if (pcm_latency < 0)
                pcm_latency = 0;
            int abuf_level = get_buffer_read_space(ringbuffer);
            process_pts_sync(pcm_latency, patch, abuf_level, stream_out);
        }
    }
    if (aml_dev->start_mute_flag) {
        dtv_process_start_mute(stream, start_ms);
    }
    dtv_avsync_main_loop(stream,output_format);
    patch->sync_para.avsync_duration = 0;
}
