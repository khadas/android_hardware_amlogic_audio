/*
 * Copyright (C) 2021 Amlogic Corporation.
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


#define LOG_TAG "audio_hw_hdmirx_utils"
//#define LOG_NDEBUG 0

/*
 *This table shows the meaning of bytes 1 to 3 of the Short Audio Descriptor.
 *|-----------------------------------------------------------------------------------------|
 *|            |                                  Bits                                      |
 *|    Byte    |----------------------------------------------------------------------------|
 *|    #       |   7     |    6   |    5   |    4   |    3   |    2    |    1    |    0     |
 *|-----------------------------------------------------------------------------------------|
 *|     1      | F17 = 0 |         Audio format code         |Maximum number of channels – 1|
 *|-----------------------------------------------------------------------------------------|
 *|     2      | F27 = 0 | 192kHz |176.4kHz| 96kHz  | 88.2kHz|   48kHz | 44.1kHz |  32kHz   |
 *|-----------------------------------------------------------------------------------------|
 *|     3      |                       Audio format code–dependent value                    |
 *|-----------------------------------------------------------------------------------------|
 */

/*
 * The audio format code values (bits [6:3] of byte 1) for Dolby technologies are:
 * 0x2 = Dolby Digital
 * 0xA = Dolby Digital Plus
 * 0xC = Dolby TrueHD and Dolby MAT
 */
#define SAD_SIZE 3 /* SAD only has 3bytes */
#define DOLBY_DIGITAL               0x2
#define DOLBY_DIGITAL_PLUS          0xA
#define DOLBY_TRUEHD_AND_DOLBY_MAT  0xC
#define AUDIO_FORMAT_CODE_BYTE1_BIT3 3

#include <errno.h>
#include <cutils/log.h>
#include <pthread.h>

#include "audio_hw.h"
#include "audio_hw_utils.h"
#include "hdmirx_utils.h"
#include "aml_alsa_mixer.h"
#include "aml_audio_stream.h"
#include "dolby_lib_api.h"
#include <aml_android_utils.h>
#include <earc_utils.h>
#include "audio_hw_resource_mgr.h"

char sad_str_default[5][5] = {
     {2, 0, 0, 0, 0},
     {7, 0, 0, 0, 0},
     {10, 0, 0, 0, 0},
     {11, 0, 0, 0, 0},
     {12, 0, 0, 0, 0},
};

//assign it to hw_resource_manager or patch_manager
typedef struct hdmi_capability_manager {
    /* The HDMI ARC capability info currently set. */
    struct aml_arc_hdmi_desc hdmi_descs;
    /* Save the HDMI ARC actual capability info. */
    struct aml_arc_hdmi_desc hdmi_arc_capability_desc;
    /*it is used to save the string of last set_arc_hdmi and to check whether ARC or EARC status has changed*/
    char last_arc_hdmi_array[EDID_ARRAY_MAX_LEN]; //
    /* HDMIRX default EDID */
    char default_EDID_array[EDID_ARRAY_MAX_LEN]; //
    bool need_to_update_arc_status;
    int arc_hdmi_updated;

    pthread_mutex_t lock;
} hdmi_capability_manager;

struct audio_format_code_list {
    AML_HDMI_FORMAT_E  id;
    char audio_format_code_name[32];
};

static const struct audio_format_code_list gAudioFormatList[] = {
    {AML_HDMI_FORMAT_RESERVED1, "AML_HDMI_FORMAT_RESERVED1"},
    {AML_HDMI_FORMAT_LPCM, "AUDIO_FORMAT_LPCM"},
    {AML_HDMI_FORMAT_AC3, "AUDIO_FORMAT_AC3"},
    {AML_HDMI_FORMAT_MPEG1, "AUDIO_FORMAT_MPEG1"},
    {AML_HDMI_FORMAT_MP3, "AUDIO_FORMAT_MP3"},
    {AML_HDMI_FORMAT_MPEG2MC, "AUDIO_FORMAT_MPEG2MC"},
    {AML_HDMI_FORMAT_AAC, "AUDIO_FORMAT_AAC"},
    {AML_HDMI_FORMAT_DTS, "AUDIO_FORMAT_DTS"},
    {AML_HDMI_FORMAT_ATRAC, "AUDIO_FORMAT_ATRAC"},
    {AML_HDMI_FORMAT_OBA, "AUDIO_FORMAT_OBA"},
    {AML_HDMI_FORMAT_DDP, "AUDIO_FORMAT_DDP"},
    {AML_HDMI_FORMAT_DTSHD, "AUDIO_FORMAT_DTSHD"},
    {AML_HDMI_FORMAT_MAT, "AUDIO_FORMAT_MAT"},
    {AML_HDMI_FORMAT_DST, "AUDIO_FORMAT_DST"},
    {AML_HDMI_FORMAT_WMAPRO, "AUDIO_FORMAT_WMAPRO"},
    {AML_HDMI_FORMAT_RESERVED2, "AUDIO_FORMAT_RESERVED2"},
};

