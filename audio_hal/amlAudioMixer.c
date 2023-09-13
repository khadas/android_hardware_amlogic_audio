/*
 * Copyright (C) 2018 The Android Open Source Project
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

#define LOG_TAG "amlAudioMixer"
//#define LOG_NDEBUG 0
#define DEBUG_DUMP 0

#define __USE_GNU
#include <cutils/log.h>
#include <errno.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <stdlib.h>
#include <system/audio.h>
#include <aml_volume_utils.h>
#include <inttypes.h>
#include <audio_utils/primitives.h>

#ifdef ENABLE_AEC_APP
#include "audio_aec.h"
#endif

#include "amlAudioMixer.h"
#include "audio_hw_utils.h"
#include "hw_avsync.h"
#include "audio_hwsync.h"
#include "audio_hwsync_wrap.h"
#include "audio_data_process.h"
#include "audio_virtual_buf.h"

#include "audio_hw.h"
#include "a2dp_hal.h"
#include "audio_bt_sco.h"
#include "aml_audio_timer.h"
#include "aml_malloc_debug.h"
#include "aml_audio_spdifout.h"
#include "audio_hw_resource_mgr.h"


enum {
    INPORT_NORMAL,   // inport not underrun
    INPORT_UNDERRUN, //inport doesn't have data, underrun may happen later
    INPORT_FEED_SILENCE_DONE, //underrun will happen quickly, we feed some silence data to avoid noise
};

//simple mixer support: 2 in , 1 out
struct amlAudioMixer {
    input_port *in_ports[NR_INPORTS];
    uint32_t inportsMasks; // records of inport IDs
    uint32_t inportsAvailMasks; // 1<< NR_INPORTS - 1
    MIXER_OUTPUT_PORT cur_output_port_type;
    output_port *out_ports[MIXER_OUTPUT_PORT_NUM];
    pthread_mutex_t outport_locks[MIXER_OUTPUT_PORT_NUM];
    pthread_mutex_t inport_lock;
    ssize_t (*write)(struct amlAudioMixer *mixer, void *buffer, int bytes);

    aml_pcm_mixing_st stereo_mixer;
    aml_pcm_mixing_st multich_mixer;
    aml_pcm_downmix_st pcm_downmix;

    uint32_t hwsync_frame_size;
    pthread_t out_mixer_tid;
    pthread_mutex_t lock;
    unsigned int exit_thread : 1;
    unsigned int mixing_enable : 1;
    aml_mixer_state state;
    struct timespec tval_last_write;
    struct aml_audio_device *adev;
    bool continuous_output;
    //int init_ok : 1;
    int submix_standby;
    //aml_audio_mixer_run_state_type_e run_state;

    //multich pcm output port
    uint64_t run_count; // use for reduce debug info
    mc_output_port *mc_out_port;
    bool mc_out_enable;
    pthread_mutex_t mc_out_lock;
    bool aaudio_low_latency;
};

int mixer_set_state(struct amlAudioMixer *audio_mixer, aml_mixer_state state)
{
    audio_mixer->state = state;
    return 0;
}

int mixer_set_continuous_output(struct amlAudioMixer *audio_mixer,
        bool continuous_output)
{
    audio_mixer->continuous_output = continuous_output;
    return 0;
}

bool mixer_is_continuous_enabled(struct amlAudioMixer *audio_mixer)
{
    return audio_mixer->continuous_output;
}

aml_mixer_state mixer_get_state(struct amlAudioMixer *audio_mixer)
{
    return audio_mixer->state;
}

/**
 * Returns the first initialized port according to pMasks and resets
 * the corresponding bit. Can be called repeatedly to iterate over
 * initialized ports.
 */
static inline input_port *mixer_get_inport_by_mask_right_first(
        struct amlAudioMixer *audio_mixer, uint32_t *pMasks)
{
    uint8_t bit_position = get_bit_position_in_mask(NR_INPORTS - 1, pMasks);
    return audio_mixer->in_ports[bit_position];
}

/**
 * Returns the index of the first available supported port.
 */
static unsigned int mixer_get_available_inport_index(struct amlAudioMixer *audio_mixer)
{
    unsigned int index = 0;
    unsigned int mask = audio_mixer->inportsAvailMasks;

    AM_LOGD("+inportsAvailMasks: %#x", audio_mixer->inportsAvailMasks);
    index = __builtin_ctz(audio_mixer->inportsAvailMasks);
    audio_mixer->inportsAvailMasks &= ~(1 << index);
    AM_LOGD("-inportsAvailMasks:%#x, index %d", audio_mixer->inportsAvailMasks, index);
    return index;
}

int init_mixer_input_port(struct amlAudioMixer *audio_mixer,
        struct audio_config *config,
        audio_output_flags_t flags,
        int (*on_notify_cbk)(void *data),
        void *notify_data,
        int (*on_input_avail_cbk)(void *data),
        void *input_avail_data,
        meta_data_cbk_t on_meta_data_cbk,
        void *meta_data,
        float volume)
{
    R_CHECK_POINTER_LEGAL(-EINVAL, audio_mixer, "");
    R_CHECK_POINTER_LEGAL(-EINVAL, config, "");
    R_CHECK_POINTER_LEGAL(-EINVAL, notify_data, "");

    input_port *in_port = NULL;
    uint8_t port_index = -1;
    struct aml_stream_out *aml_out = notify_data;
    bool direct_on = false;

    if (aml_out->inputPortID != -1) {
       AM_LOGW("stream input port id:%d exits delete it.", aml_out->inputPortID);
       delete_mixer_input_port(audio_mixer, aml_out->inputPortID);
    }
    /* if direct on, ie. the ALSA buffer is full, no need padding data anymore  */
    direct_on = (audio_mixer->in_ports[AML_MIXER_INPUT_PORT_PCM_DIRECT] != NULL);
    in_port = new_input_port(MIXER_FRAME_COUNT, config, flags, volume, direct_on);
    if (in_port == NULL) {
        AM_LOGE("new_input_port is NULL");
        return -1;
    }
    port_index = mixer_get_available_inport_index(audio_mixer);
    /*coverity[leaked_storage]*/
    R_CHECK_PARAM_LEGAL(-1, port_index, 0, NR_INPORTS - 1, "");

    if (audio_mixer->in_ports[port_index] != NULL) {
        AM_LOGW("inport index:[%d]%s already exists! recreate", port_index, mixerInputType2Str(port_index));
        free_input_port(audio_mixer->in_ports[port_index]);
    }

    in_port->ID = port_index;
    AM_LOGI("input port:%s, size %d frames, frame_write_sum:%" PRId64 "",
        mixerInputType2Str(in_port->enInPortType), MIXER_FRAME_COUNT, aml_out->frame_write_sum);
    audio_mixer->in_ports[port_index] = in_port;
    audio_mixer->inportsMasks |= 1 << port_index;
    aml_out->inputPortID = port_index;

    set_port_notify_cbk(in_port, on_notify_cbk, notify_data);
    set_port_input_avail_cbk(in_port, on_input_avail_cbk, input_avail_data);
    if (on_meta_data_cbk && meta_data) {
        in_port->is_hwsync = true;
        set_port_meta_data_cbk(in_port, on_meta_data_cbk, meta_data);
    }
    in_port->initial_frames = aml_out->frame_write_sum;
    return 0;
}

int delete_mixer_input_port(struct amlAudioMixer *audio_mixer, uint8_t port_index)
{
    R_CHECK_PARAM_LEGAL(-EINVAL, port_index, 0, NR_INPORTS - 1, "");
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);

    AM_LOGI("input port ID:%d, type:%s, cur mask:%#x", port_index,
        mixerInputType2Str(in_port->enInPortType), audio_mixer->inportsMasks);
    pthread_mutex_lock(&audio_mixer->lock);
    pthread_mutex_lock(&audio_mixer->inport_lock);
    free_input_port(in_port);
    audio_mixer->in_ports[port_index] = NULL;
    audio_mixer->inportsMasks &= ~(1 << port_index);
    audio_mixer->inportsAvailMasks |= 1 << port_index;
    pthread_mutex_unlock(&audio_mixer->inport_lock);
    pthread_mutex_unlock(&audio_mixer->lock);
    return 0;
}

int send_mixer_inport_message(struct amlAudioMixer *audio_mixer, uint8_t port_index, PORT_MSG msg)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return send_inport_message(in_port, msg);
}

int send_mixer_outport_message(struct amlAudioMixer *audio_mixer, uint8_t port_index,
        PORT_MSG msg, void *info, int info_len)
{
    output_port *out_port = audio_mixer->out_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, out_port, "port_index:%d", port_index);
    return send_outport_message(out_port, msg, info, info_len);
}

void set_mixer_hwsync_frame_size(struct amlAudioMixer *audio_mixer, uint32_t frame_size)
{
    AM_LOGI("framesize %d", frame_size);
    audio_mixer->hwsync_frame_size = frame_size;
}

uint32_t get_mixer_hwsync_frame_size(struct amlAudioMixer *audio_mixer)
{
    return audio_mixer->hwsync_frame_size;
}

uint32_t get_mixer_inport_consumed_frames(struct amlAudioMixer *audio_mixer, uint8_t port_index)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return get_inport_consumed_size(in_port) / in_port->cfg.frame_size;
}

int set_mixer_inport_volume(struct amlAudioMixer *audio_mixer, uint8_t port_index, float vol)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    if (vol > 1.0 || vol < 0) {
        AM_LOGE("invalid vol %f", vol);
        return -EINVAL;
    }
    set_inport_volume(in_port, vol);
    return 0;
}

float get_mixer_inport_volume(struct amlAudioMixer *audio_mixer, uint8_t port_index)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return get_inport_volume(in_port);
}

int mixer_write_inport(struct amlAudioMixer *audio_mixer, uint8_t port_index, const void *buffer, int bytes)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    int         written = 0;

    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    written = in_port->write(in_port, buffer, bytes);
    if (get_inport_state(in_port) != ACTIVE) {
        AM_LOGI("input port:%s is active now", mixerInputType2Str(in_port->enInPortType));
        set_inport_state(in_port, ACTIVE);
    }
    AM_LOGV("portIndex %d", port_index);
    return written;
}

