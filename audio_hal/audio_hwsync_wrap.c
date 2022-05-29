/*
 * Copyright (C) 2010 Amlogic Corporation.
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

#define LOG_TAG "audio_hwsync_wrap"
//#define LOG_NDEBUG 0

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <sys/utsname.h>

#include "audio_hw_utils.h"
#include "audio_hwsync.h"
#include "audio_mediasync_wrap.h"
#include "audio_tsync_wrap.h"
#include "aml_audio_sysfs.h"
#include "dolby_lib_api.h"

/***************  wrap hwsync interfaces.   *****************/
//wrap hwsync interfaces.
void aml_hwsync_wrap_set_pause(audio_hwsync_t *p_hwsync)
{
    ALOGI("%s(), send pause event", __func__);
    if (!p_hwsync || (p_hwsync && !p_hwsync->use_mediasync)) {
        return aml_hwsync_wrap_single_set_tsync_pause();
    }
    mediasync_wrap_setPause(p_hwsync->mediasync, true);
}

void aml_hwsync_wrap_set_resume(audio_hwsync_t *p_hwsync)
{
    ALOGI("%s(), send resume event", __func__);
    bool ret = false;
    sync_mode mode = MEDIA_SYNC_MODE_MAX;
    if (!p_hwsync || (p_hwsync && !p_hwsync->use_mediasync)) {
        return aml_hwsync_wrap_single_set_tsync_resume();
    }
    ret = mediasync_wrap_getSyncMode(p_hwsync->mediasync, &mode);
    if (ret && (mode == MEDIA_SYNC_VMASTER)) {
        ALOGI("%s(), vmaster do not send resume event", __func__);
        return;
    }
    mediasync_wrap_setPause(p_hwsync->mediasync, false);
}

void aml_hwsync_wrap_set_stop(audio_hwsync_t *p_hwsync)
{
    ALOGI("%s(), send stop event", __func__);
    if (!p_hwsync->use_mediasync) {
        return aml_hwsync_wrap_single_set_tsync_stop();
    }
    mediasync_wrap_clearAnchor(p_hwsync->mediasync);
}

int aml_hwsync_wrap_set_start_pts(audio_hwsync_t *p_hwsync, uint32_t pts)
{
    ALOGI("%s(), set tsync start pts: %d", __func__, pts);
    if (!p_hwsync->use_mediasync) {
        return aml_hwsync_wrap_single_set_tsync_start_pts(pts);
    }
    int64_t timeus = ((int64_t)pts) / 90 *1000;
    mediasync_wrap_setStartingTimeMedia(p_hwsync->mediasync, timeus);
    return 0;
}

int aml_hwsync_wrap_set_start_pts64(audio_hwsync_t *p_hwsync, uint64_t pts)
{
    ALOGI("%s(), set tsync start pts64: %" PRId64 "", __func__, pts);
    if (!p_hwsync->use_mediasync) {
        return aml_hwsync_wrap_single_set_tsync_start_pts64(pts);
    }
    int64_t timeus = pts / 90 *1000;
    mediasync_wrap_setStartingTimeMedia(p_hwsync->mediasync, timeus);
    return 0;
}

int aml_hwsync_wrap_get_pts(audio_hwsync_t *p_hwsync, uint64_t *pts)
{
    int64_t timeus = 0;
    ALOGV("%s(), get tsync pts", __func__);
    if (!p_hwsync->use_mediasync) {
        uint32_t pts32 = 0;
        int ret = 0;
        ret = aml_hwsync_wrap_single_get_tsync_pts(&pts32);
        *pts = pts32;
        return ret;
    }
    mediasync_wrap_getMediaTime(p_hwsync->mediasync, systemTime(SYSTEM_TIME_MONOTONIC) / 1000LL, &timeus, 0);
    *pts = (uint64_t)(timeus / 1000 * 90);
    return 0;
}

int aml_hwsync_wrap_get_vpts(audio_hwsync_t *p_hwsync, uint32_t *pts)
{
    ALOGI("%s(), [To do ]get tsync vpts", __func__);
    if (!p_hwsync->use_mediasync) {
        return aml_hwsync_wrap_single_get_tsync_vpts(pts);
    }
    /*To do*/
    (void)p_hwsync;
    (void)pts;
    return 0;
}

