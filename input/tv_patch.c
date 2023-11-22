/*
* Copyright 2023 Amlogic Inc. All rights reserved.
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

#define LOG_TAG "audio_hw_input_tv"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <utils/Timers.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <hardware/audio.h>
#include <aml_data_utils.h>
#include <audio_utils/channels.h>
#if ANDROID_PLATFORM_SDK_VERSION >= 25 // 8.0
#include <system/audio-base.h>
#endif

#include "audio_hw.h"
#include "aml_audio_stream.h"
#include "audio_hw_utils.h"
#include "aml_audio_timer.h"
#include "alsa_config_parameters.h"
#include "tv_patch_avsync.h"
#include "aml_ng.h"
#include "alsa_device_parser.h"
#include "tv_patch_ctrl.h"
#include "tv_patch.h"
#include "device_patch_mgr.h"
#include "audio_hw_resource_mgr.h"
#include "component_noise_gate.h"
#include "tv_private_object.h"
#include "dolby_lib_api.h"
#include "spdif_encoder_api.h"

void audio_digital_input_format_check(struct aml_audio_patch *patch)
{
    struct aml_stream_out *aml_out = patch->output_stream;
    struct aml_audio_device *adev = NULL;
    audio_format_t cur_aformat;

    if (!aml_out)
        return;

    adev = aml_out->dev;

    if (aml_out->is_tv_src_stream && IS_DIGITAL_IN_HW(patch->input_src)) {
        cur_aformat = audio_parse_get_audio_type (patch->audio_parse_para);
        if (cur_aformat != patch->aformat) {
            ALOGI ("HDMI/SPDIF input format changed from %#x to %#x   hal_format changed from %#x to %#x\n", patch->aformat, cur_aformat, aml_out->hal_format, cur_aformat);
            patch->aformat = cur_aformat;
            //FIXME: if patch audio format change, the hal_format need to redefine.
            //then the out_get_format() can get it.
            if (cur_aformat != AUDIO_FORMAT_PCM_16_BIT && cur_aformat != AUDIO_FORMAT_PCM_32_BIT) {
                aml_out->hal_format = AUDIO_FORMAT_IEC61937;
                patch->IEC61937_format = true;
            } else {
                aml_out->hal_format = cur_aformat ;
                patch->IEC61937_format = false;
            }
            aml_out->digital_input_fmt_change = true;
            patch->mode_reconfig_flag = true;
            aml_out->hal_internal_format = cur_aformat;
            aml_out->hal_channel_mask = audio_parse_get_audio_channel_mask (patch->audio_parse_para);
            ALOGI ("%s hal_channel_mask %#x, mode_reconfig_flag %d\n", __FUNCTION__, aml_out->hal_channel_mask, patch->mode_reconfig_flag);
            if (aml_out->hal_internal_format == AUDIO_FORMAT_DTS ||
                aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD) {
                if (aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD) {
                    /* For DTS-HD case, needs enlarge buffer and start threshold to anti-xrun */
                    aml_out->config.period_count = 12;
                    aml_out->config.period_size = DEFAULT_PLAYBACK_PERIOD_SIZE;
                    // The maximum dts-hd frame duration is 4096 frames, needs to be greater than this to avoid underruns at the start.
                    aml_out->config.start_threshold = 4608; // 4096 + 512
                } else {
                    // reset to default
                    aml_out->config.period_count = DEFAULT_PLAYBACK_PERIOD_CNT;
                    aml_out->config.period_size = DEFAULT_PLAYBACK_PERIOD_SIZE;
                    aml_out->config.start_threshold = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
                }

                if (audio_parse_get_audio_type_direct(patch->audio_parse_para) == DTSCD ) {
                    aml_out->is_dtscd = true;
                } else {
                    aml_out->is_dtscd = false;
                }
            } else {
                adev->dolby_lib_type = adev->dolby_lib_type_last;
                // reset to default
                aml_out->config.period_count = DEFAULT_PLAYBACK_PERIOD_CNT;
                aml_out->config.period_size = DEFAULT_PLAYBACK_PERIOD_SIZE;
                aml_out->config.start_threshold = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
            }
            /* reset audio patch ringbuffer */
            ring_buffer_reset(&patch->aml_ringbuffer);
            adev->spdif_encoder_init_flag = false;
        }
    }
}

