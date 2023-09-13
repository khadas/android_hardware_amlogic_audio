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

#ifndef _AML_AUDIO_STREAM_H_
#define _AML_AUDIO_STREAM_H_

#include <system/audio.h>
#include <pthread.h>

#include "audio_hw_profile.h"
#include "aml_dump_debug.h"
#include "audio_hw.h"

#define AUDIO_FADEOUT_TV_DURATION_US 100 * 1000
#define MS12_AUDIO_FADEOUT_TV_DURATION_US 60 * 1000
#define MS12_AUDIO_FADEIN_TV_DURATION_US  200 * 1000
#define AUDIO_FADEOUT_STB_DURATION_US 40 * 1000

enum {
    DO_FADE_AT_HAL,
    DO_FADE_AT_ALSA,
    DO_FADE_AT_MAX
};

#define RAW_USECASE_MASK ((1<<STREAM_RAW_DIRECT) | (1<<STREAM_RAW_HWSYNC) | (1<<STREAM_RAW_PATCH))
/*
 * 1.AUDIO_FORMAT_PCM_16_BIT is suitable for Speaker
 * 2.AUDIO_FORMAT_AC3 / AUDIO_FORMAT_E_AC3 / AUDIO_FORMAT_E_AC3_JOC is suitable for Sink device(up to DDP)
 * 3.AUDIO_FORMAT_MAT(1.0-truehd/2.0-pcm/2.1-pcm-atmos) is suitable for Sink device(up to MAT&depvalue=1/3)
 * 4.AUDIO_FORMAT_DTS / AUDIO_FORMAT_DTS_HD is suitable for Sink device(up to dts/dts-hd)
 */
#define IS_EXTERNAL_DECODER_SUPPORT_FORMAT(format) \
    ((format == AUDIO_FORMAT_PCM_16_BIT) ||\
        (format == AUDIO_FORMAT_AC3) ||\
        ((format & AUDIO_FORMAT_E_AC3) == AUDIO_FORMAT_E_AC3) ||\
        ((format & AUDIO_FORMAT_MAT) == AUDIO_FORMAT_MAT) ||\
        (format == AUDIO_FORMAT_DTS) ||\
        (format == AUDIO_FORMAT_DTS_HD))

#define IS_DIGITAL_IN_HW(device) ((device) == AUDIO_DEVICE_IN_HDMI ||\
                             (device) == AUDIO_DEVICE_IN_HDMI_ARC ||\
                             (device) == AUDIO_DEVICE_IN_SPDIF)

#define DATA_PCM                         (0)
#define DATA_NON_PCM                     (1)

/*temp code, we will remove it later*/
#if ANDROID_PLATFORM_SDK_VERSION < 31
/*S/T already has such enum, R doesn't have it*/
typedef enum {
    AUDIO_FORMAT_MPEGH = 0x2C000000u,
    AUDIO_FORMAT_MPEGH_SUB_BL_L3 = 0x13u,
    AUDIO_FORMAT_MPEGH_SUB_BL_L4 = 0x14u,
    AUDIO_FORMAT_MPEGH_SUB_LC_L3 = 0x23u,
    AUDIO_FORMAT_MPEGH_SUB_LC_L4 = 0x24u,
    AUDIO_FORMAT_MPEGH_BL_L3 = 0x2C000013u,
    AUDIO_FORMAT_MPEGH_BL_L4 = 0x2C000014u,
    AUDIO_FORMAT_MPEGH_LC_L3 = 0x2C000023u,
    AUDIO_FORMAT_MPEGH_LC_L4 = 0x2C000024u,
} audio_format_Ext_t;
#endif


typedef uint32_t usecase_mask_t;

/*
 *@brief get this value by adev_set_parameters(), command is "digital_audio_format"
 */
enum digital_format {
    PCM = 0,
    DD = 4,
    AUTO = 5,
    BYPASS = 6
};

