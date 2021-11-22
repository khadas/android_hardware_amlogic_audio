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
#include "aml_avsync_tuning.h"
#include "aml_android_utils.h"
#include "audio_format_parse.h"
#include "alsa_config_parameters.h"
#include "audio_hw_ms12.h"

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
    struct aml_arc_hdmi_desc *hdmi_desc = &adev->hdmi_descs;

    bool dd_is_support = false;
    bool ddp_is_support = false;
    bool mat_is_support = false;

    audio_format_t sink_capability = AUDIO_FORMAT_PCM_16_BIT;

    //STB case
    if (!adev->is_TV)
    {
        char *cap = NULL;
        cap = (char *) get_hdmi_sink_cap_new (AUDIO_PARAMETER_STREAM_SUP_FORMATS,0,&(adev->hdmi_descs));
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
        /* TODO: NEED get from CDS by mixer */
        if (aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_TX_ATTENDED_TYPE) == ATTEND_TYPE_EARC && adev->bHDMIARCon) {
            hdmi_desc->dd_fmt.is_support = true;
            hdmi_desc->ddp_fmt.is_support = true;
            hdmi_desc->mat_fmt.is_support = true;
        }
        dd_is_support = hdmi_desc->dd_fmt.is_support;
        ddp_is_support = hdmi_desc->ddp_fmt.is_support;
        mat_is_support = hdmi_desc->mat_fmt.is_support;

        if (mat_is_support) {
            sink_capability = AUDIO_FORMAT_MAT;
        } else if (ddp_is_support) {
            sink_capability = AUDIO_FORMAT_E_AC3;
        } else if (dd_is_support) {
            sink_capability = AUDIO_FORMAT_AC3;
        }
        ALOGI ("%s dd support %d ddp support %#x\n", __FUNCTION__, dd_is_support, ddp_is_support);
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
    struct aml_arc_hdmi_desc *hdmi_desc = &adev->hdmi_descs;
    audio_format_t sink_capability = AUDIO_FORMAT_PCM_16_BIT;

    //STB case
    if (!adev->is_TV)
    {
        char *cap = NULL;
        cap = (char *) get_hdmi_sink_cap_new (AUDIO_PARAMETER_STREAM_SUP_FORMATS,0,&(adev->hdmi_descs));
        if (cap) {
            if (adev->hdmi_descs.dts_fmt.is_support) {
                sink_capability = AUDIO_FORMAT_DTS;
            } else if (adev->hdmi_descs.dtshd_fmt.is_support) {
                sink_capability = AUDIO_FORMAT_DTS_HD;
            }
            ALOGI("%s mbox+dvb case sink_capability %#x\n", __FUNCTION__, sink_capability);
            aml_audio_free(cap);
            cap = NULL;
        }
    } else {
        bool dts_is_support = hdmi_desc->dts_fmt.is_support;
        bool dtshd_is_support = hdmi_desc->dtshd_fmt.is_support;

        /* TODO: NEED get from CDS by mixer */
        if (aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_TX_ATTENDED_TYPE) == ATTEND_TYPE_EARC && adev->bHDMIARCon) {
            hdmi_desc->dts_fmt.is_support = true;
            hdmi_desc->dtshd_fmt.is_support = true;
        }
        dts_is_support = hdmi_desc->dts_fmt.is_support;
        dtshd_is_support = hdmi_desc->dtshd_fmt.is_support;

        if (dtshd_is_support) {
            sink_capability = AUDIO_FORMAT_DTS_HD;
        } else if (dts_is_support) {
            sink_capability = AUDIO_FORMAT_DTS;
        }
        ALOGI ("%s dts support %d dtshd support %d\n", __FUNCTION__, dts_is_support, dtshd_is_support);
    }
    return sink_capability;
}