int aml_hwsync_wrap_get_firstvpts(audio_hwsync_t *p_hwsync, uint32_t *pts)
{
    ALOGI("%s(), [To do ]get tsync firstvpts", __func__);
    if (!p_hwsync->use_mediasync) {
        return aml_hwsync_wrap_single_get_tsync_firstvpts(pts);
    }
    /*To do*/
    (void)p_hwsync;
    (void)pts;
    return 0;
}

int aml_hwsync_wrap_reset_pcrscr(audio_hwsync_t *p_hwsync, uint64_t pts)
{
    ALOGV("%s(), reset tsync pcr:(%" PRIu64 ")", __func__, pts);
    if (!p_hwsync->use_mediasync) {
        return aml_hwsync_wrap_single_reset_tsync_pcrscr(pts);
    }
    int64_t timeus = ((int64_t)pts) / 90 *1000;
    mediasync_wrap_updateAnchor(p_hwsync->mediasync, timeus, 0, 0);

    return 0;
}



/***************  mediasync interfaces.   *****************/
//mediasync interfaces.
void* aml_hwsync_wrap_mediasync_create (void) {
    return mediasync_wrap_create();
}

bool check_support_mediasync(void)
{
    /*************************************************/
    /*    check_support_mediasync                    */
    /*    1. vendor.media.omx.use.omx2 be set true   */
    /*    2. android R use 5.4 kernel                */
    /*************************************************/
    struct utsname info;
    int kernel_version_major = 4;
    int kernel_version_minor = 9;
    char buf[PROPERTY_VALUE_MAX];
    int ret = -1;

    if (uname(&info) || sscanf(info.release, "%d.%d", &kernel_version_major, &kernel_version_minor) <= 0) {
        ALOGW("Could not get linux version: %s", strerror(errno));
    }

    ALOGI("%s kernel_version_major:%d", __func__, kernel_version_major);
    if (kernel_version_major > 4) {
        ALOGI("%s kernel 5.4 use mediasync", __func__);
        return true;
    }
    return false;
}

void* aml_hwsync_mediasync_create(void)
{
    ALOGI("%s", __func__);
    if (check_support_mediasync()) {
        ALOGI("%s use mediasync", __func__);
        return aml_hwsync_wrap_mediasync_create();
    }
    ALOGI("%s use do not use mediasync", __func__);
    return NULL;
}


bool aml_hwsync_wrap_get_id(void *mediasync, int32_t* id)
{
    if(mediasync) {
        return mediasync_wrap_allocInstance(mediasync, 0, 0, id);
    }
    return false;
}

bool aml_hwsync_wrap_set_id(audio_hwsync_t *p_hwsync, uint32_t id)
{

    if(p_hwsync->mediasync) {
        return mediasync_wrap_bindInstance(p_hwsync->mediasync, id, MEDIA_AUDIO);
    }
    return false;
}

bool aml_hwsync_wrap_release(audio_hwsync_t *p_hwsync)
{

    if(p_hwsync->mediasync) {
        mediasync_wrap_destroy(p_hwsync->mediasync);
        return true;
    }
    return false;
}


void aml_hwsync_wrap_wait_video_start(audio_hwsync_t *p_hwsync, uint32_t wait_count)
{
    bool ret = false;
    int count = 0;
    int64_t outMediaUs = -1;
    sync_mode mode = MEDIA_SYNC_MODE_MAX;

    if (!p_hwsync->mediasync) {
        return;
     }
    ret = mediasync_wrap_getSyncMode(p_hwsync->mediasync, &mode);
    if (!ret || (mode != MEDIA_SYNC_VMASTER)) {
        return;
    }

    ret = mediasync_wrap_getTrackMediaTime(p_hwsync->mediasync, &outMediaUs);
    if (!ret) {
        ALOGI("mediasync_wrap_getTrackMediaTime error");
        return;
    }
    ALOGI("start sync with video %" PRId64 "", outMediaUs);
    if (outMediaUs <= 0) {
        ALOGI("wait video start");
        while (count < wait_count) {
            usleep(20000);
            ret = mediasync_wrap_getTrackMediaTime(p_hwsync->mediasync, &outMediaUs);
            if (!ret) {
                return;
            }
            if (outMediaUs > 0) {
                break;
            }
            count++;
        }
    }
    ALOGI("video start");
    return;
}

