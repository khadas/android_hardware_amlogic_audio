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

#ifndef _SPDIF_ENCODER_API_H_
#define _SPDIF_ENCODER_API_H_

int aml_spdif_encoder_open(void **spdifenc_handle, audio_format_t format);
int aml_spdif_encoder_close(void *phandle);
int aml_spdif_encoder_process(void *phandle, const void *buffer, size_t numBytes, void **output_buf, size_t *out_size);
int aml_spdif_encoder_mute(void *phandle, bool bmute);



#endif // _ALSA_MANAGER_H_
