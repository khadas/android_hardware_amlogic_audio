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

#define LOG_TAG "audio_hw_primary"
//#define LOG_NDEBUG 0
#define __USE_GNU

#include <cutils/log.h>
#include <dolby_ms12.h>
#include <dolby_ms12_config_params.h>
#include <dolby_ms12_status.h>
#include <aml_android_utils.h>
#include <sys/prctl.h>
#include <cutils/properties.h>

#include "audio_hw_ms12.h"
#include "alsa_config_parameters.h"
#include "aml_ac3_parser.h"
#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>
#include "audio_hw.h"
#include "alsa_manager.h"
#include "aml_audio_stream.h"
#include "dolby_lib_api.h"
#include "aml_audio_timer.h"
#include "audio_virtual_buf.h"
#include "ac3_parser_utils.h"
#include "audio_hw_utils.h"
#include "alsa_device_parser.h"
#include "spdif_encoder_api.h"
#include "aml_audio_spdifout.h"
#include "aml_audio_ac3parser.h"
#include "aml_audio_spdifdec.h"
#include "aml_audio_ms12_bypass.h"

#define DOLBY_MS12_OUTPUT_FORMAT_TEST

#define DOLBY_DRC_LINE_MODE 0
#define DOLBY_DRC_RF_MODE   1

#define DDP_MAX_BUFFER_SIZE 2560//dolby ms12 input buffer threshold

#define MS12_MAIN_INPUT_BUF_NS (128000000LL)
#define MS12_MAIN_INPUT_BUF_NS_UPTHRESHOLD (160000000LL)

/*if we choose 96ms, it will cause audio filnger underrun,
  if we choose 64ms, it will cause ms12 underrun,
  so we choose 84ms now
*/
#define MS12_SYS_INPUT_BUF_NS  (84000000LL)

#define NANO_SECOND_PER_SECOND 1000000000LL
#define NANO_SECOND_PER_MILLISECOND 1000000LL


#define MS12_MAIN_BUF_INCREASE_TIME_MS (0)
#define MS12_SYS_BUF_INCREASE_TIME_MS (1000)
#define MM_FULL_POWER_SAMPLING_RATE 48000


#define MS12_DUMP_PROPERTY               "media.audiohal.ms12dump"

#define DUMP_MS12_OUTPUT_SPEAKER_PCM     0x1
#define DUMP_MS12_OUTPUT_SPDIF_PCM       0x2
#define DUMP_MS12_OUTPUT_BITSTREAN       0x4
#define DUMP_MS12_INPUT_MAIN             0x10
#define DUMP_MS12_INPUT_SYS              0x20
#define DUMP_MS12_INPUT_APP              0x40


#define MS12_OUTPUT_SPEAKER_PCM_FILE     "/data/vendor/audiohal/ms12_speaker_pcm.raw"
#define MS12_OUTPUT_SPDIF_PCM_FILE       "/data/vendor/audiohal/ms12_spdif_pcm.raw"
#define MS12_OUTPUT_BITSTREAM_FILE       "/data/vendor/audiohal/ms12_bitstream.raw"
#define MS12_INPUT_SYS_PCM_FILE          "/data/vendor/audiohal/ms12_input_sys.pcm"
#define MS12_INPUT_SYS_MAIN_FILE         "/data/vendor/audiohal/ms12_input_main.raw"
#define MS12_INPUT_SYS_APP_FILE          "/data/vendor/audiohal/ms12_input_app.pcm"
#define MS12_INPUT_SYS_MAIN_IEC_FILE     "/data/vendor/audiohal/ms12_input_main_iec.raw"


#define DDPI_UDC_COMP_LINE 2

#define DDP_SYNC_WORD                    0x0B77
#define ms12_to_adev(ms12_ptr)  (struct aml_audio_device *) (((char*) (ms12_ptr)) - offsetof(struct aml_audio_device, ms12))

#define MS12_MAIN_WRITE_RETIMES             (600)

static int get_ms12_dump_enable(int dump_type) {

    char buf[PROPERTY_VALUE_MAX];
    int ret = -1;
    int value = 0;

    ret = property_get(MS12_DUMP_PROPERTY, buf, NULL);
    if (ret > 0) {
        value = strtol (buf, NULL, 0);
    }
    return (value & dump_type);
}

/*
 *@brief dump ms12 output data
 */
static void dump_ms12_output_data(void *buffer, int size, char *file_name)
{
    FILE *fp1 = fopen(file_name, "a+");
    if (fp1) {
        int flen = fwrite((char *)buffer, 1, size, fp1);
        ALOGV("%s buffer %p size %d\n", __FUNCTION__, buffer, size);
        fclose(fp1);
    }
}


static void *dolby_ms12_threadloop(void *data);

int dolby_ms12_register_callback(struct aml_stream_out *aml_out)
{
    ALOGI("\n+%s()", __FUNCTION__);
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = 0;

    if (ms12->output_config & MS12_OUTPUT_MASK_STEREO) {
        ret = dolby_ms12_register_pcm_callback(pcm_output, (void *)aml_out);
        ALOGI("%s() dolby_ms12_register_pcm_callback return %d", __FUNCTION__, ret);
    }
    if (ms12->output_config & MS12_OUTPUT_MASK_DDP) {
        ret = dolby_ms12_register_bitstream_callback(bitstream_output, (void *)aml_out);
        ALOGI("%s() dolby_ms12_register_bitstream_callback return %d", __FUNCTION__, ret);

    }
    if (ms12->output_config & MS12_OUTPUT_MASK_DD) {
        ret = dolby_ms12_register_spdif_bitstream_callback(spdif_bitstream_output, (void *)aml_out);
        ALOGI("%s() dolby_ms12_register_spdif_bitstream_callback return %d", __FUNCTION__, ret);
    }
    ALOGI("-%s() ret %d\n\n", __FUNCTION__, ret);

    return ret;

}

/*here is some tricky code, we assume below config for dual output*/
static inline alsa_device_t usecase_device_adapter_with_ms12(struct dolby_ms12_desc *ms12, alsa_device_t usecase_device, audio_format_t output_format)
{
    ALOGI("%s usecase_device %d output_format %#x", __func__, usecase_device, output_format);
    switch (usecase_device) {
    case DIGITAL_DEVICE:
    case I2S_DEVICE:
        if ((output_format == AUDIO_FORMAT_AC3) || (output_format == AUDIO_FORMAT_E_AC3)) {
            if (ms12->dual_bitstream_support) {
                if (output_format == AUDIO_FORMAT_E_AC3) {
                    /*dual output, we use spdif b to output ddp*/
                    return DIGITAL_DEVICE2;
                } else {
                    return DIGITAL_DEVICE;
                }
            } else {
                return DIGITAL_DEVICE;
            }
        } else {
            return I2S_DEVICE;
        }
    case DIGITAL_DEVICE2:
        return DIGITAL_DEVICE2;
    default:
        return I2S_DEVICE;
    }
}


/*
 *@brief get dolby ms12 prepared
 */
