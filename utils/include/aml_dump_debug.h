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

#ifndef __AML_DUMP_DEBUG_H__
#define __AML_DUMP_DEBUG_H__

#define CC_DUMP_SRC_TYPE_INPUT         (0)
#define CC_DUMP_SRC_TYPE_OUTPUT        (1)
#define CC_DUMP_SRC_TYPE_INPUT_PARSE   (2)

typedef enum AML_DUMP_DEBUG_INFO {
    /*debug enum items*/
    AML_DEBUG_AUDIOHAL_DEBUG,
    AML_DEBUG_AUDIOHAL_LEVEL_DETECT,
    AML_DEBUG_AUDIOHAL_HW_SYNC,
    AML_DEBUG_AUDIOHAL_ALSA,
    AML_DEBUG_AUDIOHAL_SYNCPTS,
    AML_DEBUG_AUDIOHAL_MATENC,
    AML_DEBUG_AUDIOHAL_TRACE,
    AML_DEBUG_AUDIOINFO_REPORT,
    AML_DEBUG_AUDIOHAL_AUT,
    AML_DEBUG_AUDIOHAL_EDID,

    /*dump enum items*/
    AML_DUMP_AUDIOHAL_MS12,
    AML_DUMP_AUDIOHAL_ALSA,
    AML_DUMP_AUDIOHAL_TV,
    AML_DUMP_AUDIO_STREAM,
    AML_DUMP_AUDIOHAL_ASYNC_WRITE,

    AML_DEBUG_DUMP_MAX,
} AML_DUMP_DEBUG_INFO_T;

typedef struct dump_debug_item {
    AML_DUMP_DEBUG_INFO_T index;
    char name[128];
    int value;
} dump_debug_item_t;

extern dump_debug_item_t aml_debug_items[];

void DoDumpData(const void *data_buf, int size, int aud_src_type);

/*
 * Define the mask for AML_DEBUG_AUDIOHAL_DEBUG_PROPERTY("vendor.media.audio.hal.debug")
 *      usage:
 *          debug audio hal's INPUT:            setprop "vendor.media.audio.hal.debug" 0x1
 *          debug audio hal's decoder:          setprop "vendor.media.audio.hal.debug" 0x2
 *          debug audio hal's POSTPROCESS:      setprop "vendor.media.audio.hal.debug" 0x4
 *          debug audio hal's OUTPUT:           setprop "vendor.media.audio.hal.debug" 0x8
 *          debug audio hal's ALSA:             setprop "vendor.media.audio.hal.debug" 0x10
 *          debug audio hal's AVSYNC:           setprop "vendor.media.audio.hal.debug" 0x20
 *          debug audio hal's PASSTHROUGH:      setprop "vendor.media.audio.hal.debug" 0x40
 */
#define AUDIO_HAL_DEBUG_INPUT               (0x1)
#define AUDIO_HAL_DEBUG_DECODER             (0x2)
#define AUDIO_HAL_DEBUG_POSTPROCESS         (0x4)
#define AUDIO_HAL_DEBUG_OUTPUT              (0x8)
#define AUDIO_HAL_DEBUG_ALSA                (0x10)
#define AUDIO_HAL_DEBUG_AVSYNC              (0x20)
#define AUDIO_HAL_DEBUG_PASSTHROUGH         (0x40)
#define DUMP_AUDIO_INFO_DECODE              (0x1000)  //use to enable the audio_report_info prop

/*define debug enum string*/
#define AML_DEBUG_AUDIOHAL_DEBUG_PROPERTY           "vendor.media.audio.hal.debug"
#define AML_DEBUG_AUDIOHAL_LEVEL_DETECT_PROPERTY    "vendor.media.audiohal.level"
#define AML_DEBUG_AUDIOHAL_HW_SYNC_PROPERTY         "vendor.media.audiohal.hwsync"
#define AML_DEBUG_AUDIOHAL_ALSA_PROPERTY            "vendor.media.audio.hal.alsa"
#define AML_DEBUG_AUDIOHAL_SYNCPTS_PROPERTY         "vendor.media.audio.hal.syncpts"
#define AML_DEBUG_AUDIOHAL_MATENC_PROPERTY          "vendor.media.audiohal.matenc.debug"
#define AML_DEBUG_AUDIOHAL_TRACE_PROPERTY           "vendor.media.audiohal.trace.debug"
#define AML_DEBUG_AUDIOINFO_REPORT_PROPERTY         "vendor.media.audio.info.report.debug"
#define AML_DEBUG_AUDIOHAL_AUT_PROPERTY             "vendor.media.audiohal.aut"
#define AML_DEBUG_AUDIOHAL_EDID_PROPERTY            "vendor.media.audiohal.edid"
/*define dump enum string*/
#define AML_DUMP_AUDIOHAL_MS12_PROPERTY             "vendor.media.audiohal.ms12dump"
#define AML_DUMP_AUDIOHAL_ALSA_PROPERTY             "vendor.media.audiohal.alsadump"
#define AML_DUMP_AUDIOHAL_TV_PROPERTY               "vendor.media.audiohal.tvdump"
#define AML_DUMP_AUDIO_STREAM_PROPERTY              "vendor.media.audio.stream.dump"
#define AML_DUMP_AUDIOHAL_ASYNC_WRITE_PROPERTY      "vendor.media.audiohal.async.write"

void aml_audio_debug_open(void);
void aml_audio_debug_close(void);

static inline int  get_debug_value(AML_DUMP_DEBUG_INFO_T info_id) {
    return aml_debug_items[info_id].value;
}

#include <cutils/log.h>
#define AM_LOGV(fmt, ...)  ALOGV("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#define AM_LOGD(fmt, ...)  ALOGD("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#define AM_LOGI(fmt, ...)  ALOGI("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#define AM_LOGW(fmt, ...)  ALOGW("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#define AM_LOGE(fmt, ...)  ALOGE("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)

#endif