static const char *get_audio_format_code_name_by_id(int fmt_id)
{
    int i;
    int cnt_mixer = sizeof(gAudioFormatList) / sizeof(struct audio_format_code_list);

    for (i = 0; i < cnt_mixer; i++) {
        if (gAudioFormatList[i].id == fmt_id) {
            return gAudioFormatList[i].audio_format_code_name;
        }
    }

    return NULL;
}

struct hdmi_capability_manager *get_hdmi_capability_manager(struct aml_audio_device *adev);

static bool is_arc_status_update(struct aml_audio_device *adev)
{
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    return mgr->need_to_update_arc_status;
}

static void set_arc_status_update(struct aml_audio_device *adev, bool enable)
{
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    mgr->need_to_update_arc_status = enable;
}

void set_arc_hdmi_updated(struct aml_audio_device *adev, bool enable)
{
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    mgr->arc_hdmi_updated = enable;
}

bool is_arc_hdmi_updated(struct aml_audio_device *adev)
{
   struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
   return mgr->arc_hdmi_updated;
}

const char *get_default_edid_str(struct aml_audio_device *adev)
{
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    return mgr->default_EDID_array;
}

/*
* function: read edid from hdmirx and update it to default string
*/
static int read_default_edid_from_hdmirx(struct audio_hw_device *dev, int request_len)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    char EDID_audio_array[EDID_ARRAY_MAX_LEN] = {0};
    int n = 0;
    int ret = 0;
    int edid_dbg = get_debug_value(AML_DEBUG_AUDIOHAL_EDID);

    ret = aml_mixer_ctrl_get_array(&adev->alsa_mixer, AML_MIXER_ID_HDMIIN_AUDIO_EDID,
        EDID_audio_array, EDID_ARRAY_MAX_LEN);

    ALOGV("%s line %d mixer %s ret %d\n", __func__, __LINE__, "HDMIIN AUDIO EDID", ret);

    if (ret == 0) {
        /* got the SAD_array with first TLV_HEADER_SIZE all zero */
        memmove(EDID_audio_array, EDID_audio_array + TLV_HEADER_SIZE, EDID_ARRAY_MAX_LEN - TLV_HEADER_SIZE);
    }

    if (edid_dbg) {
        for (n = 0; n < EDID_ARRAY_MAX_LEN; n++) {
            ALOGI("%s line %d EDID_cur_array(%d) [%#x]\n",  __func__, __LINE__, n, EDID_audio_array[n]);
        }
    }

    char *dest_str = mgr->default_EDID_array;
    if (request_len >= EDID_ARRAY_MAX_LEN)
        memcpy(dest_str, EDID_audio_array, EDID_ARRAY_MAX_LEN);
    else {
        ALOGE("%s line %d request_len %d is less than %d, some info may lost!\n",
            __func__, __LINE__, request_len, EDID_ARRAY_MAX_LEN);
        memcpy(dest_str, EDID_audio_array, request_len);
    }
    return 0;
}

/*
 * restore default edid to hdmirx
 */
static void write_default_edid_to_hdmirx(struct audio_hw_device *dev, int length)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    struct aml_arc_hdmi_desc *hdmi_desc = &mgr->hdmi_descs;
    int edid_dbg = get_debug_value(AML_DEBUG_AUDIOHAL_EDID);
    char *edid_string = mgr->default_EDID_array;
    if (edid_dbg) {
        for (int n = 0; n < length + TLV_HEADER_SIZE ; n++) {
            ALOGI("%s line %d SAD_array(%d) [%#x]\n", __func__, __LINE__, n, edid_string[n]);
        }
    }

    aml_mixer_ctrl_set_array(&adev->alsa_mixer, AML_MIXER_ID_HDMIIN_AUDIO_EDID,
            edid_string, TLV_HEADER_SIZE);

    hdmi_desc->default_edid = true;
}

/*
 * Function: restore re-construct edid to hdmirx
 * Description:
 *  update AVR ARC capability to edid
 */
static void write_new_edid_to_hdmirx(struct audio_hw_device *dev, void *edid_array, int edid_length)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    struct aml_arc_hdmi_desc *hdmi_desc = &mgr->hdmi_descs;
    int edid_dbg = get_debug_value(AML_DEBUG_AUDIOHAL_EDID);

    int suitable_edid_len = edid_length;
    ALOGD("%s() edid_length %d will %s\n", __func__, edid_length ,"");

    char *EDID_audio_array = edid_array;

    if (edid_dbg) {
        for (int n = 0; n < edid_length + TLV_HEADER_SIZE ; n++) {
            ALOGI("%s line %d SAD_array(%d) [%#x]\n", __func__, __LINE__, n, EDID_audio_array[n]);
        }
    }

    suitable_edid_len = (edid_length <= (EDID_ARRAY_MAX_LEN - TLV_HEADER_SIZE)) ? (edid_length) : (EDID_ARRAY_MAX_LEN - TLV_HEADER_SIZE);
    aml_mixer_ctrl_set_array(&adev->alsa_mixer, AML_MIXER_ID_HDMIIN_AUDIO_EDID,
            edid_array, (suitable_edid_len + TLV_HEADER_SIZE));

    hdmi_desc->default_edid = false;
}