/*==================================patch & threadloops=========================================*/
// buffer/period ratio, bigger will add more latency
void *audio_patch_input_threadloop(void *data)
{
    struct aml_audio_patch *patch = (struct aml_audio_patch *)data;
    struct audio_hw_device *dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    ring_buffer_t *ringbuffer = & (patch->aml_ringbuffer);
    struct audio_stream_in *stream_in = NULL;
    struct patch_manager *patch_manager = get_patch_manager(aml_dev);
    struct aml_stream_in *in;
    struct audio_config stream_config;
    struct timespec ts;
    int aux_read_bytes, read_bytes;
    // FIXME: add calc for read_bytes;
    read_bytes = DEFAULT_CAPTURE_PERIOD_SIZE * CAPTURE_PERIOD_COUNT;
    int ret = 0, retry = 0;
    audio_format_t cur_aformat;
    int ring_buffer_size = 0;
    bool stable_flag = false;
    bool first_start = true;
    patch->read_size = 0;
    patch->sync_offset = -1;
    patch->start_mute = false;
    patch->mdelay = 0;

    ALOGI("++%s", __FUNCTION__);

    ALOGD("%s: enter", __func__);
    patch->chanmask = stream_config.channel_mask = patch->in_chanmask;
    patch->sample_rate = stream_config.sample_rate = patch->in_sample_rate;
    /*coverity[ missing_lock]*/
    patch->aformat = stream_config.format = patch->in_format;

    ret = adev_open_input_stream(patch->dev, 0, patch->input_src, &stream_config, &stream_in, 0, "AML_TV_SOURCE", 0);
    if (ret < 0) {
        ALOGE("%s: open input steam failed ret = %d", __func__, ret);
        return (void *)0;
    }

    in = (struct aml_stream_in *)stream_in;
    /* CVBS IN signal is stable very quickly, sometimes there is no unstable state,
     * we need to do ease in before playing CVBS IN at in_read func.
     */
    if (in->device & AUDIO_DEVICE_IN_LINE) {
        in->mute_flag = true;
    }

    patch->in_buf_size = read_bytes = in->config.period_size * audio_stream_in_frame_size(&in->stream);
    patch->in_buf = aml_audio_calloc(1, patch->in_buf_size);
    if (!patch->in_buf) {
        adev_close_input_stream(patch->dev, &in->stream);
        return (void *)0;
    }

    prctl(PR_SET_NAME, (unsigned long)"audio_input_patch");
    aml_set_thread_priority("audio_input_patch", patch->audio_input_threadID);
    /*affinity the thread to cpu 2/3 which has few IRQ*/
    // aml_audio_set_cpu23_affinity();

    if (ringbuffer) {
        ring_buffer_size = ringbuffer->size;
    }

    while (!patch->input_thread_exit) {
        int bytes_avail = 0;
        /* Todo: read bytes should reconfig with period size */
        int period_mul = 1;//convert_audio_format_2_period_mul(patch->aformat);
        int read_threshold = 0;
        aml_check_pic_mode(patch);
        if (!is_game_mode(aml_dev))
            read_bytes = DEFAULT_CAPTURE_PERIOD_SIZE * audio_stream_in_frame_size(&in->stream) * period_mul;
        else
            read_bytes = LOW_LATENCY_CAPTURE_PERIOD_SIZE * audio_stream_in_frame_size(&in->stream) * period_mul;
        bool hdmi_raw_in_flag = patch && (patch->input_src == AUDIO_DEVICE_IN_HDMI) && (!audio_is_linear_pcm(patch->aformat));
        if (hdmi_raw_in_flag) {
            read_bytes = read_bytes / 2;
            if (patch->aformat == AUDIO_FORMAT_MAT) {
                read_bytes = read_bytes * 4;
            }
        }

        if (patch->input_src == AUDIO_DEVICE_IN_LINE) {
            read_threshold = 4 * read_bytes;
        }

        // buffer size diff from allocation size, need to resize.
        if (patch->in_buf_size < (size_t)read_bytes) {
            ALOGI("%s: !!realloc in buf size from %zu to %d", __func__, patch->in_buf_size, read_bytes);
            patch->in_buf = aml_audio_realloc(patch->in_buf, read_bytes);
            if (!patch->in_buf) {
               break;
            }
            patch->in_buf_size = read_bytes;
            memset(patch->in_buf, 0, patch->in_buf_size);
        }

        if (in->standby) {
            ret = start_input_stream(in);
            if (ret < 0) {
                ALOGE("start_input_stream failed !");
            }
            in->standby = 0;
        }

        bytes_avail = read_bytes;
        /* if audio is unstable, don't read data from hardware */
        stable_flag = check_tv_stream_signal(&in->stream);
        if (aml_dev->tv_mute || !stable_flag) {
            /* case 1:we need keep read from arc so the format will be keep stable */
            /* case 2:For data type detect of hardware, it needs to read data from driver to help detecting quickly if it is HBR stream.   */
            if (is_HBR_stream(&in->stream) || (in->device & AUDIO_DEVICE_IN_HDMI_ARC) || (in->device & AUDIO_DEVICE_IN_SPDIF)) {
                aml_alsa_input_read(&in->stream, patch->in_buf, read_bytes);
                memset(patch->in_buf, 0, bytes_avail);
                ring_buffer_clear(ringbuffer);
            } else {
                /* For game mode, the sleep time is different with normal mode when play pcm stream. */
                /* Because it reads about 10ms data from driver when it is in game mode. Otherwise it */
                /* is about 20ms for normal mode. To avoid underrun happen, change sleep time to  */
                /* corresponding mode. */
                if (is_game_mode(aml_dev)) {
                    usleep(10*1000);
                } else {
                    usleep(20*1000);
                }
                memset(patch->in_buf, 0, bytes_avail);
                ring_buffer_clear(ringbuffer);
                enable_tv_mute(aml_dev, true);
            }
        } else {
            if (is_same_patch_src(aml_dev, SRC_HDMIIN) && in->audio_packet_type == AUDIO_PACKET_AUDS && in->config.channels != 2) {
                input_stream_channels_adjust(&in->stream, patch->in_buf, read_bytes);
            } else {
                if (is_tv_mute(aml_dev) && (audio_is_linear_pcm(patch->aformat)) && is_game_mode(aml_dev)) {
                    ring_buffer_reset(ringbuffer);
                    ret = pcm_ioctl(aml_dev->pcm_handle[I2S_DEVICE], SNDRV_PCM_IOCTL_RESET, 0);
                    if (ret < 0) {
                        ALOGE("cannot reset pcm!");
                        }
                    enable_tv_mute(aml_dev, false);
                }

                aml_audio_trace_int("input_read_thread", read_bytes);
                aml_alsa_input_read(&in->stream, patch->in_buf, read_bytes);
                aml_audio_trace_int("input_read_thread", 0);

                if (get_debug_value(AML_DUMP_AUDIOHAL_TV)) {
                    aml_audio_dump_audio_bitstreams("/data/vendor/audiohal/tv_read.raw", patch->in_buf, read_bytes);
                }

                if (IS_DIGITAL_IN_HW(patch->input_src) && !check_digital_in_stream_signal(&in->stream)) {
                    memset(patch->in_buf, 0, bytes_avail);
                }

                if (!check_tv_stream_signal(&in->stream)) {
                    memset(patch->in_buf, 0, bytes_avail);
                }
            }
            if (IS_DIGITAL_IN_HW(patch->input_src)) {
                audio_format_t cur_aformat;
                cur_aformat = audio_parse_get_audio_type (patch->audio_parse_para);
                if (in->data_type == DATA_NON_PCM) {
                    if (audio_is_linear_pcm(cur_aformat))
                        bytes_avail = 0;
                    audio_raw_data_continuous_check(aml_dev, patch->audio_parse_para, patch->in_buf, read_bytes);
                }
            }
            audio_digital_input_format_check(patch);
        }

        /*noise gate is only used in Linein for 16bit audio data*/
        if (get_active_inport(aml_dev) == INPORT_LINEIN && is_ng_enable(aml_dev)) {
            int ng_status = noise_gate_process(aml_dev, patch->in_buf, bytes_avail >> 1);
            /*if (ng_status == NG_MUTE)
                ALOGI("noise gate is working!");*/
        }
        ALOGV("++%s in read over read_bytes = %d, in_read returns = %d, threshold %d",
              __FUNCTION__, read_bytes, bytes_avail, read_threshold);

        if (bytes_avail > 0) {
            do {
                if (patch->input_src == AUDIO_DEVICE_IN_HDMI)
                {
                    pthread_mutex_lock(&in->lock);
                    ret = reconfig_read_param_through_hdmiin(aml_dev, in, ringbuffer, ring_buffer_size);
                    pthread_mutex_unlock(&in->lock);
                    if (ret == 0) {
                        break;
                    }
                }

                if (get_buffer_write_space(ringbuffer) >= bytes_avail) {
                    retry = 0;
                    aml_audio_trace_int("input_thread_write2buf", bytes_avail);
                    ret = ring_buffer_write(ringbuffer,
                                            (unsigned char*)patch->in_buf,
                                            bytes_avail, UNCOVER_WRITE);
                    if (ret != bytes_avail) {
                        ALOGE("%s(), write buffer fails!", __func__);
                    }
                    aml_audio_trace_int("input_thread_write2buf", 0);

                    /* for audio first start or read bytes size is more than output threshold, start output */
                    if (first_start || get_buffer_read_space(ringbuffer) >= read_threshold) {
                        pthread_cond_signal(&patch->cond);
                        first_start = false;
                    }
                } else {
                    retry = 1;
                    first_start = true;
                    /* if ringbuffer is full enough but no output, reset ringbuffer, wait a short while, go to read once more */
                    ALOGD("%s(), ring buffer no space to write, buffer free size:%d, need write size:%d", __func__,
                        get_buffer_write_space(ringbuffer), bytes_avail);
                    ring_buffer_reset(ringbuffer);
                    usleep(3000);
                }
            } while (retry && !patch->input_thread_exit);
        } else {
            ALOGV("%s(), read alsa pcm fails, to _read(%d), bytes_avail(%d)!",
                  __func__, read_bytes, bytes_avail);
            if (get_buffer_read_space(ringbuffer) >= bytes_avail) {
                pthread_cond_signal(&patch->cond);
            }
            usleep(3000);
        }
    }
    adev_close_input_stream(patch->dev, &in->stream);
    if (patch->in_buf) {
        aml_audio_free(patch->in_buf);
        patch->in_buf = NULL;
    }
    ALOGD("%s: exit", __func__);

    return (void *)0;
}