enum stream_write_func {
    OUT_WRITE_NEW = 0,
    MIXER_AUX_BUFFER_WRITE_SM = 1,
    MIXER_MAIN_BUFFER_WRITE_SM = 2,
    MIXER_MMAP_BUFFER_WRITE_SM = 3,
    MIXER_AUX_BUFFER_WRITE = 4,
    MIXER_MAIN_BUFFER_WRITE = 5,
    MIXER_APP_BUFFER_WRITE = 6,
    PROCESS_BUFFER_WRITE = 7,

    MIXER_WRITE_FUNC_MAX
};

enum tunerhal_audio_streamtype    {
    TUNERHAL_UNDEFINED,
    /*
     * Uncompressed Audio
     */
    TUNERHAL_PCM,
    /*
     * MPEG Audio Layer III versions
     */
    TUNERHAL_MP3,
    /*
     * ISO/IEC 11172 Audio
     */
    TUNERHAL_MPEG1,
    /*
     * ISO/IEC 13818-3
     */
    TUNERHAL_MPEG2,
    /*
     * ISO/IEC 23008-3 (MPEG-H Part 3)
     */
    TUNERHAL_MPEGH,
    /*
     * ISO/IEC 14496-3
     */
    TUNERHAL_AAC,
    /*
     * Dolby Digital
     */
    TUNERHAL_AC3,
    /*
     * Dolby Digital Plus
     */
    TUNERHAL_EAC3,
    /*
     * Dolby AC-4
     */
    TUNERHAL_AC4,
    /*
     * Basic DTS
     */
    TUNERHAL_DTS,
    /*
     * High Resolution DTS
     */
    TUNERHAL_DTS_HD,
    /*
     * Windows Media Audio
     */
    TUNERHAL_WMA,
    /*
     * Opus Interactive Audio Codec
     */
    TUNERHAL_OPUS,
    /*
     * VORBIS Interactive Audio Codec
     */
    TUNERHAL_VORBIS,
    /*
     * SJ/T 11368-2006
     */
    TUNERHAL_DRA,
	 /*
     * AAC with ADTS (Audio Data Transport Format).
     */
    TUNERHAL_AAC_ADTS,

    /*
     * AAC with ADTS with LATM (Low-overhead MPEG-4 Audio Transport Multiplex).
     */
    TUNERHAL_AAC_LATM,

    /*
     * High-Efficiency AAC (HE-AAC) with ADTS (Audio Data Transport Format).
     */
    TUNERHAL_AAC_HE_ADTS,

    /*
     * High-Efficiency AAC (HE-AAC) with LATM (Low-overhead MPEG-4 Audio Transport Multiplex).
     */
    TUNERHAL_AAC_HE_LATM
};

enum encoding_format {
    ENCODING_INVALID      = 0,
    ENCODING_DEFAULT      = 1,
    ENCODING_PCM_16BIT    = 2,
    ENCODING_PCM_8BIT     = 3,
    ENCODING_PCM_FLOAT    = 4,
    ENCODING_AC3          = 5,
    ENCODING_E_AC3        = 6,
    ENCODING_DTS          = 7,
    ENCODING_DTS_HD       = 8,
    ENCODING_MP3          = 9,
    ENCODING_AAC_LC       = 10,
    ENCODING_AAC_HE_V1    = 11,
    ENCODING_AAC_HE_V2    = 12,
    ENCODING_IEC61937     = 13,
    ENCODING_DOLBY_TRUEHD = 14,
    ENCODING_AAC_ELD      = 15,
    ENCODING_AAC_XHE      = 16,
    ENCODING_AC4          = 17,
    ENCODING_E_AC3_JOC    = 18,
    ENCODING_DOLBY_MAT    = 19,
    ENCODING_OPUS         = 20
};

enum {
    ATTEND_TYPE_NONE = 0,
    ATTEND_TYPE_ARC,
    ATTEND_TYPE_EARC
};

