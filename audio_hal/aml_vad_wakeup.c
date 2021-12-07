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

#define LOG_TAG "aml_vad_wakeup"

#include <tinyalsa/asoundlib.h>
#include <stdlib.h>
#include <string.h>
#include <log/log.h>
#include <unistd.h>
#include <pthread.h>
#include "audio_hw_utils.h"

#include "aml_vad_wakeup.h"

#define VAD_CARD 0
#define VAD_DEVICE 3

typedef struct vad_wakeup_t {
    bool exit_run;
    pthread_t thread_id;
    bool is_suspend;
    struct pcm *pcm;
    struct aml_mixer_handle *mixer;
} VAD_WAKEUP_T;
static VAD_WAKEUP_T *g_pst_vad_wakeup = NULL;

static void* aml_vad_thread(void* data) {
    VAD_WAKEUP_T *vad = (VAD_WAKEUP_T *)data;
    struct pcm_config config;
    struct mixer *mixer;
    void *buffer;
    unsigned int size;
    int device = VAD_DEVICE;

//    device = vad->pSysWrite->getPropertyInt("persist.vendor.sys.vad.device", VAD_DEVICE);
//    config.channels = vad->pSysWrite->getPropertyInt("persist.vendor.sys.vad.channel", 1);
//    config.rate = vad->pSysWrite->getPropertyInt("persist.vendor.sys.vad.rate", 16000);
    memset(&config, 0, sizeof(config));
    config.channels = 1;
    config.rate = 16000;

    config.period_size = 1024;
    config.period_count = 4;
    config.format = PCM_FORMAT_S16_LE;
    config.start_threshold = 0;
    config.stop_threshold = 0;
    config.silence_threshold = 0;

    if (vad->pcm == NULL) {
        vad->pcm = pcm_open(VAD_CARD, device, PCM_IN, &config);
    }
    if (!vad->pcm || !pcm_is_ready(vad->pcm)) {
        AM_LOGE("Unable to open PCM device (%s)", pcm_get_error(vad->pcm));
        return NULL;
    }

    size = pcm_frames_to_bytes(vad->pcm, pcm_get_buffer_size(vad->pcm));
    buffer = aml_audio_calloc(1,size);
    if (!buffer) {
        AM_LOGE("Unable to allocate %u bytes", size);
        pcm_close(vad->pcm);
        return NULL;
    }
    AM_LOGD("Capturing ch:%d rate:%d bit:%d, read_size:%d", config.channels, config.rate,
           pcm_format_to_bits(config.format), size);
    while (true) {
        int ret = pcm_read(vad->pcm, buffer, size);
        if (ret != 0) {
            AM_LOGD("pcm_read fail need:%d, ret:%d", size, ret);
        }
        if (vad->exit_run) {
            break;
        }
    }
    AM_LOGD("exit---");
    aml_audio_free(buffer);
    return NULL;
}

int32_t aml_vad_suspend(struct aml_mixer_handle *mixer) {
    int ret = 0;
    int source = 4;
    AM_LOGD("+++");
    if (g_pst_vad_wakeup) {
        AM_LOGW("already vadSuspend");
    } else {
        g_pst_vad_wakeup = (VAD_WAKEUP_T *)aml_audio_calloc(1, sizeof(VAD_WAKEUP_T));
        g_pst_vad_wakeup->exit_run = true;
    }

    R_CHECK_POINTER_LEGAL(-1, g_pst_vad_wakeup, "");
    if (g_pst_vad_wakeup->exit_run == false) {
        AM_LOGW("already vadThread running");
        return -1;
    }

    g_pst_vad_wakeup->exit_run = false;
    g_pst_vad_wakeup->mixer = mixer;
    aml_mixer_ctrl_set_int(mixer, AML_MIXER_ID_VAD_ENABLE, 1);
//    source = pSysWrite->getPropertyInt("persist.vendor.sys.vad.source", 4);
    aml_mixer_ctrl_set_int(mixer, AML_MIXER_ID_VAD_SOURCE_SEL, source);
    ret = pthread_create(&g_pst_vad_wakeup->thread_id, NULL, aml_vad_thread, g_pst_vad_wakeup);
    if (ret) {
        AM_LOGE("vadwake error creating thread: %s", strerror(ret));
        return false;
    }
    return true;
}

int32_t aml_vad_resume(struct aml_mixer_handle *mixer) {
    AM_LOGD("+++");
    int ret = 0;
    R_CHECK_POINTER_LEGAL(-1, g_pst_vad_wakeup, "");
    if (g_pst_vad_wakeup->exit_run) {
        AM_LOGW("already vadThread running");
        return -1;
    }
    g_pst_vad_wakeup->exit_run = true;
    if (g_pst_vad_wakeup->pcm != NULL) {
        pcm_close(g_pst_vad_wakeup->pcm);
        g_pst_vad_wakeup->pcm = NULL;
    }
    pthread_join(g_pst_vad_wakeup->thread_id, NULL);
    aml_mixer_ctrl_set_int(mixer, AML_MIXER_ID_VAD_ENABLE, 0);
    aml_audio_free(g_pst_vad_wakeup);
    g_pst_vad_wakeup = NULL;
    return true;
}


