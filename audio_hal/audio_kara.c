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

// stdc
#include <math.h>
#include <string.h>

#define LOG_TAG "audio_hw_hal_kara"
#include <cutils/log.h>
// android for memcpy_by_audio_format
#include <audio_utils/format.h>
#include <hardware/audio_alsaops.h> // for audio_format_from_pcm_format

// aml audio
#include "audio_hw_utils.h"
#include "audio_hal_debug.h"
#include <alsa_device_parser.h> // for alsa_device_get_card_index func
#include "audio_kara.h"
#include "audio_hw_resource_mgr.h"

#define DEBUG AM_LOGV // set AM_LOGD for debug, set AM_LOGV to suppress log
#define INFO  AM_LOGI
#define ERROR AM_LOGE

// util function
static int bs_dump(const char *label, struct pcm_config *cfg, const void *buf, size_t frames)
{
    char s[PCM_CONFIG_STR_LEN];
    snprintf(s, PCM_CONFIG_STR_LEN, "/data/audio/%s.%dhz_%dch_%dfmt.raw",
             label, cfg->rate, cfg->channels, cfg->format);
    return aml_audio_dump_audio_bitstreams(s, buf, frames_to_bytes(cfg, frames));
}

/** mic
 *
 * mic_open,close,pop is interface for line-in/usb-in
 * `mic_open` with main_cfg, so mic's config is as consistent as possible to main_cfg
 * `mic_pop_with_cfg` read data from mic device, and convert to main_cfg
 *
 * WHY this?
 * mixing stream with different channel, pcm format is easy;
 * but mixing stream with different rate is hard, due to buffer management
 * and resampling step cost extra delay and high cpu loading
 *
 */
struct mic_hdr_t {
    int mix_src;
    struct pcm_config pcm_cfg;
    void *buf;
    size_t sz;
};

struct pcm_mic_t {
    int mix_src;

    struct pcm_config pcm_cfg;
    void *buf;
    size_t sz;

    struct pcm *pcm;
};

struct usb_mic_t {
    int mix_src;

    struct pcm_config pcm_cfg;
    void *buf;
    size_t sz;

    alsa_device_profile profile;
    alsa_device_proxy proxy;
};

/** profile + main_cfg => cfg */
static void usb_mic_nego_cfg(int card, int device,
                             alsa_device_profile *profile,
                             struct pcm_config *main_cfg,
                             struct pcm_config *cfg)
{
    /** profile, pcm_cfg */
    profile_init(profile, PCM_IN);
    profile->card = card;
    profile->device = device;
    profile_read_device_info(profile);
    if (profile_is_sample_rate_valid(profile, main_cfg->rate)) {
        cfg->rate = main_cfg->rate;
    } else {
        // TODO
        // mic sample rate is different with playback's sample rate, we must do resampler later
        cfg->rate = profile_get_default_sample_rate(profile);
    }
    if (profile_is_format_valid(profile, main_cfg->format)) {
        cfg->format = main_cfg->format;
    } else {
        cfg->format = profile_get_default_format(profile);
    }
    if (profile_is_channel_count_valid(profile, main_cfg->channels)) {
        cfg->channels = main_cfg->channels;
    } else {
        cfg->channels = profile_get_default_channel_count(profile);
    }
    INFO("card=%d device=%d format,rate,ch=%d,%d,%d -> %d,%d,%d",
         card, device, main_cfg->format, main_cfg->rate, main_cfg->channels,
         cfg->format, cfg->rate, cfg->channels);
}

static void *usb_mic_open(int card, int device, struct pcm_config *main_cfg)
{
    struct usb_mic_t *m = calloc(1, sizeof(struct usb_mic_t));
    if (m == NULL) {
        return NULL;
    }
    m->mix_src = MIX_SRC_USBIN;
    usb_mic_nego_cfg(card, device, &m->profile, main_cfg, &m->pcm_cfg);
    char s_main[PCM_CONFIG_STR_LEN], s_ret[PCM_CONFIG_STR_LEN];
    INFO("main %s mic %s",
         show_pcm_config(main_cfg, s_main, PCM_CONFIG_STR_LEN),
         show_pcm_config(&m->pcm_cfg, s_ret, PCM_CONFIG_STR_LEN));
    /** prepare proxy */
    int ret = 0;
#if (ANDROID_PLATFORM_SDK_VERSION > 33) || (ANDROID_PLATFORM_SDK_VERSION == 33 \
            && (ANDROID_PLATFORM_SDK_EXTENSION_VERSION >= 5))
    ret = proxy_prepare(&m->proxy, &m->profile, &m->pcm_cfg, false /* exact match */);
