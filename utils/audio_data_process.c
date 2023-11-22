/*
* Copyright (c) 2020 Amlogic, Inc. All rights reserved.
* *
This source code is subject to the terms and conditions defined in the
* file 'LICENSE' which is part of this source code package.
* *
Description:
*/


#define LOG_TAG "audio_hw_utils_dataprocess"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <cutils/log.h>
#include <system/audio.h>
#include <audio_utils/primitives.h>

#include "aml_malloc_debug.h"
#include "audio_data_process.h"


#ifndef AM_LOGV
#define AM_LOGV(fmt, ...)  ALOGV("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#endif
#ifndef AM_LOGD
#define AM_LOGD(fmt, ...)  ALOGD("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#endif
#ifndef AM_LOGI
#define AM_LOGI(fmt, ...)  ALOGI("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#endif
#ifndef AM_LOGW
#define AM_LOGW(fmt, ...)  ALOGW("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#endif
#ifndef AM_LOGE
#define AM_LOGE(fmt, ...)  ALOGE("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)
#endif

#define MINUS_3_DB_IN_FLOAT 0.70710678f // -3dB = 0.70710678f
#define MINUS_6_DB_IN_FLOAT 0.5f        // -6dB = 0.5f


extern void memcpy_by_audio_format(void *dst, audio_format_t dst_format,
          const void *src, audio_format_t src_format, size_t count);


static int ch2_ch8_n_b16_b32(void *data_mixed, void *data_sys, size_t frames)
{
    uint32_t *out_buf = data_mixed;
    uint16_t *in_buf = data_sys;
    uint i = 0;
    for (i = 0; i < frames; i++) {
        out_buf[8 * i] = in_buf[2 * i] << 16;
        out_buf[8 * i + 1] = in_buf[2 * i + 1] << 16;
        out_buf[8 * i + 2] = in_buf[2 * i] << 16;
        out_buf[8 * i + 3] = in_buf[2 * i + 1] << 16;
        out_buf[8 * i + 4] = in_buf[2 * i] << 16;
        out_buf[8 * i + 5] = in_buf[2 * i + 1] << 16;
        out_buf[8 * i + 6] = in_buf[2 * i] << 16;
        out_buf[8 * i + 7] = in_buf[2 * i + 1] << 16;
    }
    return 0;
}

static inline short CLIPSHORT(int32_t r)
{
    if (r > 32767)
        r = 32767;
    else if (r < -32768)
        r = -32768;
    return r;
}

static inline int CLIPINT(int64_t r)
{
    if (r > 2147483647)
        r = 2147483647;
    else if (r < -2147483648)
        r = -2147483648;
    return r;
}

//should be same channelCnt 2
int do_mixing_2ch(void *data_mixed,
        void *data_in, size_t frames,
        audio_format_t in_format, audio_format_t out_format)
{
    int i = 0;
    if (out_format == AUDIO_FORMAT_PCM_32_BIT) {
        if (in_format == AUDIO_FORMAT_PCM_16_BIT) {
            int16_t *in = data_in;
            int32_t *out = data_mixed;
            int64_t tmp = 0;
            for (i = 0; i < frames * 2; i++) {
                tmp = (int64_t)*out + (int64_t)((*in++) << 16);
                *out++ = CLIPINT(tmp);
            }
        } else if (in_format == AUDIO_FORMAT_PCM_32_BIT) {
            int32_t *in = data_in;
            int32_t *out = data_mixed;
            int64_t tmp = 0;
            for (i = 0; i < frames * 2; i++) {
                tmp = (int64_t)*out + (int64_t)*in++;
                *out++ = CLIPINT(tmp);
            }
        }
    } else if (out_format == AUDIO_FORMAT_PCM_16_BIT) {
        if (in_format == AUDIO_FORMAT_PCM_16_BIT) {
            int16_t *in = data_in;
            int16_t *out = data_mixed;
            int32_t tmp = 0;
            for (i = 0; i < frames * 2; i++) {
                tmp = (int32_t)*out + (int32_t)*in++;
                *out++ = CLIPSHORT(tmp);
            }
        } else if (in_format == AUDIO_FORMAT_PCM_32_BIT) {
            int32_t *in = data_in;
            int16_t *out = data_mixed;
            int32_t tmp = 0;
            for (i = 0; i < frames * 2; i++) {
                tmp = (int32_t)*out + ((*in++) >> 16);
                *out++ = CLIPSHORT(tmp);
            }
        }
    } else {
        ALOGE("do_mixing_2ch invalid in_format:%#x out_format:%#x invalid", in_format, out_format);
        return 0;
    }
    return frames;
}
// 2->8ch, 32bit, in out no overlap
int extend_channel_2_8(void *data_out, void *data_in,
        size_t frames, int ch_cnt_out, int ch_cnt_in)
{
    (void) ch_cnt_out;
    (void) ch_cnt_in;
    int32_t *in = (int32_t *)data_in;
    int32_t *out = (int32_t *)data_out;
    int32_t Lval = 0, Rval = 0;
    uint i = 0 , j = 0;

    for (i = 0; i < frames; i++) {
        Lval = *in++;
        Rval = *in++;
        for (j = 0; j < 4; j++) {
            *out++ = Lval;
            *out++ = Rval;
        }
    }
    return 0;
}

