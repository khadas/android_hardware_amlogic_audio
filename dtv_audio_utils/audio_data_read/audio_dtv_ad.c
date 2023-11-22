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

#define LOG_TAG "audio_hw_dtvutils_ad"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <cutils/log.h>
#include <cutils/properties.h>

#include "aml_android_utils.h"
#include "am_ad.h"
#include "aml_ringbuffer.h"
#include "audio_dtv_ad.h"
#include "pes.h"

#define AD_DEMUX_ID 0
#define CACHE_TIME 0
#define DEFAULT_ASSOC_AUDIO_BUFFER_SIZE 1024 * 256

#define PES_HEADER_LEN 9

static pthread_mutex_t assoc_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct _dtv_assoc_audio {
    int assoc_enable;
    int demux_id;
    int sub_apid;
    int sub_afmt;
    int cache;
    int64_t cache_start_time;
    int main_frame_size;
    int ad_frame_size;
    ring_buffer_t sub_abuf;
    int bufinited;
    pthread_t tid;
    void *ad_handle;
    void *g_assoc_bst;
    int nAssocBufSize;
    bool is_pes_mode;
    unsigned int fade;
    unsigned int pan;
    int32_t ad_pts;
    void *pargs;
} dtv_assoc_audio;

static dtv_assoc_audio assoc_bst = {
    .assoc_enable = 0,
    .sub_apid = 0,
    .sub_afmt = -1,
    .cache = 0,
    .main_frame_size = 0,
    .ad_frame_size = 0,
    //sub_abuf ,
    .bufinited = 0,
    .tid = -1,
    .ad_handle = NULL,
    .g_assoc_bst = NULL,
    .nAssocBufSize = 0,
    .pargs = NULL,
};

enum {
    DTV_ASSOC_STAT_DISABLE = 0,
    DTV_ASSOC_STAT_ENABLE,
};

static int64_t get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL); //ms
}

static dtv_assoc_audio *get_assoc_audio(void)
{
    return &assoc_bst;
}

#define MS12_INPUT_AD_FILE "/data/audio_out/ms12_input_ad.ac3"

#define IS_AUDIO_STREAM_ID(id)  ((id)==0xBD || ((id) >= 0xC0 && (id) <= 0xDF))

#define AV_RB16(x)                           \
    ((((const unsigned char*)(x))[0] << 8) |          \
      ((const unsigned char*)(x))[1])

static inline int64_t parse_pes_pts(const unsigned char* buf) {
    return (int64_t)(*buf & 0x0e) << 29 |
        (AV_RB16(buf + 1) >> 1) << 15 |
        AV_RB16(buf + 3) >> 1;
}

void handle_ad_pes_header(unsigned char *buf,int in_bytes , int64_t *outpts, unsigned char* pan, unsigned char* fade)
{
   pPES_HEADER_tag pPesheader = (pPES_HEADER_tag)buf;
   PES_extension_header * pesextheader=NULL;
   AD_descriptor *pad=NULL;
   int byteindex=PES_HEADER_LEN;

   if (0x2 == pPesheader->PTS_DTS_flags)
   {
        *outpts = parse_pes_pts(&buf[byteindex]);
        byteindex+=5;
        //ALOGV("only pts %lld \n",*outpts);
   }
   if (0x3 == pPesheader->PTS_DTS_flags)
   {
        *outpts = parse_pes_pts(&buf[byteindex]);
        byteindex+=10;
   }
   if (1 == pPesheader->ESCR_flag)
   {
       byteindex+=6;
   }
   if (1 == pPesheader->ES_rate_flag)
   {
       byteindex+=3;
   }
   if (1 == pPesheader->DSM_trick_mode_flag)
   {
       byteindex+=1;
   }
   if (1 == pPesheader->additional_copy_info_flag)
   {
       byteindex+=1;
   }
   if (1 == pPesheader->PES_CRC_flag)
   {
       byteindex+=2;
   }

   if (1 == pPesheader->PES_extension_flag)
   {
       //parse5flag
       pesextheader=(PES_extension_header *)&buf[byteindex];
       byteindex+=1;
   }

   if (pesextheader != NULL)
   {
     if (1 == pesextheader->PES_private_data_flag)
     {
         pad=(AD_descriptor *)&buf[byteindex];
         //0x4454474144
         if (0x44 ==pad->AD_text_tag[0] && 0x54 == pad->AD_text_tag[1] && 0x47 == pad->AD_text_tag[2] && 0x41 == pad->AD_text_tag[3] && 0x44 == pad->AD_text_tag[4])
         {
          *pan= pad->pan;
          *fade= pad->fade;
           ALOGV("byteindex %d pan fade %d ,%d  \n",byteindex, *pan,*fade);
         }
     }
   }
}