int mixer_read_inport(struct amlAudioMixer *audio_mixer, uint8_t port_index, void *buffer, int bytes)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return in_port->read(in_port, buffer, bytes);
}

int mixer_set_inport_state(struct amlAudioMixer *audio_mixer, uint8_t port_index, port_state state)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return set_inport_state(in_port, state);
}

port_state mixer_get_inport_state(struct amlAudioMixer *audio_mixer, uint8_t port_index)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return get_inport_state(in_port);
}
//TODO: handle message queue
static void mixer_procs_msg_queue(struct amlAudioMixer *audio_mixer __unused)
{
    AM_LOGV("start");
    return;
}

static inline MIXER_OUTPUT_PORT mixer_get_cur_outport(struct amlAudioMixer *audio_mixer, output_port **out_port)
{
    MIXER_OUTPUT_PORT port_index = audio_mixer->cur_output_port_type;
    *out_port = NULL;
    R_CHECK_PARAM_LEGAL(-1, port_index, MIXER_OUTPUT_PORT_STEREO_PCM, MIXER_OUTPUT_PORT_NUM - 1, "");
    pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
    *out_port = audio_mixer->out_ports[port_index];
    if (*out_port == NULL) {
        AM_LOGW("out_port is null");
        pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
        return MIXER_OUTPUT_PORT_INVAL;
    }
    /*coverity[missing_unlock]*/
    return port_index;
}

size_t get_outport_data_avail(output_port *outport)
{
    return outport->bytes_avail;
}

int set_outport_data_avail(output_port *outport, size_t avail)
{
    if (avail > outport->data_buf_len) {
        AM_LOGE("invalid avail %zu", avail);
        return -EINVAL;
    }
    outport->bytes_avail = avail;
    return 0;
}

int init_mixer_output_port(struct amlAudioMixer *audio_mixer,
        MIXER_OUTPUT_PORT output_type,
        struct audioCfg *config,
        size_t buf_frames)
{
    R_CHECK_PARAM_LEGAL(-1, output_type, MIXER_OUTPUT_PORT_STEREO_PCM, MIXER_OUTPUT_PORT_NUM - 1, "");
    struct aml_audio_device     *adev = audio_mixer->adev;

    pthread_mutex_lock(&audio_mixer->outport_locks[output_type]);
    AM_LOGI("output port:%s", mixerOutputType2Str(output_type));
    output_port *out_port = new_output_port(output_type, config, buf_frames);
    if (out_port == NULL) {
        AM_LOGW("new_output_port fail");
        pthread_mutex_unlock(&audio_mixer->outport_locks[output_type]);
        return -1;
    }
    audio_mixer->cur_output_port_type = output_type;

    //set_port_notify_cbk(port, on_notify_cbk, notify_data);
    //set_port_input_avail_cbk(port, on_input_avail_cbk, input_avail_data);
    audio_mixer->out_ports[output_type] = out_port;
    aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIF_FORMAT, AML_STEREO_PCM);

#ifdef ENABLE_AEC_APP
    out_port->aec = audio_mixer->adev->aec;
    struct pcm_config alsa_config;
    memset(&alsa_config, 0, sizeof(struct pcm_config));
    output_get_alsa_config(out_port, &alsa_config);
    int aec_ret = init_aec_reference_config(out_port->aec, alsa_config);
    NO_R_CHECK_RET(aec_ret, "AEC: Speaker config init failed!");
#endif
    pthread_mutex_unlock(&audio_mixer->outport_locks[output_type]);
    return 0;
}

int delete_mixer_output_port(struct amlAudioMixer *audio_mixer, MIXER_OUTPUT_PORT port_index)
{
    R_CHECK_PARAM_LEGAL(-1, port_index, MIXER_OUTPUT_PORT_STEREO_PCM, MIXER_OUTPUT_PORT_NUM - 1, "");
    struct aml_audio_device     *adev = audio_mixer->adev;
#ifdef ENABLE_AEC_APP
    destroy_aec_reference_config(adev->aec);
#endif
    AM_LOGI("output port:%s", mixerOutputType2Str(port_index));
    pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
    audio_mixer->cur_output_port_type = MIXER_OUTPUT_PORT_INVAL;
    output_port *out_port = audio_mixer->out_ports[port_index];
    if (out_port == NULL) {
        AM_LOGW("out_port is null");
        pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
        return -1;
    }
    free_output_port(out_port);
    audio_mixer->out_ports[port_index] = NULL;
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIF_FORMAT, AML_STEREO_PCM);
    return 0;
}

static int mixer_output_startup(struct amlAudioMixer *audio_mixer)
{
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    AM_LOGI("output port:%s", mixerOutputType2Str(port_index));
    out_port->start(out_port);
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    audio_mixer->submix_standby = 0;

    return 0;
}

int mixer_output_standby(struct amlAudioMixer *audio_mixer)
{
    ALOGI("[%s:%d] request sleep thread", __func__, __LINE__);
    int timeoutMs = 200;
    (void)audio_mixer;
    //audio_mixer->run_state = AML_AUDIO_MIXER_RUN_STATE_REQ_SLEEP;
    return 0;
}

static int mixer_thread_sleep(struct amlAudioMixer *audio_mixer)
{
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    AM_LOGI("output port:%s", mixerOutputType2Str(port_index));
    if (false == audio_mixer->submix_standby) {
        ALOGI("[%s:%d] start going to standby", __func__, __LINE__);
        out_port->standby(out_port);
        audio_mixer->submix_standby = true;
    }
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    return 0;
}

int mixer_output_dummy(struct amlAudioMixer *audio_mixer, bool en)
{
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");

    AM_LOGI("output port:%s, en:%d", mixerOutputType2Str(port_index), en);
    outport_set_dummy(out_port, en);
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    return 0;
}

static int mixer_output_write(struct amlAudioMixer *audio_mixer)
{
    audio_config_base_t in_data_config = {48000, AUDIO_CHANNEL_OUT_STEREO, AUDIO_FORMAT_PCM_16_BIT};
    ssize_t ret = 0;
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    struct aml_audio_device *adev = audio_mixer->adev;
    uint32_t masks = 0;
    input_port *in_port = NULL;
    struct snd_pcm_status status;
    bool alsa_status = true;
    struct aml_stream_out *aml_out = NULL;
    mc_output_port *mc_out_port = NULL;

    out_port->sound_track_mode = audio_mixer->adev->sound_track_mode;
    while (out_port->bytes_avail > 0) {
        // out_write_callbacks();
        aml_audio_switch_output_mode((int16_t *)out_port->data_buf,
            out_port->bytes_avail, out_port->sound_track_mode);
        if (is_include_sco_out_port(adev->cur_out_devices)) {
            if (out_port->cfg.channelCnt == 1) {
                in_data_config.channel_mask = AUDIO_CHANNEL_OUT_MONO;
            } else if (out_port->cfg.channelCnt == 2) {
                in_data_config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
            } else {
                AM_LOGW("not supported channel:%d", out_port->cfg.channelCnt);
                pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
                return out_port->bytes_avail;
            }
            in_data_config.sample_rate = out_port->cfg.sampleRate;
            in_data_config.format = out_port->cfg.format;
            if (is_TV(adev))
                apply_volume(adev->sink_gain[OUTPORT_BT_SCO], out_port->data_buf, sizeof(uint16_t),
                    out_port->bytes_avail);
            ret = write_to_sco(adev, &in_data_config, out_port->data_buf, out_port->bytes_avail);
            if (ret < 0) {
                ALOGE("%s write_to_sco fail when insert", __func__);
                break;
           }
        } else {
            if (is_include_a2dp_out_port(adev->cur_out_devices)) {
                if (out_port->cfg.channelCnt == 1) {
                    in_data_config.channel_mask = AUDIO_CHANNEL_OUT_MONO;
                } else if (out_port->cfg.channelCnt == 2) {
                    in_data_config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                } else {
                    AM_LOGW("not supported channel:%d", out_port->cfg.channelCnt);
                    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
                    return out_port->bytes_avail;
                }
                in_data_config.sample_rate = out_port->cfg.sampleRate;
                in_data_config.format = out_port->cfg.format;
                if (is_TV(adev)) {
                    float volume = aml_audio_get_s_gain_by_src(adev, get_dev_patch_src(adev));

                    volume *= adev->sink_gain[OUTPORT_A2DP];
                    apply_volume(volume, out_port->data_buf, sizeof(uint16_t),
                        out_port->bytes_avail);
                }
                alsa_status = a2dp_out_get_status(adev);
                a2dp_out_write(adev, &in_data_config, out_port->data_buf, out_port->bytes_avail);
            }
            if (!is_TV(adev) && !adev->control_hdmitx_mute && is_include_a2dp_out_port(adev->cur_out_devices)) {
                // For STB, do not send data to spdif/hdmitx when bt is connected and mute hdmitx cannot be controlled.
            } else {
                if (audio_mixer->submix_standby) {
                    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
                    mixer_output_startup(audio_mixer);
                    pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
                }
                if (out_port->pcm_handle == NULL) {
                    alsa_status = false;
                } else {
                    pcm_ioctl(out_port->pcm_handle, SNDRV_PCM_IOCTL_STATUS, &status);
                    alsa_status = (status.state == PCM_STATE_RUNNING);
                }

                if (out_port->process) {
                    out_port->process(out_port, out_port->data_buf, out_port->bytes_avail);
                    out_port->write(out_port, out_port->processed_buf, out_port->processed_bytes);
                } else {
                    out_port->write(out_port, out_port->data_buf, out_port->bytes_avail);
                }
            }
        }

        masks = audio_mixer->inportsMasks;
        while (masks) {
            in_port = mixer_get_inport_by_mask_right_first(audio_mixer, &masks);
            if (in_port == NULL) {
                continue;
            }
            aml_out = (struct aml_stream_out *)in_port->notify_cbk_data;
            if ((aml_out == NULL) || aml_out->standby) {
                continue;
            }
            if (aml_out && (alsa_status != aml_out->alsa_running_status)) {
                ALOGI("%s alsa_running_status[%p] change from %d to %d", __func__, aml_out, aml_out->alsa_running_status, alsa_status);
                aml_out->alsa_running_status = alsa_status;
                aml_out->alsa_status_changed = true;
            }
        }
        set_outport_data_avail(out_port, 0);
    };
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    // output multi-ch pcm
    pthread_mutex_lock(&audio_mixer->mc_out_lock);
    mc_out_port = audio_mixer->mc_out_port;
    if (mc_out_port && mc_out_port->bytes_avail > 0) {
        mc_out_port->write(mc_out_port, mc_out_port->data_buf, mc_out_port->bytes_avail);
        mc_out_port->bytes_avail = 0;
    }
    pthread_mutex_unlock(&audio_mixer->mc_out_lock);

    return 0;
}