int get_the_dolby_ms12_prepared(
    struct aml_stream_out *aml_out
    , audio_format_t input_format
    , audio_channel_mask_t input_channel_mask
    , int input_sample_rate)
{
    ALOGI("+%s()", __FUNCTION__);
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int dolby_ms12_drc_mode = DOLBY_DRC_RF_MODE;
    int system_app_mixing_status = SYSTEM_APP_SOUND_MIXING_OFF;
    struct aml_stream_out *out;
    audio_format_t sink_format = ms12->sink_format = adev->sink_format;
    audio_format_t optical_format = ms12->optical_format = adev->optical_format;

    if (aml_out->dual_output_flag) {
        /*dual case, we check the optical format*/
        if (optical_format != AUDIO_FORMAT_PCM_16_BIT &&
            optical_format != AUDIO_FORMAT_AC3        &&
            optical_format != AUDIO_FORMAT_E_AC3) {
            ALOGE("dual =%d ms12 not support =0x%x", aml_out->dual_output_flag ,sink_format);
            return -1;
        }
    } else {
        /*for no dual case, we check sink format*/
        if (sink_format != AUDIO_FORMAT_PCM_16_BIT &&
            sink_format != AUDIO_FORMAT_AC3        &&
            sink_format != AUDIO_FORMAT_E_AC3) {
            ALOGE("dual =%d ms12 not support =0x%x", aml_out->dual_output_flag, sink_format);
            return -1;
        }
    }
    ALOGI("\n+%s()", __FUNCTION__);
    pthread_mutex_lock(&ms12->lock);
    ALOGI("++%s(), locked", __FUNCTION__);
    set_audio_system_format(AUDIO_FORMAT_PCM_16_BIT);
    set_audio_app_format(AUDIO_FORMAT_PCM_16_BIT);
    set_audio_main_format(input_format);
    if (input_format == AUDIO_FORMAT_AC3 || input_format == AUDIO_FORMAT_E_AC3) {
        ms12->dual_decoder_support = adev->dual_decoder_support;
    } else {
        ms12->dual_decoder_support = 0;
    }

    ALOGI("+%s() dual_decoder_support %d\n", __FUNCTION__, ms12->dual_decoder_support);

#ifdef DOLBY_MS12_OUTPUT_FORMAT_TEST
    {
        char buf[PROPERTY_VALUE_MAX];
        int prop_ret = -1;
        int out_format = 0;
        prop_ret = property_get("vendor.dolby.ms12.output.format", buf, NULL);
        if (prop_ret > 0) {
            out_format = atoi(buf);
            if (out_format == 0) {
                ms12->sink_format = AUDIO_FORMAT_PCM_16_BIT;
                ALOGI("DOLBY_MS12_OUTPUT_FORMAT_TEST %d\n", (unsigned int)ms12->sink_format);
            } else if (out_format == 1) {
                ms12->sink_format = AUDIO_FORMAT_AC3;
                ALOGI("DOLBY_MS12_OUTPUT_FORMAT_TEST %d\n", (unsigned int)ms12->sink_format);
            } else if (out_format == 2) {
                ms12->sink_format = AUDIO_FORMAT_E_AC3;
                ALOGI("DOLBY_MS12_OUTPUT_FORMAT_TEST %d\n", (unsigned int)ms12->sink_format);
            }
        }
    }
#endif

    int drc_mode = 0; int drc_cut = 0; int drc_boost = 0;
    if (0 == aml_audio_get_dolby_drc_mode(&drc_mode, &drc_cut, &drc_boost))
        dolby_ms12_drc_mode = (drc_mode == DDPI_UDC_COMP_LINE) ? DOLBY_DRC_LINE_MODE : DOLBY_DRC_RF_MODE;
    //for mul-pcm
    dolby_ms12_set_drc_boost(drc_boost);
    dolby_ms12_set_drc_cut(drc_cut);
    //for 2-channel downmix
    dolby_ms12_set_drc_boost_system(drc_boost);
    dolby_ms12_set_drc_cut_system(drc_cut);
    if ((input_format != AUDIO_FORMAT_AC3) && (input_format != AUDIO_FORMAT_E_AC3)) {
        dolby_ms12_drc_mode = DOLBY_DRC_LINE_MODE;
    }

    /*set the associate audio format*/
    if (ms12->dual_decoder_support) {
        set_audio_associate_format(input_format);
        ALOGI("%s set_audio_associate_format %#x", __FUNCTION__, input_format);
        dolby_ms12_set_asscociated_audio_mixing(adev->associate_audio_mixing_enable);
        dolby_ms12_set_user_control_value_for_mixing_main_and_associated_audio(adev->mixing_level);
        ALOGI("%s associate_audio_mixing_enable %d mixing_level set to %d\n",
              __FUNCTION__, adev->associate_audio_mixing_enable, adev->mixing_level);
        // fix for -xs configration
        //int ms12_runtime_update_ret = aml_ms12_update_runtime_params(&(adev->ms12));
        //ALOGI("aml_ms12_update_runtime_params return %d\n", ms12_runtime_update_ret);
    }
    dolby_ms12_set_drc_mode(dolby_ms12_drc_mode);
    ALOGI("%s dolby_ms12_set_drc_mode %s", __FUNCTION__, (dolby_ms12_drc_mode == DOLBY_DRC_RF_MODE) ? "RF MODE" : "LINE MODE");
    int ret = 0;

    /*set the continous output flag*/
    set_dolby_ms12_continuous_mode((bool)adev->continuous_audio_mode);
    dolby_ms12_set_atmos_lock_flag(adev->atoms_lock_flag);
    /* create  the ms12 output stream here */
    /*************************************/
    if (continous_mode(adev)) {
        // TODO: zz: Might have memory leak, not clear route to release this pointer
        out = (struct aml_stream_out *)aml_audio_calloc(1, sizeof(struct aml_stream_out));
        if (!out) {
            ALOGE("%s malloc  stream failed failed", __func__);
            return -ENOMEM;
        }
        /* copy stream information */
        memcpy(out, aml_out, sizeof(struct aml_stream_out));
        ALOGI("%s format %#x in ott\n", __func__, out->hal_format);
        /*
        *for ms12's callback bitstream_output()
        *the audio format should not be same as AUDIO_FORMAT_IEC61937.
        *this will leads some sink device sound abnormal.
        *this format is used in dolby_ms12_threadloop()
        *to determine use the spdifencoder or not.
        */
        if (out->hal_format == AUDIO_FORMAT_IEC61937) {
            out->hal_format = aml_out->hal_internal_format;
            ALOGI("%s format convert to %#x in ott\n", __func__, out->hal_format);
        }
        if (adev->is_TV) {
            out->config.channels = 8;
            out->config.format = PCM_FORMAT_S32_LE;
            out->tmp_buffer_8ch = aml_audio_malloc(out->config.period_size * 4 * 8);
            if (out->tmp_buffer_8ch == NULL) {
                aml_audio_free(out);
                ALOGE("%s cannot malloc memory for out->tmp_buffer_8ch", __func__);
                return -ENOMEM;
            }
            out->tmp_buffer_8ch_size = out->config.period_size * 4 * 8;
            out->audioeffect_tmp_buffer = aml_audio_malloc(out->config.period_size * 6);
            if (out->audioeffect_tmp_buffer == NULL) {
                aml_audio_free(out->tmp_buffer_8ch);
                aml_audio_free(out);
                ALOGE("%s cannot malloc memory for audioeffect_tmp_buffer", __func__);
                return -ENOMEM;
            }
        }
        out->spdifenc_init = false;
        out->spdifenc_handle = NULL;
        ALOGI("%s create ms12 stream %p,original stream %p", __func__, out, aml_out);
    } else {
        out = aml_out;
    }
    adev->ms12_out = out;
    ALOGI("%s adev->ms12_out =  %p sys mixing =%d", __func__, adev->ms12_out, adev->system_app_mixing_status);
    /************end**************/
    /*set the system app sound mixing enable*/
    if (adev->continuous_audio_mode) {
        system_app_mixing_status = SYSTEM_APP_SOUND_MIXING_ON;
    } else {
        system_app_mixing_status = adev->system_app_mixing_status;
    }
    dolby_ms12_set_system_app_audio_mixing(system_app_mixing_status);


    //init the dolby ms12
    //ms12->dual_bitstream_support = adev->dual_spdif_support;

    int output_config = MS12_OUTPUT_MASK_DD | MS12_OUTPUT_MASK_DDP | MS12_OUTPUT_MASK_STEREO;
    aml_ms12_config(ms12, input_format, input_channel_mask, input_sample_rate, output_config);

    if (ms12->dolby_ms12_enable) {
        //register Dolby MS12 callback
        dolby_ms12_register_callback(out);
        ms12->device = usecase_device_adapter_with_ms12(ms12, out->device, AUDIO_FORMAT_PCM_16_BIT);
        ALOGI("%s out [dual_output_flag %d] adev [format sink %#x optical %#x] ms12 [output-config %#x device %d]",
              __FUNCTION__, out->dual_output_flag, sink_format, optical_format, ms12->output_config, ms12->device);
        memcpy((void *) & (adev->ms12_config), (const void *) & (out->config), sizeof(struct pcm_config));
        get_hardware_config_parameters(
            &(adev->ms12_config)
            , AUDIO_FORMAT_PCM_16_BIT
            , audio_channel_count_from_out_mask(ms12->output_channelmask)
            , ms12->output_samplerate
            , out->is_tv_platform
            , continous_mode(adev));

        if (continous_mode(adev)) {
            ms12->dolby_ms12_thread_exit = false;
            ret = pthread_create(&(ms12->dolby_ms12_threadID), NULL, &dolby_ms12_threadloop, out);
            if (ret != 0) {
                ALOGE("%s, Create dolby_ms12_thread fail!\n", __FUNCTION__);
                goto Err_dolby_ms12_thread;
            }
            ALOGI("%s() thread is builded, get dolby_ms12_threadID %ld\n", __FUNCTION__, ms12->dolby_ms12_threadID);
        }
        //n bytes of dowmix output pcm frame, 16bits_per_sample / stereo, it value is 4btes.
        ms12->nbytes_of_dmx_output_pcm_frame = nbytes_of_dolby_ms12_downmix_output_pcm_frame();
    }

    ms12->sys_audio_base_pos = adev->sys_audio_frame_written;
    ALOGI("set ms12 sys pos =%lld", ms12->sys_audio_base_pos);
    aml_ac3_parser_open(&ms12->ac3_parser_handle);
    aml_ms12_bypass_open(&ms12->ms12_bypass_handle);
    aml_spdif_decoder_open(&ms12->spdif_dec_handle);
    ALOGI("--%s(), locked", __FUNCTION__);
    pthread_mutex_unlock(&ms12->lock);
    ALOGI("-%s()\n\n", __FUNCTION__);
    return ret;

Err_dolby_ms12_thread:
    if (continous_mode(adev)) {
        ALOGE("%s() %d exit dolby_ms12_thread\n", __FUNCTION__, __LINE__);
        ms12->dolby_ms12_thread_exit = true;
        ms12->dolby_ms12_threadID = 0;
        aml_audio_free(out->tmp_buffer_8ch);
        aml_audio_free(out->audioeffect_tmp_buffer);
        aml_audio_free(out);
    }

    pthread_mutex_unlock(&ms12->lock);
    return ret;
}