int extend_channel_5_8(void *data_out, void *data_in,
        size_t frames, int ch_cnt_out, int ch_cnt_in)
{
    (void) ch_cnt_out;
    (void) ch_cnt_in;
    int32_t *out = data_out;
    int32_t *in = data_in;
    uint i = 0;
    for (i = 0; i < frames; i++) {
        out[8 * i] = in[5 * i + 2];
        out[8 * i + 1] = in[5 * i + 3];
        out[8 * i + 2] = in[5 * i];
        out[8 * i + 3] = in[5 * i + 1];
        out[8 * i + 4] = in[5 * i + 4];
        out[8 * i + 5] = 0;
        out[8 * i + 6] = 0;
        out[8 * i + 7] = 0;
    }

    return 0;
}


int processing_and_convert(void *data_mixed,
        void *data_sys, size_t frames,
        struct audioCfg inCfg, struct audioCfg mixerCfg)
{
    if (data_mixed == NULL || data_sys == NULL) {
        ALOGE("%s(), NULL pointer", __func__);
        return -EINVAL;
    }

    if (inCfg.format == AUDIO_FORMAT_PCM_16_BIT && mixerCfg.format == AUDIO_FORMAT_PCM_32_BIT
            && inCfg.channelCnt == 2 && mixerCfg.channelCnt == 8) {
        ch2_ch8_n_b16_b32(data_mixed, data_sys, frames);
    } else {
        ALOGE("%s(), not support", __func__);
    }

    return 0;
}

/*
 * convert L R C LFE *** -> L R LFE C **
 *
 */
void channel_layout_swap_center_lfe(void * data, int size, int channels) {
    int i = 0;
    int16_t *in_buf = data;
    int16_t temp = 0;
    int frames = 0;

    if (data == NULL) {
        ALOGE("%s(), NULL pointer", __func__);
        return;
    }
    if (channels == 0 || size <= 0) {
        ALOGE("%s() channels=%d size=%d", __func__, channels, size);
        return;
    }

    frames = size / (channels * 2);
    for (i = 0; i < frames; i++) {
        temp = in_buf[channels * i + 2];
        in_buf[channels * i + 2] = in_buf[channels * i + 3];
        in_buf[channels * i + 3] = temp;
    }
}


int init_aml_pcm_mixer(aml_pcm_mixing_st *p_mixer, const struct audioCfg *p_mixer_cfg, int mixed_frames)
{
    int i = 0;

    memset(p_mixer, 0, sizeof(*p_mixer));
    p_mixer->mixed_frame_size = p_mixer_cfg->channelCnt * audio_bytes_per_sample(p_mixer_cfg->format);
    p_mixer->mixed_buf_size = mixed_frames * p_mixer->mixed_frame_size;
    p_mixer->mixed_frames = mixed_frames;
    p_mixer->mixed_buf = aml_audio_realloc(p_mixer->mixed_buf, p_mixer->mixed_buf_size);
    if (p_mixer->mixed_buf == NULL) {
        AM_LOGE("allocate mixed_buf no memory");
        return -1;
    }
    memcpy(&p_mixer->cfg, p_mixer_cfg, sizeof(p_mixer->cfg));
    return 0;
}


void deinit_aml_pcm_mixer(aml_pcm_mixing_st *p_mixer)
{
    if (p_mixer->mixed_buf && !p_mixer->mixed_buf_is_static) {
        aml_audio_free(p_mixer->mixed_buf);
    }
    if (p_mixer->format_buf) {
        aml_audio_free(p_mixer->format_buf);
    }
    if (p_mixer->channel_buf) {
        aml_audio_free(p_mixer->channel_buf);
    }
    memset(p_mixer, 0, sizeof(*p_mixer));
}