int init_stereo_mixer_buffer(struct amlAudioMixer *audio_mixer, struct audioCfg *p_mixer_cfg, int mixed_frames)
{
    R_CHECK_POINTER_LEGAL(-1, audio_mixer, "");
    R_CHECK_POINTER_LEGAL(-1, p_mixer_cfg, "");

    if (init_aml_pcm_mixer(&audio_mixer->stereo_mixer, p_mixer_cfg, mixed_frames) != 0) {
        return -1;
    }
    init_aml_pcm_downmix(&audio_mixer->pcm_downmix);
    return 0;
}

void deinit_stereo_mixer_buffer(struct amlAudioMixer *audio_mixer)
{
    if (audio_mixer == NULL) {
        AM_LOGV("audio_mixer = NULL");
        return;
    }
    deinit_aml_pcm_mixer(&audio_mixer->stereo_mixer);
    deinit_aml_pcm_downmix(&audio_mixer->pcm_downmix);
}

int init_multich_mixer_buffer(struct amlAudioMixer *audio_mixer, struct audioCfg *p_mixer_cfg, int mixed_frames)
{
    R_CHECK_POINTER_LEGAL(-1, audio_mixer, "");
    R_CHECK_POINTER_LEGAL(-1, p_mixer_cfg, "");

    if (init_aml_pcm_mixer(&audio_mixer->multich_mixer, p_mixer_cfg, mixed_frames) != 0) {
        return -1;
    }
    return 0;
}

void deinit_multich_mixer_buffer(struct amlAudioMixer *audio_mixer)
{
    if (audio_mixer == NULL) {
        AM_LOGV("audio_mixer = NULL");
        return;
    }
    deinit_aml_pcm_mixer(&audio_mixer->multich_mixer);
}

#define DEFAULT_KERNEL_FRAMES (DEFAULT_PLAYBACK_PERIOD_SIZE*DEFAULT_PLAYBACK_PERIOD_CNT)

static int mixer_update_tstamp(struct amlAudioMixer *audio_mixer)
{
    output_port *out_port = NULL;
    input_port *in_port = NULL;
    unsigned int avail = 0;
    uint32_t masks = audio_mixer->inportsMasks;

    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    if (out_port->pcm_handle == NULL) {
        AM_LOGV("pcm handle is null");
        pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
        return 0;
    }

    while (masks) {
        in_port = mixer_get_inport_by_mask_right_first(audio_mixer, &masks);
        if (in_port == NULL) {
            AM_LOGE("in_port:%p is null", in_port);
            continue;
        }

        struct aml_audio_device *adev = audio_mixer->adev;
        if (adev->cur_out_devices & AUDIO_DEVICE_OUT_ALL_A2DP) {
            uint64_t a2dp_latency_frames = a2dp_out_get_latency(adev) * in_port->cfg.sampleRate / MSEC_PER_SEC;
            if (in_port->mix_consumed_frames + in_port->initial_frames > a2dp_latency_frames) {
                in_port->presentation_frames = in_port->mix_consumed_frames + in_port->initial_frames - a2dp_latency_frames;
            } else {
                in_port->presentation_frames = 0;
            }
            clock_gettime(CLOCK_MONOTONIC, &in_port->timestamp);
            continue;
        }

        if (pcm_get_htimestamp(out_port->pcm_handle, &avail, &in_port->timestamp) == 0) {
            size_t kernel_buf_size = DEFAULT_KERNEL_FRAMES;
            int64_t signed_frames = in_port->mix_consumed_frames - kernel_buf_size + avail;
            if (signed_frames < 0) {
                signed_frames = 0;
            }
            in_port->presentation_frames = in_port->initial_frames + signed_frames;
            AM_LOGV("present frames:%" PRId64 ", initial %" PRId64 ", consumed %" PRId64 ", sec:%ld, nanosec:%ld",
                    in_port->presentation_frames,
                    in_port->initial_frames,
                    in_port->mix_consumed_frames,
                    in_port->timestamp.tv_sec,
                    in_port->timestamp.tv_nsec);
        }
    }

    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    return 0;
}

inline float get_fade_step_by_size(int fade_size, int frame_size)
{
    return 1.0/(fade_size/frame_size);
}

int init_fade(struct fade_out *fade_out, int fade_size,
        int sample_size, int channel_cnt)
{
    fade_out->vol = 1.0;
    fade_out->target_vol = 0;
    fade_out->fade_size = fade_size;
    fade_out->sample_size = sample_size;
    fade_out->channel_cnt = channel_cnt;
    fade_out->stride = get_fade_step_by_size(fade_size, sample_size * channel_cnt);
    AM_LOGI("size %d, stride %f", fade_size, fade_out->stride);
    return 0;
}

int process_fade_out(void *buf, int bytes, struct fade_out *fout)
{
    int i = 0;
    int frame_cnt = bytes / fout->sample_size / fout->channel_cnt;
    int16_t *sample = (int16_t *)buf;

    if (fout->channel_cnt != 2 || fout->sample_size != 2)
        AM_LOGE("not support yet");
    AM_LOGI("++++fade out vol %f, size %d", fout->vol, fout->fade_size);
    for (i = 0; i < frame_cnt; i++) {
        sample[i] = sample[i]*fout->vol;
        sample[i+1] = sample[i+1]*fout->vol;
        fout->vol -= fout->stride;
        if (fout->vol < 0)
            fout->vol = 0;
    }
    fout->fade_size -= bytes;
    AM_LOGI("----fade out vol %f, size %d", fout->vol, fout->fade_size);

    return 0;
}

static int update_inport_avail(input_port *in_port)
{
    // first throw away the padding frames
    if (in_port->padding_frames > 0) {
        in_port->padding_frames -= in_port->data_buf_frame_cnt;
        set_inport_pts_valid(in_port, false);
    } else {
        in_port->mix_consumed_frames += in_port->data_buf_frame_cnt;
        set_inport_pts_valid(in_port, true);
    }
    in_port->data_valid = 1;
    return 0;
}

static void process_port_msg(input_port *in_port)
{
    port_message *msg = get_inport_message(in_port);
    if (msg) {
        AM_LOGI("msg: %s", port_msg_to_str(msg->msg_what));
        switch (msg->msg_what) {
        case MSG_PAUSE: {
            struct aml_stream_out *out = (struct aml_stream_out *)in_port->notify_cbk_data;
            audio_hwsync_t *hwsync = (out != NULL) ? (out->hwsync) : NULL;
            AM_LOGI("[%s:%d] hwsync:%p tsync pause", __func__, __LINE__, hwsync);
            if (hwsync != NULL) {
                aml_hwsync_wrap_set_pause(hwsync);
            }
            set_inport_state(in_port, PAUSING);
            break;
        }
        case MSG_FLUSH:
            set_inport_state(in_port, FLUSHING);
            break;
        case MSG_RESUME: {
            struct aml_stream_out *out = (struct aml_stream_out *)in_port->notify_cbk_data;
            audio_hwsync_t *hwsync = (out != NULL) ? (out->hwsync) : NULL;
            //AM_LOGI("[%s:%d] hwsync:%p tsync resume", hwsync);
            if ((hwsync != NULL) && (hwsync->use_mediasync)) {
                hwsync->hwsync_need_resume = true;
            }
            set_inport_state(in_port, RESUMING);
            break;
        }
        default:
            AM_LOGE("not support");
        }

        remove_inport_message(in_port, msg);
    }
}

int mixer_flush_inport(struct amlAudioMixer *audio_mixer, uint8_t port_index)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return reset_input_port(in_port);
}