/*
 *|-----------------------------------------------------------------------------------------------------------|
 *|            |              Bits [1:0] of Dolby Digital Plus Short Audio Descriptor byte 3                  |
 *|   Meaning  |----------------------------------------------------------------------------------------------|
 *|            |                Bit1                           |                      Bit0                    |
 *|-----------------------------------------------------------------------------------------------------------|
 *|            | Dolby Atmos decoding and rendering of joint   | Dolby Atmos decoding and rendering of joint  |
 *| If bit = 0 | object coding content from Dolby Digital Plus | object coding content is not supported.      |
 *|            | acmod 28 is not supported.                    |                                              |
 *|-----------------------------------------------------------------------------------------------------------|
 *|            | Dolby Atmos decoding and rendering of joint   | Dolby Atmos decoding and rendering of joint  |
 *| If bit = 1 | object coding content from Dolby Digital Plus | object coding content is supported.          |
 *|            | acmod 28 is supported.                        |                                              |
 *|-----------------------------------------------------------------------------------------------------------|
 */
static int update_dolby_atmos_decoding_and_rendering_cap_for_ddp_sad(
    void *array
    , int count
    , bool is_acmod_28_supported
    , bool is_joc_supported)
{
    char *ddp_sad = NULL;
    int ret = -1;
    if (!array || (count < SAD_SIZE)) {
        ALOGE("%s line %d array %p count %d\n", __func__, __LINE__, array, count);
        return -1;
    }

    ddp_sad = (char *)array;

    /* mask Atmos bit from AVR to hdmirx edid */
    /* bytes3 bit1: bit_for_acmod_28; bytes3 bit0: bit_for_joc */
    if (DOLBY_DIGITAL_PLUS == ((ddp_sad[0] >> AUDIO_FORMAT_CODE_BYTE1_BIT3)&0xF)) {
        int value = (is_acmod_28_supported << 1) | is_joc_supported;

        ddp_sad[2] &= 0xFC;
        ddp_sad[2] |= value;

        ret = 0;
    } else {
        ret = -1;
    }

    //ALOGV("%s line %d ddp sad [%#x %#x %#x]\n", __func__, __LINE__, ddp_sad[0], ddp_sad[1], ddp_sad[2]);
    return ret;
}

/*
 *--------------------------------------------------------------------------------------------
 *|            |     Bits [1:0] of Dolby MAT and Dolby TrueHD Short Audio Descriptor byte 3  |
 *|    Meaning |-----------------------------------------------------------------------------|
 *|            |                Bit1                  |              Bit0                    |
 *|------------------------------------------------------------------------------------------|
 *|If bit = 0  |EMDF hash calculation required by     |Only Dolby TrueHD decoding is         |
 *|            |the Dolby MAT decoder for PCM input.  |supported.                            |
 *|------------------------------------------------------------------------------------------|
 *|If bit = 1  |EMDF hash calculation not required    |Dolby Atmos decoding and rendering of |
 *|            |by theDolby MAT decoder for PCM input.|Dolby TrueHD and PCM are supported.   |
 *--------------------------------------------------------------------------------------------
 *Note: If bit 0 of byte 3 is set to 0, then bits 1 through 7 of byte 3 are also set to 0.
 */
static int update_dolby_MAT_decoding_cap_for_dolby_MAT_and_dolby_TRUEHD_sad(
    void *array
    , int count
    , bool is_mat_pcm_supported
    , bool is_truehd_supported)
{
    char *mat_sad = NULL;
    int ret = -1;
    if (!array || (count < SAD_SIZE)) {
        ALOGE("%s line %d array %p count %d\n", __func__, __LINE__, array, count);
        return -1;
    }

    mat_sad = (char *)array;

    /* mask Atmos bit from AVR to hdmirx edid*/
    /* bytes3 bit1: bit_for_mat_pcm; bytes3 bit0: bit_for_truehd */
    if (DOLBY_TRUEHD_AND_DOLBY_MAT == ((mat_sad[0] >> AUDIO_FORMAT_CODE_BYTE1_BIT3)&0xF)) {
        int value = (is_mat_pcm_supported << 1) | is_truehd_supported;

        mat_sad[2] &= 0xFC;
        mat_sad[2] |= value;

        ret = 0;
    } else {
        ret = -1;
    }

    //ALOGV("%s line %d mat sad [%#x %#x %#x]\n", __func__, __LINE__, mat_sad[0], mat_sad[1], mat_sad[2]);
    return ret;
}

bool is_same_edid_str(const char *str_a, const char *str_b)
{
    return (strcmp(str_a, str_b) == 0 ? true : false);
}

/********************* APIs open for other module ******************************/

struct aml_arc_hdmi_desc *get_arc_hdmi_cap(struct aml_audio_device *adev)
{
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    return &mgr->hdmi_descs;
}


void clear_arc_cached_edid(struct aml_audio_device *adev)
{
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    memset(mgr->last_arc_hdmi_array, 0, EDID_ARRAY_MAX_LEN);
}