void set_pcm_mixing_base(aml_pcm_mixing_st *p_mixer, struct audioCfg *p_data_cfg, void *p_in_data, size_t data_bytes)
{
    int i = 0;

    p_mixer->mixed_frame_size = p_data_cfg->channelCnt * audio_bytes_per_sample(p_data_cfg->format);
    p_mixer->mixed_buf_size = data_bytes;
    p_mixer->mixed_frames = data_bytes/p_mixer->mixed_frame_size;
    p_mixer->mixed_buf = p_in_data;
    p_mixer->mixed_buf_is_static = 1;
    memcpy(&p_mixer->cfg, p_data_cfg, sizeof(p_mixer->cfg));
}



int do_mixing_multi_ch(aml_pcm_mixing_st *p_mixer, void *p_in_data, size_t in_frames, struct audioCfg *p_in_cfg)
{
    int i = 0;
    int ret = 0;
    size_t req_buf_size = 0;
    void *data_src = p_in_data;
    void *mixer_src = p_in_data;

    audio_format_t in_format = p_in_cfg->format;
    uint32_t in_channelCnt = p_in_cfg->channelCnt;
    audio_channel_mask_t in_channelMask = p_in_cfg->channelMask;
    audio_format_t out_format = p_mixer->cfg.format;
    uint32_t out_channelCnt = p_mixer->cfg.channelCnt;
    audio_channel_mask_t out_channelMask = p_mixer->cfg.channelMask;

    if (in_frames > p_mixer->mixed_frames) {
        AM_LOGE("in_frames(%d > %d) is too large !", in_frames, p_mixer->mixed_frames);
        return -1;
    }

    if (out_format != in_format) {
        req_buf_size = in_frames * audio_bytes_per_frame(in_channelCnt, out_format);
        ret = aml_audio_check_and_realloc(&p_mixer->format_buf, &p_mixer->format_buf_size, req_buf_size);
        if ((ret != 0) || (p_mixer->format_buf == NULL)) {
            AM_LOGE("allocate format_buf(%d bytes) failed", req_buf_size);
            return -1;
        }
        data_src = p_mixer->format_buf;
        memcpy_by_audio_format(data_src, out_format, p_in_data, in_format, in_channelCnt*in_frames);
    }

    if (in_channelMask == out_channelMask) {
        mixer_src = data_src;
    } else {
        int out_sample_size = audio_bytes_per_sample(out_format);
        req_buf_size = out_sample_size * out_channelCnt * in_frames;
        ret = aml_audio_check_and_realloc(&p_mixer->channel_buf, &p_mixer->channel_buf_size, req_buf_size);
        if ((ret != 0) || (p_mixer->channel_buf == NULL)) {
            AM_LOGE("allocate channel_buf(%d bytes) failed", req_buf_size);
            return -1;
        }
        mixer_src = p_mixer->channel_buf;
        memcpy_by_channel_mask(mixer_src, out_channelMask, data_src, in_channelMask, out_sample_size, in_frames);
    }

    if (out_format == AUDIO_FORMAT_PCM_16_BIT) {
        int16_t *in = mixer_src;
        int16_t *out = p_mixer->mixed_buf;
        int64_t tmp = 0;
        for (i = 0; i < in_frames * out_channelCnt; i++) {
            tmp = (int64_t)*out + (int64_t)*in++;
            *out++ = CLIPSHORT(tmp);
        }
    } else if (out_format == AUDIO_FORMAT_PCM_32_BIT) {
        int32_t *in = mixer_src;
        int32_t *out = p_mixer->mixed_buf;
        int64_t tmp = 0;
        for (i = 0; i < in_frames * out_channelCnt; i++) {
            tmp = (int64_t)*out + (int64_t)*in++;
            *out++ = CLIPINT(tmp);
        }
    } else {
        AM_LOGE("not support p_mixer format:0x%x", out_format);
        return -1;
    }
    return in_frames;
}