static int mixer_inports_read(struct amlAudioMixer *audio_mixer)
{
    unsigned int masks = audio_mixer->inportsMasks;
    AM_LOGV("+++");
    while (masks) {
        input_port *in_port = NULL;
        in_port = mixer_get_inport_by_mask_right_first(audio_mixer, &masks);
        if (NULL == in_port) {
            continue;
        }
        int ret = 0, fade_out = 0, fade_in = 0;
        aml_mixer_input_port_type_e type = in_port->enInPortType;
        process_port_msg(in_port);
        port_state state = get_inport_state(in_port);

        if (type == AML_MIXER_INPUT_PORT_PCM_DIRECT) {
            //if in pausing states, don't retrieve data
            if (state == PAUSING) {
                fade_out = 1;
            } else if (state == RESUMING) {
                struct aml_stream_out *out = (struct aml_stream_out *)in_port->notify_cbk_data;
                audio_hwsync_t *hwsync = (out != NULL) ? (out->hwsync) : NULL;
                fade_in = 1;
                AM_LOGI("input port:%s tsync resume", mixerInputType2Str(type));
                if (hwsync)
                    hwsync->hwsync_need_resume = true;
                set_inport_state(in_port, ACTIVE);
            } else if (state == STOPPED || state == PAUSED || state == FLUSHED) {
                AM_LOGV("input port:%s stopped, paused or flushed", mixerInputType2Str(type));
                continue;
            } else if (state == FLUSHING) {
                mixer_flush_inport(audio_mixer, in_port->ID);
                AM_LOGI("input port:%s flushing->flushed", mixerInputType2Str(type));
                set_inport_state(in_port, FLUSHED);
                continue;
            }
            if (get_inport_state(in_port) == ACTIVE && in_port->data_valid) {
                AM_LOGI("input port:%s data already valid", mixerInputType2Str(type));
                continue;
            }
        } else {
            if (in_port->data_valid) {
                AM_LOGI("input port ID:%d port:%s, data already valid", in_port->ID, mixerInputType2Str(type));
                continue;
            }
        }

        int input_avail_size = in_port->rbuf_avail(in_port);
        AM_LOGV("input port:%s, portId:%d, avail:%d, masks:%#x, inportsMasks:%#x, data_len_bytes:%zu",
            mixerInputType2Str(type), in_port->ID, input_avail_size, masks, audio_mixer->inportsMasks, in_port->data_len_bytes);
        if (input_avail_size >= in_port->data_len_bytes) {
            if (in_port->first_read) {
                if (input_avail_size < in_port->inport_start_threshold) {
                    continue;
                } else {
                    AM_LOGI("input port:%s first start, portId:%d, avail:%d",
                        mixerInputType2Str(type), in_port->ID, input_avail_size);
                    in_port->first_read = false;
                }
            }
            ret = mixer_read_inport(audio_mixer, in_port->ID, in_port->data, in_port->data_len_bytes);
            if (ret == (int)in_port->data_len_bytes) {
                struct aml_stream_out *out = (struct aml_stream_out *)in_port->notify_cbk_data;
                if (fade_out) {
                    audio_hwsync_t *hwsync = (out != NULL) ? (out->hwsync) : NULL;
                    struct aml_audio_device *adev = (out != NULL) ? (out->dev) : NULL;
                    AM_LOGI("output port:%s fade out, pausing->pausing_1, tsync pause audio", mixerInputType2Str(type));
                    aml_hwsync_wrap_set_pause(hwsync);
                    if (out && out->hal_format == AUDIO_FORMAT_PCM_32_BIT) {
                        audio_fade_func_32bit(in_port->data, ret, 0, out->hal_ch);
                    } else {
                        audio_fade_func_16bit(in_port->data, ret, 0, out->hal_ch);
                    }
                    set_inport_state(in_port, PAUSED);
                    /* Mute the last data to prevent gap. */
                    ring_buffer_clear(in_port->r_buf);
                    if (adev && adev->is_netflix) {
                        // prepare for the next writing.
                        in_port->first_read = true;
                        out->audio_data_handle_state = AUDIO_DATA_HANDLE_START;
                    }
                } else if (fade_in) {
                    AM_LOGI("input port:%s fade in", mixerInputType2Str(type));
                    if (out && out->hal_format == AUDIO_FORMAT_PCM_32_BIT) {
                        audio_fade_func_32bit(in_port->data, ret, 1, out->hal_ch);
                    } else {
                        audio_fade_func_16bit(in_port->data, ret, 1, out->hal_ch);
                    }
                    set_inport_state(in_port, ACTIVE);
                }
                update_inport_avail(in_port);
                if (getprop_bool("vendor.media.audiohal.inport") &&
                        (in_port->enInPortType == AML_MIXER_INPUT_PORT_PCM_DIRECT)) {
                        aml_audio_dump_audio_bitstreams("/data/audio/inportDirectFade.raw",
                                in_port->data, in_port->data_len_bytes);
                }
            } else {
                AM_LOGW("port:%s read fail, have read:%d Byte, need %zu Byte",
                    mixerInputType2Str(type), ret, in_port->data_len_bytes);
            }
        } else {
            struct aml_audio_device     *adev = audio_mixer->adev;
            if (adev->debug_flag) {
                AM_LOGD("port:%d ring buffer data is not enough", in_port->ID);
            }
        }
    }

    return 0;
}

int mixer_need_wait_forever(struct amlAudioMixer *audio_mixer)
{
    return mixer_get_state(audio_mixer) != MIXER_INPORTS_READY;
}

static uint32_t hwsync_align_to_frame(uint32_t consumed_size, uint32_t frame_size)
{
    return consumed_size - (consumed_size % frame_size);
}

static int retrieve_hwsync_header(struct amlAudioMixer *audio_mixer,
        input_port *in_port, output_port *out_port)
{
    uint32_t frame_size = get_mixer_hwsync_frame_size(audio_mixer);
    uint32_t port_consumed_size = get_inport_consumed_size(in_port);
    struct aml_audio_device *adev = aml_adev_get_handle();
    int diff_ms = 0;
    struct hw_avsync_header header;
    int ret = 0;

    if (frame_size == 0) {
        AM_LOGV("invalid frame size 0");
        return -EINVAL;
    }

    if (!in_port->is_hwsync) {
        AM_LOGE("not hwsync port");
        return -EINVAL;
    }

    memset(&header, 0, sizeof(struct hw_avsync_header));
    AM_LOGV("direct out port bytes before cbk %zu", out_port->bytes_avail);
    if (!in_port->meta_data_cbk) {
        AM_LOGE("no meta_data_cbk set!!");
        return -EINVAL;
    }
    AM_LOGV("port %p, data %p", in_port, in_port->meta_data_cbk_data);
    ret = in_port->meta_data_cbk(in_port->meta_data_cbk_data,
                port_consumed_size, &header, &diff_ms);
    if (ret < 0) {
        if (ret != -EAGAIN)
            AM_LOGE("meta_data_cbk fail err = %d!!", ret);
        return ret;
    }
    AM_LOGV("meta data cbk, diff ms = %d", diff_ms);

    if (adev->is_netflix && abs(diff_ms) > 1000) {
        AM_LOGE("invalid diff_ms %d, ingore it !", diff_ms);
        diff_ms = 0;
    }

    if (diff_ms > 0) {
        in_port->bytes_to_insert = diff_ms * 48 * 4;
    } else if (diff_ms < 0) {
        in_port->bytes_to_skip = -diff_ms * 48 * 4;
    }

    return 0;
}


