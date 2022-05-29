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

#define LOG_TAG "audio_tsync_wrap"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <cutils/log.h>
#include "audio_hw_utils.h"
#include "audio_tsync_wrap.h"
#include "aml_audio_sysfs.h"


void aml_hwsync_wrap_single_set_tsync_init(void)
{
    ALOGI("%s(), send tsync enable", __func__);
    sysfs_set_sysfs_str(TSYNC_ENABLE, "1"); // enable avsync
    sysfs_set_sysfs_str(TSYNC_MODE, "1"); // enable avsync
}

void aml_hwsync_wrap_single_set_tsync_pause(void)
{
    ALOGI("%s(), send pause event", __func__);
    sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_PAUSE");
}

void aml_hwsync_wrap_single_set_tsync_resume(void)
{
    ALOGI("%s(), send resuem event", __func__);
    sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_RESUME");
}

void aml_hwsync_wrap_single_set_tsync_stop(void)
{
    ALOGI("%s(), send stop event", __func__);
    sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_STOP");
}

int aml_hwsync_wrap_single_set_tsync_start_pts(uint32_t pts)
{
    char buf[64] = {0};

    snprintf(buf, 64, "AUDIO_START:0x%x", pts);
    ALOGI("tsync -> %s", buf);
    return sysfs_set_sysfs_str(TSYNC_EVENT, buf);
}

int aml_hwsync_wrap_single_set_tsync_start_pts64(uint64_t pts)
{
    char buf[64] = {0};
    sprintf (buf, "AUDIO_START:0x%"PRIx64"", pts & 0xffffffff);
    ALOGI("tsync -> %s", buf);
    return sysfs_set_sysfs_str(TSYNC_EVENT, buf);
}

int aml_hwsync_wrap_single_get_tsync_pts(uint32_t *pts)
{
    if (!pts) {
        ALOGE("%s(), NULL pointer", __func__);
        return -EINVAL;
    }

    return get_sysfs_uint(TSYNC_PCRSCR, pts);
}

int aml_hwsync_wrap_single_get_tsync_vpts(uint32_t *pts)
{
    if (!pts) {
        ALOGE("%s(), NULL pointer", __func__);
        return -EINVAL;
    }

    return get_sysfs_uint(TSYNC_VPTS, pts);
}

int aml_hwsync_wrap_single_get_tsync_firstvpts(uint32_t *pts)
{
    if (!pts) {
        ALOGE("%s(), NULL pointer", __func__);
        return -EINVAL;
    }

    return get_sysfs_uint(TSYNC_FIRSTVPTS, pts);
}

int aml_hwsync_wrap_single_reset_tsync_pcrscr(uint64_t pts)
{
    char buf[64] = {0};

    uint32_t pts32 = (uint32_t)pts;
    snprintf(buf, 64, "0x%x", pts32);
    ALOGI("tsync -> reset pcrscr 0x%x", pts32);
    return sysfs_set_sysfs_str(TSYNC_APTS, buf);
}


int aml_hwsync_tsync_get_pcr(audio_hwsync_t *p_hwsync, uint64_t *value)
{
    int fd = -1;
    char valstr[64];
    uint64_t val = 0;
    off_t offset;
    if (!p_hwsync) {
        ALOGE("invalid pointer %s", __func__);
        return -1;
    }
    if (p_hwsync->tsync_fd < 0) {
        fd = open(TSYNC_PCRSCR, O_RDONLY);
        p_hwsync->tsync_fd = fd;
        ALOGI("%s open tsync fd %d", __func__, fd);
    } else {
        fd = p_hwsync->tsync_fd;
    }
    if (fd >= 0) {
        memset(valstr, 0, 64);
        offset = lseek(fd, 0, SEEK_SET);
        valstr[sizeof(valstr) - 1] = '\0';
        read(fd, valstr, 64 - 1);
    } else {
        ALOGE("%s unable to open file %s\n", __func__, TSYNC_PCRSCR);
        return -1;
    }
    if (sscanf(valstr, "0x%" PRIx64 "", &val) < 1) {
        ALOGE("%s unable to get pcr from: %s,fd %d,offset %ld", __func__, valstr, fd, offset);
        return -1;
    }
    *value = val;
    return 0;
}

int aml_hwsync_get_tsync_pts_by_handle(int fd, uint64_t *pts)
{
    ALOGV("%s", __func__);
    char valstr[64];
    uint64_t val = 0;

    if (!pts) {
        ALOGE("%s(), NULL pointer", __func__);
        return -EINVAL;
    }

    if (fd >= 0) {
        memset(valstr, 0, 64);
        lseek(fd, 0, SEEK_SET);
        valstr[sizeof(valstr) - 1] = '\0';
        read(fd, valstr, 64 - 1);
    } else {
        ALOGE("invalid fd\n");
        return -EINVAL;
    }

    if (sscanf(valstr, "0x%" PRIx64 "", &val) < 1) {
        ALOGE("unable to get pts from: fd(%d), str(%s)", fd, valstr);
        return -EINVAL;
    }
    *pts = val;
    return 0;
}

int aml_hwsync_open_tsync(void)
{
    ALOGI("%s", __func__);
    return open(TSYNC_PCRSCR, O_RDONLY);
}

void aml_hwsync_close_tsync(int fd)
{
    ALOGI("%s", __func__);
    if (fd >= 0) {
        ALOGE("%s(), fd = %d", __func__, fd);
        close(fd);
    }
}