static void get_sink_pcm_capability(struct aml_audio_device *adev)
{
    struct aml_arc_hdmi_desc *hdmi_desc = &adev->hdmi_descs;
    char *cap = NULL;
    hdmi_desc->pcm_fmt.sample_rate_mask = 0;

    cap = (char *) get_hdmi_sink_cap_new (AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, AUDIO_FORMAT_PCM_16_BIT,&(adev->hdmi_descs));
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
 *   suitable format but use the sink device max capbility format.
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
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    /*set default value for sink_audio_format/optical_audio_format*/
    audio_format_t sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
    audio_format_t optical_audio_format = AUDIO_FORMAT_PCM_16_BIT;

    audio_format_t sink_capability = get_sink_capability(adev);
    audio_format_t sink_dts_capability = get_sink_dts_capability(adev);
    audio_format_t source_format = aml_out->hal_internal_format;

    get_sink_pcm_capability(adev);

    if (adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP || adev->out_device & AUDIO_DEVICE_OUT_ALL_USB) {
        ALOGD("get_sink_format: a2dp and usb set to pcm");
        adev->sink_format = AUDIO_FORMAT_PCM_16_BIT;
        adev->sink_capability = AUDIO_FORMAT_PCM_16_BIT;
        adev->optical_format = AUDIO_FORMAT_PCM_16_BIT;
        aml_out->dual_output_flag = false;
        return;
    }

    /*when device is HDMI_ARC*/
    ALOGI("!!!%s() Sink devices %#x Source format %#x digital_format(hdmi_format) %#x Sink Capability %#x\n",
          __FUNCTION__, adev->active_outport, aml_out->hal_internal_format, adev->hdmi_format, sink_capability);

    if ((source_format != AUDIO_FORMAT_PCM_16_BIT) && \
        (source_format != AUDIO_FORMAT_AC3) && \
        (source_format != AUDIO_FORMAT_E_AC3) && \
        (source_format != AUDIO_FORMAT_MAT) && \
        (source_format != AUDIO_FORMAT_AC4) && \
        (source_format != AUDIO_FORMAT_DTS) &&
        (source_format != AUDIO_FORMAT_DTS_HD) && \
        (source_format != AUDIO_FORMAT_DOLBY_TRUEHD)) {
        /*unsupport format [dts-hd/true-hd]*/
        ALOGI("%s() source format %#x change to %#x", __FUNCTION__, source_format, AUDIO_FORMAT_PCM_16_BIT);
        source_format = AUDIO_FORMAT_PCM_16_BIT;
    }
    adev->sink_capability = sink_capability;

    // "adev->hdmi_format" is the UI selection item.
    // "adev->active_outport" was set when HDMI ARC cable plug in/off
    // condition 1: ARC port, single output.
    // condition 2: for STB case with dolby-ms12 libs
    if (adev->active_outport == OUTPORT_HDMI_ARC || !adev->is_TV) {
        ALOGI("%s() HDMI ARC or mbox + dvb case", __FUNCTION__);
        switch (adev->hdmi_format) {
        case PCM:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = sink_audio_format;
            break;
        case DD:
            if (adev->continuous_audio_mode == 0) {
                sink_audio_format = get_suitable_output_format(aml_out, source_format, AUDIO_FORMAT_AC3);
            } else {
                sink_audio_format = AUDIO_FORMAT_AC3;
                if (source_format == AUDIO_FORMAT_PCM_16_BIT) {
                    ALOGI("%s continuous_audio_mode %d source_format %#x\n", __FUNCTION__, adev->continuous_audio_mode, source_format);
                }
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

            optical_audio_format = reconfig_optical_audio_format(aml_out, optical_audio_format);
            break;
        case BYPASS:
            if (is_dts_format(source_format)) {
                sink_audio_format = MIN(source_format, sink_dts_capability);
            } else {
                sink_audio_format = get_suitable_output_format(aml_out, source_format, sink_capability);
            }
            optical_audio_format = sink_audio_format;
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
        switch (adev->hdmi_format) {
        case PCM:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = sink_audio_format;
            break;
        case DD:
            if (adev->continuous_audio_mode == 0) {
                sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
                optical_audio_format = MIN(source_format, AUDIO_FORMAT_AC3);
            } else {
                sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
                optical_audio_format = AUDIO_FORMAT_AC3;
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
    adev->sink_format = sink_audio_format;
    adev->optical_format = optical_audio_format;

    /* set the dual output format flag */
    if (adev->sink_format != adev->optical_format) {
        aml_out->dual_output_flag = true;
    } else {
        aml_out->dual_output_flag = false;
    }

    ALOGI("%s sink_format %#x optical_format %#x, dual_output %d\n",
           __FUNCTION__, adev->sink_format, adev->optical_format, aml_out->dual_output_flag);
    return ;
}

bool is_hdmi_in_stable_hw (struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    struct aml_audio_patch *audio_patch = aml_dev->audio_patch;
    audio_type_parse_t *audio_type_status = (audio_type_parse_t *)audio_patch->audio_parse_para;
    int type = 0;
    int stable = 0;

    stable = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_HDMI_IN_AUDIO_STABLE);
    if (!stable) {
        ALOGV("%s() amixer %s get %d\n", __func__, "HDMIIN audio stable", stable);
        return false;
    }
    if (audio_type_status->soft_parser != 1) {
        type = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_HDMIIN_AUDIO_TYPE);
        if (type != in->spdif_fmt_hw) {
            ALOGD ("%s(), in type changed from %d to %d", __func__, in->spdif_fmt_hw, type);
            in->spdif_fmt_hw = type;
            return false;
        }
    }
    return true;
}

bool is_dual_output_stream(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    return aml_out->dual_output_flag;
}

bool is_hdmi_in_stable_sw (struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    struct aml_audio_patch *patch = aml_dev->audio_patch;
    audio_format_t fmt;

    /* now, only hdmiin->(spk, hp, arc) cases init the soft parser thread
     * TODO: init hdmiin->mix soft parser too
     */
    if (!patch)
        return true;

    fmt = audio_parse_get_audio_type (patch->audio_parse_para);
    if (fmt != in->spdif_fmt_sw) {
        ALOGD ("%s(), in type changed from %#x to %#x", __func__, in->spdif_fmt_sw, fmt);
        in->spdif_fmt_sw = fmt;
        return false;
    }

    return true;
}


void  release_audio_stream(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    if (aml_out->is_tv_platform == 1) {
        aml_audio_free(aml_out->tmp_buffer_8ch);
        aml_out->tmp_buffer_8ch = NULL;
        aml_audio_free(aml_out->audioeffect_tmp_buffer);
        aml_out->audioeffect_tmp_buffer = NULL;
    }
    aml_audio_free(stream);
}
bool is_atv_in_stable_hw (struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    int type = 0;
    int stable = 0;

    stable = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_ATV_IN_AUDIO_STABLE);
    if (!stable)
        return false;

    return true;
}
bool is_av_in_stable_hw(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *aml_dev = in->dev;
    int type = 0;
    int stable = 0;

    stable = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_AV_IN_AUDIO_STABLE);
    if (!stable)
        return false;

    return true;
}

bool is_spdif_in_stable_hw (struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    int type = 0;

    type = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_SPDIFIN_AUDIO_TYPE);
    if (type != in->spdif_fmt_hw) {
        ALOGV ("%s(), in type changed from %d to %d", __func__, in->spdif_fmt_hw, type);
        in->spdif_fmt_hw = type;
        return false;
    }

    return true;
}

