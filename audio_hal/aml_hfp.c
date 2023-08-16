/*
 * Copyright (C) 2014 The Android Open Source Project
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

#define LOG_TAG "audio_hw_hal_hfp"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <log/log.h>

#include "audio_hw.h"
#include <stdlib.h>
#include <cutils/str_parms.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <cutils/log.h>
#include "aml_hfp.h"
#include "aml_audio_signal_process.h"
#include <audio_utils/channels.h>
#include <pthread.h>

struct hfp_module hfpmod = {
    .hfp_sco_rx = NULL,
    .hfp_sco_tx = NULL,
    .hfp_pcm_rx = NULL,
    .hfp_pcm_tx = NULL,
    .hfp_volume = 0,
    .mic_volume = CAPTURE_VOLUME_DEFAULT,
    .hfp_vol_mixer_ctl = {0, },
    .is_hfp_running = 0,
    .mic_mute = 0,
    .hfp_card = 0,
};

struct pcm_config pcm_config_hfp = {
    .channels = 1,
    .rate = 16000,
    .period_size = 240,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = 0,
    .avail_min = 0,
};

struct pcm_config pcm_config_hfp_aec = {
    .channels = 6,
    .rate = 16000,
    .period_size = 240,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = 0,
    .avail_min = 0,
};

struct pcm_config pcm_config_hfp_hfp_rx = {
    .channels = 8,
    .rate = 48000,
    .period_size = 512,
    .period_count = 6,
    .format = PCM_FORMAT_S32_LE,
    .start_threshold = 512 * 2,
    .stop_threshold = 3072,
    .avail_min = 0,
};

void upmix_to_stereo_i16_from_mono_i16(int16_t *dst, const int16_t *src, size_t count)
{
    dst += count * 2;
    src += count;
    for (; count > 0; --count) {
        const int32_t temp = *--src;
        dst -= 2;
        dst[0] = temp;
        dst[1] = temp;
    }
}

static DL_HFP_T *g_dl_task_hfp = NULL;

static int32_t hfp_set_volume(struct aml_audio_device *adev, float value)
{
    int32_t vol, ret = 0;
    UNUSED(adev);

    if (value < 0.0) {
        ALOGW("%s: (%f) Under 0.0, assuming 0.0\n", __func__, value);
        value = 0.0;
    } else {
        hfpmod.hfp_volume = ((value > 15.000000) ? 1.0 : (value / HFP_VOL));
        ALOGW("%s: Volume brought with in range (%f)\n", __func__, value);
    }

    ALOGV("%s: exit", __func__);
    return ret;
}

bool if_hfp_running(struct aml_stream_out *hfp_out, struct audio_stream_out *stream, size_t bytes) {
     hfpmod.hfp_pcm_rx = hfp_out->pcm;

     if (!hfpmod.is_hfp_running) {
         return false;
     } else {
         size_t frame_size = audio_stream_out_frame_size(stream);
         usleep(bytes * 1000000 / frame_size / 48000);
         aml_audio_sleep(5000);
         return true;
     }
}

bool if_hfp_running_submix(output_port *port, int bytes) {
    hfpmod.hfp_pcm_rx_sub = port->pcm_handle;
    UNUSED(bytes);
    if (!hfpmod.is_hfp_running) {
        return false;
    } else {
        return true;
    }
}
/**
 * \create an instance of aec
 * \param mic_chan: the number of mic channels in the total channel during loopback recording
 * \param pconfig: the configuration parameter when using pcm_open to open the device
 * \return structure containing necessary parameters and instances of aec
 */
static struct aec_context* aec_create(int mic_channels, struct pcm_config config)
{
    if (config.rate != 8000 && config.rate != 16000) {
        AM_LOGE("%s aec does not support %d rate\n", __func__, config.rate);
        return NULL;
    }
    AI_ASP_CONFIG_S mConfig;

    memset(&mConfig, 0, sizeof(mConfig));
    mConfig.SampleRate = config.rate; // 8k 16k

    mConfig.FrameSample = AML_FRAME_SIZE;
    mConfig.FormatDataByte = AML_DATA_BIT >> 3;
    mConfig.MicChannels = mic_channels;
    mConfig.RefChannels = config.channels - mic_channels;

