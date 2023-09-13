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

#define LOG_TAG "audio_hw_primary"
//#define LOG_NDEBUG 0
#include <inttypes.h>
#include <cutils/log.h>
#include <tinyalsa/asoundlib.h>
#include <cutils/properties.h>
#include <audio_utils/channels.h>

#include "aml_alsa_mixer.h"
#include "aml_audio_stream.h"
#include "dolby_lib_api.h"
#include "audio_hw_utils.h"
#include "audio_hw_profile.h"
#include "alsa_manager.h"
#include "alsa_device_parser.h"
#include "tv_patch_avsync.h"
#include "aml_android_utils.h"
#include "alsa_config_parameters.h"
#include "audio_hw_ms12.h"
#include "amlAudioMixer.h"
#include "audio_hw_ms12_common.h"
#include "audio_hw_resource_mgr.h"
#include "dtv_private_object.h"

#ifdef MS12_V24_ENABLE
#include "audio_hw_ms12_v2.h"
#endif
#define FMT_UPDATE_THRESHOLD_MAX    (10)
#define DOLBY_FMT_UPDATE_THRESHOLD  (5)
#define DTS_FMT_UPDATE_THRESHOLD    (1)

/*
 * DAP Speaker Virtualizer
 * -dap_surround_virtualizer    * <2 int> Virtualizer Parameter
 *                                         - virtualizer_mode (0,1,2, def: 1)
 *                                            0:OFF
 *                                            1:ON
 *                                            2:AUTO
 *                                         - surround_boost (0...96, def: 96)
 */
#define MS12_DAP_SPEAKER_VIRTUALIZER_OFF  (0)//Disable Speaker Virtualizer(Disable Dolby Atmos Virtualization).
#define MS12_DAP_SPEAKER_VIRTUALIZER_ON   (1)//Enable Speaker Virtualizer.
#define MS12_DAP_SPEAKER_VIRTUALIZER_AUTO (2)//Enable Dolby Atmos Virtualization.

#define HDMI_HDR_STATUS_NODE        "/sys/class/amhdmitx/amhdmitx0/hdmi_hdr_status"
#define SINK_DV_KEYWORD             "DolbyVision"

#define JITTER_PRINT_THRESHOLD (100) // milliseconds
#define INFO_TIME_PRINT_THRESHOLD (100) // milliseconds


static audio_format_t ms12_max_support_output_format() {
#ifndef MS12_V24_ENABLE
    return AUDIO_FORMAT_E_AC3;
#else
    return AUDIO_FORMAT_MAT;
#endif
}


/*
 *@brief get sink capability
 */
static audio_format_t get_sink_capability (struct aml_audio_device *adev)
{
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);

    bool dd_is_support = hdmi_desc->dd_fmt.is_support;
    bool ddp_is_support = hdmi_desc->ddp_fmt.is_support;
    bool mat_is_support = hdmi_desc->mat_fmt.is_support;

    audio_format_t sink_capability = AUDIO_FORMAT_PCM_16_BIT;

    //STB case
    //TV + STB case (BDS)
    //TODO HDMITX+ARC mixed connected case
    //need check active port ???
    if (!is_TV(adev) || is_BDS(adev))
    {
        char *cap = NULL;
        /*we should get the real audio cap, so we need it report the correct truehd info*/
        cap = (char *) get_hdmi_sink_cap_new (AUDIO_PARAMETER_STREAM_SUP_FORMATS,0, hdmi_desc, true);
        if (cap) {
            /*
             * Dolby MAT 2.0/2.1 has low latency vs Dolby MAT 1.0(TRUEHD inside)
             * Dolby MS12 prefers to output MAT2.0/2.1.
             */
            if ((strstr(cap, "AUDIO_FORMAT_MAT_2_0") != NULL) || (strstr(cap, "AUDIO_FORMAT_MAT_2_1") != NULL)) {
                sink_capability = AUDIO_FORMAT_MAT;
            }
            /*
             * Dolby MAT 1.0(TRUEHD inside) vs DDP+DD
             * Dolby MS12 prefers to output DDP.
             * But set sink as TrueHD, then TrueHD can encoded with MAT encoder in Passthrough mode.
             */
            else if (strstr(cap, "AUDIO_FORMAT_MAT_1_0") != NULL) {
                sink_capability = AUDIO_FORMAT_DOLBY_TRUEHD;
            }
            /*
             * DDP vs DDP
             * Dolby MS12 prefers to output DDP.
             */
            else if (strstr(cap, "AUDIO_FORMAT_E_AC3") != NULL) {
                sink_capability = AUDIO_FORMAT_E_AC3;
            }
            /*
             * DD vs PCM
             * Dolby MS12 prefers to output DD.
             */
            else if (strstr(cap, "AUDIO_FORMAT_AC3") != NULL) {
                sink_capability = AUDIO_FORMAT_AC3;
            }
            ALOGI ("%s mbox+dvb case sink_capability =  %#x\n", __FUNCTION__, sink_capability);
            aml_audio_free(cap);
            cap = NULL;
        }

        dd_is_support = hdmi_desc->dd_fmt.is_support;
        ddp_is_support = hdmi_desc->ddp_fmt.is_support;
        mat_is_support = hdmi_desc->mat_fmt.is_support;

    } else {
        if (mat_is_support || hdmi_desc->mat_fmt.MAT_PCM_48kHz_only) {
            sink_capability = AUDIO_FORMAT_MAT;
            mat_is_support = true;
            hdmi_desc->mat_fmt.is_support = true;
        } else if (ddp_is_support) {
            sink_capability = AUDIO_FORMAT_E_AC3;
        } else if (dd_is_support) {
            sink_capability = AUDIO_FORMAT_AC3;
        }

        /* eARC TXs support formats at least support dd, for Test ID HFR5-1-27 */
        if (sink_capability == AUDIO_FORMAT_PCM_16_BIT &&
            aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_TX_ATTENDED_TYPE) == ATTEND_TYPE_EARC &&
            is_arc_connected(adev)) {
            sink_capability = AUDIO_FORMAT_AC3;
            dd_is_support = true;
            hdmi_desc->dd_fmt.is_support = true;
        }

        ALOGI ("%s mat_is_support:%d, dd support:%d ddp support:%#x\n", __FUNCTION__, mat_is_support, dd_is_support, ddp_is_support);
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        bool b_force_ddp = adev->ms12_force_ddp_out;
        ALOGI("force ddp out =%d", b_force_ddp);
        if (b_force_ddp) {
            if (ddp_is_support) {
                sink_capability = AUDIO_FORMAT_E_AC3;
            } else if (dd_is_support) {
                sink_capability = AUDIO_FORMAT_AC3;
            }
        }
    }

    return sink_capability;
}

