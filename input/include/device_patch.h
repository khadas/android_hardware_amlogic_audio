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

#ifndef DEVICE_PATCH_H_
#define DEVICE_PATCH_H_

#include <sys/types.h>
#include <system/audio.h>
#include <pthread.h>

#include "../../audio_hal/audio_hw.h"
#include "aml_ringbuffer.h"
#include "component_picture_mode.h"
#include "aml_audio_resampler.h"
#include "aml_audio_heaacparser.h"
#ifdef ENABLE_DVB_PATCH
#include "dtv_patch_utils.h"
#include "dtv_patch_hal_avsync.h"
#include "dtv_patch_dtvsync.h"
#endif

typedef void (*dtv_avsync_process_cb)(struct audio_stream_out *stream, size_t bytes, audio_format_t output_format);

enum patch_route_e {
    /*source is device, sink is device*/
    PATCH_ROUTE_DEV_DEV = 0,
     /*source is mix, sink is device*/
    PATCH_ROUTE_MIX_DEV = 1,
     /*source is dev, sink is mix*/
    PATCH_ROUTE_DEV_MIX = 2,
     /*source is mix, sink is mix*/
    PATCH_ROUTE_MIX_MIX = 3,
};

enum patch_type_e
{
    PATCH_TYPE_TV = 1,
    PATCH_TYPE_DTV = 1 << 1,
    PATCH_TYPE_ALL = 1 << 2,
    PATCH_TYPE_INVAL = 1 << 3,
};

/**\brief Audio output mode*/
typedef enum
{
    AM_AOUT_OUTPUT_STEREO,     /**< Stereo output*/
    AM_AOUT_OUTPUT_DUAL_LEFT,  /**< Left audio output to dual channel*/
    AM_AOUT_OUTPUT_DUAL_RIGHT, /**< Right audio output to dual channel*/
    AM_AOUT_OUTPUT_SWAP,       /**< Swap left and right channel*/
    AM_AOUT_OUTPUT_JOINT_STEREO,
    AM_AOUT_OUTPUT_LRMIX       /**< mix left and right channel*/
} AM_AOUT_OutputMode_t;

/* all latency in unit 'ms' */
struct audio_patch_latency_detail
{
    unsigned int ringbuffer_latency;
    unsigned int user_tune_latency;
    unsigned int alsa_in_latency;
    unsigned int alsa_i2s_out_latency;
    unsigned int alsa_spdif_out_latency;
    unsigned int ms12_latency;
    unsigned int total_latency;
};

struct aml_audio_patch
{
    struct audio_hw_device *dev;
    ring_buffer_t aml_ringbuffer;
    ring_buffer_t tvin_ringbuffer;
    ring_buffer_t assoc_ringbuffer;
    pthread_t audio_input_threadID;
    pthread_t audio_cmd_process_threadID;
    pthread_t audio_output_threadID;
    pthread_t audio_parse_threadID;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    void *in_buf;
    size_t in_buf_size;
    size_t numDecodedSamples;
    size_t numOutputSamples;
    void *out_buf;
    size_t out_buf_size;
    void *out_tmpbuf;
    size_t out_tmpbuf_size;
    int tvin_buffer_inited;
    int assoc_buffer_inited;
    int cmd_process_thread_exit;
    int input_thread_exit;
    int output_thread_exit;
    void *audio_parse_para;
    audio_devices_t input_src;
    audio_format_t aformat;
    int sample_rate;
    int input_sample_rate;
    audio_channel_mask_t chanmask;
    audio_channel_mask_t in_chanmask;
    int in_sample_rate;
    audio_format_t in_format;
    bool need_reconfig_mediasync;
    audio_devices_t output_src;
    bool is_dtv_src;
    audio_channel_mask_t out_chanmask;
    int out_sample_rate;
    audio_format_t out_format;
    /*start play strategy*/
    int startplay_avsync_flag;
    unsigned long startplay_firstvpts;
    unsigned long startplay_first_checkinapts;
    unsigned long startplay_pcrpts;
    unsigned long startplay_apts_lookup;
    unsigned int startplay_vpts;

#if 0
    struct ring_buffer
    struct thread_read
    struct thread_write
    struct audio_mixer;
    void *output_process_buf;
    void *mixed_buf;
#endif

    /* for AVSYNC tuning */
    int vltcy;
    int altcy;
    int average_vltcy;
    int average_altcy;
    int avsync_sample_accumulated;
    int max_video_latency;
    int min_video_latency;
    bool need_do_avsync;
    bool input_signal_stable;
    bool is_avsync_start;
    bool skip_frames;
    int timeout_avsync_cnt;