void update_arc_cached_edid(struct aml_audio_device *adev, const char *str)
{
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    memcpy(mgr->last_arc_hdmi_array, str, EDID_ARRAY_MAX_LEN);
}

const char *get_arc_cached_edid(struct aml_audio_device *adev)
{
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    return mgr->last_arc_hdmi_array;
}

/* here is the array spec, about two cases */
/* 1st case: [0, 1] 0: length of all the SAD, here is zero, 1, avr port*/
/* 2nd case: [9, 0, 21, 7, 80, 62, 31, 192, 87, 6, 3] */
/*            9: length of all the SAD, [21, 7, 80][62, 31, -64][87, 6, 3] */
/*               0, avr port                          */
/*                  [21, 7, 80][62, 31, -64][87, 6, 3]*/
int set_arc_hdmi(struct audio_hw_device *dev, char *value, size_t len)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);
    char *pt = NULL, *tmp = NULL;
    int i = 0;
    int edid_dbg = get_debug_value(AML_DEBUG_AUDIOHAL_EDID);

    unsigned int *ptr = (unsigned int *)(&hdmi_desc->target_EDID_array[0]);
    if (strlen (value) > len) {
        ALOGE ("value array overflow!");
        return -EINVAL;
    }

    if (edid_dbg)
        ALOGI("%s line %d value %s\n", __func__, __LINE__, value);

    /* There is no dolby lib in system, don't update EDID */
    if (adev->dolby_lib_type == eDolbyNull) {
        return 0;
    }

    /*if the latest set_arc_hdmi string is same as the last, do not update the EDID*/
    /*if not, we need to update the EDID and copy the string to last_arc_hdmi_array*/
    if (is_same_edid_str(value, get_arc_cached_edid(adev))) {
        set_arc_status_update(adev, false);
        return 0;
    } else {
        set_arc_status_update(adev, true);
        update_arc_cached_edid(adev, value);
    }

    memset(hdmi_desc->target_EDID_array, 0, EDID_ARRAY_MAX_LEN);

    pt = strtok_r (value, "[], ", &tmp);
    while (pt != NULL) {

        if (i == 0) //index 0 means avr cec length
            hdmi_desc->EDID_length = atoi (pt);
        else if (i == 1) //index 1 means avr port
            hdmi_desc->avr_port = atoi (pt);
        else
            hdmi_desc->target_EDID_array[TLV_HEADER_SIZE + i - 2] = atoi (pt);

        pt = strtok_r (NULL, "[], ", &tmp);
        i++;
    }

    ptr[0] = 0;
    ptr[1] = (unsigned int)hdmi_desc->EDID_length;

    if (hdmi_desc->EDID_length == 0) {
        ALOGI("ARC is disconnect!, Reset to default EDID.");
        set_arc_hdmi_updated(adev, false);
        write_default_edid_to_hdmirx(dev, hdmi_desc->EDID_length);
    } else {
        read_default_edid_from_hdmirx(dev, EDID_ARRAY_MAX_LEN);

        /* if dts decoder doesn't support, don't update dts edid */
        if (!adev->dts_decode_enable) {
            int edid_length = hdmi_desc->EDID_length;
            for (i = 0; i < edid_length/3; ) {
                char AudioFormatCodes = (hdmi_desc->target_EDID_array[TLV_HEADER_SIZE + 3*i] >> 3) & 0xF;
                /* mask EDID of dts and dtshd */
                if (AudioFormatCodes == AML_HDMI_FORMAT_DTS || AudioFormatCodes == AML_HDMI_FORMAT_DTSHD) {
                    char *pr = &hdmi_desc->target_EDID_array[TLV_HEADER_SIZE + 3*i];
                    memmove(pr, (pr + 3), (edid_length - 3*i - 3));
                    edid_length -= 3;
                } else {
                    i++;
                }
            }
            hdmi_desc->EDID_length = edid_length;
            ptr[1] = (unsigned int)hdmi_desc->EDID_length;
        }

        ALOGI("ARC is connected, EDID_length = [%d], ARC HDMI AVR port = [%d]",
            hdmi_desc->EDID_length, hdmi_desc->avr_port);
        if (edid_dbg) {
            for (i = 0; i < hdmi_desc->EDID_length/3; i++) {
                ALOGI ("EDID SAD in hex [%x, %x, %x]",
                hdmi_desc->target_EDID_array[TLV_HEADER_SIZE + 3*i],
                hdmi_desc->target_EDID_array[TLV_HEADER_SIZE + 3*i + 1],
                hdmi_desc->target_EDID_array[TLV_HEADER_SIZE + 3*i + 2]);
            }
        }
    }

    return 0;
}

