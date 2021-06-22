/*
 * Copyright 2021 The Android Open Source Project
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

#define LOG_TAG "a2dp_hw"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <cutils/log.h>

#include "a2dp_hal.h"
#include "audio_hw_utils.h"
#include "aml_audio_timer.h"

#define CTRL_CHAN_RETRY_COUNT 3
#define SOCK_SEND_TIMEOUT_MS 2000 /* Timeout for sending */
#define SOCK_RECV_TIMEOUT_MS 5000 /* Timeout for receiving */

/* set WRITE_POLL_MS to 0 for blocking sockets,
 * nonzero for polled non-blocking sockets
 */
#define WRITE_POLL_MS 3

#define UNUSED_ATTR __attribute__((unused))

// Re-run |fn| system call until the system call doesn't cause EINTR.
#define OSI_NO_INTR(fn) \
  do {                  \
  } while ((fn) == -1 && errno == EINTR)


static void a2dp_open_ctrl_path(void *a2dp_hal);

#define CASE_RETURN_STR(const) \
      case const:                  \
        return #const;

const char *audio_a2dp_hw_dump_ctrl_event(tA2DP_CTRL_CMD event) {
  switch (event) {
    CASE_RETURN_STR(A2DP_CTRL_CMD_NONE)
    CASE_RETURN_STR(A2DP_CTRL_CMD_CHECK_READY)
    CASE_RETURN_STR(A2DP_CTRL_CMD_START)
    CASE_RETURN_STR(A2DP_CTRL_CMD_STOP)
    CASE_RETURN_STR(A2DP_CTRL_CMD_SUSPEND)
    CASE_RETURN_STR(A2DP_CTRL_GET_INPUT_AUDIO_CONFIG)
    CASE_RETURN_STR(A2DP_CTRL_GET_OUTPUT_AUDIO_CONFIG)
    CASE_RETURN_STR(A2DP_CTRL_SET_OUTPUT_AUDIO_CONFIG)
    CASE_RETURN_STR(A2DP_CTRL_CMD_OFFLOAD_START)
    CASE_RETURN_STR(A2DP_CTRL_GET_PRESENTATION_POSITION)
  }

  return "UNKNOWN A2DP_CTRL_CMD";
}

inline const char *a2dpStatus2String(a2dp_state_t type)
{
    ENUM_TYPE_TO_STR_START("AUDIO_A2DP_STATE_");
    ENUM_TYPE_TO_STR(AUDIO_A2DP_STATE_STARTING)
    ENUM_TYPE_TO_STR(AUDIO_A2DP_STATE_STARTED)
    ENUM_TYPE_TO_STR(AUDIO_A2DP_STATE_STOPPING)
    ENUM_TYPE_TO_STR(AUDIO_A2DP_STATE_STOPPED)
    ENUM_TYPE_TO_STR(AUDIO_A2DP_STATE_SUSPENDED)
    ENUM_TYPE_TO_STR(AUDIO_A2DP_STATE_STANDBY)
    ENUM_TYPE_TO_STR_END
}

static inline  const char *btAvSampleRate2String(btav_a2dp_codec_sample_rate_t type)
{
    ENUM_TYPE_TO_STR_START("BTAV_A2DP_CODEC_SAMPLE_RATE_");
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_SAMPLE_RATE_44100)
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_SAMPLE_RATE_48000)
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_SAMPLE_RATE_88200)
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_SAMPLE_RATE_96000)
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_SAMPLE_RATE_176400)
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_SAMPLE_RATE_192000)
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_SAMPLE_RATE_16000)
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_SAMPLE_RATE_24000)
    ENUM_TYPE_TO_STR_END
}

static inline const char* btAvBitPreSample2String(btav_a2dp_codec_bits_per_sample_t type)
{
    ENUM_TYPE_TO_STR_START("BTAV_A2DP_CODEC_BITS_PER_SAMPLE_");
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16)
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24)
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32)
    ENUM_TYPE_TO_STR_END
}

