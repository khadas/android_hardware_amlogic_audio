/*
 * Copyright (C) 2018 Amlogic Corporation.
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

#define LOG_TAG "audio_hw_primary"
//#define LOG_NDEBUG 0

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
#include "aml_data_utils.h"
#include "aml_dump_debug.h"
#include "audio_hw.h"
#include "audio_hw_dtv.h"
#include "audio_hw_profile.h"
#include "audio_hw_utils.h"
#include "dtv_patch_out.h"
#include "aml_audio_parser.h"
#include "aml_audio_resampler.h"
#include "audio_hw_ms12.h"
#include "dolby_lib_api.h"
#include "audio_dtv_ad.h"

#define TSYNC_PCRSCR "/sys/class/tsync/pts_pcrscr"
#define TSYNC_EVENT "/sys/class/tsync/event"
#define TSYNC_APTS "/sys/class/tsync/pts_audio"
#define TSYNC_VPTS "/sys/class/tsync/pts_video"
#define TSYNC_FIRSTCHECKIN_APTS "/sys/class/tsync/checkin_firstapts"
#define TSYNC_DEMUX_PCR         "/sys/class/tsync/demux_pcr"

#define PATCH_PERIOD_COUNT 4
//#define SYSTIME_CORRECTION_THRESHOLD (90000 * 10 / 100)
#define AUDIO_PTS_DISCONTINUE_THRESHOLD (90000 * 5)
#define MAX_DECODER_FRAME_LENGTH 6144

//{ RAW_OUTPUT_FORMAT
#define PCM 0  /*AUDIO_FORMAT_PCM_16_BIT*/
#define DD 4   /*AUDIO_FORMAT_AC3*/
#define AUTO 5 /*choose by sink capability/source format/Digital format*/

#define DEFAULT_DTV_OUTPUT_CLOCK    (1000*1000)
#define DEFALUT_DTV_MIN_OUT_CLOCK   (1000*1000-100*1000)
#define DEFAULT_DTV_MAX_OUT_CLOCK   (1000*1000+100*1000)
#define DEFAULT_I2S_OUTPUT_CLOCK    (256*48000)



//}

#define DTV_DECODER_PTS_LOOKUP_PATH "/sys/class/tsync/apts_lookup"
#define DTV_DECODER_CHECKIN_FIRSTAPTS_PATH "/sys/class/tsync/checkin_firstapts"
#define DTV_DECODER_TSYNC_MODE      "/sys/class/tsync/mode"

//pthread_mutex_t dtv_patch_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t dtv_cmd_mutex = PTHREAD_MUTEX_INITIALIZER;
struct cmd_list {
    struct cmd_list *next;
    int cmd;
    int cmd_num;
    int used;
    int initd;
};

struct cmd_list dtv_cmd_list = {
    .next = NULL,
    .cmd = -1,
    .cmd_num = 0,
    .used = 1,
};

const unsigned int mute_dd_frame[] = {
    0x5d9c770b, 0xf0432014, 0xf3010713, 0x2020dc62, 0x4842020, 0x57100404, 0xf97c3e1f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xfb7c3e9f, 0xf97c75fe, 0x9fcfe7f3,
    0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xfb7c3e9f, 0x3e5f9dff, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0x48149ff2, 0x2091,
    0x361e0000, 0x78bc6ddb, 0xbbbbe3f1, 0xb8, 0x0, 0x0, 0x0, 0x77770700, 0x361e8f77, 0x359f6fdb, 0xd65a6bad, 0x5a6badb5, 0x6badb5d6, 0xa0b5d65a, 0x1e000000, 0xbc6ddb36,
    0xbbe3f178, 0xb8bb, 0x0, 0x0, 0x0, 0x77070000, 0x1e8f7777, 0x9f6fdb36, 0x5a6bad35, 0xa6b5d6, 0x0, 0xb66de301, 0x1e8fc7db, 0x80bbbb3b, 0x0, 0x0,
    0x0, 0x0, 0x78777777, 0xb66de3f1, 0xd65af3f9, 0x5a6badb5, 0x6badb5d6, 0xadb5d65a, 0x5a6b, 0x6de30100, 0x8fc7dbb6, 0xbbbb3b1e, 0x80, 0x0, 0x0, 0x0,
    0x77777700, 0x6de3f178, 0x5af3f9b6, 0x6badb5d6, 0x605a, 0x1e000000, 0xbc6ddb36, 0xbbe3f178, 0xb8bb, 0x0, 0x0, 0x0, 0x77070000, 0x1e8f7777, 0x9f6fdb36, 0x5a6bad35,
    0x6badb5d6, 0xadb5d65a, 0xb5d65a6b, 0xa0, 0x6ddb361e, 0xe3f178bc, 0xb8bbbb, 0x0, 0x0, 0x0, 0x7000000, 0x8f777777, 0x6fdb361e, 0x6bad359f, 0xa6b5d65a, 0x0,
    0x6de30100, 0x8fc7dbb6, 0xbbbb3b1e, 0x80, 0x0, 0x0, 0x0, 0x77777700, 0x6de3f178, 0x5af3f9b6, 0x6badb5d6, 0xadb5d65a, 0xb5d65a6b, 0x5a6bad, 0xe3010000, 0xc7dbb66d,
    0xbb3b1e8f, 0x80bb, 0x0, 0x0, 0x0, 0x77770000, 0xe3f17877, 0xf3f9b66d, 0xadb5d65a, 0x605a6b, 0x0, 0x6ddb361e, 0xe3f178bc, 0xb8bbbb, 0x0, 0x0,
    0x0, 0x7000000, 0x8f777777, 0x6fdb361e, 0x6bad359f, 0xadb5d65a, 0xb5d65a6b, 0xd65a6bad, 0xa0b5, 0xdb361e00, 0xf178bc6d, 0xb8bbbbe3, 0x0, 0x0, 0x0, 0x0,
    0x77777707, 0xdb361e8f, 0xad359f6f, 0xb5d65a6b, 0x10200a6, 0x0, 0xdbb6f100, 0x8fc7e36d, 0xc0dddd1d, 0x0, 0x0, 0x0, 0x0, 0xbcbbbb3b, 0xdbb6f178, 0x6badf97c,
    0xadb5d65a, 0xb5d65a6b, 0xd65a6bad, 0xadb5, 0xb6f10000, 0xc7e36ddb, 0xdddd1d8f, 0xc0, 0x0, 0x0, 0x0, 0xbbbb3b00, 0xb6f178bc, 0xadf97cdb, 0xb5d65a6b, 0x4deb00ad
};

const unsigned int mute_ddp_frame[] = {
    0x7f01770b, 0x20e06734, 0x2004, 0x8084500, 0x404046c, 0x1010104, 0xe7630001, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0xce7f9fcf, 0x7c3e9faf,
    0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0xf37f9fcf, 0x9fcfe7ab, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x53dee7f3, 0xf0e9,
    0x6d3c0000, 0xf178dbb6, 0x7777c7e3, 0x70, 0x0, 0x0, 0x0, 0xeeee0e00, 0x6d3c1eef, 0x6b3edfb6, 0xadb5d65a, 0xb5d65a6b, 0xd65a6bad, 0x406badb5, 0x3c000000, 0x78dbb66d,
    0x77c7e3f1, 0x7077, 0x0, 0x0, 0x0, 0xee0e0000, 0x3c1eefee, 0x3edfb66d, 0xb5d65a6b, 0x20606bad, 0x0, 0xdbb66d3c, 0xc7e3f178, 0x707777, 0x0, 0x0,
    0x0, 0xe000000, 0x1eefeeee, 0xdfb66d3c, 0xd65a6b3e, 0x5a6badb5, 0x6badb5d6, 0xadb5d65a, 0x406b, 0xb66d3c00, 0xe3f178db, 0x707777c7, 0x0, 0x0, 0x0, 0x0,
    0xefeeee0e, 0xb66d3c1e, 0x5a6b3edf, 0x6badb5d6, 0x2060, 0x6d3c0000, 0xf178dbb6, 0x7777c7e3, 0x70, 0x0, 0x0, 0x0, 0xeeee0e00, 0x6d3c1eef, 0x6b3edfb6, 0xadb5d65a,
    0xb5d65a6b, 0xd65a6bad, 0x406badb5, 0x3c000000, 0x78dbb66d, 0x77c7e3f1, 0x7077, 0x0, 0x0, 0x0, 0xee0e0000, 0x3c1eefee, 0x3edfb66d, 0xb5d65a6b, 0x20606bad, 0x0,
    0xdbb66d3c, 0xc7e3f178, 0x707777, 0x0, 0x0, 0x0, 0xe000000, 0x1eefeeee, 0xdfb66d3c, 0xd65a6b3e, 0x5a6badb5, 0x6badb5d6, 0xadb5d65a, 0x406b, 0xb66d3c00, 0xe3f178db,
    0x707777c7, 0x0, 0x0, 0x0, 0x0, 0xefeeee0e, 0xb66d3c1e, 0x5a6b3edf, 0x6badb5d6, 0x2060, 0x6d3c0000, 0xf178dbb6, 0x7777c7e3, 0x70, 0x0, 0x0,
    0x0, 0xeeee0e00, 0x6d3c1eef, 0x6b3edfb6, 0xadb5d65a, 0xb5d65a6b, 0xd65a6bad, 0x406badb5, 0x3c000000, 0x78dbb66d, 0x77c7e3f1, 0x7077, 0x0, 0x0, 0x0, 0xee0e0000,
    0x3c1eefee, 0x3edfb66d, 0xb5d65a6b, 0x20606bad, 0x0, 0xdbb66d3c, 0xc7e3f178, 0x707777, 0x0, 0x0, 0x0, 0xe000000, 0x1eefeeee, 0xdfb66d3c, 0xd65a6b3e, 0x5a6badb5,
    0x6badb5d6, 0xadb5d65a, 0x406b, 0xb66d3c00, 0xe3f178db, 0x707777c7, 0x0, 0x0, 0x0, 0x0, 0xefeeee0e, 0xb66d3c1e, 0x5a6b3edf, 0x6badb5d6, 0x40, 0x7f227c55,
};

static int create_dtv_output_stream_thread(struct aml_audio_patch *patch);
static int release_dtv_output_stream_thread(struct aml_audio_patch *patch);

struct cmd_list cmd_array[16]; // max cache 16 cmd;