int set_audio_source(struct aml_mixer_handle *mixer_handle,
        enum input_source audio_source, bool is_auge)
{
    int src = audio_source;

    if (is_auge) {
        switch (audio_source) {
        case LINEIN:
            src = TDMIN_A;
            break;
        case ATV:
            src = FRATV;
            break;
        case HDMIIN:
            src = FRHDMIRX;
            break;
        case ARCIN:
            src = EARCRX_DMAC;
            break;
        case SPDIFIN:
            src = SPDIFIN_AUGE;
            break;
        default:
            ALOGW("%s(), src: %d not support", __func__, src);
            src = FRHDMIRX;
            break;
        }
    }

    return aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_AUDIO_IN_SRC, src);
}

int set_resample_source(struct aml_mixer_handle *mixer_handle, enum ResampleSource source)
{
    return aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_HW_RESAMPLE_SOURCE, source);
}

int set_spdifin_pao(struct aml_mixer_handle *mixer_handle,int enable)
{
    return aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_SPDIFIN_PAO, enable);
}

int get_spdifin_samplerate(struct aml_mixer_handle *mixer_handle)
{
    int index = aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_SPDIF_IN_SAMPLERATE);

    return index;
}

int get_hdmiin_samplerate(struct aml_mixer_handle *mixer_handle)
{
    int stable = 0;

    stable = aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_HDMI_IN_AUDIO_STABLE);
    if (!stable) {
        return -1;
    }

    return aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_HDMI_IN_SAMPLERATE);
}

int get_hdmiin_channel(struct aml_mixer_handle *mixer_handle)
{
    int stable = 0;
    int channel_index = 0;

    stable = aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_HDMI_IN_AUDIO_STABLE);
    if (!stable) {
        return -1;
    }

    /*hmdirx audio support: N/A, 2, 3, 4, 5, 6, 7, 8*/
    channel_index = aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_HDMI_IN_CHANNELS);
    if (channel_index == 0) {
        return 0;
    }
    else if (channel_index != 7) {
        return 2;
    }
    else {
        return 8;
    }
}

hdmiin_audio_packet_t get_hdmiin_audio_packet(struct aml_mixer_handle *mixer_handle)
{
    int audio_packet = 0;
    audio_packet = aml_mixer_ctrl_get_int(mixer_handle,AML_MIXER_ID_HDMIIN_AUDIO_PACKET);
    if (audio_packet < 0) {
        return AUDIO_PACKET_NONE;
    }
    return (hdmiin_audio_packet_t)audio_packet;
}

int get_HW_resample(struct aml_mixer_handle *mixer_handle)
{
    return aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_HW_RESAMPLE_ENABLE);
}

int enable_HW_resample(struct aml_mixer_handle *mixer_handle, int enable_sr)
{
    if (enable_sr == 0)
        aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_HW_RESAMPLE_ENABLE, HW_RESAMPLE_DISABLE);
    else
        aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_HW_RESAMPLE_ENABLE, enable_sr);
    return 0;
}

bool Stop_watch(struct timespec start_ts, int64_t time) {
    struct timespec end_ts;
    int64_t start_ms, end_ms;
    int64_t interval_ms;

    clock_gettime (CLOCK_MONOTONIC, &end_ts);
    start_ms = start_ts.tv_sec * 1000LL +
               start_ts.tv_nsec / 1000000LL;
    end_ms = end_ts.tv_sec * 1000LL +
             end_ts.tv_nsec / 1000000LL;
    interval_ms = end_ms - start_ms;
    if (interval_ms < time) {
        return true;
    }
    return false;
}

bool signal_status_check(audio_devices_t in_device, int *mute_time,
                        struct audio_stream_in *stream) {

    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *adev = in->dev;
    if (in_device & AUDIO_DEVICE_IN_HDMI) {
        bool hw_stable = is_hdmi_in_stable_hw(stream);
        bool sw_stable = is_hdmi_in_stable_sw(stream);
        int txlx_chip = check_chip_name("txlx", 4, &adev->alsa_mixer);
        if (txlx_chip)
            sw_stable = true;
        if (!hw_stable || !sw_stable) {
            ALOGV("%s() hw_stable %d sw_stable %d\n", __func__, hw_stable, sw_stable);
            *mute_time = 1000;
            return false;
        }
    }
    if ((in_device & AUDIO_DEVICE_IN_TV_TUNER) &&
            !is_atv_in_stable_hw (stream)) {
        *mute_time = 1000;
        return false;
    }
    if (((in_device & AUDIO_DEVICE_IN_SPDIF) ||
            (in_device & AUDIO_DEVICE_IN_HDMI_ARC)) &&
            !is_spdif_in_stable_hw(stream)) {
        *mute_time = 1000;
        return false;
    }
    if ((in_device & AUDIO_DEVICE_IN_LINE) &&
            !is_av_in_stable_hw(stream)) {
       *mute_time = 1500;
       return false;
    }
    return true;
}