static inline bool is_main_write_usecase(stream_usecase_t usecase)
{
    return usecase > 0;
}

static inline bool is_digital_raw_format(audio_format_t format)
{
    switch (format) {
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3:
    case AUDIO_FORMAT_E_AC3_JOC:
    case AUDIO_FORMAT_AC4:
    case AUDIO_FORMAT_MAT:
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_HD:
    case AUDIO_FORMAT_DOLBY_TRUEHD:
    case AUDIO_FORMAT_IEC61937:
    case AUDIO_FORMAT_MP3:
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_HE_AAC_V1:
    case AUDIO_FORMAT_HE_AAC_V2:
    case AUDIO_FORMAT_AAC_HE_V1:
    case AUDIO_FORMAT_AAC_HE_V2:
    case AUDIO_FORMAT_AAC_LATM:
        return true;
    default:
        return false;
    }
}

static inline bool is_dolby_format(audio_format_t format) {
    switch (format) {
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3:
    case AUDIO_FORMAT_E_AC3_JOC:
    case AUDIO_FORMAT_AC4:
    case AUDIO_FORMAT_MAT:
    case AUDIO_FORMAT_DOLBY_TRUEHD:
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_HE_AAC_V1:
    case AUDIO_FORMAT_HE_AAC_V2:
        return true;
    default:
        return false;
    }
}

inline bool is_dts_format(audio_format_t format) {
    switch (format) {
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_HD:
    ///< audio_format_t does not include dts_express. So we get the format(dts_express especially) from the decoder.
    // case AUDIO_FORMAT_DTS_EXPRESS:
        return true;
    default:
        return false;
    }
}

static inline bool is_mpegh_format(audio_format_t format) {
    switch ((uint32_t)format) {
    case AUDIO_FORMAT_MPEGH:
    case AUDIO_FORMAT_MPEGH_BL_L3:
    case AUDIO_FORMAT_MPEGH_BL_L4:
    case AUDIO_FORMAT_MPEGH_LC_L3:
    case AUDIO_FORMAT_MPEGH_LC_L4:
        return true;
    default:
        return false;
    }
}

inline bool is_aac_format(audio_format_t format) {
    switch (format) {
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_HE_AAC_V1:
    case AUDIO_FORMAT_HE_AAC_V2:
    case AUDIO_FORMAT_AAC_HE_V1:
    case AUDIO_FORMAT_AAC_HE_V2:
    case AUDIO_FORMAT_AAC_LATM:
        return true;
    default:
        return false;
    }
}

static inline bool is_iec61937_format(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    return (aml_out->hal_format == AUDIO_FORMAT_IEC61937);
}

static inline stream_usecase_t attr_to_usecase(uint32_t devices __unused,
        audio_format_t format, uint32_t flags)
{
    // hwsync case
    if ((flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) && (format != AUDIO_FORMAT_IEC61937)
        && !(flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) {
        if (audio_is_linear_pcm(format)) {
            return STREAM_PCM_HWSYNC;
        } else if (is_digital_raw_format(format)) {
            return STREAM_RAW_HWSYNC;
        } else {
            return STREAM_USECASE_MAX;
        }
    }

    // non hwsync cases
    if (/*devices == AUDIO_DEVICE_OUT_HDMI ||*/
        is_digital_raw_format(format)) {
        return STREAM_RAW_DIRECT;
    } else if ((flags & AUDIO_OUTPUT_FLAG_DIRECT) && audio_is_linear_pcm(format)) {
        if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
            // AAudio case
            return STREAM_PCM_MMAP;
        } else {
            //multi-channel LPCM or hi-res LPCM
            return STREAM_PCM_DIRECT;
        }
    } else {
        return STREAM_PCM_NORMAL;
    }
}
static inline stream_usecase_t convert_usecase_mask_to_stream_usecase(usecase_mask_t mask)
{
    int i = 0;
    for (i = 0; i < STREAM_USECASE_MAX; i++) {
        if ((1 << i) & mask) {
            break;
        }
    }
    ALOGI("%s mask %#x i %d", __func__, mask, i);
    if (i >= STREAM_USECASE_MAX) {
        return STREAM_USECASE_MAX;
    } else {
        return (stream_usecase_t)i;
    }
}

