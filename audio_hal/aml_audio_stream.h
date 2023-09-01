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

#ifndef _AML_AUDIO_STREAM_H_
#define _AML_AUDIO_STREAM_H_

#include <system/audio.h>
#include <pthread.h>
#include "aml_ringbuffer.h"
#include "audio_hw.h"
#include "audio_hw_profile.h"
#include "aml_audio_heaacparser.h"
#include "aml_dump_debug.h"

#ifdef ENABLE_DVB_PATCH
#include "dtv_patch_utils.h"
#include "dtv_patch_hal_avsync.h"
#include "dtv_patch_dtvsync.h"
#endif
#define AUDIO_FADEOUT_TV_DURATION_US 100 * 1000
#define MS12_AUDIO_FADEOUT_TV_DURATION_US 60 * 1000
#define MS12_AUDIO_FADEIN_TV_DURATION_US  200 * 1000
#define AUDIO_FADEOUT_STB_DURATION_US 40 * 1000

enum {
    DO_FADE_AT_HAL,
    DO_FADE_AT_ALSA,
    DO_FADE_AT_MAX
};

#define RAW_USECASE_MASK ((1<<STREAM_RAW_DIRECT) | (1<<STREAM_RAW_HWSYNC) | (1<<STREAM_RAW_PATCH))
/*
 * 1.AUDIO_FORMAT_PCM_16_BIT is suitable for Speaker
 * 2.AUDIO_FORMAT_AC3 / AUDIO_FORMAT_E_AC3 / AUDIO_FORMAT_E_AC3_JOC is suitable for Sink device(up to DDP)
 * 3.AUDIO_FORMAT_MAT(1.0-truehd/2.0-pcm/2.1-pcm-atmos) is suitable for Sink device(up to MAT&depvalue=1/3)
 * 4.AUDIO_FORMAT_DTS / AUDIO_FORMAT_DTS_HD is suitable for Sink device(up to dts/dts-hd)
 */
#define IS_EXTERNAL_DECODER_SUPPORT_FORMAT(format) \
    ((format == AUDIO_FORMAT_PCM_16_BIT) ||\
        (format == AUDIO_FORMAT_AC3) ||\
        ((format & AUDIO_FORMAT_E_AC3) == AUDIO_FORMAT_E_AC3) ||\
        ((format & AUDIO_FORMAT_MAT) == AUDIO_FORMAT_MAT) ||\
        (format == AUDIO_FORMAT_DTS) ||\
        (format == AUDIO_FORMAT_DTS_HD))

#define IS_DIGITAL_IN_HW(device) ((device) == AUDIO_DEVICE_IN_HDMI ||\
                             (device) == AUDIO_DEVICE_IN_HDMI_ARC ||\
                             (device) == AUDIO_DEVICE_IN_SPDIF)

#define DATA_PCM                         (0)
#define DATA_NON_PCM                     (1)

/*temp code, we will remove it later*/
#if ANDROID_PLATFORM_SDK_VERSION < 31
/*S/T already has such enum, R doesn't have it*/
typedef enum {
    AUDIO_FORMAT_MPEGH = 0x2C000000u,
    AUDIO_FORMAT_MPEGH_SUB_BL_L3 = 0x13u,
    AUDIO_FORMAT_MPEGH_SUB_BL_L4 = 0x14u,
    AUDIO_FORMAT_MPEGH_SUB_LC_L3 = 0x23u,
    AUDIO_FORMAT_MPEGH_SUB_LC_L4 = 0x24u,
    AUDIO_FORMAT_MPEGH_BL_L3 = 0x2C000013u,
    AUDIO_FORMAT_MPEGH_BL_L4 = 0x2C000014u,
    AUDIO_FORMAT_MPEGH_LC_L3 = 0x2C000023u,
    AUDIO_FORMAT_MPEGH_LC_L4 = 0x2C000024u,
} audio_format_Ext_t;
#endif


typedef uint32_t usecase_mask_t;

