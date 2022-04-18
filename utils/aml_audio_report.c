/*
 * Copyright (C) 2022 Amlogic Corporation.
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


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <cutils/log.h>
#include <aml_dump_debug.h>
#include "aml_audio_report.h"
#include "aml_audio_sysfs.h"

#undef  LOG_TAG
#define LOG_TAG "audio_hw_primary"
#define VAL_LEN 64

int get_audio_info_enable(int dump_type) {
    int value = 0;
    value = get_debug_value(AML_DEBUG_AUDIOINFO_REPORT);
    return (value & dump_type);
}


void UpdateDecodedInfo_SampleRate_ChannelNum_ChannelConfiguration(int samplerate, int ch_num) {
    char sysfs_buf[VAL_LEN] = {0};
    sprintf(sysfs_buf, "samplerate %d", samplerate);
    sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
    sprintf(sysfs_buf, "ch_num %d", ch_num);
    sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
    UpdateDecodeInfo_ChannelConfiguration(sysfs_buf, ch_num);
}

void UpdateDecodedInfo_DecodedFrames(uint64_t decoded_frames) {
    char sysfs_buf[VAL_LEN] = {0};
    sprintf(sysfs_buf, "decoded_frames %" PRIu64 " ", decoded_frames);
    sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
}

void UpdateDecodedInfo_DecodedErr(unsigned int decoded_err) {
    char sysfs_buf[VAL_LEN] = {0};
    sprintf(sysfs_buf, "decoded_err %d", decoded_err);
    sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
}

void UpdateDecodeInfo_ChannelConfiguration(char *sysfs_buf, int ch_num) {
    switch (ch_num) {
        case 1:
            sprintf (sysfs_buf, "ch_configuration %d", TIF_HAL_PLAYBACK_AUDIO_SOURCE_CHANNEL_CONFIGURATION_MONO);
            break;
        case 2:
            sprintf (sysfs_buf, "ch_configuration %d", TIF_HAL_PLAYBACK_AUDIO_SOURCE_CHANNEL_CONFIGURATION_STEREO);
            break;
        case 3:
            sprintf (sysfs_buf, "ch_configuration %d", TIF_HAL_PLAYBACK_AUDIO_SOURCE_CHANNEL_CONFIGURATION_L_C_R);
            break;
        case 4:
            sprintf (sysfs_buf, "ch_configuration %d", TIF_HAL_PLAYBACK_AUDIO_SOURCE_CHANNEL_CONFIGURATION_L_R_SL_RS);
            break;
        case 5:
            sprintf (sysfs_buf, "ch_configuration %d", TIF_HAL_PLAYBACK_AUDIO_SOURCE_CHANNEL_CONFIGURATION_L_C_R_SL_SR);
            break;
        case 6:
            sprintf (sysfs_buf, "ch_configuration %d", TIF_HAL_PLAYBACK_AUDIO_SOURCE_CHANNEL_CONFIGURATION_5_1);
            break;
        case 8:
            sprintf (sysfs_buf, "ch_configuration %d", TIF_HAL_PLAYBACK_AUDIO_SOURCE_CHANNEL_CONFIGURATION_7_1);
            break;
        default:
            ALOGE("unsupport yet");
            break;
    }
    sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
}