    mConfig.stFrontCfg.f32MicMulValue = 1.0; //(0.0, 10.0)
    mConfig.stFrontCfg.s32AdaptDelayAlign = 1;
    mConfig.stFrontCfg.s32RefreshFreqGap = 10;
    mConfig.stFrontCfg.s32RefDelayMicAlignLength = 16000; //[0, 4*FrameSample]
    mConfig.stFrontCfg.s32RefDelayMicRange = 16000; //[0, 4*FrameSample]

    mConfig.bHpfOpen = 0;
    if (mConfig.bHpfOpen) {
        mConfig.stHpfCfg.s32Order = 1;
        mConfig.stHpfCfg.s32HpfFreq = 200;
    }

    mConfig.bAecOpen = 1;
    if (mConfig.bAecOpen) {
        mConfig.stAecCfg.s32Mode = 2;
        mConfig.stAecCfg.s32EchoPathLength = mConfig.SampleRate / 4;
        mConfig.stAecCfg.bUsrSingleRefChannelMode = 0;
        if (mConfig.stAecCfg.bUsrSingleRefChannelMode)
            mConfig.stAecCfg.s32SingleChannelId = 0;

        mConfig.stAecCfg.bPf1Open = 0;
        if (mConfig.stAecCfg.bPf1Open) {
            mConfig.stAecCfg.stAecPf1Cfg.s32BandWidth = 128;
            mConfig.stAecCfg.stAecPf1Cfg.s32RefreshLength = 10;
        }

        mConfig.stAecCfg.bPf2Open = 1;
        if (mConfig.stAecCfg.bPf2Open) {
            mConfig.stAecCfg.stAecPf2Cfg.s32Intensity = 2;
            mConfig.stAecCfg.stAecPf2Cfg.s32ComfortNoise = 1;
        }
        mConfig.stAecCfg.bPf3Open = 0;
    }

    mConfig.bDereverberationOpen = 0;
    if (mConfig.bDereverberationOpen) {
        mConfig.stDereverberationCfg.s32StartPoint = 1;
        mConfig.stDereverberationCfg.s32PathLength = 8;
    }

    mConfig.bAnrOpen = 1;
    if (mConfig.bAnrOpen) {
        mConfig.stAnrCfg.s32Mode = 1;
        mConfig.stAnrCfg.s32NrIntensity = 1;
        mConfig.stAnrCfg.s32Reserved = 0;
    }

    mConfig.bGainOpen = 0;
    if (mConfig.bGainOpen) {
        mConfig.stGainCfg.s32Mode = 1;
        mConfig.stGainCfg.s32FixGainDB = 25;
        mConfig.stGainCfg.s32AgcGainDB = 9;
        mConfig.stGainCfg.s32AgcGainLevel = 3;
    }

    mConfig.bNoiseOpen = 0;
    if (mConfig.bNoiseOpen) {
        mConfig.stNoiseCfg.s32Snr = 80;
    }
    mConfig.bDetecteEnergyOpen = 0;

    struct aec_context* aec = aml_audio_calloc(1, sizeof(struct aec_context));
    aec->config = config;
    aec->mic_channels = mic_channels;
    aec->aml_aec = aml_asp_create(&mConfig);
    return aec;
}

/**
 * \destroy an instance of aec
 * \param aml_aec: instance of aec
 * \return NULL
 */
static void aec_destroy(struct aec_context *aec)
{
    if (aec->aml_aec != NULL)
        aml_asp_destroy(aec->aml_aec);
    aec->aml_aec = NULL;
    aml_audio_free(aec);
}

/**
 * \AEC processing of input data
 * \param aec_task: structure pointer containing necessary parameters and instances of aec
 * \param in: input data
 * \param out: output data
 * \return 1 if aec process successful.
 * \return 0 if aec process failed.
 */
static int aec_process(struct aec_context* aec, void* in, void* out)
{
    int in_chan = aec->config.channels;
    int out_chan = aec->mic_channels;
    int bytes = pcm_format_to_bits(aec->config.format) >> 3;
    int process_ret = 0;

    if (aec->aml_aec != NULL) {
        int i = 0;
        for (i = 0; i < out_chan; i++)
            aml_asp_import(aec->aml_aec, (void *)((char *)in + i * bytes), in_chan, i, MicType);
        for (i = out_chan; i < in_chan; i++)
            aml_asp_import(aec->aml_aec, (void *)((char *)in + i * bytes), in_chan, i - out_chan, RefType);
        process_ret = aml_asp_process(aec->aml_aec);
        if (process_ret == 0) {
            AM_LOGE("%s %d error: no aec process\n", __func__, __LINE__);
            return process_ret;
        }
        for (i = 0; i < out_chan; i++)
            aml_asp_export(aec->aml_aec, (void *)((char *)out + i * bytes), out_chan, i);
    } else
        AM_LOGE("%s aml_aec is NULL\n", __func__);
    return process_ret;
}