int update_edid_after_edited_audio_sad(struct audio_hw_device *dev, struct format_desc *fmt_desc)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);
    if (!fmt_desc)
        return 0;
    ALOGD("Update [%s] support:%d, ch:%d, sample_mask:%#x, bit_rate:%d, atmos:%d",
        hdmiFormat2Str(fmt_desc->fmt), fmt_desc->is_support, fmt_desc->max_channels,
        fmt_desc->sample_rate_mask, fmt_desc->max_bit_rate, fmt_desc->atmos_supported);

    /*
     * if there is no ddp/ms12 lib, don't update edid.
     */
    if (adev->dolby_lib_type == eDolbyNull) {
        return 0;
    }

    if (AML_DIGITAL_AUDIO_MODE_BYPASS == adev->digital_audio_mode) {
        /* update the AVR's EDID */
        write_default_edid_to_hdmirx(dev, hdmi_desc->EDID_length);
        ALOGI("Bypass mode!, update AVR EDID.");
    }
    else if (AML_DIGITAL_AUDIO_MODE_AUTO == adev->digital_audio_mode) {
        if (!fmt_desc->is_support) {
            //if AVR doesn't support DDP, update EDID to default EDID
            write_default_edid_to_hdmirx(dev, hdmi_desc->EDID_length);
        } else {
            /* get the default EDID audio array */
            char EDID_cur_array[EDID_ARRAY_MAX_LEN] = {0};
            int available_edid_len = 0;
            const char *default_edid_str = get_default_edid_str(adev);
            memcpy(EDID_cur_array, default_edid_str, EDID_ARRAY_MAX_LEN);

            /* edit the current EDID audio array to add DDP-SAD(byte3-bit0~1) and MAT-SAD(byte3-bit0~1)*/
            for (int n = 0; n < EDID_ARRAY_MAX_LEN / SAD_SIZE; n++) {
                update_dolby_atmos_decoding_and_rendering_cap_for_ddp_sad(
                    (void *)(EDID_cur_array  + SAD_SIZE*n)
                    , (EDID_ARRAY_MAX_LEN - SAD_SIZE * n)
                    , hdmi_desc->ddp_fmt.atmos_supported
                    , hdmi_desc->ddp_fmt.atmos_supported);
                update_dolby_MAT_decoding_cap_for_dolby_MAT_and_dolby_TRUEHD_sad(
                    (void *)(EDID_cur_array  + SAD_SIZE*n)
                    , (EDID_ARRAY_MAX_LEN - SAD_SIZE * n)
                    , hdmi_desc->mat_fmt.atmos_supported
                    , hdmi_desc->mat_fmt.atmos_supported);

                /* From the SAD table, one invalid SAD is like this [0, 0, 0], here filter the valid SAD */
                if (EDID_cur_array[SAD_SIZE*n]) {
                    available_edid_len += SAD_SIZE;
                }
            }
            /* Skip PCM SAD */
            memmove((EDID_cur_array + TLV_HEADER_SIZE - SAD_SIZE) , EDID_cur_array, available_edid_len);
            available_edid_len -= SAD_SIZE;
            for (int cnt = 0; cnt < EDID_ARRAY_MAX_LEN; cnt++) {
                ALOGV("%s line %d EDID_cur_array(%d) [%#x]\n",  __func__, __LINE__, cnt, EDID_cur_array[cnt]);
            }

            /* DDP TB35 case:passthrough_edid_not_duplicate_mat_pcm */
            /* when DDP Library inside, EDID should ignore MAT after DUT is connected to eARC.*/
            if (adev->dolby_lib_type_last == eDolbyDcvLib) {
                int edid_length = available_edid_len;
                for (int i = 0; i < available_edid_len / SAD_SIZE; ) {
                    char AudioFormatCodes = (EDID_cur_array[TLV_HEADER_SIZE + 3*i] >> 3) & 0xF;
                    if (AudioFormatCodes == AML_HDMI_FORMAT_MAT) {
                        char *pr = &EDID_cur_array[TLV_HEADER_SIZE + 3*i];
                        memmove(pr, (pr + 3), (edid_length - 3*i - 3));
                        edid_length -= 3;
                        ALOGW("%s line %d will remove MAT codec %d\n", __func__, __LINE__, AudioFormatCodes);
                        break;
                    } else {
                        i++;
                    }
                }
                available_edid_len = edid_length;
            }

            unsigned int *ptr = (unsigned int *)EDID_cur_array;
            ptr[0] = 0;
            ptr[1] = available_edid_len;

            for (int cnt = 0; cnt < EDID_ARRAY_MAX_LEN; cnt++) {
                ALOGV("%s line %d EDID_cur_array(%d) [%#x]\n",  __func__, __LINE__, cnt, EDID_cur_array[cnt]);
            }

            /* update the EDID after editing*/
            write_new_edid_to_hdmirx(dev, (void *)EDID_cur_array, available_edid_len);
        }
    } else if (hdmi_desc->default_edid == false) {
        /* Reset the audio default EDID */
        write_default_edid_to_hdmirx(dev, hdmi_desc->EDID_length);
    }
    return 0;
}

