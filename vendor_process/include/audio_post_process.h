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

enum aml_post_effect_info_type {
    EFFECT_TYPE_VIRTUAL_X = 0,
    EFFECT_TYPE_TRUE_SURROUND_HD,
    EFFECT_TYPE_HPEQ,
    EFFECT_TYPE_BALANCE,
    EFFECT_TYPE_TREBLEBASE,
    EFFECT_TYPE_DBX,
    EFFECT_TYPE_DPE,
    EFFECT_TYPE_MS12_V2_DAP,
    EFFECT_TYPE_VIRTUAL_SURROUND,
    EFFECT_TYPE_MAX,
};

enum aml_effect_owner_type {
    EFFECT_OWNEDBY_STREAMOUT = 0,
    EFFECT_OWNEDBY_DEVICE,
};

struct effect_insert_seq_desc {
    int type;
    int seq;
    const char *name;
};

enum audio_effect_mode {
    EFFECT_MODE_OFF = 1,
    EFFECT_MODE_DAP,
    EFFECT_MODE_VX,
    EFFECT_MODE_AUTO,
};

struct aml_post_effect_ctrl {
    int dap_enable;
    int vx_enable;
    enum audio_effect_mode effect_mode;
    bool is_dts;
};

struct aml_post_effect_info {
    effect_handle_t itfe;
    //port_handle for device effect
    audio_port_handle_t port;

    //bool effect_is_repeat_create;
    int index;
    //insert sequence descriptor
    const struct effect_insert_seq_desc *idesc;
};

struct aml_native_postprocess {
    int num_postprocessors;
    struct aml_post_effect_info postprocessors[MAX_POSTPROCESSORS];

    /* VirtualX effect license library exist flag */
    /* Path: (/vendor/lib/soundfx/libvx.so) */
    bool libvx_exist;
    bool vx_force_stereo;
    /* channel num of effect input */
    int effect_in_ch;
    int AML_DTS_index;
    //native private process handle: AI AQ
    void *ai_handle;
    //if any effect is do process() should hold dev->effects_lock
    pthread_mutex_t lock;
    struct aml_post_effect_ctrl effect_ctrl;

    /* native effect chain input data format from audio hal */
    audio_format_t src_format;
    audio_format_t proc_format;
    void *temp_proc_buffer;
    size_t temp_proc_capacity;
    size_t temp_proc_bytes;
    void *temp_vx_proc_buffer;
    size_t temp_vx_proc_capacity;
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
size_t audio_post_process(struct aml_native_postprocess *native_postprocess, void *in_buffer, size_t in_frames);
int audio_VX_post_process(struct aml_native_postprocess *native_postprocess, int16_t *in_buffer, size_t bytes);
int aml_add_audio_effect(struct aml_native_postprocess *native_postprocess, effect_handle_t effect, audio_port_handle_t port_handle __unused);
int aml_remove_audio_effect(struct aml_native_postprocess *native_postprocess, effect_handle_t effect, audio_port_handle_t port_handle __unused);

void VirtualX_decoder_type_config(struct aml_native_postprocess *native_postprocess, int dts_lib_type);
void VirtualX_reset(struct aml_native_postprocess *native_postprocess);
void VirtualX_Channel_reconfig(struct aml_native_postprocess *native_postprocess, int ch_num);
int set_aml_dts_effect_param(struct aml_native_postprocess *native_postprocess, char *param);
int get_aml_dts_effect_param(struct aml_native_postprocess *native_postprocess, char *param, const char *keys);

//native_postprocess context init & release
bool is_vendor_support_libvx(struct aml_native_postprocess *native_postprocess);

/*
 *@brief audio_post_process
 * source_format:
 *      input effect chain data format, support PCM16, PCM32
 */
int init_vendor_post_process(struct aml_native_postprocess *native_postprocess, audio_format_t source_format);
void destroy_vendor_post_process(struct aml_native_postprocess *native_postprocess);
#endif
