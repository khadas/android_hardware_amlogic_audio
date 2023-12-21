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

#ifndef BUS_PLAYBACK_MIX_PORT_H_
#define BUS_PLAYBACK_MIX_PORT_H_

#include <sys/types.h>

struct playback_handler_base;
struct audio_stream_out;
struct aml_audio_device;

struct playback_handler_base *create_bus_playback_handler(struct aml_audio_device* adev,
                                                struct audio_stream_out *stream_out,
                                                struct audio_config *config,
                                                int bus_id);

int delete_bus_playback_handler(struct playback_handler_base *handle);

#endif