static unsigned long decoder_apts_lookup(unsigned int offset)
{
    unsigned int pts = 0;
    int ret;
    char buff[32];
    memset(buff, 0, 32);
    snprintf(buff, 32, "%d", offset);

    aml_sysfs_set_str(DTV_DECODER_PTS_LOOKUP_PATH, buff);
    ret = aml_sysfs_get_str(DTV_DECODER_PTS_LOOKUP_PATH, buff, sizeof(buff));

    if (ret > 0) {
        ret = sscanf(buff, "0x%x\n", &pts);
    }
    // ALOGI("decoder_apts_lookup get the pts is %x\n", pts);
    if (pts == (unsigned int) - 1) {
        pts = 0;
    }

    // adec_print("adec_apts_lookup get the pts is %lx\n", pts);

    return (unsigned long)pts;
}

static unsigned long decoder_checkin_firstapts(void)
{
    unsigned int firstapts = 0;
    int ret;
    char buff[32];

    memset(buff, 0, 32);
    ret = aml_sysfs_get_str(DTV_DECODER_CHECKIN_FIRSTAPTS_PATH, buff, sizeof(buff));

    if (ret > 0) {
        ret = sscanf(buff, "0x%x\n", &firstapts);
    }

    if (firstapts == (unsigned int) - 1) {
        firstapts = 0;
    }

    return (unsigned long)firstapts;
}

static void init_cmd_list(void)
{
    dtv_cmd_list.next = NULL;
    dtv_cmd_list.cmd = -1;
    dtv_cmd_list.cmd_num = 0;
    dtv_cmd_list.used = 0;
    dtv_cmd_list.initd = 1;
    memset(cmd_array, 0, sizeof(cmd_array));
}

static void deinit_cmd_list(void)
{
    dtv_cmd_list.next = NULL;
    dtv_cmd_list.cmd = -1;
    dtv_cmd_list.cmd_num = 0;
    dtv_cmd_list.used = 0;
    dtv_cmd_list.initd = 0;
}

static struct cmd_list *cmd_array_get(void)
{
    int index = 0;

    pthread_mutex_lock(&dtv_cmd_mutex);
    for (index = 0; index < 16; index++) {
        if (cmd_array[index].used == 0) {
            break;
        }
    }

    if (index == 16) {
        pthread_mutex_unlock(&dtv_cmd_mutex);
        return NULL;
    }
    pthread_mutex_unlock(&dtv_cmd_mutex);
    return &cmd_array[index];
}
static void cmd_array_put(struct cmd_list *list)
{
    pthread_mutex_lock(&dtv_cmd_mutex);
    list->used = 0;
    pthread_mutex_unlock(&dtv_cmd_mutex);
}

static void _add_cmd_to_tail(struct cmd_list *node)
{
    struct cmd_list *list = &dtv_cmd_list;
    pthread_mutex_lock(&dtv_cmd_mutex);
    while (list->next != NULL) {
        list = list->next;
    }
    list->next = node;
    dtv_cmd_list.cmd_num++;
    pthread_mutex_unlock(&dtv_cmd_mutex);
}

int dtv_patch_add_cmd(int cmd)
{
    struct cmd_list *list = NULL;

    if (dtv_cmd_list.initd == 0) {
        return 0;
    }

    list = cmd_array_get();
    if (list == NULL) {
        ALOGI("can't get cmd list, add by live \n");
        return -1;
    }
    ALOGI("add by live dtv_patch_add_cmd the cmd is %d \n", cmd);
    list->cmd = cmd;
    list->next = NULL;
    list->used = 1;

    _add_cmd_to_tail(list);
    return 0;
}

int dtv_patch_get_cmd(void)
{
    int cmd = AUDIO_DTV_PATCH_CMD_NUM;
    struct cmd_list *list = NULL;
    ALOGI("enter dtv_patch_get_cmd funciton now\n");
    pthread_mutex_lock(&dtv_cmd_mutex);
    list = dtv_cmd_list.next;
    if (list != NULL) {
        dtv_cmd_list.next = list->next;
        cmd = list->cmd;
        dtv_cmd_list.cmd_num--;
    } else {
        cmd =  AUDIO_DTV_PATCH_CMD_NULL;
        pthread_mutex_unlock(&dtv_cmd_mutex);
        return cmd;
    }
    pthread_mutex_unlock(&dtv_cmd_mutex);
    cmd_array_put(list);
    ALOGI("leave dtv_patch_get_cmd the cmd is %d \n", cmd);
    return cmd;
}
int dtv_patch_cmd_is_empty(void)
{
    pthread_mutex_lock(&dtv_cmd_mutex);
    if (dtv_cmd_list.next == NULL) {
        pthread_mutex_unlock(&dtv_cmd_mutex);
        return 1;
    }
    pthread_mutex_unlock(&dtv_cmd_mutex);
    return 0;
}
static int dtv_patch_buffer_space(void *args)
{
    int left = 0;
    struct aml_audio_patch *patch = (struct aml_audio_patch *)args;
    ring_buffer_t *ringbuffer = &(patch->aml_ringbuffer);
    left = get_buffer_write_space(ringbuffer);
    return left;
}

unsigned long dtv_hal_get_pts(struct aml_audio_patch *patch,
                              unsigned int lantcy)
{
    unsigned long val, offset;
    unsigned long pts;
    int data_width, channels, samplerate;
    unsigned long long frame_nums;
    unsigned long delay_pts;
    char value[PROPERTY_VALUE_MAX];

    channels = 2;
    samplerate = 48;
    data_width = 2;

    offset = patch->decoder_offset;

    // when first  look up apts,set offset 0
    if (!patch->first_apts_lookup_over) {
        offset = 0;
    }
    offset = decoder_apts_lookup(offset);

    pts = offset;
    if (!patch->first_apts_lookup_over) {
        if (pts == 0) {
            pts = decoder_checkin_firstapts();
            ALOGI("pts = 0,so get checkin_firstapts:0x%lx", pts);
        }
        patch->last_valid_pts = pts;
        patch->first_apts_lookup_over = 1;
        return pts;
    }

    if (pts == 0 || pts == patch->last_valid_pts) {
        if (patch->last_valid_pts) {
            pts = patch->last_valid_pts;
        }
        frame_nums = (patch->outlen_after_last_validpts / (data_width * channels));
        pts += (frame_nums * 90 / samplerate);
        // ALOGI("decode_offset:%d out_pcm:%d   pts:%lx,audec->last_valid_pts %lx\n",
        //       patch->decoder_offset, patch->outlen_after_last_validpts, pts,
        //       patch->last_valid_pts);
        return pts;
    }
    patch->last_valid_pts = pts;
    val = pts - lantcy * 90;

    patch->outlen_after_last_validpts = 0;
    // ALOGI("====get pts:%lx offset:%d lan %d \n", val, patch->decoder_offset,
    //       lantcy);
    return val;
}

enum {
    DIRECT_SPEED = 0, // DERIECT_SPEED
    DIRECT_SLOW,
    DIRECT_NORMAL,
};

static unsigned int compare_clock(unsigned int clock1, unsigned int clock2)
{
    if (clock1 == clock2) {
        return 1 ;
    }

    if (clock1 > clock2) {
        if (clock1 < clock2 + 5) {
            return 1;
        }
    }

    if (clock1 < clock2) {
        if (clock2 < clock1 + 5) {
            return 1;
        }
    }

    return 0;
}


static void dtv_adjust_output_clock(struct aml_audio_patch * patch, int direct, int step)
{
    struct audio_hw_device *adev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) adev;
    struct aml_mixer_handle * handle = &(aml_dev->alsa_mixer);
    int output_clock;

    unsigned int i2s_current_clock = 0;
    unsigned int spidif_current_clock = 0;

    i2s_current_clock = aml_mixer_ctrl_get_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL);
    spidif_current_clock = aml_mixer_ctrl_get_int(handle, AML_MIXER_ID_CHANGE_SPIDIF_PLL);
    // ALOGI("%s bluekde %d direct %d output_clock %d patch->dtv_default_i2s_clock  %d i2s_current_clock %d \n",
    //               __FUNCTION__, __LINE__, direct, step, patch->dtv_default_i2s_clock, i2s_current_clock);
    if (direct == DIRECT_SPEED) {
        // ALOGI("%s direct %d step %d patch->dtv_output_clock  %d i2s_current_clock %d \n",
        // __FUNCTION__, direct, step, patch->dtv_output_clock,i2s_current_clock);
        if (compare_clock(i2s_current_clock, patch->dtv_default_i2s_clock)) {
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + step * 1000;
            patch->dtv_output_clock = i2s_current_clock + step * 1000;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + step * 500;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_SPIDIF_PLL, output_clock);
            ALOGI(" bluekde  speed output_clock %d patch->dtv_default_i2s_clock  %d i2s_current_clock %d \n",
                  output_clock, patch->dtv_default_i2s_clock, i2s_current_clock);
        } else if (i2s_current_clock < patch->dtv_default_i2s_clock) {
            int value = patch->dtv_default_i2s_clock - i2s_current_clock;
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + value;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + value;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_SPIDIF_PLL, output_clock);
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + step * 1000;
            patch->dtv_output_clock = patch->dtv_default_i2s_clock + step * 1000;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + step * 500;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_SPIDIF_PLL, output_clock);
            ALOGI(" bluekde  speed output_clock %d patch->dtv_default_i2s_clock  %d i2s_current_clock %d \n",
                  output_clock, patch->dtv_default_i2s_clock, i2s_current_clock);

        } else {
            return ;
        }
    } else if (direct == DIRECT_SLOW) {
        // ALOGI("%s direct %d step %d patch->dtv_output_clock  %d i2s_current_clock %d \n",
        // __FUNCTION__, direct, step, patch->dtv_output_clock,i2s_current_clock);
        if (compare_clock(i2s_current_clock, patch->dtv_default_i2s_clock)) {
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - step * 1000;
            patch->dtv_output_clock = i2s_current_clock - step * 1000;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);

            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - step * 500;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_SPIDIF_PLL, output_clock);
            ALOGI(" bluekde  slow output_clock %d patch->dtv_default_i2s_clock  %d i2s_current_clock %d \n",
                  output_clock, patch->dtv_default_i2s_clock, i2s_current_clock);
        } else if (i2s_current_clock > patch->dtv_default_i2s_clock) {
            int value = i2s_current_clock - patch->dtv_default_i2s_clock;
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - value;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - value;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_SPIDIF_PLL, output_clock);

            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - step * 1000;
            patch->dtv_output_clock = patch->dtv_default_i2s_clock - step * 1000;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - step * 500;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_SPIDIF_PLL, output_clock);
            ALOGI(" bluekde  slow output_clock %d patch->dtv_default_i2s_clock  %d i2s_current_clock %d \n",
                  output_clock, patch->dtv_default_i2s_clock, i2s_current_clock);

        } else {
            return ;
        }
    } else {
        //    ALOGI("%s direct %d step %d patch->dtv_output_clock  %d i2s_current_clock %d \n",
        // __FUNCTION__, direct, step, patch->dtv_output_clock,i2s_current_clock);
        if (compare_clock(i2s_current_clock, patch->dtv_default_i2s_clock)) {
            return ;
        }

        if (i2s_current_clock > patch->dtv_default_i2s_clock) {
            int value = i2s_current_clock - patch->dtv_default_i2s_clock;
            if (value < 5) {
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - value;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
            value = spidif_current_clock - patch->dtv_default_spidif_clock;
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK - value;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_SPIDIF_PLL, output_clock);
            ALOGI(" bluekde  normal output_clock %d patch->dtv_default_i2s_clock  %d i2s_current_clock %d \n",
                  output_clock, patch->dtv_default_i2s_clock, i2s_current_clock);
            ALOGI(" bluekde  normal output_clock %d dtv_default_spidif_clock  %d spidif_current_clock %d \n",
                  output_clock, patch->dtv_default_spidif_clock, spidif_current_clock);
        } else if (i2s_current_clock < patch->dtv_default_i2s_clock) {
            int value = patch->dtv_default_i2s_clock - i2s_current_clock;
            if (value < 5) {
                return;
            }
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + value;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_I2S_PLL, output_clock);
            value = patch->dtv_default_spidif_clock - spidif_current_clock;
            output_clock = DEFAULT_DTV_OUTPUT_CLOCK + value;
            aml_mixer_ctrl_set_int(handle, AML_MIXER_ID_CHANGE_SPIDIF_PLL, output_clock);
            ALOGI(" bluekde  normal output_clock %d patch->dtv_default_i2s_clock  %d i2s_current_clock %d \n",
                  output_clock, patch->dtv_default_i2s_clock, i2s_current_clock);
            ALOGI(" bluekde  normal output_clock %d dtv_default_spidif_clock  %d spidif_current_clock %d \n",
                  output_clock, patch->dtv_default_spidif_clock, spidif_current_clock);
        } else {
            return ;
        }
    }

    return;
}



