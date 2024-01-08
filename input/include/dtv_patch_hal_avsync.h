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

#ifndef _DTV_PATCH_HAL_AVSYNC_H_
#define _DTV_PATCH_HAL_AVSYNC_H_

struct aml_audio_patch;

/* decoder/tsync fs node */
#define TSYNC_PCRSCR               "/sys/class/tsync/pts_pcrscr"
#define TSYNC_EVENT                "/sys/class/tsync/event"
#define TSYNC_APTS                 "/sys/class/tsync/pts_audio"
#define TSYNC_VPTS                 "/sys/class/tsync/pts_video"
#define TSYNC_CHECKIN_APTS         "/sys/class/tsync/last_checkin_apts"
#define TSYNC_CHECKIN_VPTS         "/sys/class/tsync/last_checkin_vpts"
#define TSYNC_FIRSTCHECKIN_APTS    "/sys/class/tsync/checkin_firstapts"
#define TSYNC_FIRSTCHECKIN_VPTS    "/sys/class/tsync/checkin_firstvpts"
#define TSYNC_PCR_LANTCY           "/sys/class/tsync/pts_latency"
#define TSYNC_LAST_CHECKIN_APTS    "/sys/class/tsync/last_checkin_apts"
#define TSYNC_FIRST_VPTS           "/sys/class/tsync/firstvpts"
#define TSYNC_DEMUX_PCR            "/sys/class/tsync/demux_pcr"
#define DTV_DECODER_TSYNC_MODE     "/sys/class/tsync/mode"
#define DTV_DECODER_PTS_LOOKUP_PATH        "/sys/class/tsync/apts_lookup"
#define DTV_DECODER_CHECKIN_FIRSTAPTS_PATH "/sys/class/tsync/checkin_firstapts"
#define TSYNC_DEMUX_APTS           "/sys/class/stb/audio_pts"
#define TSYNC_DEMUX_VPTS           "/sys/class/stb/video_pts"
#define TSYNC_PCR_DEBUG            "/sys/class/tsync_pcr/tsync_pcr_debug"
#define TSYNC_APTS_DIFF            "/sys/class/tsync_pcr/tsync_pcr_apts_diff"
#define TSYNC_VPTS_ADJ             "/sys/class/tsync_pcr/tsync_vpts_adjust"
#define TSYNC_PCR_MODE             "/sys/class/tsync_pcr/tsync_pcr_mode"
#define TSYNC_DMXPCR_VALID         "/sys/class/tsync_pcr/tsync_demux_pcr_valid"
#define TSYNC_PCR_DISCONTINUE      "/sys/class/tsync_pcr/tsync_pcr_discontinue"
#define TSYNC_REF_LATENCY          "/sys/class/tsync_pcr/tsync_pcr_ref_latency"
#define TSYNC_AUDIO_MODE           "/sys/class/tsync_pcr/tsync_audio_mode"
#define TSYNC_FIRSTCHECKIN_AVSTATE "/sys/class/tsync_pcr/tsync_firstcheckin_avstate"
#define TSYNC_CHECKIN_AOFFSET      "/sys/class/tsync_pcr/tsync_checkin_aoffset"
#define TSYNC_VIDEO_STATE          "/sys/class/tsync_pcr/tsync_video_state"
#define TSYNC_AUDIO_STATE          "/sys/class/tsync_pcr/tsync_audio_state"
#define TSYNC_PCR_INITED_MODE      "/sys/class/tsync_pcr/tsync_pcr_inited_mode"
#define TSYNC_PCR_INITED           "/sys/class/tsync_pcr/tsync_pcr_inited_flag"
#define TSYNC_AUDIO_LEVEL          "/sys/class/tsync_pcr/tsync_audio_level"
#define AMSTREAM_AUDIO_PORT_RESET  "/sys/class/amstream/reset_audio_port"
#define VIDEO_FIRST_FRAME_SHOW     "/sys/module/amvideo/parameters/first_frame_toggled"
#define VIDEO_DISPLAY_FRAME_CNT    "/sys/module/amvideo/parameters/display_frame_count"
#define VIDEO_RECEIVE_FRAME_CNT    "/sys/module/amvideo/parameters/receive_frame_count"
#define VIDEO_SHOW_FIRST_FRAME     "/sys/class/video/show_first_frame_nosync"
#define VIDEO_FIRST_FRAME_SHOW_2   "/sys/module/aml_media/parameters/first_frame_toggled"
#define VIDEO_DISPLAY_FRAME_CNT_2  "/sys/module/aml_media/parameters/display_frame_count"
#define VIDEO_RECEIVE_FRAME_CNT_2  "/sys/module/aml_media/parameters/receive_frame_count"
/* end decoder/tsync fs node */