/* sync with tinymix before TXL */
enum input_source {
    SRC_NA = -1,
    LINEIN  = 0,
    ATV     = 1,
    HDMIIN  = 2,
    SPDIFIN = 3,
    ARCIN   = 4,
};

/* sync with tinymix after auge */
enum auge_input_source {
    TDMIN_A = 0,
    TDMIN_B = 1,
    TDMIN_C = 2,
    SPDIFIN_AUGE = 3,
    PDMIN = 4,
    FRATV = 5,
    TDMIN_LB    = 6,
    LOOPBACK_A  = 7,
    FRHDMIRX    = 8,
    LOOPBACK_B  = 9,
    SPDIFIN_LB  = 10,
    EARCRX_DMAC = 11,
    RESERVED_0  = 12,
    RESERVED_1  = 13,
    RESERVED_2  = 14,
    VAD     = 15,
};

/*
 *@brief get this value by adev_set_parameters(), command is "digital_audio_format"
 */
enum digital_format {
    PCM = 0,
    DD = 4,
    AUTO = 5,
    BYPASS = 6
};

enum stream_write_func {
    OUT_WRITE_NEW = 0,
    MIXER_AUX_BUFFER_WRITE_SM = 1,
    MIXER_MAIN_BUFFER_WRITE_SM = 2,
    MIXER_MMAP_BUFFER_WRITE_SM = 3,
    MIXER_AUX_BUFFER_WRITE = 4,
    MIXER_MAIN_BUFFER_WRITE = 5,
    MIXER_APP_BUFFER_WRITE = 6,
    PROCESS_BUFFER_WRITE = 7,

    MIXER_WRITE_FUNC_MAX
};

/**\brief Audio output mode*/
typedef enum {
    AM_AOUT_OUTPUT_STEREO,     /**< Stereo output*/
    AM_AOUT_OUTPUT_DUAL_LEFT,  /**< Left audio output to dual channel*/
    AM_AOUT_OUTPUT_DUAL_RIGHT, /**< Right audio output to dual channel*/
    AM_AOUT_OUTPUT_SWAP,        /**< Swap left and right channel*/
    AM_AOUT_OUTPUT_LRMIX,      /**< mix left and right channel*/
    AM_AOUT_OUTPUT_JOINT_STEREO, /**< Joint stereo output*/
} AM_AOUT_OutputMode_t;

enum tunerhal_audio_streamtype    {
    TUNERHAL_UNDEFINED,
    /*
     * Uncompressed Audio
     */
    TUNERHAL_PCM,
    /*
     * MPEG Audio Layer III versions
     */
    TUNERHAL_MP3,
    /*
     * ISO/IEC 11172 Audio
     */
    TUNERHAL_MPEG1,
    /*
     * ISO/IEC 13818-3
     */
    TUNERHAL_MPEG2,
    /*
     * ISO/IEC 23008-3 (MPEG-H Part 3)
     */
    TUNERHAL_MPEGH,
    /*
     * ISO/IEC 14496-3
     */
    TUNERHAL_AAC,
    /*
     * Dolby Digital
     */
    TUNERHAL_AC3,
    /*
     * Dolby Digital Plus
     */
    TUNERHAL_EAC3,
    /*
     * Dolby AC-4
     */
    TUNERHAL_AC4,
    /*
     * Basic DTS
     */
    TUNERHAL_DTS,
    /*
     * High Resolution DTS
     */
    TUNERHAL_DTS_HD,
    /*
     * Windows Media Audio
     */
    TUNERHAL_WMA,
    /*
     * Opus Interactive Audio Codec
     */
    TUNERHAL_OPUS,
    /*
     * VORBIS Interactive Audio Codec
     */
    TUNERHAL_VORBIS,
    /*
     * SJ/T 11368-2006
     */
    TUNERHAL_DRA,
	 /*
     * AAC with ADTS (Audio Data Transport Format).
     */
    TUNERHAL_AAC_ADTS,

    /*
     * AAC with ADTS with LATM (Low-overhead MPEG-4 Audio Transport Multiplex).
     */
    TUNERHAL_AAC_LATM,