int set_arc_format(struct audio_hw_device *dev, char *value, size_t len)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);
    struct format_desc *fmt_desc = NULL;
    char *pt = NULL, *tmp = NULL;
    int i = 0, val = 0;
    AML_HDMI_FORMAT_E format = AML_HDMI_FORMAT_LPCM;
    bool is_dolby_sad = false;
    bool is_dts_sad = false;
    if (strlen (value) > len) {
        ALOGW("[%s:%d] value array len:%zu overflow!", __func__, __LINE__, strlen(value));
        return -EINVAL;
    }

    /*because pcm format is not updated now, so we add a check here, we can remove this after pcm format is added
     * when it is set arc mode, we should use 2ch pcm
     */
    if (is_arc_connected(adev)) {
        if (aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_TX_ATTENDED_TYPE) == ATTEND_TYPE_EARC) {
            hdmi_desc->pcm_fmt.max_channels = 8;
        } else {
            hdmi_desc->pcm_fmt.max_channels = 2;
        }
    }

    /*
     * ex: adev_set_parameters with (const char *kvpairs = "set_ARC_format=[10, 1, 7, 6, 3]")
     * after the progress: str_parms_get_str with (const char *key = "set_ARC_format")
     * get the (char *value = [10, 1, 7, 6, 3])
     * it means [AML_HDMI_FORMAT_DDP, is_support(1), max_channels(7+1), sample_rate_mask(6), atmos_supported(0x3&0x1)]
     * so, here to analysis the Audio EDID from CEC.
     */
    pt = strtok_r (value, "[], ", &tmp);
    while (pt != NULL) {
        val = atoi (pt);
        switch (i) {
        /* the first step, strtok_r got the "[", found the value of fmt_desc->fmt */
        case 0:
            format = val;
            if (val == AML_HDMI_FORMAT_AC3) {
                fmt_desc = &hdmi_desc->dd_fmt;
            } else if (val == AML_HDMI_FORMAT_DDP) {
                fmt_desc = &hdmi_desc->ddp_fmt;
            } else if (val == AML_HDMI_FORMAT_MAT) {
                fmt_desc = &hdmi_desc->mat_fmt;
                /*if arc format is changed or ARC switch to EARC, we need update it, then new output can be configured*/
                if (is_arc_status_update(adev)) {
                    set_arc_status_update(adev, false);
                    set_arc_hdmi_updated(adev, true);
                }
            } else if (val == AML_HDMI_FORMAT_LPCM) {
                fmt_desc = &hdmi_desc->pcm_fmt;
            } else if (val == AML_HDMI_FORMAT_DTS) {
                fmt_desc = &hdmi_desc->dts_fmt;
            } else if (val == AML_HDMI_FORMAT_DTSHD) {
                fmt_desc = &hdmi_desc->dtshd_fmt;
            } else {
                ALOGW("[%s:%d] unsupport fmt:%d", __func__, __LINE__, val);
                return -EINVAL;
            }
            fmt_desc->fmt = val;
            break;
        /* the second step, strtok_r got the ",", found the value of fmt_desc->is_support */
        case 1:
            fmt_desc->is_support = val;
            break;
        /* the three step, strtok_r got the ",", found the value of fmt_desc->max_channels */
        case 2:
            fmt_desc->max_channels = val + 1;
            break;
        /* the four step, strtok_r got the ",", found the value of fmt_desc->sample_rate_mask */
        case 3:
            fmt_desc->sample_rate_mask = val;
            break;
        /* the five step, strtok_r got the ",", found the value of fmt_desc->atmos_supported */
        case 4:
            if (format == AML_HDMI_FORMAT_AC3) {
                /* byte 3, bit 0 is atmos bit*/
                fmt_desc->atmos_supported = false;
                /* AC3 Bytes 3 means the "Maximum bit rate divided by 8000 (8 kHz)" */
                fmt_desc->max_bit_rate = val * 80;

                /* when arc is connected update AVR SAD to hdmi edid */
                update_edid_after_edited_audio_sad(dev, fmt_desc);
                update_sink_format_after_hotplug(adev);
            } else if (format == AML_HDMI_FORMAT_DDP) {
                /* byte 3, bit 0 is atmos bit*/
                fmt_desc->atmos_supported = (val & 0x1) > 0 ? true : false;

                /* when arc is connected update AVR SAD to hdmi edid */
                update_edid_after_edited_audio_sad(dev, fmt_desc);
                /*
                 * if the ARC capability format is changed, it should not support HBR(MAT/DTS-HD)
                 * for the sequence is LPCM -> DD -> DTS -> DDP -> DTSHD,
                 * which is defined in "private void setAudioFormat()" at file:DroidLogicEarcService.java
                 * so, here we choose the DDP part to update the sink format.
                 */
                update_sink_format_after_hotplug(adev);
            } else if (format == AML_HDMI_FORMAT_MAT && fmt_desc->is_support == true) {
                /* byte 3, bit 0 is profile bit, if profile 1 MAT, don't output MAT PCM*/
                fmt_desc->atmos_supported = (val & 0x1) > 0 ? true : false;
                if (fmt_desc->atmos_supported == false)
                    fmt_desc->is_support = false;

                /* when arc is connected update AVR SAD to hdmi edid */
                update_edid_after_edited_audio_sad(dev, fmt_desc);
                /*
                 * if the ARC capability format is changed, it should not support HBR(MAT/DTS-HD)
                 * for the sequence is LPCM -> DD -> DTS -> DDP -> DTSHD,
                 * which is defined in "private void setAudioFormat()" at file:DroidLogicEarcService.java
                 * so, here we choose the DDP part to update the sink format.
                 */
                update_sink_format_after_hotplug(adev);
            } else {
                //TODO, how to update the DTS/DTSHD/... SAD.
                ALOGW("[%s:%d] this SAD fmt is %s, mark it as TODO.\n",
                    __func__, __LINE__, get_audio_format_code_name_by_id(format));
            }
            break;
        default:
            break;
        }

        pt = strtok_r (NULL, "[], ", &tmp);
        i++;
    }
    memcpy(&mgr->hdmi_arc_capability_desc, hdmi_desc, sizeof(struct aml_arc_hdmi_desc));
    if (fmt_desc) {
        ALOGI("----[%s] support:%d, ch:%d, sample_mask:%#x, bit_rate:%d, atmos:%d",
            hdmiFormat2Str(fmt_desc->fmt),fmt_desc->is_support, fmt_desc->max_channels,
            fmt_desc->sample_rate_mask, fmt_desc->max_bit_rate, fmt_desc->atmos_supported);
    }

    return 0;
}