void init_aml_pcm_downmix(aml_pcm_downmix_st *p_downmix)
{
    int i = 0;

    memset(p_downmix, 0, sizeof(*p_downmix));
    for (i = 0; i < MAX_INPUT_CHANNELS_SUPPORTED; i++) {
        p_downmix->mMatrix[i][0] = 0;
        p_downmix->mMatrix[i][1] = 0;
    }
    p_downmix->mInputChannelMask = AUDIO_CHANNEL_NONE;
    p_downmix->mLastValidChannelIndexPlusOne = 0;
    p_downmix->mInputChannelCount = 0;
}

void deinit_aml_pcm_downmix(aml_pcm_downmix_st *p_downmix)
{
    if (p_downmix->output_buf) {
        aml_audio_free(p_downmix->output_buf);
    }
    memset(p_downmix, 0, sizeof(*p_downmix));
}


/*
 * reference : system/media/audio_utils/include/audio_utils/ChannelMix.h
*/
static bool specificProcess_16bit(const int16_t *src, int16_t *dst, size_t frameCount, int channelCount)
{
    while (frameCount > 0) {
        int64_t ch[2]; // left, right
        if (channelCount == 4) { // QUAD
            // sample at index 0 is FL
            // sample at index 1 is FR
            // sample at index 2 is RL (or SL)
            // sample at index 3 is RR (or SR)
            // FL + RL
            ch[0] = src[0] + src[2] * MINUS_3_DB_IN_FLOAT;
            // FR + RR
            ch[1] = src[1] + src[3] * MINUS_3_DB_IN_FLOAT;
        } else if  (channelCount == 6) { // 5.1
            // sample at index 0 is FL
            // sample at index 1 is FR
            // sample at index 2 is FC
            // sample at index 3 is LFE
            // sample at index 4 is RL (or SL)
            // sample at index 5 is RR (or SR)
            const int64_t centerPlusLfeContrib = src[2] + src[3] * MINUS_3_DB_IN_FLOAT;
            // FL + RL + centerPlusLfeContrib
            ch[0] = src[0] + (src[4] + centerPlusLfeContrib) * MINUS_3_DB_IN_FLOAT;
            // FR + RR + centerPlusLfeContrib
            ch[1] = src[1] + (src[5] + centerPlusLfeContrib) * MINUS_3_DB_IN_FLOAT;
        } else if (channelCount == 8) { // 7.1
            // sample at index 0 is FL
            // sample at index 1 is FR
            // sample at index 2 is FC
            // sample at index 3 is LFE
            // sample at index 4 is RL
            // sample at index 5 is RR
            // sample at index 6 is SL
            // sample at index 7 is SR
            const int64_t centerPlusLfeContrib = src[2] + src[3] * MINUS_3_DB_IN_FLOAT;
            // FL + RL + SL + centerPlusLfeContrib
            ch[0] = src[0] + (src[4] + src[6] + centerPlusLfeContrib) * MINUS_3_DB_IN_FLOAT;
            // FR + RR + SR + centerPlusLfeContrib
            ch[1] = src[1] + (src[5] + src[7] + centerPlusLfeContrib) * MINUS_3_DB_IN_FLOAT;
        } else {
            return false;
        }
        dst[0] = CLIPSHORT(ch[0]);
        dst[1] = CLIPSHORT(ch[1]);

        src += channelCount;
        dst += 2;
        --frameCount;
    }
    return true;
}


