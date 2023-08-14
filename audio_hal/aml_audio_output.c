/*
 * Copyright (C) 2023 The Android Open Source Project
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

#define LOG_TAG "audio_output"
//#define LOG_NDEBUG 0

#include <inttypes.h>
#include <stdlib.h>
#include <cutils/log.h>
#include <system/audio.h>
#include <hardware/audio.h>
#include <tinyalsa/asoundlib.h>
#include <audio_utils/channels.h>
#include <aml_android_utils.h>

#include "aml_audio_output.h"
#include "alsa_manager.h"
#include "aml_dump_debug.h"
#include "alsa_device_parser.h"
#include "audio_hw_utils.h"
#include "aml_audio_dev2mix_process.h"
#include "dolby_lib_api.h"
#include "aml_audio_report.h"
#include "aml_audio_sysfs.h"
#include "a2dp_hal.h"
#include "audio_bt_sco.h"
#include "aml_malloc_debug.h"
#include "spdif_encoder_api.h"
#include "aml_audio_ms12_sync.h"
#include "audio_hwsync_wrap.h"
#include "aml_audio_nonms12_render.h"
#include "aml_audio_spdifout.h"
#include "audio_hw_ms12.h"
#include "audio_hw_ms12_common.h"


ssize_t processing_multich_pcm(struct audio_stream_out *stream,
                                const void *buffer,
                                size_t bytes,
                                audio_data_info_t * in_data_info,
                                void **output_buffer,
                                size_t *output_buffer_bytes,
                                audio_data_info_t * out_data_info)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct audio_board_config *bd_config = &adev->board_config;

    /* TODO  support 24/32 bit sample */
    int out_frames = 0;
    size_t i;
    int j, ret;
    uint32_t latency_frames = 0;
    uint64_t total_frame = 0;

    int nchannels = audio_channel_count_from_out_mask(in_data_info->channel_mask);
    int bytes_per_sample = audio_bytes_per_sample(in_data_info->audio_format);
    audio_format_t output_format = in_data_info->audio_format;


    out_frames = bytes / (nchannels * bytes_per_sample);

    int enable_dump = aml_getprop_bool("vendor.media.audiohal.outdump");
    if (adev->debug_flag) {
        ALOGD("%s,size %zu,format %x,ch %d\n",__func__,bytes,output_format,nchannels);
    }
    {
        /* nchannels 16 bit PCM, and there is no effect applied after MS12 processing */
        {
            size_t out_frames = bytes / (nchannels * 2); /* input is nchannels 16 bit */
            if (enable_dump) {
                FILE *fp1 = fopen("/data/vendor/audiohal/ms12_out_spk.pcm", "a+");
                if (fp1) {
                    int flen = fwrite((char *)buffer, 1, bytes, fp1);
                    ALOGV("%s buffer %p size %zu\n", __FUNCTION__, buffer, bytes);
                    fclose(fp1);
                }
            }

            ret = aml_audio_check_and_realloc((void **)&adev->out_32_buf, &adev->out_32_buf_size, 2 * bytes);
            R_CHECK_RET(ret, "alloc out_32_buf size:%zu fail", 2 * bytes);

            /* apply volume for spk/hp, SPDIF/HDMI keep the max volume */
            float gain_speaker = adev->sink_gain[OUTPORT_SPEAKER];
            if ((eDolbyMS12Lib == adev->dolby_lib_type) && aml_out->ms12_vol_ctrl) {
                gain_speaker = 1.0;
            }
            apply_volume_16to32(gain_speaker, (int16_t *)buffer, adev->out_32_buf, bytes);
            if (enable_dump) {
                FILE *fp1 = fopen("/data/vendor/audiohal/ms12_out_spk-volume-32bit.pcm", "a+");
                if (fp1) {
                    int flen = fwrite((char *)adev->out_32_buf, 1, bytes*2, fp1);
                    ALOGV("%s buffer %p size %zu\n", __FUNCTION__, adev->out_32_buf, bytes);
                    fclose(fp1);
                }
            }

            /* nchannels 32 bit --> 8 channel 32 bit mapping */
            ret = aml_audio_check_and_realloc((void **)&adev->tmp_buffer_8ch, &adev->tmp_buffer_8ch_size,
                    out_frames * 4 * bd_config->default_alsa_ch);
            R_CHECK_RET(ret, "alloc tmp_buffer_8ch size:%zu fail", out_frames * 4 * bd_config->default_alsa_ch);

            for (i = 0; i < out_frames; i++) {
                for (j = 0; j < nchannels; j++) {
                    adev->tmp_buffer_8ch[bd_config->default_alsa_ch * i + j] = adev->out_32_buf[nchannels * i + j];
                }
                for (j = nchannels; j < bd_config->default_alsa_ch; j++) {
                    adev->tmp_buffer_8ch[bd_config->default_alsa_ch * i + j] = 0;
                }
            }
            *output_buffer = adev->tmp_buffer_8ch;
            *output_buffer_bytes = out_frames * 4 * bd_config->default_alsa_ch; /* from nchannels 32 bit to 8 ch 32 bit */
            out_data_info->audio_format = AUDIO_FORMAT_PCM_32_BIT;
            out_data_info->channel_mask = audio_channel_out_mask_from_count(bd_config->default_alsa_ch);
            if (enable_dump) {
                FILE *fp1 = fopen("/data/vendor/audiohal/ms12_out_10_spk.pcm", "a+");
                if (fp1) {
                    int flen = fwrite((char *)adev->tmp_buffer_8ch, 1, out_frames * 4 * bd_config->default_alsa_ch, fp1);
                    fclose(fp1);
                }
            }
        }
    }
    return 0;
}