static audio_format_t get_sink_dts_capability (struct aml_audio_device *adev)
{
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);

    bool dts_is_support = hdmi_desc->dts_fmt.is_support;
    bool dtshd_is_support = hdmi_desc->dtshd_fmt.is_support;

    audio_format_t sink_capability = AUDIO_FORMAT_PCM_16_BIT;

    //STB case
    if (!is_TV(adev))
    {
        char *cap = NULL;
        cap = (char *) get_hdmi_sink_cap_new (AUDIO_PARAMETER_STREAM_SUP_FORMATS,0,hdmi_desc, true);
        if (cap) {
            if (hdmi_desc->dts_fmt.is_support) {
                sink_capability = AUDIO_FORMAT_DTS;
            } else if (hdmi_desc->dtshd_fmt.is_support) {
                sink_capability = AUDIO_FORMAT_DTS_HD;
            }
            ALOGI("%s mbox+dvb case sink_capability %#x\n", __FUNCTION__, sink_capability);
            aml_audio_free(cap);
            cap = NULL;
        }
    } else {
        if (dtshd_is_support) {
            sink_capability = AUDIO_FORMAT_DTS_HD;
        } else if (dts_is_support) {
            sink_capability = AUDIO_FORMAT_DTS;
        }
        ALOGI ("%s dts support %d dtshd support %d\n", __FUNCTION__, dts_is_support, dtshd_is_support);
    }
    return sink_capability;
}


static audio_format_t get_sink_mpegh_capability (struct aml_audio_device *adev)
{
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);

    bool mpegh_is_support = hdmi_desc->mpegh_fmt.is_support;

    audio_format_t sink_capability = AUDIO_FORMAT_PCM_16_BIT;

    //STB case
    if (!is_TV(adev))
    {
        char *cap = NULL;
        cap = (char *) get_hdmi_sink_cap_new (AUDIO_PARAMETER_STREAM_SUP_FORMATS, 0, hdmi_desc, true);
        if (cap) {
            if (hdmi_desc->mpegh_fmt.is_support) {
                sink_capability = (audio_format_t)AUDIO_FORMAT_MPEGH;
            }
            ALOGI("%s mbox+dvb case sink_capability %#x\n", __FUNCTION__, sink_capability);
            aml_audio_free(cap);
            cap = NULL;
        }
    } else {
        if (mpegh_is_support) {
            sink_capability = (audio_format_t)AUDIO_FORMAT_MPEGH;
        }
        ALOGI ("%s mpegh support %d\n", __FUNCTION__, mpegh_is_support);
    }
    return sink_capability;
}

static void get_sink_pcm_capability(struct aml_audio_device *adev)
{
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);
    char *cap = NULL;
    hdmi_desc->pcm_fmt.sample_rate_mask = 0;

    cap = (char *) get_hdmi_sink_cap_new (AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, AUDIO_FORMAT_PCM_16_BIT,hdmi_desc, true);
    if (cap) {
        /*
         * bit:    6     5     4    3    2    1    0
         * rate: 192  176.4   96  88.2  48  44.1   32
         */
        if (strstr(cap, "32000") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<0);
        if (strstr(cap, "44100") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<1);
        if (strstr(cap, "48000") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<2);
        if (strstr(cap, "88200") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<3);
        if (strstr(cap, "96000") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<4);
        if (strstr(cap, "176400") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<5);
        if (strstr(cap, "192000") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<6);

        aml_audio_free(cap);
        cap = NULL;
    }

    ALOGI("pcm_fmt support sample_rate_mask:0x%x", hdmi_desc->pcm_fmt.sample_rate_mask);
}

static unsigned int get_sink_format_max_channels(struct aml_audio_device *adev, audio_format_t sink_format) {
    unsigned int max_channels = 2;
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);

    switch (sink_format) {
    case AUDIO_FORMAT_PCM_16_BIT:
        max_channels = hdmi_desc->pcm_fmt.max_channels;
        break;
    case AUDIO_FORMAT_AC3:
        max_channels = hdmi_desc->dd_fmt.max_channels;
        break;
    case AUDIO_FORMAT_E_AC3:
        max_channels = hdmi_desc->ddp_fmt.max_channels;
        break;
    case AUDIO_FORMAT_DTS:
        max_channels = hdmi_desc->dts_fmt.max_channels;
        break;
    case AUDIO_FORMAT_DTS_HD:
        max_channels = hdmi_desc->dtshd_fmt.max_channels;
        break;
    case AUDIO_FORMAT_MAT:
        max_channels = hdmi_desc->mat_fmt.max_channels;
        break;
    default:
        max_channels = 2;
        break;
    }
    if (max_channels == 0) {
        max_channels = 2;
    }
    return max_channels;
}

static bool get_sink_dv_capability()
{
    char buffer[128];
    FILE *fp = NULL;
    bool dv_enable = false;

    memset(buffer, 0, sizeof(buffer));
    fp = fopen(HDMI_HDR_STATUS_NODE, "r");
    if (fp) {
        int read_count = fread((char *)buffer, 1, sizeof(buffer)-1, fp);
        if (ferror(fp)) {
            ALOGE("%s : fread has IO wrong", __func__);
       }
        fclose(fp);
    }
    ALOGI("%s : %s = %s", __func__, HDMI_HDR_STATUS_NODE, buffer);

    if (strstr(buffer, SINK_DV_KEYWORD) != NULL) {
        dv_enable = true;
    }

    ALOGI("%s : dv enable %d", __func__, dv_enable);
    return dv_enable;
}

bool is_sink_support_dolby_passthrough(audio_format_t sink_capability)
{
    return sink_capability == AUDIO_FORMAT_MAT ||
        sink_capability == AUDIO_FORMAT_E_AC3 ||
        sink_capability == AUDIO_FORMAT_AC3;
}

/*
 *1. source format includes these formats:
 *        AUDIO_FORMAT_PCM_16_BIT = 0x1u
 *        AUDIO_FORMAT_AC3 =    0x09000000u
 *        AUDIO_FORMAT_E_AC3 =  0x0A000000u
 *        AUDIO_FORMAT_DTS =    0x0B000000u
 *        AUDIO_FORMAT_DTS_HD = 0x0C000000u
 *        AUDIO_FORMAT_AC4 =    0x22000000u
 *        AUDIO_FORMAT_MAT =    0x24000000u
 *
 *2. if the source format can output directly as PCM format or as IEC61937 format,
 *   btw, AUDIO_FORMAT_PCM_16_BIT < AUDIO_FORMAT_AC3 < AUDIO_FORMAT_MAT is true.
 *   we can use the min(a, b) to get an suitable output format.
 *3. if source format is AUDIO_FORMAT_AC4, we can not use the min(a,b) to get the
 *   suitable format but use the sink device max capability format.
 */
static audio_format_t get_suitable_output_format(struct aml_stream_out *out,
        audio_format_t source_format, audio_format_t sink_format)
{
    audio_format_t output_format;
    if (IS_EXTERNAL_DECODER_SUPPORT_FORMAT(source_format)) {
        output_format = MIN(source_format, sink_format);
        /*
         * if source: AUDIO_FORMAT_DOLBY_TRUEHD and sink: AUDIO_FORMAT_MAT
         * use the AUDIO_FORMAT_MAT as output format(from IEC 61937-1).
         */
        output_format = (output_format != AUDIO_FORMAT_DOLBY_TRUEHD) ? output_format: AUDIO_FORMAT_MAT;
    } else {
        output_format = sink_format;
    }
    if ((out->hal_rate == 32000 || out->hal_rate == 128000) && output_format == AUDIO_FORMAT_E_AC3) {
        output_format = AUDIO_FORMAT_AC3;
    }
    return output_format;
}

/*
 * When turn on the Automatic function to play the dolby audio in low speed,the TV might
 * can't decode.So if the play mode was Automatic and the output speed was in not equal
 * 1.0f, set the ret_format to AUDIO_FORMAT_PCM_16_BIT to send the PCM data to spdif.
 */