#else
    ret = proxy_prepare(&m->proxy, &m->profile, &m->pcm_cfg);
#endif
    if (ret != 0) {
        ERROR("fail to prepare proxy for usb mic, ret=%d", ret);
        free(m);
        return NULL;
    }

    ret = proxy_open(&m->proxy);
    if (ret != 0) {
        ERROR("fail to open proxy for usb mic, ret=%d", ret);
        free(m);
        return NULL;
    }
    pcm_get_config(m->proxy.pcm, &m->pcm_cfg);
    INFO("pcm=%p %s", m->proxy.pcm, show_pcm_config(&m->pcm_cfg, s_ret, PCM_CONFIG_STR_LEN));
    return m;
}

static void usb_mic_close(struct usb_mic_t *m)
{
    proxy_close(&m->proxy);
    free(m);
}

static int usb_mic_pop_with_cfg(struct usb_mic_t *m, void **data, size_t frames, struct pcm_config *cfg)
{
    char s[PCM_CONFIG_STR_LEN];
    DEBUG("m=%p data=%p frames=%zu cfg=%p/%s",
          m, data, frames, cfg, show_pcm_config(cfg, s, PCM_CONFIG_STR_LEN));
    if (cfg->rate != m->pcm_cfg.rate) {
        ERROR("TODO: add resampler, fail to convert rate from %d to %d", m->pcm_cfg.rate, cfg->rate);
        return 0;
    }
    if (m->buf == NULL) {
        m->sz = frames_to_bytes(&m->pcm_cfg, frames);
        m->buf = malloc(m->sz);
        if (m->buf == NULL) {
            ERROR("fail to malloc buffer for pcm_mic");
            return 0;
        }
        INFO("m=%p buf=%p sz=%zu", m, m->buf, m->sz);
    }
    int ret = proxy_read(&m->proxy, m->buf, m->sz);
    if (ret != 0) {
        ERROR("fail to read usb_mic ret=%d", ret);
    }
    *data = m->buf;
    *cfg = m->pcm_cfg;
    return frames;
}

static void pcm_mic_nego_cfg(struct pcm_config *main_cfg, struct pcm_config *cfg)
{
    *cfg = *main_cfg;
    cfg->channels = 2; // same config, but different channels
}

/** PCM mic for line-in */
static void *pcm_mic_open(int card, int device, struct pcm_config *main_cfg)
{
    struct pcm_mic_t *m = malloc(sizeof(struct pcm_mic_t));
    m->mix_src = MIX_SRC_LINEIN;
    pcm_mic_nego_cfg(main_cfg, &m->pcm_cfg);
    char s_main[PCM_CONFIG_STR_LEN], s_ret[PCM_CONFIG_STR_LEN];
    INFO("main %s mic %s",
         show_pcm_config(main_cfg, s_main, PCM_CONFIG_STR_LEN),
         show_pcm_config(&m->pcm_cfg, s_ret, PCM_CONFIG_STR_LEN));
    m->pcm = pcm_open(card, device, PCM_IN, &m->pcm_cfg);
    if (m->pcm == NULL || !pcm_is_ready(m->pcm)) {
        ERROR("pcm_mic_open error: %p", m->pcm);
        if (m->pcm != NULL) {
            pcm_close(m->pcm);
        }
        free(m);
        return NULL;
    }
    pcm_get_config(m->pcm, &m->pcm_cfg);
    INFO("pcm=%p %s", m->pcm, show_pcm_config(&m->pcm_cfg, s_ret, PCM_CONFIG_STR_LEN));
    return m;
}

static void pcm_mic_close(struct pcm_mic_t *m)
{
    INFO("close m=%p pcm=%p", m, m->pcm);
    pcm_close(m->pcm);
    free(m);
}