static bool is_iec61937_format(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    return (aml_out->hal_format == AUDIO_FORMAT_IEC61937);
}

bool is_ms12_passthrough(struct audio_stream_out *stream) {
    bool bypass_ms12 = false;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);

    if ((adev->disable_pcm_mixing == true)
        /* when arc output, the optical_format == sink format
         * when speaker output, the optical format != format
         * only the optical_format == hal_internal_format, we can do passthrough,
         * otherwise we need do some convert
         */
        && (ms12->optical_format == aml_out->hal_internal_format)) {
        if (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3 ||
            aml_out->hal_internal_format == AUDIO_FORMAT_AC3) {
            /*current we only support 48k ddp/dd bypass*/
            if (aml_out->hal_rate == 48000 || aml_out->hal_rate == 192000) {
                bypass_ms12 = true;
            }
        }
    }
    ALOGV("bypass_ms12 =%d disable_pcm_mixing=%d optical format =0x%x 0x%x",
        bypass_ms12, adev->disable_pcm_mixing, ms12->optical_format, aml_out->hal_internal_format);
    return bypass_ms12;
}

static void endian16_convert(void *buf, int size)
{
    int i;
    unsigned short *p = (unsigned short *)buf;
    for (i = 0; i < size / 2; i++, p++) {
        *p = ((*p & 0xff) << 8) | ((*p) >> 8);
    }
}


static int scan_dolby_frame_info(const unsigned char *frame_buf,
        int length,
        int *frame_offset,
        int *frame_size,
        int *frame_numblocks,
        int *framevalid_flag)
{
    int scan_frame_offset;
    int scan_frame_size;
    int scan_channel_num;
    int scan_numblks;
    int scan_timeslice_61937;
    // int scan_framevalid_flag;
    int ret = 0;
    int total_channel_num  = 0;

    if (!frame_buf || (length <= 0) || !frame_offset || !frame_size || !frame_numblocks || !framevalid_flag) {
        ret = -1;
    } else {
        ret = parse_dolby_frame_header(frame_buf, length, &scan_frame_offset, &scan_frame_size
                                       , &scan_channel_num, &scan_numblks,
                                       &scan_timeslice_61937, framevalid_flag);

        if (ret == 0) {
            *frame_offset = scan_frame_offset;
            *frame_size = scan_frame_size;
            *frame_numblocks = scan_numblks;
            //this scan is useful, return 0
            return 0;
        }
    }
    //this scan is useless, return -1
    return -1;
}


/*
 *@brief dolby ms12 main process
 *
 * input parameters
 *     stream: audio_stream_out handle
 *     buffer: data buffer address
 *     bytes: data size
 * output parameters
 *     use_size: buffer used size
 */