    /*
     * High-Efficiency AAC (HE-AAC) with ADTS (Audio Data Transport Format).
     */
    TUNERHAL_AAC_HE_ADTS,

    /*
     * High-Efficiency AAC (HE-AAC) with LATM (Low-overhead MPEG-4 Audio Transport Multiplex).
     */
    TUNERHAL_AAC_HE_LATM
};

enum encoding_format {
    ENCODING_INVALID      = 0,
    ENCODING_DEFAULT      = 1,
    ENCODING_PCM_16BIT    = 2,
    ENCODING_PCM_8BIT     = 3,
    ENCODING_PCM_FLOAT    = 4,
    ENCODING_AC3          = 5,
    ENCODING_E_AC3        = 6,
    ENCODING_DTS          = 7,
    ENCODING_DTS_HD       = 8,
    ENCODING_MP3          = 9,
    ENCODING_AAC_LC       = 10,
    ENCODING_AAC_HE_V1    = 11,
    ENCODING_AAC_HE_V2    = 12,
    ENCODING_IEC61937     = 13,
    ENCODING_DOLBY_TRUEHD = 14,
    ENCODING_AAC_ELD      = 15,
    ENCODING_AAC_XHE      = 16,
    ENCODING_AC4          = 17,
    ENCODING_E_AC3_JOC    = 18,
    ENCODING_DOLBY_MAT    = 19,
    ENCODING_OPUS         = 20
};

enum {
    ATTEND_TYPE_NONE = 0,
    ATTEND_TYPE_ARC,
    ATTEND_TYPE_EARC
};

static inline bool is_main_write_usecase(stream_usecase_t usecase)
{
    return usecase > 0;
}

static inline bool is_digital_raw_format(audio_format_t format)
{
    switch (format) {
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3:
    case AUDIO_FORMAT_E_AC3_JOC:
    case AUDIO_FORMAT_AC4:
    case AUDIO_FORMAT_MAT:
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_HD:
    case AUDIO_FORMAT_DOLBY_TRUEHD:
    case AUDIO_FORMAT_IEC61937:
    case AUDIO_FORMAT_MP3:
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_HE_AAC_V1:
    case AUDIO_FORMAT_HE_AAC_V2:
    case AUDIO_FORMAT_AAC_HE_V1:
    case AUDIO_FORMAT_AAC_HE_V2:
    case AUDIO_FORMAT_AAC_LATM:
        return true;
    default:
        return false;
    }
}

static inline bool is_dolby_format(audio_format_t format) {
    switch (format) {
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3:
    case AUDIO_FORMAT_E_AC3_JOC:
    case AUDIO_FORMAT_AC4:
    case AUDIO_FORMAT_MAT:
    case AUDIO_FORMAT_DOLBY_TRUEHD:
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_HE_AAC_V1:
    case AUDIO_FORMAT_HE_AAC_V2:
        return true;
    default:
        return false;
    }
}

inline bool is_dts_format(audio_format_t format) {
    switch (format) {
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_HD:
    ///< audio_format_t does not include dts_express. So we get the format(dts_express especially) from the decoder.
    // case AUDIO_FORMAT_DTS_EXPRESS:
        return true;
    default:
        return false;
    }
}

static inline bool is_mpegh_format(audio_format_t format) {
    switch ((uint32_t)format) {
    case AUDIO_FORMAT_MPEGH:
    case AUDIO_FORMAT_MPEGH_BL_L3:
    case AUDIO_FORMAT_MPEGH_BL_L4:
    case AUDIO_FORMAT_MPEGH_LC_L3:
    case AUDIO_FORMAT_MPEGH_LC_L4:
        return true;
    default:
        return false;
    }
}

inline bool is_aac_format(audio_format_t format) {
    switch (format) {
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_HE_AAC_V1:
    case AUDIO_FORMAT_HE_AAC_V2:
    case AUDIO_FORMAT_AAC_HE_V1:
    case AUDIO_FORMAT_AAC_HE_V2:
    case AUDIO_FORMAT_AAC_LATM:
        return true;
    default:
        return false;
    }
}

