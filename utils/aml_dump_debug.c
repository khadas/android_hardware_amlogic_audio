/*
 * hardware/amlogic/audio/utils/aml_dump_debug.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 */

#undef  LOG_TAG
#define LOG_TAG "audio_hw_utils_dumpdebug"
//#define LOG_NDEBUG 0

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <aml_dump_debug.h>
#include <aml_android_utils.h>
#include "aml_malloc_debug.h"
#include "aml_async_write.h"

static int gDumpDataFd = -1;

void DoDumpData(const void *data_buf, int size, int aud_src_type) {
    int tmp_type = -1;
    char prop_value[PROPERTY_VALUE_MAX] = { 0 };
    char file_path[PROPERTY_VALUE_MAX] = { 0 };

    memset(prop_value, '\0', PROPERTY_VALUE_MAX);
    property_get("vendor.media.audiohal.dumpdata.en", prop_value, "null");
    if (strcasecmp(prop_value, "null") == 0
            || strcasecmp(prop_value, "0") == 0) {
        if (gDumpDataFd >= 0) {
            close(gDumpDataFd);
            gDumpDataFd = -1;
        }
        return;
    }

    property_get("vendor.media.audiohal.dumpdata.src", prop_value, "null");
    if (strcasecmp(prop_value, "null") == 0
            || strcasecmp(prop_value, "input") == 0
            || strcasecmp(prop_value, "0") == 0) {
        tmp_type = CC_DUMP_SRC_TYPE_INPUT;
    } else if (strcasecmp(prop_value, "output") == 0
            || strcasecmp(prop_value, "1") == 0) {
        tmp_type = CC_DUMP_SRC_TYPE_OUTPUT;
    } else if (strcasecmp(prop_value, "input_parse") == 0
            || strcasecmp(prop_value, "2") == 0) {
        tmp_type = CC_DUMP_SRC_TYPE_INPUT_PARSE;
    }

    if (tmp_type != aud_src_type) {
        return;
    }

    memset(file_path, '\0', PROPERTY_VALUE_MAX);
    property_get("vendor.media.audiohal.dumpdata.path", file_path, "null");
    if (strcasecmp(file_path, "null") == 0) {
        file_path[0] = '\0';
    }

    if (gDumpDataFd < 0 && file_path[0] != '\0') {
        gDumpDataFd = open(file_path, O_WRONLY | O_CREAT | O_EXCL,
                S_IRUSR | S_IWUSR);
        if (gDumpDataFd < 0) {
            ALOGE("%s, Create device file \"%s\" error: %s.\n",
                    __FUNCTION__, file_path, strerror(errno));
        }
    }

    if (gDumpDataFd >= 0) {
        write(gDumpDataFd, data_buf, size);
    }
    return;
}

typedef struct aml_dump_debug {
    pthread_t                   threadid;
    bool                        bexit;
    pthread_mutex_t             mutex;
    pthread_condattr_t          cond_attr;
    pthread_cond_t              cond;
    dump_debug_item_t           *items;
} aml_dump_debug_t;

static aml_dump_debug_t * g_debug_handle = NULL;

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *this table sequence must match with enum AML_DUMP_DEBUG_INFO
 *!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 */
