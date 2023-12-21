/*
 * hardware/amlogic/audio/utils/aml_peq.c
 *
 * Copyright (C) 2023 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 */

#define LOG_TAG "audio_hw_peq"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <cutils/log.h>
#include <math.h>

#include "aml_peq.h"
#include "aml_malloc_debug.h"

#define COEFF_FRACTION_BIT  22
#define SAMPLE_SHIFT_BIT    4
#define COEFF_FRACTION      (1 << COEFF_FRACTION_BIT)

#define M_PI 3.14159265358979323846
#define VOLUME_MIN_DB (-100)

/* Count if coefficients */
#define   COEFF_COUNT         3
#define   TMP_COEFF           2
/* Channel */
#define   MAX_CHANNEL         8
/* Max Band */
#define   MAX_BAND            8

struct peq_filter_coeff {
    /*B coefficient array: b0, b1, b2*/
    int   b[COEFF_COUNT];
    /*A coefficient array: a1, a2 */
    int   a[COEFF_COUNT - 1];
};

struct peq_tmp_value {
    /*tmp value for channel input data*/
    long long cx[TMP_COEFF];
    /*tmp value for channel output data*/
    long long cy[TMP_COEFF];
};

struct peq_param {
    pthread_mutex_t lock;
    int peq_band;
    int channel_num;
    int sample_rate;
    bool enable;
    float pre_gain[MAX_CHANNEL];
    float output_gain[MAX_CHANNEL];
    struct peq_filter_coeff filter_coeff[MAX_CHANNEL][MAX_BAND];
    struct peq_tmp_value tmp_value[MAX_CHANNEL][MAX_BAND];
};

static inline float DbToAmpl(float decibels)
{
    if (decibels <= VOLUME_MIN_DB) {
        return 0.0f;
    }
    return exp(decibels * 0.115129f);
}

static inline int clip32(long long x) {
    if (x > 2147483647)
        x = 2147483647;
    else if (x < -2147483647)
        x = -2147483647;
    return x;
}

static void BandPassFilter(struct peq_filter_coeff *p_filter_coeff, double fc, double Q, double G, double sr) {
    double b0, b1, b2, a1, a2;
    double K = tan((M_PI)*fc/sr);
    double V0 = pow(10, (G/20));
    double Q0 = 1/Q;

    if (V0 < 1)
        V0 = 1/V0;

    if (G > 0) {
        b0 = (1 + V0*Q0*K + K*K) / (1 + (Q0*K) + K*K);
        b1 = (2 * (K*K - 1)) / (1 + (Q0*K) + K*K);
        b2 = (1 - V0*Q0*K + K*K) / (1 + (Q0*K) + K*K);
        a1 = b1;
        a2 = (1 - (Q0*K) + K*K) / (1 + (Q0*K) + K*K);
    } else if (G < 0) {
        b0 = (1 + (Q0*K) + K*K) / (1 + V0*Q0*K + K*K);
        b1 = (2 * (K*K - 1)) / (1 + V0*Q0*K + K*K);
        b2 = (1 - (Q0*K) + K*K) / (1 + V0*Q0*K + K*K);
        a1 = b1;
        a2 = (1 - V0*Q0*K + K*K) / (1 + V0*Q0*K + K*K);
    } else {
        b0 = 1.0;
        b1 = 0;
        b2 = 0;
        a1 = 0;
        a2 = 0;
    }

    p_filter_coeff->b[0] = (int)(round(b0 * COEFF_FRACTION));
    p_filter_coeff->b[1] = (int)(round(b1 * COEFF_FRACTION));
    p_filter_coeff->b[2] = (int)(round(b2 * COEFF_FRACTION));
    p_filter_coeff->a[0] = (int)(round(a1 * COEFF_FRACTION));
    p_filter_coeff->a[1] = (int)(round(a2 * COEFF_FRACTION));

    return;
}

static void AllPassFilter(struct peq_filter_coeff *p_filter_coeff) {
    p_filter_coeff->b[0] = (int) COEFF_FRACTION;
    p_filter_coeff->b[1] = 0;
    p_filter_coeff->b[2] = 0;
    p_filter_coeff->a[0] = 0;
    p_filter_coeff->a[1] = 0;
}