static void* aml_hfp_ul_thread(void* data) {
    UL_HFP_T *ul_task = (UL_HFP_T *)data;
    void *buffer = NULL;
    void *buffer_aec = NULL;
    void *buffer_out = NULL;
    unsigned int size;
    unsigned int size_aec;
    unsigned int size_out;

    ul_task->pcm_hfp_pcm_tx = hfpmod.hfp_pcm_tx;
    ul_task->pcm_hfp_sco_rx = hfpmod.hfp_sco_rx;

    if (g_ul_task_hfp->thread_created == 0) {
        pthread_exit(0);
        AM_LOGI("thread_created is 0 exit thread");
        return NULL;
    }

/*Due to the limitations of libAudioSignalProcess. a, only 256 frames can be processed at a time*/
    size = pcm_frames_to_bytes(ul_task->pcm_hfp_pcm_tx, AML_FRAME_SIZE);//16ms
    buffer = aml_audio_calloc(1, size);

    if (!buffer) {
        AM_LOGE("Unable to allocate %u bytes", size);
        pcm_close(ul_task->pcm_hfp_pcm_tx);
        pcm_close(ul_task->pcm_hfp_sco_rx);
        ul_task->pcm_hfp_pcm_tx = NULL;
        ul_task->pcm_hfp_sco_rx = NULL;
        return NULL;
    }

    size_aec = ul_task->mic_channels * (pcm_format_to_bits(ul_task->config.format) >> 3) * AML_FRAME_SIZE;
    buffer_aec = aml_audio_calloc(1, size_aec);

    if (!buffer_aec) {
        AM_LOGE("Unable to allocate %u bytes", size_aec);
        aml_audio_free(buffer);
        pcm_close(ul_task->pcm_hfp_pcm_tx);
        pcm_close(ul_task->pcm_hfp_sco_rx);
        ul_task->pcm_hfp_pcm_tx = NULL;
        ul_task->pcm_hfp_sco_rx = NULL;
        return NULL;
    }

    size_out = pcm_config_hfp.channels * (pcm_format_to_bits(ul_task->config.format) >> 3) * AML_FRAME_SIZE;
    buffer_out = aml_audio_calloc(1, size_out);

    if (!buffer_out) {
        AM_LOGE("Unable to allocate %u bytes", size_out);
        aml_audio_free(buffer_aec);
        aml_audio_free(buffer);
        pcm_close(ul_task->pcm_hfp_pcm_tx);
        pcm_close(ul_task->pcm_hfp_sco_rx);
        ul_task->pcm_hfp_pcm_tx = NULL;
        ul_task->pcm_hfp_sco_rx = NULL;
        return NULL;
    }

    while (!ul_task->exit_run && hfpmod.is_hfp_running) {
          int ret = pcm_read(ul_task->pcm_hfp_pcm_tx, buffer, size);
          if (ret != 0) {
             AM_LOGD("pcm_read fail need:%d, ret:%d", size, ret);
          }
        unsigned sample_size_in_bytes = pcm_format_to_bits(ul_task->config.format) >> 3;
        if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
            aml_audio_dump_audio_bitstreams("/data/audio/before_aec_16k_6ch_16bit.pcm", buffer, size);
        }
        ret = aec_process(ul_task->aec_handle, buffer, buffer_aec);
        if (ret == 0)
            adjust_channels(buffer, ul_task->config.channels, buffer_aec, ul_task->mic_channels, sample_size_in_bytes, size);
        if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
            aml_audio_dump_audio_bitstreams("/data/audio/after_aec_16k_4ch_16bit.pcm", buffer_aec, size_aec);
        }
        adjust_channels(buffer_aec, ul_task->mic_channels, buffer_out, pcm_config_hfp.channels, sample_size_in_bytes, size_aec);
        void *dec_data = (void *)buffer_out;
        ul_task->data_len = (int)size_out;

        //no need src for pdm && bt with the same fmt (16b && 1ch && 16k)
	  if (ul_task->data_len > 0) {
              ret = pcm_write(ul_task->pcm_hfp_sco_rx, (void *)dec_data, (unsigned int)ul_task->data_len);

             if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
                 aml_audio_dump_audio_bitstreams(AML_PARAM_AUDIO_HAL_SCO_RX_RD_SYSTEM, dec_data, ul_task->data_len);
             }
             if (ret < 0) {
                 ALOGE("%s write failed,pcm handle %p %s",
                       __func__, ul_task->pcm_hfp_sco_rx, pcm_get_error(ul_task->pcm_hfp_sco_rx));
	     }
	  }
    }

    if (ul_task->pcm_hfp_sco_rx) {
        ul_task->pcm_hfp_sco_rx = NULL; // pcm_close at stop_hfp
    }
    /*coverity[check_after_deref]*/
    if (ul_task->pcm_hfp_pcm_tx) {
        ul_task->pcm_hfp_pcm_tx = NULL; // pcm_close at stop_hfp
    }

    AM_LOGD("exit---");
    aml_audio_free(buffer);
    aml_audio_free(buffer_aec);
    aml_audio_free(buffer_out);

    return NULL;
}

