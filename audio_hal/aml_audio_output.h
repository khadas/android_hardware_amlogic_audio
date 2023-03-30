/*
 * Copyright (C) 2023 The Android Open Source Project
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


#ifndef _AUDIO_OUTPUT_API_H_
#define _AUDIO_OUTPUT_API_H_

#include "audio_hw.h"
#include "aml_audio_stream.h"

/*
 *@brief audio_hal_data_processing
 * format:
 *    if pcm-16bits-stereo, add audio effect process, and mapping to 8ch
 *    if raw data, packet it to IEC61937 format with spdif encoder
 *    if IEC61937 format, write them to hardware
 * return
 *    0, success
 *    -1, fail
 */
ssize_t audio_hal_data_processing(struct audio_stream_out *stream
                                    , const void *input_buffer
                                    , size_t input_buffer_bytes
                                    , audio_data_info_t * in_data_info
                                    , void **output_buffer
                                    , size_t *output_buffer_bytes
                                    , audio_data_info_t * out_data_info);


/*
 *@brief hw_write the api to write the data to audio hardware
 */
ssize_t hw_write(struct audio_stream_out *stream
                    , const void *buffer
                    , size_t bytes
                    , audio_data_info_t * audio_info);


ssize_t aml_audio_pcm_output(struct audio_stream_out *stream,
                                const void *buffer,
                                size_t bytes,
                                audio_data_info_t * data_info);

#endif