bool check_tv_stream_signal(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    struct aml_audio_patch* patch = adev->audio_patch;
    int in_mute = 0, parental_mute = 0;
    bool stable = true;
    stable = signal_status_check(adev->in_device, &in->mute_mdelay, stream);
    if (!stable) {
        if (in->mute_log_cntr == 0)
            ALOGI("%s: audio is unstable, mute channel", __func__);
        if (in->mute_log_cntr++ >= 100)
            in->mute_log_cntr = 0;
        clock_gettime(CLOCK_MONOTONIC, &in->mute_start_ts);
        in->mute_flag = true;
    }
    if (in->mute_flag) {
        in_mute = Stop_watch(in->mute_start_ts, in->mute_mdelay);
        if (!in_mute) {
            ALOGI("%s: unmute audio since audio signal is stable", __func__);
            /* The data of ALSA has not been read for a long time in the muted state,
             * resulting in the accumulation of data. So, cache of capture needs to be cleared.
             */
            pcm_stop(in->pcm);
            in->mute_log_cntr = 0;
            in->mute_flag = false;
        }
    }

    if (adev->parental_control_av_mute && (adev->active_inport == INPORT_TUNER || adev->active_inport == INPORT_LINEIN))
        parental_mute = 1;

    /*if need mute input source, don't read data from hardware anymore*/
    if (in_mute || parental_mute) {

        /* when audio is unstable, start avaync*/
        if (patch && in_mute) {
            patch->need_do_avsync = true;
            patch->input_signal_stable = false;
            adev->mute_start = true;
        }
        return false;
    } else {
        if (patch) {
            patch->input_signal_stable = true;
        }

        /* Audio should be read from DDR to drive HW format changed,  */
        if (in->spdif_fmt_hw == SPDIFIN_AUDIO_TYPE_PAUSE) {
            return false;
        }
    }
    return true;
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

static const char *write_func_to_str(enum stream_write_func func)
{
    return write_func_strs[func];
}

void aml_stream_out_dump(struct aml_stream_out *aml_out, int fd)
{
    if (aml_out) {
        dprintf(fd, "    usecase: %s\n", usecase2Str(aml_out->usecase));
        dprintf(fd, "    out device: %#x\n", aml_out->out_device);
        dprintf(fd, "    tv source stream: %d\n", aml_out->tv_src_stream);
        dprintf(fd, "    status: %d\n", aml_out->status);
        dprintf(fd, "    standby: %d\n", aml_out->standby);
        if (aml_out->is_normal_pcm) {
            dprintf(fd, "    normal pcm: %s\n",
                write_func_to_str(aml_out->write_func));
        }
    }
}

void aml_audio_port_config_dump(struct audio_port_config *port_config, int fd)
{
    if (port_config == NULL)
        return;

    dprintf(fd, "\t-id(%d), role(%s), type(%s)\n", port_config->id, audioPortRole2Str(port_config->role), audioPortType2Str(port_config->type));
    switch (port_config->type) {
    case AUDIO_PORT_TYPE_DEVICE:
        dprintf(fd, "\t-port device: type(%#x) addr(%s)\n",
               port_config->ext.device.type, port_config->ext.device.address);
        break;
    case AUDIO_PORT_TYPE_MIX:
        dprintf(fd, "\t-port mix: iohandle(%d)\n", port_config->ext.mix.handle);
        break;
    default:
        break;
    }
}

void aml_audio_patch_dump(struct audio_patch *patch, int fd)
{
    int i = 0;

    dprintf(fd, " handle %d\n", patch->id);
    for (i = 0; i < patch->num_sources; i++) {
        dprintf(fd, "    [src  %d]\n", i);
        aml_audio_port_config_dump(&patch->sources[i], fd);
    }

    for (i = 0; i < patch->num_sinks; i++) {
        dprintf(fd, "    [sink %d]\n", i);
        aml_audio_port_config_dump(&patch->sinks[i], fd);
    }
}

void aml_audio_patches_dump(struct aml_audio_device* aml_dev, int fd)
{
    struct audio_patch_set *patch_set = NULL;
    struct audio_patch *patch = NULL;
    struct listnode *node = NULL;
    int i = 0;

    dprintf(fd, "\nAML Audio Patches:\n");
    list_for_each(node, &aml_dev->patch_list) {
        dprintf(fd, "  patch %d:", i);
        patch_set = node_to_item (node, struct audio_patch_set, list);
        if (patch_set)
            aml_audio_patch_dump(&patch_set->audio_patch, fd);

        i++;
    }
}

int aml_dev_dump_latency(struct aml_audio_device *aml_dev, int fd)
{
    struct aml_stream_in *in = aml_dev->active_input;
    struct aml_audio_patch *patch = aml_dev->audio_patch;

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

void audio_patch_dump(struct aml_audio_device* aml_dev, int fd)
{
    struct aml_audio_patch *pstPatch = aml_dev->audio_patch;
    if (NULL == pstPatch) {
        dprintf(fd, "-------------[AML_HAL] audio patch [not create]-----------\n");
        return;
    }
    dprintf(fd, "-------------[AML_HAL] audio patch [%p]---------------\n", pstPatch);
    if (pstPatch->aml_ringbuffer.size != 0) {
        uint32_t u32FreeBuffer = get_buffer_write_space(&pstPatch->aml_ringbuffer);
        dprintf(fd, "[AML_HAL]      RingBuf   size: %10d Byte|  UnusedBuf:%10d Byte(%d%%)\n",
        pstPatch->aml_ringbuffer.size, u32FreeBuffer, u32FreeBuffer* 100 / pstPatch->aml_ringbuffer.size);
    } else {
        dprintf(fd, "[AML_HAL]      patch  RingBuf    : buffer size is 0\n");
    }
    if (pstPatch->audio_parse_para) {
        int s32AudioType = ((audio_type_parse_t*)pstPatch->audio_parse_para)->audio_type;
        dprintf(fd, "[AML_HAL]      Hal audio Type: [0x%x]%-10s| Src Format:%#10x\n", s32AudioType,
            audio_type_convert_to_string(s32AudioType),  pstPatch->aformat);
    }

    dprintf(fd, "[AML_HAL]      IN_SRC        : %#10x     | OUT_SRC   :%#10x\n", pstPatch->input_src, pstPatch->output_src);
    dprintf(fd, "[AML_HAL]      IN_Format     : %#10x     | OUT_Format:%#10x\n", pstPatch->aformat, pstPatch->out_format);
    dprintf(fd, "[AML_HAL]      sink format: %#x\n", aml_dev->sink_format);
    if (aml_dev->active_outport == OUTPORT_HDMI_ARC) {
        struct aml_arc_hdmi_desc *hdmi_desc = &aml_dev->hdmi_descs;
        bool dd_is_support = hdmi_desc->dd_fmt.is_support;
        bool ddp_is_support = hdmi_desc->ddp_fmt.is_support;
        bool mat_is_support = hdmi_desc->mat_fmt.is_support;

        dprintf(fd, "[AML_HAL]      -dd: %d, ddp: %d, mat: %d\n",
                dd_is_support, ddp_is_support, mat_is_support);
    }


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
        if (out->dual_spdif && ((adev->hdmi_format == AUTO) &&
            adev->optical_format == AUDIO_FORMAT_E_AC3) &&
            out->hal_rate != 32000) {
            return true;
        }
    }

    return false;
}

bool is_dolby_ms12_support_compression_format(audio_format_t format)
{
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
    ALOGI("%s(), [%s]", __func__, temp_buf);
}

static int update_audio_hal_info(struct aml_audio_device *adev, audio_format_t format, int atmos_flag)
{
    int update_type = get_codec_type(format);
    int update_threshold = DOLBY_FMT_UPDATE_THRESHOLD;

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

    if ((format != adev->audio_hal_info.format) || (atmos_flag != adev->audio_hal_info.is_dolby_atmos)) {
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

    bool is_dolby_atmos_off = (MS12_DAP_SPEAKER_VIRTUALIZER_OFF == dolby_ms12_get_dap_surround_virtuallizer());
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

    if (adev->audio_hal_info.update_cnt == update_threshold) {

        if ((format == AUDIO_FORMAT_DTS || format == AUDIO_FORMAT_DTS_HD) && adev->dts_hd.is_headphone_x) {
            aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_AUDIO_HAL_FORMAT, TYPE_DTS_HP);
        }
        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_AUDIO_HAL_FORMAT, update_type);
        ALOGD("%s()audio hal format change to %x, atmos flag = %d, dts_hp_x = %d, update_type = %d is_dolby_atmos_off = %d\n",
            __FUNCTION__, adev->audio_hal_info.format, adev->audio_hal_info.is_dolby_atmos,
            adev->dts_hd.is_headphone_x, adev->audio_hal_info.update_type, is_dolby_atmos_off);
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
     * else //(!is_dolby_acrive && is_dolby_format) {
     * }
     */
}