void process_ac3_sync(struct aml_audio_patch *patch, unsigned long pts)
{

    int channel_count = 2;
    int bytewidth = 2;
    int symbol = 48;
    char tempbuf[128];
    unsigned int pcrpts;
    unsigned int pts_diff;
    unsigned long cur_out_pts;

    if (patch->dtv_first_apts_flag == 0) {
        sprintf(tempbuf, "AUDIO_START:0x%x", (unsigned int)pts);
        ALOGI("dtv set tsync -> %s", tempbuf);
        if (sysfs_set_sysfs_str(TSYNC_EVENT, tempbuf) == -1) {
            ALOGE("set AUDIO_START failed \n");
        }
        patch->dtv_first_apts_flag = 1;
    } else {
        cur_out_pts = pts;
        get_sysfs_uint(TSYNC_PCRSCR, &pcrpts);
        if (pcrpts > cur_out_pts) {
            pts_diff = pcrpts - cur_out_pts;
        } else {
            pts_diff = cur_out_pts - pcrpts;
        }
        ALOGI("process_ac3_sync, diff:%d,pcrpts %x,curpts %lx", (pts_diff), pcrpts, cur_out_pts);
        if (pts_diff < SYSTIME_CORRECTION_THRESHOLD) {
            // now the av is syncd ,so do nothing;
            dtv_adjust_output_clock(patch, DIRECT_NORMAL, 50);
        } else if (pts_diff > SYSTIME_CORRECTION_THRESHOLD &&
                   pts_diff < AUDIO_PTS_DISCONTINUE_THRESHOLD) {
            if (pcrpts > cur_out_pts) {
                dtv_adjust_output_clock(patch, DIRECT_SPEED, 50);
            } else {
                dtv_adjust_output_clock(patch, DIRECT_SLOW, 50);
            }
        } else {
            sprintf(tempbuf, "AUDIO_TSTAMP_DISCONTINUITY:0x%lx", (unsigned long)pts);

            if (sysfs_set_sysfs_str(TSYNC_EVENT, tempbuf) == -1) {
                ALOGI("unable to open file %s,err: %s", TSYNC_EVENT, strerror(errno));
            }
        }
    }
}

void process_pts_sync(unsigned int pcm_lancty, struct aml_audio_patch *patch,
                      unsigned int rbuf_level)
{
    int channel_count = 2;
    int bytewidth = 2;
    int sysmbol = 48;
    char tempbuf[128];
    unsigned int pcrpts, apts;
    unsigned int calc_len = 0;
    unsigned long pts = 0, lookpts;
    unsigned long cache_pts = 0;
    unsigned long cur_out_pts = 0;

    pts = lookpts = dtv_patch_get_pts();
    if (pts == patch->last_valid_pts) {
        ALOGI("dtv_patch_get_pts pts  -> %lx", pts);
    }
    if (patch->dtv_first_apts_flag == 0) {
        if (pts == 0) {
            pts = decoder_checkin_firstapts();
            ALOGI("pts = 0,so get checkin_firstapts:0x%lx", pts);
        }
        sprintf(tempbuf, "AUDIO_START:0x%x", (unsigned int)pts);
        ALOGI("dtv set tsync -> %s", tempbuf);
        if (sysfs_set_sysfs_str(TSYNC_EVENT, tempbuf) == -1) {
            ALOGE("set AUDIO_START failed \n");
        }
        patch->dtv_pcm_readed = 0;
        patch->dtv_first_apts_flag = 1;
        patch->last_valid_pts = pts;
    } else {
        unsigned int pts_diff;
#if 1
        if (pts != (unsigned long) - 1) {
            calc_len = (unsigned int)rbuf_level;
            cache_pts = (calc_len * 90) / (sysmbol * channel_count * bytewidth);
            if (pts > cache_pts) {
                cur_out_pts = pts - cache_pts;
            } else {
                return;
            }
            if (cur_out_pts > pcm_lancty * 90) {
                cur_out_pts = cur_out_pts - pcm_lancty * 90;
            } else {
                return;
            }
            patch->last_valid_pts = cur_out_pts;
            patch->dtv_pcm_readed = 0;
        } else {
            pts = patch->last_valid_pts;
            calc_len = patch->dtv_pcm_readed;
            cache_pts = (calc_len * 90) / (sysmbol * channel_count * bytewidth);
            cur_out_pts = pts + cache_pts;
            return;
        }

        get_sysfs_uint(TSYNC_PCRSCR, &pcrpts);

        if (pcrpts > cur_out_pts) {
            pts_diff = pcrpts - cur_out_pts;
        } else {
            pts_diff = cur_out_pts - pcrpts;
        }

        ALOGI("process_pts_sync, diff:%d,pcrpts %x,curpts %lx", (pts_diff), pcrpts, cur_out_pts);

        if (pts_diff < SYSTIME_CORRECTION_THRESHOLD) {
            dtv_adjust_output_clock(patch, DIRECT_NORMAL, 50);
            // now the av is syncd ,so do nothing;
        } else if (pts_diff > SYSTIME_CORRECTION_THRESHOLD &&
                   pts_diff < AUDIO_PTS_DISCONTINUE_THRESHOLD) {
            if (pcrpts > cur_out_pts) {
                dtv_adjust_output_clock(patch, DIRECT_SPEED, 50);
            } else {
                dtv_adjust_output_clock(patch, DIRECT_SLOW, 50);
            }
        } else {
            sprintf(tempbuf, "AUDIO_TSTAMP_DISCONTINUITY:0x%lx",
                    (unsigned long)cur_out_pts);
            ALOGI("AUDIO_TSTAMP_DISCONTINUITY the apts %lx pcrpts %x pts_diff %d \n",
                  cur_out_pts, pcrpts, pts_diff);
            if (sysfs_set_sysfs_str(TSYNC_EVENT, tempbuf) == -1) {
                ALOGI("unable to open file %s,err: %s", TSYNC_EVENT, strerror(errno));
            }
        }
#endif
    }
}

static bool dtv_is_pcrmaster(void)
{
    char tsync_mode_str[12];
    int tsync_mode;
    char buf[64];
    if (sysfs_get_sysfs_str(DTV_DECODER_TSYNC_MODE, buf, sizeof(buf)) == -1) {
        ALOGI("dtv_is_pcrmaster is 22");
        return false;
    }
    if (sscanf(buf, "%d: %s", &tsync_mode, tsync_mode_str) < 1) {
        return false;
    }
    if (tsync_mode == 2) {
        ALOGI("dtv_is_pcrmaster is true");
        return true;
    }
    return true; //false;
}
static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    audio_format_t afmt = get_output_format((struct audio_stream_out *)stream);
    const struct aml_stream_out *out = (const struct aml_stream_out *)stream;
    snd_pcm_sframes_t frames = out_get_latency_frames(stream);
    if (afmt == AUDIO_FORMAT_E_AC3) {
        return (frames * 1000) / 4 / out->config.rate;
    } else {
        return (frames * 1000) / out->config.rate;
    }
}