static inline const char *btAvChannel2String(btav_a2dp_codec_channel_mode_t type)
{
    ENUM_TYPE_TO_STR_START("BTAV_A2DP_CODEC_CHANNEL_MODE_");
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_CHANNEL_MODE_MONO)
    ENUM_TYPE_TO_STR(BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO)
    ENUM_TYPE_TO_STR_END
}

/**
 * connect to peer named "name" on fd
 * returns same fd or -1 on error.
 * fd is not closed on error. that's your job.
 *
 * Used by AndroidSocketImpl
 */
int osi_socket_local_client_connect(int fd, const char* name) {
    struct sockaddr_un addr;
    socklen_t alen;
    int err;
    size_t namelen = strlen(name);

    memset(&addr, 0, sizeof(struct sockaddr_un));
    if ((namelen + 1) > sizeof(addr.sun_path)) {
        return -1;
    }
    addr.sun_path[0] = 0;
    memcpy(addr.sun_path + 1, name, namelen);
    addr.sun_family = AF_LOCAL;
    alen = namelen + offsetof(struct sockaddr_un, sun_path) + 1;

    OSI_NO_INTR(err = connect(fd, (struct sockaddr*)&addr, alen));
    if (err < 0) {
        return -1;
    }
    return fd;
}

/*****************************************************************************
 *
 *   bluedroid stack adaptation
 *
 ****************************************************************************/
static int skt_connect(const char *path, size_t buffer_sz) {
    int ret;
    int skt_fd;
    int len;

    skt_fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (osi_socket_local_client_connect(skt_fd, path) < 0) {
        AM_LOGE("failed to connect (%s)", strerror(errno));
        close(skt_fd);
        return -1;
    }

    len = buffer_sz;
    ret = setsockopt(skt_fd, SOL_SOCKET, SO_SNDBUF, (char*)&len, (int)sizeof(len));
    if (ret < 0)
        AM_LOGE("setsockopt failed (%s)", strerror(errno));

    ret = setsockopt(skt_fd, SOL_SOCKET, SO_RCVBUF, (char*)&len, (int)sizeof(len));
    if (ret < 0)
        AM_LOGE("setsockopt failed (%s)", strerror(errno));

    /* Socket send/receive timeout value */
    struct timeval tv;
    tv.tv_sec = SOCK_SEND_TIMEOUT_MS / 1000;
    tv.tv_usec = (SOCK_SEND_TIMEOUT_MS % 1000) * 1000;
    ret = setsockopt(skt_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (ret < 0)
        AM_LOGE("setsockopt failed (%s)", strerror(errno));

    tv.tv_sec = SOCK_RECV_TIMEOUT_MS / 1000;
    tv.tv_usec = (SOCK_RECV_TIMEOUT_MS % 1000) * 1000;
    ret = setsockopt(skt_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (ret < 0)
        AM_LOGE("setsockopt failed (%s)", strerror(errno));

    AM_LOGD("fd:%d (buf_size:%zu, path:%s)", skt_fd, buffer_sz, path);
    return skt_fd;
}

static int skt_disconnect(int fd) {
    AM_LOGD("fd %d", fd);
    if (fd != AUDIO_SKT_DISCONNECTED) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    return 0;
}

int skt_write(void *hal, const void *buffer, size_t bytes) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    ssize_t sent;

    if (WRITE_POLL_MS == 0) {
        // do not poll, use blocking send
        OSI_NO_INTR(sent = send(a2dp_hal->audio_fd, buffer, bytes, MSG_NOSIGNAL));
        if (sent == -1) {
            AM_LOGW("fd:%d, state:%s, write failed with error(%s)", a2dp_hal->audio_fd,
                        a2dpStatus2String(a2dp_hal->state), strerror(errno));
            skt_disconnect(a2dp_hal->audio_fd);
            a2dp_hal->audio_fd = AUDIO_SKT_DISCONNECTED;
            if (a2dp_hal->state != AUDIO_A2DP_STATE_SUSPENDED && a2dp_hal->state != AUDIO_A2DP_STATE_STOPPING) {
                a2dp_hal->state = AUDIO_A2DP_STATE_STOPPED;
            }
        }
        return (int)sent;
    }

    // use non-blocking send, poll
    int ms_timeout = SOCK_SEND_TIMEOUT_MS;
    size_t count = 0;
    while (count < bytes) {
        OSI_NO_INTR(sent = send(a2dp_hal->audio_fd, buffer, bytes - count, MSG_NOSIGNAL | MSG_DONTWAIT));
        if (sent == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                AM_LOGE("fd:%d, write failed with error(%s)", a2dp_hal->audio_fd, strerror(errno));
                return -1;
            }
            if (ms_timeout >= WRITE_POLL_MS) {
                usleep(WRITE_POLL_MS * 1000);
                ms_timeout -= WRITE_POLL_MS;
                continue;
            }
            AM_LOGW("write timeout exceeded, sent %zu bytes", count);
            return -1;
        }
        count += sent;
        buffer = (const uint8_t*)buffer + sent;
    }
    return (int)count;
}