// read data as cfg
static int pcm_mic_pop_with_cfg(struct pcm_mic_t *m, void **data, size_t frames, struct pcm_config *cfg)
{
    char s[PCM_CONFIG_STR_LEN];
    DEBUG("m=%p data=%p frames=%zu cfg=%p/%s",
          m, data, frames, cfg, show_pcm_config(cfg, s, PCM_CONFIG_STR_LEN));
    if (cfg->rate != m->pcm_cfg.rate) {
        ERROR("fail to convert rate from %d to %d", m->pcm_cfg.rate, cfg->rate);
        return 0;
    }
    if (m->buf == NULL) {
        m->sz = frames_to_bytes(&m->pcm_cfg, frames);
        m->buf = malloc(m->sz);
        if (m->buf == NULL) {
            ERROR("fail to malloc buffer for pcm_mic");
            return 0;
        }
        INFO("m=%p buf=%p sz=%zu", m, m->buf, m->sz);
    }
    pcm_read(m->pcm, m->buf, m->sz);
    *data = m->buf;
    *cfg = m->pcm_cfg; // copy struct pcm_config
    DEBUG("m=%p return data=%p fr=%zu sz=%zu cfg=%s",
          m, m->buf, frames, m->sz, show_pcm_config(cfg, s, PCM_CONFIG_STR_LEN));
    return frames;
}


/*

      48k,2ch,16b   +------+     44.1k,2ch,16b
            +--<----| MIC  |------------------
            |       +------+
            |
   main   .---.     +------+       48k,8ch,32b
   -------| + |---->| main |------------------
          '---'     +------+

- main_cfg: It's individual pcm device, open by external
- mic_cfg, is come from main_cfg, and negoate with device
- intermediate, same with mic_cfg + main_cfg'sample rate
- pop_with_cfg, final one


              mic
            +--<---
            |
   main   .---.
   -------| + |---->
          '---'
*/
/*
                                          +----------+
                   +----------/-------<---| LINE in  |
                   |                      +----------+
                   |                      +-------------+
                   |     +----/-------<---| CM USB mic1 |
                   |     |                +-------------+
                   v     v
  System         .---. .---.              +-----------------+
  ---->---[1]----| + |-| + |-[2]----+-->--| HDMI TX/speaker |
  HDMI RX        '---' '---'        |     +-----------------+
                                    V
                                    |
                                  .---.
  ----<---[2+3]-------------------| + |
  echo_ref[+usb_in]               '---'
                                    |
                                    /
                                    |     +----------+
  ----<---[3]-----------------------+--<--| USB mic2 |
  mic                                     +----------+
 */
/**
 * @brief This is general mic (line-in, usb-in) interface
 *
 * @param mix_src line-in, usb-in
 * @param card the card number
 * @param device The audio device number
 * @param main_cfg main path's pcm config
 * To achieve low-latency, internal mic is config based on main_cfg
 * For example, same period config, same rate;
 * If mic cannot support same config, it may adjust to similar config
 * For example, mic is 2channels, while main path is 8channels
 *
 * @return mic instance, NULL if fail to open mic device
 */
static void *mic_open(int mix_src, int card, int device, struct pcm_config *main_cfg) {
    switch (mix_src) {
    case MIX_SRC_LINEIN:
        return pcm_mic_open(card, device, main_cfg);
    case MIX_SRC_USBIN:
        return usb_mic_open(card, device, main_cfg);
    default:
        ERROR("unsupported mix_src=%d", mix_src);
        return NULL;
    }
}

static void mic_close(void *mic) {
    struct mic_hdr_t *m = mic;
    switch (m->mix_src) {
    case MIX_SRC_LINEIN:
        pcm_mic_close(mic);
        break;
    case MIX_SRC_USBIN:
        usb_mic_close(mic);
        break;
    default:
        ERROR("unsupported mix_src=%d", m->mix_src);
        break;
    }
}

static int mic_pop_with_cfg(void *mic, void **data, size_t frames, struct pcm_config *cfg) {
    struct mic_hdr_t *m = mic;
    switch (m->mix_src) {
    case MIX_SRC_LINEIN:
        return pcm_mic_pop_with_cfg(mic, data, frames, cfg);
    case MIX_SRC_USBIN:
        return usb_mic_pop_with_cfg(mic, data, frames, cfg);
    default:
        ERROR("unsupported mix_src=%d", m->mix_src);
        return -1;
    }
}

