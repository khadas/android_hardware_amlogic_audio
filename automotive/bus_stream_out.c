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

#define LOG_TAG "audio_hw_automotive_bus_streamout"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <inttypes.h>

#include <cutils/log.h>

#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <system/audio.h>

#include "audio_hw.h"
#include "audio_hw_utils.h"
#include "aml_audio_stream_base.h"
#include "bus_mix_playback_handler.h"
#include "playback_handler_base.h"
#include "aml_channel_index.h"

#define DEFAULT_STREAM_OUT_SAMPLE_RATE      48000
#define DEFAULT_STREAM_OUT_FORMAT          AUDIO_FORMAT_PCM_16_BIT
#define DEFAULT_STREAM_OUT_CHANNEL_MASK     AUDIO_CHANNEL_OUT_STEREO
#define DEFAULT_OUTPUT_BUFFER_FRAME_COUNT   1024
#define MAX_ADDRESS_LEN                     48

struct bus_stream_out {
    struct audio_stream_out stream;
    struct aml_streamout_base base;
    struct aml_audio_device *adev;
    struct audio_config src_config;
    struct audio_config dest_config;
    audio_output_flags_t flags;
    audio_devices_t devices;
    audio_io_handle_t outId;
    size_t buf_frame_count;
    size_t buf_latency_ms;
    size_t frame_size;
    /* presentation timestamp */
    uint64_t last_written_frames;
    struct timespec written_timestamp;
    uint64_t last_frames_report;
    struct timespec last_timestamp_report;
    uint64_t written_all_frames;
    int64_t last_write_time_us;
    int bus_id;
    char address[MAX_ADDRESS_LEN];
    bool standby;
    struct playback_handler_base *playback_handler;
    pthread_mutex_t lock;
};

uint64_t get_systime_us(void)
{
    struct timespec ts;
    uint64_t sys_time;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    sys_time = (uint64_t)ts.tv_sec * 1000000LL + (uint64_t)ts.tv_nsec / 1000LL;
    return sys_time;
}

static uint32_t bus_out_get_sample_rate(const struct audio_stream *stream)
{
    const struct bus_stream_out *out = (const struct bus_stream_out *)stream;

    AM_LOGV("out_get_sample_rate: stream:%p rate:%u", out, out->src_config.sample_rate);
    return out->src_config.sample_rate;
}

static int bus_out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    struct bus_stream_out *out = (struct bus_stream_out *)stream;
    AM_LOGI("sample_rate change from %d to %d", out->src_config.sample_rate, rate);
    out->src_config.sample_rate = rate;
    return 0;
}

static size_t bus_out_get_buffer_size(const struct audio_stream *stream)
{
   const struct bus_stream_out *out = (const struct bus_stream_out *)stream;
   size_t buffer_size = out->buf_frame_count * audio_stream_out_frame_size(&out->stream) / 2;
   AM_LOGI("buffer_size=%zu", buffer_size);
   return buffer_size;
}

static audio_channel_mask_t bus_out_get_channels(const struct audio_stream *stream)
{
   const struct bus_stream_out *out = (const struct bus_stream_out *)stream;
   AM_LOGV("out_get_channels: %x", out->src_config.channel_mask);
   return out->src_config.channel_mask;
}

static audio_format_t bus_out_get_format(const struct audio_stream *stream)
{
    const struct bus_stream_out *out = (const struct bus_stream_out *)stream;
    AM_LOGV("out_format: %d", out->src_config.format);
    return out->src_config.format;
}

static int bus_out_set_format(struct audio_stream *stream, audio_format_t format)
{
    struct bus_stream_out *out = (struct bus_stream_out *)stream;
    AM_LOGV("format change from %x to %x",out->src_config.format, format);
    out->src_config.format = format;
    return 0;
}

static int bus_out_dump(const struct audio_stream *stream, int fd)
{
    AM_LOGV("out_dump() stream:%p fd:%d", stream, fd);
    return 0;
}

static int bus_out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    AM_LOGD("stream:%p kv:%s", stream, kvpairs);
    return 0;
}

static char * bus_out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    AM_LOGD("stream:%p, keys:%s", stream, keys);
    return strdup("");
}

//TODO
static uint32_t bus_out_get_latency(const struct audio_stream_out *stream)
{
    struct bus_stream_out *out = (struct bus_stream_out *)stream;
    uint32_t latency = 0;

    if (out->standby) {
        latency = 0;
    } else if (out->playback_handler) {
        latency = 30;
    } else {
        latency = 30;
    }
    //AM_LOGD("stream:%p latency:%d", stream, latency);
    return latency;
}

static int bus_out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    AM_LOGI("out_set_volume: stream:%p Left:%f Right:%f", stream, left, right);
    return 0;
}