static int dtv_patch_pcm_wirte(unsigned char *pcm_data, int size,
                               int symbolrate, int channel, void *args)
{
    struct aml_audio_patch *patch = (struct aml_audio_patch *)args;
    struct audio_hw_device *dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    ring_buffer_t *ringbuffer = &(patch->aml_ringbuffer);
    int left, need_resample;
    int write_size, return_size;
    unsigned char *write_buf;
    int16_t tmpbuf[OUTPUT_BUFFER_SIZE];
    int valid_paramters = 1;
    write_buf = pcm_data;
    if (pcm_data == NULL || size == 0) {
        return 0;
    }

    if (patch->dtv_decoder_state == AUDIO_DTV_PATCH_DECODER_STATE_INIT) {
        return 0;
    }

    patch->sample_rate = symbolrate;
    // In the case of fast switching channels such as mpeg/dra/..., there may be an
    // error "symbolrate" and "channel" paramters, so add the check to avoid it.
    if (symbolrate > 96000 || symbolrate < 8000) {
        valid_paramters = 0;
    }
    if (channel > 8 || channel < 1) {
        valid_paramters = 0;
    }
    patch->chanmask = channel;
    if (patch->sample_rate != 48000) {
        need_resample = 1;
    } else {
        need_resample = 0;
    }
    left = get_buffer_write_space(ringbuffer);

    if (left <= 0) {
        return 0;
    }
    if (need_resample == 0 && patch->chanmask == 1) {
        if (left >= 2 * size) {
            write_size = size;
        } else {
            write_size = left / 2;
        }
    } else if (need_resample == 1 && patch->chanmask == 2) {
        if (left >= size * 48000 / patch->sample_rate) {
            write_size = size;
        } else {
            return 0;
        }

    } else if (need_resample == 1 && patch->chanmask == 1) {
        if (left >= 2 * size * 48000 / patch->sample_rate) {
            write_size = size;
        } else {
            return 0;
        }
    } else {
        if (left >= size) {
            write_size = size;
        } else {
            write_size = left;
        }
    }

    return_size = write_size;

    if ((patch->aformat != AUDIO_FORMAT_E_AC3 &&
         patch->aformat != AUDIO_FORMAT_AC3 &&
         patch->aformat != AUDIO_FORMAT_DTS) && valid_paramters) {
        if (patch->chanmask == 1) {
            int16_t *buf = (int16_t *)write_buf;
            int i = 0, samples_num;
            samples_num = write_size / (patch->chanmask * sizeof(int16_t));
            for (; i < samples_num; i++) {
                tmpbuf[2 * (samples_num - i) - 1] = buf[samples_num - i - 1];
                tmpbuf[2 * (samples_num - i) - 2] = buf[samples_num - i - 1];
            }
            write_size = write_size * 2;
            write_buf = (unsigned char *)tmpbuf;
        }
        if (need_resample == 1) {
            if (patch->dtv_resample.input_sr != (unsigned int)patch->sample_rate) {
                patch->dtv_resample.input_sr = patch->sample_rate;
                patch->dtv_resample.output_sr = 48000;
                patch->dtv_resample.channels = 2;
                resampler_init(&patch->dtv_resample);
            }
            if (!patch->resample_outbuf) {
                patch->resample_outbuf =
                    (unsigned char *)malloc(OUTPUT_BUFFER_SIZE * 3);
                if (!patch->resample_outbuf) {
                    ALOGE("malloc buffer failed\n");
                    return -1;
                }
                memset(patch->resample_outbuf, 0, OUTPUT_BUFFER_SIZE * 3);
            }
            int out_frame = write_size >> 2;
            out_frame = resample_process(&patch->dtv_resample, out_frame,
                                         (int16_t *)write_buf,
                                         (int16_t *)patch->resample_outbuf);
            write_size = out_frame << 2;
            write_buf = patch->resample_outbuf;
        }
    }

    if (aml_dev->tv_mute) {
        memset(write_buf, 0, write_size);
        if (aml_dev->need_reset_ringbuffer) {
            ring_buffer_reset(ringbuffer);
            aml_dev->need_reset_ringbuffer = 0;
        }
    }

    ring_buffer_write(ringbuffer, (unsigned char *)write_buf, write_size,
                      UNCOVER_WRITE);

    // if ((patch->aformat != AUDIO_FORMAT_E_AC3)
    //     && (patch->aformat != AUDIO_FORMAT_AC3) &&
    //     (patch->aformat != AUDIO_FORMAT_DTS)) {
    //     int abuf_level = get_buffer_read_space(ringbuffer);
    //     process_pts_sync(0, patch, 0);
    // }

    if (aml_getprop_bool("media.audiohal.outdump")) {
        FILE *fp1 = fopen("/data/audio_dtv.pcm", "a+");
        if (fp1) {
            int flen = fwrite((char *)write_buf, 1, write_size, fp1);
            ALOGI("%s buffer %p size %zu\n", __FUNCTION__, write_buf, write_size);
            fclose(fp1);
        }
    }
    // ALOGI("[%s]ring_buffer_write now wirte %d to ringbuffer\
    //  now\n",
    //       __FUNCTION__, write_size);
    patch->dtv_pcm_writed += return_size;

    pthread_cond_signal(&patch->cond);
    return return_size;
}

static int dtv_patch_raw_wirte(unsigned char *raw_data, int size, void *args)
{
    struct aml_audio_patch *patch = (struct aml_audio_patch *)args;
    ring_buffer_t *ringbuffer = &(patch->aml_ringbuffer);
    int left;
    int write_size;
    if (raw_data == NULL) {
        return 0;
    }

    if (size == 0) {
        return 0;
    }

    left = get_buffer_write_space(ringbuffer);
    if (left > size) {
        write_size = size;
    } else {
        write_size = left;
    }

    ring_buffer_write(ringbuffer, (unsigned char *)raw_data, write_size,
                      UNCOVER_WRITE);
    return write_size;
}
static int raw_dump_fd = -1;
void dump_raw_buffer(const void *data_buf, int size)
{
    ALOGI("enter the dump_raw_buffer save %d len data now\n", size);
    if (raw_dump_fd < 0) {
        if (access("/data/raw.es", 0) == 0) {
            raw_dump_fd = open("/data/raw.es", O_RDWR);
            if (raw_dump_fd < 0) {
                ALOGE("%s, Open device file \"%s\" error: %s.\n", __FUNCTION__,
                      "/data/raw.es", strerror(errno));
            }
        } else {
            raw_dump_fd = open("/data/raw.es", O_RDWR);
            if (raw_dump_fd < 0) {
                ALOGE("%s, Create device file \"%s\" error: %s.\n", __FUNCTION__,
                      "/data/raw.es", strerror(errno));
            }
        }
    }

    if (raw_dump_fd >= 0) {
        write(raw_dump_fd, data_buf, size);
    }
    return;
}

extern int do_output_standby_l(struct audio_stream *stream);
extern void adev_close_output_stream_new(struct audio_hw_device *dev,
        struct audio_stream_out *stream);
extern int adev_open_output_stream_new(struct audio_hw_device *dev,
                                       audio_io_handle_t handle __unused,
                                       audio_devices_t devices,
                                       audio_output_flags_t flags,
                                       struct audio_config *config,
                                       struct audio_stream_out **stream_out,
                                       const char *address __unused);
ssize_t out_write_new(struct audio_stream_out *stream, const void *buffer,
                      size_t bytes);

void *audio_dtv_patch_output_threadloop(void *data)
{
    struct aml_audio_patch *patch = (struct aml_audio_patch *)data;
    struct audio_hw_device *dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    ring_buffer_t *ringbuffer = &(patch->aml_ringbuffer);
    struct audio_stream_out *stream_out = NULL;
    struct aml_stream_out *aml_out = NULL;
    struct audio_config stream_config;
    struct timespec ts;
    int write_bytes = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
    int ret;

    ALOGI("++%s live ", __FUNCTION__);
    // FIXME: get actual configs
    stream_config.sample_rate = 48000;
    stream_config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    stream_config.format = AUDIO_FORMAT_PCM_16_BIT;
    /*
    may we just exit from a direct active stream playback
    still here.we need remove to standby to new playback
    */
    pthread_mutex_lock(&aml_dev->lock);
    aml_out = direct_active(aml_dev);
    if (aml_out) {
        ALOGI("%s live stream %p active,need standby aml_out->usecase:%d ",
              __func__, aml_out, aml_out->usecase);
        pthread_mutex_lock(&aml_out->lock);
        do_output_standby_l((struct audio_stream *)aml_out);
        pthread_mutex_unlock(&aml_out->lock);
        if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
            // get_dolby_ms12_cleanup(&aml_dev->ms12);
        }
        if (aml_dev->need_remove_conti_mode == true) {
            ALOGI("%s,conntinous mode still there,release ms12 here", __func__);
            aml_dev->need_remove_conti_mode = false;
            aml_dev->continuous_audio_mode = 0;
        }
    } else {
        ALOGI("++%s live cant get the aml_out now!!!\n ", __FUNCTION__);
    }
    aml_dev->mix_init_flag = false;
    pthread_mutex_unlock(&aml_dev->lock);
#ifdef TV_AUDIO_OUTPUT
    patch->output_src = AUDIO_DEVICE_OUT_SPEAKER;
#else
    patch->output_src = AUDIO_DEVICE_OUT_AUX_DIGITAL;
