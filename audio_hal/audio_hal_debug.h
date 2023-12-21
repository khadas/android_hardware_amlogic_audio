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

// This header file put device independent part

#ifndef _AUDIO_HAL_DEBUG_H_
#define _AUDIO_HAL_DEBUG_H_

#include "aml_malloc_debug.h" // for aml_audio_malloc/free

#ifndef AM_LOGV
#define AM_LOGV(fmt, ...)  ALOGV("[%s:%d] " fmt, __func__, __LINE__, ## __VA_ARGS__)
#endif
#ifndef AM_LOGD
#define AM_LOGD(fmt, ...)  ALOGD("[%s:%d] " fmt, __func__, __LINE__, ## __VA_ARGS__)
#endif
#ifndef AM_LOGI
#define AM_LOGI(fmt, ...)  ALOGI("[%s:%d] " fmt, __func__, __LINE__, ## __VA_ARGS__)
#endif
#ifndef AM_LOGW
#define AM_LOGW(fmt, ...)  ALOGW("[%s:%d] " fmt, __func__, __LINE__, ## __VA_ARGS__)
#endif
#ifndef AM_LOGE
#define AM_LOGE(fmt, ...)  ALOGE("[%s:%d] " fmt, __func__, __LINE__, ## __VA_ARGS__)
#endif

int aml_audio_dump_audio_bitstreams(const char *path, const void *buf, size_t bytes);
int getprop_bool(const char *path);
int aml_get_debug_value(void);

// utility function about pcm config
#define PCM_CONFIG_STR_LEN          128
/**
 * @brief show pcm config to string
 * For example, string is "(fmt=0 rt=48000 ch=2 period=1024*4 thr=1024-0 sil_sz=0 avail_min=0)"
 *
 * @param cfg config
 * @param s string buffer
 * @param len length of string buffer, suggested size is PCM_CONFIG_STR_LEN
 *
 * @return string buffer
 */
char *show_pcm_config(struct pcm_config *cfg, char *s, size_t len);

static unsigned int bytes_to_frames(struct pcm_config *cfg, unsigned int bytes)
{
    return bytes / (cfg->channels * (pcm_format_to_bits(cfg->format) >> 3));
}

static unsigned int frames_to_bytes(struct pcm_config *cfg, unsigned int frames)
{
    return frames * (cfg->channels * (pcm_format_to_bits(cfg->format) >> 3));
}

static unsigned int bytes_per_frame(struct pcm_config *cfg)
{
    return cfg->channels * (pcm_format_to_bits(cfg->format) >> 3);
}

static void *chk_alloc(void *p, size_t *origin_sz, size_t sz)
{
    if (*origin_sz != sz) {
        if (p) {
            aml_audio_free(p);
            p = NULL;
        }
        void *new_p = NULL;
        if (sz != 0) {
            new_p = aml_audio_malloc(sz);
        }
        AM_LOGI("p=%p/%zu => %p/%zu", (void *)p, *origin_sz, new_p, sz);
        *origin_sz = sz;
        return new_p;
    }
    return p;
}

#endif
