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
#include "aml_volume_utils.h"

#define DEFAULT_STREAM_OUT_SAMPLE_RATE      48000
#define DEFAULT_STREAM_OUT_FORMAT          AUDIO_FORMAT_PCM_16_BIT
#define DEFAULT_STREAM_OUT_CHANNEL_MASK     AUDIO_CHANNEL_OUT_STEREO
#define DEFAULT_OUTPUT_BUFFER_FRAME_COUNT   1024
#define MAX_ADDRESS_LEN                     48

#define SPK_NAME_STR_PREFIX                   "spk_"
#define BUS_NAME_STR_PREFIX                   "bus_"
#define BUS_NAME_STR_PREFIX_LEN               ( 4 )

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

    /*volume from set_port_config*/
    float volume;

    int bus_id;
    uint32_t mux_channel_mask;
    int dest_bus_id[MAX_BUS_NUM];
    int dest_bus_count;
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
    struct bus_stream_out *out = (struct bus_stream_out *)stream;

    pthread_mutex_lock(&out->lock);
    dprintf(fd, "\tbus_steam_out_dump:\n");
    dprintf(fd, "\t\taddress: %s\n", out->address);
    dprintf(fd, "\t\tdevices: %x\n", out->devices);
    dprintf(fd, "\t\tsample_rate: %u\n", bus_out_get_sample_rate(stream));
    dprintf(fd, "\t\tbuffer_size: %zu\n", bus_out_get_buffer_size(stream));
    dprintf(fd, "\t\tchannel_mask: %08x\n", bus_out_get_channels(stream));
    dprintf(fd, "\t\tformat: %x\n", bus_out_get_format(stream));
    dprintf(fd, "\t\tvolume: %f\n", out->volume);
    pthread_mutex_unlock(&out->lock);
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
    AM_LOGD("+stream:%p", stream);
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
        AM_LOGD("-stream:%p playback_handler:%p", stream, playback_handler);
    }
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static ssize_t bus_out_write(struct audio_stream_out *stream, const void* buffer, size_t bytes)
{
    struct bus_stream_out *out = (struct bus_stream_out *)stream;
    struct playback_handler_base *playback_handler;
    size_t sample_size = audio_bytes_per_sample(out->src_config.format);

    if (!buffer) {
        return bytes;
    }

    pthread_mutex_lock(&out->lock);
    playback_handler = out->playback_handler;
    if (out->standby) {
        if (playback_handler == NULL) {
            playback_handler = create_bus_playback_handler(out->adev, &out->stream, &out->dest_config, out->bus_id, out->mux_channel_mask);
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

    apply_volume(out->volume, (void *)buffer, sample_size, bytes);

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

    out->mux_channel_mask = 0;
    out->bus_id = bus_id;
    out->volume = 1.0;
    if (pthread_mutex_init(&out->lock, NULL) != 0) {
        AM_LOGE("pthread_mutex_init fail, errno:%s", strerror(errno));
        ret = -EINVAL;
    }

    AM_LOGI("out:%p ret:%d devices:0x%x rate:%u, chMask:%x, format:%d frame_size:%zu addr:%s bus_id:%d",
        out, ret, devices, config->sample_rate, config->channel_mask, config->format, out->frame_size, address, out->bus_id);
    return ret;
}

uint32_t get_channel_mask_from_bus_group(int* bus_array, int count)
{
    uint32_t mask = 0;
    uint32_t table[MAX_DEVICE_OUT_CHANNEL_COUNT];
    const uint32_t out_channel_num = 2;

    for (int i = 0; i < count; i++) {
        uint32_t temp_mask = 0;
        set_channel_table_from_bus_id(table, bus_array[i], out_channel_num);
        temp_mask = get_channel_mask_from_table(table, MAX_DEVICE_OUT_CHANNEL_COUNT);
        mask |= temp_mask;
    }
    return mask;
}

int check_bus_stream_out_map_change(struct bus_stream_out *out, int bus_array[], int count)
{
    if (!out || !count) {
        AM_LOGE("Invalid out:%p bus_count:%d", out, count);
        for (int i = 0; i < count; i++) {
            AM_LOGI("bus[%d]=%d", i, bus_array[i]);
        }
        return -EINVAL;
    }

    pthread_mutex_lock(&out->lock);

    bool need_reset = false;
    uint32_t new_channel_mask = get_channel_mask_from_bus_group(bus_array, count);
    uint32_t cur_channel_mask = out->mux_channel_mask;
    if (new_channel_mask != cur_channel_mask) {
        out->mux_channel_mask = new_channel_mask;
        for (int i = 0; i < MAX_BUS_NUM; i++) {
            if (i < count) {
                out->dest_bus_id[i] = bus_array[i];
                out->dest_bus_count = count;
            } else {
                out->dest_bus_id[i] = 0;
            }
        }
        need_reset = true;
    }
    pthread_mutex_unlock(&out->lock);

    if (need_reset) {
        bus_stream_out_standby((struct audio_stream *)out);
    }

    AM_LOGI("need_reset=%d, old_channel_mask: %x new_channel_mask:%x", need_reset, cur_channel_mask, new_channel_mask);
    return 0;
}

struct audio_hw_device;
struct aml_audio_device;

void adev_add_bus_stream_out(struct audio_hw_device *adev, struct audio_stream_out *out)
{
    if (!adev || !out) {
        AM_LOGE("Invalid adev:%p out:%p", adev, out);
        return;
    }

    struct aml_audio_device* aml_dev = (struct aml_audio_device*)adev;
    aml_dev->mBus_stream_outs[aml_dev->bus_stream_count++] = out;

    AM_LOGI("out:%p count:%d", out, aml_dev->bus_stream_count);
}

void adev_remove_bus_stream_out(struct audio_hw_device *adev, struct audio_stream_out *out)
{
    if (!adev || !out) {
        AM_LOGE("Invalid adev:%p out:%p", adev, out);
        return;
    }

    struct aml_audio_device* aml_dev = (struct aml_audio_device*)adev;
    int count = aml_dev->bus_stream_count;
    bool found = false;

    pthread_mutex_lock(&aml_dev->lock);

    for (int i = 0; i < count; i++) {
        if (found) {
            aml_dev->mBus_stream_outs[i - 1] = aml_dev->mBus_stream_outs[i];
        }

        if (aml_dev->mBus_stream_outs[i] == out) {
            aml_dev->mBus_stream_outs[i] = NULL;
            found = true;
            aml_dev->bus_stream_count--;
        }
    }
    pthread_mutex_unlock(&aml_dev->lock);
    AM_LOGI("found:%d out:%p count:%d", true, out, aml_dev->bus_stream_count);
}

struct bus_stream_out *adev_get_bus_stream_out(struct audio_hw_device *adev, int busId)
{
    if (!adev || (busId < 0)) {
        AM_LOGE("Invalid adev:%p busId:%d", adev, busId);
        return NULL;
    }

    struct aml_audio_device* aml_dev = (struct aml_audio_device*)adev;
    struct bus_stream_out *out = NULL;
    int count = aml_dev->bus_stream_count;

    pthread_mutex_lock(&aml_dev->lock);

    for (int i = 0; i < count; i++) {
        struct bus_stream_out *temp = (struct bus_stream_out *)aml_dev->mBus_stream_outs[i];
        if (temp->bus_id == busId) {
            out = temp;
            break;
        }
    }
    pthread_mutex_unlock(&aml_dev->lock);
    return out;
}

int parser_bus_src(char *cstr, int *source_bus)
{
    if (!cstr || (strlen(cstr) < BUS_NAME_STR_PREFIX_LEN + 1)) {
        AM_LOGI("Invalid cstr:%s", cstr);
        return -EINVAL;
    }

    int bus_id = atoi(cstr + strlen("bus_"));
    *source_bus = bus_id;
    return 0;
}

int parser_bus_dest(char *cstr, int *table, int* count)
{
    if (!cstr || (strlen(cstr) < BUS_NAME_STR_PREFIX_LEN + 1)) {
        AM_LOGI("Invalid cstr:%s", cstr);
        return -EINVAL;
    }

    char *tmp;
    char *off = strtok_r (cstr, ",", &tmp);
    int num = 0;
    while (off != NULL) {
        int bus_id = atoi(off + BUS_NAME_STR_PREFIX_LEN);
        table[num++] = bus_id;
        off = strtok_r (NULL, ",", &tmp);
    }
    *count = num;
    return 0;
}

/*
    Parameter command line style:
    1) customize bus switch parameters
       "switch_src=bus_1;to_dest_spk=bus_2"
    //start mirroring
    2) mirroring_src=bus_1000;mirroring_dest=bus_10,bus_20
    //stop mirroring
    3) mirroring_src=bus_1000;mirroring=off
*/
int adev_set_bus_parameters(struct audio_hw_device *dev, struct str_parms *parms)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int ret = -1, val = 0;
    char value[64] = {'\0'};
    char *param = NULL;
    const int MAX_DEST_BUS = 8;
    bool parse_src = false;
    bool parse_dest = false;
    int src_bus = -1;
    int dest_bus_table[MAX_BUS_NUM] = {-1};
    int dest_bus_count = 0;

    /* parse customize bus switch parameters */
    ret = str_parms_get_str(parms, "switch_src", value, sizeof(value));
    if (ret >= 0) {
        ret = parser_bus_src(value, &src_bus);
        parse_src = (ret == 0 ? true : false);

        if (parse_src) {
            ret = str_parms_get_str(parms, "to_dest_spk", value, sizeof(value));
            if (ret >= 0) {
                ret = parser_bus_dest(value, dest_bus_table, &dest_bus_count);
                parse_dest = (ret == 0 ? true : false);
            }
        }
        goto do_switch_map;
    }

    /* parse mirror mapping */
    ret = str_parms_get_str(parms, "mirroring_src", value, sizeof(value));
    if (ret >= 0) {
        ret = parser_bus_src(value, &src_bus);
        parse_src = (ret == 0 ? true : false);

        if (parse_src) {
            //parse mirroring_dest
            ret = str_parms_get_str(parms, "mirroring_dest", value, sizeof(value));
            if (ret >= 0) {
                ret = parser_bus_dest(value, dest_bus_table, &dest_bus_count);
                parse_dest = (ret == 0 ? true : false);
            }

            //parse mirroring_off
            ret = str_parms_get_str(parms, "mirroring", value, sizeof(value));
            if (ret >= 0) {
                if (strncmp(value, "off", 3) == 0) {
                    dest_bus_table[0] = src_bus;
                    dest_bus_count = 1;
                    parse_dest = true;
                    AM_LOGW("parse mirroring_off OK! mirroring_src=bus_%d;mirroring=%s", src_bus, value);
                } else {
                    AM_LOGW("parse mirroring_off fail! mirroring_src=bus_%d;mirroring=%s", src_bus, value);
                }
            }
        }
        goto do_switch_map;
    }

do_switch_map:
    if (parse_src && parse_dest) {
        struct bus_stream_out *streamOut = adev_get_bus_stream_out(dev, src_bus);
        check_bus_stream_out_map_change(streamOut, dest_bus_table, dest_bus_count);
    }

    //other common parameter support for bus_stream_out
    return ret;
}

int adev_set_audio_port_config_for_bus(struct audio_hw_device *dev, const struct audio_port_config *config)
{
    int bus_id = -1;
    const char *bus_addr = config->ext.device.address;
    struct bus_stream_out *out = NULL;
    int ret = 0;

    if (strstr(bus_addr, "bus")) {
        bus_id = atoi(bus_addr + strlen("bus"));
        out = adev_get_bus_stream_out(dev, bus_id);
    }

    if (out) {
        pthread_mutex_lock(&out->lock);
        out->volume =  DbToAmpl(config->gain.values[0] / 100.0);
        pthread_mutex_unlock(&out->lock);
        AM_LOGI("set volume: %f for %s",out->volume, bus_addr);
    } else {
        AM_LOGE("Can't find bus_stream_out of bus_addr:%s", bus_addr);
        ret = -EINVAL;
    }
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
        adev_add_bus_stream_out(dev, &out->stream);
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

    adev_remove_bus_stream_out(dev, stream);
}