#endif
    ret = adev_open_output_stream_new(patch->dev, 0,
                                      patch->output_src,        // devices_t
                                      AUDIO_OUTPUT_FLAG_DIRECT, // flags
                                      &stream_config, &stream_out, NULL);
    if (ret < 0) {
        ALOGE("live open output stream fail, ret = %d", ret);
        goto exit_open;
    }

    ALOGI("++%s live create a output stream success now!!!\n ", __FUNCTION__);

    patch->out_buf_size = write_bytes * EAC3_MULTIPLIER;
    patch->out_buf = calloc(1, patch->out_buf_size);
    if (!patch->out_buf) {
        ret = -ENOMEM;
        goto exit_outbuf;
    }
    memset(&patch->dtv_resample, 0, sizeof(struct resample_para));
    patch->resample_outbuf = NULL;
    ALOGI("++%s live start output pcm now patch->output_thread_exit %d!!!\n ",
          __FUNCTION__, patch->output_thread_exit);

    prctl(PR_SET_NAME, (unsigned long)"audio_output_patch");

    while (!patch->output_thread_exit) {
        if (patch->dtv_decoder_state == AUDIO_DTV_PATCH_DECODER_STATE_PAUSE) {
            usleep(1000);
            continue;
        }

        pthread_mutex_lock(&(patch->dtv_output_mutex));
        int period_mul =
            (patch->aformat == AUDIO_FORMAT_E_AC3) ? EAC3_MULTIPLIER : 1;
        if ((patch->aformat == AUDIO_FORMAT_AC3) ||
            (patch->aformat == AUDIO_FORMAT_E_AC3)) {
            //ALOGI("AD %d %d %d", aml_dev->dolby_lib_type, aml_dev->dual_decoder_support, aml_dev->sub_apid);
            if ((eDolbyMS12Lib == aml_dev->dolby_lib_type && aml_dev->dual_decoder_support
                    && VALID_PID(aml_dev->sub_apid)
                    /*&& aml_dev->associate_audio_mixing_enable*/)) {
                unsigned char main_head[128];
                unsigned char ad_head[128];
                int main_frame_size = 0, last_main_frame_size = 0, main_head_offset = 0,main_head_left = 0;
                int ad_frame_size = 0, ad_head_offset = 0,ad_head_left = 0;
                unsigned char mixbuffer[MAX_DECODER_FRAME_LENGTH*4];
                unsigned char ad_buffer[MAX_DECODER_FRAME_LENGTH*4];
                uint16_t *p16_mixbuff = NULL;
                uint32_t *p32_mixbuff = NULL;
                int main_size = 0, ad_size = 0, mix_size = 0;
                int dd_bsmod = 0,remain_size = 0;
                int main_avail = get_buffer_read_space(ringbuffer);
                int ad_avail = dtv_assoc_get_avail();
                dtv_assoc_get_main_frame_size(&last_main_frame_size);
                //ALOGI("AD main_avail=%d ad_avail=%d last_main_frame_size = %d",
                        //main_avail, ad_avail, last_main_frame_size);
                if ((last_main_frame_size == 0 && main_avail >= 6144)
                        || (last_main_frame_size != 0 && main_avail >= last_main_frame_size)) {
                    if (!patch->first_apts_lookup_over) {
                        unsigned int first_checkinapts = 0xffffffff;
                        unsigned int demux_pcr = 0xffffffff;
                        int diff = 0;
                        char buff[32];
                        int ret = 0;
                        ret = aml_sysfs_get_str(TSYNC_FIRSTCHECKIN_APTS, buff, sizeof(buff));
                        if (ret > 0) {
                            ret = sscanf(buff, "0x%x\n", &first_checkinapts);
                        }

                        ret = aml_sysfs_get_str(TSYNC_DEMUX_PCR, buff, sizeof(buff));
                        if (ret > 0) {
                            ret = sscanf(buff, "0x%x\n", &demux_pcr);
                        }

                        ALOGI("demux_pcr %x first_checkinapts %x\n", demux_pcr, first_checkinapts);
                        if ((first_checkinapts != 0xffffffff) || (demux_pcr != 0xffffffff)) {
                            if (first_checkinapts > demux_pcr) {
                                unsigned diff = first_checkinapts - demux_pcr;
                                if (diff  < AUDIO_PTS_DISCONTINUE_THRESHOLD) {
                                    ALOGI("hold the aduio for cache data\n");
                                    pthread_mutex_unlock(&(patch->dtv_output_mutex));
                                    usleep(10000);
                                    continue;
                                }
                            } else {
                                unsigned diff = demux_pcr - first_checkinapts;
                                aml_dev->dtv_droppcm_size = diff * 48 * 2 * 2 / 90;
                            }
                        }
                    }

                    //dtv_assoc_get_main_frame_size(&main_frame_size);
                    //main_frame_size = 0, get from data
                    while (main_frame_size == 0 && main_avail >= 32) {
                        memset(main_head, 0, 32);
                        ret = ring_buffer_read(ringbuffer, main_head, 32);
                        main_frame_size = dcv_decoder_get_framesize(main_head,
                                ret, &main_head_offset);
                        main_avail -= 32;
                        if (main_frame_size != 0) {
                            main_head_left = ret-main_head_offset;
                            //ALOGI("AD main_frame_size=%d  ", main_frame_size);
                        }
                    }
                    dtv_assoc_set_main_frame_size(main_frame_size);

                    if (main_frame_size > 0 && (main_avail >= main_frame_size - main_head_left)) {
                        //dtv_assoc_set_main_frame_size(main_frame_size);
                        //dtv_assoc_set_ad_frame_size(ad_frame_size);
                        //read left of frame;
                        if (main_head_left > 0)
                            memcpy(patch->out_buf, main_head + main_head_offset, main_head_left);
                        ret = ring_buffer_read(ringbuffer, (unsigned char *)patch->out_buf + main_head_left ,
                                main_frame_size - main_head_left);
                        if (ret == 0) {
                            pthread_mutex_unlock(&(patch->dtv_output_mutex));
                            /*ALOGE("%s(), ring_buffer read 0 data!", __func__);*/
                            usleep(1000);
                            continue;
                        }
                        main_size = ret + main_head_left;
                    } else {
                        /*ALOGE("%s(), ring_buffer has not enough data! %d in_avail()", __func__,
                               avail);*/
                        pthread_mutex_unlock(&(patch->dtv_output_mutex));
                        usleep(1000);
                        continue;
                    }

                    if (ad_avail > 0) {
                        //dtv_assoc_get_ad_frame_size(&ad_frame_size);
                        //ad_frame_size = 0, get from data
                        while (ad_frame_size == 0 && ad_avail >= 32) {
                            memset(ad_head, 0, 32);
                            ret = dtv_assoc_read(ad_head, 32);
                            ad_frame_size = dcv_decoder_get_framesize(ad_head,
                                    ret, &ad_head_offset);
                            ad_avail -= 32;
                            if (ad_frame_size != 0) {
                                ad_head_left = ret - ad_head_offset;
                                //ALOGI("AD ad_frame_size=%d  ", ad_frame_size);
                            }
                        }
                    }

                    memset(ad_buffer,0,MAX_DECODER_FRAME_LENGTH*4);
                    if (ad_frame_size > 0 && (ad_avail >= ad_frame_size - ad_head_left)) {
                        if (ad_head_left > 0)
                            memcpy(ad_buffer, ad_head + ad_head_offset, ad_head_left);
                        ret = dtv_assoc_read(ad_buffer + ad_head_left, ad_frame_size - ad_head_left);
                        if (ret == 0) {
                            ad_size = 0;
                        } else {
                            ad_size = ret + ad_head_left;
                        }
                    } else {
                        ad_size = 0;
                    }

                    {
                        unsigned long pts = 0;
                        aml_out = (struct aml_stream_out *)stream_out;
                        if (aml_out != NULL) {
                            unsigned int lancty = out_get_latency(&(aml_out->stream));
                            pts = dtv_hal_get_pts(patch, lancty);
                            process_ac3_sync(patch, pts);
                        }
                    }
                    {
                        struct aml_stream_out *aml_out = (struct aml_stream_out *)stream_out;
                        if (aml_out->hal_internal_format != patch->aformat) {
                            aml_out->hal_format = aml_out->hal_internal_format = patch->aformat;
                           get_sink_format(stream_out);
                        }
                    }

                    //package iec61937
                    memset(mixbuffer, 0, MAX_DECODER_FRAME_LENGTH*4);
                    //papbpcpd
                    p16_mixbuff = (uint16_t*)mixbuffer;
                    p16_mixbuff[0] = 0xf872;
                    p16_mixbuff[1] = 0x4e1f;
                    if (patch->aformat == AUDIO_FORMAT_AC3) {
                        dd_bsmod = 6;
                        p16_mixbuff[2] = ((dd_bsmod & 7) << 8) | 1;
                        if (ad_size == 0)
                            p16_mixbuff[3] = (main_size + sizeof(mute_dd_frame))*8;
                        else
                            p16_mixbuff[3] = (main_size + ad_size)*8;
                    } else {
                        dd_bsmod = 12;
                        p16_mixbuff[2] = ((dd_bsmod & 7) << 8) | 21;
                        if (ad_size == 0)
                            p16_mixbuff[3] = main_size + sizeof(mute_ddp_frame);
                        else
                            p16_mixbuff[3] = main_size + ad_size;
                    }
                    mix_size +=8;
                    //main
                    memcpy(mixbuffer + mix_size, patch->out_buf, main_size);
                    mix_size += main_size;
                    //ad
                    if (ad_size == 0) {
                        if (patch->aformat == AUDIO_FORMAT_AC3) {
                            memcpy(mixbuffer + mix_size, mute_dd_frame, sizeof(mute_dd_frame));
                        } else {
                            memcpy(mixbuffer + mix_size, mute_ddp_frame, sizeof(mute_ddp_frame));
                        }
                    } else {
                        memcpy(mixbuffer + mix_size, ad_buffer, ad_size);
                    }

                    if (patch->aformat == AUDIO_FORMAT_AC3) {//ac3 iec61937 package size 6144
                        ret = out_write_new(stream_out, mixbuffer, MAX_DECODER_FRAME_LENGTH);
                    } else {//eac3 iec61937 package size 6144*4
                        ret = out_write_new(stream_out, mixbuffer, MAX_DECODER_FRAME_LENGTH*4);
                    }

                    if ((mixbuffer[8] != 0xb && mixbuffer[8] != 0x77)
                            || (mixbuffer[9] != 0xb && mixbuffer[9] != 0x77)
                            || (mixbuffer[mix_size] != 0xb && mixbuffer[mix_size] != 0x77)
                            || (mixbuffer[mix_size+1] != 0xb && mixbuffer[mix_size+1] != 0x77)) {
                         ALOGI("AD mix main_size=%d ad_size=%d wirte_size=%d 0x%x 0x%x 0x%x 0x%x", main_size, ad_size, ret,
                                 mixbuffer[8],mixbuffer[9],mixbuffer[mix_size],mixbuffer[mix_size+1]);
                    }

                    remain_size = aml_dev->ddp.remain_size;
                    patch->outlen_after_last_validpts += aml_dev->ddp.outlen_pcm;
                    patch->decoder_offset += remain_size + main_size - aml_dev->ddp.remain_size;
                    patch->dtv_pcm_readed += main_size;
                    pthread_mutex_unlock(&(patch->dtv_output_mutex));
                } else {
                    /*ALOGE("%s(), ring_buffer has not enough data! %d in_avail()", __func__,
                          avail);*/
                    pthread_mutex_unlock(&(patch->dtv_output_mutex));
                    usleep(1000);
                }
            } else {
                int remain_size = 0;
                int avail = get_buffer_read_space(ringbuffer);
                if (avail > 0) {
                    if (avail > (int)patch->out_buf_size) {
                        avail = (int)patch->out_buf_size;
                        if (avail > 512) {
                            avail = 512;
                        }
                    } else {
                        avail = 512;
                    }

                    if (aml_dev->ddp.curFrmSize != 0) {
                        avail = aml_dev->ddp.curFrmSize;
                    }

                    if (!patch->first_apts_lookup_over) {
                        unsigned int first_checkinapts = 0xffffffff;
                        unsigned int demux_pcr = 0xffffffff;
                        int diff = 0;
                        char buff[32];
                        int ret = 0;
                        ret = aml_sysfs_get_str(TSYNC_FIRSTCHECKIN_APTS, buff, sizeof(buff));
                        if (ret > 0) {
                            ret = sscanf(buff, "0x%x\n", &first_checkinapts);
                        }

                        ret = aml_sysfs_get_str(TSYNC_DEMUX_PCR, buff, sizeof(buff));
                        if (ret > 0) {
                            ret = sscanf(buff, "0x%x\n", &demux_pcr);
                        }

                        ALOGI("demux_pcr %x first_checkinapts %x\n", demux_pcr, first_checkinapts);
                        if ((first_checkinapts != 0xffffffff) || (demux_pcr != 0xffffffff)) {
                            if (first_checkinapts > demux_pcr) {
                                unsigned diff = first_checkinapts - demux_pcr;
                                if (diff  < AUDIO_PTS_DISCONTINUE_THRESHOLD) {
                                    ALOGI("hold the aduio for cache data\n");
                                    pthread_mutex_unlock(&(patch->dtv_output_mutex));
                                    usleep(10000);
                                    continue;
                                }
                            } else {
                                unsigned diff = demux_pcr - first_checkinapts;
                                aml_dev->dtv_droppcm_size = diff * 48 * 2 * 2 / 90;
                            }

                        }
                    }

                    // ALOGE("%s(), ring_buffer_read now read %d data", __func__, avail);
                    ret = ring_buffer_read(ringbuffer, (unsigned char *)patch->out_buf,
                                           avail);
                    if (ret == 0) {
                        pthread_mutex_unlock(&(patch->dtv_output_mutex));
                        /*ALOGE("%s(), ring_buffer read 0 data!", __func__);*/
                        usleep(1000);
                        continue;
                    }
                    {
                        unsigned long pts = 0;
                        aml_out = (struct aml_stream_out *)stream_out;
                        if (aml_out != NULL) {
                            unsigned int lancty = out_get_latency(&(aml_out->stream));
                            pts = dtv_hal_get_pts(patch, lancty);
                            process_ac3_sync(patch, pts);
                        }
                    }
                    {
                        struct aml_stream_out *aml_out = (struct aml_stream_out *)stream_out;
                        if (aml_out->hal_internal_format != patch->aformat) {
                            aml_out->hal_format = aml_out->hal_internal_format = patch->aformat;
                            get_sink_format(stream_out);
                        }
                    }

                    remain_size = aml_dev->ddp.remain_size;
                    ret = out_write_new(stream_out, patch->out_buf, ret);
                    patch->outlen_after_last_validpts += aml_dev->ddp.outlen_pcm;

                    patch->decoder_offset += remain_size + ret - aml_dev->ddp.remain_size;
                    patch->dtv_pcm_readed += ret;
                    pthread_mutex_unlock(&(patch->dtv_output_mutex));
                } else {
                    /*ALOGE("%s(), ring_buffer has not enough data! %d in_avail()", __func__,
                          avail);*/
                    pthread_mutex_unlock(&(patch->dtv_output_mutex));
                    usleep(1000);
                }
            }
        } else if (patch->aformat == AUDIO_FORMAT_DTS) {
            int remain_size = 0;
            int avail = get_buffer_read_space(ringbuffer);
            if (avail > 0) {
                if (avail > (int)patch->out_buf_size) {
                    avail = (int)patch->out_buf_size;
                    if (avail > 1024) {
                        avail = 1024;
                    }
                } else {
                    avail = 1024;
                }
                if (!patch->first_apts_lookup_over) {
                    unsigned int first_checkinapts = 0xffffffff;
                    unsigned int demux_pcr = 0xffffffff;
                    int diff = 0;
                    char buff[32];
                    int ret = 0;
                    ret = aml_sysfs_get_str(TSYNC_FIRSTCHECKIN_APTS, buff, sizeof(buff));
                    if (ret > 0) {
                        ret = sscanf(buff, "0x%x\n", &first_checkinapts);
                    }

                    ret = aml_sysfs_get_str(TSYNC_DEMUX_PCR, buff, sizeof(buff));
                    if (ret > 0) {
                        ret = sscanf(buff, "0x%x\n", &demux_pcr);
                    }

                    if (demux_pcr > 300 * 90) {
                        demux_pcr = demux_pcr - 300 * 90;
                    } else {
                        demux_pcr = 0;
                    }

                    /*ALOGI("demux_pcr %x first_checkinapts %x\n",demux_pcr,first_checkinapts);*/
                    if ((first_checkinapts != 0xffffffff) || (demux_pcr != 0xffffffff)) {
                        if (first_checkinapts > demux_pcr) {
                            unsigned diff = first_checkinapts - demux_pcr;
                            if (diff  < AUDIO_PTS_DISCONTINUE_THRESHOLD) {
                                pthread_mutex_unlock(&(patch->dtv_output_mutex));
                                usleep(10000);
                                continue;
                            }
                        } else if (first_checkinapts < demux_pcr) {
                            unsigned diff = demux_pcr - first_checkinapts;
                            //calc the drop size with pts,48khz 2ch,16bbit , (pts/90000)*48000*2*2;
                            unsigned drop_size = diff * 48 * 4 / 90;
                            aml_dev->dtv_droppcm_size = drop_size;
                            ALOGE("[%d]now must drop %d pcm data now\n", __LINE__, drop_size);
                        }
                    }
                }
                ret = ring_buffer_read(ringbuffer, (unsigned char *)patch->out_buf,
                                       avail);
                if (ret == 0) {
                    pthread_mutex_unlock(&(patch->dtv_output_mutex));
                    usleep(1000);
                    /*ALOGE("%s(), live ring_buffer read 0 data!", __func__);*/
                    continue;
                }
                {
                    unsigned long pts = 0;
                    aml_out = (struct aml_stream_out *)stream_out;
                    if (aml_out != NULL) {
                        unsigned int lancty = out_get_latency(&(aml_out->stream));
                        pts = dtv_hal_get_pts(patch, lancty);
                        process_ac3_sync(patch, pts);
                    }
                }
                remain_size = aml_dev->dts_hd.remain_size;

                ret = out_write_new(stream_out, patch->out_buf, ret);
                patch->outlen_after_last_validpts += aml_dev->dts_hd.outlen_pcm;

                patch->decoder_offset +=
                    remain_size + ret - aml_dev->dts_hd.remain_size;
                patch->dtv_pcm_readed += ret;
                pthread_mutex_unlock(&(patch->dtv_output_mutex));
            } else {
                pthread_mutex_unlock(&(patch->dtv_output_mutex));
                usleep(5000);
            }

        } else {
            int avail = get_buffer_read_space(ringbuffer);

            if (avail > 0) {
                if (avail > (int)patch->out_buf_size) {
                    avail = (int)patch->out_buf_size;
                }
                if (!patch->first_apts_lookup_over) {
                    unsigned int first_checkinapts = 0xffffffff;
                    unsigned int demux_pcr = 0xffffffff;
                    int diff = 0;
                    char buff[32];
                    int ret = 0;
                    ret = aml_sysfs_get_str(TSYNC_FIRSTCHECKIN_APTS, buff, sizeof(buff));
                    if (ret > 0) {
                        ret = sscanf(buff, "0x%x\n", &first_checkinapts);
                    }

                    ret = aml_sysfs_get_str(TSYNC_DEMUX_PCR, buff, sizeof(buff));
                    if (ret > 0) {
                        ret = sscanf(buff, "0x%x\n", &demux_pcr);
                    }
                    ALOGI("demux_pcr %d first_checkinapts %d\n", (int)demux_pcr / 90, (int)first_checkinapts / 90);

                    if (demux_pcr > 300 * 90) {
                        demux_pcr = demux_pcr - 300 * 90;
                    } else {
                        demux_pcr = 0;
                    }

                    if ((first_checkinapts != 0xffffffff) || (demux_pcr != 0xffffffff)) {
                        if (first_checkinapts > demux_pcr) {
                            unsigned diff = first_checkinapts - demux_pcr;
                            if (diff  < AUDIO_PTS_DISCONTINUE_THRESHOLD) {
                                pthread_mutex_unlock(&(patch->dtv_output_mutex));
                                ALOGI("hold the aduio for cache data\n");
                                usleep(10000);
                                continue;
                            }
                        } else if (first_checkinapts < demux_pcr) {
                            unsigned diff = demux_pcr - first_checkinapts;
                            //calc the drop size with pts,48khz 2ch,16bbit , (pts/90000)*48000*2*2;
                            unsigned drop_size = diff * 48 * 4 / 90;
                            aml_dev->dtv_droppcm_size = drop_size;
                            ALOGE("[%d]now must drop %d pcm data now\n", __LINE__, drop_size);
                        }
                    }
                    patch->first_apts_lookup_over = 1;
                }
                ret = ring_buffer_read(ringbuffer, (unsigned char *)patch->out_buf,
                                       avail);
                if (ret == 0) {
                    pthread_mutex_unlock(&(patch->dtv_output_mutex));
                    usleep(1000);
                    /*ALOGE("%s(), live ring_buffer read 0 data!", __func__);*/
                    continue;
                }

                struct aml_stream_out *aml_out = (struct aml_stream_out *) stream_out;
                if (aml_out->hal_internal_format != patch->aformat) {
                    aml_out->hal_format = aml_out->hal_internal_format = patch->aformat;
                    get_sink_format(stream_out);
                }

                ret = out_write_new(stream_out, patch->out_buf, ret);
                {
                    aml_out = (struct aml_stream_out *)stream_out;
                    if (aml_out != NULL) {
                        int pcm_lantcy = out_get_latency(&(aml_out->stream));
                        int abuf_level = get_buffer_read_space(ringbuffer);
                        process_pts_sync(pcm_lantcy, patch, abuf_level);
                    }
                }
                // ALOGE("[%s]wirte pcm %d data to pcm!\n", __FUNCTION__, ret);
                patch->dtv_pcm_readed += ret;
                pthread_mutex_unlock(&(patch->dtv_output_mutex));
            } else {
                pthread_mutex_unlock(&(patch->dtv_output_mutex));
                usleep(1000);
            }
        }
    }
    free(patch->out_buf);