static int a2dp_ctrl_receive(void *hal, void* buffer, size_t length) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;

    ssize_t ret;
    int i;

    for (i = 0;; i++) {
        OSI_NO_INTR(ret = recv(a2dp_hal->ctrl_fd, buffer, length, MSG_NOSIGNAL));
        if (ret > 0)
            break;
        if (ret == 0) {
            AM_LOGE("receive control data failed: peer closed");
            break;
        }
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            AM_LOGE("receive control data failed: error(%s)", strerror(errno));
            break;
        }
        if (i == (CTRL_CHAN_RETRY_COUNT - 1)) {
            AM_LOGE("receive control data failed: max retry count");
            break;
        }
        AM_LOGD("receive control data failed (%s), retrying", strerror(errno));
    }
    if (ret <= 0) {
        skt_disconnect(a2dp_hal->ctrl_fd);
        a2dp_hal->ctrl_fd = AUDIO_SKT_DISCONNECTED;
    }
    return ret;
}

/* Sends control info for stream |out|. The data to send is stored in
 * |buffer| and has size |length|.
 * On success, returns the number of octets sent, otherwise -1.
 */
static int a2dp_ctrl_send(void *hal, const void* buffer, size_t length) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    ssize_t sent;
    size_t remaining = length;
    int i;

    if (length == 0)
        return 0;  // Nothing to do

    for (i = 0;; i++) {
        OSI_NO_INTR(sent = send(a2dp_hal->ctrl_fd, buffer, remaining, MSG_NOSIGNAL));
        if (sent == (ssize_t)(remaining)) {
            remaining = 0;
            break;
        }
        if (sent > 0) {
            buffer = ((const char*)(buffer) + sent);
            remaining -= sent;
            continue;
        }
        if (sent < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                AM_LOGE("send control data failed: error(%s)", strerror(errno));
                break;
            }
            AM_LOGD("send control data failed (%s), retrying", strerror(errno));
        }
        if (i >= (CTRL_CHAN_RETRY_COUNT - 1)) {
            AM_LOGE("send control data failed: max retry count");
            break;
        }
    }
    if (remaining > 0) {
        skt_disconnect(a2dp_hal->ctrl_fd);
        a2dp_hal->ctrl_fd = AUDIO_SKT_DISCONNECTED;
        return -1;
    }
    return length;
}