static bool specificProcess_32bit(const int32_t *src, int32_t *dst, size_t frameCount, int channelCount)
{
    while (frameCount > 0) {
        int64_t ch[2]; // left, right
        if (channelCount == 4) { // QUAD
            // sample at index 0 is FL
            // sample at index 1 is FR
            // sample at index 2 is RL (or SL)
            // sample at index 3 is RR (or SR)
            // FL + RL
            ch[0] = src[0] + src[2] * MINUS_3_DB_IN_FLOAT;
            // FR + RR
            ch[1] = src[1] + src[3] * MINUS_3_DB_IN_FLOAT;
        } else if (channelCount == 6) { // 5.1
            // sample at index 0 is FL
            // sample at index 1 is FR
            // sample at index 2 is FC
            // sample at index 3 is LFE
            // sample at index 4 is RL (or SL)
            // sample at index 5 is RR (or SR)
            const int64_t centerPlusLfeContrib = src[2] + src[3] * MINUS_3_DB_IN_FLOAT;
            // FL + RL + centerPlusLfeContrib
            ch[0] = src[0] + (src[4] + centerPlusLfeContrib) * MINUS_3_DB_IN_FLOAT;
            // FR + RR + centerPlusLfeContrib
            ch[1] = src[1] + (src[5] + centerPlusLfeContrib) * MINUS_3_DB_IN_FLOAT;
        } else if (channelCount == 8) { // 7.1
            // sample at index 0 is FL
            // sample at index 1 is FR
            // sample at index 2 is FC
            // sample at index 3 is LFE
            // sample at index 4 is RL
            // sample at index 5 is RR
            // sample at index 6 is SL
            // sample at index 7 is SR
            const int64_t centerPlusLfeContrib = src[2] + src[3] * MINUS_3_DB_IN_FLOAT;
            // FL + RL + SL + centerPlusLfeContrib
            ch[0] = src[0] + (src[4] + src[6] + centerPlusLfeContrib) * MINUS_3_DB_IN_FLOAT;
            // FR + RR + SR + centerPlusLfeContrib
            ch[1] = src[1] + (src[5] + src[7] + centerPlusLfeContrib) * MINUS_3_DB_IN_FLOAT;
        } else {
            return false;
        }
        dst[0] = CLIPINT(ch[0]);
        dst[1] = CLIPINT(ch[1]);

        src += channelCount;
        dst += 2;
        --frameCount;
    }
    return true;
}