exit_outbuf:
    adev_close_output_stream_new(dev, stream_out);
exit_open:
    ALOGI("--%s live ", __FUNCTION__);
    return ((void *)0);
}


static void patch_thread_get_cmd(struct aml_audio_patch *patch, int *cmd)
{
    if (dtv_cmd_list.initd == 0) {
        *cmd = AUDIO_DTV_PATCH_CMD_NULL;
        return;
    }
    if (patch == NULL) {
        *cmd = AUDIO_DTV_PATCH_CMD_NULL;
        return;
    }
    if (patch->input_thread_exit == 1) {
        *cmd = AUDIO_DTV_PATCH_CMD_NULL;
        return;
    }
    if (dtv_patch_cmd_is_empty() == 1) {
        *cmd = AUDIO_DTV_PATCH_CMD_NULL;
    } else {
        *cmd = dtv_patch_get_cmd();
    }
}

static void *audio_dtv_patch_process_threadloop(void *data)
{
    struct aml_audio_patch *patch = (struct aml_audio_patch *)data;
    struct audio_hw_device *dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    ring_buffer_t *ringbuffer = &(patch->aml_ringbuffer);
    struct audio_stream_in *stream_in = NULL;
    struct audio_config stream_config;
    // FIXME: add calc for read_bytes;
    int read_bytes = DEFAULT_CAPTURE_PERIOD_SIZE * CAPTURE_PERIOD_COUNT;
    int ret = 0, retry = 0;
    audio_format_t cur_aformat;
    unsigned int adec_handle;
    int cmd = AUDIO_DTV_PATCH_CMD_NUM;
    struct dolby_ddp_dec *ddp_dec = &(aml_dev->ddp);
    struct dca_dts_dec *dts_dec = &(aml_dev->dts_hd);
    patch->sample_rate = stream_config.sample_rate = 48000;
    patch->chanmask = stream_config.channel_mask = AUDIO_CHANNEL_IN_STEREO;
    patch->aformat = stream_config.format = AUDIO_FORMAT_PCM_16_BIT;

    ALOGI("++%s live \n", __FUNCTION__);
    patch->dtv_decoder_state = AUDIO_DTV_PATCH_DECODER_STATE_INIT;

    while (!patch->input_thread_exit) {
        pthread_mutex_lock(&patch->dtv_input_mutex);

        switch (patch->dtv_decoder_state) {
        case AUDIO_DTV_PATCH_DECODER_STATE_INIT: {
            ALOGI("++%s live now  open the audio decoder now !\n", __FUNCTION__);
            dtv_patch_input_open(&adec_handle, dtv_patch_pcm_wirte,
                                 dtv_patch_buffer_space, patch);
            patch->dtv_decoder_state = AUDIO_DTV_PATCH_DECODER_STATE_START;
        }
        break;
        case AUDIO_DTV_PATCH_DECODER_STATE_START:

            if (patch->input_thread_exit == 1) {
                pthread_mutex_unlock(&patch->dtv_input_mutex);
                goto exit;
            }
            patch_thread_get_cmd(patch, &cmd);
            if (cmd == AUDIO_DTV_PATCH_CMD_NULL) {
                pthread_mutex_unlock(&patch->dtv_input_mutex);
                usleep(50000);
                continue;
            }

            ring_buffer_reset(&patch->aml_ringbuffer);

            if (cmd == AUDIO_DTV_PATCH_CMD_START) {
                dtv_patch_input_start(adec_handle, patch->dtv_aformat,
                                      patch->dtv_has_video);
                create_dtv_output_stream_thread(patch);
                dtv_assoc_audio_start(1,aml_dev->sub_apid,aml_dev->sub_afmt);
                ALOGI("++%s live now  start the audio decoder now !\n",
                      __FUNCTION__);
                patch->dtv_first_apts_flag = 0;
                patch->outlen_after_last_validpts = 0;
                patch->last_valid_pts = 0;
                patch->first_apts_lookup_over = 0;
                if (patch->dtv_aformat == ACODEC_FMT_AC3) {
                    patch->aformat = AUDIO_FORMAT_AC3;
                    ddp_dec->is_iec61937 = false;
                    patch->decoder_offset = 0;
                    patch->first_apts_lookup_over = 0;
                } else if (patch->dtv_aformat == ACODEC_FMT_EAC3) {
                    patch->aformat = AUDIO_FORMAT_E_AC3;
                    ddp_dec->is_iec61937 = false;
                    patch->decoder_offset = 0;
                    patch->first_apts_lookup_over = 0;
                } else if (patch->dtv_aformat == ACODEC_FMT_DTS) {
                    patch->aformat = AUDIO_FORMAT_DTS;
                    dca_decoder_init_patch(dts_dec);
                    patch->decoder_offset = 0;
                    patch->first_apts_lookup_over = 0;
                } else {
                    patch->aformat = AUDIO_FORMAT_PCM_16_BIT;
                    patch->decoder_offset = 0;
                    patch->first_apts_lookup_over = 0;
                }
                patch->dtv_pcm_readed = patch->dtv_pcm_writed = 0;
                patch->dtv_decoder_state = AUDIO_DTV_PATCH_DECODER_STATE_RUNING;
                create_dtv_output_stream_thread(patch);
            } else {
                ALOGI("++%s line %d  live state unsupport state %d cmd %d !\n",
                      __FUNCTION__, __LINE__, patch->dtv_decoder_state, cmd);
                pthread_mutex_unlock(&patch->dtv_input_mutex);
                usleep(50000);
                continue;
            }
            break;
        case AUDIO_DTV_PATCH_DECODER_STATE_RUNING:

            if (patch->input_thread_exit == 1) {
                pthread_mutex_unlock(&patch->dtv_input_mutex);
                goto exit;
            }
            patch_thread_get_cmd(patch, &cmd);
            if (cmd == AUDIO_DTV_PATCH_CMD_NULL) {
                pthread_mutex_unlock(&patch->dtv_input_mutex);
                usleep(50000);
                continue;
            }
            if (cmd == AUDIO_DTV_PATCH_CMD_PAUSE) {
                ALOGI("++%s live now start  pause  the audio decoder now \n",
                      __FUNCTION__);
                dtv_patch_input_pause(adec_handle);
                patch->dtv_decoder_state = AUDIO_DTV_PATCH_DECODER_STATE_PAUSE;
                ALOGI("++%s live now end  pause  the audio decoder now \n",
                      __FUNCTION__);
            } else if (cmd == AUDIO_DTV_PATCH_CMD_STOP) {
                ALOGI("++%s live now  stop  the audio decoder now \n",
                      __FUNCTION__);
                dtv_adjust_output_clock(patch, DIRECT_NORMAL, 50);
                dtv_patch_input_stop(adec_handle);
                release_dtv_output_stream_thread(patch);
                dtv_assoc_audio_stop(1);
                patch->dtv_decoder_state = AUDIO_DTV_PATCH_DECODER_STATE_INIT;
            } else {
                ALOGI("++%s line %d  live state unsupport state %d cmd %d !\n",
                      __FUNCTION__, __LINE__, patch->dtv_decoder_state, cmd);
                pthread_mutex_unlock(&patch->dtv_input_mutex);
                usleep(50000);
                continue;
            }

            break;

        case AUDIO_DTV_PATCH_DECODER_STATE_PAUSE:
            patch_thread_get_cmd(patch, &cmd);
            if (cmd == AUDIO_DTV_PATCH_CMD_NULL) {
                pthread_mutex_unlock(&patch->dtv_input_mutex);
                usleep(50000);
                continue;
            }
            if (cmd == AUDIO_DTV_PATCH_CMD_RESUME) {
                dtv_patch_input_resume(adec_handle);
                patch->dtv_decoder_state = AUDIO_DTV_PATCH_DECODER_STATE_RUNING;
            } else if (cmd == AUDIO_DTV_PATCH_CMD_STOP) {
                ALOGI("++%s live now  stop  the audio decoder now \n", __FUNCTION__);
                dtv_patch_input_stop(adec_handle);
                dtv_assoc_audio_stop(1);
                patch->dtv_decoder_state = AUDIO_DTV_PATCH_DECODER_STATE_INIT;
            } else {
                ALOGI("++%s line %d  live state unsupport state %d cmd %d !\n",
                      __FUNCTION__, __LINE__, patch->dtv_decoder_state, cmd);
                pthread_mutex_unlock(&patch->dtv_input_mutex);
                usleep(50000);
                continue;
            }
            break;
        default:
            if (patch->input_thread_exit == 1) {
                pthread_mutex_unlock(&patch->dtv_input_mutex);
                goto exit;
            }
            break;
        }
        pthread_mutex_unlock(&patch->dtv_input_mutex);
    }

exit:
    ALOGI("++%s now  live  release  the audio decoder", __FUNCTION__);
    dtv_patch_input_stop(adec_handle);
    dtv_assoc_audio_stop(1);
    pthread_exit(NULL);
}

