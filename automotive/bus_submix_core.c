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

#define LOG_TAG "audio_hw_automotive_submix"
#define LOG_NDEBUG 0

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "bus_submix_core.h"
#include "amlAudioMixer.h"
#include "audio_port.h"
#include "audio_hw_utils.h"
#include "audio_hw.h"
#include "aml_channel_index.h"

input_port * create_mixer_port(BusSubMixCore *mixCore,
                            struct audioCfg *audCfg,
                            audio_output_flags_t flags,
                            const struct mixer_callbacks *callbacks,
                            uint64_t frames_written,
                            float volume)
{
    int ret = 0;
    input_port *in_port = NULL;
    struct amlAudioMixer *audio_mixer = mixCore->audio_mixer;

    if (!mixCore || !audio_mixer || !callbacks) {
        AM_LOGE("Error, Invalid param: mixCore:%p callbacks:%p", mixCore, callbacks);
        return NULL;
    }

    in_port = new_input_port(MIXER_FRAME_COUNT, audCfg, flags, volume, false, false);
    if (in_port == NULL) {
        AM_LOGE("new_input_port fail return NULL!");
        return NULL;
    }
    //user already write out frames sum
    in_port->initial_frames = frames_written;
    set_port_notify_cbk(in_port, callbacks->onNotifyEvent, callbacks->user);
    set_port_input_avail_cbk(in_port, callbacks->onInputAvail, callbacks->user);
    if (callbacks->onMetaCbk && callbacks->user) {
        in_port->is_hwsync = true;
        set_port_meta_data_cbk(in_port, callbacks->onMetaCbk, callbacks->user);
    }

    ret = add_new_input_port_on_mixer(audio_mixer, in_port);
    if (ret < 0) {
        AM_LOGE("Error, failed add new input_port!");
        free_input_port(in_port);
        return NULL;
    }

    mixCore->in_port_count++;
    AM_LOGI("in_port:%p, index:%d, count:%d buffer_frames:%d, frame_write_sum:%" PRId64 "",
        in_port, in_port->ID, mixCore->in_port_count, MIXER_FRAME_COUNT, frames_written);
    return in_port;
}

int delete_mixer_port(BusSubMixCore *mixCore, input_port * in_port)
{
    if (!in_port || !mixCore || !mixCore->audio_mixer) {
        AM_LOGW("Invalid in_port, return!");
        return 0;
    }

    delete_mixer_input_port(mixCore->audio_mixer, in_port->ID);
    mixCore->in_port_count--;
    AM_LOGI("in_port:%p index:%d count:%d", in_port, in_port->ID, mixCore->in_port_count);
    return 0;
}

int set_inport_channel_mux_table(input_port *in_port, int bus_id)
{
    int ret = 0;
    int source_channels = in_port->cfg.channelCnt;

    ret = set_channel_table_from_bus_id(in_port->mux_channel_table, bus_id, source_channels);
    if (ret < 0) {
        AM_LOGE("Fail, bus_id:%d source_channels:%d", bus_id, source_channels);
        return ret;
    }

    int mux_channels = get_channels_from_channel_table(in_port->mux_channel_table, MAX_MAIN_CHANNEL_COUNT);
    in_port->mux_channels = mux_channels;
    return 0;
}

int get_mixer_port_presentation(BusSubMixCore *mixCore, input_port* in_port, uint64_t *frames, struct timespec *timestamp)
{
    int ret = 0;
    ret = mixer_get_presentation_position(mixCore->audio_mixer, in_port->ID, frames, timestamp);
    return ret;
}

BusSubMixCore *get_bus_mix_core(struct aml_audio_device *adev)
{
    if (adev->bus_mixer_core) {
        return adev->bus_mixer_core;
    }

    if (!adev->sm) {
        initHalSubMixing(&adev->sm, MIXER_LPCM, adev, false);
    }

    BusSubMixCore *mixCore = aml_audio_calloc(1, sizeof(BusSubMixCore));
    if (!mixCore) {
        AM_LOGE("No memory, return!");
        return NULL;
    }
    mixCore->audio_mixer = (struct amlAudioMixer*)adev->sm->mixerData;
    adev->bus_mixer_core = mixCore;
    return mixCore;
}

void release_bus_mix_core(struct aml_audio_device *adev)
{
    if (!adev->bus_mixer_core) {
        return;
    }

    if (adev->sm) {
        deleteHalSubMixing(adev->sm);
    }

    aml_audio_free(adev->bus_mixer_core);
    adev->bus_mixer_core = NULL;
}