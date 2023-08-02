
#define LOG_TAG "audio_dummy_streamout"
#define LOG_NDEBUG 0

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cutils/log.h>

#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <system/audio.h>

#include "audio_hw.h"
#include "aml_audio_stream_base.h"

#define STUB_DEFAULT_SAMPLE_RATE   48000
#define STUB_DEFAULT_AUDIO_FORMAT  AUDIO_FORMAT_PCM_16_BIT

#define STUB_INPUT_BUFFER_MILLISECONDS  20
#define STUB_INPUT_DEFAULT_CHANNEL_MASK AUDIO_CHANNEL_IN_STEREO

#define STUB_OUTPUT_BUFFER_MILLISECONDS  10
#define STUB_OUTPUT_DEFAULT_CHANNEL_MASK AUDIO_CHANNEL_OUT_STEREO


struct dummy_stream_out {
    struct audio_stream_out stream;
    struct aml_streamout_base base;
    struct aml_audio_device *adev;
    int64_t last_write_time_us;
    uint32_t sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    size_t frame_count;
};


static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    const struct dummy_stream_out *out = (const struct dummy_stream_out *)stream;

    ALOGV("out_get_sample_rate: stream:%p rate:%u", out, out->sample_rate);
    return out->sample_rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    struct dummy_stream_out *out = (struct dummy_stream_out *)stream;

    ALOGV("out_set_sample_rate: %d", rate);
    out->sample_rate = rate;
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
   const struct dummy_stream_out *out = (const struct dummy_stream_out *)stream;
   size_t buffer_size = out->frame_count *
                        audio_stream_out_frame_size(&out->stream);

   ALOGV("out_get_buffer_size: %zu", buffer_size);
   return buffer_size;
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
   const struct dummy_stream_out *out = (const struct dummy_stream_out *)stream;

   ALOGV("out_get_channels: %x", out->channel_mask);
   return out->channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    const struct dummy_stream_out *out = (const struct dummy_stream_out *)stream;

    ALOGV("out_get_format: %d", out->format);
    return out->format;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    struct dummy_stream_out *out = (struct dummy_stream_out *)stream;

    ALOGV("out_set_format: %d", format);
    out->format = format;
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    ALOGV("out_standby() stream:%p", stream);
    // out->last_write_time_us = 0; unnecessary as a stale write time has same effect
    return 0;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    ALOGV("out_dump() stream:%p fd:%d", stream, fd);
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    ALOGV("out_set_parameters: stream:%p kv:%s", stream, kvpairs);
    return 0;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    ALOGV("out_get_parameters: stream:%p, keys:%s", stream, keys);
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    ALOGV("out_get_latency stream:%p", stream);
    return STUB_OUTPUT_BUFFER_MILLISECONDS;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    ALOGV("out_set_volume: stream:%p Left:%f Right:%f", stream, left, right);
    return 0;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    ALOGI("dummy_streamout_write() should happen!! bytes: %zu", bytes);

    if (!buffer) {
        return bytes;
    }

    /* XXX: fake timing for audio output */
    struct dummy_stream_out *out = (struct dummy_stream_out *)stream;
    struct timespec t = { .tv_sec = 0, .tv_nsec = 0 };
    clock_gettime(CLOCK_MONOTONIC, &t);
    const int64_t now = (t.tv_sec * 1000000000LL + t.tv_nsec) / 1000;
    const int64_t elapsed_time_since_last_write = now - out->last_write_time_us;
    int64_t sleep_time = bytes * 1000000LL / audio_stream_out_frame_size(stream) /
               out_get_sample_rate(&stream->common) - elapsed_time_since_last_write;
    if (sleep_time > 0) {
        usleep(sleep_time);
    } else {
        // we don't sleep when we exit standby (this is typical for a real alsa buffer).
        sleep_time = 0;
    }
    out->last_write_time_us = now + sleep_time;
    // last_write_time_us is an approximation of when the (simulated) alsa
    // buffer is believed completely full. The usleep above waits for more space
    // in the buffer, but by the end of the sleep the buffer is considered
    // topped-off.
    //
    // On the subsequent out_write(), we measure the elapsed time spent in
    // the mixer. This is subtracted from the sleep estimate based on frames,
    // thereby accounting for drain in the alsa buffer during mixing.
    // This is a crude approximation; we don't handle underruns precisely.
    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    *dsp_frames = 0;
    ALOGV("out_get_render_position: stream:%p dsp_frames: %p",stream, dsp_frames);
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    ALOGD("out_dummy_add_audio_effect: stream:%p effect:%p",stream, effect);
    struct dummy_stream_out *out = (struct dummy_stream_out *) stream;
    struct aml_audio_device *adev = out->adev;
    int status = -EINVAL;

    pthread_mutex_lock (&adev->lock);
    status = aml_add_audio_effect(&adev->native_postprocess, effect, -1);

    if (status >= 0 && adev->useSubMix) {
        void *process = &adev->native_postprocess;
        subMixingSetAudioPostprocess(adev, &process);
        ALOGI("%s, add audio postprocess: %p", __func__, process);
    }
    pthread_mutex_unlock (&adev->lock);
    return status;

}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    ALOGD("out_dummy_remove_audio_effect: stream:%p effect:%p",stream, effect);
    struct dummy_stream_out *out = (struct dummy_stream_out *) stream;
    struct aml_audio_device *adev = out->adev;
    int status = -EINVAL;

    pthread_mutex_lock (&adev->lock);
    status = aml_remove_audio_effect(&adev->native_postprocess, effect, -1);
    pthread_mutex_unlock (&adev->lock);
    return status;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
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

int adev_open_dummy_output_stream(struct audio_hw_device *dev,
                                    audio_io_handle_t handle,
                                    audio_devices_t devices,
                                    audio_output_flags_t flags,
                                    struct audio_config *config,
                                    struct audio_stream_out **stream_out,
                                    const char *address __unused)
{
    ALOGI("+%s() dev:%p io:%d, devices:0x%x, address:%s flags:0x%x", __func__, dev, handle, devices, address, flags);
    *stream_out = NULL;
    struct dummy_stream_out *out =
            (struct dummy_stream_out *)calloc(1, sizeof(struct dummy_stream_out));
    if (!out)
        return -ENOMEM;
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->sample_rate = config->sample_rate;
    if (out->sample_rate == 0)
        out->sample_rate = STUB_DEFAULT_SAMPLE_RATE;
    out->channel_mask = config->channel_mask;
    if (out->channel_mask == AUDIO_CHANNEL_NONE)
        out->channel_mask = STUB_OUTPUT_DEFAULT_CHANNEL_MASK;
    out->format = config->format;
    if (out->format == AUDIO_FORMAT_DEFAULT)
        out->format = STUB_DEFAULT_AUDIO_FORMAT;
    out->frame_count = samples_per_milliseconds(
                           STUB_OUTPUT_BUFFER_MILLISECONDS,
                           out->sample_rate, 1);

    out->adev = (struct aml_audio_device*)dev;
    ALOGI("-adev_open_dummy_output_stream: sample_rate: %u, channels: %x, format: %d,"
          " frames: %zu", out->sample_rate, out->channel_mask, out->format,
          out->frame_count);
    *stream_out = &out->stream;
    return 0;
}

void adev_close_dummy_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    ALOGV("adev_close_output_stream...");

    if (!dev || !stream) {
        return;
    }

    free(stream);
}