#define PTSSERVER_DEVICE "/dev/ptsserver"
#define PTSSERVER_IOC_MAGIC 'P'
#define PTSSERVER_IOC_CHECKOUT_APTS   _IOW(PTSSERVER_IOC_MAGIC, 0x12, int)
#define PTSSERVER_IOC_INSTANCE_STATIC_BINDER   _IOW(PTSSERVER_IOC_MAGIC, 0x13, int)
#define PTSSERVER_IOC_GET_LIST_SIZE   _IOW(PTSSERVER_IOC_MAGIC, 0x14, int)
#define PTSSERVER_IOC_INSTANCE_SET_ID   _IOW(PTSSERVER_IOC_MAGIC, 0x15, int)

/* property */
#define PROPERTY_LOCAL_PASSTHROUGH_LATENCY  "vendor.media.dtv.passthrough.latencyms"
#define PROPERTY_PRESET_AC3_PASSTHROUGH_LATENCY  "vendor.media.dtv.passthrough.ac3prelatencyms"
#define PROPERTY_AUDIO_ADJUST_PCR_MAX   "vendor.media.audio.adjust.pcr.max"
#define PROPERTY_UNDERRUN_MUTE_MINTIME     "vendor.media.audio.underrun.mute.mintime"
#define PROPERTY_UNDERRUN_MUTE_MAXTIME     "vendor.media.audio.underrun.mute.maxtime"
#define PROPERTY_UNDERRUN_MAX_TIME      "vendor.media.audio.underruncheck.max.time"
#define PROPERTY_AUDIO_TUNING_PCR_CLOCK_STEPS "vendor.media.audio.tuning.pcr.clocksteps"
#define PROPERTY_AUDIO_TUNING_CLOCK_FACTOR  "vendor.media.audio.tuning.clock.factor"
#define PROPERTY_AUDIO_DROP_THRESHOLD  "vendor.media.audio.drop.thresholdms"
#define PROPERTY_AUDIO_LEAST_CACHE  "vendor.media.audio.leastcachems"
#define PROPERTY_DEBUG_TIME_INTERVAL  "vendor.media.audio.debug.timeinterval"
#define PROPERTY_AUDIO_JUMPED_THRESHOLD_PROPERTY   "vendor.media.audio.dtv.jumped.threshold"
#define PROPERTY_AUDIO_RETUNE_THRESHOLD_PROPERTY   "vendor.media.audio.dtv.retune.threshold"
#define PROPERTY_AUDIO_MAX_CACHE_THRESHOLD         "vendor.media.audio.dtv.max.cache.threshold"
#define PROPERTY_AUDIO_UNDERRUN_MUTE_PROPERTY      "vendor.media.audio.hal.dtv.underrun.mute.enable"

#define PROPERTY_AUDIO_DISCONTINUE_THRESHOLD  "vendor.media.audio.discontinue_threshold"
#define PROPERTY_DTV_RESAMPLE_DISABLE         "vendor.media.audio.dtv.resample.disable"
#define PROPERTY_DTV_AUDIO_DROP_TIMEOUTMS     "vendor.media.audio.dtv.policy.drop.timeout"
#define PROPERTY_DTV_AUDIO_DROP_DISABLE       "vendor.media.audio.dtv.policy.drop.disable"
#define PROPERTY_DTV_AUDIO_HOLD_DISABLE       "vendor.media.audio.dtv.policy.hold.disable"
#define PROPERTY_DTV_FADED_OUT_MS             "vendor.media.audio.dtv.fadedout.ms"

#define DTV_PTS_CORRECTION_THRESHOLD (90000 * 30 / 1000)
#define AUDIO_PTS_DISCONTINUE_THRESHOLD (90000 * 5)
#define DECODER_PTS_MAX_LATENCY (500 * 90)
#define DECODER_PTS_MIN_LATENCY (200 * 90)
#define DEMUX_PCR_APTS_LATENCY (300 * 90)
#define DEMUX_PCR_VPTS_LATENCY (500 * 90)
#define DECODER_PTS_DEFAULT_LATENCY (200 * 90)

#define DEFAULT_ARC_DELAY_MS (100)
#define DEFAULT_SYSTEM_TIME (90000)
#define DEFAULT_AV_THRESHOLD (1000 * 3)

