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


#ifndef AML_AUDIO_DELAY_H
#define AML_AUDIO_DELAY_H
#include <cutils/log.h>
#include "aml_audio_stream.h"
#include "audio_hw.h"

#define OUTPUT_DELAY_MAX_MS     1000
#define OUTPUT_DELAY_MIN_MS     0

int aml_audiodelay_init(struct audio_hw_device *dev);
int aml_audiodelay_close(struct audio_hw_device *dev);
int aml_audiodelay_process(struct audio_hw_device *dev, void * in_data, int size, audio_format_t format);


#endif

