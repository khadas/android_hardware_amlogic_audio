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

#ifndef _SPDIF_OUT_API_H_
#define _SPDIF_OUT_API_H_

#include "audio_hw.h"
#define IEC_DD_FRAME_SIZE 1536
#define IEC_DDP_FRAME_SIZE 6144

typedef struct {
    audio_format_t audio_format;
    audio_format_t sub_format;
    uint32_t      rate;
    int           data_ch;  /*the channel for transfer the pcm data, eg, the pcm data is 8ch, but only contains 2ch audio data*/
    audio_channel_mask_t channel_mask;  /*exactly audio channel in the data*/
    bool          mute;
    bool is_dtscd;
} spdif_config_t;

int aml_audio_get_spdif_port(eMixerSpdif_Format spdif_format);
int aml_audio_get_spdifa_port(void);
void aml_audio_set_spdif_format(int spdif_port, eMixerSpdif_Format aml_spdif_format, struct aml_stream_out *stream);

void aml_audio_select_spdif_to_hdmi(int spdif_select);

int aml_audio_spdifout_open(void **pphandle, spdif_config_t *spdif_config);

int aml_audio_spdifout_process(void *phandle, void *buffer, size_t byte);

int aml_audio_spdifout_close(void *phandle);

int aml_audio_spdifout_mute(void *phandle, bool b_mute);

int aml_audio_spdifout_pause(void *phandle);

int aml_audio_spdifout_resume(void *phandle);

int aml_audio_spdifout_get_delay(void *phandle);

/**
* @brief convert the audio channel mask layout to earc channel allocation and set to arc
* @param  phandle:   spdif handle
*         channel_mask:  the channel mask for the data
* @return [success]: 0 set success
*         [fail]: -1
*/
int aml_audio_spdifout_config_earc_ca(void *phandle, int channel_mask);


#endif