static void* aml_hfp_dl_thread(void* data) {
    DL_HFP_T *dl_task = (DL_HFP_T *)data;
    struct aml_audio_device *dev = dl_task->dl_dev;

    void *buffer = NULL;
    int16_t *dec_data_1_t_2 = NULL;
    int32_t *hfp_out_32_buf = NULL;
    int32_t *hfp_tmp_buffer = NULL;
    int32_t *hfp_tmp_buffer_8ch = NULL;

    unsigned int size;
    size_t dec_data_1_t_2_buf_size;
    size_t hfp_out_32_buf_size;
    size_t hfp_out_frames;
    size_t hfp_tmp_buffer_8ch_size;

    dl_task->pcm_hfp_pcm_rx = hfpmod.hfp_pcm_rx;
    dl_task->pcm_hfp_sco_tx = hfpmod.hfp_sco_tx;

    size = pcm_frames_to_bytes(dl_task->pcm_hfp_sco_tx, pcm_get_buffer_size(dl_task->pcm_hfp_sco_tx) / 6);//32ms
    dec_data_1_t_2_buf_size = size * 2;
    buffer = aml_audio_calloc(1,size);
    hfp_out_32_buf = aml_audio_calloc(1,size);
    hfp_tmp_buffer_8ch = aml_audio_calloc(1,size * 8);
	dec_data_1_t_2 = aml_audio_calloc(1,size * 2);

    if ((!buffer) || (!hfp_out_32_buf) || (!hfp_tmp_buffer_8ch) || (!dec_data_1_t_2)) {
        AM_LOGE("Unable to allocate %u bytes", size);
        goto exit;
    }

    while (!dl_task->exit_run  && hfpmod.is_hfp_running) {
        //1 read from  bt :1ch 16b 16k
        int ret = pcm_read(dl_task->pcm_hfp_sco_tx, buffer, size);

        if (ret != 0) {
            AM_LOGD("pcm_read fail need:%d, ret:%d", size, ret);
        }

        if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
            aml_audio_dump_audio_bitstreams(AML_PARAM_AUDIO_HAL_SCO_TX_RD_SYSTEM, buffer, size);
        }
        //2 one T stereo
        upmix_to_stereo_i16_from_mono_i16(dec_data_1_t_2,buffer,size / sizeof(int16_t));

        if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
            aml_audio_dump_audio_bitstreams(AML_PARAM_AUDIO_HAL_SCO_TX_RD_STEREO_SYSTEM, dec_data_1_t_2, dec_data_1_t_2_buf_size);
        }

        void  *dec_data = (void *)dec_data_1_t_2;
        dl_task->data_len = (int)dec_data_1_t_2_buf_size;

        if (dl_task->data_len > 0) {
            if (pcm_config_hfp.rate != OUTPUT_DL_SRC_SAMPLERATE) {
                //3 resample
                ret = aml_audio_resample_process_wrapper(&dl_task->resample_handle, dec_data,
                      dl_task->data_len, pcm_config_hfp.rate, 2);//fix 2 ch,for the API not support 1 ch
                if (ret != 0) {
                    ALOGW("[%s:%d] resample fail, size:%d, data_sr:%d", __func__, __LINE__, dl_task->data_len, pcm_config_hfp.rate);
                } else {
                    dec_data = dl_task->resample_handle->resample_buffer;
                    dl_task->data_len = dl_task->resample_handle->resample_size;//for real_src_size

                    if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
                        aml_audio_dump_audio_bitstreams(AML_PARAM_AUDIO_HAL_SCO_TX_RD_SRC_SYSTEM, dec_data, dl_task->data_len);
                    }
                    //4 16b T 32b
                    int16_t *input16 = (int16_t *)dec_data;
                    hfp_out_32_buf_size = dl_task->data_len * 2;
                    hfp_out_32_buf = aml_audio_realloc(hfp_out_32_buf, hfp_out_32_buf_size);
                    for (int i = 0; i < dl_task->data_len / sizeof(int16_t); i++) {
                         hfp_out_32_buf[i] = ((int32_t)input16[i]) << 16;
                    }
                    if (aml_getprop_bool("vendor.media.audiohal.outdump") && hfp_out_32_buf) {
                        aml_audio_dump_audio_bitstreams(AML_PARAM_AUDIO_HAL_SCO_TX_RD_SRC_16_T_32_SYSTEM, hfp_out_32_buf, hfp_out_32_buf_size);
                    }
                    //5 2ch T 8ch
                    hfp_tmp_buffer_8ch = aml_audio_realloc(hfp_tmp_buffer_8ch, hfp_out_32_buf_size * 4);
                    hfp_tmp_buffer_8ch_size = hfp_out_32_buf_size * 4;
                    hfp_out_frames = hfp_out_32_buf_size / FRAMESIZE_32BIT_STEREO;
                    hfp_tmp_buffer = (int32_t *)hfp_out_32_buf;
                    if (hfp_tmp_buffer_8ch) {
                        for (int n = 0; n < hfp_out_frames; n++) {
                             hfp_tmp_buffer_8ch[8 * n] = hfp_tmp_buffer[2 * n];
                             hfp_tmp_buffer_8ch[8 * n + 1] = hfp_tmp_buffer[2 * n + 1];
                             hfp_tmp_buffer_8ch[8 * n + 2] = hfp_tmp_buffer[2 * n];
                             hfp_tmp_buffer_8ch[8 * n + 3] = hfp_tmp_buffer[2 * n + 1];
                             hfp_tmp_buffer_8ch[8 * n + 4] = hfp_tmp_buffer[2 * n];
                             hfp_tmp_buffer_8ch[8 * n + 5] = hfp_tmp_buffer[2 * n + 1];
                             hfp_tmp_buffer_8ch[8 * n + 6] = hfp_tmp_buffer[2 * n];
                             hfp_tmp_buffer_8ch[8 * n + 7] = hfp_tmp_buffer[2 * n + 1];
                         }
                         apply_volume(hfpmod.hfp_volume, hfp_tmp_buffer_8ch, sizeof(uint32_t), hfp_tmp_buffer_8ch_size);
                         if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
                             aml_audio_dump_audio_bitstreams(AML_PARAM_AUDIO_HAL_SCO_TX_RD_SRC_16_T_32_8_CH_ALL_SYSTEM, hfp_tmp_buffer_8ch, hfp_tmp_buffer_8ch_size);
                         }
                    }
                     if (hfpmod.is_hfp_running) {
                         ret = pcm_write(dl_task->pcm_hfp_pcm_rx, (void *)hfp_tmp_buffer_8ch, hfp_tmp_buffer_8ch_size);
                     } else
                          break;
                }
            }
            if (ret < 0) {
                ALOGE("%s write failed,pcm handle %p %s",
                          __func__, dl_task->pcm_hfp_pcm_rx, pcm_get_error(dl_task->pcm_hfp_pcm_rx));
            }
        }
    }