static inline bool is_iec61937_format(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    return (aml_out->hal_format == AUDIO_FORMAT_IEC61937);
}

static inline stream_usecase_t attr_to_usecase(uint32_t devices __unused,
        audio_format_t format, uint32_t flags)
{
    // hwsync case
    if ((flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) && (format != AUDIO_FORMAT_IEC61937)
        && !(flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) {
        if (audio_is_linear_pcm(format)) {
            return STREAM_PCM_HWSYNC;
        } else if (is_digital_raw_format(format)) {
            return STREAM_RAW_HWSYNC;
        } else {
            return STREAM_USECASE_MAX;
        }
    }

    // non hwsync cases
    if (/*devices == AUDIO_DEVICE_OUT_HDMI ||*/
        is_digital_raw_format(format)) {
        return STREAM_RAW_DIRECT;
    } else if ((flags & AUDIO_OUTPUT_FLAG_DIRECT) && audio_is_linear_pcm(format)) {
        if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
            // AAudio case
            return STREAM_PCM_MMAP;
        } else {
            //multi-channel LPCM or hi-res LPCM
            return STREAM_PCM_DIRECT;
        }
    } else {
        return STREAM_PCM_NORMAL;
    }
}
static inline stream_usecase_t convert_usecase_mask_to_stream_usecase(usecase_mask_t mask)
{
    int i = 0;
    for (i = 0; i < STREAM_USECASE_MAX; i++) {
        if ((1 << i) & mask) {
            break;
        }
    }
    ALOGI("%s mask %#x i %d", __func__, mask, i);
    if (i >= STREAM_USECASE_MAX) {
        return STREAM_USECASE_MAX;
    } else {
        return (stream_usecase_t)i;
    }
}

static inline alsa_device_t usecase_to_device(stream_usecase_t usecase)
{
    switch (usecase) {
    case STREAM_PCM_NORMAL:
    case STREAM_PCM_DIRECT:
    case STREAM_PCM_HWSYNC:
    case STREAM_PCM_PATCH:
        return I2S_DEVICE;
    case STREAM_RAW_PATCH:
    case STREAM_RAW_DIRECT:
    case STREAM_RAW_HWSYNC:
        return DIGITAL_DEVICE;
    default:
        return I2S_DEVICE;
    }
}

static inline bool is_hdmi_out(audio_devices_t cur_out_devices) {
    return (cur_out_devices & AUDIO_DEVICE_OUT_HDMI_ARC) || (cur_out_devices & AUDIO_DEVICE_OUT_HDMI);
}

typedef void (*dtv_avsync_process_cb)(struct audio_stream_out *stream, size_t bytes, audio_format_t output_format);


/* all latency in unit 'ms' */
struct audio_patch_latency_detail {
    unsigned int ringbuffer_latency;
    unsigned int user_tune_latency;
    unsigned int alsa_in_latency;
    unsigned int alsa_i2s_out_latency;
    unsigned int alsa_spdif_out_latency;
    unsigned int ms12_latency;
    unsigned int total_latency;
};

