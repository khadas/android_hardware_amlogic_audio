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

#define LOG_TAG "audio_hw_automotive_playback_handler"
//#define LOG_NDEBUG 0

#include <stdio.h>
#include <system/audio.h>
#include <pthread.h>

#include "bus_mix_playback_handler.h"
#include "playback_handler_base.h"
#include "audio_hw_utils.h"
#include "aml_dump_debug.h"
#include "audio_data_process.h"
#include "audio_port.h"
#include "bus_submix_core.h"
#include "aml_channel_index.h"

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

typedef struct bus_mix_playback_handler {
    struct playback_handler_base playback_handler;
    struct audio_stream_out *stream_out;
    struct bus_submix_core *mixCore;
    /* config define by AML audio hal*/
    struct audioCfg config;
    /* port context is created by SubMix core*/
    input_port *mix_port;
    int portId;
    int busId;
    bool mixer_exit;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} BusMixPlaybackHandler;

int waitPortAvail(BusMixPlaybackHandler *playbackPort, uint32_t timeout_us)
{
    struct timespec ts;
    ts_wait_time_us(&ts, timeout_us);
    pthread_mutex_lock(&playbackPort->lock);
    AM_LOGV("++wait start");
    pthread_cond_timedwait(&playbackPort->cond, &playbackPort->lock, &ts);
    AM_LOGV("--wait wakeup");
    pthread_mutex_unlock(&playbackPort->lock);
    return 0;
}

int onPortBufAvail(void *impl)
{
    BusMixPlaybackHandler *playbackPort = (BusMixPlaybackHandler *)impl;
    pthread_mutex_lock(&playbackPort->lock);
    pthread_cond_signal(&playbackPort->cond);
    pthread_mutex_unlock(&playbackPort->lock);
    return 0;
}

int onNotifyMixerExit(void *impl)
{
    BusMixPlaybackHandler *playbackPort = (BusMixPlaybackHandler *)impl;
    pthread_mutex_lock(&playbackPort->lock);
    playbackPort->mixer_exit = true;
    pthread_cond_signal(&playbackPort->cond);
    pthread_mutex_unlock(&playbackPort->lock);
    return 0;
}


static struct mixer_callbacks bus_mix_callbacks = {
    .user = NULL, /*set it when create mix port*/
    .onInputAvail = onPortBufAvail,
    .onNotifyEvent = onNotifyMixerExit,
    .onMetaCbk = NULL,
};

size_t bus_port_write(void *handle, const void* buffer, size_t bytes)
{
    BusMixPlaybackHandler *playbackPort = (BusMixPlaybackHandler *)handle;
    input_port *mix_port = playbackPort->mix_port;
    size_t written = 0;
    size_t remain = bytes;
    const uint8_t *data = (const uint8_t*)buffer;

    if (playbackPort->mixer_exit || !mix_port) {
        AM_LOGW("Invalid mix condition, directly return!");
        return bytes;
    }

    if (getprop_bool("vendor.media.audiohal.outdump")) {
        aml_dump_audio_bitstreams("/data/audio/bus_playback_handler_out.raw", buffer, bytes);
    }

    while (written < bytes) {
        int ret_size = mix_port->write(mix_port, data, remain);
        if (get_inport_state(mix_port) != ACTIVE) {
            AM_LOGI("input index:%d is active now", mix_port->ID);
            set_inport_state(mix_port, ACTIVE);
        }
        if (ret_size < 0) {
            AM_LOGE("write in_port failed, ret:%d", ret_size);
            return bytes;
        }

        written += ret_size;
        remain -= ret_size;
        data += ret_size;

        if (written < bytes) {
            waitPortAvail(playbackPort, 50 * 1000 /*us*/);
        }
        if (playbackPort->mixer_exit) {
            AM_LOGW("mixer core exit!!");
            break;
        }
        AM_LOGV("bytes:%zu written:%zu remain:%zu ret_size:%d", bytes, written, remain, ret_size);
    }
    return bytes;
}