exit:
    AM_LOGD("exit---");
    if (dl_task->pcm_hfp_sco_tx) {
        dl_task->pcm_hfp_sco_tx  = NULL; // pcm_close at stop_hfp
    }
    if (dl_task->pcm_hfp_pcm_rx) {
        dl_task->pcm_hfp_pcm_rx = NULL;  // pcm_close at stop_hfp
    }

    if (buffer) {
        aml_audio_free(buffer);
        buffer = NULL;
    }

    if (dec_data_1_t_2) {
        aml_audio_free(dec_data_1_t_2);
        dec_data_1_t_2 = NULL;
    }

    if (hfp_out_32_buf) {
        aml_audio_free(hfp_out_32_buf);
        hfp_out_32_buf = NULL;
    }

    if (hfp_tmp_buffer_8ch) {
        aml_audio_free(hfp_tmp_buffer_8ch);
        hfp_tmp_buffer_8ch = NULL;
        hfp_tmp_buffer = NULL;
    }
    return NULL;
}

static int32_t start_hfp(struct aml_audio_device *adev,
                         struct str_parms *parms __unused)
{
    int32_t ret = 0;
    audio_format_t  format = AUDIO_FORMAT_PCM_16_BIT;
    int card = 0;
    int pcm_ul_rd_index = -1;
    int pcm_ul_wr_index = -1;
    int pcm_dl_rd_index = -1;
    int pcm_dl_wr_index = -1;

