/*
 * Copyright (C) 2021 Amlogic Corporation.
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
#ifndef _AML_AUDIO_NONMS12_RENDER_H_
#define _AML_AUDIO_NONMS12_RENDER_H_

#include "audio_hw.h"

/**
 * @brief This function use aml audio decoder to process the audio data
 *
 * @returns the process result
 */
int aml_audio_nonms12_render(struct audio_stream_out *stream, const void *buffer, size_t bytes);
bool aml_decoder_output_compatible(struct audio_stream_out *stream, audio_format_t sink_format, audio_format_t optical_format);
int aml_decoder_config_prepare(struct audio_stream_out *stream, audio_format_t format, aml_dec_config_t * dec_config);

/**
* @brief Get dca decoder output channel(internal use).
* @param None
* @return [success]: 0 decoder not init.
*         [success]: 1 ~ 8, output channel number
*            [fail]: -1 get output channel fail.
*/
int dca_get_out_ch_internal(void);
/**
* @brief Set dca decoder output channel(internal use).
* @param ch_num: The num of channels you want the decoder to output
*              0: Default setting, decoder configs output channel automatically.
*          1 ~ 8: The decoder outputs the specified number of channels
*                (At present, @ch_num only supports 2-ch and 6-ch).
* @return [success]: 0
*            [fail]: -1 set output channel fail.
*/
int dca_set_out_ch_internal(int ch_num);

#endif

