/*
* Copyright 2023 Amlogic Inc. All rights reserved.
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

#define LOG_TAG "audio_hw_tv_patch"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <utils/Timers.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <hardware/audio.h>
#include <aml_data_utils.h>
#include <audio_utils/channels.h>
#if ANDROID_PLATFORM_SDK_VERSION >= 25 // 8.0
#include <system/audio-base.h>
#endif

#include "audio_hw.h"
#include "aml_audio_stream.h"
#include "audio_hw_utils.h"
#include "aml_audio_timer.h"
#include "alsa_config_parameters.h"
#include "tv_patch_avsync.h"
#include "aml_ng.h"
#include "alsa_device_parser.h"
#include "tv_patch_ctrl.h"


#define INVALID_TYPE                -1

/*==================================input commands=========================================*/
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
        if (!in->input_tmp_buffer) {
            AM_LOGE("aml_audio_realloc is fail");
            return ret;
        }
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


bool is_HBR_stream(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    bool ret = false;

    if (aml_dev->in_device & AUDIO_DEVICE_IN_HDMI && aml_dev->audio_patch) {
        struct aml_audio_patch *audio_patch = aml_dev->audio_patch;
        audio_type_parse_t *audio_type_status = (audio_type_parse_t *)audio_patch->audio_parse_para;

        if (audio_patch && audio_type_status && audio_type_status->soft_parser != 1) {
            if (in->last_audio_packet_type == AUDIO_PACKET_HBR) {
                ret = true;
            }
        }
    }
    return ret;
}

bool is_game_mode(struct aml_audio_device *aml_dev)
{
    if (aml_dev->patch_src != SRC_HDMIIN ||
        !aml_dev->audio_patch ||
        (is_audio_patch_valid(aml_dev) && aml_dev->audio_patch && (aml_dev->audio_patch->input_src != AUDIO_DEVICE_IN_HDMI ||
        aml_dev->audio_patch->IEC61937_format == true))) {
        return false;
    }

    return (is_audio_patch_valid(aml_dev) && aml_dev->audio_patch && aml_dev->audio_patch->pic_mode == PQ_GAME);
}

void aml_check_pic_mode(struct aml_audio_patch *patch)
{
    struct aml_audio_device *aml_dev = NULL;
    if (!patch || patch->input_src != AUDIO_DEVICE_IN_HDMI) {
        return;
    }
    aml_dev = (struct aml_audio_device *)patch->dev;

    if (aml_dev->pic_mode == PQ_GAME && patch->mode_reconfig_flag == true) {
        ALOGD("%s(), IEC61937 data, reconfig audio path", __func__);
        aml_dev->mode_reconfig_in = true;
        aml_dev->mode_reconfig_out = true;
        patch->mode_reconfig_flag = false;
        return;
    }

    /* in PCM data case, picture mode setting changed */
    if (patch->IEC61937_format == false && patch->pic_mode != aml_dev->pic_mode) {
        ALOGD("%s(), pic mode changes from %d to %d", __func__, patch->pic_mode, aml_dev->pic_mode);
        aml_dev->mode_reconfig_in = true;
        aml_dev->mode_reconfig_out = true;
        patch->pic_mode = aml_dev->pic_mode;
    }

}

bool signal_status_check(audio_devices_t in_device, int *mute_time,
                        struct audio_stream_in *stream) {
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *adev = in->dev;
    hdmiin_audio_packet_t last_audio_packet = in->last_audio_packet_type;
    int pre_data_type = in->data_type;
    bool is_audio_packet_changed = false, is_data_changed = false;

    hdmiin_audio_packet_t cur_audio_packet = get_hdmiin_audio_packet(&adev->alsa_mixer);
    is_audio_packet_changed = (((cur_audio_packet == AUDIO_PACKET_AUDS) || (cur_audio_packet == AUDIO_PACKET_HBR)) &&
                               (last_audio_packet != cur_audio_packet));

    int cur_data_type = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_HDMIIN_NONAUDIO);
    if (cur_data_type == DATA_NON_PCM && pre_data_type == DATA_PCM) {
        enable_HW_resample(&adev->alsa_mixer, HW_RESAMPLE_DISABLE);
        is_data_changed = true;
        ALOGI("%s Cur_data_type %d", __func__, cur_data_type);
    }

    if (cur_data_type == DATA_PCM && pre_data_type == DATA_NON_PCM) {
        is_data_changed = true;
    }

    in->data_type = cur_data_type;
    if (in_device & AUDIO_DEVICE_IN_HDMI) {
        hdmiin_audio_packet_t last_audio_packet = in->last_audio_packet_type;
        hdmiin_audio_packet_t cur_audio_packet = get_hdmiin_audio_packet(&adev->alsa_mixer);
        bool is_audio_packet_changed = (((cur_audio_packet == AUDIO_PACKET_AUDS) ||
                                         (cur_audio_packet == AUDIO_PACKET_HBR)) &&
                                        (last_audio_packet != cur_audio_packet));
        bool hw_stable = is_hdmi_in_stable_hw(stream);
        bool hw_format_change = is_hdmi_in_hw_format_change(stream);
        bool hw_sample_rate_change = is_hdmi_in_sample_rate_changed(stream);
        if ((!hw_stable) || is_audio_packet_changed || hw_format_change || hw_sample_rate_change || is_data_changed || adev->reset_hpd) {
            /* HBR audio is stable about 1s */
            *mute_time = 500;

            /* when reset hpd, it takes 2s for audio to be stable */
            if (adev->reset_hpd) {
                *mute_time = 2000;
                adev->reset_hpd = 0;
                ALOGI("%s mute hdmiin %d ms for reset hpd\n", __func__, *mute_time);
            }

            in->last_audio_packet_type = cur_audio_packet;
            if (is_audio_packet_changed || hw_format_change) {
                ALOGD("%s() cur_audio_packet = %d, hw_stable = %d, fmt_hw = %d\n",
                    __func__, cur_audio_packet, hw_stable, in->spdif_fmt_hw);
            }

            /* only reconfig once for HBR audio*/
            if (hw_stable && cur_audio_packet == AUDIO_PACKET_HBR && in->spdif_fmt_hw == MAT) {
                return true;
            }
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
       *mute_time = 100;
       return false;
    }
    return true;
}