static bool setInputChannelMask(aml_pcm_downmix_st *p_downmix, audio_channel_mask_t inputChannelMask)
{
    float (*mMatrix)[2] = p_downmix->mMatrix;

    if (p_downmix->mInputChannelMask != inputChannelMask) {
        if (inputChannelMask & ~((1 << MAX_INPUT_CHANNELS_SUPPORTED) - 1)) {
            return false;  // not channel position mask, or has unknown channels.
        }

        // Compute at what index each channel is: samples will be in the following order:
        //     FL  FR  FC    LFE     BL  BR  BC    SL  SR
        //
        // Prior to API 32, use of downmix resulted in channels being scaled in half amplitude.
        // We now use a compliant downmix matrix for 5.1 with the following standards:
        // ITU-R 775-2, ATSC A/52, ETSI TS 101 154, IEC 14496-3, which is unity gain for the
        // front left and front right channel contribution.
        //
        // For 7.1 to 5.1 we set equal contributions for the side and back channels
        // which follow Dolby downmix recommendations.
        //
        // We add contributions from the LFE into the L and R channels
        // at a weight of 0.5 (rather than the power preserving 0.707)
        // which is to ensure that headphones can still experience LFE
        // with lesser risk of speaker overload.
        //
        // Note: geometrically left and right channels contribute only to the corresponding
        // left and right outputs respectively.  Geometrically center channels contribute
        // to both left and right outputs, so they are scaled by 0.707 to preserve power.
        //
        //    (transfer matrix)
        //     FL  FR  FC    LFE    BL    BR       BC  SL     SR
        //     1.0     0.707 0.5    0.707       0.5 0.707
        //         1.0 0.707 0.5         0.707 0.5         0.707
        int index = 0;
        const float COEF_25 = 0.2508909536f;
        const float COEF_35 = 0.3543928915f;
        const float COEF_36 = 0.3552343859f;
        const float COEF_61 = 0.6057043428f;
        for (unsigned tmp = inputChannelMask; tmp != 0; ++index) {
            const unsigned lowestBit = tmp & -(signed)tmp;
            switch (lowestBit) {
                case AUDIO_CHANNEL_OUT_FRONT_LEFT:
                case AUDIO_CHANNEL_OUT_TOP_FRONT_LEFT:
                case AUDIO_CHANNEL_OUT_BOTTOM_FRONT_LEFT:
                    mMatrix[index][0] = 1.f;
                    mMatrix[index][1] = 0.f;
                    break;
                case AUDIO_CHANNEL_OUT_SIDE_LEFT:
                case AUDIO_CHANNEL_OUT_BACK_LEFT:
                case AUDIO_CHANNEL_OUT_TOP_BACK_LEFT:
                case AUDIO_CHANNEL_OUT_FRONT_WIDE_LEFT: // FRONT_WIDE closer to SIDE.
                    mMatrix[index][0] = MINUS_3_DB_IN_FLOAT;
                    mMatrix[index][1] = 0.f;
                    break;
                case AUDIO_CHANNEL_OUT_FRONT_RIGHT:
                case AUDIO_CHANNEL_OUT_TOP_FRONT_RIGHT:
                case AUDIO_CHANNEL_OUT_BOTTOM_FRONT_RIGHT:
                    mMatrix[index][0] = 0.f;
                    mMatrix[index][1] = 1.f;
                    break;
                case AUDIO_CHANNEL_OUT_SIDE_RIGHT:
                case AUDIO_CHANNEL_OUT_BACK_RIGHT:
                case AUDIO_CHANNEL_OUT_TOP_BACK_RIGHT:
                case AUDIO_CHANNEL_OUT_FRONT_WIDE_RIGHT: // FRONT_WIDE closer to SIDE.
                    mMatrix[index][0] = 0.f;
                    mMatrix[index][1] = MINUS_3_DB_IN_FLOAT;
                    break;
                case AUDIO_CHANNEL_OUT_FRONT_CENTER:
                case AUDIO_CHANNEL_OUT_TOP_FRONT_CENTER:
                case AUDIO_CHANNEL_OUT_BOTTOM_FRONT_CENTER:
                    mMatrix[index][0] = mMatrix[index][1] = MINUS_3_DB_IN_FLOAT;
                    break;
                case AUDIO_CHANNEL_OUT_TOP_SIDE_LEFT:
                    mMatrix[index][0] = COEF_61;
                    mMatrix[index][1] = 0.f;
                    break;
                case AUDIO_CHANNEL_OUT_TOP_SIDE_RIGHT:
                    mMatrix[index][0] = 0.f;
                    mMatrix[index][1] = COEF_61;
                    break;
                case AUDIO_CHANNEL_OUT_FRONT_LEFT_OF_CENTER:
                    mMatrix[index][0] = COEF_61;
                    mMatrix[index][1] = COEF_25;
                    break;
                case AUDIO_CHANNEL_OUT_FRONT_RIGHT_OF_CENTER:
                    mMatrix[index][0] = COEF_25;
                    mMatrix[index][1] = COEF_61;
                    break;
                case AUDIO_CHANNEL_OUT_TOP_CENTER:
                    mMatrix[index][0] = mMatrix[index][1] = COEF_36;
                    break;
                case AUDIO_CHANNEL_OUT_TOP_BACK_CENTER:
                    mMatrix[index][0] = mMatrix[index][1] = COEF_35;
                    break;
                case AUDIO_CHANNEL_OUT_LOW_FREQUENCY_2:
                    mMatrix[index][0] = 0.f;
                    mMatrix[index][1] = MINUS_3_DB_IN_FLOAT;
                    break;
                case AUDIO_CHANNEL_OUT_LOW_FREQUENCY:
                    if (inputChannelMask & AUDIO_CHANNEL_OUT_LOW_FREQUENCY_2) {
                        mMatrix[index][0] = MINUS_3_DB_IN_FLOAT;
                        mMatrix[index][1] = 0.f;
                        break;
                    }
                    FALLTHROUGH_INTENDED;
                case AUDIO_CHANNEL_OUT_BACK_CENTER:
                    mMatrix[index][0] = mMatrix[index][1] = 0.5f;
                    break;
            }
            tmp ^= lowestBit;
        }
        p_downmix->mInputChannelMask = inputChannelMask;
        // Note: mLastValidChannelIndexPlusOne is the same as mInputChannelCount for
        // this particular matrix, as it has a nonzero column for every channel position.
        p_downmix->mInputChannelCount = index;
        p_downmix->mLastValidChannelIndexPlusOne = index;
    }
    return true;
}


static bool matrixProcess_16bit(aml_pcm_downmix_st *p_downmix, const int16_t *src, int16_t *dst, size_t frameCount)
{
    // matrix multiply
    if (p_downmix->mInputChannelMask == AUDIO_CHANNEL_NONE) return false;
    while (frameCount) {
        int64_t ch[2] = {0, 0}; // left, right
        for (size_t i = 0; i < p_downmix->mLastValidChannelIndexPlusOne; ++i) {
            ch[0] += p_downmix->mMatrix[i][0] * src[i];
            ch[1] += p_downmix->mMatrix[i][1] * src[i];
        }

        dst[0] = CLIPSHORT(ch[0]);
        dst[1] = CLIPSHORT(ch[1]);
        src += p_downmix->mInputChannelCount;
        dst += 2;
        --frameCount;
    }
    return true;
}