void release_dtvin_buffer(struct aml_audio_patch *patch)
{
    if (patch->dtvin_buffer_inited == 1) {
        patch->dtvin_buffer_inited = 0;
        ring_buffer_release(&(patch->dtvin_ringbuffer));
    }
}

void dtv_in_write(struct audio_stream_out *stream, const void* buffer, size_t bytes)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct aml_audio_patch *patch = adev->audio_patch;
    int abuf_level = 0;

    if (stream == NULL || buffer == NULL || bytes == 0) {
        ALOGI("[%s] pls check the input parameters \n", __FUNCTION__);
        return ;
    }
    if ((adev->patch_src == SRC_DTV) && (patch->dtvin_buffer_inited == 1)) {
        abuf_level = get_buffer_write_space(&patch->dtvin_ringbuffer);
        if (abuf_level <= (int)bytes) {
            ALOGI("[%s] dtvin ringbuffer is full", __FUNCTION__);
            return;
        }
        ring_buffer_write(&patch->dtvin_ringbuffer, (unsigned char *)buffer, bytes, UNCOVER_WRITE);
    }
    //ALOGI("[%s] dtvin write ringbuffer successfully,abuf_level=%d", __FUNCTION__,abuf_level);
}
int dtv_in_read(struct audio_stream_in *stream, void* buffer, size_t bytes)
{
    int ret = 0;
    unsigned int es_length = 0;
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;

    if (stream == NULL || buffer == NULL || bytes == 0) {
        ALOGI("[%s] pls check the input parameters \n", __FUNCTION__);
    }

    struct aml_audio_patch *patch = adev->audio_patch;
    struct dolby_ddp_dec *ddp_dec = & (adev->ddp);
    //ALOGI("[%s] patch->aformat=0x%x patch->dtv_decoder_ready=%d bytes:%d\n", __FUNCTION__,patch->aformat,patch->dtv_decoder_ready,bytes);

    if (patch->dtvin_buffer_inited == 1) {
        int abuf_level = get_buffer_read_space(&patch->dtvin_ringbuffer);
        if (abuf_level <= (int)bytes) {
            memset(buffer, 0, sizeof(unsigned char)* bytes);
            ret = bytes;
        } else {
            ret = ring_buffer_read(&patch->dtvin_ringbuffer, (unsigned char *)buffer, bytes);
            //ALOGI("[%s] abuf_level =%d ret=%d\n", __FUNCTION__,abuf_level,ret);
        }
        return bytes;
    } else {
        memset(buffer, 0, sizeof(unsigned char)* bytes);
        ret = bytes;
        return ret;
    }

    if (patch->aformat == AUDIO_FORMAT_AC3 ||
        patch->aformat == AUDIO_FORMAT_E_AC3) {
        int data_size = 0;
        if (patch->dtv_decoder_ready == 0) {
            dcv_decoder_init_patch(ddp_dec);
            patch->dtv_decoder_ready = 1;
        }
        data_size = get_buffer_read_space(&patch->aml_ringbuffer);
        ALOGI("[%s] data_size =%d\n", __FUNCTION__, data_size);
        if (data_size == 0) {
            memset(buffer, 0, sizeof(unsigned char)* bytes);
            ret = bytes;
        } else {
            ret = ring_buffer_read(&patch->aml_ringbuffer, (unsigned char *)buffer, bytes);
            ALOGI("[%s] ddp data_size =%d ret=%d\n", __FUNCTION__, data_size, ret);
            dcv_decoder_process_patch(ddp_dec, (unsigned char *)buffer, bytes);
            //memcpy(buffer, (unsigned char *)es_buff, ret);
        }
    } else if (patch->aformat == AUDIO_FORMAT_DTS) {
        if (patch->dtv_decoder_ready == 0) {
        } else {

        }
    } else {
        //ret = ring_buffer_read(&patch->aml_ringbuffer,(unsigned char*)buffer, bytes);
        int abuf_level = get_buffer_read_space(&patch->aml_ringbuffer);
        ALOGI("[%s] abuf_level =%d\n", __FUNCTION__, abuf_level);
        if (abuf_level == 0) {
            memset(buffer, 0, sizeof(unsigned char)* bytes);
            ret = bytes;
        } else {
            ret = ring_buffer_read(&patch->aml_ringbuffer, (unsigned char *)buffer, bytes);
            ALOGI("[%s] abuf_level =%d ret=%d\n", __FUNCTION__, abuf_level, ret);
            //memcpy(buffer, (unsigned char *)es_buff, ret);
        }
        process_pts_sync(0, patch, abuf_level);
    }
    return ret;
}

