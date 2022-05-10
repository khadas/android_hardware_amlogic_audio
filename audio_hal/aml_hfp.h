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

#ifndef _AUDIO_HFP_H_
#define _AUDIO_HFP_H_
extern void audio_extn_hfp_set_parameters(struct aml_audio_device *adev, struct str_parms *parms);
#define AUDIO_PARAMATER_HFP_VALUE_MAX         128
#include "audio_hw_utils.h"
#include "alsa_device_parser.h"
#include <tinyalsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <endian.h>
#include "aml_malloc_debug.h"
#include "aml_volume_utils.h"
#include <aml_android_utils.h>

#define AML_PARAM_AUDIO_HAL_SCO_RX_RD_SYSTEM "/data/audio/hfp_sco_rx_rd_from_pdm.pcm"
#define AML_PARAM_AUDIO_HAL_SCO_TX_RD_SYSTEM "/data/audio/hfp_sco_tx_rd_from_bt.pcm"
#define AML_PARAM_AUDIO_HAL_SCO_TX_RD_SRC_SYSTEM "/data/audio/hfp_sco_tx_rd_from_bt_after_src.pcm"
#define AML_PARAM_AUDIO_HAL_SCO_TX_RD_SRC_16_T_32_SYSTEM "/data/audio/hfp_sco_tx_rd_from_bt_after_src_16_t_32.pcm"
#define AML_PARAM_AUDIO_HAL_SCO_TX_RD_SRC_16_T_32_8_CH_SYSTEM "/data/audio/hfp_sco_tx_rd_from_bt_after_src_16_t_32_8ch.pcm"
#define AML_PARAM_AUDIO_HAL_SCO_TX_RD_STEREO_SYSTEM "/data/audio/hfp_sco_tx_rd_from_bt_t_stereo.pcm"
#define AML_PARAM_AUDIO_HAL_SCO_TX_RD_SRC_16_T_32_8_CH_ALL_SYSTEM "/data/audio/hfp_sco_tx_rd_from_bt_after_src_16_t_32_8ch_all.pcm"
#define AML_PARAM_AUDIO_HAL_SCO_TX_RD_SRC_16_8_CH_ALL_SYSTEM "/data/audio/hfp_sco_tx_rd_from_bt_16b_8ch.pcm"

#define AUDIO_PARAMETER_HFP_ENABLE            "hfp_enable"
#define AUDIO_PARAMETER_HFP_SET_SAMPLING_RATE "hfp_set_sampling_rate"
#define AUDIO_PARAMETER_KEY_HFP_VOLUME        "hfp_volume"
#define AUDIO_PARAMETER_HFP_VOL_MIXER_CTL     "hfp_vol_mixer_ctl"
#define AUDIO_PARAMATER_HFP_VALUE_MAX         128

#define AUDIO_PARAMETER_KEY_HFP_MIC_VOLUME "hfp_mic_volume"
#define PLAYBACK_VOLUME_MAX 0x2000
#define CAPTURE_VOLUME_DEFAULT                (15.0)
#define UNUSED(x) (void)(x)

#define PERIOD_SIZE                     512
#undef PLAYBACK_PERIOD_COUNT
#define PLAYBACK_PERIOD_COUNT 6

/* number of periods for capture */
#undef CAPTURE_PERIOD_COUNT
#define CAPTURE_PERIOD_COUNT 4

#define HFP_CARD 0
#define HFP_UL_RD_DEVICE 3
#define HFP_UL_WR_DEVICE 0
#define HFP_DL_RD_DEVICE 0
#define HFP_DL_WR_DEVICE 1

#define OUTPUT_UL_SRC_SAMPLERATE                          (16000)
#define OUTPUT_DL_SRC_SAMPLERATE                          (48000)
#define HFP_VOL 15.000000

static int32_t start_hfp(struct aml_audio_device *adev,
                               struct str_parms *parms);

static int32_t stop_hfp(struct aml_audio_device *adev);

struct hfp_module {
    struct pcm *hfp_sco_rx;
    struct pcm *hfp_sco_tx;
    struct pcm *hfp_pcm_rx;
    struct pcm *hfp_pcm_tx;
    float  hfp_volume;
    float  mic_volume;
    char   hfp_vol_mixer_ctl[AUDIO_PARAMATER_HFP_VALUE_MAX];
    bool   is_hfp_running;
    bool   mic_mute;
    unsigned int hfp_card;
};

typedef struct ul_task_hfp_t {
    bool exit_run;
    pthread_t thread_id;
    int thread_enable : 1;
    struct pcm *pcm_hfp_sco_rx;
    struct pcm *pcm_hfp_pcm_tx;
    int data_len;
    aml_audio_resample_t *resample_handle;
    struct aml_mixer_handle *mixer;
} UL_HFP_T;

static UL_HFP_T *g_ul_task_hfp = NULL;

typedef struct dl_task_hfp_t {
    bool exit_run;
    pthread_t thread_id;
    int thread_enable : 1;
    struct pcm *pcm_hfp_sco_tx;
    struct pcm *pcm_hfp_pcm_rx;
    int data_len;
    aml_audio_resample_t *resample_handle;
    struct aml_mixer_handle *mixer;
} DL_HFP_T;

extern struct hfp_module hfpmod;
extern struct pcm_config pcm_config_hfp;
extern struct pcm_config pcm_config_hfp_hfp_rx;

extern bool if_hfp_running(struct aml_stream_out *hfp_out, struct audio_stream_out *stream, size_t bytes);

#endif