struct aml_audio_patch {
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
    audio_format_t  aformat;
    int  sample_rate;
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
    unsigned int dtv_pcm_wrote;
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
    bool dtv_replay_flag;  //set for the first play
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
    int64_t last_checkin_apts;
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
    pthread_cond_t  dtv_cmd_process_cond;
    pthread_mutex_t dtv_cmd_process_mutex;
    pthread_mutex_t assoc_mutex;
    pthread_mutex_t apts_cal_mutex;
    int need_drop_size;
    /*end dtv play*/
    struct resample_para dtv_resample;
    unsigned char *resample_outbuf;
    AM_AOUT_OutputMode_t   mode;
    bool ac3_pcm_dropping;
	bool tysnc_tune_processing;
    int last_audio_delay;
    //add only for debug.
    int dtv_log_retry_cnt;
    unsigned int last_apts_record;
    unsigned int last_vpts_record;
    unsigned int last_pcrpts_record;
    struct timespec last_debug_record;
    bool pcm_inserting;
    int tsync_pcr_debug;
    int pre_latency;
    int  video_valid_time;//Effective time of video
    bool video_invalid; //Used to determine whether the video is valid
    bool ad_substream_checked_flag;
    int a_discontinue_threshold;
    int pid;
    int i2s_div_factor;
    struct timespec speed_time;
    struct timespec slow_time;
    int media_sync_id;
#ifdef ENABLE_DVB_PATCH
    struct audiohal_debug_para debug_para;
    struct avsync_para  sync_para;
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
    void * ac3_parser_handle;
    void * ad_ac3_parser_handle;
	void * heaac_parser_handle;
    void * ad_heaac_parser_handle;
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

struct audio_stream_out;
struct audio_stream_in;

stream_usecase_t convert_usecase_mask_to_stream_usecase(usecase_mask_t mask);

static inline bool need_hw_mix(usecase_mask_t masks)
{
    return (masks > 1);
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
void get_sink_format(struct audio_stream_out *stream);



//for DTV
bool is_dual_output_stream(struct audio_stream_out *stream);

/* dumpsys media.audio_flinger interfaces */
const char *audio_port_role_to_str(audio_port_role_t role);
const char *audio_port_type_to_str(audio_port_type_t type);
void aml_stream_out_info_print(struct aml_stream_out *aml_out, uint64_t *frames, struct timespec *timestamp);
void aml_stream_out_dump(struct aml_stream_out *aml_out, int fd);
void aml_audio_port_config_dump(struct audio_port_config *port_config, int fd);
void aml_audio_patch_dump(struct audio_patch *patch, int fd);
void aml_audio_patches_dump(struct aml_audio_device* aml_dev, int fd);
int aml_dev_dump_latency(struct aml_audio_device *aml_dev, int fd);
void audio_patch_dump(struct aml_audio_device* aml_dev, int fd);
void aml_alsa_device_status_dump(struct aml_audio_device* aml_dev, int fd);
bool is_use_spdifb(struct aml_stream_out *out);
bool is_dolby_ms12_support_compression_format(audio_format_t format);
bool is_dolby_ddp_support_compression_format(audio_format_t format);
bool is_direct_stream_and_pcm_format(struct aml_stream_out *out);
bool is_mmap_stream_and_pcm_format(struct aml_stream_out *out);
void get_audio_indicator(struct aml_audio_device *dev, char *temp_buf);
void update_audio_format(struct aml_audio_device *adev, audio_format_t format);
int audio_route_set_hdmi_arc_mute(struct aml_mixer_handle *mixer_handle, int enable);
int audio_route_set_spdif_mute(struct aml_mixer_handle *mixer_handle, int enable);
void audio_route_set_speaker_mute(struct aml_audio_device* aml_dev, int enable);
void audio_route_set_speaker_mute_l(struct aml_audio_device* aml_dev, int enable);



/*
 *@brief update the sink format after HDMI/HDMI-ARC hot plugged
 * return zero if success.
 */
int update_sink_format_after_hotplug(struct aml_audio_device *adev);

void create_tvin_buffer(struct aml_audio_patch *patch);
void release_tvin_buffer(struct aml_audio_patch *patch);
uint32_t tv_in_write(struct audio_stream_out *stream, const void* buffer, size_t bytes);
uint32_t tv_in_read(struct audio_stream_in *stream, void* buffer, size_t bytes);
void tv_do_ease_out(struct aml_audio_device *adev);
void tv_do_ease_in(struct audio_stream_out *stream, void *write_buf, size_t write_bytes);

const char *write_func_to_str(enum stream_write_func func);
int aml_audio_earctx_get_type(struct aml_audio_device *adev);
int aml_audio_earc_get_latency(struct aml_audio_device *adev);
int set_device_control(struct audio_hw_device *dev, struct str_parms *parms);

#endif /* _AML_AUDIO_STREAM_H_ */
