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

#define LOG_TAG "audio_hw_hal_kara"
//#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <audio_utils/channels.h>

#include "audio_data_process.h"
#include "audio_hw_utils.h"
#include "karaoke_manager.h"
#include "aml_volume_utils.h"
//#include "EffectReverb.h"
#include "aml_malloc_debug.h"
#include "audio_hw_ms12.h"

#define USB_DEFAULT_PERIOD_SIZE      512
#define USB_DEFAULT_PERIOD_COUNT     2

#define LINEIN_DEFAULT_SAMPLE_RATE   48000
#define LINEIN_DEFAULT_CHANNEL       2
#define LINEIN_DEFAULT_FORMAT        PCM_FORMAT_S16_LE
#define LINEIN_DEFAULT_PERIOD_SIZE   1024
#define LINEIN_DEFAULT_PERIOD_COUNT  4

static audio_format_t convert_alsa_format_2_audio_format(enum pcm_format format)
{
    switch (format) {
        case PCM_FORMAT_S16_LE:
            return AUDIO_FORMAT_PCM_16_BIT;
        case PCM_FORMAT_S32_LE:
            return AUDIO_FORMAT_PCM_32_BIT;
        case PCM_FORMAT_S8:
            return AUDIO_FORMAT_PCM_8_BIT;
        case  PCM_FORMAT_S24_LE:
            return AUDIO_FORMAT_PCM_8_24_BIT;
        case PCM_FORMAT_S24_3LE:
            return AUDIO_FORMAT_PCM_24_BIT_PACKED;
        default:
            AM_LOGE("invalid format:%#x, return 16bit format.", format);
            return AUDIO_FORMAT_PCM_16_BIT;
    }
}

static ssize_t voice_in_read(struct kara_manager *kara, size_t frames)
{
    if (!kara || 0 == frames) {
        return -EINVAL;
    }
    struct voice_in *in = &kara->in;
    size_t num_read_buff_bytes = 0; // real read size from alsa
    size_t after_conversion_size = 0; // max size of kara->buf
    void *read_buff = kara->buf;
    void *out_buff = kara->buf;
    unsigned int num_in_channels = in->cfg.channelCnt;
    unsigned int num_mixout_channels = kara->mixout_config.channelCnt;
    uint32_t in_framesize = in->cfg.frame_size;
    uint32_t out_framesize = kara->mixout_config.frame_size;
    if (!in || 0 == num_in_channels || 0 == num_mixout_channels
        || 0 == in_framesize || 0 == out_framesize) {
        return -EINVAL;
    }
    int ret = -1;
    num_read_buff_bytes = frames * in_framesize;
    after_conversion_size = num_read_buff_bytes; // initial with no conversion

    /* 1.use conversion buffer for saving initial mic data
       2.do channel adjust and save to kara->buf */
    if (num_in_channels != num_mixout_channels) {
        if (num_read_buff_bytes > in->conversion_buffer_size) {
            ALOGV("num_read_buff_bytes:%zu conversion_buffer_size:%zu",
                   num_read_buff_bytes, in->conversion_buffer_size);
            in->conversion_buffer_size = num_read_buff_bytes;
            in->conversion_buffer = aml_audio_realloc(in->conversion_buffer, in->conversion_buffer_size);
            if (!in->conversion_buffer) {
                AM_LOGE("conversion_buffer malloc is fail");
                return -1;
            }
        }
        read_buff = in->conversion_buffer;
        /* calculate the size of kara->buf if need conversion */
        after_conversion_size = (num_read_buff_bytes * num_mixout_channels) / num_in_channels;
    }

    /* kara->buf is for saving the mic data whether need convert or not */
    if (after_conversion_size > kara->buf_len) {
        kara->buf = aml_audio_realloc(kara->buf, after_conversion_size);
        if (!kara->buf) {
            AM_LOGE("kara->buf malloc is fail");
            return -1;
        }
        kara->buf_len = after_conversion_size;
    }

    if (KARA_TYPE_USB == kara->kara_type) {
        ret = proxy_read(&in->proxy, read_buff, num_read_buff_bytes);
    } else if (KARA_TYPE_LINEIN == kara->kara_type) {
        ret = pcm_read(in->pcm_handle, read_buff, num_read_buff_bytes);
    }
//    AM_LOGD("ret = %d, num_read_buff_bytes = %zu", ret, num_read_buff_bytes);
    if (0 == ret) {
        if (get_debug_value(AML_DUMP_AUDIOHAL_OUT) || in->debug) {
            aml_dump_audio_bitstreams("/data/audio/karaoke_in.raw", read_buff, num_read_buff_bytes);
        }
        if (kara->kara_mic_record) {
            ring_buffer_write(&kara->mic_buffer, (unsigned char *)read_buff, num_read_buff_bytes, UNCOVER_WRITE);
        }
        /* Channels conversion */
        if (num_in_channels != num_mixout_channels) {
            out_buff = kara->buf;
            enum pcm_format format = in->pcm_in_config.format;
            unsigned sample_size_in_bytes = pcm_format_to_bits(format) / 8;
            after_conversion_size = adjust_channels(read_buff, num_in_channels,
                                                  out_buff, num_mixout_channels,
                                                  sample_size_in_bytes, num_read_buff_bytes);
        }
    } else {
        after_conversion_size = 0;
    }

    return after_conversion_size;
}