static void dump_ad_input_data(void *buffer, int size, char *file_name)
{
    if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
        FILE *fp1 = fopen(file_name, "a+");
        if (fp1) {
            int flen = fwrite((char *)buffer, 1, size, fp1);
            ALOGV("%s buffer %p size %d flen %d\n", __FUNCTION__, buffer, size, flen);
            fclose(fp1);
        }
    }
}

static void audio_adcallback(const unsigned char * data, int len, void * handle)
{
    UNUSED(handle);
    //ALOGI("ASSOC %s,len =%d,data:%02x %02x %02x %02x %02x %02x %02x %02x %02x", __FUNCTION__, len, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8]);
    //pthread_mutex_lock(&assoc_mutex);
    dtv_assoc_audio *param = get_assoc_audio();

    if (len <= 0 || len > DEFAULT_ASSOC_AUDIO_BUFFER_SIZE) {
        ALOGI("invalid data size len %d",len);
        return;
    }
    dump_ad_input_data((void*)data, len, MS12_INPUT_AD_FILE);
    ring_buffer_t *ringbuffer = &(param->sub_abuf);
    int left,es_data_len = 0;
    unsigned char *es_data;
    if (param->is_pes_mode) {
         unsigned char fade = 0, pan = 0;
         int64_t outpts;
         pPES_HEADER_tag pes_header  = (pPES_HEADER_tag )data;
         if (len < PES_HEADER_LEN) {
             return;
         }
         int pes_len = ((data[4] << 8) | data[5]);
         if (pes_len > 0) {
             handle_ad_pes_header((unsigned char *)data, len, &outpts, &pan, &fade);
             param->fade = fade;
             param->pan = pan;
         } else {
             return;
         }
          ALOGV("%0x %0x %0x %0x pes_len %d fade %d pan %d",
            pes_header->packet_start_code_prefix[0],
            pes_header->packet_start_code_prefix[1],
            pes_header->packet_start_code_prefix[2],
            pes_header->stream_id,pes_len, fade, pan);
         es_data_len = len - pes_header->PES_header_data_length - PES_HEADER_LEN;
         es_data = (unsigned char * )data + pes_header->PES_header_data_length + PES_HEADER_LEN;
         if (es_data_len <= 0) {
             return;
         }

         if (param->assoc_enable == DTV_ASSOC_STAT_ENABLE && param->bufinited == 1) {
          left = get_buffer_write_space(ringbuffer);
           if (left < es_data_len) {
                ALOGI("buffer is full left = %d reset buffer",left);
                ring_buffer_reset(ringbuffer);
            } else {
               ALOGV("es_data_len %d left %d", es_data_len, left);
               ring_buffer_write(ringbuffer, (unsigned char *)es_data, es_data_len, UNCOVER_WRITE);
            }
         }
    } else {
        /*add by lianlian.zhu, ad data should not be dropped*/
        if (param->assoc_enable == DTV_ASSOC_STAT_ENABLE && param->bufinited == 1 /*&& param->cache > 0*/) {
            unsigned short head1 = data[0] << 8 | data[1];
            left = get_buffer_write_space(ringbuffer);
            if (left < len) {
                ALOGI("buffer is full left = %d reset buffer",left);
                ring_buffer_reset(ringbuffer);
            } else if (head1 == 0x0b77 || head1 == 0x770b) {
                //ALOGI("audio_adcallback write buffer size:%d ",len);
                ring_buffer_write(ringbuffer, (unsigned char *)data, len, UNCOVER_WRITE);
            } else {
                //ALOGI("audio_adcallback,not ac3/eac3 data len=%d\n", len);
                ring_buffer_write(ringbuffer, (unsigned char *)data, len, UNCOVER_WRITE);
            }
        } else {
            ALOGI("[%s]-[associate_dec_supported:%d]-[g_assoc_bst:%p]\n", __FUNCTION__, param->assoc_enable, param->g_assoc_bst);
        }
    }
}

