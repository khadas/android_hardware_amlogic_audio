/*
 * Copyright (C) 2023 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 */


#ifndef _AML_PEQ_H
#define _AML_PEQ_H

#ifdef __cplusplus
extern "C" {
#endif

/* Each peq supports up to 8 band; peq_band is defined as actually used band count */
/* less peq_band, lower CPU costs */
/* Support up to 8ch, sample_rate: 8000 ~ 192000Hz */
void* aml_peq_init(unsigned int peq_band, unsigned int channel_num, unsigned int sample_rate);
/* Start/Stop processing */
int aml_peq_enable(void *handle, bool enable);
/* reset all peqs to all pass filter, clear all buffer */
int aml_peq_reset(void *handle);
/* Each channel has an independent PEQ param: band_id: 0~7; channel_index: 0 ~ (channel_num - 1) */
/* Gain: -10~10dB; Fc: 20~20K; Q > 0*/
int aml_peq_set_param(void *handle, float Q, float Gain, unsigned int Fc, unsigned int band_id, unsigned int channel_index);
/* pre_gain < 0dB */
int aml_peq_set_gain(void *handle, float pre_gain, float output_gain, int channel_index);
int aml_peq_processing(void *handle, int32_t *buffer, int bytes);
int aml_peq_release(void *handle);

#ifdef __cplusplus
}
#endif

#endif