static audio_format_t reconfig_optical_audio_format(struct aml_stream_out *aml_out,
        audio_format_t org_optical_format)
{
    audio_format_t ret_format = org_optical_format;

    if (aml_out == NULL)
        return org_optical_format;

    if (aml_out->output_speed != 1.0f && aml_out->output_speed != 0.0f) {
        ALOGI("change to micro speed need reconfig optical audio format to PCM");
        ret_format = AUDIO_FORMAT_PCM_16_BIT;
    }

    return ret_format;
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
void get_sink_format(struct audio_stream_out *stream)
{
   if (stream == NULL) {
        ALOGE("stream NULL");
        return;
    }
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    /*set default value for sink_audio_format/optical_audio_format*/
    audio_format_t sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
    audio_format_t optical_audio_format = AUDIO_FORMAT_PCM_16_BIT;

    audio_format_t sink_capability = get_sink_capability(adev);
    audio_format_t sink_dts_capability = get_sink_dts_capability(adev);
    audio_format_t sink_mpegh_capability = get_sink_mpegh_capability(adev);
    audio_format_t source_format = aml_out->hal_internal_format;

    get_sink_pcm_capability(adev);

    adev->bDVEnable = get_sink_dv_capability();

    if (adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP || adev->out_device & AUDIO_DEVICE_OUT_ALL_USB) {
        ALOGD("get_sink_format: a2dp and usb set to pcm");
        adev->sink_format = AUDIO_FORMAT_PCM_16_BIT;
        adev->sink_capability = AUDIO_FORMAT_PCM_16_BIT;
        adev->optical_format = AUDIO_FORMAT_PCM_16_BIT;
        aml_out->dual_output_flag = false;
        adev->sink_max_channels = 2;
        return;
    }

    if (adev->is_netflix && adev->aaudio_low_latency) {
        unsigned int max_channels = get_sink_format_max_channels(adev, AUDIO_FORMAT_PCM_16_BIT);

        ALOGD("get_sink_format: netflix aaudio_low_latency set to pcm");
        adev->sink_format = AUDIO_FORMAT_PCM_16_BIT;
        adev->sink_capability = AUDIO_FORMAT_PCM_16_BIT;
        adev->optical_format = AUDIO_FORMAT_PCM_16_BIT;

        if (aml_out->hal_ch > 2 && max_channels >= aml_out->hal_ch) {
            adev->sink_max_channels = max_channels;
        } else {
            adev->sink_max_channels = 2;
        }
        return;
    }

    /*when device is HDMI_ARC*/
    ALOGI("!!!%s() Sink devices %#x Source format %#x digital_format(hdmi_format) %#x Sink Capability %#x\n",
          __FUNCTION__, adev->cur_out_devices, aml_out->hal_internal_format, adev->digital_audio_format, sink_capability);

    if ((source_format != AUDIO_FORMAT_PCM_16_BIT) && \
        (source_format != AUDIO_FORMAT_AC3) && \
        (source_format != AUDIO_FORMAT_E_AC3) && \
        (source_format != AUDIO_FORMAT_MAT) && \
        (source_format != AUDIO_FORMAT_AC4) && \
        (source_format != AUDIO_FORMAT_DTS) &&
        (source_format != AUDIO_FORMAT_DTS_HD) && \
        (source_format != AUDIO_FORMAT_DOLBY_TRUEHD) && \
        (source_format != AUDIO_FORMAT_AAC) && \
        (source_format != AUDIO_FORMAT_AAC_LATM) && \
        (source_format != AUDIO_FORMAT_HE_AAC_V1) && \
        (source_format != AUDIO_FORMAT_HE_AAC_V2) && \
        (source_format != AUDIO_FORMAT_MPEGH)) {
        /*unsupport format [dts-hd/true-hd]*/
        ALOGI("%s() source format %#x change to %#x", __FUNCTION__, source_format, AUDIO_FORMAT_PCM_16_BIT);
        source_format = AUDIO_FORMAT_PCM_16_BIT;
    }
    adev->sink_capability = sink_capability;

    // "adev->digital_audio_format" is the UI selection item.
    // "adev->active_outport" was set when HDMI ARC cable plug in/off
    // condition 1: ARC port, single output.
    // condition 2: for STB case with dolby-ms12 libs
    // condition 3: T7 BDS with HDMITX case
    if ((adev->cur_out_devices & AUDIO_DEVICE_OUT_HDMI_ARC) != 0 || !is_TV(adev) || is_BDS(adev)) {
        struct audio_board_config *bd_config = &adev->board_config;
        ALOGI("%s() HDMI ARC or mbox + dvb case", __FUNCTION__);
        switch (adev->digital_audio_format) {
        case PCM:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = sink_audio_format;
            break;
        case DD:
            if (dts_stream_active(adev)) {
                sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            } else {
                sink_audio_format = get_suitable_output_format(aml_out, AUDIO_FORMAT_AC3, sink_capability);
            }
            optical_audio_format = sink_audio_format;
            break;
        case AUTO:
            if (is_dts_format(source_format)) {
                sink_audio_format = MIN(source_format, sink_dts_capability);
            } else {
                sink_audio_format = get_suitable_output_format(aml_out, source_format, sink_capability);
            }
            if (eDolbyMS12Lib == adev->dolby_lib_type && !is_dts_format(source_format)) {
                sink_audio_format = MIN(ms12_max_support_output_format(), sink_capability);
            }
            optical_audio_format = sink_audio_format;

            /*if the sink device only support pcm, we check whether we can output dd or dts to spdif,
             *For arc case, we can't support pcm and dd dual output, so we limit it to non arc case
             */
            if (bd_config->spdif_independent && ((adev->cur_out_devices & AUDIO_DEVICE_OUT_HDMI_ARC) == 0)) {
                if (sink_audio_format == AUDIO_FORMAT_PCM_16_BIT) {
                    if (is_dts_format(source_format)) {
                        optical_audio_format = MIN(source_format, AUDIO_FORMAT_DTS);
                    } else {
                        if (eDolbyMS12Lib == adev->dolby_lib_type) {
                            optical_audio_format = AUDIO_FORMAT_AC3;
                        } else {
                            optical_audio_format = MIN(source_format, AUDIO_FORMAT_AC3);
                        }
                    }
                }
            }

            optical_audio_format = reconfig_optical_audio_format(aml_out, optical_audio_format);
            break;
        case BYPASS:
            if (is_dts_format(source_format)) {
                sink_audio_format = MIN(source_format, sink_dts_capability);
            } else if (is_mpegh_format(source_format)) {
                sink_audio_format = sink_mpegh_capability;
            } else {
                sink_audio_format = get_suitable_output_format(aml_out, source_format, sink_capability);
            }
            optical_audio_format = sink_audio_format;
            /*if the sink device only support pcm, we check whether we can output dd or dts to spdif*/
            if (bd_config->spdif_independent && ((adev->cur_out_devices & AUDIO_DEVICE_OUT_HDMI_ARC) == 0)) {
                if (sink_audio_format == AUDIO_FORMAT_PCM_16_BIT) {
                    if (is_dts_format(source_format)) {
                        optical_audio_format = MIN(source_format, AUDIO_FORMAT_DTS);
                    } else {
                        optical_audio_format = MIN(source_format, AUDIO_FORMAT_AC3);
                    }
                }
            }

            break;
        default:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = sink_audio_format;
            break;
        }
    }
    /*when device is SPEAKER/HEADPHONE*/
    else {
        ALOGI("%s() SPEAKER/HEADPHONE case", __FUNCTION__);
        switch (adev->digital_audio_format) {
        case PCM:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = sink_audio_format;
            break;
        case DD:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = AUDIO_FORMAT_AC3;
            if (dts_stream_active(adev)) {
                optical_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            }
            break;
        case AUTO:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = (source_format != AUDIO_FORMAT_DTS && source_format != AUDIO_FORMAT_DTS_HD)
                                   ? MIN(source_format, AUDIO_FORMAT_AC3)
                                   : AUDIO_FORMAT_DTS;

            if (eDolbyMS12Lib == adev->dolby_lib_type && !is_dts_format(source_format)) {
                optical_audio_format = AUDIO_FORMAT_AC3;
            }

            optical_audio_format = reconfig_optical_audio_format(aml_out, optical_audio_format);
            break;
        case BYPASS:
           sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
           if (is_dts_format(source_format)) {
               optical_audio_format = MIN(source_format, AUDIO_FORMAT_DTS);
           } else {
               optical_audio_format = MIN(source_format, AUDIO_FORMAT_AC3);
           }
           break;
        default:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = sink_audio_format;
            break;
        }
    }
    if (adev->sink_format != sink_audio_format) {
        adev->sink_format_changed = true;
    }
    adev->sink_format = sink_audio_format;
    adev->optical_format = optical_audio_format;
    adev->sink_max_channels = get_sink_format_max_channels(adev, adev->sink_format);

    /* set the dual output format flag */
    if (adev->sink_format != adev->optical_format) {
        aml_out->dual_output_flag = true;
    } else {
        aml_out->dual_output_flag = false;
    }

    ALOGI("%s sink_format %#x max channel =%d optical_format %#x, dual_output %d\n",
           __FUNCTION__, adev->sink_format, adev->sink_max_channels, adev->optical_format, aml_out->dual_output_flag);
    return ;
}





bool is_dual_output_stream(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    if (aml_out == NULL)
        return 0;

    return aml_out->dual_output_flag;
}






const char *write_func_strs[MIXER_WRITE_FUNC_MAX] = {
    "OUT_WRITE_NEW",
    "MIXER_AUX_BUFFER_WRITE_SM",
    "MIXER_MAIN_BUFFER_WRITE_SM,"
    "MIXER_MMAP_BUFFER_WRITE_SM"
    "MIXER_AUX_BUFFER_WRITE",
    "MIXER_MAIN_BUFFER_WRITE",
    "MIXER_APP_BUFFER_WRITE",
    "PROCESS_BUFFER_WRITE,"
};

const char *write_func_to_str(enum stream_write_func func)
{
    return write_func_strs[func];
}

/*this should match with stream_status_t in audio_hw.h*/
const char *stream_status_2_string[STREAM_STATUS_MAX] = {
    "_STANDBY",
    "_HW_WRITING",
    "_MIXING",
    "_PAUSED"
};

void aml_stream_out_info_print(struct aml_stream_out *aml_out, uint64_t *frames, struct timespec *timestamp)
{
    struct aml_audio_device *adev = aml_out->dev;
    struct timespec *cur_timestamp = timestamp;

    uint64_t cur_info_time_in_ms = ((uint64_t)cur_timestamp->tv_sec * 1000 + (uint64_t)cur_timestamp->tv_nsec / 1000000);
    uint64_t last_info_timestamp_in_ms = ((uint64_t)aml_out->last_info_timestamp.tv_sec * 1000 + (uint64_t)aml_out->last_info_timestamp.tv_nsec / 1000000);

    int64_t time_gap = cur_info_time_in_ms - last_info_timestamp_in_ms;
    int64_t position_gap = (*frames - aml_out->last_frame_reported) / (aml_out->hal_rate / 1000);

    /* Print the audio stream out log if one of below conditions is true.
     *
     * 1. Print per 5 seconds.
     * 2. Time gap between last and current output system time exceeds threshold.
     * 3. The absolute value of jitter is over the threshold.
     * 4. Debug flag is enabled.
     */
    if (llabs(aml_out->jitter_ms) > JITTER_PRINT_THRESHOLD
        || cur_info_time_in_ms - aml_out->last_periodic_print_time_in_ms > 5000
        || adev->debug_flag > 1) {
        char *stream_type = audio_is_linear_pcm(aml_out->hal_format) ? "pcm" : "raw";
        char *sync_mode = aml_out->hw_sync_mode ? "tunnel" : "non tunnel";
        char *jitter_case = aml_out->jitter_ms >= 0 ?
            "Position gap is ahead of system time gap by" : "Position gap is behind system time gap by";

        ALOGI("%s: stream:%p, stream_type:%s, sync_mode:%s, input_size:%"PRIu64" bytes\n"
                "%s: last_time:%"PRIu64" ms (sec:%ld, nsec:%ld), last_position:%"PRIu64" ms (%"PRIu64"), "
                "cur_time:%"PRIu64" ms (sec:%ld, nsec:%ld), cur_position:%"PRIu64" ms (%"PRIu64")\n"
                "%s: time_gap:%"PRId64" ms (thr:%d ms), position_gap:%"PRId64" ms, delay:%d ms, jitter: %s %"PRId64" ms (thr:%d ms)",
            __func__, aml_out, stream_type, sync_mode, aml_out->input_bytes_size,
            __func__, last_info_timestamp_in_ms, aml_out->last_info_timestamp.tv_sec,
            aml_out->last_info_timestamp.tv_nsec, aml_out->last_frame_reported / (aml_out->hal_rate / 1000), aml_out->last_frame_reported,
            cur_info_time_in_ms, cur_timestamp->tv_sec, cur_timestamp->tv_nsec, *frames / (aml_out->hal_rate / 1000), *frames,
            __func__, time_gap, INFO_TIME_PRINT_THRESHOLD, position_gap,
            aml_out->audio_delay / (aml_out->hal_rate / 1000), jitter_case, (int64_t)llabs(aml_out->jitter_ms), JITTER_PRINT_THRESHOLD);

        if (cur_info_time_in_ms - aml_out->last_periodic_print_time_in_ms > 5000) {
            aml_out->last_periodic_print_time_in_ms = cur_info_time_in_ms;
        }
    }

    aml_out->last_info_timestamp.tv_sec = cur_timestamp->tv_sec;
    aml_out->last_info_timestamp.tv_nsec = cur_timestamp->tv_nsec;

    return;
}



void aml_stream_out_dump(struct aml_stream_out *aml_out, int fd)
{
    if (aml_out) {
        dprintf(fd, "    usecase: %s\n", usecase2Str(aml_out->usecase));
        dprintf(fd, "    out device: %#x\n", aml_out->out_device);
        dprintf(fd, "    is tv source stream: %s\n", aml_out->is_tv_src_stream?"true":"false");
        dprintf(fd, "    stream status:%d %s\n", aml_out->stream_status, stream_status_2_string[aml_out->stream_status]);
        dprintf(fd, "    standby: %s\n", aml_out->standby?"true":"false");
        if (aml_out->is_normal_pcm) {
            dprintf(fd, "    normal pcm: %s\n",
                write_func_to_str(aml_out->write_func));
        }
    }
}

int aml_dev_dump_latency(struct aml_audio_device *aml_dev, int fd)
{
    struct aml_stream_in *in = aml_dev->active_input;
    struct aml_audio_patch *patch = get_dev_patch(aml_dev);

    dprintf(fd, "-------------[AML_HAL] audio Latency--------------------------\n");

    if (patch) {
        aml_dev_sample_audio_path_latency(aml_dev, NULL);
        dprintf(fd, "[AML_HAL]      audio patch latency         : %6d ms\n", patch->audio_latency.ringbuffer_latency);
        dprintf(fd, "[AML_HAL]      audio spk tuning latency    : %6d ms\n", patch->audio_latency.user_tune_latency);
        dprintf(fd, "[AML_HAL]      MS12 buffer latency         : %6d ms\n", patch->audio_latency.ms12_latency);
        dprintf(fd, "[AML_HAL]      alsa out hw i2s latency     : %6d ms\n", patch->audio_latency.alsa_i2s_out_latency);
        dprintf(fd, "[AML_HAL]      alsa out hw spdif latency   : %6d ms\n", patch->audio_latency.alsa_spdif_out_latency);
        dprintf(fd, "[AML_HAL]      alsa in hw latency          : %6d ms\n\n", patch->audio_latency.alsa_in_latency);
        dprintf(fd, "[AML_HAL]      audio total latency         :%6d ms\n", patch->audio_latency.total_latency);

        int v_ltcy = aml_dev_sample_video_path_latency(patch);
        if (v_ltcy > 0) {
            dprintf(fd, "[AML_HAL]      video path total latency    : %6d ms\n", v_ltcy);
        } else {
            dprintf(fd, "[AML_HAL]      video path total latency    : N/A\n");
        }
    }
    return 0;
}

void aml_alsa_device_status_dump(struct aml_audio_device* aml_dev, int fd)
{
    dprintf(fd, "\n-------------[AML_HAL]  ALSA devices status ---------------\n");
    bool stream_using = false;
    /* StreamOut using alsa devices list */
    for (int i = 0; i < ALSA_DEVICE_CNT; i++) {
        pthread_mutex_lock(&aml_dev->lock);
        pthread_mutex_lock(&aml_dev->alsa_pcm_lock);
        struct pcm *pcm_1 = aml_dev->pcm_handle[i];
        void *alsa_handle = aml_dev->alsa_handle[i];
        if (!pcm_1 && !alsa_handle) {
            pthread_mutex_unlock(&aml_dev->alsa_pcm_lock);
            pthread_mutex_unlock(&aml_dev->lock);
            continue;
        }

        if (!stream_using) {
            dprintf(fd, "  [AML_HAL] StreamOut using PCM list:\n");
            stream_using = true;
        }

        if (pcm_1) {
            aml_alsa_pcm_info_dump(pcm_1, fd);
        }

        if (alsa_handle) {
            struct pcm* pcm_2 = (struct pcm*) get_internal_pcm(alsa_handle);
            aml_alsa_pcm_info_dump(pcm_2, fd);
        }
        pthread_mutex_unlock(&aml_dev->alsa_pcm_lock);
        pthread_mutex_unlock(&aml_dev->lock);
    }
    if (!stream_using) {
        dprintf(fd, "  [AML_HAL] StreamOut using PCM list: None!\n");
    }

    /* Mixer outport using alsa devices list */
    dprintf(fd, "  [AML_HAL] mixer using PCM list:\n");
    mixer_using_alsa_device_dump(fd, aml_dev);

    /* StreamIn using alsa devices list */
    pthread_mutex_lock(&aml_dev->lock);
    stream_using = false;
    if (aml_dev->active_input) {
        struct pcm *pcm = aml_dev->active_input->pcm;
        if (pcm) {
            stream_using = true;
            dprintf(fd, "  [AML_HAL] StreamIn using PCM list:\n");
            aml_alsa_pcm_info_dump(pcm, fd);
        }
    }
    if (!stream_using) {
       dprintf(fd, "  [AML_HAL] StreamIn using PCM list: None!\n");
    }
    pthread_mutex_unlock(&aml_dev->lock);
}


bool is_use_spdifb(struct aml_stream_out *out) {
    struct aml_audio_device *adev = out->dev;
    /*this patch is for DCV DDP noise.
    **DCV have two kinds of mode, DCV decoder and passthrough.
    **the dolby_decode_enable is 0 when DCV passthrough.
    **so here should remove the dolby_decode_enable judgment.
    */
    if (eDolbyDcvLib == adev->dolby_lib_type /*&& adev->dolby_decode_enable*/ &&
        (out->hal_format == AUDIO_FORMAT_E_AC3 || out->hal_internal_format == AUDIO_FORMAT_E_AC3 ||
        (out->need_convert && out->hal_internal_format == AUDIO_FORMAT_AC3))) {
        /*dual spdif we need convert
          or non dual spdif, we need check audio setting and optical format*/
        if (adev->dual_spdif_support) {
            out->dual_spdif = true;
        }
        if (out->dual_spdif && ((adev->digital_audio_format == AUTO) &&
            adev->optical_format == AUDIO_FORMAT_E_AC3) &&
            out->hal_rate != 32000) {
            return true;
        }
    }

    return false;
}

bool is_dolby_ms12_support_compression_format(audio_format_t format)
{

#ifdef MS12_V24_ENABLE
    if (format == AUDIO_FORMAT_HE_AAC_V1 ||
        format == AUDIO_FORMAT_HE_AAC_V2 ||
        format == AUDIO_FORMAT_AAC ||
        format == AUDIO_FORMAT_AAC_LATM)  {
        if (property_get_bool("ro.vendor.audio.use.ms12heaac", false)) {
            return true;
        }
    }
#endif

    return (format == AUDIO_FORMAT_AC3 ||
            format == AUDIO_FORMAT_E_AC3 ||
            format == AUDIO_FORMAT_E_AC3_JOC ||
            format == AUDIO_FORMAT_DOLBY_TRUEHD ||
            format == AUDIO_FORMAT_AC4 ||
            format == AUDIO_FORMAT_MAT);
}

bool is_dolby_ddp_support_compression_format(audio_format_t format)
{
    return (format == AUDIO_FORMAT_AC3 ||
            format == AUDIO_FORMAT_E_AC3 ||
            format == AUDIO_FORMAT_E_AC3_JOC);
}

bool is_direct_stream_and_pcm_format(struct aml_stream_out *out)
{
    return audio_is_linear_pcm(out->hal_internal_format) && (out->flags & AUDIO_OUTPUT_FLAG_DIRECT);
}

bool is_mmap_stream_and_pcm_format(struct aml_stream_out *out)
{
    return audio_is_linear_pcm(out->hal_internal_format) && (out->flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ);
}

void get_audio_indicator(struct aml_audio_device *dev, char *temp_buf) {
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;

    if (adev->audio_hal_info.update_type == TYPE_PCM)
        sprintf (temp_buf, "audioindicator=");
    else if (adev->audio_hal_info.update_type == TYPE_DTS_EXPRESS)
        sprintf (temp_buf, "audioindicator=DTS EXPRESS");
    else if (adev->audio_hal_info.update_type == TYPE_AC3)
        sprintf (temp_buf, "audioindicator=Dolby AC3");
    else if (adev->audio_hal_info.update_type == TYPE_EAC3)
        sprintf (temp_buf, "audioindicator=Dolby EAC3");
    else if (adev->audio_hal_info.update_type == TYPE_AC4)
        sprintf (temp_buf, "audioindicator=Dolby AC4");
    else if (adev->audio_hal_info.update_type == TYPE_MAT)
        sprintf (temp_buf, "audioindicator=Dolby MAT");
    else if (adev->audio_hal_info.update_type == TYPE_TRUE_HD)
        sprintf (temp_buf, "audioindicator=Dolby THD");
    else if (adev->audio_hal_info.update_type == TYPE_DDP_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby EAC3,Dolby Atmos");
    else if (adev->audio_hal_info.update_type == TYPE_TRUE_HD_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby THD,Dolby Atmos");
    else if (adev->audio_hal_info.update_type == TYPE_MAT_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby MAT,Dolby Atmos");
    else if (adev->audio_hal_info.update_type == TYPE_AC4_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby AC4,Dolby Atmos");
    else if (adev->audio_hal_info.update_type == TYPE_DTS)
        sprintf (temp_buf, "audioindicator=DTS");
    else if (adev->audio_hal_info.update_type == TYPE_DTS_HD_MA)
        sprintf (temp_buf, "audioindicator=DTS HD");
    else if (adev->audio_hal_info.update_type == TYPE_DDP_ATMOS_PROMPT_ON_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby EAC3");
    else if (adev->audio_hal_info.update_type == TYPE_TRUE_HD_ATMOS_PROMPT_ON_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby THD");
    else if (adev->audio_hal_info.update_type == TYPE_MAT_ATMOS_PROMPT_ON_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby MAT");
    else if (adev->audio_hal_info.update_type == TYPE_AC4_ATMOS_PROMPT_ON_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby AC4");

    ALOGI("%s(), [%s]", __func__, temp_buf);
}

static int update_audio_hal_info(struct aml_audio_device *adev, audio_format_t format, int atmos_flag)
{
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int update_type = get_codec_type(format);
    int update_threshold = DOLBY_FMT_UPDATE_THRESHOLD;
    int cur_aml_dap_surround_virtualizer = dolby_ms12_get_dap_surround_virtualizer();

    if (is_dolby_ms12_support_compression_format(format)) {
        update_threshold = DOLBY_FMT_UPDATE_THRESHOLD;
    } else if (is_dts_format(format)) {
        update_threshold = DTS_FMT_UPDATE_THRESHOLD;
    }

    /* avoid the value out-of-bounds */
    if (adev->audio_hal_info.update_cnt < FMT_UPDATE_THRESHOLD_MAX) {
        adev->audio_hal_info.update_cnt++;
    }

    /* Check whether the update_type is stable or not as bellow. */
    bool is_virt_updated_off_vs_on_auto = (!!adev->audio_hal_info.aml_dap_surround_virtualizer != !!cur_aml_dap_surround_virtualizer);
    bool is_virt_updated_for_aml_atmos = (atmos_flag && is_virt_updated_off_vs_on_auto);

    if ((format != adev->audio_hal_info.format) ||
        (atmos_flag != adev->audio_hal_info.is_dolby_atmos) ||
        is_virt_updated_for_aml_atmos) {
        adev->audio_hal_info.update_cnt = 0;
    }

    /* @audio_format_t does not include dts_express.
     * So we get the format(dts_express especially) from the decoder(@dts_hd.stream_type).
     * @dts_hd.stream_type is updated after decoding at least one frame.
     */
    if (is_dts_format(format)) {
        if (adev->dts_hd.stream_type <= 0 /*TYPE_PCM*/) {
            adev->audio_hal_info.update_cnt = 0;
        }

        update_type = adev->dts_hd.stream_type;
        if (update_type != adev->audio_hal_info.update_type) {
            adev->audio_hal_info.update_cnt = 0;
        }
    }

    bool is_dolby_atmos_off = (MS12_DAP_SPEAKER_VIRTUALIZER_OFF == cur_aml_dap_surround_virtualizer);
    if (atmos_flag == 1) {
        if (format == AUDIO_FORMAT_E_AC3)
            update_type = (is_dolby_atmos_off) ? TYPE_DDP_ATMOS_PROMPT_ON_ATMOS : TYPE_DDP_ATMOS;
        else if (format == AUDIO_FORMAT_DOLBY_TRUEHD)
            update_type = (is_dolby_atmos_off) ? TYPE_TRUE_HD_ATMOS_PROMPT_ON_ATMOS : TYPE_TRUE_HD_ATMOS;
        else if (format == AUDIO_FORMAT_MAT)
            update_type = (is_dolby_atmos_off) ? TYPE_MAT_ATMOS_PROMPT_ON_ATMOS : TYPE_MAT_ATMOS;
        else if (format == AUDIO_FORMAT_AC4)
            update_type = (is_dolby_atmos_off) ? TYPE_AC4_ATMOS_PROMPT_ON_ATMOS : TYPE_AC4_ATMOS;
    }

    ALOGV("%s() update_cnt %d format %#x vs hal_internal_format %#x  atmos_flag %d vs is_dolby_atmos %d update_type %d\n",
        __FUNCTION__, adev->audio_hal_info.update_cnt, format, adev->audio_hal_info.format,
        atmos_flag, adev->audio_hal_info.is_dolby_atmos, update_type);

    adev->audio_hal_info.format = format;
    adev->audio_hal_info.is_dolby_atmos = atmos_flag;
    adev->audio_hal_info.update_type = update_type;
    adev->audio_hal_info.aml_dap_surround_virtualizer = cur_aml_dap_surround_virtualizer;

    if (adev->audio_hal_info.update_cnt == update_threshold) {

        if ((format == AUDIO_FORMAT_DTS || format == AUDIO_FORMAT_DTS_HD) && adev->dts_hd.is_headphone_x) {
            aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_AUDIO_HAL_FORMAT, TYPE_DTS_HP);
        }
        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_AUDIO_HAL_FORMAT, update_type);
        ALOGD("%s() audio hal format change to %x, atmos flag = %d, is_dolby_atmos = %d, dts_hp_x = %d, update_type = %d is_dolby_atmos_off = %d\n",
            __FUNCTION__, adev->audio_hal_info.format, adev->audio_hal_info.is_dolby_atmos, adev->ms12.is_dolby_atmos,
            adev->dts_hd.is_headphone_x, adev->audio_hal_info.update_type, is_dolby_atmos_off);
        ALOGD("%s() cur_out_devices %#x, dap_bypass_enable = %d, is_ms12_tuning_dat = %d, dolby_ms12_enable = %d, output_config = %#x\n",
            __FUNCTION__, adev->cur_out_devices, adev->ms12.dap_bypass_enable, adev->is_ms12_tuning_dat, ms12->dolby_ms12_enable, ms12->output_config);
    }

    return 0;
}

void update_audio_format(struct aml_audio_device *adev, audio_format_t format)
{
    int atmos_flag = 0;
    int update_type = TYPE_PCM;
    bool is_dolby_active = dolby_stream_active(adev);
    bool is_dolby_format = is_dolby_ms12_support_compression_format(format);
    bool is_dts_active = dts_stream_active(adev);
    /*
     *for dolby & pcm case or dolby case
     *to update the dolby stream's format
     */
    if (is_dolby_active && is_dolby_format) {
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            atmos_flag = adev->ms12.is_dolby_atmos;
        }

#ifdef MS12_V24_ENABLE
        /* when DAP is not in audio postprocessing, there is no ATMOS Experience. */
        if (is_audio_postprocessing_add_dolbyms12_dap(adev) == 0) {
            atmos_flag = 0;
        }
#else
        atmos_flag = 0; /* Todo: MS12 V1, how to indicate the Dolby ATMOS? */
#endif

        update_audio_hal_info(adev, format, atmos_flag);
    }
    /*
     *to update the audio format for other cases
     *DTS-format / DTS format & Mixer-PCM
     *only Mixer-PCM
     */
    else if (!is_dolby_active && !is_dolby_format) {

        /* if there is DTS/DTS alive, only update DTS/DTS-HD */
        if (is_dts_active) {
            if (is_dts_format(format)) {
                update_audio_hal_info(adev, format, false);
            }
            /*else case is PCM format, will ignore that PCM*/
        }

        /* there is none-dolby or none-dts alive, then update PCM format*/
        if (!is_dts_active) {
            update_audio_hal_info(adev, format, false);
        }
    }
    /*
     * **Dolby stream is active, and get the Mixer-PCM case steam format,
     * **we should ignore this Mixer-PCM update request.
     * else if (is_dolby_active && !is_dolby_format) {
     * }
     * **If Dolby steam is not active, the available format is LPCM or DTS
     * **The following case do not exit at all **
     //else //(!is_dolby_active && is_dolby_format) {
     * }
     */
}


int update_sink_format_after_hotplug(struct aml_audio_device *adev)
{
    struct audio_stream_out *stream = NULL;
    /* raw stream is at high priority */
    if (adev->active_outputs[STREAM_RAW_DIRECT]) {
        stream = (struct audio_stream_out *)adev->active_outputs[STREAM_RAW_DIRECT];
    }
    else if (adev->active_outputs[STREAM_RAW_HWSYNC]) {
        stream = (struct audio_stream_out *)adev->active_outputs[STREAM_RAW_HWSYNC];
    }
    /* pcm direct/hwsync stream is at medium priority */
    else if (adev->active_outputs[STREAM_PCM_HWSYNC]) {
        stream = (struct audio_stream_out *)adev->active_outputs[STREAM_PCM_HWSYNC];
    }
    else if (adev->active_outputs[STREAM_PCM_DIRECT]) {
        stream = (struct audio_stream_out *)adev->active_outputs[STREAM_PCM_DIRECT];
    }
    /* pcm stream from mixer is at lower priority */
    else if (adev->active_outputs[STREAM_PCM_NORMAL]){
        stream = (struct audio_stream_out *)adev->active_outputs[STREAM_PCM_NORMAL];
    }


    if (stream) {
        ALOGD("%s() active stream %p\n", __FUNCTION__, stream);
        get_sink_format(stream);
    }
    else {
        if (adev->ms12_out && continuous_mode(adev)) {
            ALOGD("%s() active stream is ms12_out %p\n", __FUNCTION__, adev->ms12_out);
            get_sink_format((struct audio_stream_out *)adev->ms12_out);
        }
        else {
            ALOGD("%s() active stream %p ms12_out %p\n", __FUNCTION__, stream, adev->ms12_out);
        }
    }
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        audiohal_send_msg_2_ms12(&adev->ms12, MS12_MESG_TYPE_RESET_MS12_ENCODER);
    }

    return 0;
}


uint32_t tv_in_write(struct audio_stream_out *stream, const void* buffer, size_t bytes)
{
    R_CHECK_POINTER_LEGAL(bytes, stream, "");
    R_CHECK_POINTER_LEGAL(bytes, buffer, "");
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct aml_audio_patch *patch = get_dev_patch(adev);
    R_CHECK_POINTER_LEGAL(bytes, patch, "");
    if (bytes == 0 || patch->tvin_buffer_inited != 1) {
        return bytes;
    }

    if (patch->is_dtv_src && patch->tvin_buffer_inited && patch->first_apts_lookup_over) {
        /* dtv case: process drop and mute policy */
        if (patch->need_drop_size > 0) {
            ALOGI("%s, dtv avsync drop %d bytes, need_drop_size %d\n", __func__, (int)bytes, patch->need_drop_size);
            return bytes;
        }
        if (is_dtv_discontinue_mute(adev) || is_dtv_start_mute(adev) || !patch->dtv_first_apts_flag) {
            ALOGI("%s, dtv avsync mute %d bytes, start_mute %d, discontinue_mute %d", __func__, (int)bytes,
                is_dtv_start_mute(adev), is_dtv_discontinue_mute(adev));
            memset((char *)buffer, 0, bytes);
        }
    }

    uint32_t bytes_written = 0;
    uint32_t wr_size = bytes;
    uint32_t full_count = 0;
    while (bytes_written < bytes) {
        uint32_t sent = ring_buffer_write(&patch->tvin_ringbuffer, (uint8_t *)buffer + bytes_written, bytes - bytes_written, UNCOVER_WRITE);
        AM_LOGV("need_write:%zu, actual sent:%d, bytes:%zu", (bytes - bytes_written), sent, bytes);
        bytes_written += sent;
        if (bytes_written == bytes) {
            if (adev->debug_flag) {
                AM_LOGD("write finished, bytes:%zu, timeout:%d ms", bytes, 5 * full_count);
            }
            return bytes;
        }
        full_count++;
        if (full_count >= 20) {
            AM_LOGW("write data timeout 100ms, need write:%zu, bytes_written:%d, reset buffer", bytes, bytes_written);
            ring_buffer_reset(&patch->tvin_ringbuffer);
            return bytes_written;
        }
        usleep(5000);
    }
    return bytes;
}

uint32_t tv_in_read(struct audio_stream_in *stream, void* buffer, size_t bytes)
{
    R_CHECK_POINTER_LEGAL(bytes, stream, "");
    R_CHECK_POINTER_LEGAL(bytes, buffer, "");
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    struct aml_audio_patch *patch = get_dev_patch(adev);
    R_CHECK_POINTER_LEGAL(bytes, patch, "");
    if (bytes == 0 || patch->tvin_buffer_inited != 1) {
        memset(buffer, 0, bytes);
        return bytes;
    }

    uint32_t read_bytes = 0;
    uint32_t nodata_count = 0;
    while (read_bytes < bytes) {
        uint32_t ret = ring_buffer_read(&patch->tvin_ringbuffer, (uint8_t *)buffer + read_bytes, bytes - read_bytes);
        AM_LOGV("need_write:%zu, actual sent:%d, bytes:%zu", (bytes - read_bytes), ret, bytes);
        read_bytes += ret;
        if (read_bytes == bytes) {
            if (adev->debug_flag) {
                int available = get_buffer_read_space(&patch->tvin_ringbuffer);
                AM_LOGD("read finished, bytes:%zu, timeout:%d ms, available:%d", bytes, 5 * nodata_count, available);
            }
            return bytes;
        }
        nodata_count++;
        if (nodata_count >= 20) {
            AM_LOGW("read data timeout 100ms, need:%zu, read_bytes:%d", bytes, read_bytes);
            return read_bytes;
        }
        usleep(5000);
    }
    return bytes;
}


int aml_audio_earctx_get_type(struct aml_audio_device *adev)
{
    int attend_type = 0;

    attend_type = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_TX_ATTENDED_TYPE);
    return attend_type;
}

int aml_audio_earc_get_latency(struct aml_audio_device *adev)
{
    int latency = 0;

    latency = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_TX_LATENCY);
    return latency;
}

#define AML_DETECT_VALUE 1500
void tv_do_ease_in(struct audio_stream_out *stream, void *write_buf, size_t write_bytes) {

    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = out->dev;
    int fade_mode = property_get_int32("vendor.dtv.audio.fade_mode", DO_FADE_AT_HAL);
    switch (fade_mode) {
        case DO_FADE_AT_ALSA:
            if (aml_dev->mute_start) {
                /* fade in start */
                ALOGI("start fade in fade_mode %d", fade_mode);
                set_output_device_mute(aml_dev, AUDIO_DEVICE_OUT_SPEAKER, false, true);
                aml_dev->mute_start = false;
            }
            break;
        case DO_FADE_AT_HAL:
            if (eDolbyMS12Lib == aml_dev->dolby_lib_type &&
                is_dolby_ms12_support_compression_format(out->hal_internal_format)) {
                ALOGV("dolby raw data, no need check ");
            } else {
                int ret = aml_audio_data_detect((int16_t *)write_buf, write_bytes , AML_DETECT_VALUE);
                if (ret == true)  {
                    return;
                }
            }
            if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
                struct dolby_ms12_desc *ms12 = &(aml_dev->ms12);
                if (aml_dev->tv_mute) {
                    if (!ms12->is_muted) {
                        set_ms12_main_audio_mute(ms12, true, 0);
                    }
                } else {
                    if (aml_dev->mute_start) {
                         int fade_duration = MS12_AUDIO_FADEIN_TV_DURATION_US / 1000;
                         ALOGI("ms12 render easing in using %d ms ",fade_duration);
                         set_ms12_main_audio_mute(ms12, false, fade_duration);
                         aml_dev->mute_start = false;
                    }
                }
            } else {
                if (aml_dev->mute_start)  {
                    /* fade in start */
                    ALOGI("start fade in");
                    start_ease_in(aml_dev->audio_ease);
                    aml_dev->mute_start = false;
                }
                if (is_dev_patch_running(aml_dev)) {
                      /*ease in or ease out*/
                     aml_audio_ease_process(aml_dev->audio_ease, write_buf, write_bytes);
                }
            }
            break;
        default:
            ALOGW("invalid fade mode %d", fade_mode);
    }

}

void tv_do_ease_out(struct aml_audio_device *aml_dev)
{

    int fade_mode = property_get_int32("vendor.dtv.audio.fade_mode", DO_FADE_AT_ALSA);
    int duration_ms = 0;

    switch (fade_mode) {
        case DO_FADE_AT_ALSA:
            if (aml_dev->mute_start == false) {
                set_output_device_mute(aml_dev, AUDIO_DEVICE_OUT_SPEAKER, true, true/*use fade*/);
                // Need time to fedaout
                aml_audio_sleep(15000);
                aml_dev->mute_start = true;
            }
            break;
        case DO_FADE_AT_HAL:

            if (aml_dev && aml_dev->audio_ease) {
                bool need_do_fade = false;
                if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
                    need_do_fade = !aml_dev->ms12.is_muted;
                } else {
                    float vol_now = aml_audio_ease_get_current_volume(aml_dev->audio_ease);
                    need_do_fade = (vol_now != 0.0f);
                }
                if (!need_do_fade) {
                    ALOGI("%s()skip fade out", __func__);
                } else {
                    ALOGI("%s()do fade out fade_mode %d", __func__, fade_mode);
                    if (is_TV(aml_dev)) {
                        if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
                            duration_ms = property_get_int32("vendor.media.audio.dtv.fadeout.us", MS12_AUDIO_FADEOUT_TV_DURATION_US) / 1000;
                        } else {
                            duration_ms = property_get_int32("vendor.media.audio.dtv.fadeout.us", AUDIO_FADEOUT_TV_DURATION_US) / 1000;
                        }
                    } else {
                        duration_ms = property_get_int32("vendor.media.audio.dtv.fadeout.us", AUDIO_FADEOUT_STB_DURATION_US) / 1000;
                    }
                    if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
                        aml_dev->ms12.do_easing = true;
                        ALOGI("%s()  %d ms doing easing out", __func__, duration_ms);
                        set_ms12_main_audio_mute(&aml_dev->ms12, true, duration_ms);
                        usleep(2 * duration_ms * 1000);
                        aml_dev->ms12.do_easing = false;
                    } else {
                        start_ease_out(aml_dev->audio_ease, is_TV(aml_dev), duration_ms / 2);
                        usleep(duration_ms * 1000);
                    }
                }
            }
            break;
        default:
        ALOGW("invalid fade mode %d", fade_mode);
    }

}

