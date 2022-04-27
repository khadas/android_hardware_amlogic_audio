/*
 * hardware/amlogic/audio/utils/aml_volume_utils.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 */

#define LOG_TAG "aml_volume_utils"

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <cutils/log.h>

#include "aml_volume_utils.h"

/* default volume cruve in dB. 101 index.
   0 is mute, 100 is the max volume.
   normally, 0dB is the max volume*/
#define AUDIO_VOLUME_INDEX 101
static float volume_cruve_in_dB[AUDIO_VOLUME_INDEX] = {
    VOLUME_MIN_DB, /*mute*/
    -60,   -53,   -47.5, -43.5, -39,   -36,   -34,   -32,   -30,   -28,    /*1-10*/
    -27,   -26,   -25,   -24,   -23,   -22.2, -21.5, -21,   -20.6, -20.3,  /*11-20*/
    -19.9, -19.5, -19,   -18.7, -18.4, -18.2, -18,   -17.8, -17.5, -17.3,  /*21-30*/
    -17,   -16.8, -16.5, -16.2, -15.9, -15.6, -15.4, -15.2, -14.9, -14.7,  /*31-40*/
    -14.4, -14.1, -13.9, -13.7, -13.5, -13.3, -13.1, -12.9, -12.7, -12.4,  /*41-50*/
    -12.1, -11.8, -11.6, -11.4, -11.2, -11,   -10.8, -10.6, -10.3, -10,    /*51-60*/
    -9.8,  -9.6,  -9.4,  -9.2,  -9,    -8.7,  -8.4,  -8.1,  -7.8,  -7.5,   /*61-70*/
    -7.2,  -6.9,  -6.7,  -6.4,  -6.1,  -5.8,  -5.5,  -5.2,  -5,    -4.8,   /*71-80*/
    -4.7,  -4.5,  -4.3,  -4.1,  -3.8,  -3.6,  -3.3,  -3,    -2.7,  -2.5,   /*81-90*/
    -2.2,  -2,    -1.8,  -1.5,  -1.3,  -1,    -0.8,  -0.5,  -0.3,  0,      /*91-100*/
};

static inline int16_t clamp16(int32_t sample)
{
    if ((sample >> 15) ^ (sample >> 31)) {
        sample = 0x7FFF ^ (sample >> 31);
    }
    return sample;
}

static inline int32_t clamp32(int64_t sample)
{
    if ((sample >> 31) ^ (sample >> 63)) {
        sample = 0x7FFFFFFF ^ (sample >> 63);
    }
    return sample;
}

void apply_volume(float volume, void *buf, int sample_size, int bytes)
{
    int16_t *input16 = (int16_t *)buf;
    int32_t *input32 = (int32_t *)buf;
    unsigned int i = 0;

    if (sample_size == 2) {
        for (i = 0; i < bytes / sizeof(int16_t); i++) {
            int32_t samp = (int32_t)(input16[i]);
            input16[i] = clamp16((int32_t)(volume * samp));
        }
    } else if (sample_size == 4) {
        for (i = 0; i < bytes / sizeof(int32_t); i++) {
            int64_t samp = (int64_t)(input32[i]);
            input32[i] = clamp32((int64_t)(volume * samp));
        }
    } else {
        ALOGE("%s, unsupported audio format: %d!\n", __FUNCTION__, sample_size);
    }
    return;
}

const float msmix_pes_pan_LEFT_RIGHT[43] = {
  (0.5000000000f),
  (0.4996503524f),
  (0.4986018986f),
  (0.4968561049f),
  (0.4944154131f),
  (0.4912832366f),
  (0.4874639561f),
  (0.4829629131f),
  (0.4777864029f),
  (0.4719416652f),
  (0.4654368743f),
  (0.4582811279f),
  (0.4504844340f),
  (0.4420576968f),
  (0.4330127019f),
  (0.4233620996f),
  (0.4131193872f),
  (0.4022988899f),
  (0.3909157412f),
  (0.3789858616f),
  (0.3665259359f),
  (0.3535533906f),
  (0.3400863689f),
  (0.3261437056f),
  (0.3117449009f),
  (0.2969100928f),
  (0.2816600290f),
  (0.2660160383f),
  (0.2500000000f),
  (0.2336343141f),
  (0.2169418696f),
  (0.1999460122f),
  (0.1826705122f),
  (0.1651395310f),
  (0.1473775872f),
  (0.1294095226f),
  (0.1112604670f),
  (0.0929558036f),
  (0.0745211331f),
  (0.0559822381f),
  (0.0373650468f),
  (0.0186955971f),
  (0.0000000000f)
};