static int bus_stream_out_standby(struct audio_stream *stream)
{
    AM_LOGI("stream:%p", stream);
    struct bus_stream_out *out = (struct bus_stream_out *)stream;

    pthread_mutex_lock(&out->lock);
    struct playback_handler_base *playback_handler = out->playback_handler;
    if (!out->standby) {
        if (playback_handler != NULL) {
            playback_handler->close(playback_handler);
            delete_bus_playback_handler(playback_handler);
            out->playback_handler = NULL;
        }
        out->standby = true;
    }
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static ssize_t bus_out_write(struct audio_stream_out *stream, const void* buffer, size_t bytes)
{
    struct bus_stream_out *out = (struct bus_stream_out *)stream;
    struct playback_handler_base *playback_handler;

    if (!buffer) {
        return bytes;
    }

    pthread_mutex_lock(&out->lock);
    playback_handler = out->playback_handler;
    if (out->standby) {
        if (playback_handler == NULL) {
            playback_handler = create_bus_playback_handler(out->adev, &out->stream, &out->dest_config, out->bus_id);
            if (playback_handler != NULL) {
                playback_handler->open(playback_handler, out->written_all_frames);
                out->playback_handler = playback_handler;
            } else {
                AM_LOGE("new playback_handler fail, return!");
                pthread_mutex_unlock(&out->lock);
                return bytes;
            }
        }
        out->standby = false;
    }

    if (playback_handler) {
        playback_handler->write(playback_handler, buffer, bytes);
    } else {
        AM_LOGW("Warning, playback_handler =NULL!");
    }

    out->written_all_frames += bytes / out->frame_size;
    clock_gettime (CLOCK_MONOTONIC, &out->written_timestamp);
    pthread_mutex_unlock(&out->lock);
    return bytes;
}

static int bus_out_get_presentation_position(const struct audio_stream_out *stream, uint64_t *frames, struct timespec *timestamp)
{
    struct bus_stream_out *out = (struct bus_stream_out *) stream;
    int ret = -1;

    if (out->written_all_frames == 0) {
        *frames = out->written_all_frames;
        *timestamp = out->written_timestamp;
        return 0;
    }

    pthread_mutex_lock(&out->lock);
    if (out->playback_handler != NULL) {
        ret = out->playback_handler->get_presentation_position(out->playback_handler, frames, timestamp);
    }
    pthread_mutex_unlock(&out->lock);

    if (ret < 0) {
        *frames = out->written_all_frames;
        *timestamp = out->written_timestamp;
    }
    out->last_timestamp_report = *timestamp;
    out->last_frames_report = *frames;
    //AM_LOGD("ret:%d stream:%p written: %"PRIu64" presentation_frame: %"PRIu64"", ret, stream, out->written_all_frames, *frames);
    return 0;
}

static int bus_out_get_render_position(const struct audio_stream_out *stream, uint32_t *dsp_frames)
{
    struct bus_stream_out *out = (struct bus_stream_out *) stream;
    uint64_t presentation_frames = 0;
    struct timespec timestamp = {0};
    int ret = 0;

    ret = bus_out_get_presentation_position(stream, &presentation_frames, &timestamp);
    if (ret == 0) {
        *dsp_frames = (uint32_t)(presentation_frames & 0xffffffff);
    } else {
       *dsp_frames = out->written_all_frames;
    }
    AM_LOGV("stream:%p written: %"PRIu64" dsp_frames: %"PRIu32"", stream, out->written_all_frames, *dsp_frames);
    return 0;
}

static int bus_out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    AM_LOGI("stream:%p effect:%p", stream, effect);
    struct bus_stream_out *out = (struct bus_stream_out *) stream;
    struct aml_audio_device *adev = out->adev;
    int status = -EINVAL;

    pthread_mutex_lock (&adev->lock);
    status = aml_add_audio_effect(&adev->native_postprocess, effect, -1);
    pthread_mutex_unlock (&adev->lock);
    return status;

}

static int bus_out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    AM_LOGI("stream:%p effect:%p", stream, effect);
    struct bus_stream_out *out = (struct bus_stream_out *) stream;
    struct aml_audio_device *adev = out->adev;
    int status = -EINVAL;

    pthread_mutex_lock (&adev->lock);
    status = aml_remove_audio_effect(&adev->native_postprocess, effect, -1);
    pthread_mutex_unlock (&adev->lock);
    return status;
}

static int bus_out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    *timestamp = 0;
    ALOGV("out_get_next_write_timestamp: stream:%p,  time:%ld", stream, (long int)(*timestamp));
    return -EINVAL;
}

static size_t samples_per_milliseconds(size_t milliseconds,
                                       uint32_t sample_rate,
                                       size_t channel_count)
{
    return milliseconds * sample_rate * channel_count / 1000;
}