static ssize_t mic_buffer_read(struct kara_manager *kara, void *buffer, size_t bytes)
{
    if (!kara || !buffer || kara->mic_buffer.size == 0 || !kara->karaoke_start) {
        return -EINVAL;
    }

    uint32_t read_bytes = 0;
    uint32_t nodata_count = 0;
    int retry_max_count = 20;
    int sleep_us = 5000;
    size_t ret = 0;
    while (read_bytes < bytes) {
        ret = ring_buffer_read(&kara->mic_buffer, (uint8_t *)buffer + read_bytes, bytes - read_bytes);
        read_bytes += ret;
        if (read_bytes == bytes) {
            return bytes;
        }
        nodata_count++;
        if (nodata_count >= retry_max_count) {
            AM_LOGW("read data timeout, need:%zu, read_bytes:%d", bytes, read_bytes);
            memset(buffer, 0, bytes);
            return bytes;
        }
        usleep(sleep_us);
    }
    return bytes;
}

static int kara_open_micphone(struct kara_manager *kara, struct audioCfg *cfg)
{
    struct voice_in *in = NULL;
    struct pcm_config proxy_config;
    alsa_device_profile *profile = NULL;
    int ret = 0;

    ALOGI("++%s()", __func__);

    if (!cfg || !kara) {
        ALOGE("%s() NULL pointer, cfg %p, kara %p", __func__, cfg, kara);
        return -EINVAL;
    }

    pthread_mutex_lock(&kara->lock);
    if (kara->karaoke_start == true) {
        ALOGI("%s() karaoke is opened!", __func__);
        pthread_mutex_unlock(&kara->lock);
        return 0;
    }

    in = &kara->in;
    profile = in->in_profile;
    memset(&proxy_config, 0, sizeof(proxy_config));
    proxy_config.channels = profile_get_closest_channel_count(profile, cfg->channelCnt);

    if (profile_is_sample_rate_valid(profile, cfg->sampleRate)) {
        proxy_config.rate = cfg->sampleRate;
    } else {
        ALOGE("USB profile can't support rate: %d", cfg->sampleRate);
        proxy_config.rate = profile_get_default_sample_rate(profile);
    }
    proxy_config.format = convert_audio_format_2_alsa_format(cfg->format);
    if (!profile_is_format_valid(profile, proxy_config.format)) {
        proxy_config.format = profile_get_default_format(profile);
    }
    proxy_config.period_size = USB_DEFAULT_PERIOD_SIZE;
    proxy_config.period_count = USB_DEFAULT_PERIOD_COUNT;

    /* in config for channel adjust and mix */
    in->cfg.channelCnt = proxy_config.channels;
    in->cfg.sampleRate = proxy_config.rate;
    in->cfg.format = convert_alsa_format_2_audio_format(proxy_config.format);
    in->cfg.frame_size = in->cfg.channelCnt * pcm_format_to_bits(proxy_config.format) / 8;
    in->debug = 0;
    in->pcm_in_config = proxy_config;
    kara->mixout_config = *cfg;

#if (ANDROID_PLATFORM_SDK_VERSION > 33) || (ANDROID_PLATFORM_SDK_VERSION == 33 \
        && (ANDROID_PLATFORM_SDK_EXTENSION_VERSION >= 5))
    ret = proxy_prepare(&in->proxy, profile, &proxy_config, false);