bool check_tv_stream_signal(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    struct aml_audio_patch* patch = adev->audio_patch;
    int in_mute = 0;
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
            if (!(in->device & AUDIO_DEVICE_IN_HDMI_ARC || in->device & AUDIO_DEVICE_IN_SPDIF))
                pcm_stop(in->pcm);
            in->mute_log_cntr = 0;
            in->mute_flag = false;
        }
    }

    /*if need mute input source, don't read data from hardware anymore*/
    if (in_mute) {
        /* when audio is unstable, start avsync*/
        if (patch && in_mute) {
            if (!(in->device & AUDIO_DEVICE_IN_HDMI_ARC || in->device & AUDIO_DEVICE_IN_SPDIF))
                patch->need_do_avsync = true;
            patch->input_signal_stable = false;
            adev->mute_start = true;
            ALOGV("%s: audio is unstable, adev->mute_start %d patch->need_do_avsync %d", __func__, adev->mute_start, patch->need_do_avsync);
        }
        return false;
    } else {
        if (patch) {
            patch->input_signal_stable = true;
        }
    }
    return true;
}

bool check_digital_in_stream_signal(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    struct aml_audio_patch *patch = aml_dev->audio_patch;
    audio_type_parse_t *audio_type_status = (audio_type_parse_t *)patch->audio_parse_para;
    enum audio_type cur_audio_type = LPCM;

    /* parse thread may have exited ,add the code to avoid NULL point visit*/
    if (audio_type_status == NULL)  {
        return true;
    }

    if (audio_type_status->soft_parser != 1) {
        if (in->spdif_fmt_hw == SPDIFIN_AUDIO_TYPE_PAUSE) {
            ALOGV("%s(), hw detect iec61937 PAUSE packet, mute input", __func__);
            return false;
        }
    } else {
        cur_audio_type = audio_parse_get_audio_type_direct(patch->audio_parse_para);
        if (cur_audio_type == PAUSE || cur_audio_type == MUTE) {
            ALOGV("%s(), soft parser iec61937 %s packet, mute input", __func__,
                cur_audio_type == PAUSE ? "PAUSE" : "MUTE");
            return false;
        }
    }

    return true;
}


