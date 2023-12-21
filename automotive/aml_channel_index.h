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

#ifndef AML_CHANNEL_INDEX_H_
#define AML_CHANNEL_INDEX_H_

#include <sys/types.h>
#include <cutils/bitops.h>

#include "audio_hw_utils.h"

enum am_channel_index_mask_t {
    AML_CHANNEL_INDEX_MASK_1              = 0x1,
    AML_CHANNEL_INDEX_MASK_2              = 0x1 << 1,
    AML_CHANNEL_INDEX_MASK_3              = 0x1 << 2,
    AML_CHANNEL_INDEX_MASK_4              = 0x1 << 3,
    AML_CHANNEL_INDEX_MASK_5              = 0x1 << 4,
    AML_CHANNEL_INDEX_MASK_6              = 0x1 << 5,
    AML_CHANNEL_INDEX_MASK_7              = 0x1 << 6,
    AML_CHANNEL_INDEX_MASK_8              = 0x1 << 7,
    AML_CHANNEL_INDEX_MASK_9              = 0x1 << 8,
    AML_CHANNEL_INDEX_MASK_10             = 0x1 << 9,
    AML_CHANNEL_INDEX_MASK_11             = 0x1 << 10,
    AML_CHANNEL_INDEX_MASK_12             = 0x1 << 11,
    AML_CHANNEL_INDEX_MASK_13             = 0x1 << 12,
    AML_CHANNEL_INDEX_MASK_14             = 0x1 << 13,
    AML_CHANNEL_INDEX_MASK_15             = 0x1 << 14,
    AML_CHANNEL_INDEX_MASK_16             = 0x1 << 15,
    AML_CHANNEL_INDEX_MASK_17             = 0x1 << 16,
    AML_CHANNEL_INDEX_MASK_18             = 0x1 << 17,
    AML_CHANNEL_INDEX_MASK_19             = 0x1 << 18,
    AML_CHANNEL_INDEX_MASK_20             = 0x1 << 19,
    AML_CHANNEL_INDEX_MASK_21             = 0x1 << 20,
    AML_CHANNEL_INDEX_MASK_22             = 0x1 << 21,
    AML_CHANNEL_INDEX_MASK_23             = 0x1 << 22,
    AML_CHANNEL_INDEX_MASK_24             = 0x1 << 23,
};

enum am_bus_id_t
{
    AM_BUS_ID_0 = 0,
    AM_BUS_ID_1,
    AM_BUS_ID_2,
    AM_BUS_ID_3,
    AM_BUS_ID_4,
    AM_BUS_ID_5,
    AM_BUS_ID_6,
    AM_BUS_ID_MAX,
};

