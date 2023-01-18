/*
 * Copyright (C) 2019 Amlogic Corporation.
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
#ifndef _AML_CONFIG_DATA_H_
#define _AML_CONFIG_DATA_H_

#include <stdbool.h>
#include <aml_alsa_mixer.h>

#include "aml_config_parser.h"

/* board specific json configs */
struct audio_board_config {
    int hdmitx_src; /* HDMITX src select for TDM */
    bool spdif_independent;  /*spdif output can be independent with HDMI output*/
    enum AML_SRC_TO_HDMITX hdmitx_multi_ch_src;
    enum AML_SRC_TO_HDMITX hdmitx_hbr_src;
    /*
    defined for default speaker output channels:
    stb: default 2 channels.
    tv:  default 8 channels(2ch speaker,2ch spdif,2ch headphone)
    soundbar:depending on the prop defined by device
    */
    int default_alsa_ch;
    int ms12_output_mask;
    int DTS_output_ch;
    int cpu4_affinity_support;
};

int aml_audio_config_parser();
int aml_get_jason_int_value(char* key,int defvalue);
bool aml_get_codec_support(char* aformat);
void aml_audio_board_config_init(struct audio_board_config *config);

#endif
