
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

#ifndef SUB_MIX_CORE_H_
#define SUB_MIX_CORE_H_

#include <sys/types.h>

#include "audio_port.h"

struct aml_audio_device;
struct amlAudioMixer;

typedef struct bus_submix_core {
    struct aml_audio_device *adev;
    struct amlAudioMixer *audio_mixer;
    int in_port_count;
} BusSubMixCore;

typedef int (*on_notify_t)(void *data);
typedef int (*on_input_avail_t)(void *data);

typedef struct mixer_callbacks
{
    void *user;
    on_input_avail_t onInputAvail;
    on_notify_t onNotifyEvent;
    meta_data_cbk_t onMetaCbk;
} MixCallbacks;

input_port * create_mixer_port(BusSubMixCore *mixCore,
                            struct audioCfg *audCfg,
                            audio_output_flags_t flags,
                            const struct mixer_callbacks *callbacks,
                            uint64_t frames_written,
                            float volume);

int delete_mixer_port(BusSubMixCore *mixCore, input_port * in_port);

int set_inport_channel_mux_table(input_port *in_port, int bus_id);

int get_mixer_port_presentation(BusSubMixCore *mixCore, input_port* in_port, uint64_t *frames, struct timespec *timestamp);

BusSubMixCore *get_bus_mix_core(struct aml_audio_device *adev);
void release_bus_mix_core(struct aml_audio_device *adev);

#endif