int dolby_ms12_main_process(
    struct audio_stream_out *stream
    , const void *buffer
    , size_t bytes
    , size_t *use_size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ms12_output_size = 0;
    int dolby_ms12_input_bytes = 0;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;

    void *input_buffer = (void *)buffer;
    size_t input_bytes = bytes;
    int dual_decoder_used_bytes = 0;
    int single_decoder_used_bytes = 0;
    void *main_frame_buffer = input_buffer;/*input_buffer as default*/
    int main_frame_size = input_bytes;/*input_bytes as default*/
    void *associate_frame_buffer = NULL;
    int associate_frame_size = 0;
    int32_t parser_used_size = 0;
    int32_t spdif_dec_used_size = 0;
    int dependent_frame = 0;
    int sample_rate = 48000;

    if (adev->debug_flag >= 2) {
        ALOGI("\n%s() in continuous %d input ms12 bytes %d input bytes %zu\n",
              __FUNCTION__, adev->continuous_audio_mode, dolby_ms12_input_bytes, input_bytes);
    }

    /*this status is only updated in hw_write, continuous mode also need it*/
    if (adev->continuous_audio_mode) {
        if (aml_out->status != STREAM_HW_WRITING) {
            aml_out->status = STREAM_HW_WRITING;
        }
    }
    /*I can't find where to init this, so I put it here*/
    if ((ms12->main_virtual_buf_handle == NULL) && adev->continuous_audio_mode == 1) {
        /*set the virtual buf size to 96ms*/
        audio_virtual_buf_open(&ms12->main_virtual_buf_handle, "ms12 main input", MS12_MAIN_INPUT_BUF_NS, MS12_MAIN_INPUT_BUF_NS, MS12_MAIN_BUF_INCREASE_TIME_MS);
    }

    if (adev->continuous_audio_mode == 1) {
        uint64_t buf_max_threshold = MS12_MAIN_INPUT_BUF_NS_UPTHRESHOLD;
        uint64_t main_buffer_duration_ns = (ms12->main_input_us - ms12->main_output_us) * 1000;
        if ((main_buffer_duration_ns != 0) && main_buffer_duration_ns / NANO_SECOND_PER_MILLISECOND < ms12->main_buffer_min_level) {
            ms12->main_buffer_min_level = main_buffer_duration_ns / NANO_SECOND_PER_MILLISECOND;
        }
        if (main_buffer_duration_ns /NANO_SECOND_PER_MILLISECOND > ms12->main_buffer_max_level) {
            ms12->main_buffer_max_level = main_buffer_duration_ns / NANO_SECOND_PER_MILLISECOND;
        }
        ALOGV("main input_us =%lld output_us=%lld diff_us=%lld min =%d max=%d",
            ms12->main_input_us, ms12->main_output_us, main_buffer_duration_ns/1000,
            ms12->main_buffer_min_level, ms12->main_buffer_max_level);

        if (main_buffer_duration_ns >= buf_max_threshold) {
            ALOGI("main_buffer_duration_ns =%lld", main_buffer_duration_ns);
            audio_virtual_buf_reset(ms12->main_virtual_buf_handle);
            audio_virtual_buf_process(ms12->main_virtual_buf_handle, MS12_MAIN_INPUT_BUF_NS_UPTHRESHOLD);
        }
    }

    if (ms12->dolby_ms12_enable) {
        //ms12 input main
        int dual_input_ret = 0;
        pthread_mutex_lock(&ms12->main_lock);
        if (ms12->dual_decoder_support == true) {
            dual_input_ret = scan_dolby_main_associate_frame(input_buffer
                             , input_bytes
                             , &dual_decoder_used_bytes
                             , &main_frame_buffer
                             , &main_frame_size
                             , &associate_frame_buffer
                             , &associate_frame_size);
            if (dual_input_ret) {
                ALOGE("%s used size %zu dont find the iec61937 format header, rescan next time!\n", __FUNCTION__, *use_size);
                goto  exit;
            }
        }
        /*
        As the audio payload may cross two write process,we can not skip the
        data when we do not get a complete payload.for ATSC,as we have a
        complete burst align for 6144/24576,so we always can find a valid
        payload in one write process.
        */
        else if (is_iec61937_format(stream)) {
            struct ac3_parser_info ac3_info = { 0 };
            void * dolby_inbuf = NULL;
            int32_t dolby_buf_size = 0;
            int temp_used_size = 0;
            void * temp_main_frame_buffer = NULL;
            int temp_main_frame_size = 0;
            aml_spdif_decoder_process(ms12->spdif_dec_handle, input_buffer , input_bytes, &spdif_dec_used_size, &main_frame_buffer, &main_frame_size);
            if (main_frame_size && main_frame_buffer) {
                uint16_t * data = (uint16_t *)main_frame_buffer;
                /*if is 0x77 0x0b, we need convert it*/
                if (data[0] == DDP_SYNC_WORD) {
                    endian16_convert(main_frame_buffer, main_frame_size);
                }
            }

            if (main_frame_size == 0) {
                *use_size = spdif_dec_used_size;
                goto exit;
            }
            dolby_inbuf = main_frame_buffer;
            dolby_buf_size = main_frame_size;
            aml_ac3_parser_process(ms12->ac3_parser_handle, dolby_inbuf, dolby_buf_size, &temp_used_size, &temp_main_frame_buffer, &temp_main_frame_size, &ac3_info);
            if (ac3_info.sample_rate != 0) {
                sample_rate = ac3_info.sample_rate;
            }
            ALOGV("Input size =%d used_size =%d output size=%d rate=%d interl format=0x%x rate=%d",
                input_bytes, spdif_dec_used_size, main_frame_size, aml_out->hal_rate, aml_out->hal_internal_format, sample_rate);

            if (main_frame_size != 0 && adev->continuous_audio_mode) {
                struct bypass_frame_info frame_info = { 0 };
                aml_out->ddp_frame_size    = main_frame_size;
                frame_info.audio_format    = aml_out->hal_format;
                frame_info.samplerate      = ac3_info.sample_rate;
                frame_info.dependency_frame = ac3_info.frame_dependent;
                frame_info.numblks         = ac3_info.numblks;
                aml_ms12_bypass_checkin_data(ms12->ms12_bypass_handle, main_frame_buffer, main_frame_size, &frame_info);
            }

        }
        /*
         *continuous output with dolby atmos input, the ddp frame size is variable.
         */
        else if (adev->continuous_audio_mode == 1) {
            if ((aml_out->hal_format == AUDIO_FORMAT_AC3) ||
                (aml_out->hal_format == AUDIO_FORMAT_E_AC3)) {
                struct ac3_parser_info ac3_info = { 0 };
                {
                    aml_ac3_parser_process(ms12->ac3_parser_handle, input_buffer, bytes, &parser_used_size, &main_frame_buffer, &main_frame_size, &ac3_info);
                    aml_out->ddp_frame_size = main_frame_size;
                    aml_out->ddp_frame_nblks = ac3_info.numblks;
                    aml_out->total_ddp_frame_nblks += aml_out->ddp_frame_nblks;
                    dependent_frame = ac3_info.frame_dependent;
                    sample_rate = ac3_info.sample_rate;
                    if (ac3_info.frame_size == 0) {
                        *use_size = parser_used_size;
                        if (parser_used_size == 0) {
                            *use_size = bytes;
                        }
                        goto exit;

                    }
                }
                if (main_frame_size != 0) {
                    struct bypass_frame_info frame_info = { 0 };
                    aml_out->ddp_frame_size    = main_frame_size;
                    frame_info.audio_format    = aml_out->hal_format;
                    frame_info.samplerate      = ac3_info.sample_rate;
                    frame_info.dependency_frame = ac3_info.frame_dependent;
                    frame_info.numblks         = ac3_info.numblks;
                    aml_ms12_bypass_checkin_data(ms12->ms12_bypass_handle, main_frame_buffer, main_frame_size, &frame_info);
                }
            }
        }

        if (ms12->dual_decoder_support == true) {
            /*if there is associate frame, send it to dolby ms12.*/
            char tmp_array[4096] = {0};
            if (!associate_frame_buffer || (associate_frame_size == 0)) {
                associate_frame_buffer = (void *)&tmp_array[0];
                associate_frame_size = sizeof(tmp_array);
            }
            if (associate_frame_size < main_frame_size) {
                ALOGV("%s() main frame addr %p size %d associate frame addr %p size %d, need a larger ad input size!\n",
                      __FUNCTION__, main_frame_buffer, main_frame_size, associate_frame_buffer, associate_frame_size);
                memcpy(&tmp_array[0], associate_frame_buffer, associate_frame_size);
                associate_frame_size = sizeof(tmp_array);
            }
            dolby_ms12_input_associate(ms12->dolby_ms12_ptr
                                       , (const void *)associate_frame_buffer
                                       , (size_t)associate_frame_size
                                       , ms12->input_config_format
                                       , audio_channel_count_from_out_mask(ms12->config_channel_mask)
                                       , ms12->config_sample_rate
                                      );
        }

MAIN_INPUT:
        if (main_frame_buffer && (main_frame_size > 0)) {
            /*input main frame*/
            int main_format = ms12->input_config_format;
            int main_channel_num = audio_channel_count_from_out_mask(ms12->config_channel_mask);
            int main_sample_rate = ms12->config_sample_rate;
            if ((dolby_ms12_get_dolby_main1_file_is_dummy() == true) && \
                (dolby_ms12_get_ott_sound_input_enable() == true) && \
                (adev->continuous_audio_mode == 1)) {
                //hwsync pcm, 16bits-stereo
                main_format = AUDIO_FORMAT_PCM_16_BIT;
                main_channel_num = 2;
                main_sample_rate = 48000;
            }
            /*we check whether there is enough space*/
            if ((adev->continuous_audio_mode == 1) &&
                ((aml_out->hal_format == AUDIO_FORMAT_AC3) ||
                (aml_out->hal_format == AUDIO_FORMAT_E_AC3) ||
                (aml_out->hal_format == AUDIO_FORMAT_IEC61937))) {
                int max_size = 0;
                int main_avail = 0;
                int wait_retry = 0;
                do {
                    main_avail = dolby_ms12_get_main_buffer_avail(&max_size);
                    if ((max_size - main_avail) >= main_frame_size) {
                        break;
                    }
                    pthread_mutex_unlock(&ms12->main_lock);
                    aml_audio_sleep(5*1000);
                    pthread_mutex_lock(&ms12->main_lock);
                    wait_retry++;
                    /*it cost 3s*/
                    if (wait_retry >= MS12_MAIN_WRITE_RETIMES) {
                        *use_size = parser_used_size;
                        if (parser_used_size == 0) {
                            *use_size = bytes;
                        }
                        ALOGE("write dolby main time out, discard data=%d main_frame_size=%d", *use_size, main_frame_size);
                        goto exit;
                    }

                } while (aml_out->status != STREAM_STANDBY);
            }

            dolby_ms12_input_bytes =
                dolby_ms12_input_main(
                    ms12->dolby_ms12_ptr
                    , main_frame_buffer
                    , main_frame_size
                    , main_format
                    , main_channel_num
                    , main_sample_rate);
            if (adev->debug_flag >= 2)
                ALOGI("%s line %d main_frame_size %d ret dolby_ms12 input_bytes %d", __func__, __LINE__, main_frame_size, dolby_ms12_input_bytes);

            if (adev->continuous_audio_mode == 0) {
                dolby_ms12_scheduler_run(ms12->dolby_ms12_ptr);
            }

            if (dolby_ms12_input_bytes > 0) {
                if (ms12->dual_decoder_support == true) {
                    *use_size = dual_decoder_used_bytes;
                } else {
                    if (adev->debug_flag >= 2) {
                        ALOGI("%s() continuous %d input ms12 bytes %d input bytes %zu sr %d main size %d parser size %d\n\n",
                              __FUNCTION__, adev->continuous_audio_mode, dolby_ms12_input_bytes, input_bytes, ms12->config_sample_rate, main_frame_size, single_decoder_used_bytes);
                    }
                    if (adev->continuous_audio_mode == 1) {
                        //FIXME, if ddp input, the size suppose as CONTINUOUS_OUTPUT_FRAME_SIZE
                        //if pcm input, suppose 2ch/16bits/48kHz
                        uint64_t input_ns = 0;
                        if ((aml_out->hal_format == AUDIO_FORMAT_AC3) || \
                            (aml_out->hal_format == AUDIO_FORMAT_E_AC3)) {
                            int sample_nums = aml_out->ddp_frame_nblks * SAMPLE_NUMS_IN_ONE_BLOCK;
                            int frame_duration = DDP_FRAME_DURATION(sample_nums*1000, DDP_OUTPUT_SAMPLE_RATE);
                            input_ns = (uint64_t)dolby_ms12_input_bytes * frame_duration * 1000000 / aml_out->ddp_frame_size;
                            if (dependent_frame) {
                                input_ns = 0;
                            }
                            ms12->main_input_rate = sample_rate;
                        } else if(aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                            input_ns = (uint64_t)1536 * 1000000000LL / sample_rate;
                            ms12->main_input_rate = sample_rate;
                        } else {
                            /*
                            for LPCM audio,we support it is 2 ch 48K audio.
                            */
                            input_ns = (uint64_t)dolby_ms12_input_bytes * 1000000000LL / 4 / ms12->config_sample_rate;
                            ms12->main_input_rate = ms12->config_sample_rate;
                        }
                        ALOGV("input_ns =%lld", input_ns);
                        audio_virtual_buf_process(ms12->main_virtual_buf_handle, input_ns);
                        ms12->main_input_us += input_ns / 1000;
                    }

                    if (is_iec61937_format(stream)) {
                        *use_size = spdif_dec_used_size;
                    } else {
                        *use_size = dolby_ms12_input_bytes;
                        if (adev->continuous_audio_mode == 1) {
                            if ((aml_out->hal_format == AUDIO_FORMAT_AC3) || (aml_out->hal_format == AUDIO_FORMAT_E_AC3)) {
                                *use_size = parser_used_size;
                            } else if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                                *use_size = spdif_dec_used_size;
                            }
                        }
                    }

                }
            }
        } else {
            if (ms12->dual_decoder_support == true) {
                *use_size = dual_decoder_used_bytes;
            } else {
                *use_size = input_bytes;
            }
        }
        ALOGV("in =%d used =%d", input_bytes, *use_size);
        ms12->is_bypass_ms12 = is_ms12_passthrough(stream);
        ms12->is_dolby_atmos = (dolby_ms12_get_input_atmos_info() == 1);
exit:
        if (get_ms12_dump_enable(DUMP_MS12_INPUT_MAIN)) {
            dump_ms12_output_data((void*)buffer, *use_size, MS12_INPUT_SYS_MAIN_FILE);
        }
        pthread_mutex_unlock(&ms12->main_lock);
        return 0;
    } else {
        return -1;
    }
}


