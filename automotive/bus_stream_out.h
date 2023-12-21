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

#ifndef BUS_STREAM_OUT_H_
#define BUS_STREAM_OUT_H_

#include <system/audio.h>

struct audio_hw_device;
struct audio_stream_out;

int adev_open_bus_output_stream(struct audio_hw_device *dev,
                                audio_io_handle_t handle,
                                audio_devices_t devices,
                                audio_output_flags_t flags,
                                struct audio_config *config,
                                struct audio_stream_out **stream_out,
                                const char *address);

void adev_close_bus_output_stream(struct audio_hw_device *dev,
                            struct audio_stream_out *stream);
#endif