    hfpmod.is_hfp_running = true;
    if (adev->enable_hfp == true) {
        ALOGD("%s: HFP is already active!\n", __func__);
        return 0;
    }
    adev->enable_hfp = true;
    /* maybe for sync issue */
    while (NULL != hfpmod.hfp_pcm_rx_sub) {
             AM_LOGE("wait to PCM_STATE_SETUP");
             aml_audio_sleep(1000);
    }
    pcm_config_hfp.channels = 1;
    pcm_config_hfp.format = convert_audio_format_2_alsa_format(format);
    pcm_config_hfp.period_count = HFP_PLAYBACK_PERIOD_COUNT;
    pcm_config_hfp.period_size = HFP_PERIOD_SIZE;
    pcm_config_hfp.start_threshold = pcm_config_hfp.period_size * pcm_config_hfp.period_count / 2;
    pcm_config_hfp.stop_threshold = pcm_config_hfp.period_size * pcm_config_hfp.period_count;
    pcm_config_hfp.avail_min = 0;

    ALOGD("%s: PCM info (rx: %d tx: %d asm: rx:%d tx:%d)",
	       __func__, pcm_config_hfp.rate, pcm_config_hfp.format, pcm_config_hfp.start_threshold,pcm_config_hfp.stop_threshold);

    //select_devices(adev);//maybe for device special route

    card = alsa_device_get_card_index();
    adev->card = card;
    hfpmod.hfp_card = card;//maybe for refine to use, ex: move open pcm to respective thread

    pcm_ul_rd_index = HFP_UL_RD_DEVICE;//hfp_pcm_tx(pdmin)
    pcm_ul_wr_index = HFP_UL_WR_DEVICE;//hfp_sco_rx(tdma_out)
    pcm_dl_rd_index = HFP_DL_RD_DEVICE;//hfp_sco_tx(tdma_in)
    pcm_dl_wr_index = HFP_DL_WR_DEVICE;//hfp_pcm_rx(tdmb_out)

    //1 For PCM_OUT Open All
    ALOGD("%s: Opening PCM->SCO ul playback device card_id(%d) device_id(%d)",
          __func__, adev->card, pcm_ul_wr_index);
    hfpmod.hfp_sco_rx = pcm_open(adev->card,
                                  pcm_ul_wr_index,
                                  PCM_OUT, &pcm_config_hfp);
    if (hfpmod.hfp_sco_rx && !pcm_is_ready(hfpmod.hfp_sco_rx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(hfpmod.hfp_sco_rx));
        ret = -EIO;
        goto exit;
    }
    ALOGD("%s: hfpmod.hfp_sco_rx already active : %p!\n", __func__, hfpmod.hfp_sco_rx);

    ALOGD("%s: Opening PCM->SCO dl playback device card_id(%d) device_id(%d)",
          __func__, adev->card, pcm_dl_wr_index);

