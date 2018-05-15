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

#ifndef _ALSA_MANAGER_H_
#define _ALSA_MANAGER_H_

/**
 * pcm open with configs in streams: card, device, pcm_config
 * If device has been opened, close it and reopen with new params
 * and increase the refs count
 */
int aml_alsa_output_open(struct audio_stream_out *stream);

/**
 * decrease the pcm refs count and do pcm close when refs count equals zero.
 */
void aml_alsa_output_close(struct audio_stream_out *stream);
/**
 * pcm_write to the pcm handle saved in stream instance.
 */
size_t aml_alsa_output_write(struct audio_stream_out *stream,
                        const void *buffer,
                        size_t bytes);
/**
 * get the stream latency.
 */
int aml_alsa_output_get_letancy(struct audio_stream_out *stream);


#endif // _ALSA_MANAGER_H_