static int a2dp_command(void *hal, tA2DP_CTRL_CMD cmd) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    char ack;

    if (a2dp_hal->ctrl_fd == AUDIO_SKT_DISCONNECTED) {
        AM_LOGD("starting up or recovering from previous error: command=%s",
                audio_a2dp_hw_dump_ctrl_event(cmd));
        a2dp_open_ctrl_path(a2dp_hal);
        if (a2dp_hal->ctrl_fd == AUDIO_SKT_DISCONNECTED) {
            AM_LOGE("failure to open ctrl path: command=%s",
            audio_a2dp_hw_dump_ctrl_event(cmd));
            return -1;
        }
    }

    /* send command */
    ssize_t sent;
    OSI_NO_INTR(sent = send(a2dp_hal->ctrl_fd, &cmd, 1, MSG_NOSIGNAL));
    if (sent == -1) {
        AM_LOGE("cmd failed (%s): command=%s", strerror(errno),
                audio_a2dp_hw_dump_ctrl_event(cmd));
        skt_disconnect(a2dp_hal->ctrl_fd);
        a2dp_hal->ctrl_fd = AUDIO_SKT_DISCONNECTED;
        return -1;
    }

    /* wait for ack byte */
    if (a2dp_ctrl_receive(a2dp_hal, &ack, 1) < 0) {
        AM_LOGE("A2DP COMMAND %s: no ACK", audio_a2dp_hw_dump_ctrl_event(cmd));
        return -1;
    }

    if (ack == A2DP_CTRL_ACK_INCALL_FAILURE) {
        AM_LOGE("A2DP COMMAND %s error %d", audio_a2dp_hw_dump_ctrl_event(cmd), ack);
        return ack;
    }
    if (ack != A2DP_CTRL_ACK_SUCCESS) {
        AM_LOGE("A2DP COMMAND %s error %d", audio_a2dp_hw_dump_ctrl_event(cmd), ack);
        return -1;
    }
    return A2DP_CTRL_ACK_SUCCESS;
}

static int check_a2dp_ready(void *hal) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    if (a2dp_command(a2dp_hal, A2DP_CTRL_CMD_CHECK_READY) < 0) {
        AM_LOGE("check a2dp ready failed");
        return -1;
    }
    return 0;
}

static void a2dp_open_ctrl_path(void *hal) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    int i;

    if (a2dp_hal->ctrl_fd != AUDIO_SKT_DISCONNECTED)
        return;  // already connected

    /* retry logic to catch any timing variations on control channel */
    for (i = 0; i < CTRL_CHAN_RETRY_COUNT; i++) {
        /* connect control channel if not already connected */
        if ((a2dp_hal->ctrl_fd = skt_connect(
                A2DP_CTRL_PATH, AUDIO_STREAM_CONTROL_OUTPUT_BUFFER_SZ)) >= 0) {
            /* success, now check if stack is ready */
            if (check_a2dp_ready(a2dp_hal) == 0)
                break;
            AM_LOGE("error : a2dp not ready, wait 250 ms and retry");
            usleep(250000);
            skt_disconnect(a2dp_hal->ctrl_fd);
            a2dp_hal->ctrl_fd = AUDIO_SKT_DISCONNECTED;
        }
        /* ctrl channel not ready, wait a bit */
        usleep(250000);
    }
}