void *audio_patch_output_threadloop(void *data)
{
    struct aml_audio_patch *patch = (struct aml_audio_patch *)data;
    struct audio_hw_device *dev = NULL;
    struct audio_stream_out *stream_out = NULL;
    struct aml_stream_out *aml_out = NULL,*out = NULL;
    struct audio_config stream_config = AUDIO_CONFIG_INITIALIZER;
    struct timespec ts;
    int write_bytes = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
    int ret;
    if (!patch) {
        ALOGE("%s: patch is NULL", __func__);
        return (void *)0;
    }

    patch->output_stream = NULL;
    dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    ring_buffer_t *ringbuffer = & (patch->aml_ringbuffer);
    int txlx_chip = check_chip_name("txlx", 4, &aml_dev->alsa_mixer);
    ALOGD("%s: enter", __func__);
    stream_config.channel_mask = patch->out_chanmask;
    stream_config.sample_rate = patch->out_sample_rate;
    stream_config.format = patch->out_format;

#ifdef DOLBY_MS12_INPUT_FORMAT_TEST
    char buf[PROPERTY_VALUE_MAX] = {0};
    int prop_ret = -1;
    int format = 0;
    prop_ret = property_get("vendor.dolby.ms12.input.format", buf, NULL);
    if (prop_ret > 0) {
        format = atoi(buf);
        if (format == 1) {
            stream_config.format = AUDIO_FORMAT_AC3;
        } else if (format == 2) {
            stream_config.format = AUDIO_FORMAT_E_AC3;
        }
    }
#endif
    /*
    may we just exit from a direct active stream playback
    still here.we need remove to standby to new playback
    */
    pthread_mutex_lock(&aml_dev->lock);
    aml_out = direct_active(aml_dev);
    if (aml_out) {
        ALOGI("%s stream %p active,need standby aml_out->usecase:%s ", __func__, aml_out, usecase2Str(aml_out->usecase));
        pthread_mutex_lock(&aml_out->lock);
        do_output_standby_l((struct audio_stream *)aml_out);
        pthread_mutex_unlock(&aml_out->lock);

    }
    aml_dev->mix_init_flag = false;
    aml_dev->mute_start = true;
    pthread_mutex_unlock(&aml_dev->lock);
    ret = adev_open_output_stream_new(patch->dev,
                                      0,
                                      patch->output_src,
                                      AUDIO_OUTPUT_FLAG_DIRECT,
                                      &stream_config,
                                      &stream_out,
                                      "AML_TV_SOURCE");
    if (ret < 0) {
        ALOGE("%s: open output stream failed", __func__);
        return (void *)0;
    }

    out = (struct aml_stream_out *)stream_out;
    out->digital_input_fmt_change = false;
    patch->out_buf_size = write_bytes = out->config.period_size * audio_stream_out_frame_size(&out->stream);
    patch->out_buf = aml_audio_calloc(1, patch->out_buf_size);
    patch->output_stream = (struct aml_stream_out *)stream_out;
    if (!patch->out_buf) {
        adev_close_output_stream_new(patch->dev, &out->stream);
        return (void *)0;
    }
    prctl(PR_SET_NAME, (unsigned long)"audio_output_patch");
    aml_set_thread_priority("audio_output_patch", patch->audio_output_threadID);
    /*affinity the thread to cpu 2/3 which has few IRQ*/
    aml_audio_set_cpu23_affinity();

    while (!patch->output_thread_exit) {
        int period_mul;

        if (patch->aformat == AUDIO_FORMAT_E_AC3)
            period_mul = EAC3_MULTIPLIER;
        else if (IS_DIGITAL_IN_HW(patch->input_src) && audio_parse_get_audio_packet_type(patch->audio_parse_para) == AUDIO_PACKET_HBR)
            period_mul = HBR_MULTIPLIER;    // 16
        else if (IS_DIGITAL_IN_HW(patch->input_src) && patch->aformat == AUDIO_FORMAT_DTS_HD
            && audio_parse_get_audio_packet_type(patch->audio_parse_para) != AUDIO_PACKET_HBR)
            period_mul = HBR_MULTIPLIER / 2;
        else
            period_mul = 1;

        if (is_game_mode(aml_dev))
            write_bytes = LOW_LATENCY_PLAYBACK_PERIOD_SIZE * audio_stream_out_frame_size(&out->stream);

        // buffer size diff from allocation size, need to resize.
        ret = aml_audio_check_and_realloc(&patch->out_buf, &patch->out_buf_size, write_bytes * period_mul);
        if (ret != 0) {
            AM_LOGE("aml_audio_check_and_realloc fail");
            return (void *)-1;
        }
        pthread_mutex_lock(&patch->mutex);
        ALOGV("%s(), ringbuffer level read before wait--%d",
              __func__, get_buffer_read_space(ringbuffer));
        if (get_buffer_read_space(ringbuffer) < (write_bytes * period_mul)) {
            // wait 300ms
            ts_wait_time(&ts, 300000);
            pthread_cond_timedwait(&patch->cond, &patch->mutex, &ts);
        }
        pthread_mutex_unlock(&patch->mutex);

        ALOGV("%s(), ringbuffer level read after wait-- %d",
              __func__, get_buffer_read_space(ringbuffer));
        if (get_buffer_read_space(ringbuffer) >= (write_bytes * period_mul)) {
            aml_audio_trace_int("output_thread_read_from_buf", write_bytes * period_mul);
            ret = ring_buffer_read(ringbuffer,
                                   (unsigned char*)patch->out_buf, write_bytes * period_mul);
            if (ret == 0) {
                ALOGE("%s(), ring_buffer read 0 data!", __func__);
            }
            aml_audio_trace_int("output_thread_read_from_buf", 0);
            /* avsync for dev->dev patch*/
            if ((patch->need_do_avsync == true) && (patch->input_signal_stable == true) &&
                    (is_same_patch_src(aml_dev, SRC_ATV) ||
                     is_same_patch_src(aml_dev, SRC_HDMIIN)||
                     is_same_patch_src(aml_dev, SRC_LINEIN))) {

                if (!txlx_chip && !is_game_mode(aml_dev)) {
                    aml_dev_try_avsync(patch);
                    if (patch->skip_frames) {
                        ALOGD("%s(), skip this period data for avsync!", __func__);
                        usleep(5);
                        continue;
                    }
                } else {
                    patch->need_do_avsync = false;
                }
            }

            /* reconfig output in picture mode switch */
            if (patch->input_src == AUDIO_DEVICE_IN_HDMI) {
                stream_check_reconfig_param(stream_out);
            }

            out_write_new(stream_out, patch->out_buf, ret);

        } else {
            ALOGV("%s(), no enough data in ring buffer, available data size:%d, need data size:%d", __func__,
                get_buffer_read_space(ringbuffer), (write_bytes * period_mul));
            if (audio_is_linear_pcm(patch->aformat)) {
                usleep( (DEFAULT_PLAYBACK_PERIOD_SIZE) * 1000000 / 4 /
                    stream_config.sample_rate);
            }
        }
    }
    do_output_standby_l((struct audio_stream *)out);
    adev_close_output_stream_new(patch->dev, &out->stream);
    if (patch->out_buf) {
        aml_audio_free(patch->out_buf);
        patch->out_buf = NULL;
    }
    ALOGD("%s: exit", __func__);
    return (void *)0;
}