static int bus_stream_out_init(struct bus_stream_out *out,
                        audio_io_handle_t handle,
                        audio_devices_t devices,
                        audio_output_flags_t flags,
                        struct audio_config *config,
                        const char *address __unused)
{
    int ret = 0;
    int bus_id = -1;

    out->flags = flags;
    out->devices = devices;
    out->outId = handle;
    out->standby = true;
    /* check source config and set output stream source config */
    if (config->sample_rate == 0) {
        config->sample_rate = DEFAULT_STREAM_OUT_SAMPLE_RATE;
    }
    if (config->channel_mask == AUDIO_CHANNEL_NONE) {
        config->channel_mask = DEFAULT_STREAM_OUT_CHANNEL_MASK;
    }
    if (config->format == AUDIO_FORMAT_DEFAULT) {
        config->format = DEFAULT_STREAM_OUT_FORMAT;
    }
    out->src_config.sample_rate = config->sample_rate;
    out->src_config.channel_mask = config->channel_mask;
    out->src_config.format = config->format;
    out->frame_size = audio_bytes_per_sample(config->format) * audio_channel_count_from_out_mask(config->channel_mask);
    out->buf_frame_count = DEFAULT_OUTPUT_BUFFER_FRAME_COUNT;
    memcpy(&out->dest_config, &out->src_config, sizeof(struct audio_config));

    //parse bus id from address
    if (strlen(address) > strlen("bus") && strstr(address, "bus")) {
        strncpy(out->address, address, MAX_ADDRESS_LEN);
        bus_id = atoi(address + 3);
    } else {
        AM_LOGE("Un-support address:%s", address);
        ret = -EINVAL;
    }

    out->bus_id = bus_id;
    if (pthread_mutex_init(&out->lock, NULL) != 0) {
        AM_LOGE("pthread_mutex_init fail, errno:%s", strerror(errno));
        ret = -EINVAL;
    }

    AM_LOGI("out:%p ret:%d devices:0x%x rate:%u, chMask:%x, format:%d frame_size:%zu addr:%s bus_id:%d",
        out, ret, devices, config->sample_rate, config->channel_mask, config->format, out->frame_size, address, out->bus_id);
    return ret;
}

int adev_open_bus_output_stream(struct audio_hw_device *dev,
                                audio_io_handle_t handle,
                                audio_devices_t devices,
                                audio_output_flags_t flags,
                                struct audio_config *config,
                                struct audio_stream_out **stream_out,
                                const char *address)
{
    int ret = 0;
    *stream_out = NULL;
    struct bus_stream_out *out = (struct bus_stream_out *)calloc(1, sizeof(struct bus_stream_out));
    if (!out) {
        AM_LOGE("No memory!");
        return -ENOMEM;
    }

    out->stream.common.get_sample_rate = bus_out_get_sample_rate;
    out->stream.common.set_sample_rate = bus_out_set_sample_rate;
    out->stream.common.get_buffer_size = bus_out_get_buffer_size;
    out->stream.common.get_channels = bus_out_get_channels;
    out->stream.common.get_format = bus_out_get_format;
    out->stream.common.set_format = bus_out_set_format;
    out->stream.common.standby = bus_stream_out_standby;
    out->stream.common.dump = bus_out_dump;
    out->stream.common.set_parameters = bus_out_set_parameters;
    out->stream.common.get_parameters = bus_out_get_parameters;
    out->stream.common.add_audio_effect = bus_out_add_audio_effect;
    out->stream.common.remove_audio_effect = bus_out_remove_audio_effect;
    out->stream.get_latency = bus_out_get_latency;
    out->stream.set_volume = bus_out_set_volume;
    out->stream.write = bus_out_write;
    out->stream.get_render_position = bus_out_get_render_position;
    out->stream.get_next_write_timestamp = bus_out_get_next_write_timestamp;
    out->stream.get_presentation_position = bus_out_get_presentation_position;
    ret = bus_stream_out_init(out, handle, devices, flags, config, address);
    if (ret == 0) {
        out->adev = (struct aml_audio_device*)dev;
        *stream_out = &out->stream;
    } else {
        free(out);
        *stream_out = NULL;
        out = NULL;
    }

    return ret;
}

void adev_close_bus_output_stream(struct audio_hw_device *dev,
                                struct audio_stream_out *stream)
{
    if (!dev || !stream) {
        return;
    }

    struct bus_stream_out *out = (struct bus_stream_out *)stream;
    AM_LOGI("stream:%p busId:%d", stream, out->bus_id);
    if (!out->standby) {
        stream->common.standby((struct audio_stream*)stream);
    }
    free(stream);
}