#else
    ret = proxy_prepare(&in->proxy, profile, &proxy_config);
#endif
    if (ret < 0) {
        ALOGE("%s(), proxy prepare fail", __func__);
        goto err;
    }

    AM_LOGI(" proxy_prepare configs: channels %d format %d rate %d",
            proxy_config.channels, proxy_config.format, proxy_config.rate);
    AM_LOGI("in_configs: channels = %d, format = %d, rate = %d, frame_size = %d",
            in->cfg.channelCnt, in->cfg.format, in->cfg.sampleRate, in->cfg.frame_size);
    AM_LOGI(" mixout_configs: channels %d, format %d, rate %d, frame_size %d",
            cfg->channelCnt, cfg->format, cfg->sampleRate, cfg->frame_size);

    ret = proxy_open(&in->proxy);
    if (ret < 0) {
        ALOGE("%s(), proxy open fail", __func__);
        goto err;
    }

    in->conversion_buffer = NULL;
    in->conversion_buffer_size = 0;

    kara->buf = NULL;
    kara->buf_len = 0;
    int init_ret = ring_buffer_init(&kara->mic_buffer, USB_DEFAULT_PERIOD_SIZE * 32);
    if (init_ret == -1) {
        ALOGE("[%s:%d] init is error", __func__, __LINE__);
        pthread_mutex_unlock(&kara->lock);
        return -1;
    }
    kara->karaoke_start = true;

    pthread_mutex_unlock(&kara->lock);
    ALOGV("--%s()", __func__);

    return 0;
err:
    pthread_mutex_unlock(&kara->lock);
    kara->karaoke_start = false;
    return ret;
}

static int kara_close_micphone(struct kara_manager *kara)
{
    struct voice_in *in = &kara->in;

    ALOGV("++%s()", __func__);
    pthread_mutex_lock(&kara->lock);
    if (kara->karaoke_start == false) {
        ALOGI("%s() karaoke is closed!", __func__);
        pthread_mutex_unlock(&kara->lock);
        return 0;
    }
    if (KARA_TYPE_USB == kara->kara_type) {
        proxy_close(&in->proxy);
    } else if (KARA_TYPE_LINEIN == kara->kara_type) {
        if (in->pcm_handle != NULL) {
            pcm_close(in->pcm_handle);
            in->pcm_handle == NULL;
        }
    }

    aml_audio_free(in->conversion_buffer);
    in->conversion_buffer = NULL;
    in->conversion_buffer_size = 0;
    aml_audio_free(kara->buf);
    kara->buf = NULL;
    kara->buf_len = 0;
    ring_buffer_release(&kara->mic_buffer);
    kara->karaoke_start = false;
    pthread_mutex_unlock(&kara->lock);
    ALOGV("--%s()", __func__);

    return 0;
}