/* audio clock tuning parameter */
#define DEFAULT_DTV_OUTPUT_CLOCK    (1000*1000)
#define DEFAULT_DTV_ADJUST_CLOCK    (1000)
#define DEFAULT_DTV_MIN_OUT_CLOCK   (1000*1000-100*1000)
#define DEFAULT_DTV_MAX_OUT_CLOCK   (1000*1000+100*1000)
#define DEFAULT_I2S_OUTPUT_CLOCK    (256*48000)
#define DEFAULT_CLOCK_MUL    (4)
#define DEFAULT_SPDIF_PLL_DDP_CLOCK    (256*48000*2)
#define DEFAULT_SPDIF_ADJUST_TIMES    (4)
#define DEFAULT_STRATEGY_ADJUST_CLOCK    (100)
#define DEFAULT_TUNING_PCR_CLOCK_STEPS (256 * 64)
#define DEFAULT_TUNING_CLOCK_FACTOR (7)
#define DEFAULT_AUDIO_DROP_THRESHOLD_MS (60)
#define DEFAULT_AUDIO_LEAST_CACHE_MS (50)
#define AUDIO_PCR_LATENCY_MAX (3000)

#define DEFULT_DEBUG_TIME_INTERVAL (5000)
#define DTV_AUDIO_START_MUTE_MAX_HTRESHOLD    (3 * 1000) //ms
#define DTV_AUDIO_RETUNE_DEFAULT_THRESHOLD    (60)  //ms
#define DTV_AUDIO_DROP_TIMEOUT_THRESHOLD      (5000) //ms
#define DTV_AUDIO_DROP_DEFAULT_THRESHOLD      (200)  //ms
#define DEFAULT_DTV_ADJUST_CLOCK_THRESHOLD   (5) //percent

#define DTV_AUDIO_CACHE_LATENCY_THRESHOLD   (1500)  //ms
#define DTV_AUDIO_JUMPED_DEFAULT_THRESHOLD   (400)  //ms

/* dtv NONMS12 tuning part */
//input format
#define  DTV_AVSYNC_NONMS12_PCM_LATENCY                    (0)
#define  DTV_AVSYNC_NONMS12_DD_LATENCY                     (0)
#define  DTV_AVSYNC_NONMS12_DDP_LATENCY                    (0)
#define  DTV_AVSYNC_NONMS12_PCM_LATENCY_PROPERTY           "vendor.media.audio.hal.nonms12.dtv.pcm"
#define  DTV_AVSYNC_NONMS12_DD_LATENCY_PROPERTY            "vendor.media.audio.hal.nonms12.dtv.dd"
#define  DTV_AVSYNC_NONMS12_DDP_LATENCY_PROPERTY           "vendor.media.audio.hal.nonms12.dtv.ddp"
//port speaker
#define  DTV_AVSYNC_NONMS12_TV_SPEAKER_LATENCY                    (0)
#define  DTV_AVSYNC_NONMS12_TV_SPEAKER_LATENCY_PROPERTY           "vendor.media.audio.hal.nonms12.tv.dtv.speaker"
//port A2DP
#define  DTV_AVSYNC_NONMS12_TV_MIX_A2DP_LATENCY                   (120)
#define  DTV_AVSYNC_NONMS12_TV_MIX_A2DP_LATENCY_PROPERTY          "vendor.media.audio.hal.nonms12.tv.dtv.mix.a2dp"
//port arc
#define  DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_PCM_LATENCY              (0)
#define  DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_DD_LATENCY               (60)
#define  DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_DDP_LATENCY              (90)
#define  DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_PCM_LATENCY_PROPERTY     "vendor.media.audio.hal.nonms12.dtv.arc.pcm"
#define  DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_DD_LATENCY_PROPERTY      "vendor.media.audio.hal.nonms12.dtv.arc.dd"
#define  DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_DDP_LATENCY_PROPERTY     "vendor.media.audio.hal.nonms12.dtv.arc.ddp"
//passthrough arc
#define  DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_PT_DD_LATENCY            (0)
#define  DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_PT_DDP_LATENCY           (0)
#define  DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_PT_DD_LATENCY_PROPERTY   "vendor.media.audio.hal.nonms12.dtv.pt.arc.dd"
#define  DTV_AVSYNC_NONMS12_HDMI_ARC_OUT_PT_DDP_LATENCY_PROPERTY  "vendor.media.audio.hal.nonms12.dtv.pt.arc.ddp"
/* end dtv NONMS12 tuning part */