size_t a2dp_hw_buffer_size(btav_a2dp_codec_config_t * config) {
    size_t buffer_sz = AUDIO_STREAM_OUTPUT_BUFFER_SZ;  // Default value
    const uint64_t time_period_ms = 40;                // Conservative 20ms
    uint32_t sample_rate;
    uint32_t bytes_per_sample;
    uint32_t number_of_channels;

    // Check the codec config sample rate
    switch (config->sample_rate) {
        case BTAV_A2DP_CODEC_SAMPLE_RATE_44100:
            sample_rate = 44100;
            break;
        case BTAV_A2DP_CODEC_SAMPLE_RATE_48000:
          sample_rate = 48000;
            break;
        case BTAV_A2DP_CODEC_SAMPLE_RATE_88200:
            sample_rate = 88200;
            break;
        case BTAV_A2DP_CODEC_SAMPLE_RATE_96000:
            sample_rate = 96000;
            break;
        case BTAV_A2DP_CODEC_SAMPLE_RATE_176400:
            sample_rate = 176400;
            break;
        case BTAV_A2DP_CODEC_SAMPLE_RATE_192000:
            sample_rate = 192000;
            break;
        case BTAV_A2DP_CODEC_SAMPLE_RATE_NONE:
        default:
            AM_LOGE("Invalid sample rate: 0x%x", config->sample_rate);
            return buffer_sz;
    }

    // Check the codec config bits per sample
    switch (config->bits_per_sample) {
        case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16:
            bytes_per_sample = 2;
            break;
        case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24:
            bytes_per_sample = 3;
            break;
        case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32:
            bytes_per_sample = 4;
            break;
        case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE:
        default:
            AM_LOGE("Invalid bits per sample: 0x%x", config->bits_per_sample);
            return buffer_sz;
    }

    // Check the codec config channel mode
    switch (config->channel_mode) {
        case BTAV_A2DP_CODEC_CHANNEL_MODE_MONO:
            number_of_channels = 1;
            break;
        case BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO:
            number_of_channels = 2;
            break;
        case BTAV_A2DP_CODEC_CHANNEL_MODE_NONE:
        default:
            AM_LOGE("Invalid channel mode: 0x%x", config->channel_mode);
            return buffer_sz;
    }

    /*
    * The buffer size is computed by using the following formula:
    *
    * AUDIO_STREAM_OUTPUT_BUFFER_SIZE =
    *    (TIME_PERIOD_MS * AUDIO_STREAM_OUTPUT_BUFFER_PERIODS *
    *     SAMPLE_RATE_HZ * NUMBER_OF_CHANNELS * (BITS_PER_SAMPLE / 8)) / 1000
    *
    * AUDIO_STREAM_OUTPUT_BUFFER_PERIODS controls how the socket buffer is
    * divided for AudioFlinger data delivery. The AudioFlinger mixer delivers
    * data in chunks of
    * (AUDIO_STREAM_OUTPUT_BUFFER_SIZE / AUDIO_STREAM_OUTPUT_BUFFER_PERIODS) .
    * If the number of periods is 2, the socket buffer represents "double
    * buffering" of the AudioFlinger mixer buffer.
    *
    * Furthermore, the AudioFlinger expects the buffer size to be a multiple
    * of 16 frames.
    */
    const size_t divisor = (AUDIO_STREAM_OUTPUT_BUFFER_PERIODS * 16 *
                number_of_channels * bytes_per_sample);
    buffer_sz = (time_period_ms * AUDIO_STREAM_OUTPUT_BUFFER_PERIODS *
            sample_rate * number_of_channels * bytes_per_sample) / 1000;
    // Adjust the buffer size so it can be divided by the divisor
    const size_t remainder = buffer_sz % divisor;
    if (remainder != 0) {
        buffer_sz += divisor - remainder;
    }
    return buffer_sz;
}

int a2dp_get_output_audio_config(void *hal, btav_a2dp_codec_config_t *codec_config,
        btav_a2dp_codec_config_t *codec_capability) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    if (a2dp_command(a2dp_hal, A2DP_CTRL_GET_OUTPUT_AUDIO_CONFIG) < 0) {
        AM_LOGE("get a2dp output audio config failed");
        return -1;
    }

    // Receive the current codec config
    if (a2dp_ctrl_receive(a2dp_hal, &codec_config->sample_rate,
            sizeof(btav_a2dp_codec_sample_rate_t)) < 0) {
        return -1;
    }
    if (a2dp_ctrl_receive(a2dp_hal, &codec_config->bits_per_sample,
            sizeof(btav_a2dp_codec_bits_per_sample_t)) < 0) {
        return -1;
    }
    if (a2dp_ctrl_receive(a2dp_hal, &codec_config->channel_mode,
            sizeof(btav_a2dp_codec_channel_mode_t)) < 0) {
        return -1;
    }

    // Receive the current codec capability
    if (a2dp_ctrl_receive(a2dp_hal, &codec_capability->sample_rate,
            sizeof(btav_a2dp_codec_sample_rate_t)) < 0) {
        return -1;
    }
    if (a2dp_ctrl_receive(a2dp_hal, &codec_capability->bits_per_sample,
            sizeof(btav_a2dp_codec_bits_per_sample_t)) < 0) {
        return -1;
    }
    if (a2dp_ctrl_receive(a2dp_hal, &codec_capability->channel_mode,
            sizeof(btav_a2dp_codec_channel_mode_t)) < 0) {
        return -1;
    }
    return 0;
}