static int audio_ad_set_source(int enable, int pid, int fmt, void *user)
{
    AM_ErrorCode_t err = AM_SUCCESS;
    dtv_assoc_audio *param = get_assoc_audio();
    if (VALID_PID(pid))
        ALOGI("AD set source enable[%d] pid[%d] fmt[%d]", enable, pid, fmt);
    if ((enable == DTV_ASSOC_STAT_ENABLE) && VALID_PID(pid)) {
        AM_AD_Para_t para = {.dmx_id = AD_DEMUX_ID, .pid = pid, .fmt = fmt};
        para.dmx_id = param->demux_id;
        err = AM_AD_Create(&param->ad_handle, &para);
        if (err == AM_SUCCESS) {
            ALOGI("AM_AD_Create success\n");
            AM_AD_SetCallback(param->ad_handle, audio_adcallback, user);
            err = AM_AD_Start(param->ad_handle);
            ALOGI("AM_AD_start err=%d\n", err);
            if (err != AM_SUCCESS) {
                ALOGI("AM_AD_start failed ,err=%d\n", err);
                AM_AD_Destroy(param->ad_handle);
            } else {
                return 0;
            }
        } else {
            ALOGI("AM_AD_Create error,err=%d\n", err);
        }
    } else {
        ALOGI("disable AD\n");
        AM_AD_Stop(param->ad_handle);
        err = AM_AD_Destroy(param->ad_handle);
        ALOGI("disable AD, success,ret=%d\n", err);
    }
    return 1;
}

int dtv_assoc_init(void)
{
    int ret;
    dtv_assoc_audio *param = get_assoc_audio();
    if (param->bufinited) {
        return 0;
    }
    //pthread_mutex_init(&assoc_mutex, NULL);
    pthread_mutex_lock(&assoc_mutex);

    param->nAssocBufSize = DEFAULT_ASSOC_AUDIO_BUFFER_SIZE;

    param->assoc_enable = DTV_ASSOC_STAT_DISABLE;
    param->bufinited = 1;
    param->sub_apid = 0;
    param->sub_afmt = -1;
    param->cache= 0;
    param->main_frame_size= 0;
    param->ad_frame_size= 0;
    ret = ring_buffer_init(&(param->sub_abuf),
                           DEFAULT_ASSOC_AUDIO_BUFFER_SIZE);
    if (ret < 0) {
        pthread_mutex_unlock(&assoc_mutex);
        ALOGE("Fail to init audio ringbuffer!");
        return -1;
    }

    if (property_get_bool("vendor.media.dtv.pesmode",true)) {
        param->is_pes_mode = true;
     } else {
        param->is_pes_mode = false;
     }
    ALOGI("[%s %d] associate audio init success! \n", __FUNCTION__, __LINE__);
    pthread_mutex_unlock(&assoc_mutex);
    return 0;
}

int dtv_assoc_deinit(void)
{
    pthread_mutex_lock(&assoc_mutex);
    dtv_assoc_audio *param = get_assoc_audio();
    if (param->assoc_enable == DTV_ASSOC_STAT_ENABLE) {
        audio_ad_set_source(DTV_ASSOC_STAT_DISABLE, param->sub_apid, param->sub_afmt, NULL);
    }
    param->assoc_enable = DTV_ASSOC_STAT_DISABLE;
    if (param->bufinited) {
        ring_buffer_release(&(param->sub_abuf));
        param->bufinited = 0;
    }
    param->sub_apid = 0;
    param->sub_afmt = -1;
    param->cache= 0;
    param->main_frame_size= 0;
    param->ad_frame_size= 0;
    ALOGI("[%s %d] dtv_assoc_deinit success! \n", __FUNCTION__, __LINE__);
    pthread_mutex_unlock(&assoc_mutex);
    //pthread_mutex_destroy(&assoc_mutex);
    return 0;
}