/* dtv MS12 tuning part */
//input format
#define  DTV_AVSYNC_MS12_PCM_LATENCY                    (0)
#define  DTV_AVSYNC_MS12_DD_LATENCY                     (60)
#define  DTV_AVSYNC_MS12_DDP_LATENCY                    (60)
#define  DTV_AVSYNC_MS12_AC4_LATENCY                    (60)
#define  DTV_AVSYNC_MS12_AAC_LATENCY                    (60)
#define  DTV_AVSYNC_MS12_PCM_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.dtv.pcm"
#define  DTV_AVSYNC_MS12_DD_LATENCY_PROPERTY          "vendor.media.audio.hal.ms12.dtv.dd"
#define  DTV_AVSYNC_MS12_DDP_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.dtv.ddp"
#define  DTV_AVSYNC_MS12_AC4_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.dtv.ac4"
#define  DTV_AVSYNC_MS12_AAC_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.dtv.aac"
//port speaker
#define  DTV_AVSYNC_MS12_TV_SPEAKER_LATENCY                  (0)
#define  DTV_AVSYNC_MS12_TV_SPEAKER_LATENCY_PROPERTY                  "vendor.media.audio.hal.ms12.tv.dtv.speaker"
//port A2DP
#define  DTV_AVSYNC_MS12_TV_MIX_A2DP_LATENCY                  (200)
#define  DTV_AVSYNC_MS12_TV_MIX_A2DP_LATENCY_PROPERTY                 "vendor.media.audio.hal.ms12.tv.dtv.mix.a2dp"
//port arc
#define  DTV_AVSYNC_MS12_HDMI_ARC_OUT_PCM_LATENCY             (0)
#define  DTV_AVSYNC_MS12_HDMI_ARC_OUT_DD_LATENCY             (100)
#define  DTV_AVSYNC_MS12_HDMI_ARC_OUT_DDP_LATENCY             (100)
#define  DTV_AVSYNC_MS12_HDMI_ARC_OUT_MAT_LATENCY             (30)
#define  DTV_AVSYNC_MS12_HDMI_ARC_OUT_PCM_LATENCY_PROPERTY            "vendor.media.audio.hal.ms12.dtv.arc.pcm"
#define  DTV_AVSYNC_MS12_HDMI_ARC_OUT_DD_LATENCY_PROPERTY             "vendor.media.audio.hal.ms12.dtv.arc.dd"
#define  DTV_AVSYNC_MS12_HDMI_ARC_OUT_DDP_LATENCY_PROPERTY             "vendor.media.audio.hal.ms12.dtv.arc.ddp"
#define  DTV_AVSYNC_MS12_HDMI_ARC_OUT_MAT_LATENCY_PROPERTY            "vendor.media.audio.hal.ms12.dtv.arc.mat"
//passthrough arc
#define  DTV_AVSYNC_MS12_HDMI_ARC_OUT_PT_DD_LATENCY            (0)
#define  DTV_AVSYNC_MS12_HDMI_ARC_OUT_PT_DDP_LATENCY           (0)
#define  DTV_AVSYNC_MS12_HDMI_ARC_OUT_PT_DD_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.dtv.pt.arc.dd"
#define  DTV_AVSYNC_MS12_HDMI_ARC_OUT_PT_DDP_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.dtv.pt.arc.ddp"
/* end dtv MS12 tuning part */

//channel define
#define DEFAULT_CHANNELS 2
#define DEFAULT_SAMPLERATE 48
#define DEFAULT_DATA_WIDTH 2
//#define PROCESS_TSYNC_DISCONTINUE 1
#define VALID_PTS32(_pts_) ((_pts_) != 0xffffffff)
//#define TIME_UNIT90K (90000)

enum {
    STRATEGY_AVOUT_NORMAL = 0,
    STRATEGY_A_ZERO_V_NOSHOW,
    STRATEGY_A_NORMAL_V_SHOW_QUICK,
    STRATEGY_A_NORMAL_V_SHOW_BLOCK,
    STRATEGY_A_NORMAL_V_NOSHOW,
    STRATEGY_A_MUTE_V_SHOW_BLOCK,
    STRATEGY_A_DROP_V_SHOW_BLOCK,
    STRATEGY_A_DROP_V_NOSHOW,
};

enum {
    AVSYNC_FREE,
    AVSYNC_START,
    AVSYNC_RESTART,
    AVSYNC_RUNNING,
};

enum {
    PTS_NORMAL = 0,
    PTS_FF_JUMPED,
    PTS_FB_JUMPED,
    PTS_DURATION,
};

enum {
    PCR_INIT_NONE = 0, //invalid or not initialized;
    PCR_INIT_DMXPCR = 1, //initialized by demux pcr;
    PCR_INIT_VPTS = 2, //initialized by video pts;
    PCR_INIT_APTS = 4, //initialized by audio pts;
};