static int mixer_do_mixing_32bit(struct amlAudioMixer *audio_mixer)
{
    input_port *in_port_sys = audio_mixer->in_ports[AML_MIXER_INPUT_PORT_PCM_SYSTEM];
    input_port *in_port_drct = audio_mixer->in_ports[AML_MIXER_INPUT_PORT_PCM_DIRECT];
    output_port *out_port = NULL;
    struct aml_audio_device *adev = audio_mixer->adev;
    int16_t *data_sys, *data_drct, *data_mixed;
    int mixing = 0, sys_only = 0, direct_only = 0;
    int dirct_okay = 0, sys_okay = 0;
    float dirct_vol = 1.0, sys_vol = 1.0;
    int mixed_32 = 0;
    size_t i = 0, mixing_len_bytes = 0;
    size_t frames = 0;
    size_t frames_written = 0;
    float gain_speaker = adev->sink_gain[OUTPORT_SPEAKER];
    aml_pcm_mixing_st *p_2ch_mixer = NULL;

    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    if (!in_port_sys && !in_port_drct) {
        AM_LOGE("sys or direct pcm must exist!!!");
        return 0;
    }

    if (in_port_sys && in_port_sys->data_valid) {
        sys_okay = 1;
    }
    if (in_port_drct && in_port_drct->data_valid) {
        dirct_okay = 1;
    }
    if (sys_okay && dirct_okay) {
        mixing = 1;
    } else if (dirct_okay) {
        AM_LOGV("only direct okay");
        direct_only = 1;
    } else if (sys_okay) {
        sys_only = 1;
    } else {
        AM_LOGV("sys direct both not ready!");
        return -EINVAL;
    }

    p_2ch_mixer = &audio_mixer->stereo_mixer;
    R_CHECK_POINTER_LEGAL(-1, p_2ch_mixer->mixed_buf, "");
    if (p_2ch_mixer->mixed_buf_size < MIXER_FRAME_COUNT * out_port->cfg.frame_size) {
        ALOGE("mixed_buf_size is too small(%zu < %d)", p_2ch_mixer->mixed_buf_size, MIXER_FRAME_COUNT * out_port->cfg.frame_size);
        return -1;
    }

    data_mixed = (int16_t *)out_port->data_buf;
    memset(p_2ch_mixer->mixed_buf, 0 , MIXER_FRAME_COUNT * out_port->cfg.frame_size);
    if (mixing) {
        AM_LOGV("mixing");
        data_sys = (int16_t *)in_port_sys->data;
        data_drct = (int16_t *)in_port_drct->data;
        mixing_len_bytes = in_port_drct->data_len_bytes;
        //TODO: check if the two stream's frames are equal
        if (DEBUG_DUMP) {
            aml_audio_dump_audio_bitstreams("/data/audio/audiodrct.raw",
                    in_port_drct->data, in_port_drct->data_len_bytes);
            aml_audio_dump_audio_bitstreams("/data/audio/audiosyst.raw",
                    in_port_sys->data, in_port_sys->data_len_bytes);
        }
        if (in_port_drct->is_hwsync && in_port_drct->bytes_to_insert < mixing_len_bytes) {
            retrieve_hwsync_header(audio_mixer, in_port_drct, out_port);
        }

        // insert data for direct hwsync case, only send system sound
        if (in_port_drct->bytes_to_insert >= mixing_len_bytes) {
            frames = mixing_len_bytes / in_port_drct->cfg.frame_size;
            AM_LOGD("insert mixing data, need %zu, insert length %zu",
                    in_port_drct->bytes_to_insert, mixing_len_bytes);
            //memcpy(data_mixed, data_sys, mixing_len_bytes);
            //memcpy(p_mixer->mixed_buf, data_sys, mixing_len_bytes);
            if (DEBUG_DUMP) {
                aml_audio_dump_audio_bitstreams("/data/audio/systbeforemix.raw",
                        data_sys, in_port_sys->data_len_bytes);
            }
            frames_written = do_mixing_2ch(p_2ch_mixer->mixed_buf, data_sys,
                frames, in_port_sys->cfg.format, out_port->cfg.format);
            if (DEBUG_DUMP) {
                aml_audio_dump_audio_bitstreams("/data/audio/sysAftermix.raw",
                        p_2ch_mixer->mixed_buf, frames * FRAMESIZE_32BIT_STEREO);
            }
            if (is_TV(adev)) {
                apply_volume(gain_speaker, p_2ch_mixer->mixed_buf,
                    sizeof(uint32_t), frames * FRAMESIZE_32BIT_STEREO);
            }

            extend_channel_2_8(data_mixed, p_2ch_mixer->mixed_buf,
                    frames, 2, 8);

            if (DEBUG_DUMP) {
                aml_audio_dump_audio_bitstreams("/data/audio/dataInsertMixed.raw",
                        data_mixed, frames * out_port->cfg.frame_size);
            }
            in_port_drct->bytes_to_insert -= mixing_len_bytes;
            in_port_sys->data_valid = 0;
            set_outport_data_avail(out_port, frames * out_port->cfg.frame_size);
        } else {
            frames = mixing_len_bytes / in_port_drct->cfg.frame_size;
            frames_written = do_mixing_2ch(p_2ch_mixer->mixed_buf, data_drct,
                frames, in_port_drct->cfg.format, out_port->cfg.format);
            if (DEBUG_DUMP)
                aml_audio_dump_audio_bitstreams("/data/audio/tmpMixed0.raw",
                    p_2ch_mixer->mixed_buf, frames * p_2ch_mixer->mixed_frame_size);
            frames_written = do_mixing_2ch(p_2ch_mixer->mixed_buf, data_sys,
                frames, in_port_sys->cfg.format, out_port->cfg.format);
            if (DEBUG_DUMP)
                aml_audio_dump_audio_bitstreams("/data/audio/tmpMixed1.raw",
                    p_2ch_mixer->mixed_buf, frames * p_2ch_mixer->mixed_frame_size);
            if (is_TV(adev)) {
                apply_volume(gain_speaker, p_2ch_mixer->mixed_buf,
                    sizeof(uint32_t), frames * FRAMESIZE_32BIT_STEREO);
            }

            extend_channel_2_8(data_mixed, p_2ch_mixer->mixed_buf,
                    frames, 2, 8);

            in_port_drct->data_valid = 0;
            in_port_sys->data_valid = 0;
            set_outport_data_avail(out_port, frames * out_port->cfg.frame_size);
        }
        if (DEBUG_DUMP) {
            aml_audio_dump_audio_bitstreams("/data/audio/data_mixed.raw",
                out_port->data_buf, frames * out_port->cfg.frame_size);
        }
    }

    if (sys_only) {
        frames = in_port_sys->data_buf_frame_cnt;
        AM_LOGV("sys_only, frames %zu", frames);
        mixing_len_bytes = in_port_sys->data_len_bytes;
        data_sys = (int16_t *)in_port_sys->data;
        if (DEBUG_DUMP) {
            aml_audio_dump_audio_bitstreams("/data/audio/audiosyst.raw",
                    in_port_sys->data, mixing_len_bytes);
        }
        // processing data and make conversion according to cfg
        // processing_and_convert(data_mixed, data_sys, frames, in_port_sys->cfg, out_port->cfg);
        frames_written = do_mixing_2ch(p_2ch_mixer->mixed_buf, data_sys,
                frames, in_port_sys->cfg.format, out_port->cfg.format);
        if (DEBUG_DUMP) {
            aml_audio_dump_audio_bitstreams("/data/audio/sysTmp.raw",
                    p_2ch_mixer->mixed_buf, frames * FRAMESIZE_32BIT_STEREO);
        }
        if (is_TV(adev)) {
            apply_volume(gain_speaker, p_2ch_mixer->mixed_buf,
                sizeof(uint32_t), frames * FRAMESIZE_32BIT_STEREO);
        }
        if (DEBUG_DUMP) {
            aml_audio_dump_audio_bitstreams("/data/audio/sysvol.raw",
                    p_2ch_mixer->mixed_buf, frames * FRAMESIZE_32BIT_STEREO);
        }

        extend_channel_2_8(data_mixed, p_2ch_mixer->mixed_buf, frames, 2, 8);

        if (DEBUG_DUMP) {
            aml_audio_dump_audio_bitstreams("/data/audio/extendsys.raw",
                    data_mixed, frames * out_port->cfg.frame_size);
        }
        in_port_sys->data_valid = 0;
        set_outport_data_avail(out_port, frames * out_port->cfg.frame_size);
    }

    if (direct_only) {
        AM_LOGV("direct_only");
        //dirct_vol = get_inport_volume(in_port_drct);
        mixing_len_bytes = in_port_drct->data_len_bytes;
        data_drct = (int16_t *)in_port_drct->data;
        AM_LOGV("direct_only, inport consumed %zu",
                get_inport_consumed_size(in_port_drct));

        if (in_port_drct->is_hwsync && in_port_drct->bytes_to_insert < mixing_len_bytes) {
            retrieve_hwsync_header(audio_mixer, in_port_drct, out_port);
        }

        if (DEBUG_DUMP) {
            aml_audio_dump_audio_bitstreams("/data/audio/audiodrct.raw",
                    in_port_drct->data, mixing_len_bytes);
        }
        // insert 0 data to delay audio
        if (in_port_drct->bytes_to_insert >= mixing_len_bytes) {
            frames = mixing_len_bytes / in_port_drct->cfg.frame_size;
            AM_LOGD("inserting direct_only, need %zu, insert length %zu",
                    in_port_drct->bytes_to_insert, mixing_len_bytes);
            memset(data_mixed, 0, mixing_len_bytes);
            extend_channel_2_8(data_mixed, p_2ch_mixer->mixed_buf,
                    frames, 2, 8);
            in_port_drct->bytes_to_insert -= mixing_len_bytes;
            set_outport_data_avail(out_port, frames * out_port->cfg.frame_size);
        } else {
            AM_LOGV("direct_only, vol %f", dirct_vol);
            frames = mixing_len_bytes / in_port_drct->cfg.frame_size;
            //cpy_16bit_data_with_gain(data_mixed, data_drct,
            //        in_port_drct->data_len_bytes, dirct_vol);
            AM_LOGV("direct_only, frames %zu, bytes %zu", frames, mixing_len_bytes);

            frames_written = do_mixing_2ch(p_2ch_mixer->mixed_buf, data_drct,
                frames, in_port_drct->cfg.format, out_port->cfg.format);
            if (DEBUG_DUMP) {
                aml_audio_dump_audio_bitstreams("/data/audio/dirctTmp.raw",
                        p_2ch_mixer->mixed_buf, frames * FRAMESIZE_32BIT_STEREO);
            }
            if (is_TV(adev)) {
                apply_volume(gain_speaker, p_2ch_mixer->mixed_buf,
                    sizeof(uint32_t), frames * FRAMESIZE_32BIT_STEREO);
            }

            extend_channel_2_8(data_mixed, p_2ch_mixer->mixed_buf,
                    frames, 2, 8);

            if (DEBUG_DUMP) {
                aml_audio_dump_audio_bitstreams("/data/audio/exDrct.raw",
                        data_mixed, frames * out_port->cfg.frame_size);
            }
            in_port_drct->data_valid = 0;
            set_outport_data_avail(out_port, frames * out_port->cfg.frame_size);
        }
    }

    if (0) {
        aml_audio_dump_audio_bitstreams("/data/audio/data_mixed.raw",
                out_port->data_buf, mixing_len_bytes);
    }
    return 0;
}

static int mixer_add_mixing_data(struct amlAudioMixer *audio_mixer, void *input, input_port *in_port, output_port *out_port)
{
    char *data_ptr = NULL;
    mc_output_port *mc_out_port = NULL;
    aml_pcm_downmix_st *p_downmix = &audio_mixer->pcm_downmix;
    aml_pcm_mixing_st  *p_2ch_mixer = &audio_mixer->stereo_mixer;
    aml_pcm_mixing_st  *p_multich_mixer = &audio_mixer->multich_mixer;

    if (in_port->data_buf_frame_cnt < MIXER_FRAME_COUNT) {
        AM_LOGE("input port type:%s buf frames:%zu too small",
            mixerInputType2Str(in_port->enInPortType), in_port->data_buf_frame_cnt);
        return -EINVAL;
    }

    // 2ch pcm output is always exist.
    if (in_port->cfg.channelCnt != 2) {
        do_downmix_to_2ch(p_downmix, input, MIXER_FRAME_COUNT, &in_port->cfg);
        do_mixing_2ch(p_2ch_mixer->mixed_buf, p_downmix->output_buf, MIXER_FRAME_COUNT, in_port->cfg.format, out_port->cfg.format);
    } else {
        do_mixing_2ch(p_2ch_mixer->mixed_buf, input, MIXER_FRAME_COUNT, in_port->cfg.format, out_port->cfg.format);
    }

    // multich pcm output
    pthread_mutex_lock(&audio_mixer->mc_out_lock);
    mc_out_port = audio_mixer->mc_out_port;
    if (audio_mixer->mc_out_enable && mc_out_port && mc_out_port->port_status != STOPPED) {
        do_mixing_multi_ch(p_multich_mixer, input, MIXER_FRAME_COUNT, &in_port->cfg);
    }
    pthread_mutex_unlock(&audio_mixer->mc_out_lock);

    in_port->data_valid = 0;
    AM_LOGV("input port ID:%d  channels:%d", in_port->ID, out_port->cfg.channelCnt);
    return 0;
}

void mixer_enable_multich_output(struct amlAudioMixer *audio_mixer, bool enable)
{
    if (audio_mixer->mc_out_enable == enable) {
        return;
    }
    AM_LOGI("mc_out_enable %d", enable);

    pthread_mutex_lock(&audio_mixer->mc_out_lock);
    audio_mixer->mc_out_enable = enable;
    if (!enable && audio_mixer->mc_out_port) {
        // close multich alsa handle, then npcm can use it.
        free_mc_output_port(&audio_mixer->mc_out_port);
    }
    pthread_mutex_unlock(&audio_mixer->mc_out_lock);
}