int dtv_assoc_get_avail(void)
{
    //ALOGI(" dtv_assoc_read enter,size=%d", size);
    dtv_assoc_audio *param = get_assoc_audio();
    ring_buffer_t *ringbuffer = &(param->sub_abuf);
    int avail = 0;
    int cache_time = get_time() - param->cache_start_time;

    if (cache_time >= CACHE_TIME) {
        avail = get_buffer_read_space(ringbuffer);
    }

    return avail;
}
int dtv_assoc_resetbuf(void)
{
    dtv_assoc_audio *param = get_assoc_audio();
    int ret = 0;
    ALOGI("%s",__FUNCTION__);
    ring_buffer_t *ringbuffer = &(param->sub_abuf);
    if (param->assoc_enable == DTV_ASSOC_STAT_ENABLE
        && param->bufinited == 1) {
        ret = ring_buffer_reset(ringbuffer);
    }
    return ret;
}

//To get the associate data if assoc is able
int dtv_assoc_read(unsigned char *data, int size)
{
    int ret = 0;
    //ALOGI(" dtv_assoc_read enter,size=%d", size);
    dtv_assoc_audio *param = get_assoc_audio();
    ring_buffer_t *ringbuffer = &(param->sub_abuf);

    if (param->assoc_enable == DTV_ASSOC_STAT_ENABLE
        && param->bufinited == 1) {
        ret = ring_buffer_read(ringbuffer, (unsigned char *)data, size);
    } else {
        ret = 0;
    }
    /*
        head1 = data[0] << 8 | data[1];
        if (head1 == 0x0b77 || head1 == 0x770b)
            return ret;
        else
            ALOGI("!!!!!!!!!!!!!!!!!!! read_assoc_data buffer is error\n");
            return 0;*/

    //  pthread_mutex_unlock(&assoc_mutex);
    //ALOGI("dtv_assoc_read size=%d ret=%d", size, ret);
    return ret;
}

void dtv_assoc_set_main_frame_size(int main_frame_size)
{
    dtv_assoc_audio *param = get_assoc_audio();

    pthread_mutex_lock(&assoc_mutex);
    param->main_frame_size = main_frame_size;
    pthread_mutex_unlock(&assoc_mutex);
}

void dtv_assoc_get_main_frame_size(int* main_frame_size)
{
    dtv_assoc_audio *param = get_assoc_audio();

    *main_frame_size = param->main_frame_size;
}

void dtv_assoc_set_ad_frame_size(int ad_frame_size)
{
    dtv_assoc_audio *param = get_assoc_audio();

    pthread_mutex_lock(&assoc_mutex);
    param->ad_frame_size = ad_frame_size;
    pthread_mutex_unlock(&assoc_mutex);
}

void dtv_assoc_get_ad_frame_size(int* ad_frame_size)
{
    dtv_assoc_audio *param = get_assoc_audio();

    *ad_frame_size = param->ad_frame_size;
}

int dtv_assoc_get_ad_fade()
{
    dtv_assoc_audio *param = get_assoc_audio();
    if (param) {
       return param->fade;
    }
    return 0;
}
int dtv_assoc_get_ad_pan()
{
    dtv_assoc_audio *param = get_assoc_audio();
    if (param) {
       return param->pan;
    }
    return 0;
}

int32_t dtv_assoc_get_ad_lastpts()
{
    dtv_assoc_audio *param = get_assoc_audio();
    if (param) {
       return param->ad_pts;
    }
    return 0;
}

void dtv_assoc_audio_cache(int value)
{
    dtv_assoc_audio *param = get_assoc_audio();

    if (value < 0) {
        param->cache = -1000;
        ring_buffer_reset(&param->sub_abuf);
    } else {
        param->cache += value;
        if (param->cache == 1) {
            param->cache_start_time = get_time();
        }
    }
}