#define AVSYNC_SAMPLE_INTERVAL (50)
#define AVSYNC_SAMPLE_MAX_CNT (10)

static int create_dtv_output_stream_thread(struct aml_audio_patch *patch)
{
    int ret = 0;
    ALOGI("++%s   ---- %d\n", __FUNCTION__, patch->ouput_thread_created);

    if (patch->ouput_thread_created == 0) {
        patch->output_thread_exit = 0;
        pthread_mutex_init(&patch->dtv_output_mutex, NULL);
        ret = pthread_create(&(patch->audio_output_threadID), NULL,
                             audio_dtv_patch_output_threadloop, patch);
        if (ret != 0) {
            ALOGE("%s, Create output thread fail!\n", __FUNCTION__);
            pthread_mutex_destroy(&patch->dtv_output_mutex);
            return -1;
        }

        patch->ouput_thread_created = 1;
    }
    ALOGI("--%s", __FUNCTION__);
    return 0;
}

static int release_dtv_output_stream_thread(struct aml_audio_patch *patch)
{
    int ret = 0;
    ALOGI("++%s   ---- %d\n", __FUNCTION__, patch->ouput_thread_created);
    if (patch->resample_outbuf) {
        free(patch->resample_outbuf);
        patch->resample_outbuf = NULL;
    }
    if (patch->ouput_thread_created == 1) {
        patch->output_thread_exit = 1;
        pthread_join(patch->audio_output_threadID, NULL);
        pthread_mutex_destroy(&patch->dtv_output_mutex);

        patch->ouput_thread_created = 0;
    }
    ALOGI("--%s", __FUNCTION__);

    return 0;
}

int create_dtv_patch_l(struct audio_hw_device *dev, audio_devices_t input,
                     audio_devices_t output __unused)
{
    struct aml_audio_patch *patch;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    int period_size = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
    pthread_attr_t attr;
    struct sched_param param;

    int ret = 0;
    // ALOGI("++%s live period_size %d\n", __func__, period_size);
    //pthread_mutex_lock(&aml_dev->patch_lock);
    if (aml_dev->audio_patch) {
        ALOGD("%s: patch exists, first release it", __func__);
        if (aml_dev->audio_patch->is_dtv_src)
            release_dtv_patch_l(aml_dev);
        else
            release_patch_l(aml_dev);
    }
    patch = calloc(1, sizeof(*patch));
    if (!patch) {
        ret = -1;
        goto err;
    }
    if (aml_dev->dtv_i2s_clock == 0) {
        aml_dev->dtv_i2s_clock = aml_mixer_ctrl_get_int(&(aml_dev->alsa_mixer), AML_MIXER_ID_CHANGE_I2S_PLL);
        aml_dev->dtv_spidif_clock = aml_mixer_ctrl_get_int(&(aml_dev->alsa_mixer), AML_MIXER_ID_CHANGE_SPIDIF_PLL);
    }

    memset(cmd_array, 0, sizeof(cmd_array));
    // save dev to patch
    patch->dev = dev;
    patch->input_src = input;
    patch->aformat = AUDIO_FORMAT_PCM_16_BIT;
    patch->avsync_sample_max_cnt = AVSYNC_SAMPLE_MAX_CNT;
    patch->is_dtv_src = true;

    patch->output_thread_exit = 0;
    patch->input_thread_exit = 0;
    aml_dev->audio_patch = patch;
    pthread_mutex_init(&patch->mutex, NULL);
    pthread_cond_init(&patch->cond, NULL);
    pthread_mutex_init(&patch->dtv_input_mutex, NULL);

    ret = ring_buffer_init(&(patch->aml_ringbuffer),
                           4 * period_size * PATCH_PERIOD_COUNT * 10);
    if (ret < 0) {
        ALOGE("Fail to init audio ringbuffer!");
        goto err_ring_buf;
    }

    if (aml_dev->useSubMix) {
        // switch normal stream to old tv mode writing
        switchNormalStream(aml_dev->active_outputs[STREAM_PCM_NORMAL], 0);
    }

    ret = pthread_create(&(patch->audio_input_threadID), NULL,
                         audio_dtv_patch_process_threadloop, patch);
    if (ret != 0) {
        ALOGE("%s, Create process thread fail!\n", __FUNCTION__);
        goto err_in_thread;
    }
    create_dtv_output_stream_thread(patch);
    //pthread_mutex_unlock(&aml_dev->patch_lock);
    init_cmd_list();
    if (aml_dev->tuner2mix_patch) {
        ret = ring_buffer_init(&patch->dtvin_ringbuffer, 4 * 8192 * 2 *16);
        ALOGI("[%s] aring_buffer_init ret=%d\n", __FUNCTION__, ret);
        if (ret == 0) {
            patch->dtvin_buffer_inited = 1;
        }
    }
    dtv_assoc_init();
    patch->dtv_aformat = aml_dev->dtv_aformat;
    patch->dtv_output_clock = 0;
    patch->dtv_default_i2s_clock = aml_dev->dtv_i2s_clock;
    patch->dtv_default_spidif_clock = aml_dev->dtv_spidif_clock;

    ALOGI("--%s", __FUNCTION__);
    return 0;
err_parse_thread:
err_out_thread:
    patch->input_thread_exit = 1;
    pthread_join(patch->audio_input_threadID, NULL);
err_in_thread:
    ring_buffer_release(&(patch->aml_ringbuffer));
err_ring_buf:
    free(patch);
err:
    //pthread_mutex_unlock(&aml_dev->patch_lock);
    return ret;
}

int release_dtv_patch_l(struct aml_audio_device *aml_dev)
{
    if (aml_dev == NULL) {
        ALOGI("[%s]release the dtv patch failed  aml_dev == NULL\n", __FUNCTION__);
        return -1;
    }
    struct aml_audio_patch *patch = aml_dev->audio_patch;

    ALOGI("++%s live\n", __FUNCTION__);
    //pthread_mutex_lock(&aml_dev->patch_lock);
    if (patch == NULL) {
        ALOGI("release the dtv patch failed  patch == NULL\n");
        //pthread_mutex_unlock(&aml_dev->patch_lock);
        return -1;
    }
    deinit_cmd_list();
    patch->input_thread_exit = 1;
    pthread_join(patch->audio_input_threadID, NULL);

    pthread_mutex_destroy(&patch->dtv_input_mutex);
    release_dtv_output_stream_thread(patch);
    release_dtvin_buffer(patch);
    dtv_assoc_deinit();
    ring_buffer_release(&(patch->aml_ringbuffer));
    free(patch);
    aml_dev->audio_patch = NULL;
    ALOGI("--%s", __FUNCTION__);
    //pthread_mutex_unlock(&aml_dev->patch_lock);
    if (aml_dev->useSubMix) {
        switchNormalStream(aml_dev->active_outputs[STREAM_PCM_NORMAL], 1);
    }
    return 0;
}

int create_dtv_patch(struct audio_hw_device *dev, audio_devices_t input,
             audio_devices_t output)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    int ret = 0;

    pthread_mutex_lock(&aml_dev->patch_lock);
    ret = create_dtv_patch_l(dev, input, output);
    pthread_mutex_unlock(&aml_dev->patch_lock);

    return ret;
}

int release_dtv_patch(struct aml_audio_device *aml_dev)
{
    int ret = 0;

    pthread_mutex_lock(&aml_dev->patch_lock);
    ret = release_dtv_patch_l(aml_dev);
    pthread_mutex_unlock(&aml_dev->patch_lock);

    return ret;
}