static bool matrixProcess_32bit(aml_pcm_downmix_st *p_downmix, const int32_t *src, int32_t *dst, size_t frameCount)
{
    // matrix multiply
    if (p_downmix->mInputChannelMask == AUDIO_CHANNEL_NONE) return false;
    while (frameCount) {
        int64_t ch[2] = {0, 0}; // left, right
        for (size_t i = 0; i < p_downmix->mLastValidChannelIndexPlusOne; ++i) {
            ch[0] += p_downmix->mMatrix[i][0] * src[i];
            ch[1] += p_downmix->mMatrix[i][1] * src[i];
        }

        dst[0] = CLIPINT(ch[0]);
        dst[1] = CLIPINT(ch[1]);
        src += p_downmix->mInputChannelCount;
        dst += 2;
        --frameCount;
    }
    return true;
}

static bool processSwitch_16bit(aml_pcm_downmix_st *p_downmix, const int16_t *src, int16_t *dst, size_t frameCount)
{
    switch (p_downmix->mInputChannelMask) {
        case AUDIO_CHANNEL_OUT_QUAD_BACK:
        case AUDIO_CHANNEL_OUT_QUAD_SIDE:
            return specificProcess_16bit(src, dst, frameCount, 4);
        case AUDIO_CHANNEL_OUT_5POINT1_BACK:
        case AUDIO_CHANNEL_OUT_5POINT1_SIDE:
            return specificProcess_16bit(src, dst, frameCount, 6);
        case AUDIO_CHANNEL_OUT_7POINT1:
            return specificProcess_16bit(src, dst, frameCount, 8);
        default:
            break; // handled below.
    }

    return matrixProcess_16bit(p_downmix, src, dst, frameCount);
}


static bool processSwitch_32bit(aml_pcm_downmix_st *p_downmix, const int32_t *src, int32_t *dst, size_t frameCount)
{
    switch (p_downmix->mInputChannelMask) {
        case AUDIO_CHANNEL_OUT_QUAD_BACK:
        case AUDIO_CHANNEL_OUT_QUAD_SIDE:
            return specificProcess_32bit(src, dst, frameCount, 4);
        case AUDIO_CHANNEL_OUT_5POINT1_BACK:
        case AUDIO_CHANNEL_OUT_5POINT1_SIDE:
            return specificProcess_32bit(src, dst, frameCount, 6);
        case AUDIO_CHANNEL_OUT_7POINT1:
            return specificProcess_32bit(src, dst, frameCount, 8);
        default:
            break; // handled below.
    }

    return matrixProcess_32bit(p_downmix, src, dst, frameCount);
}


/*
 * FIXME : Maybe we should use Android Downmix regardless of latency/performance.
 *
 * Reference : system/media/audio_utils/include/audio_utils/ChannelMix.h
*/
int do_downmix_to_2ch(aml_pcm_downmix_st *p_downmix, void *data_in, size_t in_frames, struct audioCfg *p_in_cfg)
{
    int ret = 0;
    audio_format_t out_format = p_in_cfg->format;
    uint32_t in_channelCnt = p_in_cfg->channelCnt;
    int req_buf_size = in_frames * 2 * audio_bytes_per_sample(out_format);

    ret = aml_audio_check_and_realloc(&p_downmix->output_buf, &p_downmix->output_buf_size, req_buf_size);
    if ((ret != 0) || (p_downmix->output_buf == NULL)) {
        AM_LOGE("allocate output_buf(%d bytes) failed", req_buf_size);
        return -1;
    }

    if (in_channelCnt == 2) {
        memcpy(p_downmix->output_buf, data_in, req_buf_size);
        return 0;
    }

    if ((out_format != AUDIO_FORMAT_PCM_16_BIT) && (out_format != AUDIO_FORMAT_PCM_32_BIT)) {
        AM_LOGE("not support output format 0x%x", out_format);
        return -1;
    }
    if (setInputChannelMask(p_downmix, p_in_cfg->channelMask) != true) {
        AM_LOGE("setInputChannelMask(0x%x) failed !", p_in_cfg->channelMask);
        return -1;
    }

    if (out_format == AUDIO_FORMAT_PCM_16_BIT) {
        ret = processSwitch_16bit(p_downmix, data_in, p_downmix->output_buf, in_frames);
    } else if (out_format == AUDIO_FORMAT_PCM_32_BIT) {
        ret = processSwitch_32bit(p_downmix, data_in, p_downmix->output_buf, in_frames);
    }
    return ret;
}