/*
 *@brief dolby ms12 system process
 *
 * input parameters
 *     stream: audio_stream_out handle
 *     buffer: data buffer address
 *     bytes: data size
 * output parameters
 *     use_size: buffer used size
 */
int dolby_ms12_system_process(
    struct audio_stream_out *stream
    , const void *buffer
    , size_t bytes
    , size_t *use_size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    audio_channel_mask_t mixer_default_channelmask = AUDIO_CHANNEL_OUT_STEREO;
    int mixer_default_samplerate = 48000;
    int dolby_ms12_input_bytes = 0;
    int ms12_output_size = 0;
    int ret = 0;

    pthread_mutex_lock(&ms12->lock);
    if (ms12->dolby_ms12_enable) {
        //Dual input, here get the system data
        dolby_ms12_input_bytes =
            dolby_ms12_input_system(
                ms12->dolby_ms12_ptr
                , buffer
                , bytes
                , AUDIO_FORMAT_PCM_16_BIT
                , audio_channel_count_from_out_mask(mixer_default_channelmask)
                , mixer_default_samplerate);
        if (dolby_ms12_input_bytes > 0) {
            *use_size = dolby_ms12_input_bytes;
            ret = 0;
        } else {
            *use_size = 0;
            ret = -1;
        }
    }
    if (get_ms12_dump_enable(DUMP_MS12_INPUT_SYS)) {
        dump_ms12_output_data((void*)buffer, *use_size, MS12_INPUT_SYS_PCM_FILE);
    }
    pthread_mutex_unlock(&ms12->lock);

    if (adev->continuous_audio_mode == 1) {
        uint64_t input_ns = 0;
        input_ns = (uint64_t)(*use_size) * 1000000000LL / 4 / mixer_default_samplerate;

        if (ms12->system_virtual_buf_handle == NULL) {
            //aml_audio_sleep(input_ns/1000);
            if (input_ns == 0) {
                input_ns = (uint64_t)(bytes) * 1000000000LL / 4 / mixer_default_samplerate;
            }
            audio_virtual_buf_open(&ms12->system_virtual_buf_handle, "ms12 system input", input_ns/2, MS12_SYS_INPUT_BUF_NS, MS12_SYS_BUF_INCREASE_TIME_MS);
        }
        audio_virtual_buf_process(ms12->system_virtual_buf_handle, input_ns);
    }

    return ret;
}


