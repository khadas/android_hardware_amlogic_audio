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

#define LOG_TAG "aml_channel_delay"
//#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <stdlib.h>
#include <string.h>

#include "aml_audio_delay.h"
#include "audio_hw_utils.h"

#define ALIGN(size, align) ((size + align - 1) & (~(align - 1)))

static aml_audio_delay_st g_stAudioOutputDelay[AML_DELAY_OUTPORT_BUTT];
static const int g_u32OutDelayMaxDefault[AML_DELAY_OUTPORT_BUTT] = {250, 250, 250, 250, 250, 250};
static bool g_bAudioDelayInit = false;

int aml_audio_delay_init()
{
    memset(&g_stAudioOutputDelay, 0, sizeof(aml_audio_delay_st)*AML_DELAY_OUTPORT_BUTT);
    g_bAudioDelayInit = true;
    return 0;
}

int aml_audio_delay_deinit()
{
    if (!g_bAudioDelayInit) {
        ALOGW("[%s:%d] audio delay not initialized", __func__, __LINE__);
        return -1;
    }
    for (unsigned int i=0; i<AML_DELAY_OUTPORT_BUTT; i++) {
        ring_buffer_release(&g_stAudioOutputDelay[i].stDelayRbuffer);
        g_stAudioOutputDelay[i].is_init_buffer = false;
    }
    g_bAudioDelayInit = false;
    return 0;
}

/*
* s32DelayTimeMs: In milliseconds
*/
int aml_audio_delay_set_time(aml_audio_delay_type_e enAudioDelayType, int s32DelayTimeMs)
{
    if (!g_bAudioDelayInit) {
        ALOGW("[%s:%d] audio delay not initialized", __func__, __LINE__);
        return -1;
    }

    if (enAudioDelayType < AML_DELAY_OUTPORT_SPEAKER || enAudioDelayType >= AML_DELAY_OUTPORT_BUTT) {
        ALOGW("[%s:%d] delay type:%d invalid, min:%d, max:%d",
            __func__, __LINE__, enAudioDelayType, AML_DELAY_OUTPORT_SPEAKER, AML_DELAY_OUTPORT_BUTT-1);
        return -1;
    }

    if (s32DelayTimeMs < 0 || s32DelayTimeMs > g_u32OutDelayMaxDefault[enAudioDelayType]) {
        ALOGW("[%s:%d] unsupport delay time:%dms, min:%dms, max:%dms",
            __func__, __LINE__, s32DelayTimeMs, 0, g_u32OutDelayMaxDefault[enAudioDelayType]);
        return -1;
    }

    if (s32DelayTimeMs > 0 && g_stAudioOutputDelay[enAudioDelayType].is_init_buffer == false)
    {
        int s32BufferSize = 0;
        unsigned int u32ChannelCnt = 2;

        if (AML_DELAY_OUTPORT_ALL == enAudioDelayType) {
            // calculate the max size for 8ch
            u32ChannelCnt = 8;
        }
        s32BufferSize = 192 * u32ChannelCnt * 4 * g_u32OutDelayMaxDefault[enAudioDelayType]; // use max buffer size
        ring_buffer_init(&g_stAudioOutputDelay[enAudioDelayType].stDelayRbuffer, s32BufferSize);
        g_stAudioOutputDelay[enAudioDelayType].is_init_buffer = true;

        if (enAudioDelayType == AML_DELAY_OUTPORT_SPDIF) {
            s32BufferSize = 48000 * 2 * 2 / 1000 * g_u32OutDelayMaxDefault[AML_DELAY_OUTPORT_SPDIF_RAW]; // use max buffer size
            ring_buffer_init(&g_stAudioOutputDelay[AML_DELAY_OUTPORT_SPDIF_RAW].stDelayRbuffer, s32BufferSize);
            g_stAudioOutputDelay[AML_DELAY_OUTPORT_SPDIF_RAW].is_init_buffer = true;

            s32BufferSize = 192000 * 2 * 2 * 4 / 1000 * g_u32OutDelayMaxDefault[AML_DELAY_OUTPORT_SPDIF_B_RAW]; // use max buffer size for AML_MAT
            ring_buffer_init(&g_stAudioOutputDelay[AML_DELAY_OUTPORT_SPDIF_B_RAW].stDelayRbuffer, s32BufferSize);
            g_stAudioOutputDelay[AML_DELAY_OUTPORT_SPDIF_B_RAW].is_init_buffer = true;
        }
    }

    g_stAudioOutputDelay[enAudioDelayType].delay_time = s32DelayTimeMs;

    // spdif/spdif raw/spdif b raw use same delay value
    if (enAudioDelayType == AML_DELAY_OUTPORT_SPDIF ||
            enAudioDelayType == AML_DELAY_OUTPORT_SPDIF_RAW ||
            enAudioDelayType == AML_DELAY_OUTPORT_SPDIF_B_RAW ) {
        g_stAudioOutputDelay[AML_DELAY_OUTPORT_SPDIF].delay_time = s32DelayTimeMs;
        g_stAudioOutputDelay[AML_DELAY_OUTPORT_SPDIF_RAW].delay_time = s32DelayTimeMs;
        g_stAudioOutputDelay[AML_DELAY_OUTPORT_SPDIF_B_RAW].delay_time = s32DelayTimeMs;
    }

    if (g_stAudioOutputDelay[enAudioDelayType].delay_time == 0) {
        // drop all data in ring buffer
        ring_buffer_release(&g_stAudioOutputDelay[enAudioDelayType].stDelayRbuffer);
        g_stAudioOutputDelay[enAudioDelayType].is_init_buffer = false;

        if (enAudioDelayType == AML_DELAY_OUTPORT_SPDIF) {
            ring_buffer_release(&g_stAudioOutputDelay[AML_DELAY_OUTPORT_SPDIF_RAW].stDelayRbuffer);
            g_stAudioOutputDelay[AML_DELAY_OUTPORT_SPDIF_RAW].is_init_buffer = false;

            ring_buffer_release(&g_stAudioOutputDelay[AML_DELAY_OUTPORT_SPDIF_B_RAW].stDelayRbuffer);
            g_stAudioOutputDelay[AML_DELAY_OUTPORT_SPDIF_B_RAW].is_init_buffer = false;
        }
    }

    ALOGI("set audio output type:%d, delay time: %dms, has init buffer: %d",
            enAudioDelayType, s32DelayTimeMs, g_stAudioOutputDelay[enAudioDelayType].is_init_buffer);
    return 0;
}