/*
 * 2ch pcm output always exist, multi-ch pcm output is additional and dependent with sink device.
*/
static int mixer_config_multich_output(struct amlAudioMixer *audio_mixer, struct audioCfg *out_port_cfg)
{
    int ret = 0;
    uint32_t masks = 0;
    input_port *in_port = NULL;
    uint32_t input_max_ch = 1;
    uint32_t mc_output_ch = 2;
    uint32_t in_channelCnt = 0;
    int sink_max_channels = 2;
    audio_channel_mask_t input_max_ch_mask = AUDIO_CHANNEL_OUT_MONO;
    audio_channel_mask_t mc_output_ch_mask = AUDIO_CHANNEL_OUT_STEREO;
    struct aml_audio_device *adev = audio_mixer->adev;
    struct audioCfg *p_mixer_cfg = &audio_mixer->multich_mixer.cfg;
    struct audioCfg cfg;
    bool input_port_empty = true;
    struct aml_arc_hdmi_desc* hdmi_descs = get_arc_hdmi_cap(adev);

    sink_max_channels = hdmi_descs->pcm_fmt.max_channels;
    if (is_bypass_submix_active(adev)) {
        AM_LOGV("is_bypass_submix_active");
        return 0;
    }

    masks = audio_mixer->inportsMasks;
    while (masks) {
        in_port = mixer_get_inport_by_mask_right_first(audio_mixer, &masks);
        if (in_port == NULL) {
            continue;
        }
        input_port_empty = false;
        in_channelCnt = in_port->cfg.channelCnt;
        if (in_channelCnt > input_max_ch) {
            input_max_ch = in_channelCnt;
            input_max_ch_mask = in_port->cfg.channelMask;
        }
        if ((in_channelCnt > mc_output_ch) && (in_channelCnt <= sink_max_channels)) {
            mc_output_ch = in_channelCnt;
            mc_output_ch_mask = in_port->cfg.channelMask;
        }
    }
    if (input_port_empty) {
        input_max_ch = 2;
        input_max_ch_mask = AUDIO_CHANNEL_OUT_STEREO;
    }

    /* If not connected A2DP/Headphone, and HDMI RX/ARC supports multi-channel, so we have multi-channel output.
     * Otherwise the default 2 channel output.
    */
    if (adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP
        || adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADSET
        || adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE
        || !audio_mixer->mc_out_enable) {
        mc_output_ch = 2;
        mc_output_ch_mask = AUDIO_CHANNEL_OUT_STEREO;
    } /*else if (adev->is_netflix && !adev->aaudio_low_latency) {
        if (input_max_ch <= 6) {
            if (sink_max_channels >= 6) {
                mc_output_ch = 6;
                mc_output_ch_mask = AUDIO_CHANNEL_OUT_5POINT1;
                input_max_ch = 6;
                input_max_ch_mask = AUDIO_CHANNEL_OUT_5POINT1;
            } else {
                mc_output_ch = 2;
                mc_output_ch_mask = AUDIO_CHANNEL_OUT_STEREO;
            }
        } else {
            // do nothing
        }
    }*/
    if (adev->debug_flag && (audio_mixer->run_count % 10 == 0)) {
        bool ad2p_connected = adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP;
        AM_LOGI("mc_out_enable %d, A2DP_connected %d, is_netflix %d, input_max_ch %d, mc_output_ch %d",
                audio_mixer->mc_out_enable, ad2p_connected, adev->is_netflix, input_max_ch, mc_output_ch);
    }

    if ((input_max_ch != p_mixer_cfg->channelCnt) || (input_max_ch_mask != p_mixer_cfg->channelMask)) {
        memcpy(&cfg, out_port_cfg, sizeof(cfg));
        cfg.channelCnt  = input_max_ch;
        cfg.channelMask = input_max_ch_mask;
        cfg.frame_size  = cfg.channelCnt * audio_bytes_per_sample(cfg.format);
        deinit_multich_mixer_buffer(audio_mixer);
        init_multich_mixer_buffer(audio_mixer, &cfg, MIXER_FRAME_COUNT);
    }

    pthread_mutex_lock(&audio_mixer->mc_out_lock);
    mc_output_port *mc_out_port = audio_mixer->mc_out_port;
    if (audio_mixer->mc_out_enable && mc_output_ch != 2) {
        if (mc_out_port == NULL || mc_out_port->cfg.channelCnt != mc_output_ch) {
            memcpy(&cfg, out_port_cfg, sizeof(cfg));
            cfg.channelCnt  = mc_output_ch;
            cfg.channelMask = mc_output_ch_mask;
            cfg.frame_size  = mc_output_ch * audio_bytes_per_sample(cfg.format);

            AM_LOGI("mc output channel change to  %d", mc_output_ch);
            if (mc_out_port != NULL) {
                free_mc_output_port(&audio_mixer->mc_out_port);
            }

            mc_out_port = new_mc_output_port(&cfg, MIXER_FRAME_COUNT);
            audio_mixer->mc_out_port = mc_out_port;
            if (mc_out_port == NULL) {
                AM_LOGE("new_mc_output_port failed !");
                return -1;
            }
            mc_out_port->start(mc_out_port);
        } else {
            if (mc_out_port->port_status != ACTIVE) {
                mc_out_port->start(mc_out_port);
            }
        }
    }else if (mc_out_port) {
        if (mc_out_port->port_status != STOPPED) {
            mc_out_port->standby(mc_out_port);
        }
        mc_out_port->port_status = STOPPED;
    }
    pthread_mutex_unlock(&audio_mixer->mc_out_lock);

    return ret;
}


static int mixer_do_mixing_16bit(struct amlAudioMixer *audio_mixer)
{
    bool                        is_data_valid = false;
    input_port                  *in_port = NULL;
    output_port                 *out_port = NULL;
    struct aml_audio_device     *adev = audio_mixer->adev;
    char                        acFilePathStr[ENUM_TYPE_STR_MAX_LEN] = {0};
    uint32_t                    need_output_ch = 2;
    audio_channel_mask_t        need_output_ch_mask = AUDIO_CHANNEL_OUT_STEREO;
    uint32_t                    masks = 0;
    aml_pcm_mixing_st           *p_2ch_mixer = &audio_mixer->stereo_mixer;
    aml_pcm_mixing_st           *p_multich_mixer = &audio_mixer->multich_mixer;
    void                        *mixed_data_ptr = NULL;
    int                         mixed_data_size = 0;
    mc_output_port              *mc_out_port = NULL;
    struct aml_arc_hdmi_desc * hdmi_descs = get_arc_hdmi_cap(adev);

    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    mixer_config_multich_output(audio_mixer, &out_port->cfg);

    memset(p_2ch_mixer->mixed_buf, 0, p_2ch_mixer->mixed_buf_size);
    if (p_multich_mixer->mixed_buf) {
        memset(p_multich_mixer->mixed_buf, 0, p_multich_mixer->mixed_buf_size);
    }

    masks = audio_mixer->inportsMasks;
    while (masks) {
        in_port = mixer_get_inport_by_mask_right_first(audio_mixer, &masks);
        if (NULL == in_port || 0 == in_port->data_valid) {
            continue;
        }
        is_data_valid = true;
        if (getprop_bool("vendor.media.audiohal.indump")) {
            char acFilePathStr[ENUM_TYPE_STR_MAX_LEN];
            sprintf(acFilePathStr, "/data/audio/%s_%d", mixerInputType2Str(in_port->enInPortType), in_port->ID);
            aml_audio_dump_audio_bitstreams(acFilePathStr, in_port->data, in_port->data_len_bytes);
        }
        if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
            check_audio_level(mixerInputType2Str(in_port->enInPortType), in_port->data, in_port->data_len_bytes);
        }
        if (AML_MIXER_INPUT_PORT_PCM_DIRECT == in_port->enInPortType) {
            if (in_port->is_hwsync && in_port->bytes_to_insert < in_port->data_len_bytes) {
                pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
                retrieve_hwsync_header(audio_mixer, in_port, out_port);
                pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
            }
            if (in_port->bytes_to_insert >= in_port->data_len_bytes) {
                in_port->bytes_to_insert -= in_port->data_len_bytes;
                AM_LOGD("PCM_DIRECT inport insert mute data, still need %zu, inserted length %zu",
                        in_port->bytes_to_insert, in_port->data_len_bytes);
                continue;
            }
        }

        pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
        mixer_add_mixing_data(audio_mixer, in_port->data, in_port, out_port);
        pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    }
    static uint32_t no_data_cnt = 0;
    if (!is_data_valid && (adev->cur_out_devices & AUDIO_DEVICE_OUT_ALL_A2DP)) {
        if (adev->debug_flag) {
            AM_LOGI("inport no valid data");
        }
        /* If all input ports timeout for 1.6s and there is no data, we stop sending
         * data to the BT stack in order to save power. (200 * 8ms = 1.6s)
         */
        if (no_data_cnt >= 200) {
            return -1;
        }
        no_data_cnt++;
    } else {
        no_data_cnt = 0;
    }

    pthread_mutex_lock(&audio_mixer->mc_out_lock);
    mc_out_port = audio_mixer->mc_out_port;
    mixed_data_ptr  = p_multich_mixer->mixed_buf;
    mixed_data_size = p_multich_mixer->mixed_buf_size;
    if (mixed_data_ptr && mc_out_port && mc_out_port->port_status != STOPPED) {
        if (mixed_data_size > mc_out_port->data_buf_len) {
            AM_LOGE("mixed_data_size too large(%d > %zu), truncate", mixed_data_size, mc_out_port->data_buf_len);
            mixed_data_size = mc_out_port->data_buf_len;
        }
        memcpy(mc_out_port->data_buf, mixed_data_ptr, mixed_data_size);
        mc_out_port->bytes_avail = mixed_data_size;
    }
    pthread_mutex_unlock(&audio_mixer->mc_out_lock);

    pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
    mixed_data_ptr  = p_2ch_mixer->mixed_buf;
    mixed_data_size = p_2ch_mixer->mixed_buf_size;
    if (mixed_data_size > out_port->data_buf_len) {
        AM_LOGE("mixed_data_size too large(%d > %zu), truncate", mixed_data_size, out_port->data_buf_len);
        mixed_data_size = out_port->data_buf_len;
    }
    if (mixed_data_ptr == NULL || mixed_data_size <= 0) {
        AM_LOGE("p_2ch_mixer : invalid data_ptr(%p) or data_size(0x%x)", mixed_data_ptr, mixed_data_size);
    } else {
        memcpy(out_port->data_buf, mixed_data_ptr, mixed_data_size);
    }

    if (getprop_bool("vendor.media.audiohal.outdump")) {
        sprintf(acFilePathStr, "/data/audio/audio_mixed_%dch", need_output_ch);
        aml_audio_dump_audio_bitstreams(acFilePathStr, out_port->data_buf, mixed_data_size);
    }
    if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
        check_audio_level("audio_mixed", out_port->data_buf, mixed_data_size);
    }
    set_outport_data_avail(out_port, mixed_data_size);
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    return 0;
}

