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

#define LOG_TAG "audio_hw_utils_resampler"
// #define LOG_NDEBUG 0

#include <math.h>
#include <cutils/log.h>
#include <system/audio.h>
#include <aml_audio_resampler.h>

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

int resampler_init(struct resample_para *resample) {

    ALOGD("%s, Init Resampler: input_sr = %d, output_sr = %d, channel = %d, format = %d\n",
        __FUNCTION__, resample->input_sr, resample->output_sr, resample->channels, resample->aformat);

    static const double kPhaseMultiplier = 1L << 28;
    unsigned int i;

    if (resample->channels > MAX_RESAMPLE_CHANNEL) {
        ALOGE("Error: %s, max support channels: %d\n",
        __FUNCTION__, MAX_RESAMPLE_CHANNEL);
        return -1;
    }

    if (resample->aformat != AUDIO_FORMAT_PCM_16_BIT &&
        resample->aformat != AUDIO_FORMAT_PCM_32_BIT) {
        ALOGE("%s not support audio foramt =0x%x", __func__, resample->aformat);
        return -1;
    }

    resample->FractionStep = (unsigned int) (resample->input_sr * kPhaseMultiplier
                            / resample->output_sr);
    resample->SampleFraction = 0;
    for (i = 0; i < resample->channels; i++)
        resample->lastsample[i] = 0;

    return 0;
}

int resample_process(struct resample_para *resample, unsigned int in_frame,
        void *input, void *output) {
    unsigned int inputIndex = 0;
    unsigned int outputIndex = 0;
    unsigned int FractionStep = resample->FractionStep;
    int32_t last_sample[MAX_RESAMPLE_CHANNEL];
    unsigned int i;
    unsigned int channels = resample->channels;
    int16_t *input16, *output16;
    int32_t *input32, *output32;

    static const unsigned int kPhaseMask = (1LU << 28) - 1;
    unsigned int frac = resample->SampleFraction;

    for (i = 0; i < channels; i++)
        last_sample[i] = resample->lastsample[i];

    if (resample->aformat == AUDIO_FORMAT_PCM_16_BIT) {
        input16 = (int16_t *)input;
        output16 = (int16_t *)output;

        while (inputIndex == 0) {
            for (i = 0; i < channels; i++) {
                *output16++ = clamp16((int32_t) last_sample[i] +
                    ((((int32_t) input16[i] - (int32_t) last_sample[i]) * ((int32_t) frac >> 13)) >> 15));
            }

            frac += FractionStep;
            inputIndex += (frac >> 28);
            frac = (frac & kPhaseMask);
            outputIndex++;
        }

        while (inputIndex < in_frame) {
            for (i = 0; i < channels; i++) {
                *output16++ = clamp16((int32_t) input16[channels * (inputIndex - 1) + i] +
                    ((((int32_t) input16[channels * inputIndex + i]
                    - (int32_t) input16[channels * (inputIndex - 1) + i]) * ((int32_t) frac >> 13)) >> 15));
            }

            frac += FractionStep;
            inputIndex += (frac >> 28);
            frac = (frac & kPhaseMask);
            outputIndex++;
        }

        resample->SampleFraction = frac;

        for (i = 0; i < channels; i++)
            resample->lastsample[i] = input16[channels * (in_frame - 1) + i];
    } else {
        input32 = (int32_t *)input;
        output32 = (int32_t *)output;

        while (inputIndex == 0) {
            for (i = 0; i < channels; i++) {
                *output32++ = clamp32((int64_t) last_sample[i] +
                    ((((int64_t) input32[i] - (int64_t) last_sample[i]) * ((int64_t) frac >> 13)) >> 15));
            }

            frac += FractionStep;
            inputIndex += (frac >> 28);
            frac = (frac & kPhaseMask);
            outputIndex++;
        }

        while (inputIndex < in_frame) {
            for (i = 0; i < channels; i++) {
                *output32++ = clamp32((int64_t) input32[channels * (inputIndex - 1) + i] +
                    ((((int64_t) input32[channels * inputIndex + i]
                    - (int64_t) input32[channels * (inputIndex - 1) + i]) * ((int64_t) frac >> 13)) >> 15));
            }

            frac += FractionStep;
            inputIndex += (frac >> 28);
            frac = (frac & kPhaseMask);
            outputIndex++;
        }

        resample->SampleFraction = frac;

        for (i = 0; i < channels; i++)
            resample->lastsample[i] = input32[channels * (in_frame - 1) + i];
    }

    return outputIndex;
}

