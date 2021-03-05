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
 *
 * DESCRIPTION:
 *     This program is the factory of PCM data.
 *
 */

#define LOG_TAG "AML_Reverb"
//#define LOG_NDEBUG 0

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <log/log.h>

#include "reverb.h"

#define MAXRATE 48000 //max sample rate in Hz
#define MAXTIME 1 //max delay time in second
#define DELAY_BUFSIZ (MAXTIME * MAXRATE)
#define MAXREVERBS 8

struct reverb_t{
    int     counter;
    int     numdelays;
    float   *reverbbuf;
    float   in_gain;
    float   out_gain;
    float   time;
    float   delay[MAXREVERBS];
    float   decay[MAXREVERBS];
    int     samples[MAXREVERBS];
    int     maxsamples;
};

struct aml_reverb {
    struct reverb_t AML_Reverb_L;
    struct reverb_t AML_Reverb_R;
    unsigned int mode;
    float reverb_in_gain;
    float reverb_out_gain;
};

static inline int clip16(int x) {
    if (x < -32768) {
        return -32768;
    } else if (x > 32767) {
        return 32767;
    } else {
        return x;
    }
}

static int reverb_start(struct reverb_t *aml_Reverb) {
    struct reverb_t *reverb = aml_Reverb;
    int i;

    reverb->counter = 0;
    reverb->numdelays = 5;
    reverb->in_gain = 0.75;
    reverb->out_gain = 1.0;
    reverb->maxsamples = 0;
    reverb->counter = 0;
    reverb->reverbbuf = NULL;

    for (i = 0; i < reverb->numdelays; i++) {
        reverb->delay[i] = 20 * (i +1);
    }

    for (i = 0; i < reverb->numdelays; i++) {
        reverb->samples[i] = reverb->delay[i] * MAXRATE / 1000.0;
        if ( reverb->samples[i] > DELAY_BUFSIZ ) {
            ALOGE("reverb: delay must be less than %d seconds!\n", MAXTIME);
            return-1;
        }
        /* Compute a realistic decay */
        reverb->decay[i] = (float) pow(10.0,(-3.0 * reverb->delay[i] / reverb->time));
        ALOGE("reverb: reverb->decay[%d] = %f\n",i, reverb->decay[i]);
        if ( reverb->samples[i] > reverb->maxsamples )
            reverb->maxsamples = reverb->samples[i];
    }

    if (! (reverb->reverbbuf = (float *) malloc(sizeof (float) * reverb->maxsamples))) {
        ALOGE("reverb: Cannot malloc %d bytes!\n", sizeof(float) * reverb->maxsamples);
        return-1;
    }

    for (i = 0; i < reverb->maxsamples; ++i)
        reverb->reverbbuf[i] = 0.0;

    /* Compute the input volume carefully */
    for (i = 0; i < reverb->numdelays; i++)
        reverb->in_gain *= ( 1.0 - ( reverb->decay[i] * reverb->decay[i] ));

    ALOGI("reverb: reverb->in_gain = %f\n", reverb->in_gain);

    return 0;
}

/*
 * Processed signed long samples from ibuf to obuf.
 * Return number of samples processed.
 */
static int reverb_process(struct reverb_t *aml_Reverb, int16_t *ibuf, int16_t *obuf,  int frame_count)
{
    struct reverb_t *reverb = aml_Reverb;
    int len, done;
    int i, j, k;

    float d_in, d_out;
    int out;

    i = reverb->counter;
    len = frame_count;
    for (done = 0; done < len; done++) {
        d_in = (float) *ibuf;
        ibuf += 2;
        d_in = d_in * reverb->in_gain;
        /* Mix decay of delay and input as output */
        for (j = 0; j < reverb->numdelays; j++) {
            k = (i + reverb->maxsamples - reverb->samples[j]) % reverb->maxsamples;
            d_in += reverb->reverbbuf[k] * reverb->decay[j];
        }
        d_out = d_in * reverb->out_gain;
        out = clip16((int) d_out);
        *obuf = out;
        obuf += 2;
        reverb->reverbbuf[i] = d_in;
        i++;
        i %= reverb->maxsamples;
    }
    reverb->counter = i;
    return 0;
}

/*
 * Clean up reverb effect.
 */
static int reverb_stop(struct reverb_t *aml_Reverb)
{
    struct reverb_t *reverb = aml_Reverb;

    free((char *) reverb->reverbbuf);
    reverb->reverbbuf = NULL;
    return 0;
}

int AML_Reverb_Init(void **reverb_handle)
{
    struct aml_reverb *reverb;

    reverb = calloc(1, sizeof(struct aml_reverb));
    if (!reverb) {
        ALOGE("%s, malloc error\n", __func__);
        return -EINVAL;
    }

    reverb_start(&reverb->AML_Reverb_L);
    reverb_start(&reverb->AML_Reverb_R);

    *reverb_handle = (void *)reverb;

    ALOGI("init. aml reverb!");
    return 0;
}

int AML_Reverb_Process(void *reverb_handle, int16_t *inBuffer, int16_t *outBuffer, int frameCount)
{
    struct aml_reverb *reverb = (struct aml_reverb *)reverb_handle;
    struct reverb_t *AML_Reverb_L;
    struct reverb_t *AML_Reverb_R;
    float reverb_time;

    if (!reverb)
        return 0;

    AML_Reverb_L = &reverb->AML_Reverb_L;
    AML_Reverb_R = &reverb->AML_Reverb_R;

    reverb_time = (float)(reverb->mode * 30);
    if (reverb_time != AML_Reverb_L->time || reverb_time != AML_Reverb_R->time) {
        reverb_stop(AML_Reverb_L);
        reverb_stop(AML_Reverb_R);
        AML_Reverb_L->time = reverb_time;
        AML_Reverb_R->time = reverb_time;
        reverb_start(AML_Reverb_L);
        reverb_start(AML_Reverb_R);
    }

    reverb_process(AML_Reverb_L, inBuffer, outBuffer, frameCount);
    reverb_process(AML_Reverb_R, (inBuffer + 1), (outBuffer + 1), frameCount);

    return 0;
}

int AML_Reverb_Release(void *reverb_handle)
{
    struct aml_reverb *reverb = (struct aml_reverb *)reverb_handle;

    if (!reverb)
        return 0;

    ALOGI("%s, remove aml reverb\n", __func__);

    free(reverb->AML_Reverb_L.reverbbuf);
    free(reverb->AML_Reverb_R.reverbbuf);
    free(reverb);

    return 0;
}

void AML_Reverb_Set_Mode(void *reverb_handle, unsigned int mode)
{
    struct aml_reverb *reverb = (struct aml_reverb *)reverb_handle;

    if (!reverb)
        return;

    if (reverb->mode != mode) {
        ALOGV("%s, set aml reverb mode: %d\n", __func__, mode);
        reverb->mode = mode;
    }

    return;
}