#if ANDROID_PLATFORM_SDK_VERSION > 32
void read_hdmi_arc_info(struct audio_hw_device *dev,
    const struct audio_extra_audio_descriptor *audio_descriptors, uint32_t size, bool connected) {
    uint8_t descriptor[EXTRA_AUDIO_DESCRIPTOR_SIZE] = {0};
    uint8_t length = 0;

    int edid_dbg = get_debug_value(AML_DEBUG_AUDIOHAL_EDID);

    if (size == 0) {
        AM_LOGW("audio_descriptor special AVR pcm only. length:%d", size);
    } else if (size > 1) { // arc
        for (int i = 0; i < size; i++) {
            if (edid_dbg) {
                AM_LOGD("audio_descriptors[%d].descriptor:%s descriptor_length %d",
                    i,audio_descriptors[i].descriptor, audio_descriptors[i].descriptor_length);
            }
            for (int j = 0; j < audio_descriptors[i].descriptor_length; j++) {
                if (length >= EXTRA_AUDIO_DESCRIPTOR_SIZE) {
                    AM_LOGE("sad descriptor_length too large");
                    return;
                }
                descriptor[length++] = audio_descriptors[i].descriptor[j];
            }
        }
    } else if (size == 1) { // earc
        length = audio_descriptors[0].descriptor_length;
        if (edid_dbg) {
            AM_LOGD("audio_descriptors[0].descriptor:%s descriptor_length %d",
                audio_descriptors[0].descriptor, audio_descriptors[0].descriptor_length);
        }
        if (length > EXTRA_AUDIO_DESCRIPTOR_SIZE) {
            AM_LOGE("sad descriptor_length:%d too large", audio_descriptors[0].descriptor_length);
            return;
        }
        memcpy(descriptor, &audio_descriptors[0].descriptor[0], length);
    }

    // 1. set the arc hdmi info.
    uint8_t edid_buf[EXTRA_AUDIO_DESCRIPTOR_SIZE + 2] = {0};
    char edid_str_buf[1024] = {0};

    int spec_length = 0;

    if (connected) {
        edid_buf[0] = length;
        spec_length = length + 2;
    } else { //use default EDID
        edid_buf[0] = 0;
        length = 2;
        spec_length = length;
    }
    edid_buf[1] = aml_getprop_int("persist.vendor.sys.arc_port");
    AM_LOGD("%s arc_port:%d", __func__, edid_buf[1]);

    if ((size == 0) && connected) {
        edid_buf[0] = 1;
        length = 2;
    }
    if (edid_dbg) {
        AM_LOGD("length %d spec_length %d", length, spec_length);
        for (int n = 0; n < length; n++) {
            ALOGI("descriptor[%d]:%x", n, descriptor[n]);
        }
    }

    memcpy(edid_buf + 2, &descriptor[0], length);
    strcat(edid_str_buf, "[");

    for (int i = 0; i < spec_length; i++) {
        char temp_str[5] = {0};
        snprintf(temp_str, 5, "%d", edid_buf[i]);
        strcat(edid_str_buf, temp_str);
        if (edid_dbg) {
            AM_LOGD("set arc hdmi loop %d edid_str_buf:%s", i, edid_str_buf);
        }
        if (i + 1 < spec_length) {
            strcat(edid_str_buf, ", ");
        }
        if (edid_dbg) {
            AM_LOGD("set arc hdmi edid_str_buf:%s", edid_str_buf);
        }
    }
    strcat(edid_str_buf, "]");
    AM_LOGD("set arc hdmi edid_str_buf:%s", edid_str_buf);
    set_arc_hdmi(dev, edid_str_buf, 1024);

    // 2. read the arc format info.

    if (connected) {
       if (size == 0) {
            int sad_buffer[5] = {0};
            char temp_sad_str[128] = {0};
            // find a descriptor for each SUPPORT_CODECS
            // CEA-861-D Table 34, 35, 36
            sad_buffer[0] = 1;
            sad_buffer[1] = 1; // supported
            sad_buffer[2] = 0x1; // Max Channels - 1
            sad_buffer[3] = 0x6; // Default Sample Rate
            sad_buffer[4] = 0x1; // Max bit rate / 8kHz

            snprintf(temp_sad_str, 128, "[%d, %d, %d, %d, %d]", sad_buffer[0],
                sad_buffer[1], sad_buffer[2], sad_buffer[3], sad_buffer[4]);
            AM_LOGD("set pcm only arc format: %s", temp_sad_str);
            set_arc_format(dev, temp_sad_str, AUDIO_HAL_CHAR_MAX_LEN);
        } else {
            for (int i = 0; i + 2 < length; i += 3) {
                int sad_buffer[5] = {0};
                char temp_sad_str[128] = {0};
                // find a descriptor for each SUPPORT_CODECS
                // CEA-861-D Table 34, 35, 36
                sad_buffer[0] = (descriptor[i] & 0x78) >> 3;
                sad_buffer[1] = 1; // supported
                sad_buffer[2] = descriptor[i] & 0x7; // Max Channels - 1
                sad_buffer[3] = descriptor[i + 1] & 0x7F; // Support Sample Rate
                sad_buffer[4] = descriptor[i + 2] & 0xFF; // Max bit rate / 8kHz

                snprintf(temp_sad_str, 128, "[%d, %d, %d, %d, %d]", sad_buffer[0],
                   sad_buffer[1], sad_buffer[2], sad_buffer[3], sad_buffer[4]);
                AM_LOGD("set arc format: %s", temp_sad_str);
                set_arc_format(dev, temp_sad_str, AUDIO_HAL_CHAR_MAX_LEN);
            }
        }
    } else {// clear all sads
        for (int i = 0; i < sizeof(sad_str_default)/sizeof(sad_str_default[0]); i++) {
            int sad_buffer[5] = {0};
            char temp_sad_str[128] = {0};
            memcpy(sad_buffer, sad_str_default[i], 5);
            snprintf(temp_sad_str, 128, "[%d, %d, %d, %d, %d]", sad_buffer[0],
                    sad_buffer[1], sad_buffer[2], sad_buffer[3], sad_buffer[4]);
            AM_LOGD("set arc format: %s", temp_sad_str);
            set_arc_format(dev, temp_sad_str, AUDIO_HAL_CHAR_MAX_LEN);
        }
    }
}