int a2dp_read_output_audio_config(void *hal, btav_a2dp_codec_config_t *codec_capability) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    btav_a2dp_codec_config_t codec_config;
    unsigned int rate;
    audio_format_t format;
    audio_channel_mask_t channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    int ret = 0;

    ret = a2dp_get_output_audio_config(a2dp_hal, &codec_config, codec_capability);
    R_CHECK_RET(ret, "a2dp_get_output_audio_config fail");

    // Check the codec config sample rate
    switch (codec_config.sample_rate) {
        case BTAV_A2DP_CODEC_SAMPLE_RATE_44100:
            rate = 44100;
            break;
        case BTAV_A2DP_CODEC_SAMPLE_RATE_48000:
            rate = 48000;
            break;
        case BTAV_A2DP_CODEC_SAMPLE_RATE_88200:
            rate = 88200;
            break;
        case BTAV_A2DP_CODEC_SAMPLE_RATE_96000:
            rate = 96000;
            break;
        case BTAV_A2DP_CODEC_SAMPLE_RATE_176400:
            rate = 176400;
            break;
        case BTAV_A2DP_CODEC_SAMPLE_RATE_192000:
            rate = 192000;
            break;
        case BTAV_A2DP_CODEC_SAMPLE_RATE_NONE:
        default:
            AM_LOGE("Invalid sample rate: 0x%x", codec_config.sample_rate);
            return -1;
    }

    // Check the codec config bits per sample
    switch (codec_config.bits_per_sample) {
        case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16:
            format = AUDIO_FORMAT_PCM_16_BIT;
            break;
        case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24:
            format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
            break;
        case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32:
            format = AUDIO_FORMAT_PCM_32_BIT;
            break;
        case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE:
        default:
            AM_LOGE("Invalid bits per sample: 0x%x", codec_config.bits_per_sample);
            return -1;
    }

    // Check the codec config channel mode
    switch (codec_config.channel_mode) {
        case BTAV_A2DP_CODEC_CHANNEL_MODE_MONO:
            channel_mask = AUDIO_CHANNEL_OUT_MONO;
            break;
        case BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO:
            channel_mask = AUDIO_CHANNEL_OUT_STEREO;
            break;
        case BTAV_A2DP_CODEC_CHANNEL_MODE_NONE:
        default:
            AM_LOGE("Invalid channel mode: 0x%x", codec_config.channel_mode);
            return -1;
    }
    a2dp_hal->a2dp_config.channel_mask = channel_mask;
    a2dp_hal->a2dp_config.sample_rate = rate;
    a2dp_hal->a2dp_config.format = format;
    a2dp_hal->buffer_sz = a2dp_hw_buffer_size(&codec_config);
    AM_LOGD("config: rate=%#x bits=%#x chn=%#x",
        codec_config.sample_rate, codec_config.bits_per_sample, codec_config.channel_mode);
    AM_LOGD("capability: rate=%#x bits=%#x chn=%#x",
        codec_capability->sample_rate, codec_capability->bits_per_sample, codec_capability->channel_mode);
    return 0;
}

int a2dp_write_output_audio_config(void *hal, btav_a2dp_codec_config_t *codec_capability) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    btav_a2dp_codec_config_t config;

    if (a2dp_command(a2dp_hal, A2DP_CTRL_SET_OUTPUT_AUDIO_CONFIG) < 0) {
        AM_LOGE("set a2dp output audio config failed");
        return -1;
    }

    if (codec_capability->sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_48000)
        config.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
    else
        config.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_44100;

    if (codec_capability->bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16)
        config.bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16;
    else
        config.bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32;

    if (codec_capability->channel_mode & BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO)
        config.channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
    else
        config.channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;

    // Send the current codec config that has been selected by us
    if (a2dp_ctrl_send(a2dp_hal, &config.sample_rate,
            sizeof(btav_a2dp_codec_sample_rate_t)) < 0)
        return -1;
    if (a2dp_ctrl_send(a2dp_hal, &config.bits_per_sample,
            sizeof(btav_a2dp_codec_bits_per_sample_t)) < 0)
        return -1;
    if (a2dp_ctrl_send(a2dp_hal, &config.channel_mode,
            sizeof(btav_a2dp_codec_channel_mode_t)) < 0)
        return -1;

    AM_LOGD("config: rate=%#x bits=%#x chn=%#x",
        config.sample_rate, config.bits_per_sample, config.channel_mode);
    return 0;
}