int notify_mixer_input_avail(struct amlAudioMixer *audio_mixer)
{
    for (uint8_t port_index = 0; port_index < NR_INPORTS; port_index++) {
        input_port *in_port = audio_mixer->in_ports[port_index];
        if (in_port && in_port->on_input_avail_cbk)
            in_port->on_input_avail_cbk(in_port->input_avail_cbk_data);
    }

    return 0;
}

int notify_mixer_exit(struct amlAudioMixer *audio_mixer)
{
    for (uint8_t port_index = 0; port_index < NR_INPORTS; port_index++) {
        input_port *in_port = audio_mixer->in_ports[port_index];
        if (in_port && in_port->on_notify_cbk)
            in_port->on_notify_cbk(in_port->notify_cbk_data);
    }

    return 0;
}

#define THROTTLE_TIME_US 3000
static void *mixer_32b_threadloop(void *data)
{
    struct amlAudioMixer *audio_mixer = data;
    int ret = 0;
    struct aml_audio_device *adev = (struct aml_audio_device *)adev_get_handle();

    AM_LOGI("++start");

    audio_mixer->exit_thread = 0;
    prctl(PR_SET_NAME, "amlAudioMixer32");
    aml_audio_set_cpu23_affinity();
    while (!audio_mixer->exit_thread) {
        //pthread_mutex_lock(&audio_mixer->lock);
        //mixer_procs_msg_queue(audio_mixer);
        // processing throttle
        struct timespec tval_new;
        clock_gettime(CLOCK_MONOTONIC, &tval_new);
        const uint32_t delta_us = tspec_diff_to_us(audio_mixer->tval_last_write, tval_new);
        ret = mixer_inports_read(audio_mixer);
        if (ret < 0) {
            //usleep(5000);
            AM_LOGV("data not enough, next turn");
            notify_mixer_input_avail(audio_mixer);
            continue;
            //notify_mixer_input_avail(audio_mixer);
            //continue;
        }
        notify_mixer_input_avail(audio_mixer);
        AM_LOGV("do mixing");
        mixer_do_mixing_32bit(audio_mixer);
        uint64_t tpast_us = 0;
        clock_gettime(CLOCK_MONOTONIC, &tval_new);
        tpast_us = tspec_diff_to_us(audio_mixer->tval_last_write, tval_new);
        // audio patching should not in this write
        // TODO: fix me, make compatible with source output
        if (!is_dev_patch_running(audio_mixer->adev)) {
            mixer_output_write(audio_mixer);
            mixer_update_tstamp(audio_mixer);
        }
    }

    AM_LOGI("--");
    return NULL;
}

uint32_t get_mixer_inport_count(struct amlAudioMixer *audio_mixer)
{
    return __builtin_popcount(audio_mixer->inportsMasks);
}

static bool is_submix_disable(struct amlAudioMixer *audio_mixer) {
    struct aml_audio_device *adev = audio_mixer->adev;

    if (is_bypass_submix_active(adev)) {
        return true;
    }
    return false;
}

static void mixer_config_low_latency_mode(struct amlAudioMixer *audio_mixer,
                    bool low_latency, struct audio_virtual_buf **ppstVirtualBuffer)
{
    if (audio_mixer->aaudio_low_latency == low_latency) {
        return;
    }

    AM_LOGI("change aaudio_low_latency to %d", low_latency);
    audio_mixer->aaudio_low_latency = low_latency;

    // Restart alsa output port to reduce buffer level
    mixer_outport_pcm_restart(audio_mixer);

    // Standby mc_out_port, latter it will restart by mixer_config_multich_output
    pthread_mutex_lock(&audio_mixer->mc_out_lock);
    if (audio_mixer->mc_out_enable && audio_mixer->mc_out_port) {
        audio_mixer->mc_out_port->standby(audio_mixer->mc_out_port);
    }
    pthread_mutex_unlock(&audio_mixer->mc_out_lock);

    if (ppstVirtualBuffer && *ppstVirtualBuffer) {
        audio_virtual_buf_close((void **)ppstVirtualBuffer);
        *ppstVirtualBuffer = NULL;
    }
}


static void *mixer_16b_threadloop(void *data)
{
    struct amlAudioMixer        *audio_mixer = data;
    struct audio_virtual_buf    *pstVirtualBuffer = NULL;
    struct aml_audio_device *adev = (struct aml_audio_device *)adev_get_handle();
    uint64_t buffer_frame_ns = 0;

    AM_LOGI("begin create thread");
    if (audio_mixer->mixing_enable == 0) {
        pthread_exit(0);
        AM_LOGI("mixing_enable is 0 exit thread");
        return NULL;
    }
    audio_mixer->exit_thread = 0;
    audio_mixer->run_count = 0;
    prctl(PR_SET_NAME, "amlAudioMixer16");
    aml_audio_set_cpu23_affinity();
    aml_set_thread_priority("amlAudioMixer16", audio_mixer->out_mixer_tid);
    while (!audio_mixer->exit_thread) {
        mixer_config_low_latency_mode(audio_mixer, adev->aaudio_low_latency, &pstVirtualBuffer);
        if (pstVirtualBuffer == NULL) {
            if (audio_mixer->aaudio_low_latency) {
                buffer_frame_ns = MIXER_WRITE_PERIOD_TIME_NANO * 3;
            } else {
                buffer_frame_ns = MIXER_WRITE_PERIOD_TIME_NANO * 4;
            }
            audio_virtual_buf_open((void **)&pstVirtualBuffer, "mixer_16bit_thread",
                    buffer_frame_ns, buffer_frame_ns, 0, 0);
            audio_virtual_buf_process((void *)pstVirtualBuffer, buffer_frame_ns);
        }

        pthread_mutex_lock(&audio_mixer->lock);
        mixer_inports_read(audio_mixer);
        pthread_mutex_unlock(&audio_mixer->lock);

        audio_virtual_buf_process((void *)pstVirtualBuffer, MIXER_WRITE_PERIOD_TIME_NANO);
        pthread_mutex_lock(&audio_mixer->lock);
        notify_mixer_input_avail(audio_mixer);
        mixer_do_mixing_16bit(audio_mixer);
        pthread_mutex_unlock(&audio_mixer->lock);

        if (!is_submix_disable(audio_mixer)) {
            pthread_mutex_lock(&audio_mixer->lock);
            mixer_output_write(audio_mixer);
            mixer_update_tstamp(audio_mixer);
            pthread_mutex_unlock(&audio_mixer->lock);
        }
        audio_mixer->run_count++;
    }
    if (pstVirtualBuffer != NULL) {
        audio_virtual_buf_close((void **)&pstVirtualBuffer);
    }

    AM_LOGI("exit thread");
    return NULL;
}

uint32_t mixer_get_inport_latency_frames(struct amlAudioMixer *audio_mixer, uint8_t port_index)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return in_port->get_latency_frames(in_port);
}

uint32_t mixer_get_outport_latency_frames(struct amlAudioMixer *audio_mixer)
{
    MIXER_OUTPUT_PORT port_index = audio_mixer->cur_output_port_type;
    output_port *out_port = NULL;
    R_CHECK_PARAM_LEGAL(-1, port_index, MIXER_OUTPUT_PORT_STEREO_PCM, MIXER_OUTPUT_PORT_NUM - 1, "");
    out_port = audio_mixer->out_ports[port_index];
    if (out_port == NULL) {
        AM_LOGW("out_port is null");
        return -1;
    }
    uint32_t ret = outport_get_latency_frames(out_port);
    return ret;
}

int pcm_mixer_thread_run(struct amlAudioMixer *audio_mixer)
{
    int ret = 0;
    AM_LOGI("++");
    R_CHECK_POINTER_LEGAL(-EINVAL, audio_mixer, "");
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");

    audio_format_t format = out_port->cfg.format;
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    if (audio_mixer->out_mixer_tid > 0) {
        AM_LOGE("out mixer thread already running");
        return -EINVAL;
    }
    audio_mixer->mixing_enable = 1;
    switch (format) {
    case AUDIO_FORMAT_PCM_32_BIT:
        //ret = pthread_create(&audio_mixer->out_mixer_tid, NULL, mixer_32b_threadloop, audio_mixer);
        //break;
        ALOGI("%s(), whatever 32bit output, mixing 16bit for 32 is for TV alsa output", __func__);
    case AUDIO_FORMAT_PCM_16_BIT:
        ret = pthread_create(&audio_mixer->out_mixer_tid, NULL, mixer_16b_threadloop, audio_mixer);
        break;
    default:
        AM_LOGE("format not supported");
        break;
    }
    if (ret < 0) {
        AM_LOGE("thread run failed.");
    }
    AM_LOGI("++mixing_enable:%d, format:%#x", audio_mixer->mixing_enable, format);

    return ret;
}

int pcm_mixer_thread_exit(struct amlAudioMixer *audio_mixer)
{
    audio_mixer->mixing_enable = 0;
    AM_LOGI("++ audio_mixer->mixing_enable %d", audio_mixer->mixing_enable);
    // block exit
    audio_mixer->exit_thread = 1;
    pthread_join(audio_mixer->out_mixer_tid, NULL);
    audio_mixer->out_mixer_tid = 0;

    notify_mixer_exit(audio_mixer);
    return 0;
}