static int kara_mix_micphone(struct kara_manager *kara, void *buf, size_t bytes)
{
    if (!kara || !buf || 0 == bytes) {
        return -EINVAL;
    }
    if (0 == kara->mixout_config.frame_size) {
        return -EINVAL;
    }
    struct voice_in *in = &kara->in;
    size_t frames = bytes / kara->mixout_config.frame_size; // depend on main config
    ssize_t size_ret = 0;

    pthread_mutex_lock(&kara->lock);
    size_ret = voice_in_read(kara, frames);
    if (size_ret > 0) {
        //AM_LOGD("size_ret=%zu mute=%d", size_ret, kara->kara_mic_mute);
        if (kara->kara_mic_mute) {
            memset(kara->buf, 0, size_ret);
        } else {
#if 0
            if (kara->reverb_enable) {
                Set_AML_Reverb_Mode(kara->reverb_handle, kara->reverb_mode);
                AML_Reverb_Process(kara->reverb_handle, kara->buf, kara->buf, bytes >> 2);
            }
#endif
            apply_volume(kara->kara_mic_gain, kara->buf, pcm_format_to_bits(in->pcm_in_config.format) / 8, size_ret);
        }

        /* mixer to output */
        do_mixing_2ch(buf, kara->buf, frames, in->cfg.format, kara->mixout_config.format);

        if (KARA_TYPE_USB == kara->kara_type) {
            /* Save mic data to echo reference*/
            if (kara->echo_reference != NULL) {
                struct echo_reference_buffer b;
                b.raw = (void *)kara->buf;
                b.frame_count = frames;
                clock_gettime(CLOCK_REALTIME, &b.time_stamp);
                b.delay_ns = 0;
                kara->echo_reference->write(kara->echo_reference, &b);

                if (get_debug_value(AML_DUMP_AUDIOHAL_OUT) || in->debug) {
                    aml_dump_audio_bitstreams("/data/audio/usb_kara_echo_reference.pcm", kara->buf, bytes);
                }
            }
        }
    }

    pthread_mutex_unlock(&kara->lock);
    return size_ret;
}

static int linein_kara_open_micphone(struct kara_manager *kara, struct audioCfg *cfg)
{
    if (!cfg || !kara) {
        AM_LOGE("Input null pointer");
        return -EINVAL;
    }
    AM_LOGI("Enter");
    pthread_mutex_lock(&kara->lock);
    if (true == kara->karaoke_start) {
        AM_LOGI("%s() linein karaoke is already opened!", __func__);
        pthread_mutex_unlock(&kara->lock);
        return 0;
    }

    int ret = 0;
    struct voice_in *in = NULL;
    struct pcm_config pcm_linein_config;
    struct pcm *pcmIn = NULL;
    memset(&pcm_linein_config, 0, sizeof(pcm_linein_config));
    /*the record parameter depends on pcm in config */
    pcm_linein_config.channels = LINEIN_DEFAULT_CHANNEL;
    pcm_linein_config.rate = LINEIN_DEFAULT_SAMPLE_RATE;
    pcm_linein_config.format = LINEIN_DEFAULT_FORMAT;
    pcm_linein_config.period_size = LINEIN_DEFAULT_PERIOD_SIZE;
    pcm_linein_config.period_count = LINEIN_DEFAULT_PERIOD_COUNT;
    AM_LOGD("pcm_linein_config: channels %d, format %d, rate %d",
            pcm_linein_config.channels, pcm_linein_config.format, pcm_linein_config.rate);

    in = &kara->in;
    /* in config for channel adjust and mix */
    in->cfg.channelCnt = pcm_linein_config.channels;
    in->cfg.sampleRate = pcm_linein_config.rate;
    in->cfg.format = convert_alsa_format_2_audio_format(pcm_linein_config.format);
    in->cfg.frame_size = in->cfg.channelCnt * pcm_format_to_bits(pcm_linein_config.format) / 8;
    in->debug = 0;
    in->pcm_in_config = pcm_linein_config;
    kara->mixout_config = *cfg;

    int card = alsa_device_get_card_index();
    int device = alsa_device_update_pcm_index(PORT_I2S, CAPTURE);
    pcmIn = pcm_open(card, device, PCM_IN, &pcm_linein_config);
    if (!pcm_is_ready(pcmIn)) {
        AM_LOGI("pcm_is_ready error!");
        if (pcmIn != NULL) {
            pcm_close(pcmIn);
            pcmIn = NULL;
        }
        ret = -1;
        goto err;
    }
    in->pcm_handle = pcmIn;
    in->conversion_buffer = NULL;
    in->conversion_buffer_size = 0;
    kara->buf = NULL;
    kara->buf_len = 0;

    ret = ring_buffer_init(&kara->mic_buffer, LINEIN_DEFAULT_PERIOD_SIZE * 32);
    if (-1 == ret) {
        AM_LOGE("ring_buffer_init error");
        goto err;
    }

    AM_LOGI("in_configs: channels = %d, format = %d, rate = %d, frame_size = %d",
            in->cfg.channelCnt, in->cfg.format, in->cfg.sampleRate, in->cfg.frame_size);
    AM_LOGI("mixout_configs: channels = %d, format = %d, rate = %d, frame_size = %d",
            cfg->channelCnt, cfg->format, cfg->sampleRate, cfg->frame_size);

    kara->karaoke_start = true;
    pthread_mutex_unlock(&kara->lock);
    AM_LOGI("exit");
    return 0;
err:
    kara->karaoke_start = false;
    pthread_mutex_unlock(&kara->lock);
    return ret;
}