static int a2dp_get_presentation_position_cmd(void *hal,
        uint64_t* bytes, uint16_t* delay, struct timespec* timestamp) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    if ((a2dp_hal->ctrl_fd == AUDIO_SKT_DISCONNECTED) ||
            (a2dp_hal->state != AUDIO_A2DP_STATE_STARTED))  // Audio is not streaming
        return -1;
    if (a2dp_command(a2dp_hal, A2DP_CTRL_GET_PRESENTATION_POSITION) < 0)
        return -1;
    if (a2dp_ctrl_receive(a2dp_hal, bytes, sizeof(*bytes)) < 0)
        return -1;
    if (a2dp_ctrl_receive(a2dp_hal, delay, sizeof(*delay)) < 0)
        return -1;

    uint32_t seconds;
    if (a2dp_ctrl_receive(a2dp_hal, &seconds, sizeof(seconds)) < 0)
        return -1;

    uint32_t nsec;
    if (a2dp_ctrl_receive(a2dp_hal, &nsec, sizeof(nsec)) < 0)
        return -1;

    timestamp->tv_sec = seconds;
    timestamp->tv_nsec = nsec;
    return 0;
}

void a2dp_stream_common_init(void *hal) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    a2dp_hal->ctrl_fd = AUDIO_SKT_DISCONNECTED;
    a2dp_hal->audio_fd = AUDIO_SKT_DISCONNECTED;
    a2dp_hal->state = AUDIO_A2DP_STATE_STOPPED;
    /* manages max capacity of socket pipe */
    a2dp_hal->buffer_sz = AUDIO_STREAM_OUTPUT_BUFFER_SZ;
    a2dp_hal->a2dp_config.sample_rate = 44100;
    a2dp_hal->a2dp_config.format = AUDIO_FORMAT_PCM_16_BIT;
}

void a2dp_stream_common_destroy(void *hal) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    if (a2dp_hal->ctrl_fd != AUDIO_SKT_DISCONNECTED) {
        skt_disconnect(a2dp_hal->ctrl_fd);
        a2dp_hal->ctrl_fd = AUDIO_SKT_DISCONNECTED;
    }
    if (a2dp_hal->audio_fd != AUDIO_SKT_DISCONNECTED) {
        skt_disconnect(a2dp_hal->audio_fd);
        a2dp_hal->audio_fd = AUDIO_SKT_DISCONNECTED;
    }
}

int start_audio_datapath(void *hal) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    a2dp_state_t oldstate = a2dp_hal->state;

    AM_LOGI("audio_fd:%d, state:%s", a2dp_hal->audio_fd, a2dpStatus2String(a2dp_hal->state));
    a2dp_hal->state = AUDIO_A2DP_STATE_STARTING;

    int a2dp_status = a2dp_command(a2dp_hal, A2DP_CTRL_CMD_START);
    if (a2dp_status < 0) {
        AM_LOGE("Audiopath start failed (status %s)", a2dpStatus2String(a2dp_status));
        goto error;
    } else if (a2dp_status == A2DP_CTRL_ACK_INCALL_FAILURE) {
        AM_LOGE("Audiopath start failed - in call, move to suspended");
        goto error;
    }

    /* connect socket if not yet connected */
    if (a2dp_hal->audio_fd == AUDIO_SKT_DISCONNECTED) {
        a2dp_hal->audio_fd = skt_connect(A2DP_DATA_PATH, a2dp_hal->buffer_sz);
        if (a2dp_hal->audio_fd < 0) {
            AM_LOGE("Audiopath start failed - error opening data socket");
            goto error;
        }
    }
    a2dp_hal->state = AUDIO_A2DP_STATE_STARTED;
    return 0;