dump_debug_item_t aml_debug_items[AML_DEBUG_DUMP_MAX] = {
    /*define debug items*/
    {AML_DEBUG_AUDIOHAL_DEBUG,          AML_DEBUG_AUDIOHAL_DEBUG_PROPERTY,                 0},    //AML_DEBUG_AUDIOHAL_DEBUG
    {AML_DEBUG_AUDIOHAL_LEVEL_DETECT,   AML_DEBUG_AUDIOHAL_LEVEL_DETECT_PROPERTY,          0},    //AML_DEBUG_AUDIOHAL_LEVEL_DETECT
    {AML_DEBUG_AUDIOHAL_HW_SYNC,        AML_DEBUG_AUDIOHAL_HW_SYNC_PROPERTY,               0},    //AML_DEBUG_AUDIOHAL_HW_SYNC
    {AML_DEBUG_AUDIOHAL_ALSA,           AML_DEBUG_AUDIOHAL_ALSA_PROPERTY,                  0},    //AML_DEBUG_AUDIOHAL_ALSA
    {AML_DEBUG_AUDIOHAL_SYNCPTS,        AML_DEBUG_AUDIOHAL_SYNCPTS_PROPERTY,               0},    //AML_DEBUG_AUDIOHAL_SYNCPTS
    {AML_DEBUG_AUDIOHAL_MATENC,         AML_DEBUG_AUDIOHAL_MATENC_PROPERTY,                0},    //AML_DEBUG_AUDIOHAL_MATENC
    {AML_DEBUG_AUDIOHAL_TRACE,          AML_DEBUG_AUDIOHAL_TRACE_PROPERTY,                 0},    //AML_DEBUG_AUDIOHAL_TRACE
    {AML_DEBUG_AUDIOHAL_DECODED_INFO,   AML_DEBUG_AUDIOINFO_REPORT_PROPERTY,               0},    //AML_DEBUG_AUDIOHAL_DECODED_INFO
    {AML_DEBUG_AUDIOHAL_AUT,            AML_DEBUG_AUDIOHAL_AUT_PROPERTY,                   0},    //AML_DEBUG_AUDIOHAL_AUT
    {AML_DEBUG_AUDIOHAL_EDID,           AML_DEBUG_AUDIOHAL_EDID_PROPERTY,                  0},    //AML_DEBUG_AUDIOHAL_EDID

    /*define dump items*/
    {AML_DUMP_AUDIOHAL_IN,              AML_DUMP_AUDIOHAL_IN_PROPERTY,                     0},    //AML_DUMP_AUDIOHAL_IN
    {AML_DUMP_AUDIOHAL_OUT,             AML_DUMP_AUDIOHAL_OUT_PROPERTY,                    0},    //AML_DUMP_AUDIOHAL_OUT
    {AML_DUMP_AUDIOHAL_MS12,            AML_DUMP_AUDIOHAL_MS12_PROPERTY,                   0},    //AML_DUMP_AUDIOHAL_MS12
    {AML_DUMP_AUDIOHAL_SPDIF,           AML_DUMP_AUDIOHAL_SPDIF_PROPERTY,                  0},    //AML_DUMP_AUDIOHAL_SPDIF
    {AML_DUMP_AUDIOHAL_SUBMIXING,       AML_DUMP_AUDIOHAL_SUBMIXING_PROPERTY,              0},    //AML_DUMP_AUDIOHAL_SUBMIXING
    {AML_DUMP_AUDIOHAL_TV,              AML_DUMP_AUDIOHAL_TV_PROPERTY,                     0},    //AML_DUMP_AUDIOHAL_TV_PATH
    {AML_DUMP_AUDIOHAL_DTV,             AML_DUMP_AUDIOHAL_DTV_PROPERTY,                    0},    //AML_DUMP_AUDIOHAL_DTV
    {AML_DUMP_AUDIOHAL_MMAP,            AML_DUMP_AUDIOHAL_MMAP_PROPERTY,                   0},    //AML_DUMP_AUDIOHAL_MMAP
    {AML_DUMP_AUDIOHAL_HFP,             AML_DUMP_AUDIOHAL_HFP_PROPERTY,                    0},    //AML_DUMP_AUDIOHAL_HFP
    {AML_DUMP_AUDIOHAL_SCO,             AML_DUMP_AUDIOHAL_SCO_PROPERTY,                    0},    //AML_DUMP_AUDIOHAL_SCO
    {AML_DUMP_AUDIOHAL_A2DP,            AML_DUMP_AUDIOHAL_A2DP_PROPERTY,                   0},    //AML_DUMP_AUDIOHAL_A2DP
    {AML_DUMP_AUDIOHAL_USB,             AML_DUMP_AUDIOHAL_USB_PROPERTY,                    0},    //AML_DUMP_AUDIOHAL_USB
    {AML_DUMP_AUDIOHAL_DECODER,         AML_DUMP_AUDIOHAL_DECODER_PROPERTY,                0},    //AML_DUMP_AUDIOHAL_DECODER
    {AML_DUMP_AUDIOHAL_RESAMPLE,        AML_DUMP_AUDIOHAL_RESAMPLE_PROPERTY,               0},    //AML_DUMP_AUDIOHAL_RESAMPLE
    {AML_DUMP_AUDIOHAL_SPEED,           AML_DUMP_AUDIOHAL_SPEED_PROPERTY,                  0},    //AML_DUMP_AUDIOHAL_SPEED
    {AML_DUMP_AUDIOHAL_EFFECT,          AML_DUMP_AUDIOHAL_EFFECT_PROPERTY,                 0},    //AML_DUMP_AUDIOHAL_EFFECT
    {AML_DUMP_AUDIOHAL_ASYNC,           AML_DUMP_AUDIOHAL_ASYNC_PROPERTY,                  1},    //AML_DUMP_AUDIOHAL_ASYNC
};


void ts_wait_monotonic_time(struct timespec *ts, uint32_t time)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_sec += time / 1000000;
    ts->tv_nsec += (time * 1000) % 1000000000;
    if (ts->tv_nsec >= 1000000000) {
        ts->tv_sec++;
        ts->tv_nsec -=1000000000;
    }
}