    if (NULL == hfpmod.hfp_pcm_rx) {
        //default reuse the pcm*p for speaker
        //need refine the pcm_config_hfp with 8 channel
        hfpmod.hfp_pcm_rx = pcm_open(adev->card,
                                     pcm_dl_wr_index,
                                     PCM_OUT, &pcm_config_hfp_hfp_rx);
        if (hfpmod.hfp_pcm_rx && !pcm_is_ready(hfpmod.hfp_pcm_rx)) {
            ALOGE("%s: %s", __func__, pcm_get_error(hfpmod.hfp_pcm_rx));
            ret = -EIO;
            goto exit;
        }
    }
    ALOGD("%s: hfpmod.hfp_pcm_rx already active : %p!\n", __func__, hfpmod.hfp_pcm_rx);

    //2 For PCM_IN Open All
    pcm_config_hfp.start_threshold = 1;//for capture
    ALOGD("%s: Opening PCM->SCO dl capture device card_id(%d) device_id(%d)",
          __func__, adev->card, pcm_dl_rd_index);
    hfpmod.hfp_sco_tx = pcm_open(adev->card,
                                  pcm_dl_rd_index,
                                  PCM_IN, &pcm_config_hfp);
    if (hfpmod.hfp_sco_tx && !pcm_is_ready(hfpmod.hfp_sco_tx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(hfpmod.hfp_sco_tx));
        ret = -EIO;
        goto exit;
    }
    ALOGD("%s: hfpmod.hfp_sco_tx already active : %p!\n", __func__, hfpmod.hfp_sco_tx);

    ALOGD("%s: Opening PCM->SCO ul capture device card_id(%d) device_id(%d)",
          __func__, adev->card, pcm_ul_rd_index);
    hfpmod.hfp_pcm_tx = pcm_open(adev->card,
                                   pcm_ul_rd_index,
                                   PCM_IN, &pcm_config_hfp_aec);
    if (hfpmod.hfp_pcm_tx && !pcm_is_ready(hfpmod.hfp_pcm_tx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(hfpmod.hfp_pcm_tx));
        ret = -EIO;
        goto exit;
    }
    ALOGD("%s: hfpmod.hfp_pcm_tx already active : %p!\n", __func__, hfpmod.hfp_pcm_tx);

    if (g_ul_task_hfp) {
        AM_LOGW("already create task_ul_hfp");
    } else {
        g_ul_task_hfp = (UL_HFP_T *)aml_audio_calloc(1, sizeof(UL_HFP_T));
        g_ul_task_hfp->exit_run = true;
        g_ul_task_hfp->ul_dev = adev;
    }

    R_CHECK_POINTER_LEGAL(-1, g_ul_task_hfp, "");
    if (g_ul_task_hfp->exit_run == false) {
        AM_LOGW("already g_ul_task_hfp running");
        return -1;
    }

    if (g_dl_task_hfp) {
        AM_LOGW("already create task_dl_hfp");
    } else {
        g_dl_task_hfp = (DL_HFP_T *)aml_audio_calloc(1, sizeof(DL_HFP_T));
        g_dl_task_hfp->exit_run = true;
        g_dl_task_hfp->dl_dev = adev;
    }

    R_CHECK_POINTER_LEGAL(-1, g_dl_task_hfp, "");
    if (g_dl_task_hfp->exit_run == false) {
        AM_LOGW("already g_dl_task_hfp running");
        return -1;
    }


    g_ul_task_hfp->exit_run = false;

    int mic_channels = 4;

    g_ul_task_hfp->aec_handle = aec_create(mic_channels, pcm_config_hfp_aec);
    g_ul_task_hfp->mic_channels = mic_channels;
    g_ul_task_hfp->config = pcm_config_hfp_aec;
    ret = pthread_create(&g_ul_task_hfp->thread_id, NULL, aml_hfp_ul_thread, g_ul_task_hfp);
    if (ret) {
        AM_LOGE("g_ul_task_hfp error creating thread: %s", strerror(ret));
        goto exit;
    }
    g_ul_task_hfp->thread_created = 1;

    g_dl_task_hfp->exit_run = false;
    ret = pthread_create(&g_dl_task_hfp->thread_id, NULL, aml_hfp_dl_thread, g_dl_task_hfp);
    if (ret) {
        AM_LOGE("g_dl_task_hfp error creating thread: %s", strerror(ret));
        goto exit;
    }
    g_dl_task_hfp->thread_created = 1;

    hfp_set_volume(adev, hfpmod.hfp_volume);//for volume Ctrl

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return ret;

exit:
    stop_hfp(adev);
    ALOGE("%s: Problem in HFP start: status(%d)", __func__, ret);
    return ret;
}