int audio_route_set_hdmi_arc_mute(struct aml_mixer_handle *mixer_handle, int enable)
{
    int extern_arc = 0;

    if (check_chip_name("t5", 2, mixer_handle))
        extern_arc = 1;
    /*t5w hdmi arc mute path is HDMI ARC Switch off */
    if (extern_arc == 1 && check_chip_name("t5w", 3, mixer_handle))
        extern_arc  = 0;
    if (extern_arc) {
        return aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_SPDIF_MUTE, enable);
    } else {
        return aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_HDMI_ARC_AUDIO_ENABLE, !enable);
    }
}

int audio_route_set_spdif_mute(struct aml_mixer_handle *mixer_handle, int enable)
{
    int extern_arc = 0;

    if (check_chip_name("t5", 2, mixer_handle))
        extern_arc = 1;
    if (extern_arc == 1 && check_chip_name("t5w", 3, mixer_handle))
        extern_arc  = 0;
    if (extern_arc) {
        return aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_SPDIF_B_MUTE, enable);
    } else {
        return aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_SPDIF_MUTE, enable);
    }
}

int reconfig_read_param_through_hdmiin(struct aml_audio_device *aml_dev,
                                       struct aml_stream_in *stream_in,
                                       ring_buffer_t *ringbuffer, int buffer_size)
{
    int ring_buffer_size = buffer_size;
    int last_channel_count = 2;
    int s32Ret = 0;
    bool is_channel_changed = false;
    bool is_audio_packet_changed = false;
    hdmiin_audio_packet_t last_audio_packet = AUDIO_PACKET_AUDS;
    int period_size = 0;
    int buf_size = 0;

    if (!aml_dev || !stream_in) {
        ALOGE("%s line %d aml_dev %p stream_in %p\n", __func__, __LINE__, aml_dev, stream_in);
        return -1;
    }

    /* check game mode change and reconfig input */
    if (aml_dev->mode_reconfig_in) {
        int play_buffer_size = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;

        if (aml_dev->game_mode) {
            period_size = LOW_LATENCY_CAPTURE_PERIOD_SIZE;
            buf_size = 2 * 4 * LOW_LATENCY_PLAYBACK_PERIOD_SIZE;
        } else {
            period_size = DEFAULT_CAPTURE_PERIOD_SIZE;
            buf_size = 4 * 2 * 2 * play_buffer_size * PATCH_PERIOD_COUNT;
        }
        ALOGD("%s(), game pic mode %d, period size %d",
                __func__, aml_dev->game_mode, period_size);
        stream_in->config.period_size = period_size;
        if (ringbuffer) {
            ring_buffer_reset_size(ringbuffer, buf_size);
        }

        aml_dev->mode_reconfig_in = false;
    }

    last_channel_count = stream_in->config.channels;
    last_audio_packet = stream_in->audio_packet_type;

    hdmiin_audio_packet_t cur_audio_packet = get_hdmiin_audio_packet(&aml_dev->alsa_mixer);
    int current_channel = get_hdmiin_channel(&aml_dev->alsa_mixer);

    is_channel_changed = ((current_channel > 0) && last_channel_count != current_channel);
    is_audio_packet_changed = ((cur_audio_packet != AUDIO_PACKET_NONE) && (last_audio_packet != cur_audio_packet));
    //reconfig input stream and buffer when HBR and AUDS audio switching or channel num changed
    if ((is_channel_changed) || is_audio_packet_changed) {
        int period_size = 0;
        int buf_size = 0;
        int channel = 2;
        bool bSpdifin_PAO = false;

        ALOGI("HDMI Format Switch [audio_packet pre:%d->cur:%d changed:%d] [channel pre:%d->cur:%d changed:%d]",
            last_audio_packet, cur_audio_packet, is_audio_packet_changed, last_channel_count, current_channel, is_channel_changed);
        if (cur_audio_packet == AUDIO_PACKET_HBR) {
            // if it is high bitrate bitstream, use PAO and increase the buffer size
            bSpdifin_PAO = true;
            period_size = DEFAULT_CAPTURE_PERIOD_SIZE * 4;
            // increase the buffer size
            buf_size = ring_buffer_size * 8;
            channel = 8;
            //if (check_chip_name("t3", 2, &aml_dev->alsa_mixer)) {
            //    channel = 2;    // T3's HDMIRX IP does not support 8CH in design.
            //}
        } else if (cur_audio_packet == AUDIO_PACKET_AUDS) {
            bSpdifin_PAO = false;
            period_size = DEFAULT_CAPTURE_PERIOD_SIZE;
            // reset to original one
            buf_size = ring_buffer_size;
            channel = current_channel;
        }

        if (ringbuffer) {
            ring_buffer_reset_size(ringbuffer, buf_size);
        }
        stream_in->config.period_size = period_size;
        stream_in->config.channels = channel;
        if (!stream_in->standby) {
            do_input_standby(stream_in);
        }
        s32Ret = start_input_stream(stream_in);
        stream_in->standby = 0;
        if (s32Ret < 0) {
            ALOGE("[%s:%d] start input stream failed! ret:%#x", __func__, __LINE__, s32Ret);
        }
        stream_in->audio_packet_type = cur_audio_packet;
        return 0;
    } else {
        return -1;
    }
}