ssize_t audio_hal_data_processing(struct audio_stream_out *stream,
                                const void *buffer,
                                size_t bytes,
                                audio_data_info_t * in_data_info,
                                void **output_buffer,
                                size_t *output_buffer_bytes,
                                audio_data_info_t * out_data_info)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct aml_audio_patch *patch = adev->audio_patch;
    size_t out_frames = 0;
    size_t buffer_need_size = bytes + EFFECT_PROCESS_BLOCK_SIZE;
    int ch = audio_channel_count_from_out_mask(in_data_info->channel_mask);
    int bytes_per_sample = audio_bytes_per_sample(in_data_info->audio_format);
    audio_format_t output_format = in_data_info->audio_format;

    size_t i, j;
    int ret;
    uint32_t latency_frames = 0;
    uint64_t total_frame = 0;
    int auge_chip = alsa_device_is_auge();

    if (ch == 0 || bytes_per_sample == 0) {
        ALOGE("%s invalid ch =%d bytes_per_sample=%d", __func__, ch, bytes_per_sample);
        return 0;
    }

    /*if it is more than 2 ch, we need to use below channel map process*/
    if (ch > 2) {
        processing_multich_pcm(stream, buffer, bytes, in_data_info, output_buffer, output_buffer_bytes, out_data_info);
        return 0;
    }

    out_frames = bytes / (ch * bytes_per_sample);

    /* raw data need packet to IEC61937 format by spdif encoder */
    if (output_format == AUDIO_FORMAT_IEC61937) {
        //ALOGI("IEC61937 Format");
        *output_buffer = (void *) buffer;
        *output_buffer_bytes = bytes;
        out_data_info->audio_format = AUDIO_FORMAT_IEC61937;
        out_data_info->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    } else if ((output_format == AUDIO_FORMAT_AC3) || (output_format == AUDIO_FORMAT_E_AC3)) {
        //ALOGI("%s, aml_out->hal_format %x , is_iec61937_format = %d, \n", __func__,
        //      aml_out->hal_format,is_iec61937_format(stream));
        if ((is_iec61937_format(stream) == true) ||
            (adev->dolby_lib_type == eDolbyDcvLib)) {
            *output_buffer = (void *) buffer;
            *output_buffer_bytes = bytes;
            /* Need mute when play DTV, HDMI output. */
            if (adev->sink_gain[OUTPORT_HDMI] < FLOAT_ZERO &&
                (AUDIO_DEVICE_OUT_HDMI & adev->cur_out_devices) && adev->audio_patching) {
                memset(*output_buffer, 0, bytes);
            }
        } else {
            if (aml_out->spdifenc_init == false) {
                ALOGI("%s, aml_spdif_encoder_open, output_format %#x\n", __func__, output_format);
                ret = aml_spdif_encoder_open(&aml_out->spdifenc_handle, output_format);
                if (ret) {
                    ALOGE("%s() aml_spdif_encoder_open failed", __func__);
                    return ret;
                }
                aml_out->spdifenc_init = true;
                aml_out->spdif_enc_init_frame_write_sum = aml_out->frame_write_sum;
                adev->spdif_encoder_init_flag = true;
            }
            aml_spdif_encoder_process(aml_out->spdifenc_handle, buffer, bytes, output_buffer, output_buffer_bytes);
        }
        out_data_info->audio_format = AUDIO_FORMAT_IEC61937;
        out_data_info->sub_format   = output_format;
        out_data_info->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    } else if (output_format == AUDIO_FORMAT_DTS) {
        *output_buffer = (void *) buffer;
        *output_buffer_bytes = bytes;
        out_data_info->audio_format = AUDIO_FORMAT_IEC61937;
        out_data_info->sub_format   = output_format;
        out_data_info->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    } else if (output_format == AUDIO_FORMAT_PCM_32_BIT) {
        int32_t *tmp_buffer = (int32_t *)buffer;
        out_frames = bytes / FRAMESIZE_32BIT_STEREO;
        if (!aml_out->ms12_vol_ctrl) {
            float gain_speaker = adev->sink_gain[OUTPORT_SPEAKER];
            if (aml_out->hw_sync_mode)
                gain_speaker *= aml_out->volume_l;
            apply_volume(gain_speaker, tmp_buffer, sizeof(uint32_t), bytes);

        }

        /* 2 ch 32 bit --> 8 ch 32 bit mapping, need 8X size of input buffer size */
        ret = aml_audio_check_and_realloc((void **)&adev->tmp_buffer_8ch, &adev->tmp_buffer_8ch_size,
            FRAMESIZE_32BIT_8ch * out_frames);
        R_CHECK_RET(ret, "alloc tmp_buffer_8ch size:%zu fail", FRAMESIZE_32BIT_8ch * out_frames);

        for (i = 0; i < out_frames; i++) {
            adev->tmp_buffer_8ch[8 * i] = tmp_buffer[2 * i];
            adev->tmp_buffer_8ch[8 * i + 1] = tmp_buffer[2 * i + 1];
            adev->tmp_buffer_8ch[8 * i + 2] = tmp_buffer[2 * i];
            adev->tmp_buffer_8ch[8 * i + 3] = tmp_buffer[2 * i + 1];
            adev->tmp_buffer_8ch[8 * i + 4] = tmp_buffer[2 * i];
            adev->tmp_buffer_8ch[8 * i + 5] = tmp_buffer[2 * i + 1];
            adev->tmp_buffer_8ch[8 * i + 6] = 0;
            adev->tmp_buffer_8ch[8 * i + 7] = 0;
        }

        *output_buffer = adev->tmp_buffer_8ch;
        *output_buffer_bytes = FRAMESIZE_32BIT_8ch * out_frames;
        out_data_info->audio_format = AUDIO_FORMAT_PCM_32_BIT;
        out_data_info->channel_mask = AUDIO_CHANNEL_OUT_7POINT1;
    } else {
        if (aml_out->is_tv_platform == 1) {
            struct audio_board_config *bd_config = &adev->board_config;
            aml_audio_out_dev_type_e num_dev = bd_config->default_alsa_ch / 2;

            ret = aml_audio_check_and_realloc((void **)&adev->out_16_buf, &adev->out_16_buf_size, buffer_need_size);
            R_CHECK_RET(ret, "alloc out_16_buf size:%zu fail", bytes);

            ret = aml_audio_check_and_realloc((void **)&adev->out_32_buf, &adev->out_32_buf_size, 2 * buffer_need_size);
            R_CHECK_RET(ret, "alloc out_32_buf size:%zu fail", 2 * bytes);

            /* 2 ch 16 bit --> x ch 32 bit mapping, need x*size of input buffer size */
            ret = aml_audio_check_and_realloc((void **)&adev->tmp_buffer_8ch, &adev->tmp_buffer_8ch_size,
                    bd_config->default_alsa_ch * buffer_need_size);
            R_CHECK_RET(ret, "alloc tmp_buffer_8ch size:%zu fail", bd_config->default_alsa_ch * bytes);

            audio_config_base_t in_data_config = {48000, AUDIO_CHANNEL_OUT_STEREO, AUDIO_FORMAT_PCM_16_BIT};
            if (is_include_sco_out_port(adev->cur_out_devices)) {
                memcpy(adev->out_16_buf, buffer, bytes);
                write_to_sco(adev, &in_data_config, buffer, bytes);
            } else if (is_include_a2dp_out_port(adev->cur_out_devices)) {
                memcpy(adev->out_16_buf, buffer, bytes);
                float volume = aml_audio_get_s_gain_by_src(adev, adev->patch_src);
                if (is_tvinput_source(adev->patch_src) && adev->audio_patching) {
                    /* for dev->a2dp path, volume control in audio hal. */
                    volume *= adev->sink_gain[OUTPORT_A2DP];
                } else {
                    /* for mix->a2dp path, volume control in AudioFlinger. */
                }
                apply_volume(volume, adev->out_16_buf, sizeof(uint16_t), bytes);
                a2dp_out_write(adev, &in_data_config, adev->out_16_buf, bytes);
            }

            bool dap_processing = is_audio_postprocessing_add_dolbyms12_dap(adev) && adev->ms12.dolby_ms12_enable;
            if (dap_processing) {
                ret = aml_audio_check_and_realloc((void **)&adev->audioeffect_tmp_buffer, &adev->audioeffect_tmp_buffer_size, buffer_need_size);
                R_CHECK_RET(ret, "alloc audioeffect_tmp_buffer size:%zu fail", buffer_need_size);

                if (adev->ms12.spdif_ring_buffer.size && get_buffer_read_space(&adev->ms12.spdif_ring_buffer) >= (int)bytes) {
                    ring_buffer_read(&adev->ms12.spdif_ring_buffer, (unsigned char*)adev->audioeffect_tmp_buffer, bytes);
                }
            }

            for (int dev = AML_AUDIO_OUT_DEV_TYPE_SPEAKER; dev < num_dev; dev++) {
                float volume = aml_audio_get_s_gain_by_src(adev, adev->patch_src);
                memcpy(adev->out_16_buf, buffer, bytes);

                /* apply volume for SPK/HP/SPDIF/HDMItx, HMDITX for BDS platform */
                /* all source should apply source gain, spk: spk volume + effect */
                if (dev == AML_AUDIO_OUT_DEV_TYPE_SPEAKER) {
                    /* special add external gain for media->speaker */
                    if (adev->patch_src != SRC_DTV && adev->patch_src != SRC_ATV &&
                        adev->patch_src != SRC_LINEIN && adev->patch_src != SRC_HDMIIN) {
                        volume *= adev->eq_data.p_gain.media2spk_extra_gain;
                    }
                    volume *= adev->eq_data.p_gain.speaker * adev->sink_gain[OUTPORT_SPEAKER];

                    /* for ms12 lib, and audio volume control in ms12, bypass all volume apply */
                    if (eDolbyMS12Lib == adev->dolby_lib_type && aml_out->ms12_vol_ctrl) {
                        volume = 1.0;
                    } else if (adev->volume_ease.config_easing) {
                        /* start audio volume easing */
                        float vol_now = aml_audio_ease_get_current_volume(adev->volume_ease.ease);
                        config_volume_easing(adev->volume_ease.ease, vol_now, volume);
                        adev->volume_ease.config_easing = false;
                    }

                    out_frames = audio_post_process(&adev->native_postprocess, adev->out_16_buf, out_frames);
                    bytes = out_frames * 4;

                    if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
                        aml_audio_dump_audio_bitstreams("/data/vendor/audiohal/audio_spk.pcm", adev->out_16_buf, bytes);
                    }
                } else if (dev == AML_AUDIO_OUT_DEV_TYPE_SPDIF) {
                    volume *= adev->eq_data.p_gain.spdif_arc;
                } else if (dev == AML_AUDIO_OUT_DEV_TYPE_HEADPHONE) {
                    volume *= adev->eq_data.p_gain.headphone * adev->sink_gain[OUTPORT_HEADPHONE];
                    if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
                        aml_audio_dump_audio_bitstreams("/data/vendor/audiohal/audio_headphone.pcm", adev->out_16_buf, bytes);
                    }
                } else if (dev == AML_AUDIO_OUT_DEV_TYPE_OTHER) {
                    /* todo: apply speaker volume for hdmitx of BDS */
                    volume *= adev->sink_gain[OUTPORT_SPEAKER];
                }

                if (dap_processing && dev != AML_AUDIO_OUT_DEV_TYPE_SPEAKER) {
                    memcpy(adev->out_16_buf, (unsigned char*)adev->audioeffect_tmp_buffer, bytes);
                }

                /* For local play or dtv input, analog audio output channel should be switched by User setting*/
                if ((dev == AML_AUDIO_OUT_DEV_TYPE_SPEAKER || dev == AML_AUDIO_OUT_DEV_TYPE_HEADPHONE) &&
                        (adev->audio_patch == NULL || adev->patch_src == SRC_DTV)) {
                    aml_audio_switch_output_mode((int16_t *)adev->out_16_buf, bytes, adev->sound_track_mode);
                }

#ifdef ADD_AUDIO_DELAY_INTERFACE
                if (dev != AML_AUDIO_OUT_DEV_TYPE_OTHER) {
                    aml_audio_delay_process(out_dev_convert_to_delay_type(dev), adev->out_16_buf, bytes,
                            AUDIO_FORMAT_PCM_16_BIT, MM_FULL_POWER_SAMPLING_RATE);
                }
#endif
                if (!adev->volume_ease.ease->do_easing || dev != AML_AUDIO_OUT_DEV_TYPE_SPEAKER) {
                    apply_volume_16to32(volume, adev->out_16_buf, adev->out_32_buf, bytes);
                } else {
                    /*do ease process when adjust vol,vol apply is handled by ease process,when ease process finished,
                    vol apply need handled by apply volume function,vol is float type,use fabs to compare*/
                    apply_volume_16to32(1.0, adev->out_16_buf, adev->out_32_buf, bytes);
                    aml_audio_ease_process(adev->volume_ease.ease, adev->out_32_buf, bytes * 2);
                }

                for (j = 0; j < out_frames; j++) {
                    adev->tmp_buffer_8ch[bd_config->default_alsa_ch * j + 2 * alsa_out_ch_mask[auge_chip][dev]]      = adev->out_32_buf[2 * j];
                    adev->tmp_buffer_8ch[bd_config->default_alsa_ch * j + 2 * alsa_out_ch_mask[auge_chip][dev] + 1]  = adev->out_32_buf[2 * j + 1];
                }
            }
            *output_buffer = adev->tmp_buffer_8ch;
            *output_buffer_bytes = bd_config->default_alsa_ch * bytes;
            out_data_info->audio_format = AUDIO_FORMAT_PCM_32_BIT;
            out_data_info->channel_mask = audio_channel_out_mask_from_count(bd_config->default_alsa_ch);

            /* use original information */
            if (is_include_sco_out_port(adev->cur_out_devices)) {
                *output_buffer =(void *)buffer;
                *output_buffer_bytes = bytes;
                out_data_info->audio_format = AUDIO_FORMAT_PCM_16_BIT;
                out_data_info->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
            }
        } else {
            if (adev->patch_src == SRC_DTV && adev->audio_patch != NULL) {
                aml_audio_switch_output_mode((int16_t *)buffer, bytes, adev->audio_patch->mode);
            } else if (adev->audio_patch == NULL) {
                aml_audio_switch_output_mode((int16_t *)buffer, bytes, adev->sound_track_mode);
            }

            *output_buffer = (void *) buffer;
            *output_buffer_bytes = bytes;
            out_data_info->audio_format = AUDIO_FORMAT_PCM_16_BIT;
            out_data_info->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
        }
    }
    /*when REPORT_DECODED_INFO is added, we will enable it*/