void get_left_right_volume(unsigned char panByte,float* left_volume,float* right_volume)
{
    #define PAN_ONE    1.0f
    #define PAN_ZERO   (0)     /**< Factor when no panning is applied. */
    #define PAN_M3DB   0.707106769f /**< 3dB <=> 1/sqrt(2) */
    *left_volume  = PAN_M3DB;
    *right_volume = PAN_M3DB;
    if ((panByte>0) && (panByte <= 0xff))
    {
     if ((panByte < 21)) {
         * left_volume = (msmix_pes_pan_LEFT_RIGHT[42-(21-panByte)]);
         * right_volume = (msmix_pes_pan_LEFT_RIGHT[21-panByte]);
     } else if ((panByte >= 21) && (panByte <= 127)) {
         * right_volume = PAN_ONE;
     } else if ((panByte >= 128) && (panByte <= 234)) {
         * left_volume = PAN_ONE;
     } else if ((panByte >= 235) && (panByte <= 255)) {
         * left_volume = (msmix_pes_pan_LEFT_RIGHT[panByte-235]);
         * right_volume = (msmix_pes_pan_LEFT_RIGHT[42-(panByte-235)]);
     }
    }
}

void apply_volume_pan(unsigned char panByte, void *buf, int sample_size, int bytes)
{
    int16_t *input16 = (int16_t *)buf;
    int32_t *input32 = (int32_t *)buf;
    unsigned int i = 0;
    float left_volume;
    float right_volume;
    if ((panByte <= 0)|| (panByte > 0xFF))
    {
        return ;
    }
    get_left_right_volume(panByte,&left_volume,&right_volume);
    if (sample_size == 2) {
        for (i = 0; i < bytes / sizeof(int16_t); i++) {
            int32_t samp = (int32_t)(input16[i]);
            if (0 == i%2)
            {
                input16[i] = clamp16((int32_t)(left_volume * samp));
            }
            else
            {
                input16[i] = clamp16((int32_t)(right_volume * samp));
            }
        }
    } else if (sample_size == 4) {
        for (i = 0; i < bytes / sizeof(int32_t); i++) {
            int64_t samp = (int64_t)(input32[i]);
            if (0 == i%2)
            {
                input32[i] = clamp32((int64_t)(left_volume * samp));
            }
            else
            {
                input32[i] = clamp32((int64_t)(right_volume * samp));
            }
        }
    } else {
        ALOGE("%s, unsupported audio format: %d!\n", __FUNCTION__, sample_size);
    }
    return;
}
void apply_volume_16to32(float volume, int16_t *in_buf, int32_t *out_buf, int bytes)
{
    int16_t *input16 = (int16_t *)in_buf;
    int32_t *output32 = (int32_t *)out_buf;
    unsigned int i = 0;

    for (i = 0; i < bytes / sizeof(int16_t); i++) {
        int32_t samp = ((int32_t)input16[i]) << 16;
        output32[i] = clamp32((int64_t)(samp * (double)(volume)));
    }

    return;
}

void apply_volume_fade(float last_volume, float volume, void *buf, int sample_size, int channels, int bytes)
{
    int16_t *input16 = (int16_t *)buf;
    int32_t *input32 = (int32_t *)buf;
    unsigned int i = 0, j= 0;
    float gain_step = 0.0;
    float new_volume = 1.0;
    if (channels == 0 || sample_size == 0) {
        return;
    }
    int32_t out_frames = bytes/(channels * sample_size);

    if (last_volume != volume) {
        gain_step = (volume - last_volume)/out_frames;
    }

    if (sample_size == 2) {
        for (i = 0; i < out_frames; i++) {
            new_volume = last_volume + i * gain_step;
            for (j = 0; j < channels; j ++) {
                input16[i * channels + j] = clamp16((int32_t)(new_volume * input16[i * channels + j]));
            }
        }
    } else if (sample_size == 4) {
        for (i = 0; i < out_frames; i++) {
            new_volume = last_volume + i * gain_step;
            for (j = 0; j < channels; j ++) {
                input32[i * channels + j] = clamp32((int64_t)(new_volume * input32[i * channels + j]));
            }
        }
    } else {
        ALOGE("%s, unsupported audio format: %d!\n", __FUNCTION__, sample_size);
    }
    return;
}