int karaoke_init(struct kara_manager *karaoke, alsa_device_profile *profile)
{
    if (!karaoke || !profile) {
        return -EINVAL;
    }

    int ret;
    ALOGI("%s()", __func__);
    karaoke->open = kara_open_micphone;
    karaoke->read = mic_buffer_read;
    karaoke->close = kara_close_micphone;
    karaoke->mix = kara_mix_micphone;
    karaoke->in.in_profile = profile;
#if 0
    if (!karaoke->reverb_handle) {
        ret = AML_Reverb_Init(&karaoke->reverb_handle);
        if (ret < 0) {
            ALOGE("%s() int Reverb Error!", __func__);
            return -EINVAL;
        }
    }
#endif
    return 0;
}

int linein_karaoke_init(struct kara_manager *karaoke)
{
    if (!karaoke) {
        return -EINVAL;
    }
    AM_LOGI("enter");
    karaoke->open = linein_kara_open_micphone;
    karaoke->read = mic_buffer_read;
    karaoke->close = kara_close_micphone;
    karaoke->mix = kara_mix_micphone;

    return 0;
}


static void add_echo_reference(struct kara_manager *kara,
                               struct echo_reference_itfe *reference)
{
    pthread_mutex_lock(&kara->lock);
    kara->echo_reference = reference;
    pthread_mutex_unlock(&kara->lock);
}

static void remove_echo_reference(struct kara_manager *kara,
                                  struct echo_reference_itfe *reference)
{
    pthread_mutex_lock(&kara->lock);
    if (kara->echo_reference == reference) {
        /* stop writing to echo reference */
        reference->write(reference, NULL);
        kara->echo_reference = NULL;
    }
    pthread_mutex_unlock(&kara->lock);
}

void put_echo_reference(struct kara_manager *kara,
                          struct echo_reference_itfe *reference)
{
    /*coverity[missing_lock]*/
    if (kara->echo_reference != NULL &&
            reference == kara->echo_reference) {
        remove_echo_reference(kara, reference);
        aml_release_echo_reference(reference);
        pthread_mutex_lock(&kara->lock);
        kara->echo_reference = NULL;
        pthread_mutex_unlock(&kara->lock);
    }
}