static inline alsa_device_t usecase_to_device(stream_usecase_t usecase)
{
    switch (usecase) {
    case STREAM_PCM_NORMAL:
    case STREAM_PCM_DIRECT:
    case STREAM_PCM_HWSYNC:
    case STREAM_PCM_PATCH:
        return I2S_DEVICE;
    case STREAM_RAW_PATCH:
    case STREAM_RAW_DIRECT:
    case STREAM_RAW_HWSYNC:
        return DIGITAL_DEVICE;
    default:
        return I2S_DEVICE;
    }
}

static inline bool is_hdmi_out(audio_devices_t cur_out_devices) {
    return (cur_out_devices & AUDIO_DEVICE_OUT_HDMI_ARC) || (cur_out_devices & AUDIO_DEVICE_OUT_HDMI);
}


struct audio_stream_out;
struct audio_stream_in;

stream_usecase_t convert_usecase_mask_to_stream_usecase(usecase_mask_t mask);

static inline bool need_hw_mix(usecase_mask_t masks)
{
    return (masks > 1);
}

/*
 *@brief get sink format by logic min(source format / digital format / sink capability)
 * For Speaker/Headphone output, sink format keep PCM-16bits
 * For optical output, min(dd, source format, digital format)
 * For HDMI_ARC output
 *      1.digital format is PCM, sink format is PCM-16bits
 *      2.digital format is dd, sink format is min (source format,  AUDIO_FORMAT_AC3)
 *      3.digital format is auto, sink format is min (source format, digital format)
 */
void get_sink_format(struct audio_stream_out *stream);



//for DTV
bool is_dual_output_stream(struct audio_stream_out *stream);

/* dumpsys media.audio_flinger interfaces */
const char *audio_port_role_to_str(audio_port_role_t role);
const char *audio_port_type_to_str(audio_port_type_t type);
void aml_stream_out_info_print(struct aml_stream_out *aml_out, uint64_t *frames, struct timespec *timestamp);
void aml_stream_out_dump(struct aml_stream_out *aml_out, int fd);

int aml_dev_dump_latency(struct aml_audio_device *aml_dev, int fd);
void aml_alsa_device_status_dump(struct aml_audio_device* aml_dev, int fd);
bool is_use_spdifb(struct aml_stream_out *out);
bool is_dolby_ms12_support_compression_format(audio_format_t format);
bool is_dolby_ddp_support_compression_format(audio_format_t format);
bool is_direct_stream_and_pcm_format(struct aml_stream_out *out);
bool is_mmap_stream_and_pcm_format(struct aml_stream_out *out);
void get_audio_indicator(struct aml_audio_device *dev, char *temp_buf);
void update_audio_format(struct aml_audio_device *adev, audio_format_t format);


/*
 *@brief update the sink format after HDMI/HDMI-ARC hot plugged
 * return zero if success.
 */
int update_sink_format_after_hotplug(struct aml_audio_device *adev);

uint32_t tv_in_write(struct audio_stream_out *stream, const void* buffer, size_t bytes);
uint32_t tv_in_read(struct audio_stream_in *stream, void* buffer, size_t bytes);
void tv_do_ease_out(struct aml_audio_device *adev);
void tv_do_ease_in(struct audio_stream_out *stream, void *write_buf, size_t write_bytes);

const char *write_func_to_str(enum stream_write_func func);
int aml_audio_earctx_get_type(struct aml_audio_device *adev);
int aml_audio_earc_get_latency(struct aml_audio_device *adev);
int set_device_control(struct audio_hw_device *dev, struct str_parms *parms);

#endif /* _AML_AUDIO_STREAM_H_ */