    struct audio_patch_latency_detail audio_latency;
    /* end of AVSYNC tuning */
    /*for dtv play parameters */
    int dtv_aformat;
    int dtv_has_video;
    int dtv_decoder_state;
    int dtv_decoder_cmd;
    int dtv_first_apts_flag; /*first apts looked up flag*/
    unsigned char dtv_NchOriginal;
    unsigned char dtv_lfepresent;
    unsigned int dtv_first_apts;
    uint64_t  dtv_pcm_wrote;
    unsigned int dtv_pcm_readed;
    unsigned int dtv_decoder_ready;
    unsigned int input_thread_created;
    unsigned int output_thread_created;
    uint64_t decoder_offset;
    uint64_t input_skipped_bytes;
    unsigned int outlen_after_last_validpts;
    unsigned long last_valid_pts;
    unsigned int first_apts_lookup_over; /*cache audio data before start-play flag*/
    int dtv_sample_rate;
    int dtv_pcm_channel;
    bool dtv_replay_flag; // set for the first play
    unsigned int dtv_output_clock;
    unsigned int dtv_default_i2s_clock;
    unsigned int dtv_default_spdif_clock;
    unsigned int dtv_default_arc_clock;
    unsigned int spdif_format_set;
    int spdif_step_clk;
    int i2s_step_clk;
    int arc_step_clk;
    int dtv_audio_mode;
    int tsync_mode;
    int dtv_apts_lookup;
    int dtv_audio_tune;
    int pll_state;
    unsigned int last_checkin_apts;
    int64_t last_min_pts;
    int64_t last_max_pts;
    unsigned int last_apts;
    unsigned int last_pcrpts;
    unsigned int cur_outapts;
    unsigned int anchor_apts;
    unsigned int show_first_frame;
    dtv_avsync_process_cb avsync_callback;
    pthread_mutex_t dtv_output_mutex;
    pthread_mutex_t dtv_input_mutex;
    pthread_cond_t dtv_cmd_process_cond;
    pthread_mutex_t dtv_cmd_process_mutex;
    pthread_mutex_t assoc_mutex;
    pthread_mutex_t apts_cal_mutex;
    int need_drop_size;
    /*end dtv play*/
    struct resample_para dtv_resample;
    unsigned char *resample_outbuf;
    AM_AOUT_OutputMode_t mode;
    bool ac3_pcm_dropping;
    bool tysnc_tune_processing;
    int last_audio_delay;
    // add only for debug.
    int dtv_log_retry_cnt;
    unsigned int last_apts_record;
    unsigned int last_vpts_record;
    unsigned int last_pcrpts_record;
    struct timespec last_debug_record;
    bool pcm_inserting;
    int tsync_pcr_debug;
    int pre_latency;
    int video_valid_time; // Effective time of video
    bool video_invalid;   // Used to determine whether the video is valid
    bool ad_substream_checked_flag;
    int a_discontinue_threshold;
    int pid;
    int i2s_div_factor;
    struct timespec speed_time;
    struct timespec slow_time;
    int media_sync_id;
#ifdef ENABLE_DVB_PATCH
    struct audiohal_debug_para debug_para;
    struct avsync_para sync_para;
    struct mAudioEsDataInfo *mADEsData;
    void *demux_handle;
    void *demux_info;
    aml_dtvsync_t *dtvsync;
    int uio_fd;
    struct cmd_node *dtv_cmd_list;
    void *dtv_package_list;
    struct package *cur_package;
#endif
    bool skip_amadec_flag;
    int sync_type;
    /*add a new flag to check the patch is created from tuner framework*/
    bool cbs_patch;
    int adec_handle;
    void *ac3_parser_handle;
    void *ad_ac3_parser_handle;
    void *heaac_parser_handle;
    void *ad_heaac_parser_handle;
    struct heaac_parser_info main_heaac_info;
    struct heaac_parser_info ad_heaac_info;

    /* user setting picture mode */
    picture_mode_t pic_mode;
    bool IEC61937_format;
    bool mode_reconfig_flag;
    /* user setting picture mode end */
    int dtv_disable_tune_latency;
    unsigned char main_head[32];
    bool need_save_main_head;
    int main_head_read_size;
    int sync_offset;
    int read_size;
    struct timespec start_ts;
    int mdelay;
    bool start_mute;
};

void create_tvin_buffer(struct aml_audio_patch *patch);
void release_tvin_buffer(struct aml_audio_patch *patch);

void adev_audio_patches_dump(struct aml_audio_device *aml_dev, int fd);
#endif