/*
 *@brief dolby ms12 app process
 *
 * input parameters
 *     stream: audio_stream_out handle
 *     buffer: data buffer address
 *     bytes: data size
 * output parameters
 *     use_size: buffer used size
 */
int dolby_ms12_app_process(
    struct audio_stream_out *stream
    , const void *buffer
    , size_t bytes
    , size_t *use_size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    audio_channel_mask_t mixer_default_channelmask = AUDIO_CHANNEL_OUT_STEREO;
    int mixer_default_samplerate = 48000;
    int dolby_ms12_input_bytes = 0;
    int ms12_output_size = 0;
    int ret = 0;

    pthread_mutex_lock(&ms12->lock);
    if (ms12->dolby_ms12_enable) {
        //Dual input, here get the system data
        dolby_ms12_input_bytes =
            dolby_ms12_input_app(
                ms12->dolby_ms12_ptr
                , buffer
                , bytes
                , AUDIO_FORMAT_PCM_16_BIT
                , audio_channel_count_from_out_mask(mixer_default_channelmask)
                , mixer_default_samplerate);
        if (dolby_ms12_input_bytes > 0) {
            *use_size = dolby_ms12_input_bytes;
            ret = 0;
        } else {
            *use_size = 0;
            ret = -1;
        }
    }
    if (get_ms12_dump_enable(DUMP_MS12_INPUT_APP)) {
        dump_ms12_output_data((void*)buffer, *use_size, MS12_INPUT_SYS_APP_FILE);
    }
    pthread_mutex_unlock(&ms12->lock);

    return ret;
}


/*
 *@brief get dolby ms12 cleanup
 */
int get_dolby_ms12_cleanup(struct dolby_ms12_desc *ms12)
{
    int is_quit = 1;
    int i = 0;
    struct aml_audio_device *adev = NULL;

    ALOGI("+%s()", __FUNCTION__);
    if (!ms12) {
        return -EINVAL;
    }

    adev = ms12_to_adev(ms12);

    pthread_mutex_lock(&ms12->lock);
    pthread_mutex_lock(&ms12->main_lock);
    ALOGI("++%s(), locked", __FUNCTION__);
    ALOGI("%s() dolby_ms12_set_quit_flag %d", __FUNCTION__, is_quit);
    dolby_ms12_set_quit_flag(is_quit);

    if (ms12->dolby_ms12_threadID != 0) {
        ms12->dolby_ms12_thread_exit = true;
        int ms12_runtime_update_ret = aml_ms12_update_runtime_params(ms12);
        ALOGI("aml_ms12_update_runtime_params return %d\n", ms12_runtime_update_ret);
        pthread_join(ms12->dolby_ms12_threadID, NULL);
        ms12->dolby_ms12_threadID = 0;
        ALOGI("%s() dolby_ms12_threadID reset to %ld\n", __FUNCTION__, ms12->dolby_ms12_threadID);
    }
    set_audio_system_format(AUDIO_FORMAT_INVALID);
    set_audio_app_format(AUDIO_FORMAT_INVALID);
    set_audio_main_format(AUDIO_FORMAT_INVALID);
    dolby_ms12_flush_main_input_buffer();
    dolby_ms12_config_params_set_system_flag(false);
    dolby_ms12_config_params_set_app_flag(false);
    aml_ms12_cleanup(ms12);
    ms12->output_config = 0;
    ms12->dolby_ms12_enable = false;
    ms12->is_dolby_atmos = false;
    ms12->input_total_ms = 0;
    ms12->bitsteam_cnt = 0;
    ms12->nbytes_of_dmx_output_pcm_frame = 0;
    ms12->dual_bitstream_support = 0;
    ms12->is_bypass_ms12 = false;
    ms12->last_frames_postion = 0;
    ms12->main_input_us = 0;
    ms12->main_output_us = 0;
    ms12->main_input_rate = DDP_OUTPUT_SAMPLE_RATE;
    ms12->main_buffer_min_level = 0xFFFFFFFF;
    ms12->main_buffer_max_level = 0;
    audio_virtual_buf_close(&ms12->main_virtual_buf_handle);
    audio_virtual_buf_close(&ms12->system_virtual_buf_handle);
    for (i = 0; i < BITSTREAM_OUTPUT_CNT; i++) {
        struct bitstream_out_desc * bitstream_out = &ms12->bitstream_out[i];
        if (bitstream_out->spdifout_handle) {
            aml_audio_spdifout_close(bitstream_out->spdifout_handle);
            bitstream_out->spdifout_handle = NULL;
        }
    }
    aml_ac3_parser_close(ms12->ac3_parser_handle);
    ms12->ac3_parser_handle = NULL;
    aml_spdif_decoder_close(ms12->spdif_dec_handle);
    ms12->spdif_dec_handle = NULL;
    aml_ms12_bypass_close(ms12->ms12_bypass_handle);
    ms12->ms12_bypass_handle = NULL;
    ALOGI("--%s(), locked", __FUNCTION__);
    pthread_mutex_unlock(&ms12->main_lock);
    pthread_mutex_unlock(&ms12->lock);
    ALOGI("-%s()", __FUNCTION__);
    return 0;
}

/*
 *@brief set dolby ms12 primary gain
 */
int set_dolby_ms12_primary_input_db_gain(struct dolby_ms12_desc *ms12, int db_gain , int duration)
{
    MixGain gain;
    int ret = 0;

    ALOGI("+%s(): gain %ddb, ms12 enable(%d)",
          __FUNCTION__, db_gain, ms12->dolby_ms12_enable);
    if (!ms12) {
        return -EINVAL;
    }

    //pthread_mutex_lock(&ms12->lock);
    if (!ms12->dolby_ms12_enable) {
        ret = -EINVAL;
        goto exit;
    }

    gain.target = db_gain;
    gain.duration = duration;
    gain.shape = 0;
    dolby_ms12_set_system_sound_mixer_gain_values_for_primary_input(&gain);
    dolby_ms12_set_input_mixer_gain_values_for_main_program_input(&gain);
    //Fixme when tunnel mode is working, the Alexa start and mute the main input!
    //dolby_ms12_set_input_mixer_gain_values_for_ott_sounds_input(&gain);
    // only update very limited parameter with out lock
    //ret = aml_ms12_update_runtime_params_lite(ms12);

exit:
    //pthread_mutex_unlock(&ms12->lock);
    return ret;
}

static ssize_t aml_ms12_spdif_output_new (struct audio_stream_out *stream,
                                struct bitstream_out_desc * bitstream_desc, audio_format_t output_format, void *buffer, size_t byte)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    spdif_format_t spdif_format = { 0 };

    int ret = 0;

    /*some switch happen*/
    if (bitstream_desc->spdifout_handle != NULL && bitstream_desc->audio_format != output_format) {
        ALOGI("spdif output format chamged from =0x%x to 0x%x", bitstream_desc->audio_format, output_format);
        aml_audio_spdifout_close(bitstream_desc->spdifout_handle);
        ALOGI("%s spdif format changed from 0x%x to 0x%x", __FUNCTION__, bitstream_desc->audio_format, output_format);
        bitstream_desc->spdifout_handle = NULL;
    }

    if (output_format == AUDIO_FORMAT_IEC61937) {
        spdif_format.audio_format = AUDIO_FORMAT_IEC61937;
        spdif_format.sub_format   = aml_out->hal_internal_format;
    } else {
        spdif_format.audio_format = output_format;
        spdif_format.sub_format   = output_format;
    }

    if (bitstream_desc->spdifout_handle == NULL) {
        ret = aml_audio_spdifout_open(&bitstream_desc->spdifout_handle, &spdif_format);
    }
    if (ret != 0) {
        ALOGE("open spdif out failed\n");
        return ret;
    }
    bitstream_desc->audio_format = output_format;

    ret = aml_audio_spdifout_processs(bitstream_desc->spdifout_handle, buffer, byte);


    return ret;
}