/** kara
 *
 * audio_kara_mix function
 * # read data from mic via mic_pop
 * # mix stream
 */
/** Stream format, channel, sample rate negotiation
 *
 * [2] is followed [1], LINE-in, Customized USB mic is 48K, 16bit, 2ch in general
 * [3]'s format may various format, due to third-party USB mic
 */
struct kara_t {
    struct pcm_config main_cfg;

    // PCM(linein) or USB
    void *mic;

    // intermediated buffer
    void *tmp_buf;
    size_t tmp_buf_sz;
};

void *audio_kara_open(int mix_src, int card, int device, struct pcm_config *main_cfg)
{
    char s[PCM_CONFIG_STR_LEN];
    struct kara_t *k = malloc(sizeof(struct kara_t));
    if (k == NULL) {
        ERROR("fail to alloc for kara_t");
        return NULL;
    }
    k->main_cfg = *main_cfg;
    k->mic = mic_open(mix_src, card, device, main_cfg);
    if (k->mic == NULL) {
        ERROR("fail to open mix src=%d dev=%d,%d main=%s",
              mix_src, card, device, show_pcm_config(main_cfg, s, PCM_CONFIG_STR_LEN));
        free(k);
        return NULL;
    }
    k->tmp_buf = NULL;
    k->tmp_buf_sz = 0;
    INFO("audio_kara open succ, src=%d dev=%d,%d main=%s",
         mix_src, card, device, show_pcm_config(main_cfg, s, PCM_CONFIG_STR_LEN));
    return k;
}

void audio_kara_close(void *kara) {
    struct kara_t *k = kara;

    if (k->tmp_buf) {
        free(k->tmp_buf);
        k->tmp_buf_sz = 0;
    }
    mic_close(k->mic);
    free(k);
}

/**
 * if (different format) {
 *     B = (format)B (via tmp_buf)
 * }
 * A += B
 */
// TODO: indicate base_ch from param
static void *mix_channel_format(void *a, struct pcm_config *a_cfg,
                                void *b, struct pcm_config *b_cfg,
                                size_t frames,
                                void **tmp_buf, size_t *tmp_sz)
{
    if (b_cfg->format != a_cfg->format) {
        // B = (format)B
        *tmp_buf = chk_alloc(*tmp_buf, tmp_sz, frames * bytes_per_frame(a_cfg));
        memcpy_by_audio_format(*tmp_buf, audio_format_from_pcm_format(a_cfg->format),
                               b, audio_format_from_pcm_format(b_cfg->format),
                               frames * b_cfg->channels);
        b = *tmp_buf;
    }
    // same format, maybe different channels
    size_t min_ch = MIN(a_cfg->channels, b_cfg->channels);
    size_t i, j;
    if (a_cfg->format == PCM_FORMAT_S16_LE) {
        ERROR("TODO: support 16bit");
    } else if (a_cfg->format == PCM_FORMAT_S32_LE) {
        size_t i, j;
        int32_t *d = (int32_t *)a, *s = (int32_t *)b;
        for (i = 0; i != frames; i++) {
            const size_t base_ch = 2;
            for (j = base_ch; j != base_ch + min_ch; j++) {
                d[j] = CLIP32((int64_t)d[j] + (int64_t)s[j - base_ch]);
            }
            d += a_cfg->channels;
            s += b_cfg->channels;
        }
        char sa[PCM_CONFIG_STR_LEN], sb[PCM_CONFIG_STR_LEN];
        DEBUG("a=%p/%s b=%p/%s fr=%zu",
              a, show_pcm_config(a_cfg, sa, PCM_CONFIG_STR_LEN),
              b, show_pcm_config(b_cfg, sb, PCM_CONFIG_STR_LEN),
              frames);
    } else {
        ERROR("unsupported output format=%d", a_cfg->format);
    }
    return a;
}

/*
 * - Read mic data
 * - Mix to data
 * - And let audio_port part output mixed data to HDMI-out path
 */