static long long filter_processing(struct peq_filter_coeff *p_filter_coeff, struct peq_tmp_value *p_tmp_value, long long input_sample)
{
    long long sample = input_sample >> SAMPLE_SHIFT_BIT;
    long long output_sample = 0, temp = 0;
    long long cx_0, cx_1, cy_0, cy_1;

    cx_0 = p_tmp_value->cx[0];
    cx_1 = p_tmp_value->cx[1];
    cy_0 = p_tmp_value->cy[0];
    cy_1 = p_tmp_value->cy[1];

    temp =  sample * p_filter_coeff->b[0];
    temp += cx_0 * p_filter_coeff->b[1];
    temp += cx_1 * p_filter_coeff->b[2];
    temp -= cy_0 * p_filter_coeff->a[0];
    temp -= cy_1 * p_filter_coeff->a[1];
    output_sample = (temp >= 0) ? (temp >> COEFF_FRACTION_BIT) : ((temp * (-1)) >> COEFF_FRACTION_BIT) * (-1);

    cx_1 = cx_0;
    cx_0 = sample;
    cy_1 = cy_0;
    cy_0 = output_sample;

    p_tmp_value->cx[0] = cx_0;
    p_tmp_value->cx[1] = cx_1;
    p_tmp_value->cy[0] = cy_0;
    p_tmp_value->cy[1] = cy_1;

    return clip32(output_sample << SAMPLE_SHIFT_BIT);
}

static int32_t peq_processing(void *handle, int32_t input_sample, int channel_index)
{
    struct peq_param *peq_handle = (struct peq_param *)handle;
    struct peq_filter_coeff *p_filter_coeff;
    struct peq_tmp_value *p_tmp_value;
    float pre_gain = peq_handle->pre_gain[channel_index];
    float output_gain = peq_handle->output_gain[channel_index];
    long long sample = (long long)input_sample * pre_gain;
    int i;
    int32_t output_sample;

    for (int i = 0; i < peq_handle->peq_band; i++) {
        p_filter_coeff = &peq_handle->filter_coeff[channel_index][i];
        p_tmp_value = &peq_handle->tmp_value[channel_index][i];
        sample = filter_processing(p_filter_coeff, p_tmp_value, sample);
    }
    output_sample = clip32(sample * output_gain);
    return output_sample;
}

void* aml_peq_init(unsigned int peq_band, unsigned int channel_num, unsigned int sample_rate) {
    struct peq_param *peq_handle;
    struct peq_filter_coeff *p_filter_coeff;
    int i, j;

    if (peq_band > MAX_BAND || peq_band == 0 || channel_num > MAX_CHANNEL ||
            channel_num == 0 || sample_rate > 192000 || sample_rate < 8000) {
        ALOGE("invalid param: band = %d, channel_num = %d, sample_rate = %d\n",
            peq_band, channel_num, sample_rate);
        return NULL;
    }

    peq_handle = aml_audio_calloc(1, sizeof(struct peq_param));
    if (!peq_handle) {
        ALOGE("init peq failed!\n");
        return NULL;
    }

    peq_handle->peq_band = peq_band;
    peq_handle->channel_num = channel_num;
    peq_handle->sample_rate = sample_rate;

    pthread_mutex_init(&peq_handle->lock, NULL);

    /* set filter param to all pass filter*/
    for (int i = 0; i < peq_handle->channel_num; i++) {
        for (int j = 0; j < peq_handle->peq_band; j++) {
            p_filter_coeff = &peq_handle->filter_coeff[i][j];
            AllPassFilter(p_filter_coeff);
        }
        peq_handle->pre_gain[i] = 1.0;
        peq_handle->output_gain[i] = 1.0;
    }

    return (void*)peq_handle;
}

int aml_peq_processing(void *handle, int32_t *buffer, int bytes) {
    struct peq_param *peq_handle = (struct peq_param *)handle;
    int sample_count, channel, i, j;
    int32_t *input = buffer;

    if (!handle) {
        return -1;
    }
    pthread_mutex_lock(&peq_handle->lock);
    if (!peq_handle->enable) {
        pthread_mutex_unlock(&peq_handle->lock);
        return 0;
    }

    channel = peq_handle->channel_num;
    sample_count = bytes / (channel * sizeof(int32_t));
    for (int i = 0; i < sample_count; i++) {
        for (int j = 0; j < channel; j++) {
            *input = peq_processing(peq_handle, *input, j);
            input++;
        }
    }
    pthread_mutex_unlock(&peq_handle->lock);
    return 0;
}