/*this function for non continuous mode passthrough*/
int dolby_ms12_bypass_process(struct audio_stream_out *stream, void *buffer, size_t bytes) {
    int ret = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];
    audio_format_t output_format = aml_out->hal_format;
    ALOGV("output_format=0x%x internal =0x%x", output_format, aml_out->hal_internal_format);
    bool is_dolby = (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) || (aml_out->hal_internal_format == AUDIO_FORMAT_AC3);
    if (ms12->is_bypass_ms12
        && (adev->continuous_audio_mode == 0)
        && is_dolby
        && !adev->dual_decoder_support) {
        if (bytes != 0 && buffer != NULL) {
            ret = aml_ms12_spdif_output_new(stream, bitstream_out, output_format, buffer, bytes);
        }
    }

    return ret;
}

/*this function for continuous mode passthrough*/
int ms12_passthrough_output(struct aml_stream_out *aml_out) {
    int ret = 0;
    int i = 0;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    audio_format_t output_format = AUDIO_FORMAT_PCM_16_BIT;
    void *output_buf = NULL;
    int32_t out_size = 0;
    struct bypass_frame_info frame_info = { 0 };
    uint64_t ms12_dec_out_nframes = dolby_ms12_get_n_bytes_pcmout_of_udc();
    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];

    if (ms12_dec_out_nframes != 0 &&
        (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3 || aml_out->hal_internal_format == AUDIO_FORMAT_AC3)) {
        uint64_t consume_offset = dolby_ms12_get_consumed_payload();
        aml_ms12_bypass_checkout_data(ms12->ms12_bypass_handle, &output_buf, &out_size, consume_offset, &frame_info);
    }
    if (adev->disable_pcm_mixing == false) {
        ms12->is_bypass_ms12 = false;
    }


    if (ms12->is_bypass_ms12) {
        ALOGV("bypass ms12 size=%d", out_size);
        output_format = aml_out->hal_internal_format;
        if (ms12->is_continuous_paused) {
            for (i = 0; i < BITSTREAM_OUTPUT_CNT; i++) {
                struct bitstream_out_desc * bitstream_out = &ms12->bitstream_out[i];
                if (bitstream_out->spdifout_handle) {
                    aml_audio_spdifout_close(bitstream_out->spdifout_handle);
                    bitstream_out->spdifout_handle = NULL;
                }
            }
            out_size = 0;
        }

        if (out_size != 0 && output_buf != NULL) {
            struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
            ret = aml_ms12_spdif_output_new(stream_out, bitstream_out, output_format, output_buf, out_size);
        }
    }

    return ret;
}

int pcm_output(void *buffer, void *priv_data, size_t size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    audio_format_t output_format = AUDIO_FORMAT_PCM_16_BIT;
    int ret = 0;
    int i;

    if (adev->debug_flag > 1) {
        ALOGI("+%s() size %zu", __FUNCTION__, size);
    }

    /*we update the optical format in pcm, because it is always output*/
    if (ms12->optical_format != adev->optical_format) {
         ALOGI("ms12 optical format change from 0x%x to  0x%x\n",adev->ms12.optical_format,adev->optical_format);
         ms12->optical_format= adev->optical_format;
         for (i = 0; i < BITSTREAM_OUTPUT_CNT; i++) {
             struct bitstream_out_desc * bitstream_out = &ms12->bitstream_out[i];
             if (bitstream_out->spdifout_handle) {
                 aml_audio_spdifout_close(bitstream_out->spdifout_handle);
                 bitstream_out->spdifout_handle = NULL;
             }
         }
    }

    if (adev->continuous_audio_mode) {
        uint32_t sample_rate = ms12->main_input_rate ? ms12->main_input_rate : DDP_OUTPUT_SAMPLE_RATE;
        uint64_t ms12_dec_out_nframes = 0;
        if (!audio_is_linear_pcm(aml_out->hal_internal_format)) {
            ms12_dec_out_nframes = dolby_ms12_get_n_bytes_pcmout_of_udc() / adev->ms12.nbytes_of_dmx_output_pcm_frame;
        } else if (aml_out->hw_sync_mode) {
            ms12_dec_out_nframes = dolby_ms12_get_consumed_payload() / 4;
        } else {
            ms12_dec_out_nframes = 0;
        }
        ms12->main_output_us = ms12_dec_out_nframes * NANO_SECOND_PER_MILLISECOND / sample_rate;
        ALOGV("format = 0x%x ms12_dec_out_nframes=%lld", aml_out->hal_internal_format, ms12_dec_out_nframes);
    }


    /*dump ms12 pcm output*/
    if (get_ms12_dump_enable(DUMP_MS12_OUTPUT_SPEAKER_PCM)) {
        dump_ms12_output_data(buffer, size, MS12_OUTPUT_SPEAKER_PCM_FILE);
    }

    ms12->is_dolby_atmos = (dolby_ms12_get_input_atmos_info() == 1);

    if (audio_hal_data_processing((struct audio_stream_out *)aml_out, buffer, size, &output_buffer, &output_buffer_bytes, output_format) == 0) {
        ret = hw_write((struct audio_stream_out *)aml_out, output_buffer, output_buffer_bytes, output_format);
    }


    /*we put passthrough ms12 data here*/
    ms12_passthrough_output(aml_out);


    if (adev->debug_flag > 1) {
        ALOGI("-%s() ret %d", __FUNCTION__, ret);
    }

    return ret;
}

int bitstream_output(void *buffer, void *priv_data, size_t size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    audio_format_t output_format = AUDIO_FORMAT_AC3;
    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];
    struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
    int ret = 0;
    ms12->bitsteam_cnt++;

    if (adev->debug_flag > 1) {
        ALOGI("+%s() size %zu,dual_output = %d, optical_format = 0x%0x, sink_format = 0x%x out total=%d main in=%d", __FUNCTION__, size, aml_out->dual_output_flag, adev->optical_format, adev->sink_format, ms12->bitsteam_cnt, ms12->input_total_ms);
    }

    if (ms12->is_bypass_ms12) {
        return 0;
    }

    ms12->is_dolby_atmos = (dolby_ms12_get_input_atmos_info() == 1);

    if (ms12->optical_format != AUDIO_FORMAT_E_AC3) {
        return 0;
    }

    /*dump ms12 bitstream output*/
    if (get_ms12_dump_enable(DUMP_MS12_OUTPUT_BITSTREAN)) {
        dump_ms12_output_data(buffer, size, MS12_OUTPUT_BITSTREAM_FILE);
    }

    output_format = AUDIO_FORMAT_E_AC3;
    ret = aml_ms12_spdif_output_new(stream_out, bitstream_out, output_format, buffer, size);


    if (adev->debug_flag > 1) {
        ALOGI("-%s() ret %d", __FUNCTION__, ret);
    }

    return ret;
}