size_t audio_kara_mix(void *kara, void *data, size_t bytes) {
    struct kara_t *k = kara;
    if (k->mic == NULL) {
        return 0;
    }
    size_t fr = bytes_to_frames(&k->main_cfg, bytes);
    int r;
    void *mic_data = NULL;
    struct pcm_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.rate = k->main_cfg.rate;
    r = mic_pop_with_cfg(k->mic, &mic_data, fr, &cfg);
    int dump = 0;
    if (getprop_bool("vendor.media.audiohal.kara")) {
        dump++;
        bs_dump("mic", &cfg, mic_data, fr);
        bs_dump("main", &k->main_cfg, data, fr);
    }
    mix_channel_format(data, &k->main_cfg,
                       mic_data, &cfg,
                       fr, &k->tmp_buf, &k->tmp_buf_sz);
    if (getprop_bool("vendor.media.audiohal.kara")) {
        dump++;
        bs_dump("mixed", &k->main_cfg, data, fr);
    }
    DEBUG("mix data=%p mic=%p bytes=%zu fr=%zu dump=%d",
          data, mic_data, bytes, fr, dump);
    return bytes;
}

int audio_kara_get_src(void *kara)
{
    struct kara_t *k = kara;
    struct mic_hdr_t *m = k->mic;
    return m->mix_src;
}

/** helper function for audio_hw layer */
void check_switch_audio_kara(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct pcm_config cfg;
    int active_src = -1;
    if (aml_out->kara != NULL) {
        active_src = audio_kara_get_src(aml_out->kara);
        if (adev->output_mix_source != active_src) {
            AM_LOGI("active_src=%d set_src=%d, close it", active_src, adev->output_mix_source);
            audio_kara_close(aml_out->kara);
            aml_out->kara = NULL;
        } else {
            // AM_LOGE("audio_kara=%p active=%d set=%d SAME, do nothing",
            //        aml_out->audio_kara, active_src, adev->output_mix_source);
        }
    }
    if (aml_out->kara == NULL) {
        if (adev->output_mix_source == MIX_SRC_NIL) {
            // AM_LOGE("audio_kara=NULL set=NIL, do nothing");
        } else if (adev->output_mix_source == MIX_SRC_LINEIN) {
            pcm_get_config(aml_out->pcm, &cfg);

            do_input_device_routing(adev, AUDIO_DEVICE_IN_LINE, true);

            int card = alsa_device_get_card_index();
            int device = alsa_device_update_pcm_index(PORT_I2S, CAPTURE);

            aml_out->kara = audio_kara_open(MIX_SRC_LINEIN, card, device, &cfg);
            AM_LOGI("set_src=%d dev=%d,%d kara=%p",
                    adev->output_mix_source, card, device,
                    aml_out->kara);
        } else if (adev->output_mix_source == MIX_SRC_USBIN) {
            pcm_get_config(aml_out->pcm, &cfg);
            aml_out->kara = audio_kara_open(MIX_SRC_USBIN, adev->customized_usb_card,
                                        adev->customized_usb_device, &cfg);
            AM_LOGI("set_src=%d dev=%d,%d kara=%p",
                    adev->output_mix_source, adev->customized_usb_card, adev->customized_usb_device,
                    aml_out->kara);
        }
    }
}

int set_param_kara(struct audio_hw_device *dev, struct str_parms *parms)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    char value[AUDIO_HAL_CHAR_MAX_LEN] = {'\0'};
    int val = 0, ret = 0;

    ret = str_parms_get_int(parms, "customized_usb_card", &val);
    if (ret >= 0) {
        adev->customized_usb_card = val;
        AM_LOGI("customized_usb_card=%d", adev->customized_usb_card);
    }

    ret = str_parms_get_int(parms, "customized_usb_device", &val);
    if (ret >= 0) {
        adev->customized_usb_device = val;
        AM_LOGI("customized_usb_device=%d", adev->customized_usb_device);
    }
    ret = str_parms_get_str(parms, "output_mix_source", value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, "linein") == 0) {
            adev->customized_usb_card = adev->customized_usb_device = -1;
            adev->output_mix_source = MIX_SRC_LINEIN;
        } else if (strcmp(value, "usbin") == 0) {
            adev->output_mix_source = MIX_SRC_USBIN;
        } else if (strcmp(value, "null") == 0) {
            adev->customized_usb_card = adev->customized_usb_device = -1;
            adev->output_mix_source = MIX_SRC_NIL;
        }
        AM_LOGI("output_mix_source=%d cm_usb=%d:%d",
                adev->output_mix_source,
                adev->customized_usb_card, adev->customized_usb_device);
        return 0;
    }
    return -1;
}