enum {
    PCR_DISCONTINUE = 0x1, //pcr discontinued;
    VPTS_DISCONTINUE = 0x2, //vpts discontinued;
    APTS_DISCONTINUE = 0x10, //apts discontinued;
};

typedef struct checkoutptsoffset {
    uint64_t offset;
    uint64_t pts_90k;
    uint64_t pts_64;
} checkout_pts_offset;

struct avsync_para {
    int cur_pts_diff; // pcr-apts
    int last_pts_diff; // pcr-apts
    int pcr_adjust_max; //pcr adjust max value
    int checkin_underrun_flag; //audio checkin underrun
    int in_out_underrun_flag; //input/output both underrun
    unsigned long underrun_checkinpts; //the checkin apts when in underrun
    int underrun_mute_time_min; //not happen loop underrun mute time ms
    int underrun_mute_time_max; //happen loop underrun mute max time ms
    int underrun_max_time;  //max time of underrun to force clear
    struct timespec underrun_starttime; //input-output both underrun starttime
    struct timespec underrun_mute_starttime; //underrun mute start time
    struct timespec apts_discontinue_record;
    struct timespec audio_tune_record;
    struct timespec pcrpts_tune_record;
    struct timespec start_output_record;
    struct timespec first_apts_record;
    struct timespec main_loop_record;
    struct timespec outapts_jumped_record;
    struct timespec pcrpts_jumped_record;
    uint32_t last_validpts_duration;
    uint32_t last_synced_frame_pts;
    uint32_t out_synced_frame_count;
    uint32_t validpts_cnt_record;
    uint32_t avsync_duration;
    uint32_t audio_drop_sum;
    uint32_t last_lookup_apts;
    uint32_t cur_checkin_vpts;
    uint32_t cur_checkin_apts;
    uint32_t discontinue_apts;
    uint32_t cur_tsync_apts;
    uint32_t cur_demux_pcr;
    uint32_t cur_out_vpts;
    uint32_t cur_pcrpts;
    uint32_t last_pcrpts;
    int avsync_syncshow;
    int show_first_nosync;
    int out_apts_offset;
    int avsync_status;
    int avsync_retune;
    int avsync_retune_threshold;
    int default_acache_offset;
    int audio_pause_resumed;
    int pcr_init_mode;
    int dmxpcr_valid;
    uint32_t dmxpcr_latency;
    int tsync_latency;
    int max_apts_cache;
    int tuned_pcrpts;
    int out_apts_jumped;
    int dmx_apts_jumped;
    int pcrscr_jumped;
};

struct audiohal_debug_para {
    int debug_time_interval;
    unsigned int debug_last_checkin_apts;
    unsigned int debug_last_checkin_vpts;
    unsigned int debug_last_out_apts;
    unsigned int debug_last_out_vpts;
    unsigned int debug_last_demux_pcr;
    struct timespec debug_system_time;
};

extern int get_audio_discontinue(void);
extern int dtv_get_tsync_mode(void);
extern int get_dtv_pcr_sync_mode(void);
extern unsigned long decoder_apts_lookup(unsigned int offset);
extern int drop_dtv_pcm(struct audio_stream_out *stream, const void *buffer, size_t bytes);
extern void call_dtv_avsync_callback(struct audio_stream_out *stream,size_t bytes);

void dtv_adjust_i2s_output_clock(struct aml_audio_patch* patch, int direct, int step);
unsigned int dtv_get_i2s_output_clock(struct aml_audio_patch* patch);
void dtv_adjust_spdif_output_clock(struct aml_audio_patch* patch, int direct, int step, bool spdifb);
void dtv_avsync_param_init(struct audio_stream_out *stream);
bool dtv_avsync_audio_freerun(struct aml_audio_patch* patch);
bool dtv_firstapts_lookup_over(struct aml_audio_patch *patch, struct aml_audio_device *aml_dev, bool a_discontinue, int *apts_diff);
int dtv_avsync_get_frame_duration(struct audio_stream_out *stream, size_t bytes, audio_format_t output_format);
bool decoder_firstcheckin_avasync(struct aml_audio_patch* patch);
int dtv_set_audio_latency(int apts_diff,struct aml_audio_patch* patch);
bool dtv_avsync_lookup_process(struct aml_audio_patch *patch, struct aml_audio_device *aml_dev);
void dtv_avsync_pause_process(struct audio_stream_out *stream, int cmd);
void dtv_avsync_param_reset(struct audio_stream_out *stream);
void get_dtv_checkin_pts (struct audio_stream_out *stream, int64_t *in_frame_pts, int64_t out_frame_pts, int *out_frames);


#endif  /* _DTV_PATCH_HAL_AVSYNC_H_ */
