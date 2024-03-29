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

#ifndef _AUDIO_HW_MS12_COMMON_H_
#define _AUDIO_HW_MS12_COMMON_H_

#include <tinyalsa/asoundlib.h>
#include <system/audio.h>
#include <stdbool.h>
#include <aml_audio_ms12.h>

#include "audio_hw.h"


#define DISABLE_CONTINUOUS_OUTPUT "persist.vendor.audio.continuous.disable"

#define DOLBY_DRC_LINE_MODE 0
#define DOLBY_DRC_RF_MODE   1

/*
 *@brief define enum for MS12 message type
 */
typedef enum ms12_mesg_type {
    MS12_MESG_TYPE_NONE = 0,
    MS12_MESG_TYPE_FLUSH,
    MS12_MESG_TYPE_PAUSE,
    MS12_MESG_TYPE_RESUME,
    MS12_MESG_TYPE_SET_MAIN_DUMMY,
    MS12_MESG_TYPE_UPDATE_RUNTIME_PARAMS,
    MS12_MESG_TYPE_RESET_MS12_ENCODER,
    MS12_MESG_TYPE_EXIT_THREAD,
    MS12_MESG_TYPE_SCHEDULER_STATE,
    MS12_MESG_TYPE_MAX,
}ms12_mesg_type_t;

/*
 *@brief define ms12 message struct.
 */
struct ms12_mesg_desc {
    struct listnode list;
    ms12_mesg_type_t mesg_type;
    /* define a Zero Length Array to extend for audio data in the future. */
    //char data[0];
};

/*
 *@brief set dolby ms12 pause/resume
 */
int set_dolby_ms12_runtime_pause(struct dolby_ms12_desc *ms12, int is_pause);
int dolby_ms12_main_pause(struct audio_stream_out *stream);
int dolby_ms12_main_resume(struct audio_stream_out *stream);

/*
 *@brief send scheduler state to ms12
 */
int aml_audiohal_sch_state_2_ms12(struct dolby_ms12_desc *ms12, int sch_state);
/*
 *@brief set dolby ms12 scheduler state
 */
int aml_set_ms12_scheduler_state(struct dolby_ms12_desc *ms12);
/*
 *@brief check whether the mesg list is empty
 */
bool ms12_msg_list_is_empty(struct dolby_ms12_desc *ms12);

/*
 *@brief check whether the mesg list is empty
 */

bool ms12_msg_list_is_empty(struct dolby_ms12_desc *ms12);

/*
 *@brief Receive message from audiohal to ms12.
 */
int audiohal_send_msg_2_ms12(struct dolby_ms12_desc *ms12, ms12_mesg_type_t mesg_type);
/*
 *@brief ms12 message thread create and destroy function.
 */
int ms12_mesg_thread_create(struct dolby_ms12_desc *ms12);
int ms12_mesg_thread_destroy(struct dolby_ms12_desc *ms12);

int aml_send_ms12_scheduler_state_2_ms12(void);
void ms12_timer_callback_handler(union sigval sigv);

bool is_ad_data_available(int digital_audio_mode);
void set_continuous_audio_mode(struct aml_audio_device *adev, int enable, int is_suspend);

/* @brief set ms12 full dap disable as full_dap_disable [0/1] */
void set_ms12_full_dap_disable(struct dolby_ms12_desc *ms12, int full_dap_disable);

/* @brief set ms12 multi_channel enable [0/1] */
void set_ms12_mc_enable(struct dolby_ms12_desc *ms12, int mc_enable);


//-lang               * <str> [ac4] 1st Preferred Language code (3 Letter ISO 639)
void set_ms12_ac4_1st_preferred_language_code(struct dolby_ms12_desc *ms12, char *lang_iso639_code);
//-lang2              * <str> [ac4] 2nd Preferred Language code (3 Letter ISO 639)
void set_ms12_ac4_2nd_preferred_language_code(struct dolby_ms12_desc *ms12, char *lang_iso639_code);
//-pat                * <int> [ac4] Prefer Presentation Selection by associated type over language.
//                            0: Prefer selection by language
//                            1: Prefer selection by associated type (default)
#define PERFER_SELECTION_BY_LANGUAGE (0)
#define PERFER_SELECTION_BY_AD_TYPE  (1)
void set_ms12_ac4_prefer_presentation_selection_by_associated_type_over_language(struct dolby_ms12_desc *ms12, int prefer_selection_type);

//-ac4_short_prog_id  * <int> [ac4] The short program identifier as an 16 bit unsigned value or -1 for no program ID (default)
void set_ms12_ac4_short_prog_identifier(struct dolby_ms12_desc *ms12, int short_program_identifier);

/*
 *@brief get the mat decoder delay
 */
int get_ms12_mat_dec_delay(void);

/*
 *@brief on non-TV device, dtv to set ms12 volume
 */
void dtv_set_ms12_volume_on_non_TV_device(struct aml_stream_out *aml_out);


void set_ms12_ext_pcm_acmod_lfe(struct dolby_ms12_desc *ms12, audio_channel_mask_t channel_mask);

void set_ms12_sys_pcm_acmod_lfe(struct dolby_ms12_desc *ms12, audio_channel_mask_t channel_mask);

void set_ms12_app_pcm_acmod_lfe(struct dolby_ms12_desc *ms12, audio_channel_mask_t channel_mask);

/*update drc/cut/boost for stereo pcm output*/
void set_ms12_drc_boost_value_for_2ch_downmixed_output(struct dolby_ms12_desc *ms12, int boost);

void set_ms12_drc_cut_value_for_2ch_downmixed_output(struct dolby_ms12_desc *ms12, int cut);

void set_ms12_drc_mode_for_2ch_downmixed_output(struct dolby_ms12_desc *ms12, bool drc_mode);

/*update drc/cut/boost for dap or multi-pcm output*/
void set_ms12_drc_boost_value(struct dolby_ms12_desc *ms12, int boost);

void set_ms12_drc_cut_value(struct dolby_ms12_desc *ms12, int cut);

void set_ms12_drc_mode_for_multichannel_and_dap_output(struct dolby_ms12_desc *ms12, bool drc_mode);

void set_ms12_drc_parameters_for_2ch_downmix_output(struct dolby_ms12_desc *aml_ms12, int drc_mode, int drc_cut, int drc_boost);
void set_ms12_drc_parameters_for_multi_and_dap_output(struct dolby_ms12_desc *aml_ms12, int drc_mode, int drc_cut, int drc_boost);
void dynamic_get_dolby_ms12_drc_parameters(struct aml_audio_device *adev, struct dolby_ms12_desc *ms12, audio_format_t format);
void set_ms12_drc_params_for_stereo_and_dap_multi_pcm_output(
    struct aml_audio_device *adev
    , struct dolby_ms12_desc *ms12
    , audio_format_t format);

int get_ms12_syss_mixgain_target(void);

void pcm_data_do_pre_attenuation(
    const void *buffer
    , size_t bytes
    , bool dolby_ms12_enable
    , bool is_valid_patch
    , bool is_drc_rf_mode
    , int syss_target
    , int bytes_per_sample
    );
#endif //end of _AUDIO_HW_MS12_COMMON_H_