//set channel map table
static int set_channel_table_from_bus_id(uint32_t *channel_mask_table, int bus_id, int channelNum)
{
    switch(bus_id) {
    case AM_BUS_ID_0:
        channel_mask_table[0] = AML_CHANNEL_INDEX_MASK_1;
        if (channelNum >= 2) {
            channel_mask_table[1] = AML_CHANNEL_INDEX_MASK_2;
        }
        break;
    case AM_BUS_ID_1:
        channel_mask_table[0] = AML_CHANNEL_INDEX_MASK_3;
        if (channelNum >= 2) {
            channel_mask_table[1] = AML_CHANNEL_INDEX_MASK_4;
        }
        break;
    case AM_BUS_ID_2:
        channel_mask_table[0] = AML_CHANNEL_INDEX_MASK_5;
        if (channelNum >= 2) {
            channel_mask_table[1] = AML_CHANNEL_INDEX_MASK_6;
        }
        break;
    case AM_BUS_ID_3:
        channel_mask_table[0] = AML_CHANNEL_INDEX_MASK_7;
        if (channelNum >= 2) {
            channel_mask_table[1] = AML_CHANNEL_INDEX_MASK_8;
        }
        break;
    default:
        channel_mask_table[0] = AML_CHANNEL_INDEX_MASK_5;
        if (channelNum >= 2) {
            channel_mask_table[1] = AML_CHANNEL_INDEX_MASK_6;
        }
        AM_LOGI("Invalid bus_id:%d Using default channel mask", bus_id);
        break;
    }

    if (channelNum >= 3) {
        switch (channelNum) {
        case 8:
            channel_mask_table[0] = AML_CHANNEL_INDEX_MASK_1;
            channel_mask_table[1] = AML_CHANNEL_INDEX_MASK_2;
            channel_mask_table[2] = AML_CHANNEL_INDEX_MASK_3;
            channel_mask_table[3] = AML_CHANNEL_INDEX_MASK_4;
            channel_mask_table[4] = AML_CHANNEL_INDEX_MASK_5;
            channel_mask_table[5] = AML_CHANNEL_INDEX_MASK_6;
            channel_mask_table[6] = AML_CHANNEL_INDEX_MASK_7;
            channel_mask_table[7] = AML_CHANNEL_INDEX_MASK_8;
            break;
        case 6:
            channel_mask_table[0] = AML_CHANNEL_INDEX_MASK_1;
            channel_mask_table[1] = AML_CHANNEL_INDEX_MASK_2;
            channel_mask_table[2] = AML_CHANNEL_INDEX_MASK_3;
            channel_mask_table[3] = AML_CHANNEL_INDEX_MASK_4;
            channel_mask_table[4] = AML_CHANNEL_INDEX_MASK_5;
            channel_mask_table[5] = AML_CHANNEL_INDEX_MASK_6;
            break;
        case 4:
            channel_mask_table[0] = AML_CHANNEL_INDEX_MASK_1;
            channel_mask_table[1] = AML_CHANNEL_INDEX_MASK_2;
            channel_mask_table[2] = AML_CHANNEL_INDEX_MASK_3;
            channel_mask_table[3] = AML_CHANNEL_INDEX_MASK_4;
            break;
        default:
            AM_LOGE("Un-support channel count:%d", channelNum);
            break;
        }
    }
    for (int i = 0; i < channelNum; i++) {
        AM_LOGI("channels:%d bus_id:%d channel_tab[%d]=0x%x", channelNum, bus_id, i, channel_mask_table[i]);
    }
    return 0;
}

static inline int set_bus_out_main_channel_mask(uint32_t *channel_mask_table, uint32_t channelNum)
{
    int ret = 0;

    if ((channelNum < 4) || (channelNum > 8)) {
        AM_LOGE("Un-support channels:%d", channelNum);
        return -1;
    }

    switch (channelNum) {
    case 8:
        channel_mask_table[0] = AML_CHANNEL_INDEX_MASK_1;
        channel_mask_table[1] = AML_CHANNEL_INDEX_MASK_2;
        channel_mask_table[2] = AML_CHANNEL_INDEX_MASK_3;
        channel_mask_table[3] = AML_CHANNEL_INDEX_MASK_4;
        channel_mask_table[4] = AML_CHANNEL_INDEX_MASK_5;
        channel_mask_table[5] = AML_CHANNEL_INDEX_MASK_6;
        channel_mask_table[6] = AML_CHANNEL_INDEX_MASK_7;
        channel_mask_table[7] = AML_CHANNEL_INDEX_MASK_8;
        break;
    case 6:
        channel_mask_table[0] = AML_CHANNEL_INDEX_MASK_1;
        channel_mask_table[1] = AML_CHANNEL_INDEX_MASK_2;
        channel_mask_table[2] = AML_CHANNEL_INDEX_MASK_3;
        channel_mask_table[3] = AML_CHANNEL_INDEX_MASK_4;
        channel_mask_table[4] = AML_CHANNEL_INDEX_MASK_5;
        channel_mask_table[5] = AML_CHANNEL_INDEX_MASK_6;
        break;
    case 4:
        channel_mask_table[0] = AML_CHANNEL_INDEX_MASK_1;
        channel_mask_table[1] = AML_CHANNEL_INDEX_MASK_2;
        channel_mask_table[2] = AML_CHANNEL_INDEX_MASK_3;
        channel_mask_table[3] = AML_CHANNEL_INDEX_MASK_4;
        break;
    default:
        ret = -1;
        AM_LOGE("Error invalid channel count:%d", channelNum);
        break;
    }
    for (int i = 0; i < (int)channelNum; i++) {
        AM_LOGI("channels:%d channel_tab[%d]=0x%x", channelNum, i, channel_mask_table[i]);
    }
    return 0;
}

static inline uint32_t get_channels_from_channel_table(uint32_t *channel_table, int max_channels)
{
    int table_channels = 0;
    for (int i = 0; i < max_channels; i++) {
        if (channel_table[i] != 0) {
            table_channels++;
        }
    }
    return table_channels;
}

#endif