#ifdef ENABLE_DVB_PATCH
    if (get_audio_info_enable(DUMP_AUDIO_INFO_DECODE)) {
        get_dtv_amadec_audio_info(adev);
    }
#endif
    if (adev->dev2mix_patch) {
        if (patch && (patch->need_do_avsync == true) && (patch->input_signal_stable == false) &&
            ((adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) || (adev->out_device & AUDIO_DEVICE_OUT_ALL_USB)) &&
             ((adev->in_device & AUDIO_DEVICE_IN_HDMI) || (adev->in_device & AUDIO_DEVICE_IN_LINE))){
        } else {
            tv_in_write(stream, buffer, bytes);
            memset((char *)buffer, 0, bytes);
        }
        if (aml_out->is_tv_platform == 1) {
           memset(adev->tmp_buffer_8ch, 0, (*output_buffer_bytes));
        }
    }

    return 0;
}


ssize_t hw_write (struct audio_stream_out *stream
                  , const void *buffer
                  , size_t bytes
                  , audio_data_info_t * data_info)
{
    AM_LOGV ("+%s() buffer %p bytes %zu", __func__, buffer, bytes);
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    const uint16_t *tmp_buffer = buffer;
    int16_t *effect_tmp_buf = NULL;
    struct aml_audio_patch *patch = adev->audio_patch;
    bool is_dtv = (adev->patch_src == SRC_DTV);
    int ch = audio_channel_count_from_out_mask(data_info->channel_mask);
    int bytes_per_sample = audio_bytes_per_sample(data_info->audio_format);
    audio_format_t output_format = data_info->audio_format;
    if (output_format == AUDIO_FORMAT_IEC61937 && data_info->sub_format != AUDIO_FORMAT_DEFAULT) {
        output_format = data_info->sub_format;
    }

    int out_frames = 0;
    ssize_t ret = 0;
    int i;
    uint32_t latency_frames = 0;
    uint64_t total_frame = 0;
    uint64_t write_frames = 0;
    uint64_t  sys_total_cost = 0;
    int  adjust_ms = 0;
    int  alsa_port = -1;

    if (ch == 0 || bytes_per_sample == 0) {
        ALOGE("%s invalid ch =%d bytes_per_sample=%d", __func__, ch, bytes_per_sample);
        return -1;
    }

    out_frames = bytes / (ch * bytes_per_sample);
    adev->debug_flag = aml_audio_get_debug_flag();
    if (adev->debug_flag) {
        ALOGI("+%s() buffer %p bytes %zu, format %#x out %p hw_sync_mode %d\n",
            __func__, buffer, bytes, output_format, aml_out, aml_out->hw_sync_mode);
    }
    if (patch && !adev->is_multi_demux) {
        if (is_dtv && need_hw_mix(adev->usecase_masks)) {
        if (adev->audio_patch->avsync_callback && aml_out->dtvsync_enable)
            adev->audio_patch->avsync_callback(stream, bytes, output_format);
        }
        if (patch->skip_amadec_flag) {
            if (patch->dtv_apts_lookup >= 0 && !patch->pcm_inserting)  {
                if (adev->is_TV) {
                    patch->outlen_after_last_validpts += (bytes / 8);
                } else {
                    patch->outlen_after_last_validpts += bytes;
                }
            }
        }
    }

    pthread_mutex_lock(&adev->alsa_pcm_lock);
    aml_out->alsa_output_format = output_format;
    if (aml_out->stream_status != STREAM_HW_WRITING) {
        ALOGI("%s, aml_out %p alsa open output_format %#x\n", __func__, aml_out, output_format);
        if (adev->useSubMix) {
            if (/*adev->audio_patching &&*/
                output_format != AUDIO_FORMAT_PCM_16_BIT &&
                output_format != AUDIO_FORMAT_PCM) {
                // TODO: mbox+dvb and bypass case
                ret = aml_alsa_output_open(stream);
                if (ret) {
                    ALOGE("%s() open failed", __func__);
                }
            } else {
                aml_out->pcm = getSubMixingPCMdev(adev->sm);
                if (aml_out->pcm == NULL) {
                    ALOGE("%s() get pcm handle failed", __func__);
                }
                if (adev->raw_to_pcm_flag && aml_out->pcm) {
                    ALOGI("disable raw_to_pcm_flag --");
                    pcm_stop(aml_out->pcm);
                    adev->raw_to_pcm_flag = false;
                }
            }
        } else {
            {
                ret = aml_alsa_output_open(stream);
                if (ret) {
                    ALOGE("%s() open failed", __func__);
                }
#ifdef AUDIO_KARA
                check_switch_audio_kara(stream);
#endif
            }
        }
#ifdef ENABLE_DVB_PATCH
        if (is_dtv) {
            audio_set_spdif_clock(aml_out, get_codec_type(output_format));
        }
#endif
        aml_out->stream_status = STREAM_HW_WRITING;
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type && !is_bypass_dolbyms12(stream)) {
        if (aml_out->hw_sync_mode && !adev->ms12.is_continuous_paused) {
            // history here: ms12lib->pcm_output()->hw_write() aml_out is passed as private data
            //               when registering output callback function in dolby_ms12_register_pcm_callback()
            // some times "aml_out->hwsync->aout == NULL"
            // one case is no main audio playing, only aux audio playing (Netflix main screen)
            // in this case dolby_ms12_get_consumed_payload() always return 0, no AV sync can be done zzz
            if (aml_out->hwsync->aout) {
                if (is_bypass_dolbyms12(stream)) {
                    aml_audio_hwsync_audio_process(aml_out->hwsync, aml_out->hwsync->payload_offset, out_frames, &adjust_ms);
                }
                else {
                    if (!audio_is_linear_pcm(aml_out->hal_internal_format)) {
                        /*if udc decode doesn't generate any data, we should not use the consume offset to get pts*/
                        ALOGV("udc generate pcm =%" PRId64 "", dolby_ms12_get_main_pcm_generated(stream));
                        if (dolby_ms12_get_main_pcm_generated(stream)) {
                            aml_audio_hwsync_audio_process(aml_out->hwsync, dolby_ms12_get_main_bytes_consumed(stream), out_frames, &adjust_ms);
                        }
                    } else {
                        /* because the pcm consumed payload offset is at the end of consume buffer,
                         * we need the beginning position and ms12 always
                         * output 1536 frame every time
                         */
                        uint64_t consume_payload = dolby_ms12_get_main_bytes_consumed(stream);
                        /* for non 48khz hwsync pcm, the pts check in is used original 44.1khz offset,
                         * but we resample it before send to ms12, so we consumed offset is 48khz,
                         * so we need convert it
                         */
                        if (audio_is_linear_pcm(aml_out->hwsync->aout->hal_internal_format) &&
                            (aml_out->hwsync->aout->hal_rate != 48000)) {
                            consume_payload = consume_payload * aml_out->hwsync->aout->hal_rate / 48000;
                        }

                        aml_audio_hwsync_audio_process(aml_out->hwsync, consume_payload, out_frames, &adjust_ms);
                    }
                }
            } else {
                if (adev->debug_flag) {
                    ALOGI("%s,aml_out->hwsync->aout == NULL",__FUNCTION__);
                }
            }
        }
    } else {
        if (aml_out->hw_sync_mode && aml_out->is_insert_zero_data) {
            adjust_ms = aml_out->insert_zero_data_ms;
        }
    }
    if (aml_out->pcm || adev->a2dp_hal || is_include_sco_out_port(adev->cur_out_devices)) {
#ifdef ADD_AUDIO_DELAY_INTERFACE
        ret = aml_audio_delay_process(AML_DELAY_OUTPORT_ALL, (void *) tmp_buffer, bytes,
                output_format, MM_FULL_POWER_SAMPLING_RATE);
        if (ret < 0) {
            //ALOGW("aml_audio_delay_process skip, ret:%#x", ret);
        }
#endif
        if (adjust_ms) {
            int adjust_bytes = 0;
            if ((output_format == AUDIO_FORMAT_E_AC3) || (output_format == AUDIO_FORMAT_AC3)) {
                int i = 0;
                int bAtmos = 0;
                int insert_frame = adjust_ms/32;
                int raw_size = 0;
                if (insert_frame > 0) {
                    char *raw_buf = NULL;
                    char *temp_buf = NULL;
                    /*atmos lock or input is ddp atmos*/
                    if (adev->atoms_lock_flag ||
                        (adev->ms12.is_dolby_atmos && adev->ms12_main1_dolby_dummy == false)) {
                        bAtmos = 1;
                    }
                    raw_buf = aml_audio_get_muteframe(output_format, &raw_size, bAtmos);
                    ALOGI("insert atmos=%d raw frame size=%d times=%d", bAtmos, raw_size, insert_frame);
                    if (raw_buf && (raw_size > 0)) {
                        temp_buf = aml_audio_malloc(raw_size);
                        if (!temp_buf) {
                            ALOGE("%s malloc failed", __func__);
                            pthread_mutex_unlock(&adev->alsa_pcm_lock);
                            return -1;
                        }
                        for (i = 0; i < insert_frame; i++) {
                            memcpy(temp_buf, raw_buf, raw_size);
                            /*coverity[sleep]*/
                            ret = aml_alsa_output_write(stream, (void*)temp_buf, raw_size);
                            if (ret < 0) {
                                ALOGE("%s alsa write fail when insert", __func__);
                                break;
                            }
                        }
                        aml_audio_free(temp_buf);
                    }
                }
            } else {
                memset((void*)buffer, 0, bytes);
                if (adev->is_TV) {
                    adjust_bytes = 48 * 32 * abs(adjust_ms);    //8ch 32 bit.
                } else {
                    adjust_bytes = 48 * 4 * abs(adjust_ms); // 2ch 16bit
                }
                adjust_bytes &= ~255;
                ALOGI("%s hwsync audio need %s %d ms,adjust bytes %d",
                      __func__, adjust_ms > 0 ? "insert" : "skip", abs(adjust_ms), adjust_bytes);
                if (adjust_ms > 0) {
                    char *buf = aml_audio_malloc(1024);
                    int write_size = 0;
                    if (!buf) {
                        ALOGE("%s malloc failed", __func__);
                        pthread_mutex_unlock(&adev->alsa_pcm_lock);
                        return -1;
                    }
                    memset(buf, 0, 1024);
                    while (adjust_bytes > 0) {
                        write_size = adjust_bytes > 1024 ? 1024 : adjust_bytes;
                        if (!adev->is_TV && !adev->control_hdmitx_mute && is_include_a2dp_out_port(adev->cur_out_devices)) {
                            // For STB, do not send data to spdif/hdmitx when bt is connected and mute hdmitx cannot be controlled.
                        } else {
                            ret = aml_alsa_output_write(stream, (void*)buf, write_size);
                        }
                        if (ret < 0) {
                            ALOGE("%s alsa write fail when insert", __func__);
                            break;
                        }
                        adjust_bytes -= write_size;
                    }
                    aml_audio_free(buf);
                } else {
                    //do nothing.
                    /*
                    if (bytes > (size_t)adjust_bytes)
                        bytes -= adjust_bytes;
                    else
                        bytes = 0;
                    */
                }
            }
        }

        if (!adev->is_TV && !adev->control_hdmitx_mute && is_include_a2dp_out_port(adev->cur_out_devices)) {
            // For STB, do not send data to spdif/hdmitx when bt is connected and mute hdmitx cannot be controlled.
        } else {
#ifdef AUDIO_KARA
            check_switch_audio_kara(stream);
            if (aml_out->kara) {
                // WARNING: buffer is changed, discard 'const' qualifiers
                ret = audio_kara_mix(aml_out->kara, (void *)buffer, bytes);
            }
#endif
            ret = aml_alsa_output_write(stream, (void *) buffer, bytes); // HDMI output HERE
        }

        //ALOGE("!!aml_alsa_output_write"); ///zzz
        if (ret < 0) {
            ALOGE("ALSA out write fail");
            aml_out->frame_write_sum += out_frames;
        } else {
            if (!continuous_mode(adev)) {
                if ((output_format == AUDIO_FORMAT_AC3) ||
                    (output_format == AUDIO_FORMAT_E_AC3) ||
                    (output_format == AUDIO_FORMAT_MAT) ||
                    (output_format == AUDIO_FORMAT_DTS)) {
                    if (is_iec61937_format(stream) == true) {
                        //continuous_audio_mode = 0, when mixing-off and 7.1ch
                        //FIXME out_frames 4 or 16 when DD+ output???
                        aml_out->frame_write_sum += out_frames;

                    } else {
                        int sample_per_bytes = (output_format == AUDIO_FORMAT_MAT) ? 64 :
                                                (output_format == AUDIO_FORMAT_E_AC3) ? 16 : 4;

                        if (eDolbyDcvLib == adev->dolby_lib_type && aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                            aml_out->frame_write_sum = aml_out->input_bytes_size / audio_stream_out_frame_size(stream);
                            //aml_out->frame_write_sum += out_frames;
                        } else {
                            aml_out->frame_write_sum += bytes / sample_per_bytes; //old code
                            //aml_out->frame_write_sum = spdif_encoder_ad_get_total() / sample_per_bytes + aml_out->spdif_enc_init_frame_write_sum; //8.1
                        }
                    }
                } else {
                    aml_out->frame_write_sum += out_frames;
                    total_frame =  aml_out->frame_write_sum;
                }
            }
        }
    }
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        latency_frames = aml_audio_out_get_ms12_latency_frames(stream);
    } else {
        latency_frames = out_get_latency_frames(stream);
    }

    pthread_mutex_unlock(&adev->alsa_pcm_lock);

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        /*it is the main alsa write function we need notify that to all sub-stream */
        adev->ms12.latency_frame = latency_frames;
        if (continuous_mode(adev)) {
            adev->ms12.sys_avail = dolby_ms12_get_system_buffer_avail(NULL);
        }
        //ALOGD("alsa latency=%d", latency_frames);
    }

    /*
    */
    if (!continuous_mode(adev)) {
        if (aml_out->hal_internal_format == AUDIO_FORMAT_PCM_16_BIT) {
            write_frames = aml_out->input_bytes_size / aml_out->hal_frame_size;
            //total_frame = write_frames;
        } else {
            if (eDolbyMS12Lib != adev->dolby_lib_type) {
                total_frame = aml_out->frame_write_sum + aml_out->frame_skip_sum;
            } else {
                total_frame = aml_out->frame_write_sum + aml_out->frame_skip_sum + aml_out->frame_offset;
            }
        }
    } else {
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            if (!audio_is_linear_pcm(aml_out->hal_internal_format)) {
                /*use the pcm which is generated by udc, to get the total frame by nbytes/nbytes_per_sample
                 *Please be careful about the aml_out->continuous_audio_offset;*/

                total_frame = dolby_ms12_get_main_pcm_generated(stream);
                write_frames =  aml_out->total_ddp_frame_nblks * 256;/*256samples in one block*/
                if (adev->debug_flag) {
                    ALOGI("%s,total_frame %"PRIu64" write_frames %"PRIu64" total frame block nums %"PRIu64"",
                        __func__, total_frame, write_frames, aml_out->total_ddp_frame_nblks);
                }
            }
            /*case 3:pcm_hwsync*/
            else if (aml_out->hw_sync_mode) {
                total_frame = dolby_ms12_get_main_pcm_generated(stream);
            }
            /*case 1:pcm_direct+pcm_normal*/
            else {
                /*system volume calculation is done inside aux write thread*/
                total_frame = dolby_ms12_get_main_pcm_generated(stream);
                //write_frames = (aml_out->input_bytes_size - dolby_ms12_get_system_buffer_avail(NULL)) / 4;
                //total_frame = write_frames;
            }
        }

    }

    /*we should also to calculate the alsa latency*/
    {
        /* SWPL-88828
         * If out_get_presentation_position() and hw_write()
         * are called by different threads, frames_written_hw
         * and timestamp may not be updated synchronously. This can cause jitter.
         */
        pthread_mutex_lock(&aml_out->apts_update_lock);
        clock_gettime (CLOCK_MONOTONIC, &aml_out->timestamp);
        aml_out->lasttimestamp.tv_sec = aml_out->timestamp.tv_sec;
        aml_out->lasttimestamp.tv_nsec = aml_out->timestamp.tv_nsec;
        if (total_frame >= latency_frames) {
            if (!adev->frame_write_sum_updated || aml_out->is_insert_zero_data) {
                aml_out->last_frames_position = total_frame;
            } else {
                aml_out->last_frames_position = total_frame - latency_frames;
            }
            ALOGV("%s  frame_write_sum_updated:%d, total_frame:%" PRIu64 ", latency_frames:%u", __func__, adev->frame_write_sum_updated, total_frame, latency_frames);
        } else {
            aml_out->last_frames_position = 0;
        }
        aml_out->position_update = 1;
        pthread_mutex_unlock(&aml_out->apts_update_lock);
        //ALOGI("position =%lld time sec = %ld, nanosec = %ld", aml_out->last_frames_position, aml_out->lasttimestamp.tv_sec , aml_out->lasttimestamp.tv_nsec);
    }
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (continuous_mode(adev)) {
            if (adev->ms12.is_continuous_paused) {
                if (total_frame == adev->ms12.last_ms12_pcm_out_position) {
                    adev->ms12.ms12_position_update = false;
                }
            }
            /* the ms12 generate pcm out is not changed, we assume it is the same one
             * don't update the position
             */
            if (total_frame != adev->ms12.last_ms12_pcm_out_position) {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                adev->ms12.timestamp.tv_sec = ts.tv_sec;
                adev->ms12.timestamp.tv_nsec = ts.tv_nsec;
                adev->ms12.last_frames_position = aml_out->last_frames_position;
                adev->ms12.last_ms12_pcm_out_position = total_frame;
                adev->ms12.ms12_position_update = true;
            }
        }
        /* check sys audio position */
        sys_total_cost = dolby_ms12_get_consumed_sys_audio();
        if (adev->ms12.last_sys_audio_cost_pos != sys_total_cost) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            adev->ms12.sys_audio_timestamp.tv_sec = ts.tv_sec;
            adev->ms12.sys_audio_timestamp.tv_nsec = ts.tv_nsec;
            /*FIXME. 2ch 16 bit audio */
            adev->ms12.sys_audio_frame_pos = adev->ms12.sys_audio_base_pos + adev->ms12.sys_audio_skip + sys_total_cost - latency_frames;
            adev->ms12.sys_data_write2alsa_status = true;
        }
        if (adev->debug_flag) {
            ALOGI("%s  ms12.last_frames_position:%" PRIu64 ",last_ms12_pcm_out_position:%" PRIu64 "; sys audio pos %"PRIu64" ms ,sys_total_cost %"PRIu64",base pos %"PRIu64",latency %d \n", __func__,
                  adev->ms12.last_frames_position, adev->ms12.last_ms12_pcm_out_position,
                  adev->ms12.sys_audio_frame_pos/48,sys_total_cost,adev->ms12.sys_audio_base_pos,latency_frames);
        }
        adev->ms12.last_sys_audio_cost_pos = sys_total_cost;
    }
    if (adev->debug_flag) {
        ALOGI("%s() stream(%p) pcm handle %p format input %#x output %#x 61937 frame %d",
              __func__, stream, aml_out->pcm, aml_out->hal_internal_format, output_format, is_iec61937_format(stream));

        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            //ms12 internal buffer avail(main/associate/system)
            if (adev->ms12.dolby_ms12_enable == true) {
                ALOGI("%s MS12 buffer avail main %d associate %d system %d\n",
                      __FUNCTION__, dolby_ms12_get_main_buffer_avail(NULL), dolby_ms12_get_associate_buffer_avail(), dolby_ms12_get_system_buffer_avail(NULL));
            }
        }

        if ((aml_out->hal_internal_format == AUDIO_FORMAT_AC3) || (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3)) {
            ALOGI("%s() total_frame %"PRIu64" latency_frames %d last_frames_position %"PRIu64" total write %"PRIu64" total writes frames %"PRIu64" diff latency %"PRIu64" ms\n",
                  __FUNCTION__, total_frame, latency_frames, aml_out->last_frames_position, aml_out->input_bytes_size, write_frames, (write_frames - total_frame) / 48);
        } else {
            ALOGI("%s() total_frame %"PRIu64" latency_frames %d last_frames_position %"PRIu64" total write %"PRIu64" total writes frames %"PRIu64" diff latency %"PRIu64" ms\n",
                  __FUNCTION__, total_frame, latency_frames, aml_out->last_frames_position, aml_out->input_bytes_size, write_frames, (write_frames - aml_out->last_frames_position) / 48);
        }
    }
    return ret;
}




ssize_t aml_audio_pcm_output(struct audio_stream_out *stream,
                                const void *buffer,
                                size_t bytes,
                                audio_data_info_t * data_info) {
    ssize_t ret = 0;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    audio_data_info_t out_data_info = { 0 };

    if (audio_hal_data_processing((struct audio_stream_out *)stream, buffer, bytes, data_info, &output_buffer, &output_buffer_bytes, &out_data_info) == 0) {
        ret = hw_write(stream, output_buffer, output_buffer_bytes, &out_data_info);
    }


    return ret;
}