int bus_port_open(void *handle, uint64_t written_frames)
{
    int ret = 0;
    BusMixPlaybackHandler *playbackPort = (BusMixPlaybackHandler *)handle;
    int source_channels = playbackPort->config.channelCnt;

    pthread_mutex_lock(&playbackPort->lock);

    if (playbackPort->mix_port == NULL) {
        bus_mix_callbacks.user = playbackPort;
        input_port *in_port = create_mixer_port(playbackPort->mixCore,
                                            &playbackPort->config,
                                            0, /*flags*/
                                            &bus_mix_callbacks,
                                            written_frames,
                                            1.0);
        if (!in_port) {
            AM_LOGE("input_port=NULL, return!");
            pthread_mutex_unlock(&playbackPort->lock);
            return -EINVAL;
        }

        ret = set_inport_channel_mux_table(in_port, playbackPort->busId);
        if (ret < 0) {
            delete_mixer_port(playbackPort->mixCore, in_port);
            pthread_mutex_unlock(&playbackPort->lock);
            AM_LOGE("input_port=NULL, return!");
            return -EINVAL;
        }

        playbackPort->mix_port = in_port;
        playbackPort->portId = in_port->ID;
    }

    pthread_mutex_unlock(&playbackPort->lock);
    AM_LOGI("OK channels:%d portId:%d busId:%d", source_channels, playbackPort->portId, playbackPort->busId);
    return ret;
}

int bus_port_close(void *handle)
{
    BusMixPlaybackHandler *playbackPort = (BusMixPlaybackHandler *)handle;
    pthread_mutex_lock(&playbackPort->lock);
    if (playbackPort->mix_port) {
        delete_mixer_port(playbackPort->mixCore, playbackPort->mix_port);
        playbackPort->mix_port = NULL;
        playbackPort->portId = -1;
    }
    pthread_mutex_unlock(&playbackPort->lock);
    return 0;
}


int bus_port_get_presentation_position(void *handle, uint64_t *frames, struct timespec *timestamp)
{
    BusMixPlaybackHandler *playbackPort = (BusMixPlaybackHandler *)handle;
    int ret = -1;
    pthread_mutex_lock(&playbackPort->lock);
    if (playbackPort->mix_port != NULL) {
        ret = get_mixer_port_presentation(playbackPort->mixCore, playbackPort->mix_port, frames, timestamp);
    }
    pthread_mutex_unlock(&playbackPort->lock);
    return ret;
}

PlaybackHandlerBase *create_bus_playback_handler(struct aml_audio_device* adev,
                                        struct audio_stream_out *stream_out,
                                        struct audio_config *config,
                                        int bus_id)
{
    BusMixPlaybackHandler *playbackPort = (BusMixPlaybackHandler *)aml_audio_calloc(1, sizeof(BusMixPlaybackHandler));
    if (!playbackPort) {
        AM_LOGE("No memory!");
        return NULL;
    }

    init_playback_handle_base(&playbackPort->playback_handler);
    playbackPort->playback_handler.open = bus_port_open;
    playbackPort->playback_handler.close = bus_port_close;
    playbackPort->playback_handler.write = bus_port_write;
    playbackPort->playback_handler.get_presentation_position = bus_port_get_presentation_position;

    struct audioCfg portCfg;
    portCfg.channelMask = config->channel_mask;
    portCfg.channelCnt = audio_channel_count_from_out_mask(config->channel_mask);
    portCfg.sampleRate = config->sample_rate;
    portCfg.format = config->format;
    portCfg.frame_size = audio_bytes_per_sample(portCfg.format) * portCfg.channelCnt;
    memcpy(&playbackPort->config, &portCfg, sizeof(struct audioCfg));

    playbackPort->stream_out = stream_out;
    playbackPort->busId = bus_id;

    if (pthread_mutex_init(&playbackPort->lock, NULL) != 0) {
        AM_LOGE("pthread_mutex_init fail, errno:%s", strerror(errno));
        goto err;
    }
    if (pthread_cond_init(&playbackPort->cond, NULL) != 0) {
        AM_LOGE("pthread_cond_init fail, errno:%s", strerror(errno));
        goto err;
    }

    playbackPort->mixCore = get_bus_mix_core(adev);
    if (playbackPort->mixCore == NULL) {
        AM_LOGE("SubMixCore = NULL, return NULL!");
        goto err;
    }

    AM_LOGI("OK bus_id:%d", bus_id);
    return &playbackPort->playback_handler;

err:
    aml_audio_free(playbackPort);
    return NULL;
}

int delete_bus_playback_handler(PlaybackHandlerBase *handle)
{
    if (!handle) {
        return -1;
    }

    BusMixPlaybackHandler *playbackPort = (BusMixPlaybackHandler *)handle;
    pthread_mutex_destroy(&playbackPort->lock);
    pthread_cond_destroy(&playbackPort->cond);
    aml_audio_free(playbackPort);
    return 0;
}