void aml_hwsync_wrap_wait_video_drop(audio_hwsync_t *p_hwsync, uint64_t cur_pts, uint32_t wait_count)
{
    bool ret = false;
    int count = 0;
    int64_t nowUs;
    int64_t outRealMediaUs;
    int64_t outMediaPts;
    int64_t audio_cur_pts = 0;
    sync_mode mode = MEDIA_SYNC_MODE_MAX;

    if (!p_hwsync->mediasync) {
        return;
    }
    ret = mediasync_wrap_getSyncMode(p_hwsync->mediasync, &mode);

    nowUs = systemTime(SYSTEM_TIME_MONOTONIC) / 1000LL;
    ret = mediasync_wrap_getMediaTime(p_hwsync->mediasync, nowUs,
                                    &outRealMediaUs, false);
    if (!ret) {
        return;
    }
    outMediaPts = outRealMediaUs / 1000LL * 90;
    audio_cur_pts = (int64_t)cur_pts;
    ALOGI("====================, now audio pts %" PRId64 " vpts  %" PRId64 " ", audio_cur_pts, outMediaPts);
    if ((audio_cur_pts - outMediaPts) > SYSTIME_CORRECTION_THRESHOLD) {
        bool ispause = false;
        bool ret = mediasync_wrap_getPause(p_hwsync->mediasync, &ispause);
        if (ret && ispause) {
            mediasync_wrap_setPause(p_hwsync->mediasync, false);
        }
        count = 0;
        while (count < wait_count) {
            int64_t nowUs = systemTime(SYSTEM_TIME_MONOTONIC) / 1000LL;
            ret = mediasync_wrap_getMediaTime(p_hwsync->mediasync, nowUs,
                                    &outRealMediaUs, false);
            if (!ret) {
                return;
            }
            outMediaPts = outRealMediaUs / 1000LL * 90;
            if ((audio_cur_pts - outMediaPts) <= SYSTIME_CORRECTION_THRESHOLD)
                break;
            usleep(20000);
            count++;
            ALOGI("first audio wait video %d ms,now audiopts %" PRId64 " vpts %" PRId64 " ", count * 20, audio_cur_pts, outMediaPts);
        }
    } else {
        bool ispause = false;
        bool ret = mediasync_wrap_getPause(p_hwsync->mediasync, &ispause);
        if (ret && ispause) {
            mediasync_wrap_setPause(p_hwsync->mediasync, false);
        }
    }
    mediasync_wrap_setSyncMode(p_hwsync->mediasync, MEDIA_SYNC_AMASTER);

    return;
}


void aml_hwsync_wait_video_start(audio_hwsync_t *p_hwsync)
{
    int waitcount = 0;
    int ret = -1;

    if (!p_hwsync->use_mediasync) {
        return;
    }

    waitcount = property_get_int32("vendor.media.audio.hal.waitvpts.count", 100);
    aml_hwsync_wrap_wait_video_start(p_hwsync, waitcount);
    return;
}

void aml_hwsync_wait_video_drop(audio_hwsync_t *p_hwsync, uint64_t cur_pts)
{
    int waitcount = 0;
    int ret = -1;

    if (!p_hwsync->use_mediasync) {
        return;
    }

    waitcount = property_get_int32("vendor.media.audio.hal.waitvpts.count", 100);
    aml_hwsync_wrap_wait_video_drop(p_hwsync, cur_pts, waitcount);
    return;
}


bool aml_audio_hwsync_set_time_gap_threshold(audio_hwsync_t *p_hwsync, int64_t threshold_value)
{
    int64_t value = 0;
    bool ret = false;

    ret = mediasync_wrap_getUpdateTimeThreshold(p_hwsync->mediasync, &value);
    if (ret && value != threshold_value) {
        ret = mediasync_wrap_setUpdateTimeThreshold(p_hwsync->mediasync, threshold_value);
        ALOGD("%s, ret:%d threshold value is changed from %" PRId64 " to %" PRId64 "", __func__,ret, value, threshold_value);
    } else {
        ALOGV("%s, ret:%d, threshold value is same %" PRId64 ", not to set again.", __func__, ret, value);
    }

    return ret;
}