int create_tv_patch(struct aml_audio_device *aml_dev,
                        audio_devices_t input,
                        audio_devices_t output __unused)
{
    struct aml_audio_patch *patch;
    int play_buffer_size = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
    pthread_attr_t attr;
    struct sched_param param;
    int ret = 0;

    ALOGD("%s: enter", __func__);

    patch = aml_audio_calloc(1, sizeof(*patch));
    if (!patch) {
        return -ENOMEM;
    }

    patch->dev = (struct audio_hw_device *)aml_dev;
    patch->input_src = input;
    patch->is_dtv_src = false;
    patch->aformat = AUDIO_FORMAT_PCM_16_BIT;
    set_dev_patch(aml_dev, patch);
    aml_dev->foreground_stream_type = FG_STREAM_TYPE_PATCH;
    pthread_mutex_init(&patch->mutex, NULL);
    pthread_cond_init(&patch->cond, NULL);

    patch->in_sample_rate = 48000;
    patch->in_chanmask = AUDIO_CHANNEL_IN_STEREO;
    patch->output_src = aml_dev->cur_out_devices;
    patch->out_sample_rate = 48000;
    patch->out_chanmask = AUDIO_CHANNEL_OUT_STEREO;
    patch->in_format = AUDIO_FORMAT_PCM_16_BIT;
    patch->out_format = AUDIO_FORMAT_PCM_16_BIT;

    /* when audio patch start, signal is unstable or
     * patch signal is unstable, it need do avsync
     * except the arcin and spdifin input src
     */
    if (patch->input_src != AUDIO_DEVICE_IN_HDMI_ARC && patch->input_src != AUDIO_DEVICE_IN_SPDIF)
        patch->need_do_avsync = true;

    if (patch->out_format == AUDIO_FORMAT_PCM_16_BIT) {
        ALOGE("%s: init audio ringbuffer game %d", __func__, is_game_mode(aml_dev));
        if (!is_game_mode(aml_dev))
            ret = ring_buffer_init(&patch->aml_ringbuffer, 4 * 2 * play_buffer_size * PATCH_PERIOD_COUNT);
        else
            ret = ring_buffer_init(&patch->aml_ringbuffer, 2 * 4 * LOW_LATENCY_PLAYBACK_PERIOD_SIZE);
    } else {
        ret = ring_buffer_init(&patch->aml_ringbuffer, 4 * 4 * play_buffer_size * PATCH_PERIOD_COUNT);
    }

    if (aml_dev->dev2mix_patch) {
        create_tvin_buffer(patch);
    }

    if (ret < 0) {
        ALOGE("%s: init audio ringbuffer failed", __func__);
        goto err_ring_buf;
    }
    ret = pthread_create(&patch->audio_input_threadID, NULL,
                          &audio_patch_input_threadloop, patch);

    if (ret != 0) {
        ALOGE("%s: Create input thread failed", __func__);
        goto err_in_thread;
    }
    ret = pthread_create(&patch->audio_output_threadID, NULL,
                          &audio_patch_output_threadloop, patch);
    if (ret != 0) {
        ALOGE("%s: Create output thread failed", __func__);
        goto err_out_thread;
    }

    if (IS_DIGITAL_IN_HW(patch->input_src)) {
        //TODO add sample rate and channel information
        ret = create_pthread_for_audio_type_parse(&patch->audio_parse_threadID,
                &patch->audio_parse_para, &aml_dev->alsa_mixer, patch->input_src);
        if (ret !=  0) {
            ALOGE("%s: create format parse thread failed", __func__);
            goto err_parse_thread;
        }
    }

    if (aml_dev->useSubMix) {
        float src_gain = aml_audio_get_s_gain_by_src(aml_dev, get_dev_patch_src(aml_dev));

        subMixingSetSrcGain(aml_dev, src_gain);
    }

    set_dev_patch(aml_dev, patch);
    /* Use flag to indicate that patch struct is ready.  TBD */
    validate_dev_patch(aml_dev);
    ALOGD("%s: exit", __func__);

    return 0;
err_parse_thread:
    patch->output_thread_exit = 1;
    pthread_join(patch->audio_output_threadID, NULL);
err_out_thread:
    patch->input_thread_exit = 1;
    pthread_join(patch->audio_input_threadID, NULL);
err_in_thread:
    ring_buffer_release(&patch->aml_ringbuffer);
err_ring_buf:
    aml_audio_free(patch);
    return ret;
}