int aml_peq_enable(void *handle, bool enable) {
    struct peq_param *peq_handle = (struct peq_param *)handle;

    if (!handle) {
        ALOGE("Null pointer to start/stop!\n");
        return -1;
    }

    pthread_mutex_lock(&peq_handle->lock);
    peq_handle->enable = enable;
    pthread_mutex_unlock(&peq_handle->lock);

    return 0;
}

int aml_peq_set_gain(void *handle, float pre_gain, float output_gain, int channel_index) {
    struct peq_param *peq_handle = (struct peq_param *)handle;

    if (!handle) {
        ALOGE("Null pointer to set gain!\n");
        return -1;
    }
    if (pre_gain > 0 || channel_index >= peq_handle->channel_num) {
        ALOGE("invalid param: pre_gain = %f, channel_index = %d\n", pre_gain, channel_index);
        return -1;
    }

    pthread_mutex_lock(&peq_handle->lock);
    peq_handle->pre_gain[channel_index] = DbToAmpl(pre_gain);
    peq_handle->output_gain[channel_index] = DbToAmpl(output_gain);
    pthread_mutex_unlock(&peq_handle->lock);

    ALOGI("pre_gain = %f, output_gain = %f, channel_index = %d\n", pre_gain, output_gain, channel_index);
    return 0;
}

int aml_peq_reset(void *handle) {
    struct peq_param *peq_handle = (struct peq_param *)handle;
    struct peq_filter_coeff *p_filter_coeff;
    struct peq_tmp_value *p_tmp_value;
    int i, j;

    if (!handle) {
        ALOGE("Null pointer to start/stop!\n");
        return -1;
    }

    pthread_mutex_lock(&peq_handle->lock);
    for (int i = 0; i < peq_handle->channel_num; i++) {
        for (int j = 0; j < peq_handle->peq_band; j++) {
            p_filter_coeff = &peq_handle->filter_coeff[i][j];
            AllPassFilter(p_filter_coeff);
        }
        peq_handle->pre_gain[i] = 1.0;
        peq_handle->output_gain[i] = 1.0;
    }
    p_tmp_value = &peq_handle->tmp_value[0][0];
    memset(p_tmp_value, 0, sizeof(struct peq_tmp_value) * MAX_CHANNEL * MAX_BAND);
    pthread_mutex_unlock(&peq_handle->lock);

    return 0;
}

int aml_peq_set_param(void *handle, float Q, float Gain, unsigned int Fc, unsigned int band_id, unsigned int channel_index) {
    struct peq_param *peq_handle = (struct peq_param *)handle;
    struct peq_filter_coeff *p_filter_coeff;

    if (!handle) {
        ALOGE("Null pointer to set param!\n");
        return -1;
    }
    if (Q <= 0 || Fc < 20 || Fc > 20000 || band_id >= peq_handle->peq_band ||
        channel_index >= peq_handle->channel_num || Gain > 10 || Gain < -10) {
        ALOGE("invalid param: Q = %f, Gain = %f, Fc = %d, band_id = %d, channel_index = %d\n",
                Q, Gain, Fc, band_id, channel_index);
        return -1;
    }
    ALOGI("set param: Q = %f, Gain = %f, Fc = %d, band_id = %d, channel_index = %d\n", Q, Gain, Fc, band_id, channel_index);

    pthread_mutex_lock(&peq_handle->lock);
    p_filter_coeff = &peq_handle->filter_coeff[channel_index][band_id];
    BandPassFilter(p_filter_coeff, (double)Fc, (double)Q, (double)Gain, (double)peq_handle->sample_rate);
    pthread_mutex_unlock(&peq_handle->lock);
    return 0;
}

int aml_peq_release(void *handle) {
    struct peq_param *peq_handle = (struct peq_param *)handle;

    if (!handle) {
        ALOGE("Null pointer to release peq!\n");
        return -1;
    }

    aml_peq_enable(peq_handle, false);
    aml_audio_free(peq_handle);

    ALOGI("aml peq release!\n");
    return 0;
}

#ifdef __cplusplus
}
#endif
