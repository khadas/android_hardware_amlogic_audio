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
#define LOG_TAG "audio_hw_hal_cfgdata"


#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <cutils/log.h>
#include <string.h>
#include <hardware/audio.h>
#include <audio_hw_utils.h>

#include "aml_config_data.h"


#define AML_AUDIO_CONFIG_FILE_PATH "/vendor/etc/aml_audio_config.json"
cJSON *audio_config_jason = NULL;

int aml_audio_config_parser()
{
    audio_config_jason = aml_config_parser(AML_AUDIO_CONFIG_FILE_PATH);
    if (audio_config_jason) {
        return 0;
    } else {
        return -1;
    }
}

int aml_get_jason_int_value(char* key,int defvalue)
{
    cJSON *temp = NULL;
    int value = defvalue;
    if (audio_config_jason) {
        temp = cJSON_GetObjectItem(audio_config_jason, key);
        aml_printf_cJSON(key, temp);
        if (temp) {
            value = temp->valueint;
        }
    }
    return value;
}

bool aml_get_codec_support(char* aformat)
{
    cJSON *item = NULL;
    cJSON *list = NULL;
    cJSON *format = NULL;
    cJSON *support = NULL;
    int array_size = 0;
    ALOGI("aformat %s!\n", aformat);
    if (audio_config_jason) {
        list = cJSON_GetObjectItem(audio_config_jason, "Codec_Support_List");
        if (!list || !cJSON_IsArray(list)) {
            ALOGI("no Codec_Support_List or not a Array!");
            return false;
        }

        array_size = cJSON_GetArraySize(list);
        for (int i=0; i< array_size; i++) {
            item = cJSON_GetArrayItem(list, i);
            format = cJSON_GetObjectItem(item, "Format");
            if (!format) {
                ALOGI("no format string!");
                continue;
            }
            if (strcmp(aformat, format->valuestring) == 0) {
                support = cJSON_GetObjectItem(item, "Support");
                if (!support) {
                    ALOGI("no support string!\n");
                    return false;
                } else {
                    ALOGI("support:%d", support->type == cJSON_True);
                    return (support->type == cJSON_True);
                }
            }
        }

    }
    return false;
}

void aml_audio_board_config_init(struct audio_board_config *config)
{
    config->hdmitx_src = -1;
    config->hdmitx_hbr_src = -1;
    config->hdmitx_multi_ch_src = -1;
    config->cpux_affinity_support = -1;

#if defined(TV_AUDIO_OUTPUT)
    config->default_alsa_ch =  aml_audio_get_default_alsa_output_ch();
#else
    /* for stb/ott, fixed 2 channels speaker output for alsa*/
    config->default_alsa_ch = 2;
#endif

    if (aml_audio_config_parser() == 0) {
        config->hdmitx_src = aml_get_jason_int_value("HDMITX_Src_Select", -1);
        if (config->hdmitx_src != -1) {
            config->spdif_independent = true;
        }

        config->hdmitx_multi_ch_src = aml_get_jason_int_value("HDMITX_Multi_CH_Src_Select", -1);
        config->hdmitx_hbr_src = aml_get_jason_int_value("HDMITX_HBR_Src_Select", -1);
        config->default_alsa_ch = aml_get_jason_int_value("ALSA_Speaker_Channels", config->default_alsa_ch);
        config->ms12_output_mask = aml_get_jason_int_value("MS12_Output_Masks", 0);
        config->DTS_output_ch = aml_get_jason_int_value("DTS_Output_Channels", 0);
        config->cpux_affinity_support = aml_get_jason_int_value("CPUX_Affinity_Support", -1);
    }
}