int stream_check_reconfig_param(struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int period_size = 0;

    if (adev->mode_reconfig_out) {
        ALOGD("%s(), game reconfig out", __func__);
        if (ms12->dolby_ms12_enable && !is_bypass_dolbyms12(stream)) {
            ALOGD("%s(), game reconfig out line %d", __func__, __LINE__);
            get_hardware_config_parameters(&(adev->ms12_config),
                AUDIO_FORMAT_PCM_16_BIT,
                audio_channel_count_from_out_mask(ms12->output_channelmask),
                ms12->output_samplerate,
                out->is_tv_platform, continous_mode(adev),
                adev->game_mode);

            alsa_out_reconfig_params(stream);
            adev->mode_reconfig_ms12 = true;
        }
        adev->mode_reconfig_out = false;
    }
    return 0;
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
        if (adev->ms12_out && continous_mode(adev)) {
            ALOGD("%s() active stream is ms12_out %p\n", __FUNCTION__, adev->ms12_out);
            get_sink_format((struct audio_stream_out *)adev->ms12_out);
        }
        else {
            ALOGD("%s() active stream %p ms12_out %p\n", __FUNCTION__, stream, adev->ms12_out);
        }
    }

    return 0;
}


/* expand channels or contract channels*/
int input_stream_channels_adjust(struct audio_stream_in *stream, void* buffer, size_t bytes)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int ret = -1;

    if (!in || !bytes)
        return ret;

    int channel_count = audio_channel_count_from_in_mask(in->hal_channel_mask);

    if (!channel_count)
        return ret;

    size_t read_bytes = in->config.channels * bytes / channel_count;
    if (!in->input_tmp_buffer || in->input_tmp_buffer_size < read_bytes) {
        in->input_tmp_buffer = aml_audio_realloc(in->input_tmp_buffer, read_bytes);
        in->input_tmp_buffer_size = read_bytes;
    }

    ret = aml_alsa_input_read(stream, in->input_tmp_buffer, read_bytes);
    if (in->config.format == PCM_FORMAT_S16_LE)
        adjust_channels(in->input_tmp_buffer, in->config.channels,
            buffer, channel_count, 2, read_bytes);
    else if (in->config.format == PCM_FORMAT_S32_LE)
        adjust_channels(in->input_tmp_buffer, in->config.channels,
            buffer, channel_count, 4, read_bytes);

   return ret;
}


void create_tvin_buffer(struct aml_audio_patch *patch)
{
    int ret = ring_buffer_init(&patch->tvin_ringbuffer, 512 * 1024);
    ALOGI("[%s] aring_buffer_init ret=%d\n", __FUNCTION__, ret);
    if (ret == 0) {
        patch->tvin_buffer_inited = 1;
    }

}


void release_tvin_buffer(struct aml_audio_patch *patch)
{
    if (patch->tvin_buffer_inited == 1) {
        patch->tvin_buffer_inited = 0;
        ring_buffer_release(&(patch->tvin_ringbuffer));
    }
}