struct pcm *pcm_mixer_get_pcm_handle(struct amlAudioMixer *audio_mixer)
{
    struct pcm *pcm_handle = NULL;
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return NULL;
    }
    R_CHECK_POINTER_LEGAL(NULL, out_port, "");
    pcm_handle = out_port->pcm_handle;
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    return pcm_handle;
}

struct amlAudioMixer *newAmlAudioMixer(struct aml_audio_device *adev, struct audioCfg cfg)
{
    struct amlAudioMixer *audio_mixer = NULL;
    int ret = 0;
    AM_LOGD("");

    audio_mixer = aml_audio_calloc(1, sizeof(*audio_mixer));
    R_CHECK_POINTER_LEGAL(NULL, audio_mixer, "allocate amlAudioMixer:%zu no memory", sizeof(struct amlAudioMixer));
    audio_mixer->adev = adev;
    audio_mixer->submix_standby = 1;
    mixer_set_state(audio_mixer, MIXER_IDLE);
    audio_mixer->mc_out_enable = true;

    for (int i = 0; i < MIXER_OUTPUT_PORT_NUM; i++) {
        pthread_mutex_init(&audio_mixer->outport_locks[i], NULL);
    }

    ret = init_mixer_output_port(audio_mixer, MIXER_OUTPUT_PORT_STEREO_PCM, &cfg, MIXER_FRAME_COUNT);
    if (ret < 0) {
        AM_LOGE("init mixer out port failed");
        goto err_tmp;
    }
    ret = init_stereo_mixer_buffer(audio_mixer, &cfg, MIXER_FRAME_COUNT);
    if (ret != 0) {
        AM_LOGE("init_mixer_process_buffer failed");
        goto err_state;
    }
    audio_mixer->inportsMasks = 0;
    audio_mixer->inportsAvailMasks = (1 << NR_INPORTS) - 1;
    audio_mixer->aaudio_low_latency = false;
    pthread_mutex_init(&audio_mixer->lock, NULL);
    pthread_mutex_init(&audio_mixer->inport_lock, NULL);
    pthread_mutex_init(&audio_mixer->mc_out_lock, NULL);

    return audio_mixer;

err_state:
    delete_mixer_output_port(audio_mixer, MIXER_OUTPUT_PORT_STEREO_PCM);
err_tmp:
    aml_audio_free(audio_mixer);
    return NULL;
}

void freeAmlAudioMixer(struct amlAudioMixer *audio_mixer)
{
    R_CHECK_POINTER_LEGAL((void)0, audio_mixer, "");
    pthread_mutex_destroy(&audio_mixer->lock);
    pthread_mutex_destroy(&audio_mixer->inport_lock);
    pthread_mutex_destroy(&audio_mixer->mc_out_lock);
    if (audio_mixer->cur_output_port_type == MIXER_OUTPUT_PORT_STEREO_PCM ||
        audio_mixer->cur_output_port_type == MIXER_OUTPUT_PORT_MULTI_PCM) {
        delete_mixer_output_port(audio_mixer, audio_mixer->cur_output_port_type);
    }
    for (int i = 0; i < MIXER_OUTPUT_PORT_NUM; i++) {
        pthread_mutex_destroy(&audio_mixer->outport_locks[i]);
    }
    deinit_stereo_mixer_buffer(audio_mixer);
    if (audio_mixer->mc_out_port) {
        free_mc_output_port(&audio_mixer->mc_out_port);
    }
    deinit_multich_mixer_buffer(audio_mixer);
    aml_audio_free(audio_mixer);
}

int mixer_get_presentation_position(
        struct amlAudioMixer *audio_mixer,
        uint8_t port_index,
        uint64_t *frames,
        struct timespec *timestamp)
{
    int ret = 0;
    R_CHECK_PARAM_LEGAL(-1, port_index, 0, NR_INPORTS - 1, "");
    pthread_mutex_lock(&audio_mixer->inport_lock);
    input_port *in_port = audio_mixer->in_ports[port_index];
    if (in_port == NULL) {
        AM_LOGE("in_port is null pointer, port_index:%d", port_index);
        pthread_mutex_unlock(&audio_mixer->inport_lock);
        return -EINVAL;
    }
    *frames = in_port->presentation_frames;
    *timestamp = in_port->timestamp;
    if (!is_inport_pts_valid(in_port)) {
        AM_LOGW("not valid now");
        ret = -EINVAL;
    }
    pthread_mutex_unlock(&audio_mixer->inport_lock);
    return ret;
}

int mixer_set_padding_size(
        struct amlAudioMixer *audio_mixer,
        uint8_t port_index,
        int padding_bytes)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return set_inport_padding_size(in_port, padding_bytes);
}

int mixer_outport_pcm_restart(struct amlAudioMixer *audio_mixer)
{
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    /*
    * tdm and spdif have same source for HDMI.
    * Here need to restart pcm/tdm device when select audio source tdm to HDMITx.
    * Or the sink device will no sound when it just only support pcm.
    **/
    outport_pcm_restart(out_port);
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    return 0;
}

bool has_hwsync_stream_running(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct subMixing *sm = adev->sm;
    if (sm == NULL)
        return false;
    struct amlAudioMixer *audio_mixer = sm->mixerData;
    if (audio_mixer == NULL)
        return false;

    unsigned int masks = audio_mixer->inportsMasks;

    while (masks) {
        input_port *in_port = mixer_get_inport_by_mask_right_first(audio_mixer, &masks);
        if (NULL != in_port && in_port->enInPortType == AML_MIXER_INPUT_PORT_PCM_DIRECT
            && in_port->notify_cbk_data) {
            struct aml_stream_out *out = (struct aml_stream_out *)in_port->notify_cbk_data;
            if ((out != aml_out) && out->hw_sync_mode && !out->standby)
                return true;
        }
    }
    return false;
}

void mixer_dump(int s32Fd, const struct aml_audio_device *pstAmlDev)
{
    if (NULL == pstAmlDev || NULL == pstAmlDev->sm) {
        dprintf(s32Fd, "[AML_HAL] [%s:%d] device or sub mixing is NULL !\n", __func__, __LINE__);
        return;
    }
    struct amlAudioMixer *pstAudioMixer = (struct amlAudioMixer *)pstAmlDev->sm->mixerData;
    if (NULL == pstAudioMixer) {
        dprintf(s32Fd, "[AML_HAL] [%s:%d] struct amlAudioMixer is NULL !\n", __func__, __LINE__);
        return;
    }
    dprintf(s32Fd, "[AML_HAL]---------------input port description cnt: [%d](masks:%#x)---------\n",
        get_mixer_inport_count(pstAudioMixer), pstAudioMixer->inportsMasks);
    for (uint8_t index=0; index < NR_INPORTS; index++) {
        input_port *pstInputPort = pstAudioMixer->in_ports[index];
        if (pstInputPort) {
            dprintf(s32Fd, "[AML_HAL]  input port type: %s(ID:%d)\n", mixerInputType2Str(pstInputPort->enInPortType), pstInputPort->ID);
            dprintf(s32Fd, "[AML_HAL]      Channel       : %10d     | Format            : %#10x\n",
                pstInputPort->cfg.channelCnt, pstInputPort->cfg.format);
            dprintf(s32Fd, "[AML_HAL]      FrameCnt      : %zu     | data size         : %zu Byte\n",
                pstInputPort->data_buf_frame_cnt, pstInputPort->data_len_bytes);
            dprintf(s32Fd, "[AML_HAL]      rbuf size     : %10d Byte| Avail size        : %10d Byte\n",
                pstInputPort->r_buf->size, get_buffer_read_space(pstInputPort->r_buf));
            dprintf(s32Fd, "[AML_HAL]      is_hwsync     : %10d     | start_threshold   : %10d Byte\n",
                pstInputPort->is_hwsync, pstInputPort->inport_start_threshold);
        }
    }
    dprintf(s32Fd, "[AML_HAL]---------------------output port description----------------------\n");
    output_port *pstOutPort = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(pstAudioMixer, &pstOutPort);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return ;
    }
    if (pstOutPort) {
        dprintf(s32Fd, "[AML_HAL]  output port type: %s\n", mixerOutputType2Str(pstOutPort->enOutPortType));
        dprintf(s32Fd, "[AML_HAL]      Channel       : %10d     | Format            : %#10x\n", pstOutPort->cfg.channelCnt, pstOutPort->cfg.format);
        dprintf(s32Fd, "[AML_HAL]      FrameCnt      : %zu     | data size         : %zu Byte\n",
            pstOutPort->data_buf_frame_cnt, pstOutPort->data_buf_len);
        pthread_mutex_unlock(&pstAudioMixer->outport_locks[port_index]);
    } else {
        dprintf(s32Fd, "[AML_HAL] not find output port description!!!\n");
    }
}

void mixer_using_alsa_device_dump(int s32Fd, const struct aml_audio_device *pstAmlDev)
{
    if (NULL == pstAmlDev || NULL == pstAmlDev->sm) {
        dprintf(s32Fd, "\t[AML_HAL] device or sub mixing is NULL !\n");
        return;
    }
    struct amlAudioMixer *pstAudioMixer = (struct amlAudioMixer *)pstAmlDev->sm->mixerData;
    if (NULL == pstAudioMixer) {
        dprintf(s32Fd, "\t[AML_HAL] amlAudioMixer is NULL !\n");
        return;
    }

    output_port *pstOutPort = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(pstAudioMixer, &pstOutPort);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return ;
    }

    if (pstOutPort) {
        struct pcm *pcm = pstOutPort->pcm_handle;
        if (!pcm) {
            pthread_mutex_unlock(&pstAudioMixer->outport_locks[port_index]);
            return;
        }

        aml_alsa_pcm_info_dump(pcm, s32Fd);
        pthread_mutex_unlock(&pstAudioMixer->outport_locks[port_index]);
    }
}


int mixer_set_karaoke(struct amlAudioMixer *audio_mixer, struct kara_manager *kara)
{
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    ALOGI("++%s(), set karaoke = %p", __func__, kara);
    outport_set_karaoke(out_port, kara);
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    return 0;
}