static void aml_debug_update(void)
{
    int i = 0;
    int ret = -1;
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    for (i = 0; i < AML_DEBUG_DUMP_MAX; i++) {
        ret = property_get(aml_debug_items[i].name, buf, NULL);
        if (ret > 0) {
            aml_debug_items[i].value = strtol (buf, NULL, 0);
        }
        ALOGV("%s  %s = 0x%x", __func__, aml_debug_items[i].name, aml_debug_items[i].value);
    }
    return;
}

static void *aml_debug_Thread(void *pArg)
{
    aml_dump_debug_t * p_handle = (aml_dump_debug_t *)pArg;
    ALOGI("enter %s", __FUNCTION__);
    while (!p_handle->bexit) {
        aml_debug_update();
        struct timespec ts;
        ts_wait_monotonic_time(&ts, 1000 * 1000);
        pthread_mutex_lock(&p_handle->mutex);
        pthread_cond_timedwait(&p_handle->cond, &p_handle->mutex, &ts);
        pthread_mutex_unlock(&p_handle->mutex);
    }
    ALOGI("exit %s", __FUNCTION__);
    return ((void *)0);
}


void aml_audio_debug_open(void)
{
    ALOGI("%s enter", __FUNCTION__);
    if (g_debug_handle == NULL) {
        g_debug_handle = aml_audio_calloc(1, sizeof(aml_dump_debug_t));
        if (g_debug_handle) {
            pthread_mutex_init(&g_debug_handle->mutex, NULL);
            pthread_condattr_init(&g_debug_handle->cond_attr);
            pthread_condattr_setclock(&g_debug_handle->cond_attr, CLOCK_MONOTONIC);
            pthread_cond_init(&g_debug_handle->cond, &g_debug_handle->cond_attr);
            if (pthread_create(&g_debug_handle->threadid, NULL, &aml_debug_Thread, (void *)g_debug_handle)) {
                ALOGE("%s create thread failed", __FUNCTION__);
                return;
            }
            g_debug_handle->bexit = false;
            g_debug_handle->items = aml_debug_items;
        } else {
            ALOGE("%s calloc failed", __FUNCTION__);
            return;
        }
    }
    ALOGI("%s exit", __FUNCTION__);
    return;
}

void aml_audio_debug_close(void)
{
    ALOGI("%s enter", __FUNCTION__);
    aml_dump_debug_t * p_handle = g_debug_handle;
    if (p_handle) {
        p_handle->bexit = true;
        if (p_handle->threadid != 0) {
            pthread_mutex_lock(&p_handle->mutex);
            pthread_cond_signal(&p_handle->cond);
            pthread_mutex_unlock(&p_handle->mutex);
            pthread_join(p_handle->threadid, NULL);
        }
        aml_audio_free(p_handle);
        g_debug_handle = NULL;
    }
    ALOGI("%s exit", __FUNCTION__);
    return;
}

int aml_dump_audio_bitstreams(const char *path, const void *buf, size_t bytes)
{
    char *token = NULL, *savePtr = NULL;
    char FilePathStr[ENUM_TYPE_STR_MAX_LEN] = {0}, suffixName[ENUM_TYPE_STR_MAX_LEN] = {'\0'};

    if (!path) {
        ALOGW("%s %d, path is null, please check it.", __func__, __LINE__);
        return -1;
    } else {//handle path string, add pid and tid to FilePath name.
        strncpy(FilePathStr, path, ENUM_TYPE_STR_MAX_LEN);
        token = strtok_r(FilePathStr, ".", &savePtr);
        if (token && savePtr) {
            strncpy(suffixName, savePtr, ENUM_TYPE_STR_MAX_LEN);
            snprintf(FilePathStr, ENUM_TYPE_STR_MAX_LEN, "%s_pid-%d_tid-%d.%s", token, getpid(), gettid(), suffixName);
        } else {
            snprintf(FilePathStr, ENUM_TYPE_STR_MAX_LEN, "%s_pid-%d_tid-%d", path, getpid(), gettid());
        }
        ALOGV("%s line %d, path:%s, FilePathStr:%s, token:%s, savePtr:%s, suffixName:%s\n", __func__, __LINE__, path, FilePathStr, token, savePtr, suffixName);
    }

    if (get_debug_value(AML_DUMP_AUDIOHAL_ASYNC)) {
        aml_async_dump_data(buf, bytes, FilePathStr);
    } else {
        FILE *fp = fopen(FilePathStr, "a+");
        if (fp) {
            int flen = fwrite((char *)buf, 1, bytes, fp);
            fclose(fp);
            return 0;
        }
        AM_LOGE("fail to open path=%s, errno=%d/%s",  path, errno, strerror(errno));
        return -1;
    }

    return 0;
}