float get_volume_by_index(int volume_index)
{
    float volume = 1.0;
    if (volume_index >= AUDIO_VOLUME_INDEX) {
        ALOGE("%s, invalid index!\n", __FUNCTION__);
        return volume;
    }
    if (volume_index >= 0) {
        volume *= DbToAmpl(volume_cruve_in_dB[volume_index]);
    }

    return volume;
}


float get_db_by_index(int volume_index)
{
    float db = 0.0;
    if (volume_index >= AUDIO_VOLUME_INDEX) {
        ALOGE("%s, invalid index!\n", __FUNCTION__);
        return VOLUME_MIN_DB;
    }
    if (volume_index >= 0) {
        db = volume_cruve_in_dB[volume_index];
    }

    return db;
}

// inVol range [0.0---1.0]
int volume2Ms12DBGain(float inVol)
{
    float fTargetDB;
    // MS12 parameter is INT: -96 is mute
    int iMS12DB = -96;

    if (inVol > 1 || inVol < 0) {
        ALOGE("%s, invalid volume %f\n", __FUNCTION__, inVol);
        inVol = 1;
    }

    // As compared by human ear, AmplToDb() is better than get_db_by_index() here.
    fTargetDB = AmplToDb(inVol);

    // backup method here
    //int iVolIdx;
    //fTargetDB = get_db_by_index(iVolIdx);

    if (VOLUME_MIN_DB >= fTargetDB) {
        fTargetDB = -96;
    }

    iMS12DB = (int)(fTargetDB - 0.5);
    return iMS12DB;
}
static signed int jj=0;
static signed short sine_tone_data[108] = {
    514,988,1443,1917,2359,2821,3246,3684,4091,4499,4881,5254,5606,5941,6257,6549,
    6824,7069,7300,7498,7679,7828,7956,8054,8126,8173,8189,8183,8146,8085,7993,7879,
    7735,7569,7375,7159,6916,6653,6367,6059,5733,5385,5021,4639,4245,3833,3411,2976,
    2532,2080,1619,1156,686,216,-257,-727,-1196,-1661,-2120,-2571,-3015,-3448,-3871,-4280,
    -4675,-5055,-5415,-5763,-6088,-6394,-6677,-6940,-7179,-7394,-7584,-7750,-7890,-8004,-8092,-8151,
    -8185,-8191,-8171,-8123,-8048,-7947,-7819,-7664,-7484,-7280,-7051,-6801,-6526,-6231,-5913,-5578,
    -5223,-4851,-4463,-4061,-3644,-3215,-2777,-2328,-1873,-1411,-944,-473
};

void apply_tone_16bit2ch(unsigned char* buf, int datalen)
{
    int ii =0;
    for (ii=0;ii< datalen;) {
        signed short *pL = &buf[ii];
        signed short *pR = pL + 1;
        *pL = sine_tone_data[jj];
        *pR = sine_tone_data[jj];
        ii += 4;
        jj += 1;
        jj = jj % 108;
    }
}

/*
 * -dap_gains       * <1 int>
 *                    - postgain (-2080...480, def: 0)
 * Settings From -130 to +30 dB, in 0.0625 dB steps
 */
int volume2Ms12DapPostgain(float inVol)
{
    float fTargetDB;
    // MS12 parameter is INT: -96 is mute
    int dap_postgain = 0;

    if (inVol > 1.0 || inVol < 0) {
        ALOGE("%s, invalid volume %f\n", __FUNCTION__, inVol);
        inVol = 1.0;
    }

    if (fabsf(inVol) <= 1e-6) {
        fTargetDB = -130;
    }
    else {
        fTargetDB = 20 * log10(inVol);
    }

    if (-130 >= fTargetDB) {
        fTargetDB = -130;
    }
    else if (fTargetDB >30) {
        fTargetDB = 30;
    }

    dap_postgain = (int)(fTargetDB / 0.0625);
    return dap_postgain;
}

