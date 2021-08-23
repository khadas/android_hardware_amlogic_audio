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

#ifndef _A2DP_HAL_H_
#define _A2DP_HAL_H_

#include "a2dp_hal.h"

/*****************************************************************************
 *  Constants & Macros
 *****************************************************************************/

#define A2DP_AUDIO_HARDWARE_INTERFACE "audio.a2dp"
#define A2DP_CTRL_PATH "/data/misc/bluedroid/.a2dp_ctrl"
#define A2DP_DATA_PATH "/data/misc/bluedroid/.a2dp_data"

// AUDIO_STREAM_OUTPUT_BUFFER_SZ controls the size of the audio socket buffer.
// If one assumes the write buffer is always full during normal BT playback,
// then increasing this value increases our playback latency.
//
// FIXME: The BT HAL should consume data at a constant rate.
// AudioFlinger assumes that the HAL draws data at a constant rate, which is
// true for most audio devices; however, the BT engine reads data at a variable
// rate (over the short term), which confuses both AudioFlinger as well as
// applications which deliver data at a (generally) fixed rate.
//
// 20 * 512 is not sufficient to smooth the variability for some BT devices,
// resulting in mixer sleep and throttling. We increase this to 28 * 512 to help
// reduce the effect of variable data consumption.
#define AUDIO_STREAM_OUTPUT_BUFFER_SZ (28 * 512)
#define AUDIO_STREAM_CONTROL_OUTPUT_BUFFER_SZ 256

// AUDIO_STREAM_OUTPUT_BUFFER_PERIODS controls how the socket buffer is divided
// for AudioFlinger data delivery. The AudioFlinger mixer delivers data in
// chunks of AUDIO_STREAM_OUTPUT_BUFFER_SZ / AUDIO_STREAM_OUTPUT_BUFFER_PERIODS.
// If the number of periods is 2, the socket buffer represents "double
// buffering" of the AudioFlinger mixer buffer.
//
// In general, AUDIO_STREAM_OUTPUT_BUFFER_PERIODS * 16 * 4 should be a divisor
// of AUDIO_STREAM_OUTPUT_BUFFER_SZ.
//
// These values should be chosen such that
//
// AUDIO_STREAM_BUFFER_SIZE * 1000 / (AUDIO_STREAM_OUTPUT_BUFFER_PERIODS
//         * AUDIO_STREAM_DEFAULT_RATE * 4) > 20 (ms)
//
// to avoid introducing the FastMixer in AudioFlinger. Using the FastMixer
// results in unnecessary latency and CPU overhead for Bluetooth.
#define AUDIO_STREAM_OUTPUT_BUFFER_PERIODS 2

#define AUDIO_SKT_DISCONNECTED (-1)

typedef enum {
    A2DP_CTRL_CMD_NONE,
    A2DP_CTRL_CMD_CHECK_READY,
    A2DP_CTRL_CMD_START,
    A2DP_CTRL_CMD_STOP,
    A2DP_CTRL_CMD_SUSPEND,
    A2DP_CTRL_GET_INPUT_AUDIO_CONFIG,
    A2DP_CTRL_GET_OUTPUT_AUDIO_CONFIG,
    A2DP_CTRL_SET_OUTPUT_AUDIO_CONFIG,
    A2DP_CTRL_CMD_OFFLOAD_START,
    A2DP_CTRL_GET_PRESENTATION_POSITION,
} tA2DP_CTRL_CMD;

typedef enum {
    A2DP_CTRL_ACK_SUCCESS,
    A2DP_CTRL_ACK_FAILURE,
    A2DP_CTRL_ACK_INCALL_FAILURE, /* Failure when in Call*/
    A2DP_CTRL_ACK_UNSUPPORTED,
    A2DP_CTRL_ACK_PENDING,
    A2DP_CTRL_ACK_DISCONNECT_IN_PROGRESS,
} tA2DP_CTRL_ACK;

typedef enum {
    BTAV_A2DP_CODEC_SAMPLE_RATE_NONE = 0x0,
    BTAV_A2DP_CODEC_SAMPLE_RATE_44100 = 0x1 << 0,
    BTAV_A2DP_CODEC_SAMPLE_RATE_48000 = 0x1 << 1,
    BTAV_A2DP_CODEC_SAMPLE_RATE_88200 = 0x1 << 2,
    BTAV_A2DP_CODEC_SAMPLE_RATE_96000 = 0x1 << 3,
    BTAV_A2DP_CODEC_SAMPLE_RATE_176400 = 0x1 << 4,
    BTAV_A2DP_CODEC_SAMPLE_RATE_192000 = 0x1 << 5,
    BTAV_A2DP_CODEC_SAMPLE_RATE_16000 = 0x1 << 6,
    BTAV_A2DP_CODEC_SAMPLE_RATE_24000 = 0x1 << 7
} btav_a2dp_codec_sample_rate_t;

typedef enum {
    BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE = 0x0,
    BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16 = 0x1 << 0,
    BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24 = 0x1 << 1,
    BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32 = 0x1 << 2
} btav_a2dp_codec_bits_per_sample_t;

typedef enum {
    BTAV_A2DP_CODEC_CHANNEL_MODE_NONE = 0x0,
    BTAV_A2DP_CODEC_CHANNEL_MODE_MONO = 0x1 << 0,
    BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO = 0x1 << 1
} btav_a2dp_codec_channel_mode_t;

typedef struct btav_a2dp_codec_config {
    btav_a2dp_codec_sample_rate_t sample_rate;
    btav_a2dp_codec_bits_per_sample_t bits_per_sample;
    btav_a2dp_codec_channel_mode_t channel_mode;
} btav_a2dp_codec_config_t;

typedef enum {
    AUDIO_A2DP_STATE_STARTING           = 0,
    AUDIO_A2DP_STATE_STARTED            = 1,
    AUDIO_A2DP_STATE_STOPPING           = 2,
    AUDIO_A2DP_STATE_STOPPED            = 3,
    /* need explicit set param call to resume (suspend=false) */
    AUDIO_A2DP_STATE_SUSPENDED          = 4,
    AUDIO_A2DP_STATE_STANDBY            = 5,/* allows write to autoresume */
} a2dp_state_t;

void a2dp_stream_common_init(void *hal);
void a2dp_stream_common_destroy(void *hal);
int a2dp_write_output_audio_config(void *hal, btav_a2dp_codec_config_t *codec_capability);
int a2dp_read_output_audio_config(void *hal, btav_a2dp_codec_config_t *codec_capability);
int a2dp_get_output_audio_config(void *hal, btav_a2dp_codec_config_t *codec_config,
        btav_a2dp_codec_config_t *codec_capability);
int start_audio_datapath(void *hal);
int suspend_audio_datapath(void *hal, bool standby);
int stop_audio_datapath(void *hal);
int skt_write(void *hal, const void *buffer, size_t bytes);

const char* a2dpStatus2String(a2dp_state_t type);

int a2dp_hw_dump(void *hal, int fd);


#endif

