/*
 * Copyright (C) 2017 Amlogic Corporation.
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

#ifndef _AML_AUDIO_TYPES_DEF_H_
#define _AML_AUDIO_TYPES_DEF_H_

#include <system/audio.h>

struct pcm_info {
    int sample_rate;
    int channel_num;
    int bytes_per_sample;
    int bitstream_type;
    int is_dolby_atmos;
    int lorocmixlev;
    int lorosurmixlev;
    char padding[200];
};

struct audio_data_info {
    audio_format_t audio_format;
    audio_format_t sub_format;
    audio_channel_mask_t channel_mask;
    //uint32_t      samplerate;
};

typedef struct audio_data_info audio_data_info_t;


#endif /* _AML_AUDIO_TYPES_DEF_H_ */