int release_tv_patch(struct aml_audio_device *aml_dev)
{
    struct aml_audio_patch *patch = get_dev_patch(aml_dev);

    ALOGD("%s: enter", __func__);
    if (!is_dev_patch_exist(aml_dev)) {
        ALOGD("%s(), no patch to release", __func__);
        goto exit;
    }
    /* Use flag to indicate that it will start to free patch struct.  TBD */
    invalidate_dev_patch(aml_dev);
    tv_do_ease_out(aml_dev);
    if (IS_DIGITAL_IN_HW(patch->input_src))
        exit_pthread_for_audio_type_parse(patch->audio_parse_threadID,&patch->audio_parse_para);
    patch->input_thread_exit = 1;
    pthread_join(patch->audio_input_threadID, NULL);
    patch->output_thread_exit = 1;
    pthread_join(patch->audio_output_threadID, NULL);
    ring_buffer_release(&patch->aml_ringbuffer);
    release_tvin_buffer(patch);
    set_output_device_mute(aml_dev, AUDIO_DEVICE_OUT_SPEAKER, false, true);
    aml_audio_free(patch);
    set_dev_patch(aml_dev, NULL);
    aml_dev->audio_patch_2_af_stream = true;
    stop_dtv_patch(aml_dev);
    set_dev_patch_src(aml_dev, SRC_INVAL);
    /* when exit audio HAL patch, set src gain to default: media */
    if (aml_dev->useSubMix) {
        float src_gain = aml_audio_get_s_gain_by_src(aml_dev, SRC_OTHER);

        subMixingSetSrcGain(aml_dev, src_gain);
    }

exit:
    ALOGD("%s: done", __func__);
    return 0;
}