error:
    a2dp_hal->state = oldstate;
    return -1;
}

int stop_audio_datapath(void *hal) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    a2dp_state_t oldstate = a2dp_hal->state;

    AM_LOGI("state:%s", a2dpStatus2String(a2dp_hal->state));
    /* prevent any stray output writes from autostarting the stream
    * while stopping audiopath
    */
    a2dp_hal->state = AUDIO_A2DP_STATE_STOPPING;

    if (a2dp_command(a2dp_hal, A2DP_CTRL_CMD_STOP) < 0) {
        AM_LOGE("audiopath stop failed");
        a2dp_hal->state = oldstate;
        return -1;
    }

    a2dp_hal->state = AUDIO_A2DP_STATE_STOPPED;

    /* disconnect audio path */
    skt_disconnect(a2dp_hal->audio_fd);
    a2dp_hal->audio_fd = AUDIO_SKT_DISCONNECTED;

    if (a2dp_hal->resample) {
        aml_audio_resample_close(a2dp_hal->resample);
        a2dp_hal->resample = NULL;
    }
    return 0;
}

int suspend_audio_datapath(void *hal, bool standby) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;

    AM_LOGD("suspend_audio_datapath state:%s", a2dpStatus2String(a2dp_hal->state));
    if (a2dp_hal->state == AUDIO_A2DP_STATE_STOPPING)
        return -1;
    if (a2dp_command(a2dp_hal, A2DP_CTRL_CMD_SUSPEND) < 0)
        return -1;
    if (standby)
        a2dp_hal->state = AUDIO_A2DP_STATE_STANDBY;
    else
        a2dp_hal->state = AUDIO_A2DP_STATE_SUSPENDED;

    /* disconnect audio path */
    skt_disconnect(a2dp_hal->audio_fd);
    a2dp_hal->audio_fd = AUDIO_SKT_DISCONNECTED;
    return 0;
}

int a2dp_hw_dump(void *hal, int fd) {
    aml_a2dp_hal *a2dp_hal = (aml_a2dp_hal *)hal;
    btav_a2dp_codec_config_t *capability = &a2dp_hal->capability;
    int max_index = 0;

    dprintf(fd, "-[AML_HAL]--capability--\n");
    dprintf(fd, "-[AML_HAL] sample_rate (%#-8x    :", capability->sample_rate);
    max_index = 32 - __builtin_clz(capability->sample_rate);
    for (int i = 0; i < max_index; i++) {
        btav_a2dp_codec_sample_rate_t value = capability->sample_rate & (1 << i);
        if (value != 0) {
            dprintf(fd, "%s, ", btAvSampleRate2String(value));
        }
    }
    dprintf(fd, "\n-[AML_HAL] bits_per_sample (%#-8x:", capability->bits_per_sample);
    max_index = 32 - __builtin_clz(capability->bits_per_sample);
    for (int i = 0; i < max_index; i++) {
        btav_a2dp_codec_bits_per_sample_t value = capability->bits_per_sample & (1 << i);
        if (value != 0) {
            dprintf(fd, "%s bit, ", btAvBitPreSample2String(value));
        }
    }
    dprintf(fd, "\n-[AML_HAL] channel_mode (%#-8x   :", capability->channel_mode);
    max_index = 32 - __builtin_clz(capability->channel_mode);
    for (int i = 0; i < max_index; i++) {
        btav_a2dp_codec_channel_mode_t value = capability->channel_mode & (1 << i);
        if (value != 0) {
            dprintf(fd, "%s, ", btAvChannel2String(value));
        }
    }
    dprintf(fd, "\n");
    return 0;
}

