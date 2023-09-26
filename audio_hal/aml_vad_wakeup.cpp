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
#include <thread>
#include <log/log.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <cutils/properties.h>

extern "C" {
#include "audio_hw_utils.h"
#include "alsa_device_parser.h"
}

#include "aml_vad_wakeup.h"

using namespace std;
#define VAD_DEVICE 3

typedef struct vad_wakeup_t {
    bool exit_run;
    thread *p_thread;
    bool is_suspend;
    struct pcm *pcm;
    struct aml_mixer_handle *mixer;
} VAD_WAKEUP_T;
static VAD_WAKEUP_T *g_pst_vad_wakeup = NULL;

#define ASR_IOC_TYPE                                    'C'
#define ASR_IOC_TYPE_GET_ASR_DATA_SIZE                  0
#define ASR_IOC_TYPE_RESET_ASR_DATA                     1

#define ASR_IOC_TYPE_CMD_GET_ASR_DATA_SIZE              \
    _IOR(ASR_IOC_TYPE, ASR_IOC_TYPE_GET_ASR_DATA_SIZE, unsigned int)
#define ASR_IOC_TYPE_CMD_RESET_ASR_DATA              \
    _IO(ASR_IOC_TYPE, ASR_IOC_TYPE_RESET_ASR_DATA)

static bool g_is_dumping_data = false;

static void aml_vad_thread(VAD_WAKEUP_T* vad) {
    if (vad == NULL) {
        AM_LOGE("vad is null");
        return;
    }
    struct pcm_config config;
    struct mixer *mixer;
    void *buffer;
    unsigned int size;
    int device = VAD_DEVICE;

    device = property_get_int32(AML_AUDIO_VAD_DEVICE_PROP, VAD_DEVICE);
    config.channels = property_get_int32(AML_AUDIO_VAD_CHANNEL_PROP, 1);
    config.rate = property_get_int32(AML_AUDIO_VAD_RATE_PROP, 16000);
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
        vad->pcm = pcm_open(alsa_device_get_card_index(), device, PCM_IN, &config);
    }
    if (!vad->pcm || !pcm_is_ready(vad->pcm)) {
        AM_LOGE("Unable to open PCM device (%s)", pcm_get_error(vad->pcm));
        return;
    }

    size = pcm_frames_to_bytes(vad->pcm, pcm_get_buffer_size(vad->pcm));
    buffer = aml_audio_calloc(1,size);
    if (!buffer) {
        AM_LOGE("Unable to allocate %u bytes", size);
        pcm_close(vad->pcm);
        return;
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
    pcm_close(vad->pcm);
    vad->pcm = NULL;
    AM_LOGD("exit---");
    aml_audio_free(buffer);
    return;
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
    if (g_pst_vad_wakeup->p_thread != nullptr || g_pst_vad_wakeup->exit_run == false) {
        AM_LOGW("already vadThread running");
        return -1;
    }

    g_pst_vad_wakeup->exit_run = false;
    g_pst_vad_wakeup->mixer = mixer;
    aml_mixer_ctrl_set_int(mixer, AML_MIXER_ID_VAD_ENABLE, 1);
    source = property_get_int32(AML_AUDIO_VAD_SOURCE_PROP, 4);
    aml_mixer_ctrl_set_int(mixer, AML_MIXER_ID_VAD_SOURCE_SEL, source);
    g_pst_vad_wakeup->p_thread = new thread(aml_vad_thread, g_pst_vad_wakeup);
    if (ret) {
        AM_LOGE("vad wake error creating thread: %s", strerror(ret));
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
    g_pst_vad_wakeup->p_thread->join();
    delete g_pst_vad_wakeup->p_thread;
    g_pst_vad_wakeup->p_thread = nullptr;
    aml_mixer_ctrl_set_int(mixer, AML_MIXER_ID_VAD_ENABLE, 0);
    aml_audio_free(g_pst_vad_wakeup);
    g_pst_vad_wakeup = NULL;
    return true;
}

static void aml_vad_read_dump_data() {
    int vad_dump_dev_fd = -1;
    unsigned int dump_data_size = 0;
    unsigned int read_bytes = 0;
    FILE *file = NULL;

    vad_dump_dev_fd = open("/dev/asr_dump_data", O_RDWR);
    if (vad_dump_dev_fd < 0) {
        AM_LOGE("open device file:/dev/asr_dump_data failed. err: %s", strerror(errno));
        goto exit;
    }
    AM_LOGD("starting dump vad data, fd:%d", vad_dump_dev_fd);

    if (ioctl(vad_dump_dev_fd, ASR_IOC_TYPE_CMD_GET_ASR_DATA_SIZE, &dump_data_size) < 0) {
        AM_LOGE("ioctl GET_ASR_DATA_SIZE failed. err:%s", strerror(errno));
        goto out;
    }
    AM_LOGD("available vad data size:%d", dump_data_size);

    file = fopen("/data/vad_dump_data", "w");
    if (file == NULL) {
        AM_LOGE("open /data/vad_dump_data fail. err:%s", strerror(errno));
        goto out;
    }

    while (read_bytes < dump_data_size) {
        char data[4096];
        AM_LOGI("");
        ssize_t ret = read(vad_dump_dev_fd, data, sizeof(data));
        if (ret > 0) {
            fwrite(data, 1, ret, file);
            AM_LOGV("read data ret:%zd, read_bytes:%d", ret, read_bytes);
            read_bytes += ret;
        } else if (ret == 0) {
            AM_LOGW("read end, no data.");
            break;
        } else {
            AM_LOGE("read fail ret:%zd err:%s", ret, strerror(errno));
            break;
        }
    }

    fclose(file);
out:
    if (ioctl(vad_dump_dev_fd, ASR_IOC_TYPE_CMD_RESET_ASR_DATA) < 0) {
        AM_LOGE("ioctl RESET_ASR_DATA failed. err:%s", strerror(errno));
    }
    close(vad_dump_dev_fd);

exit:
    g_is_dumping_data = false;
    AM_LOGI("dump vad data end");
}

void aml_vad_dump() {
    if (g_is_dumping_data) {
        AM_LOGW("already dumping data.");
        return;
    }
    g_is_dumping_data = true;
    thread dump_thread(aml_vad_read_dump_data);
    dump_thread.detach();
}