static int32_t stop_hfp(struct aml_audio_device *adev)
{
    int32_t i, ret = 0;

    ALOGI("%s: enter", __func__);
    if (g_dl_task_hfp && g_dl_task_hfp->thread_created) {
        g_dl_task_hfp->exit_run = true;
        pthread_join(g_dl_task_hfp->thread_id, NULL);
        g_dl_task_hfp->thread_id = 0;
        g_dl_task_hfp->thread_created = 0;
    }
    ALOGI("%s: release dl thread", __func__);

    if (g_ul_task_hfp && g_ul_task_hfp->thread_created) {
        g_ul_task_hfp->exit_run = true;
        pthread_join(g_ul_task_hfp->thread_id, NULL);
        g_ul_task_hfp->thread_id = 0;
        g_ul_task_hfp->thread_created = 0;
    }
    ALOGI("%s: release ul thread", __func__);

    if (hfpmod.hfp_sco_rx) {
        pcm_close(hfpmod.hfp_sco_rx);
        hfpmod.hfp_sco_rx = NULL;
    }
    if (hfpmod.hfp_sco_tx) {
        pcm_close(hfpmod.hfp_sco_tx);
        hfpmod.hfp_sco_tx = NULL;
    }
    if (hfpmod.hfp_pcm_rx) {
        pcm_close(hfpmod.hfp_pcm_rx);
        hfpmod.hfp_pcm_rx = NULL;
    }
    if (hfpmod.hfp_pcm_tx) {
        pcm_close(hfpmod.hfp_pcm_tx);
        hfpmod.hfp_pcm_tx = NULL;
    }
    hfpmod.is_hfp_running = false;
    adev->enable_hfp = false;

    g_ul_task_hfp->exit_run = 1;
    pthread_join(g_ul_task_hfp->thread_id, NULL);
    g_ul_task_hfp->thread_id = 0;
    g_dl_task_hfp->exit_run = 1;
    pthread_join(g_dl_task_hfp->thread_id, NULL);
    g_dl_task_hfp->thread_id = 0;
    aec_destroy(g_ul_task_hfp->aec_handle);
    ALOGD("%s: exit: status(%d)", __func__, ret);

    return ret;
}

void audio_extn_hfp_set_parameters(struct aml_audio_device *adev, struct str_parms *parms)
{
    int ret;
    int rate;
    int val;
    float vol;
    char value[AUDIO_PARAMETER_HFP_VALUE_MAX] = {0, };
    AM_LOGD("%s", __func__);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_HFP_ENABLE, value,
                            sizeof(value));
    if (ret >= 0) {
           if (!strncmp(value,"true",sizeof(value))) {
               if (!hfpmod.is_hfp_running)
                   start_hfp(adev,parms);
               else
                   ALOGW("%s: HFP is already active.", __func__);
           } else {
               if (hfpmod.is_hfp_running)
                   stop_hfp(adev);
               else
                   ALOGW("%s:  ignore STOP, HFC not active", __func__);
           }
    }
    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms,AUDIO_PARAMETER_HFP_SET_SAMPLING_RATE, value,
                            sizeof(value));
    if (ret >= 0) {
           rate = atoi(value);
           if ((rate == 8000) || (rate == 16000)) {
               pcm_config_hfp.rate = rate;
			   ALOGE(" rate. %d.",pcm_config_hfp.rate);
           } else
               ALOGE(" Unsupported rate..");
    }

    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_HFP_VOL_MIXER_CTL,
                          value, sizeof(value));
    if (ret >= 0) {
        ALOGD("%s:  mixer ctl name: %s", __func__, value);
        strcpy(hfpmod.hfp_vol_mixer_ctl, value);
        str_parms_del(parms, AUDIO_PARAMETER_HFP_VOL_MIXER_CTL);
    }

    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HFP_VOLUME,
                            value, sizeof(value));
    if (ret >= 0) {
        if (sscanf(value, "%f", &vol) != 1) {
            ALOGE("%s: error in retrieving hfp volume", __func__);
            goto exit;
        }
        ALOGD("%s:  set_hfp_volume usecase, Vol: [%f]", __func__, vol);
        hfp_set_volume(adev, vol);
    }

exit:
    ALOGV("%s Exit",__func__);
}
