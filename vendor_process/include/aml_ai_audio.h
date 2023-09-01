
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

#ifndef _AI_AUDIO_PROCESS_H_
#define _AI_AUDIO_PROCESS_H_

struct audio_buffer_t;
struct aml_audio_device;
struct aml_mixer_handle;
struct aml_native_postprocess;

enum aiaq_cmd_e {
    AIAQ_CMD_DISABLE = 0,
    AIAQ_CMD_ENABLE = 1,
    AIAQ_CMD_RESET = 2,
};

int aml_open_ai_audio_module(struct aml_native_postprocess *native_postprocess, struct aml_mixer_handle* alsa_mixer);

int aml_close_ai_audio_module(struct aml_native_postprocess *native_postprocess);

int aml_ai_audio_process(void *handle, audio_buffer_t *inBuf, audio_buffer_t *outBuf);

int get_aml_ai_process_result(void *handle, float *score, int32_t *label);

int aml_ai_audio_module_command(void *handle, int32_t cmdCode);
#endif