void tv_in_write(struct audio_stream_out *stream, const void* buffer, size_t bytes)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct aml_audio_patch *patch = adev->audio_patch;
    int abuf_level = 0;

    if (stream == NULL || buffer == NULL || bytes == 0 || patch == NULL) {
        ALOGW("[%s:%d] stream:%p or buffer:%p is null, or bytes:%d = 0 patch:%p.", __func__, __LINE__, stream, buffer, bytes, patch);
        return;
    }
    if (patch->tvin_buffer_inited == 1) {
        abuf_level = get_buffer_write_space(&patch->tvin_ringbuffer);
        if (abuf_level <= (int)bytes) {
            ALOGI("[%s] dtvin ringbuffer is full,reset  ringbuffer", __FUNCTION__);
            ring_buffer_reset(&patch->tvin_ringbuffer);
            return;
        }

        ring_buffer_write(&patch->tvin_ringbuffer, (unsigned char *)buffer, bytes, UNCOVER_WRITE);
    }
    ALOGV("[%s] dtvin write ringbuffer successfully,abuf_level=%d", __FUNCTION__,abuf_level);
}

int tv_in_read(struct audio_stream_in *stream, void* buffer, size_t bytes)
{
    int ret = 0;
    unsigned int es_length = 0;
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;

    if (stream == NULL || buffer == NULL || bytes == 0 || adev->audio_patch == NULL) {
        ALOGW("[%s:%d] stream:%p or buffer:%p is null, or bytes:%d = 0. adev->audio_patch %p", __func__, __LINE__, stream, buffer, bytes, adev->audio_patch );
        return bytes;
    }

    struct aml_audio_patch *patch = adev->audio_patch;
    ALOGV("[%s] patch->aformat=0x%x patch->dtv_decoder_ready=%d bytes:%d\n", __FUNCTION__,patch->aformat,patch->dtv_decoder_ready,bytes);

    if (patch->tvin_buffer_inited == 1) {
        int abuf_level = get_buffer_read_space(&patch->tvin_ringbuffer);
        if (abuf_level <= (int)bytes) {
            bytes = ret = 0;
            ALOGI("[%s] abuf_level =%d <  bytes =%d\n", __FUNCTION__,abuf_level,bytes);
        } else {
            ret = ring_buffer_read(&patch->tvin_ringbuffer, (unsigned char *)buffer, bytes);
            ALOGV("[%s] abuf_level =%d ret=%d\n", __FUNCTION__,abuf_level,ret);
        }
        return bytes;
    } else {
        memset(buffer, 0, sizeof(unsigned char)* bytes);
        ret = bytes;
        return ret;
    }
    return ret;
}