void update_earc_sad(struct audio_hw_device *dev)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    struct audio_extra_audio_descriptor audio_descriptors = {};
    struct aml_arc_hdmi_desc * hdmi_descs = get_arc_hdmi_cap(adev);
    char cds[AUDIO_HAL_CHAR_MAX_LEN] = {0};

    earctx_fetch_cds(&adev->alsa_mixer, cds, 0, hdmi_descs);

    char *p;
    int cnt = 0;

    p = strtok(cds, ",");
    while (p != NULL) {
        int value = atoi(p);
        if (value > 255) {
            AM_LOGW("invalid sad value:%d, set default 255.", value);
            value = 255;
        }
        if (cnt >= EXTRA_AUDIO_DESCRIPTOR_SIZE) {
            AM_LOGE("invalid sad index break.");
            return;
        }
        audio_descriptors.descriptor[cnt] = (uint8_t)value;
        p = strtok(NULL, ",");
        cnt++;
    }
    audio_descriptors.descriptor_length = cnt;

    if (get_debug_value(AML_DEBUG_AUDIOHAL_EDID)) {
        AM_LOGD("sad cnt: %d ", cnt);
        for (int i = 0; i < audio_descriptors.descriptor_length; i++) {
            AM_LOGD("%d ", audio_descriptors.descriptor[i]);
        }
    }
    read_hdmi_arc_info(dev, &audio_descriptors, 1, true);
}
#endif


struct hdmi_capability_manager *get_hdmi_capability_manager(struct aml_audio_device *adev)
{
    if (adev->hdmi_cap_mgr == NULL) {
        struct hdmi_capability_manager *mgr = aml_audio_calloc(1, sizeof(struct hdmi_capability_manager));
        adev->hdmi_cap_mgr = mgr;
    }
    return adev->hdmi_cap_mgr;
}

int init_hdmi_capability_manager(struct aml_audio_device *adev)
{
    int ret = 0;
    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    if (!mgr) {
        ALOGE("%s() Error, hdmi_capability_manager = NULL, return!", __func__);
        return -EINVAL;
    }

    pthread_mutex_init(&mgr->lock, NULL);
    ALOGI("%s() OK", __func__);
    return ret;
}

void destroy_hdmi_capability_manager(struct aml_audio_device *adev)
{
    if (!adev->hdmi_cap_mgr) {
        return;
    }

    struct hdmi_capability_manager *mgr = get_hdmi_capability_manager(adev);
    pthread_mutex_destroy(&mgr->lock);
    free(mgr);
    adev->hdmi_cap_mgr = NULL;
    ALOGI("%s() done!", __func__);
}