struct echo_reference_itfe *get_echo_reference(struct kara_manager *kara,
        audio_format_t format,
        uint32_t channel_count,
        uint32_t sampling_rate)
{
    struct echo_reference_itfe *echo = NULL;
    /*coverity[missing_lock]*/
    put_echo_reference(kara, kara->echo_reference);
    if (kara->karaoke_start) {
        uint32_t wr_channel_count = 2;//proxy_get_channel_count(&kara->in.proxy);
        uint32_t wr_sampling_rate = 48000;//proxy_get_sample_rate(&kara->in.proxy);
        ALOGI("%s() rd channel %d, rate %d, wr channel %d rate %d",
            __func__, channel_count, sampling_rate,
            wr_channel_count, wr_sampling_rate);

        int status = aml_create_echo_reference(AUDIO_FORMAT_PCM_16_BIT,
                channel_count,
                sampling_rate,
                format,
                wr_channel_count,
                wr_sampling_rate,
                &echo);
        if (status == 0) {
            add_echo_reference(kara, echo);
            ALOGI("%s() success", __func__);
        }
    }

    return echo;
}

int check_kara_mix_output(struct kara_manager *karaoke, void *buffer, size_t bytes)
{
    if (!karaoke || !buffer || 0 == bytes) {
        AM_LOGE("parameter invalid");
        return -EINVAL;
    }
    if (KARA_TYPE_USB != karaoke->kara_type && KARA_TYPE_LINEIN != karaoke->kara_type) {
        AM_LOGE("karaoke type invalid");
        return -EINVAL;
    }

    int ret = 0;
    if (KARA_TYPE_USB == karaoke->kara_type) {
//        AM_LOGI("[USB_KARA] karaoke_on=%d, karaoke_enable=%d, karaoke_start=%d",
//                 karaoke->karaoke_on, karaoke->karaoke_enable, karaoke->karaoke_start);
        if (karaoke->karaoke_on && karaoke->karaoke_enable &&
            karaoke->in.in_profile && profile_is_valid(karaoke->in.in_profile)) {
            if (!karaoke->karaoke_start && karaoke->open) {
                ret = karaoke->open(karaoke, &karaoke->mixout_config);
                if (ret < 0) {
                    AM_LOGE("karaoke open usb mic failed: %d", ret);
                }
            } else if (!ret && karaoke->mix) {
                karaoke->mix(karaoke, buffer, bytes);
            }
        } else if (karaoke->karaoke_start && karaoke->close) {
            karaoke->close(karaoke);
        }
    } else if (KARA_TYPE_LINEIN == karaoke->kara_type) {
//        AM_LOGI("[LINEIN_KARA] karaoke_on=%d, karaoke_enable=%d, karaoke_start=%d",
//                 karaoke->karaoke_on, karaoke->karaoke_enable, karaoke->karaoke_start);
        if (karaoke->karaoke_on && karaoke->karaoke_enable) {
            if (!karaoke->karaoke_start && karaoke->open) {
                ret = karaoke->open(karaoke, &karaoke->mixout_config);
                if (ret < 0) {
                    AM_LOGE("karaoke open linein input failed: %d", ret);
                }
            } else if (!ret && karaoke->mix) {
                karaoke->mix(karaoke, buffer, bytes);
            }
        } else if (karaoke->karaoke_start && karaoke->close) {
            karaoke->close(karaoke);
        }

    }

    return 0;
}

int get_audioCfg_from_ms12_info(struct audioCfg *cfg, struct aml_ms12_dec_info *ms12_info)
{
    int ret = 0;
    if (!cfg || !ms12_info) {
        AM_LOGE("parameter invalid");
        ret = -EINVAL;
        return ret;
    }
    cfg->format = ms12_info->data_type;
    cfg->channelCnt = ms12_info->output_ch;
    cfg->sampleRate = ms12_info->output_sr;
    cfg->frame_size = cfg->channelCnt *
                      pcm_format_to_bits(convert_audio_format_2_alsa_format(cfg->format)) / 8;

    AM_LOGI("format=%d, channel=%d, sampleRate=%d", cfg->format, cfg->channelCnt, cfg->sampleRate);
    return ret;
}

int karaoke_close(struct kara_manager *kara)
{
    int ret = 0;
    if (!kara || !kara->close) {
        ret = -ENOSYS;
        return ret;
    }

    if (kara->karaoke_start) {
        kara->close(kara);
    }
    return ret;
}

