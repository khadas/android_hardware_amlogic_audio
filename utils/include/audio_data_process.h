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


#ifndef _AUDIO_DATA_PROCESS_H_
#define _AUDIO_DATA_PROCESS_H_

#include <system/audio.h>

struct audioCfg {
    int card;
    int device;
    int is_tv;
    uint32_t sampleRate;
    uint32_t channelCnt;
    audio_channel_mask_t channelMask;
    audio_format_t format;
    uint32_t frame_size;
};


typedef struct _aml_pcm_mixing_st {
    struct audioCfg cfg;
    void *mixed_buf;
    size_t mixed_buf_size;
	int mixed_buf_is_static;
    int mixed_frame_size;
    int mixed_frames;

    // allocate memory when needed
    void *channel_buf;
    size_t channel_buf_size;
    void *format_buf;
    size_t format_buf_size;
} aml_pcm_mixing_st;


#define MAX_INPUT_CHANNELS_SUPPORTED 26
typedef struct _aml_pcm_downmix_st {
    float mMatrix[MAX_INPUT_CHANNELS_SUPPORTED][2];
    audio_channel_mask_t mInputChannelMask;
    size_t mLastValidChannelIndexPlusOne;
    size_t mInputChannelCount;

    // allocate memory when needed
    void *output_buf;
    size_t output_buf_size;
}aml_pcm_downmix_st;



int processing_and_convert(void *data_mixed,
        void *data_sys, size_t frames, struct audioCfg inCfg, struct audioCfg mixerCfg);
int do_mixing_2ch(void *data_mixed,
        void *data_in, size_t frames,
        audio_format_t in_format, audio_format_t out_format);
int extend_channel_2_8(void *data_out, void *data_in,
        size_t frames, int ch_cnt_out, int ch_cnt_in);
int extend_channel_5_8(void *data_out, void *data_in,
        size_t frames, int ch_cnt_out, int ch_cnt_in);

void channel_layout_swap_center_lfe(void * data, int size, int channels);

int init_aml_pcm_mixer(aml_pcm_mixing_st *p_mixer, struct audioCfg *p_mixer_cfg, int mixed_frames);
void deinit_aml_pcm_mixer(aml_pcm_mixing_st *p_mixer);
void set_pcm_mixing_base(aml_pcm_mixing_st *p_mixer, struct audioCfg *p_data_cfg, void *p_in_data, size_t data_bytes);
int do_mixing_multi_ch(aml_pcm_mixing_st *p_mixer, void *data_in, size_t in_frames, struct audioCfg *p_in_cfg);

void init_aml_pcm_downmix(aml_pcm_downmix_st *p_downmix);
void deinit_aml_pcm_downmix(aml_pcm_downmix_st *p_downmix);
int do_downmix_to_2ch(aml_pcm_downmix_st *p_downmix, void *data_in, size_t in_frames, struct audioCfg *p_in_cfg);

#endif