int set_device_control(struct audio_hw_device *dev, struct str_parms *parms)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int ret = -1, val = 0;

    ret = str_parms_get_int(parms, "hp_mute", &val);
    if (ret >= 0) {
        int dac_value[2] = {251, 251};
        if (val > 0) {
            int mute[2] = {0,0};
            aml_mixer_ctrl_set_array(&adev->alsa_mixer, AML_MIXER_ID_DAC_PLAYBACK_VOLUME, &mute, 2);
            ALOGI("set hp mute,set dac to 0");
        } else {
            dac_value[0] = dac_value[1] = adev->dac_value;
            aml_mixer_ctrl_set_array(&adev->alsa_mixer, AML_MIXER_ID_DAC_PLAYBACK_VOLUME, &dac_value, 2);
            ALOGI("set hp unmute,set dac to dac_value[0]:%d,dac_value[1]:%d",dac_value[0], dac_value[1]);
        }
        goto exit;
    }
    ret = str_parms_get_int(parms, "set_hp_vol", &val);
    if (ret >= 0) {
        adev->sink_gain[OUTPORT_HEADPHONE] = DbToAmpl(val / 100.0);
        ALOGI("set set_hp_vol = %f", adev->sink_gain[OUTPORT_HEADPHONE]);
        goto exit;
    }
    ret = str_parms_get_int(parms, "set_bt_vol", &val);
    if (ret >= 0) {
        if (adev->bt_avrcp_supported)
            adev->sink_gain[OUTPORT_A2DP] = 1.0;
        else
            adev->sink_gain[OUTPORT_A2DP] = DbToAmpl(val / 100.0);
        ALOGI("bt_avrcp_supported:%d,set set_bt_vol = %f", adev->bt_avrcp_supported, adev->sink_gain[OUTPORT_A2DP]);
        adev->a2dp_vol = adev->sink_gain[OUTPORT_A2DP];
        goto exit;
    }
    ret = str_parms_get_int(parms, "a2dp_mute", &val);
    if (ret >= 0) {
        if (val > 0) {
            adev->sink_gain[OUTPORT_A2DP] = 0.0;
            ALOGI("set a2dp mute,a2dp gain is %f", adev->sink_gain[OUTPORT_A2DP]);
        } else {
            adev->sink_gain[OUTPORT_A2DP] = adev->a2dp_vol;
            ALOGI("set a2dp unmute,resume a2dp gain:%f",adev->sink_gain[OUTPORT_A2DP]);
        }
        goto exit;
    }
exit:
    return ret;
}


