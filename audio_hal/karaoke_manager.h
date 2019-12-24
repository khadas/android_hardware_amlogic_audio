/*
 * Copyright (C) 2018 Amlogic Corporation.
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


#ifndef _KARAOKE_MANAGER_H
#define _KARAOKE_MANAGER_H

#include <alsa_device_profile.h>
#include <alsa_device_proxy.h>

#include <aml_ringbuffer.h>
#include "sub_mixing_factory.h"

struct voice_in {
    struct audioCfg cfg;
    struct pcm *pcm;
    bool debug;
    /** Read audio buffer in from audio driver. Returns number of bytes read, or a
     *  negative status_t. If at least one frame was read prior to the error,
     *  read should return that byte count and then return an error in the subsequent call.
     */
    ssize_t (*read)(struct voice_in *in, void *buffer, size_t bytes);
    alsa_device_profile in_profile;
    alsa_device_proxy proxy;
    void *conversion_buffer;
    size_t conversion_buffer_size;
};

struct kara_manager {
    bool karaoke_start;
    struct voice_in in;
    void *buf;
    size_t buf_len;
    ring_buffer_t mic_buffer;
    ssize_t (*read)(struct kara_manager *in, void *buffer, size_t bytes);
};

int kara_open_micphone(struct audioCfg *cfg,
    struct kara_manager *k_manager, alsa_device_profile* profile);
int kara_close_micphone(struct kara_manager *kara);
int kara_mix_micphone(struct kara_manager *kara, void *buf, size_t bytes);
int kara_read_micphone(struct kara_manager *kara, void *buf, size_t bytes);

#endif
