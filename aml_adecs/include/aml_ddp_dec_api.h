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

#ifndef _AML_DCV_DEC_API_H_
#define _AML_DCV_DEC_API_H_

#include <hardware/audio.h>
#include "aml_ringbuffer.h"
#include "aml_audio_types_def.h"
#include "aml_dec_api.h"

#define MAX_BUFF_LEN 36


/* *references:*/
/*  (1) page 8 of ImplManualDolbyDigitalPlusDecoder.pdf */
 /* (2) Table 5.8 and Table E2.5 of Digital Audio Compression (AC-3, E-AC-3) */
enum {
    AUDIO_CHANNEL_MONO_1 = 0x1u,
    AUDIO_CHANNEL_MONO_2 = 0x2u,
    AUDIO_CHANNEL_C = 0x4u,             //Center
    AUDIO_CHANNEL_L = 0x8u,             //Left
    AUDIO_CHANNEL_R = 0x10u,            //Right
    AUDIO_CHANNEL_S = 0x20u,            //Mono Surround
    AUDIO_CHANNEL_LS = 0x40u,           //Left surround
    AUDIO_CHANNEL_RS = 0x80u,           //Right Surround
    AUDIO_CHANNEL_LFE = 0x100u,         //Low-Frequency Effects
    AUDIO_CHANNEL_Lc = 0x200u,          //Left_center
    AUDIO_CHANNEL_Rc = 0x400u,          //Right_center
    AUDIO_CHANNEL_Lrs = 0x800u,         //Left Rear Surround
    AUDIO_CHANNEL_Rrs = 0x1000u,        //Right Rear Surround
    AUDIO_CHANNEL_Cs = 0x2000u,         //Center Surround
    AUDIO_CHANNEL_Ts = 0x4000u,         //Top Surround
    AUDIO_CHANNEL_Lsd = 0x8000u,        //Left Surround Direct
    AUDIO_CHANNEL_Rsd = 0x10000u,       //Right Surround Direct
    AUDIO_CHANNEL_Lw = 0x20000u,        //Left Wide
    AUDIO_CHANNEL_Rw = 0x40000u,        //Right Wide
    AUDIO_CHANNEL_Vhl = 0x80000u,       //Left Vertical Height
    AUDIO_CHANNEL_Vhr = 0x100000u,      //Right Vertical Height
    AUDIO_CHANNEL_Vhc = 0x200000u,      //Center Vertical Height
    AUDIO_CHANNEL_Lts = 0x400000u,      //Left Top Surround
    AUDIO_CHANNEL_Rts = 0x800000u,      //Right Top Surround
    AUDIO_CHANNEL_LFE2 = 0x1000000u,    //Secondary Low-Frequency Effects
};

typedef enum  {
    DDP_CONFIG_MIXER_LEVEL, //runtime param
    DDP_CONFIG_OUT_BITDEPTH, //static param
    DDP_CONFIG_OUT_CH, //static param
    DDP_CONFIG_AD_PCMSCALE, //runtime param
    DDP_CONFIG_MAIN_PCMSCALE,//runtime param
} ddp_config_type_t;

typedef enum  {
    DDP_DECODE_MODE_SINGLE = 1,
    DDP_DECODE_MODE_AD_DUAL = 2,
    DDP_DECODE_MODE_AD_SUBSTREAM = 3,
} ddp_decoding_mode_t;

typedef union ddp_config {
    int  mixer_level;
} ddp_config_t;

struct dolby_ddp_dec {
    aml_dec_t  aml_dec;
    unsigned char *inbuf;
    size_t dcv_pcm_writed;
    uint64_t dcv_decoded_samples;
    unsigned int dcv_decoded_errcount;
    aml_dec_stream_info_t stream_info;
    unsigned long total_raw_size;
    unsigned long total_time; //
    unsigned int bit_rate;
    char sysfs_buf[MAX_BUFF_LEN];
    int status;
    int inbuf_size;
    int remain_size;
    int outlen_pcm;
    int outlen_raw;
    int nIsEc3;
    aml_dec_control_type_t digital_raw;
    int decoding_mode;
    int  mixer_level;
    bool is_iec61937;
    int curFrmSize;
    int (*get_parameters)(void *, int *, int *, int *, int *, int *, int *);
    int (*decoder_process)(unsigned char*, int, unsigned char *, int *, char *, int *, int, struct pcm_info *);
    struct pcm_info pcm_out_info;
    //struct resample_para aml_resample;
    //unsigned char *resample_outbuf;
    //ring_buffer_t output_ring_buf;
    void *spdifout_handle;
    int ad_substream_supported;
    int mainvol_level;
    int advol_level;
    int is_dolby_atmos;
    int channel_mask_all;
    int channel_mask_independent_frame; //channel_mask of independent frame
    int channel_mask_dependent_frame; //channel_mask of dependent frame
    int ChannelNum;
    int Frame_Count;
    int Sample_Rate;
    int Same_ChNum_Count;
    int Same_SampleRate_Count;

};

int dcv_decoder_init_patch(aml_dec_t ** ppaml_dec, aml_dec_config_t * dec_config);
int dcv_decoder_release_patch(aml_dec_t * aml_dec);
int dcv_decoder_process_patch(aml_dec_t * aml_dec, unsigned char*buffer, int bytes);
int dcv_decoder_get_framesize(unsigned char*buffer, int bytes, int* p_head_offset);
int is_ad_substream_supported(unsigned char *buffer,int bytes);
int dcv_decoder_config(aml_dec_t * aml_dec, aml_dec_config_type_t config_type, aml_dec_config_t * dec_config);
int parse_report_info_samplerate_channelnum (unsigned char *read_pointer, struct dolby_ddp_dec *ddp_dec, int mFrame_size);

int IndependentFrame_Acmod_Lfeon_to_ChannelMask(short Acmod, short Lfeon);
int DependentFrame_Chanmap_to_ChannelMask(short Chanmap);


extern aml_dec_func_t aml_dcv_func;

#endif