int set_tv_source_switch_parameters(struct audio_hw_device *dev, struct str_parms *parms)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int ret = -1;
    char value[64];

    /*----ATV <-> DTV switch----*/
    ret = str_parms_get_str(parms, "hal_param_tuner_in", value, sizeof(value));
    // tuner_in=atv: tuner_in=dtv
    if (ret >= 0 && adev->is_TV) {
        if (strncmp(value, "dtv", 3) == 0) {
            // no audio patching in dtv
            if (adev->audio_patching && (adev->patch_src == SRC_ATV)) {
                // this is to handle atv->dtv case
                ALOGI("%s, atv->dtv", __func__);
                ret = release_patch(adev);
                if (!ret) {
                    adev->audio_patching = 0;
                }
            }
            ALOGI("%s, now the audio patch src is %s, the audio_patching is %d ", __func__,
                patchSrc2Str(adev->patch_src), adev->audio_patching);

            if ((adev->patch_src == SRC_DTV) && adev->audio_patching) {
                ALOGI("[audiohal_kpi] %s, now release the dtv patch now\n ", __func__);
                ret = release_dtv_patch(adev);
                if (!ret) {
                    adev->audio_patching = 0;
                }
            }
            ALOGI("[audiohal_kpi] %s, now end release dtv patch the audio_patching is %d ", __func__, adev->audio_patching);
            ALOGI("[audiohal_kpi] %s, now create the dtv patch now\n ", __func__);
            adev->patch_src = SRC_DTV;
            if (eDolbyMS12Lib == adev->dolby_lib_type) {
                bool set_ms12_non_continuous = true;
                get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                adev->exiting_ms12 = 1;
                clock_gettime(CLOCK_MONOTONIC, &adev->ms12_exiting_start);
                if (adev->active_outputs[STREAM_PCM_NORMAL] != NULL)
                    usecase_change_validate_l(adev->active_outputs[STREAM_PCM_NORMAL], true);
            }

            ret = create_dtv_patch(dev, AUDIO_DEVICE_IN_TV_TUNER, AUDIO_DEVICE_OUT_SPEAKER);
            if (ret == 0) {
                adev->audio_patching = 1;
            }
            ALOGI("[audiohal_kpi] %s, now end create dtv patch the audio_patching is %d ", __func__, adev->audio_patching);

        } else if (strncmp(value, "atv", 3) == 0) {

            // need create patching
            if ((adev->patch_src == SRC_DTV) && adev->audio_patching) {
                ALOGI("[audiohal_kpi] %s, release dtv patching", __func__);
                ret = release_dtv_patch(adev);
                if (!ret) {
                    adev->audio_patching = 0;
                }
            }
            if (eDolbyMS12Lib == adev->dolby_lib_type && adev->continuous_audio_mode) {
                ALOGI("In ATV exit MS12 continuous mode");
                bool set_ms12_non_continuous = true;
                get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                adev->exiting_ms12 = 1;
                clock_gettime(CLOCK_MONOTONIC, &adev->ms12_exiting_start);
                if (adev->active_outputs[STREAM_PCM_NORMAL] != NULL)
                    usecase_change_validate_l(adev->active_outputs[STREAM_PCM_NORMAL], true);
            }

            if (!adev->audio_patching) {
                ALOGI("[audiohal_kpi] %s, create atv patching", __func__);
                set_audio_source(&adev->alsa_mixer,
                        ATV, alsa_device_is_auge());
                ret = create_patch (dev, AUDIO_DEVICE_IN_TV_TUNER, AUDIO_DEVICE_OUT_SPEAKER);
                // audio_patching ok, mark the patching status
                if (ret == 0) {
                    adev->audio_patching = 1;
                }
            }
            adev->patch_src = SRC_ATV;
        }
        goto exit;
    }

    /*----HDMIIN <-> LINEIN switch----*/
    ret = str_parms_get_str(parms, "audio", value, sizeof(value));
    if (ret >= 0) {
        /*
         * This is a work around when plug in HDMI-DVI connector
         * first time application only recognize it as HDMI input device
         * then it can know it's DVI in, and then send "audio=linein" message to audio hal
         */
        struct audio_patch *pAudPatchTmp = NULL;
        if (strncmp(value, "linein", 6) == 0) {

            get_audio_patch_by_src_dev(dev, AUDIO_DEVICE_IN_HDMI, &pAudPatchTmp);
            if (pAudPatchTmp == NULL) {
                ALOGE("%s,There is no autio patch using HDMI as input", __func__);
                goto exit;
            }
            if (pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_HDMI) {
                ALOGE("%s, pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_HDMI", __func__);
                goto exit;
            }

            // dev->dev (example: HDMI in-> speaker out)
            if (pAudPatchTmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE
                && pAudPatchTmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
                // This "adev->audio_patch" will be created in create_patch() function
                if (adev->audio_patch && (adev->patch_src == SRC_HDMIIN)) {
                    ALOGI("%s, create hdmi-dvi patching dev->dev", __func__);
                    release_patch(adev);
                    create_patch(dev, AUDIO_DEVICE_IN_LINE, pAudPatchTmp->sinks[0].ext.device.type);
                }
            }

            adev->patch_src = SRC_LINEIN;
            adev->active_inport = INPORT_LINEIN;
            pAudPatchTmp->sources[0].ext.device.type = AUDIO_DEVICE_IN_LINE;
            set_audio_source(&adev->alsa_mixer, LINEIN, alsa_device_is_auge());

        } else if (strncmp(value, "hdmi", 4) == 0 && adev->audio_patch) {

            get_audio_patch_by_src_dev(dev, AUDIO_DEVICE_IN_LINE, &pAudPatchTmp);
            if (pAudPatchTmp == NULL) {
                ALOGE("%s,There is no autio patch using LINEIN as input", __func__);
                goto exit;
            }
            if (pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_LINE) {
                ALOGE("%s, pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_HDMI", __func__);
                goto exit;
            }

            // dev->dev (example: LINE in -> speaker out)
            if (pAudPatchTmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE
                && pAudPatchTmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
                // This "adev->audio_patch" will be created in create_patch() function
                if (adev->audio_patch && (adev->patch_src == SRC_LINEIN)) {
                    ALOGI("%s, create dvi-hdmi patching dev->dev", __func__);
                    release_patch(adev);
                    create_patch(dev, AUDIO_DEVICE_IN_HDMI, pAudPatchTmp->sinks[0].ext.device.type);
                }
            }

            adev->patch_src = SRC_HDMIIN;
            adev->active_inport = INPORT_HDMIIN;
            pAudPatchTmp->sources[0].ext.device.type = AUDIO_DEVICE_IN_HDMI;
            set_audio_source(&adev->alsa_mixer, HDMIIN, alsa_device_is_auge());

        }
        goto exit;
    }

exit:
    return ret;
}

int set_hdmiin_audio_mode(struct aml_mixer_handle *mixer_handle, char *mode)
{
    if (mode == NULL || strlen(mode) > 5)
        return -EINVAL;

    return aml_mixer_ctrl_set_str(mixer_handle,
            AML_MIXER_ID_HDMIIN_AUDIO_MODE, mode);
}

enum hdmiin_audio_mode get_hdmiin_audio_mode(struct aml_mixer_handle *mixer_handle)
{
    return (enum hdmiin_audio_mode)aml_mixer_ctrl_get_int(mixer_handle,
            AML_MIXER_ID_HDMIIN_AUDIO_MODE);
}

void tv_do_ease_out(struct aml_audio_device *aml_dev)
{
    int duration_ms = 0;
    if (aml_dev && aml_dev->audio_ease) {
        float vol_now = aml_audio_ease_get_current_volume(aml_dev->audio_ease);
        if (vol_now == 0.0f) {
            ALOGI("%s(),vol_now %f skip fade out", __func__, vol_now);
        } else {
            ALOGI("%s(), vol_now %f do fade out", __func__, vol_now);
            if (aml_dev->is_TV) {
                duration_ms = property_get_int32("vendor.media.audio.dtv.fadeout.us", AUDIO_FADEOUT_TV_DURATION_US) / 1000;
            } else {
                duration_ms = property_get_int32("vendor.media.audio.dtv.fadeout.us", AUDIO_FADEOUT_STB_DURATION_US) / 1000;
            }
            start_ease_out(aml_dev->audio_ease, aml_dev->is_TV, duration_ms / 2);
            usleep(duration_ms * 1000);
        }
    }
}