int aml_audio_delay_clear(aml_audio_delay_type_e enAudioDelayType)
{
    if (!g_bAudioDelayInit) {
        ALOGW("[%s:%d] audio delay not initialized", __func__, __LINE__);
        return -1;
    }
    if (enAudioDelayType < AML_DELAY_OUTPORT_SPEAKER || enAudioDelayType >= AML_DELAY_OUTPORT_BUTT) {
        ALOGW("[%s:%d] delay type:%d invalid, min:%d, max:%d",
            __func__, __LINE__, enAudioDelayType, AML_DELAY_OUTPORT_SPEAKER, AML_DELAY_OUTPORT_BUTT-1);
        return -1;
    }

    ring_buffer_reset(&g_stAudioOutputDelay[enAudioDelayType].stDelayRbuffer);
    return 0;
}

int aml_audio_delay_process(aml_audio_delay_type_e enAudioDelayType, void *pData, int s32Size,
        audio_format_t enFormat, uint32_t sample_rate)
{
    if (!g_bAudioDelayInit) {
        //ALOGW("[%s:%d] audio delay not initialized", __func__, __LINE__);
        return -1;
    }

    if (enAudioDelayType < AML_DELAY_OUTPORT_SPEAKER || enAudioDelayType >= AML_DELAY_OUTPORT_BUTT) {
        ALOGW("[%s:%d] delay type:%d invalid, min:%d, max:%d",
            __func__, __LINE__, enAudioDelayType, AML_DELAY_OUTPORT_SPEAKER, AML_DELAY_OUTPORT_BUTT-1);
        return -1;
    }

    if (g_stAudioOutputDelay[enAudioDelayType].is_init_buffer == false ||
            g_stAudioOutputDelay[enAudioDelayType].delay_time == 0) {
        ALOGV("%s:%d delay type:%d, 0ms delay, do nothing", __func__, __LINE__, enAudioDelayType);
        return 0;
    }

    if (s32Size == 0) {
        ALOGV("%s:%d delay type:%d, 0byte size, do nothing", __func__, __LINE__, enAudioDelayType);
        return 0;
    }

    unsigned int    u32OneMsSize = 48 * 2 * 2;
    int             s32CurNeedDelaySize = 0;
    int             s32AvailDataSize = 0;

    if (AML_DELAY_OUTPORT_ALL == enAudioDelayType) {
        if (enFormat == AUDIO_FORMAT_E_AC3) {
            u32OneMsSize = sample_rate * 2 * 2 * 4 / 1000; // sample_rate * 2ch * 2Byte * 4(high bit rate)
        } else if (enFormat == AUDIO_FORMAT_AC3) {
            u32OneMsSize = sample_rate * 2 * 2 / 1000; // sample_rate * 2ch * 2Byte
        } else {
            u32OneMsSize = sample_rate * 8 * 4 / 1000; // sample_rate * 8ch * 4Byte
        }
    } else if (AML_DELAY_OUTPORT_SPEAKER == enAudioDelayType) {
        if (AUDIO_FORMAT_PCM_16_BIT == enFormat) {
            u32OneMsSize = sample_rate * 2 * 2 / 1000;  // sample_rate * 2ch * 2Byte
        } else if (AUDIO_FORMAT_PCM_32_BIT == enFormat) {
            u32OneMsSize = sample_rate * 2 * 4 / 1000;  // sample_rate * 2ch * 4Byte
        } else {
            u32OneMsSize = sample_rate * 2 * 2 / 1000;  // sample_rate * 2ch * 2Byte
        }
    } else if (AML_DELAY_OUTPORT_SPDIF == enAudioDelayType) {
        if (AUDIO_FORMAT_PCM_16_BIT == enFormat) {
            u32OneMsSize = sample_rate * 2 * 2 / 1000;  // sample_rate * 2ch * 2Byte
        } else if (AUDIO_FORMAT_PCM_32_BIT == enFormat) {
            u32OneMsSize = sample_rate * 2 * 4 / 1000;  // sample_rate * 2ch * 4Byte [Notes: alsa only support 32bit(4Byte)]
        } else {
            u32OneMsSize = sample_rate * 2 * 2 / 1000;  // sample_rate * 2ch * 2Byte
        }
    } else if (AML_DELAY_OUTPORT_SPDIF_RAW == enAudioDelayType || AML_DELAY_OUTPORT_SPDIF_B_RAW == enAudioDelayType) {
        if (enFormat == AUDIO_FORMAT_AC3) {
            u32OneMsSize = sample_rate * 2 * 2 / 1000; // sample_rate * 2ch * 2Byte
        } else if (enFormat == AUDIO_FORMAT_E_AC3) {
            u32OneMsSize = sample_rate * 2 * 2 * 4 / 1000; // sample_rate * 2ch * 2Byte * 4(high bit rate)
        } else if (enFormat == AUDIO_FORMAT_MAT) {
            if (sample_rate == 44100 || sample_rate == 88200 || sample_rate == 176400) {
                u32OneMsSize = 176400 * 2 * 2 * 4 / 1000; // 176400 * 2ch * 2Byte * 4(high bit rate)
            } else if (sample_rate == 48000 || sample_rate == 96000 || sample_rate == 192000) {
                u32OneMsSize = 192000 * 2 * 2 * 4 / 1000; // 192000 * 2ch * 2Byte * 4(high bit rate)
            } else {
                u32OneMsSize = 192000 * 2 * 2 * 4 / 1000; // 192000 * 2ch * 2Byte * 4(high bit rate)
            }
        }
    }

    // calculate need delay total size
    s32CurNeedDelaySize = ALIGN(g_stAudioOutputDelay[enAudioDelayType].delay_time * u32OneMsSize, 16);
    // get current ring buffer delay data size
    s32AvailDataSize = ALIGN((get_buffer_read_space(&g_stAudioOutputDelay[enAudioDelayType].stDelayRbuffer) / u32OneMsSize) * u32OneMsSize, 16);

    ring_buffer_write(&g_stAudioOutputDelay[enAudioDelayType].stDelayRbuffer, (unsigned char *)pData, s32Size, UNCOVER_WRITE);
    ALOGV("%s:%d AvailDataSize:%d, enFormat:%#x, u32OneMsSize:%d", __func__, __LINE__, s32AvailDataSize, enFormat, u32OneMsSize);

    // accumulate this delay data
    if (s32CurNeedDelaySize > s32AvailDataSize) {
        int s32NeedAddDelaySize = s32CurNeedDelaySize - s32AvailDataSize;
        if (s32NeedAddDelaySize >= s32Size) {
            memset(pData, 0, s32Size);
            ALOGD("%s:%d type:%d,accumulate Data, CurNeedDelaySize:%d, need more DelaySize:%d, size:%d", __func__, __LINE__,
                enAudioDelayType, s32CurNeedDelaySize, s32NeedAddDelaySize, s32Size);
        } else {
            // splicing this pData data
            memset(pData, 0, s32NeedAddDelaySize);
            ring_buffer_read(&g_stAudioOutputDelay[enAudioDelayType].stDelayRbuffer, (unsigned char *)pData+s32NeedAddDelaySize, s32Size-s32NeedAddDelaySize);
            ALOGD("%s:%d type:%d accumulate part pData CurNeedDelaySize:%d, need more DelaySize:%d, size:%d", __func__, __LINE__,
                enAudioDelayType, s32CurNeedDelaySize, s32NeedAddDelaySize, s32Size);
        }
    // decrease this delay data
    } else if (s32CurNeedDelaySize < s32AvailDataSize) {
        unsigned int u32NeedDecreaseDelaySize = s32AvailDataSize - s32CurNeedDelaySize;
        // drop this delay data
        unsigned int    u32ClearedSize = 0;
        for (;u32ClearedSize < u32NeedDecreaseDelaySize; ) {
            unsigned int u32ResidualClearSize = u32NeedDecreaseDelaySize - u32ClearedSize;
            if (u32ResidualClearSize > (unsigned int)s32Size) {
                ring_buffer_read(&g_stAudioOutputDelay[enAudioDelayType].stDelayRbuffer, (unsigned char *)pData, s32Size);
                u32ClearedSize += s32Size;
            } else {
                ring_buffer_read(&g_stAudioOutputDelay[enAudioDelayType].stDelayRbuffer, (unsigned char *)pData, u32ResidualClearSize);
                break;
            }
        }
        ring_buffer_read(&g_stAudioOutputDelay[enAudioDelayType].stDelayRbuffer, (unsigned char *)pData, s32Size);
        ALOGD("%s:%d type:%d drop delay data, CurNeedDelaySize:%d, NeedDecreaseDelaySize:%d, size:%d", __func__, __LINE__,
            enAudioDelayType, s32CurNeedDelaySize, u32NeedDecreaseDelaySize, s32Size);
    } else {
        ring_buffer_read(&g_stAudioOutputDelay[enAudioDelayType].stDelayRbuffer, (unsigned char *)pData, s32Size);
        ALOGV("%s:%d do nothing, CurNeedDelaySize:%d, size:%d", __func__, __LINE__, s32CurNeedDelaySize, s32Size);
    }

    return 0;
}

aml_audio_delay_type_e out_dev_convert_to_delay_type(aml_audio_out_dev_type_e type)
{
    aml_audio_delay_type_e ret = AML_DELAY_OUTPORT_SPEAKER;
    switch (type) {
    case AML_AUDIO_OUT_DEV_TYPE_SPEAKER:
        ret = AML_DELAY_OUTPORT_SPEAKER;
        break;
    case AML_AUDIO_OUT_DEV_TYPE_SPDIF:
        ret = AML_DELAY_OUTPORT_SPDIF;
        break;
    case AML_AUDIO_OUT_DEV_TYPE_HEADPHONE:
        ret = AML_DELAY_OUTPORT_HEADPHONE;
        break;
    default:
        AM_LOGW("unsupport type:%d, return SPK.", type);
        break;
    }
    return ret;
}