bool aml_audio_hwsync_update_threshold(audio_hwsync_t *p_hwsync)
{
    bool ret = false;
    int64_t avsync_time_threshold_value = 0;
    struct aml_audio_device *adev = p_hwsync->aout->dev;
    /* the threshold default value is 50ms,
     * it will be changed to 100ms for drop when pass through mode.
     * this modification is for MiBox plist passthrough of NTS cases
     */
    if (adev && (BYPASS == adev->digital_audio_format || eDolbyMS12Lib != adev->dolby_lib_type)) {
        avsync_time_threshold_value = 100*1000;
        ret = aml_audio_hwsync_set_time_gap_threshold(p_hwsync, avsync_time_threshold_value);
    } else {
        avsync_time_threshold_value = 50*1000;
        ret = aml_audio_hwsync_set_time_gap_threshold(p_hwsync, avsync_time_threshold_value);
    }
    ALOGV("%s, avsync_time_threshold_value:%" PRId64 ", update threshold finished.",
            __func__, avsync_time_threshold_value);

    return ret;
}



/***************  tsync interfaces.   *****************/
//tsync interfaces.
int aml_hwsync_get_tsync_pcr(audio_hwsync_t *p_hwsync, uint64_t *value)
{
    return aml_hwsync_tsync_get_pcr(p_hwsync, value);
}

int aml_hwsync_wrap_get_tsync_pts_by_handle(int fd, uint64_t *pts)
{
    return aml_hwsync_get_tsync_pts_by_handle(fd, pts);
}

int aml_hwsync_wrap_set_tsync_open(void)
{
    return aml_hwsync_open_tsync();
}

void aml_hwsync_wrap_set_tsync_close(int fd)
{
    aml_hwsync_close_tsync(fd);
}

void aml_hwsync_wrap_set_tsync_init(audio_hwsync_t *p_hwsync)
{
    ALOGI("%s(), send tsync enable", __func__);
    if (!p_hwsync->use_mediasync) {
        return aml_hwsync_wrap_single_set_tsync_init();
    }
}


int aml_audio_start_trigger(void *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    char tempbuf[128];
    ALOGI("reset alsa to set the audio start\n");
    pcm_stop(aml_out->pcm);
    sprintf(tempbuf, "AUDIO_START:0x%x", adev->first_apts);
    ALOGI("audio start set tsync -> %s", tempbuf);

    aml_hwsync_wrap_set_tsync_init(aml_out->hwsync);

    if (aml_hwsync_wrap_set_start_pts(aml_out->hwsync, adev->first_apts)  == -1) {
        ALOGE("set AUDIO_START failed \n");
        return -1;
    }
    return 0;
}

/*FIXME: not use them currently*/
/*wrap the tsync/mediasync create and release interfaces*/
int aml_audio_hwsync_wrap_create(void)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)aml_adev_get_handle();

    //try to create mediasync firstly.
    adev->hw_mediasync = aml_hwsync_mediasync_create();
    if (adev->hw_mediasync) {
        ALOGI("%s  create mediasync successful\n",__func__);
        return 0;
    }

    //then try to open tsync
    adev->tsync_fd = aml_hwsync_wrap_set_tsync_open();
    if (adev->tsync_fd < 0) {
        ALOGE("%s() open tsync failed", __func__);
    } else {
        ALOGI("%s  open tsync successful\n",__func__);
    }
    return 0;
}

void aml_audio_hwsync_wrap_release(audio_hwsync_t *p_hwsync)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)aml_adev_get_handle();

    ALOGI("%s", __func__);
    if (p_hwsync) {
        //close the mediasync
        aml_hwsync_wrap_release(p_hwsync);
        ALOGI("%s release mediasync done", __func__);
        return;
    }

    //try to close the tsync
    aml_hwsync_wrap_set_tsync_close(adev->tsync_fd);

    ALOGI("%s close tsync done", __func__);
    return;
}