int spdif_bitstream_output(void *buffer, void *priv_data, size_t size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];
    struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    audio_format_t output_format = AUDIO_FORMAT_AC3;
    int ret = 0;

    if (adev->debug_flag > 1) {
        ALOGI("+%s() size %zu,dual_output = %d, optical_format = 0x%x, sink_format = 0x%x out total=%d main in=%d", __FUNCTION__, size, aml_out->dual_output_flag, adev->optical_format, adev->sink_format, ms12->bitsteam_cnt, ms12->input_total_ms);
    }

    if (ms12->is_bypass_ms12) {
        return 0;
    }

    if (adev->active_outport == OUTPORT_HDMI_ARC && adev->sink_format == AUDIO_FORMAT_PCM_16_BIT) {
        return 0;
    }

    if (ms12->optical_format != AUDIO_FORMAT_AC3) {
        return 0;
    }

    if (adev->patch_src ==  SRC_DTV && aml_out->need_drop_size > 0) {
        if (adev->debug_flag > 1)
            ALOGI("func:%s, av sync drop data,need_drop_size=%d\n",
                __FUNCTION__, aml_out->need_drop_size);
        return ret;
    }

    /*dump ms12 bitstream output*/
    if (get_ms12_dump_enable(DUMP_MS12_OUTPUT_BITSTREAN)) {
        dump_ms12_output_data(buffer, size, MS12_OUTPUT_BITSTREAM_FILE);
    }

    ret = aml_ms12_spdif_output_new(stream_out, bitstream_out, output_format, buffer, size);

    return ret;
}


static void *dolby_ms12_threadloop(void *data)
{
    ALOGI("+%s() ", __FUNCTION__);
    struct aml_stream_out *aml_out = (struct aml_stream_out *)data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    if (ms12 == NULL) {
        ALOGE("%s ms12 pointer invalid!", __FUNCTION__);
        goto Error;
    }

    if (ms12->dolby_ms12_enable) {
        dolby_ms12_set_quit_flag(ms12->dolby_ms12_thread_exit);
    }

    prctl(PR_SET_NAME, (unsigned long)"DOLBY_MS12");
    aml_set_thread_priority("DOLBY_MS12", pthread_self());

    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    CPU_SET(2, &cpuSet);
    CPU_SET(3, &cpuSet);
    int sastat = sched_setaffinity(0, sizeof(cpu_set_t), &cpuSet);
    if (sastat) {
        ALOGW("%s(), failed to set cpu affinity", __FUNCTION__);
    }

    while ((ms12->dolby_ms12_thread_exit == false) && (ms12->dolby_ms12_enable)) {
        ALOGV("%s() goto dolby_ms12_scheduler_run", __FUNCTION__);
        if (ms12->dolby_ms12_ptr) {
            dolby_ms12_scheduler_run(ms12->dolby_ms12_ptr);
        } else {
            ALOGE("%s() ms12->dolby_ms12_ptr is NULL, fatal error!", __FUNCTION__);
            break;
        }
        ALOGV("%s() dolby_ms12_scheduler_run end", __FUNCTION__);
    }
    ALOGI("%s remove   ms12 stream %p", __func__, aml_out);
    if (continous_mode(adev)) {
        pthread_mutex_lock(&adev->alsa_pcm_lock);
        aml_alsa_output_close((struct audio_stream_out*)aml_out);
        adev->spdif_encoder_init_flag = false;
#if 0
        struct pcm *pcm = adev->pcm_handle[DIGITAL_DEVICE];
        if (aml_out->dual_output_flag && pcm) {
            ALOGI("%s close dual output pcm handle %p", __func__, pcm);
            pcm_close(pcm);
            adev->pcm_handle[DIGITAL_DEVICE] = NULL;
            aml_out->dual_output_flag = 0;
        }
#endif
        aml_audio_set_spdif_format(PORT_SPDIF, AML_STEREO_PCM, aml_out);
        pthread_mutex_unlock(&adev->alsa_pcm_lock);
        if (aml_out->spdifenc_init) {
            aml_spdif_encoder_close(aml_out->spdifenc_handle);
            aml_out->spdifenc_handle = NULL;
            aml_out->spdifenc_init = false;
        }
        release_audio_stream((struct audio_stream_out *)aml_out);
    }
    adev->ms12_out = NULL;
    ALOGI("-%s(), exit dolby_ms12_thread\n", __FUNCTION__);
    return ((void *)0);

Error:
    ALOGI("-%s(), exit dolby_ms12_thread, because of erro input params\n", __FUNCTION__);
    return ((void *)0);
}

int set_system_app_mixing_status(struct aml_stream_out *aml_out, int stream_status)
{
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int system_app_mixing_status = SYSTEM_APP_SOUND_MIXING_OFF;
    int ret = 0;

    if (STREAM_STANDBY == stream_status) {
        system_app_mixing_status = SYSTEM_APP_SOUND_MIXING_OFF;
    } else {
        system_app_mixing_status = SYSTEM_APP_SOUND_MIXING_ON;
    }

    adev->system_app_mixing_status = system_app_mixing_status;

    //when under continuous_audio_mode, system app sound mixing always on.
    if (adev->continuous_audio_mode) {
        system_app_mixing_status = SYSTEM_APP_SOUND_MIXING_ON;
    }

    if (adev->debug_flag) {
        ALOGI("%s stream-status %d set system-app-audio-mixing %d current %d continuous_audio_mode %d\n", __func__,
              stream_status, system_app_mixing_status, dolby_ms12_get_system_app_audio_mixing(), adev->continuous_audio_mode);
    }

    dolby_ms12_set_system_app_audio_mixing(system_app_mixing_status);

    if (ms12->dolby_ms12_enable) {
        pthread_mutex_lock(&ms12->lock);
        ret = aml_ms12_update_runtime_params(&(adev->ms12));
        pthread_mutex_unlock(&ms12->lock);
        ALOGI("%s return %d stream-status %d set system-app-audio-mixing %d\n",
              __func__, ret, stream_status, system_app_mixing_status);
        return ret;
    }

    return 1;
}


int nbytes_of_dolby_ms12_downmix_output_pcm_frame()
{
    int pcm_out_chanenls = 2;
    int bytes_per_sample = 2;

    return pcm_out_chanenls*bytes_per_sample;
}

void dolby_ms12_app_flush()
{
    dolby_ms12_flush_app_input_buffer();
}

int dolby_ms12_main_flush(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    dolby_ms12_flush_main_input_buffer();
    ms12->main_input_us = 0;
    ms12->main_output_us = 0;
    ms12->main_input_rate = DDP_OUTPUT_SAMPLE_RATE;
    ms12->main_buffer_min_level = 0xFFFFFFFF;
    ms12->main_buffer_max_level = 0;
    ms12->last_frames_postion = 0;

    if (ms12->spdif_dec_handle) {
        aml_spdif_decoder_reset(ms12->ac3_parser_handle);
    }

    if (ms12->ac3_parser_handle) {
        aml_ac3_parser_reset(ms12->ac3_parser_handle);
    }
    if (ms12->ms12_bypass_handle) {
        aml_ms12_bypass_reset(ms12->ms12_bypass_handle);
    }
    return 0;
}

bool is_ms12_continous_mode(struct aml_audio_device *adev)
{
    if ((eDolbyMS12Lib == adev->dolby_lib_type) && (adev->continuous_audio_mode)) {
        return true;
    } else {
        return false;
    }
}

/*
 *The audio data(direct/offload/hwsync) should bypass Dolby MS12,
 *if Audio Mixing is Off, and (Sink & Output) format are both EAC3,
 *specially, the dual decoder is false and continuous audio mode is false.
 *because Dolby MS12 is working at LiveTV+(Dual Decoder) or Continuous Mode.
 */
 bool is_bypass_dolbyms12(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    bool is_dts = is_dts_format(aml_out->hal_internal_format);
    bool is_dolby_audio = is_dolby_format(aml_out->hal_internal_format);

    return (is_dts
            || is_high_rate_pcm(stream)
            || is_multi_channel_pcm(stream));
}

int dolby_ms12_hwsync_init(void) {
    return dolby_ms12_hwsync_init_internal();
}

int dolby_ms12_hwsync_release(void) {
    return dolby_ms12_hwsync_release_internal();
}

int dolby_ms12_hwsync_checkin_pts(int offset, int apts) {
    return dolby_ms12_hwsync_checkin_pts_internal(offset, apts);
}