void audio_raw_data_continuous_check(struct aml_audio_device *aml_dev, audio_type_parse_t *status, char *buffer, int size)
{
    audio_type_parse_t *audio_type_status = status;
    struct aml_audio_patch* patch = aml_dev->audio_patch;

    if (!audio_type_status || !aml_dev->audio_patch) {
        return;
    }

    int sync_word_offset = find_61937_sync_word(buffer, size);
    if (sync_word_offset >= 0) {
        patch->sync_offset = sync_word_offset;
        if (patch->start_mute) {
            audio_route_set_speaker_mute_l(aml_dev, false);
            patch->start_mute = false;
            patch->mdelay = 0;
        }
        if (patch->read_size > 0) {
            patch->read_size = 0;
        }
        if (!patch->read_size) {
            patch->read_size = size;
            patch->read_size -= sync_word_offset;
        }
    } else if (patch->sync_offset >= 0) {
        if ((patch->read_size < audio_type_status->package_size) && ((patch->read_size + size) > audio_type_status->package_size)) {
            audio_route_set_speaker_mute_l(aml_dev, true);
            clock_gettime(CLOCK_MONOTONIC, &patch->start_ts);
            patch->start_mute = true;
            patch->read_size = 0;
            patch->mdelay = DEFAULT_PLAYBACK_PERIOD_SIZE * DEFAULT_PLAYBACK_PERIOD_CNT / (MM_FULL_POWER_SAMPLING_RATE / 1000);
        } else {
            if (patch->start_mute) {
                int flag = Stop_watch(patch->start_ts, patch->mdelay);
                if (!flag) {
                    patch->sync_offset = -1;
                    patch->start_mute = false;
                    audio_route_set_speaker_mute_l(aml_dev, false);
                }
            } else {
                patch->read_size += size;
            }
        }
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

        if (is_game_mode(aml_dev)) {
            period_size = LOW_LATENCY_CAPTURE_PERIOD_SIZE;
            buf_size = 2 * 4 * LOW_LATENCY_PLAYBACK_PERIOD_SIZE;
        } else {
            period_size = DEFAULT_CAPTURE_PERIOD_SIZE;
            buf_size = 4 * 2 * 2 * play_buffer_size * PATCH_PERIOD_COUNT;
        }
        ALOGD("%s(), game pic mode %d, period size %d",
                __func__, is_game_mode(aml_dev), period_size);
        stream_in->config.period_size = period_size;
        if (!stream_in->standby) {
            do_input_standby(stream_in);
        }
        s32Ret = start_input_stream(stream_in);
        stream_in->standby = 0;
        if (s32Ret < 0) {
            ALOGE("[%s:%d] start input stream failed! ret:%#x", __func__, __LINE__, s32Ret);
        }

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
    is_audio_packet_changed = (((cur_audio_packet == AUDIO_PACKET_AUDS) || (cur_audio_packet == AUDIO_PACKET_HBR)) &&
                               (last_audio_packet != cur_audio_packet));
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
    struct audio_board_config *bd_config = &adev->board_config;
    int period_size = 0;

    if (adev->mode_reconfig_out) {
        ALOGD("%s(), game mode reconfig out", __func__);
        if (ms12->dolby_ms12_enable && !is_bypass_dolbyms12(stream)) {
            get_hardware_config_parameters(&(adev->ms12_config),
                AUDIO_FORMAT_PCM_16_BIT,
                bd_config->default_alsa_ch,
                ms12->output_samplerate,
                out->is_tv_platform, continuous_mode(adev),
                is_game_mode(adev));

            adev->mode_reconfig_ms12 = true;
        }
        alsa_out_reconfig_params(stream);
        adev->mode_reconfig_out = false;
    }
    return 0;
}

/*==================================mixer control commands=========================================*/
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

    /*hdmirx audio support: N/A, 2, 3, 4, 5, 6, 7, 8*/
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


bool is_hdmi_in_stable_hw(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    int stable = 0;

    stable = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_HDMI_IN_AUDIO_STABLE);
    if (!stable) {
        ALOGV("%s() amixer %s get %d\n", __func__, "HDMIIN audio stable", stable);
        return false;
    }
    return true;
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

bool is_spdif_in_stable_hw(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    int type = 0;

    if (is_earc_descrpt())
        type = eArcIn_audio_format_detection(&aml_dev->alsa_mixer);
    else
        type = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_SPDIFIN_AUDIO_TYPE);
    if (type != in->spdif_fmt_hw) {
        ALOGI ("%s(), in type changed from %d to %d", __func__, in->spdif_fmt_hw, type);
        in->spdif_fmt_hw = type;
        return false;
    }

    return true;
}

bool is_hdmi_in_sample_rate_changed(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    int last_hdmi_in_samplerate = in->hdmi_in_samplerate;

    int samplerate = aml_mixer_ctrl_get_int(&aml_dev->alsa_mixer, AML_MIXER_ID_HDMI_IN_SAMPLERATE);
    if (last_hdmi_in_samplerate != samplerate) {
        ALOGD("hdmi in samplerate changes from %d to %d",last_hdmi_in_samplerate, samplerate);
        in->hdmi_in_samplerate = samplerate;
        return true;
    }
    return false;
}

bool is_hdmi_in_hw_format_change(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    struct aml_audio_patch *audio_patch = aml_dev->audio_patch;
    audio_type_parse_t *audio_type_status = (audio_type_parse_t *)audio_patch->audio_parse_para;
    int tl1_chip = check_chip_name("tl1", 3, &aml_dev->alsa_mixer);
    int type = 0;
    bool ret = false;

    /* TL1 do not use HDMIIN_AUDIO_TYPE */
    if (audio_type_status != NULL && audio_type_status->soft_parser != 1 && !tl1_chip) {
        type = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_HDMIIN_AUDIO_TYPE);
        if ((type != INVALID_TYPE) && (type != in->spdif_fmt_hw)) {
            ALOGD ("%s(), in type changed from %d to %d", __func__, in->spdif_fmt_hw, type);
            ret = true;
        }
        in->spdif_fmt_hw = type;
    }
    return ret;
}