int dtv_assoc_audio_start(unsigned int handle, int pid, int fmt, int demux_id)
{
    int ret = -1;
    pthread_mutex_lock(&assoc_mutex);
    dtv_assoc_audio *param = get_assoc_audio();
    ALOGI("%s, pid=%d, fmt= %d", __FUNCTION__, pid, fmt);
    if (handle == 0 || param->bufinited == 0) {
        ALOGI("%s, buffer was not inited, the handle is %d,return", __FUNCTION__, handle);
    } else if (VALID_PID(pid) && param->assoc_enable == DTV_ASSOC_STAT_DISABLE) {
        ALOGI("%s, pid %d, fmt %d demux_id %d", __FUNCTION__, pid, fmt, demux_id);
        param->demux_id = demux_id;
        param->sub_apid = pid;
        param->sub_afmt = fmt;
        param->cache = 0;
        param->main_frame_size= 0;
        param->ad_frame_size= 0;
        ret = audio_ad_set_source(DTV_ASSOC_STAT_ENABLE, param->sub_apid, param->sub_afmt, NULL);
        if (ret == 0) {
            ALOGI("%s, DTV_ASSOC_STAT_ENABLE", __FUNCTION__);
            param->assoc_enable = DTV_ASSOC_STAT_ENABLE;
        }

    } else if (!VALID_PID(pid) && param->assoc_enable == DTV_ASSOC_STAT_ENABLE) {
        ALOGI("%s, assoc is enable, disable it", __FUNCTION__);
        param->sub_apid = 0;
        param->sub_afmt = -1;
        param->cache= 0;
        param->main_frame_size= 0;
        param->ad_frame_size= 0;
        param->fade = 0;
        param->pan = 0;
        param->assoc_enable = DTV_ASSOC_STAT_DISABLE;
        audio_ad_set_source(DTV_ASSOC_STAT_DISABLE, param->sub_apid, param->sub_afmt, NULL);
    } else {
        ALOGV("%s, invalid", __FUNCTION__);
    }
    pthread_mutex_unlock(&assoc_mutex);
    if (ret == 0) {
        ring_buffer_reset(&param->sub_abuf);
    }

    return ret;
}

void dtv_assoc_audio_stop(unsigned int handle)
{
    pthread_mutex_lock(&assoc_mutex);
    dtv_assoc_audio *param = get_assoc_audio();
    if (handle == 0) {
        ALOGI("%s, handle is error\n", __FUNCTION__);
    } else if (param->assoc_enable == DTV_ASSOC_STAT_ENABLE) {
        ALOGI("%s, disable it\n", __FUNCTION__);
        param->assoc_enable = DTV_ASSOC_STAT_DISABLE;
        param->demux_id  = 0;
        param->sub_apid = 0;
        param->sub_afmt = -1;
        param->cache= 0;
        param->main_frame_size= 0;
        param->ad_frame_size= 0;
        param->fade = 0;
        param->pan = 0;
        audio_ad_set_source(DTV_ASSOC_STAT_DISABLE, param->sub_apid, param->sub_afmt, NULL);
    } else {
        ALOGI("%s, nothing to do\n", __FUNCTION__);
    }
    pthread_mutex_unlock(&assoc_mutex);
}

void dtv_assoc_audio_pause(unsigned int handle)
{
    if (handle == 0) {
        return;
    }
    ALOGI("%s, paused\n", __FUNCTION__);
    dtv_assoc_audio *param = get_assoc_audio();
    if (handle == 0) {
        return ;
    }
    if (param->assoc_enable == DTV_ASSOC_STAT_ENABLE) {
        audio_ad_set_source(DTV_ASSOC_STAT_DISABLE, param->sub_apid, param->sub_afmt, NULL);
    }
    param->assoc_enable = DTV_ASSOC_STAT_DISABLE;
    param->sub_apid = 0;
    return ;
}
void dtv_assoc_audio_resume(unsigned int handle, int pid)
{
    dtv_assoc_audio *param = get_assoc_audio();
    if (handle == 0) {
        return;
    }
    ALOGI("%s, pid=%d\n", __FUNCTION__, pid);
    if (pid == 0) {
        param->sub_apid = 0;
        ALOGI("%s,disable... param->assoc_enable=%d", __FUNCTION__, param->assoc_enable);
        if (param->assoc_enable == DTV_ASSOC_STAT_ENABLE) {
            audio_ad_set_source(DTV_ASSOC_STAT_DISABLE, param->sub_apid, param->sub_afmt, NULL);

        }
        param->assoc_enable = DTV_ASSOC_STAT_DISABLE;
        return ;
    } else if (pid > 0 && pid < 0x1fff) {
        param->sub_apid = pid;
        ALOGI("%s,enable... param->assoc_enable=%d", __FUNCTION__, param->assoc_enable);
        if ((param->bufinited) && (!param->assoc_enable)) {
            audio_ad_set_source(DTV_ASSOC_STAT_ENABLE, param->sub_apid, param->sub_afmt, NULL);
            param->assoc_enable = DTV_ASSOC_STAT_ENABLE;
        }
    } else {
        ALOGI("%s, err... param->assoc_enable=%d", __FUNCTION__, param->assoc_enable);
    }
}
