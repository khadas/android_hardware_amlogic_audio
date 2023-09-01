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

#ifndef _AUDIO_POST_PROCESS_H_
#define _AUDIO_POST_PROCESS_H_

#include <hardware/audio_effect.h>

#define MAX_POSTPROCESSORS 9

/*now audio_post_process  support 2ch 16bit data
*vx process 256 as a block(256 *  4 bytes)
* 32bit multich  case need to do
*/
#define EFFECT_PROCESS_BLOCK_SIZE (256 *  4)

struct aml_post_effect_info {
    bool effect_is_repeat_create;
    int effect_index;
};

struct aml_native_postprocess {
    int num_postprocessors;
    effect_handle_t postprocessors[MAX_POSTPROCESSORS];
    int total_postprocessors;

    /* VirtualX effect license library exist flag */
    /* Path: (/vendor/lib/soundfx/libvx.so) */
    bool libvx_exist;
    bool vx_force_stereo;
    /* channel num of effect input */
    int effect_in_ch;
    int AML_DTS_index;
    /* audio flinger effect chain length*/
    int audio_effectchain_length;
    //native private process handle: AI AQ
    void *ai_handle;
    /*  0 vx; 1 tru_sur; 2 hpeq; 3 balance; 4 treblebass; 5 dbx; 6 dpe ;7 dapv2; 8 virtual_sur */
    struct aml_post_effect_info effect_info[MAX_POSTPROCESSORS];
};

/*
 *@brief audio_post_process
 * native_postprocess:
 *     effect process handles
 * in_buffer:
 *     input data
 * in_frames:
 *    input data frames
 * return
 *    output data frames
 *
 */
size_t audio_post_process(struct aml_native_postprocess *native_postprocess, int16_t *in_buffer, size_t in_frames);
int audio_VX_post_process(struct aml_native_postprocess *native_postprocess, int16_t *in_buffer, size_t bytes);
int aml_add_audio_effect(struct aml_native_postprocess *native_postprocess, effect_handle_t effect);

/* VirtualX: */
/* path of virtualx effect license library */
#define VIRTUALX_LICENSE_LIB_PATH "/vendor/lib/soundfx/libvx.so"

void VirtualX_reset(struct aml_native_postprocess *native_postprocess);
void VirtualX_Channel_reconfig(struct aml_native_postprocess *native_postprocess, int ch_num);
bool Check_VX_lib(void);
int set_aml_dts_effect_param(struct aml_native_postprocess *native_postprocess, char *param);
int get_aml_dts_effect_param(struct aml_native_postprocess *native_postprocess, char *param, const char *keys);

#endif
