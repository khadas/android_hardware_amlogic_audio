/*
 * Copyright (C) 2010 Amlogic Corporation.
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



#ifndef  _AUDIO_HW_UTILS_H_
#define _AUDIO_HW_UTILS_H_
#include <system/audio.h>
#include "audio_hw.h"
#ifdef ENABLE_DVB_PATCH
#include "dtv_patch.h"
#endif
#include "aml_audio_types_def.h"
#include "aml_audio_stream.h"

#include "aml_dump_debug.h"
/* Maximum string length in audio hal. */
#define AUDIO_HAL_CHAR_MAX_LEN     (256)

#define ENUM_TYPE_STR_MAX_LEN                           (100)


#define ENUM_TYPE_TO_STR_DEFAULT_STR            "INVALID_ENUM"
#define ENUM_TYPE_TO_STR_START(prefix)                      \
    const char *pStr = ENUM_TYPE_TO_STR_DEFAULT_STR;        \
    int prefixLen = strlen(prefix);                         \
    switch (type) {
#define ENUM_TYPE_TO_STR(x)                                 \
    case x:                                                 \
        pStr = #x;                                          \
        pStr += prefixLen;                                  \
        if (strlen(#x) - prefixLen > 70) {                  \
            pStr += 70;                                     \
        }                                                   \
        break;
#define ENUM_TYPE_TO_STR_END                                \
    default:                                                \
        break;                                              \
    }                                                       \
    return pStr;

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

#define AM_LOGV(fmt, ...)  ALOGV("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#define AM_LOGD(fmt, ...)  ALOGD("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#define AM_LOGI(fmt, ...)  ALOGI("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#define AM_LOGW(fmt, ...)  ALOGW("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#define AM_LOGE(fmt, ...)  ALOGE("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)

#define R_CHECK_RET(ret, fmt, ...)                                                              \
    if (ret != 0) {                                                                             \
        AM_LOGE("ret:%d " fmt, ret, ##__VA_ARGS__);                                             \
        return ret;                                                                             \
    }

#define NO_R_CHECK_RET(ret, fmt, ...)                                                           \
    if (ret != 0) {                                                                             \
        AM_LOGE("ret:%d " fmt, ret, ##__VA_ARGS__);                                             \
    }

#define R_CHECK_PARAM_LEGAL(ret, param, min, max, fmt, ...)                                     \
    if ((int)param < min || param > max) {                                                           \
        AM_LOGE("%s:%d is illegal, min:%d, max:%d " fmt, #param, param, min, max, ##__VA_ARGS__);\
        return ret;                                                                             \
    }

#define R_CHECK_POINTER_LEGAL(ret, pointer, fmt, ...)                                           \
    if (pointer == NULL) {                                                                      \
        AM_LOGE("%s is null pointer " fmt, #pointer, ##__VA_ARGS__);                            \
        return ret;                                                                             \
    }

typedef bool (*CHECK_AUDIO_DEVICE_PTR)(audio_devices_t);

enum {
    AUDIO_DEVICE_OUT_XXXXXXXXXX                 = 0,
#if ANDROID_PLATFORM_SDK_VERSION < 31 // < S(12)
    AUDIO_DEVICE_OUT_HDMI_EARC                  = 0x40001u,
    AUDIO_DEVICE_OUT_BLE_HEADSET                = 0x20000000u,
    AUDIO_DEVICE_OUT_BLE_SPEAKER                = 0x20000001u,

    AUDIO_DEVICE_IN_HDMI_EARC                   = 0x88000001u,
    AUDIO_DEVICE_IN_BLE_HEADSET                 = 0xA0000000u,
#endif
};

#define AVSYNC_NONMS12_AUDIO_HAL_EARC_LATENCY_DDP_PROPERTY "vendor.media.audio.hal.nonms12.earc_latency.ddp"
#define AVSYNC_NONMS12_AUDIO_HAL_EARC_LATENCY_DDP (-40)

#define AVSYNC_NONMS12_AUDIO_HAL_ARC_LATENCY_DDP_PROPERTY "vendor.media.audio.hal.nonms12.arc_latency.ddp"
#define AVSYNC_NONMS12_AUDIO_HAL_ARC_LATENCY_DDP (-35)



int64_t aml_gettime(void);
int get_sysfs_uint(const char *path, uint *value);
int set_sysfs_int(const char *path, int value);
int get_sysfs_int(const char *path);
int mystrstr(char *mystr, char *substr) ;
void set_codec_type(int type);
int get_codec_type(int format);
int getprop_bool(const char *path);
unsigned char codec_type_is_raw_data(int type);
int mystrstr(char *mystr, char *substr);
void *convert_audio_sample_for_output(int input_frames, int input_format, int input_ch, void *input_buf, int *out_size/*,float lvol*/);
int check_chip_name(char *chip_name, unsigned int length, struct aml_mixer_handle *mixer_handle);
bool is_rtl_bt_module();
bool is_multi_demux();
int aml_audio_get_debug_flag();
int aml_audio_get_default_alsa_output_ch();
int aml_audio_debug_set_optical_format();
int aml_audio_dump_audio_bitstreams(const char *path, const void *buf, size_t bytes);
int aml_audio_get_arc_latency_offset(int format);
int aml_audio_get_ddp_latency_offset(int aformat,  bool dual_spdif);
int aml_audio_get_pcm_latency_offset(int format, bool is_netflix);
int aml_audio_get_hwsync_latency_offset(bool b_raw);
int aml_audio_get_ddp_frame_size();
bool is_stream_using_mixer(struct aml_stream_out *out);
uint32_t out_get_outport_latency(const struct audio_stream_out *stream);
uint32_t out_get_latency_frames(const struct audio_stream_out *stream);
int aml_audio_get_spdif_tuning_latency(void);
int aml_audio_get_arc_tuning_latency(audio_format_t arc_afmt);
int aml_audio_get_src_tune_latency(enum patch_src_assortion patch_src);
void audio_fade_func(void *buf,int fade_size,int is_fadein);
void ts_wait_time_us(struct timespec *ts, uint32_t time_us);
int cpy_16bit_data_with_gain(int16_t *dst, int16_t *src, int size_in_bytes, float vol);
uint64_t get_systime_ns(void);
int aml_audio_get_hdmi_latency_offset(audio_format_t source_format,
                                    audio_format_t sink_format,int ms12_enable);
int aml_audio_get_latency_offset(audio_devices_t devices, audio_format_t source_format,
                                    audio_format_t sink_format,int ms12_enable, int is_eARC);
uint32_t tspec_diff_to_us(struct timespec tval_old,
        struct timespec tval_new);
int aml_audio_get_dolby_drc_mode(int *drc_mode, int *drc_cut, int *drc_boost);
int aml_audio_get_dolby_dap_drc_mode(int *drc_mode, int *drc_cut, int *drc_boost);
void aml_audio_set_cpu23_affinity();
void * aml_audio_get_muteframe(audio_format_t output_format, int * frame_size, int bAtmos);
void aml_audio_switch_output_mode(int16_t *buf, size_t bytes, AM_AOUT_OutputMode_t mode);
bool aml_audio_data_detect(int16_t *buf, size_t bytes, int detect_value);
int aml_audio_data_handle(struct audio_stream_out *stream, const void* buffer, size_t bytes);
int aml_audio_compensate_video_delay( int enable);
int aml_audio_get_ms12_timestamp_offset(void);
int aml_audio_delay_timestamp(struct timespec *timestamp, int delay_time_us);
int halformat_convert_to_spdif(audio_format_t format, int ch_mask);
int halformat_convert_to_arcformat(audio_format_t format, int ch_mask);
int alsa_device_get_port_index(alsa_device_t alsa_device);
int aml_set_thread_priority(char *pName, pthread_t threadId);
uint32_t out_get_alsa_latency_frames(const struct audio_stream_out *stream);
bool is_multi_channel_pcm(struct audio_stream_out *stream);
bool is_high_rate_pcm(struct audio_stream_out *stream);
bool is_disable_ms12_continuous(struct audio_stream_out *stream);
int find_offset_in_file_strstr(char *mystr, char *substr);
float aml_audio_get_s_gain_by_src(struct aml_audio_device *adev, enum patch_src_assortion type);
int android_dev_convert_to_hal_dev(audio_devices_t android_dev, int *hal_dev_port);
#if ANDROID_PLATFORM_SDK_VERSION > 29
int android_fmt_convert_to_dmx_fmt(audio_format_t android_fmt);
audio_format_t tunerhal_fmt_to_native_fmt(int audioFormat);
audio_format_t encoding_fmt_to_native_fmt(int audioFormat);

#endif
enum patch_src_assortion android_input_dev_convert_to_hal_patch_src(audio_devices_t android_dev);
enum input_source android_input_dev_convert_to_hal_input_src(audio_devices_t android_dev);

const char* patchSrc2Str(enum patch_src_assortion type);
const char* usecase2Str(stream_usecase_t type);
const char* outputPort2Str(enum OUT_PORT type);
const char* inputPort2Str(enum IN_PORT type);
const char* mixerInputType2Str(aml_mixer_input_port_type_e type);
const char* mixerOutputType2Str(MIXER_OUTPUT_PORT type);
uint8_t get_bit_position_in_mask(uint8_t max_position, uint32_t *p_mask);

#ifdef ENABLE_DVB_PATCH
const char* mediasyncAudiopolicyType2Str(audio_policy type);
const char* dtvAudioPatchCmd2Str(AUDIO_DTV_PATCH_CMD_TYPE type);
#endif
const char* hdmiFormat2Str(AML_HDMI_FORMAT_E type);
const char* audioPortRole2Str(audio_port_role_t type);
const char* audioPortType2Str(audio_port_type_t type);
const char* audioDevType2Str(audio_devices_t type);
bool aml_audio_check_sbr_product();
void check_audio_level(const char *name, const void *buffer, size_t bytes);

uint32_t aml_audio_read_audio_data_by_file(const char *file, const char *prop,
    char *buffer, uint32_t bytes, long int *cur_pos);

int aml_audio_trace_int(char *name, int value);
int aml_audio_trace_debug_level(void);

/** convert the audio input format to in buffer's period multi coefficient.
 * @return period multi coefficient(1/4/16)
 */
int convert_audio_format_2_period_mul(audio_format_t format);

static inline void endian16_convert(void *buf, int size)
{
    int i;
    unsigned short *p = (unsigned short *)buf;
    for (i = 0; i < size / 2; i++, p++) {
        *p = ((*p & 0xff) << 8) | ((*p) >> 8);
    }
}
int get_media_video_delay(struct aml_mixer_handle *mixer_handle);
void check_write_time(struct audio_stream_out *stream, size_t bytes);

int aml_get_stream_dump_file_name(audio_format_t audio_format, char *file_name);
static inline bool is_include_filter_out_port(audio_devices_t devices, CHECK_AUDIO_DEVICE_PTR filter) {
    int i = 0;
    audio_devices_t device = AUDIO_DEVICE_NONE;
    while ((device = (audio_devices_t)(1 << i)) != AUDIO_DEVICE_OUT_DEFAULT) {
        if ((devices & device) != 0) {
            if (filter(device)) {
                return true;
            }
        }
        i++;
    }
    return false;
}

static inline bool is_include_sco_out_port(audio_devices_t devices) {
    return is_include_filter_out_port(devices, audio_is_bluetooth_out_sco_device);
}

static inline bool is_include_a2dp_out_port(audio_devices_t devices) {
    return is_include_filter_out_port(devices, audio_is_a2dp_out_device);
}

static inline bool is_include_usb_out_port(audio_devices_t devices) {
    return is_include_filter_out_port(devices, audio_is_usb_out_device);
}
enum OUT_PORT get_output_by_devices(audio_devices_t devices);

bool is_AC4_stream_with_pcm_sink_on_stb(struct aml_stream_out *aml_out);
float get_ac4_stream_volume(struct aml_stream_out *aml_out);
int aml_audio_get_netflix_port_latency(enum OUT_PORT port, audio_format_t output_format);

static inline bool is_tvinput_source(enum patch_src_assortion patch_src) {
    return (patch_src == SRC_DTV || patch_src == SRC_ATV || patch_src == SRC_HDMIIN || \
           patch_src == SRC_LINEIN || patch_src == SRC_SPDIFIN || patch_src == SRC_ARCIN);
}

void aml_alsa_pcm_info_dump(struct pcm* pcm, int fd);

void aml_enter_aaudio_low_latency(struct aml_audio_device *adev);
void aml_leave_aaudio_low_latency(struct aml_audio_device *adev);
bool is_aaudio_low_latency_mode();


#endif
