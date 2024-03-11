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

#define LOG_TAG "audio_hw_hal_ms12v2"
//#define LOG_NDEBUG 0
#define __USE_GNU

#include <cutils/log.h>
#include "audio_hw_ms12.h"
#include "audio_hw.h"
#include "aml_audio_stream.h"


/*this enum should be same with ms12 lib*/
typedef enum {
    MS12_SYNC_AUDIO_UNKNOWN = 0,
    MS12_SYNC_AUDIO_NORMAL_OUTPUT,
    MS12_SYNC_AUDIO_DROP_PCM,
    MS12_SYNC_AUDIO_INSERT,
    MS12_SYNC_AUDIO_HOLD,
    MS12_SYNC_AUDIO_MUTE,
    MS12_SYNC_AUDIO_RESAMPLE,
    MS12_SYNC_AUDIO_ADJUST_CLOCK,
} MS12_Sync_Policy;

typedef struct Aml_MS12_SyncPolicy_s {
    MS12_Sync_Policy eSyncPolicy;
    int s32TagFrame;
    int s32CurFrame;
} Aml_MS12_SyncPolicy_t;


typedef struct Aml_MS12_Delay_s {
    unsigned int u32DelayFrame;
    unsigned long long u64DelayTimeStamp;
} Aml_MS12_Delay_t;


unsigned int get_ms12_buffer_latency(struct aml_stream_out *out __unused)
{
    return 0;
}




bool is_platform_supported_ddp_atmos(bool atmos_supported __unused, audio_devices_t cur_out_devices __unused, bool is_tv __unused)
{
    return false;
}

bool is_ms12_out_ddp_5_1_suitable(bool is_ddp_atmos __unused)
{
    return false;
}


audio_format_t ms12_get_audio_hal_format(audio_format_t hal_format __unused)
{
    return 0;
}


void set_ms12_ad_mixing_enable(struct dolby_ms12_desc *ms12 __unused, int ad_mixing_enable __unused)
{
    return;
}

void set_ms12_ad_mixing_level(struct dolby_ms12_desc *ms12 __unused, int mixing_level __unused)
{
    return;
}

void set_ms12_ad_vol(struct dolby_ms12_desc *ms12 __unused, int ad_vol __unused)
{
    return;
}


void set_dolby_ms12_runtime_system_mixing_enable(struct dolby_ms12_desc *ms12 __unused, int system_mixing_enable __unused)
{
    return;
}

void set_ms12_atmos_lock(struct dolby_ms12_desc *ms12 __unused, bool is_atmos_lock_on __unused)
{
    return;
}

void set_ms12_acmod2ch_lock(struct dolby_ms12_desc *ms12 __unused, bool is_lock_on __unused)
{
    return;
}

void set_ms12_chmod_lock(struct dolby_ms12_desc *ms12 __unused, bool is_lock_on __unused)
{
    return;
}
void set_ms12_main_volume(struct dolby_ms12_desc *ms12 __unused, float volume __unused) {
    return;
}


void set_ms12_ac4_presentation_group_index(struct dolby_ms12_desc *ms12 __unused, int index __unused)
{
    return;
}


void set_ms12_drc_boost_value_for_2ch_downmixed_output(struct dolby_ms12_desc *ms12 __unused, int boost __unused)
{
    return;
}

void set_ms12_drc_cut_value_for_2ch_downmixed_output(struct dolby_ms12_desc *ms12 __unused, int cut __unused)
{
    return;

}
void set_ms12_drc_mode_for_2ch_downmixed_output(struct dolby_ms12_desc *ms12 __unused, bool drc_mode __unused)
{
    return;

}

void set_ms12_drc_boost_value(struct dolby_ms12_desc *ms12 __unused, int boost __unused)
{
    return;

}

void set_ms12_drc_cut_value(struct dolby_ms12_desc *ms12 __unused, int cut __unused)
{
    return;

}

void set_ms12_dap_postgain(struct dolby_ms12_desc *ms12 __unused, int postgain __unused)
{
    return;
}


void set_ms12_drc_mode_for_multichannel_and_dap_output(struct dolby_ms12_desc *ms12 __unused, bool drc_mode __unused)
{
    return;
}

void set_ms12_fade_pan
    (struct dolby_ms12_desc *ms12 __unused
    , int fade_byte __unused
    , int gain_byte_center __unused
    , int gain_byte_front __unused
    , int gain_byte_surround __unused
    , int pan_byte __unused
    )
{
    return;
}


void set_ms12_main_audio_pts(struct dolby_ms12_desc *ms12 __unused, uint64_t apts __unused, uint64_t bytes_offset __unused)
{
    return;
}

void set_ms12_main1_audio_pts(struct dolby_ms12_desc *ms12 __unused, uint64_t apts __unused, uint64_t bytes_offset __unused)
{
    return;
}

int get_ms12_mat_dec_delay() {
    return 0;
}


void dynamic_set_dolby_ms12_drc_parameters(struct dolby_ms12_desc *ms12 __unused)
{
    return;
}

void set_ms12_main_audio_mute(struct dolby_ms12_desc *ms12 __unused, bool b_mute __unused, unsigned int duration __unused)
{
    return;
}

void set_dolby_ms12_drc_parameters(audio_format_t input_format __unused, int output_config_mask __unused)
{
    return;
}

void set_dolby_ms12_main_speed(struct dolby_ms12_desc *ms12 __unused, double speed __unused)
{
    return;
}


void update_drc_parameter_when_output_config_changed(struct dolby_ms12_desc *ms12 __unused)
{
    return;
}


/*
 *@brief get dolby ms12 prepared
 */
int get_the_dolby_ms12_prepared(
    struct aml_stream_out *aml_out __unused
    , audio_format_t input_format __unused
    , audio_channel_mask_t input_channel_mask __unused
    , int input_sample_rate __unused)
{
    return 0;
}

bool is_ms12_passthrough(struct audio_stream_out *stream __unused) {
    return false;
}


/*
 *@brief dolby ms12 main process
 *
 * input parameters
 *     stream: audio_stream_out handle
 *     buffer: data buffer address
 *     bytes: data size
 * output parameters
 *     use_size: buffer used size
 */

int dolby_ms12_main_process(
    struct audio_stream_out *stream __unused
    , const void *buffer __unused
    , size_t bytes __unused
    , size_t *use_size __unused)
{
    return 0;
}



/*
 *@brief dolby ms12 system process
 *
 * input parameters
 *     stream: audio_stream_out handle
 *     buffer: data buffer address
 *     bytes: data size
 * output parameters
 *     use_size: buffer used size
 */
int dolby_ms12_system_process(
    struct audio_stream_out *stream __unused
    , const void *buffer __unused
    , size_t bytes __unused
    , size_t *use_size __unused)
{
    return 0;
}



/*
 *@brief dolby ms12 app process
 *
 * input parameters
 *     stream: audio_stream_out handle
 *     buffer: data buffer address
 *     bytes: data size
 * output parameters
 *     use_size: buffer used size
 */
int dolby_ms12_app_process(
    struct audio_stream_out *stream __unused
    , const void *buffer __unused
    , size_t bytes __unused
    , size_t *use_size __unused)
{
    return 0;
}



int dolby_ms12_multi_app_process(
    struct dolby_ms12_desc *ms12 __unused
    , const void *buffer __unused
    , size_t bytes __unused
    , size_t *use_size __unused
    , const struct audioCfg *pstAudioConfig __unused
    , bool bConfigUpdate __unused)
{
    return 0;
}



/*
 *@brief get dolby ms12 cleanup
 */
int get_dolby_ms12_cleanup(struct dolby_ms12_desc *ms12 __unused, bool set_non_continuous __unused)
{
    return 0;
}


/*
 *@brief set dolby ms12 primary gain
 */
int set_dolby_ms12_primary_input_db_gain(struct dolby_ms12_desc *ms12 __unused, int db_gain  __unused, int duration __unused)
{
    return 0;
}

int ac3_and_eac3_bypass_process(struct audio_stream_out *stream __unused, void *buffer __unused, size_t bytes __unused) {
    return 0;
}

int dolby_truehd_bypass_process(struct audio_stream_out *stream __unused, void *buffer __unused, size_t bytes __unused) {
    return 0;
}

int mat_bypass_process(struct audio_stream_out *stream __unused, void *buffer __unused, size_t bytes __unused) {
    return 0;
}


int dolby_ms12_bypass_process(struct audio_stream_out *stream __unused, void *buffer __unused, size_t bytes __unused) {
    return 0;
}


int master_pcm_type(struct aml_stream_out *aml_out __unused) {
    return 0;
}

int ms12_passthrough_output(struct aml_stream_out *aml_out __unused) {
    return 0;
}

int dap_pcm_output(void *buffer __unused, void *priv_data __unused, size_t size __unused, aml_ms12_dec_info_t *ms12_info __unused)
{
    return 0;
}


int stereo_pcm_output(void *buffer __unused, void *priv_data __unused, size_t size __unused, aml_ms12_dec_info_t *ms12_info __unused)
{
    return 0;
}

int bitstream_output(void *buffer __unused, void *priv_data __unused, size_t size __unused)
{
    return 0;
}


int spdif_bitstream_output(void *buffer __unused, void *priv_data __unused, size_t size __unused)
{
    return 0;
}


int mat_bitstream_output(void *buffer __unused, void *priv_data __unused, size_t size __unused)
{
    return 0;
}

int mc_pcm_output(void *buffer __unused, void *priv_data __unused, size_t size __unused, aml_ms12_dec_info_t *ms12_info __unused)
{
    return 0;
}

int ms12_sync_callback(void *priv_data __unused, unsigned long long u64DecOutFrame __unused, Aml_MS12_Delay_t stDelay __unused, Aml_MS12_SyncPolicy_t syncpolicy_status __unused __unused) {
    return 0;
}

#ifdef ENABLE_DVB_PATCH
int ms12_dtv_sync_callback(void *priv_data __unused, unsigned long long u64DecOutFrame __unused, Aml_MS12_Delay_t stDelay __unused, Aml_MS12_SyncPolicy_t syncpolicy_status __unused) {
    return 0;
}

void ms12_do_dtv_sync(struct audio_stream_out *stream __unused)
{
    return;
}

#endif

int dolby_ms12_get_latency(audio_format_t output_format __unused, int pcm_type __unused)
{
    return 0;
}

int ms12_output(void *buffer __unused, void *priv_data __unused, size_t size __unused, aml_ms12_dec_info_t *ms12_info __unused)
{
    return 0;
}


int ms12_scaletempo(void *priv_data __unused, void *info __unused) {
    return 0;
}




int set_system_app_mixing_status(struct aml_stream_out *aml_out __unused, int stream_status __unused)
{
    return 0;
}


int dolby_ms12_main_open(struct audio_stream_out *stream __unused) {
    return 0;
}

int dolby_ms12_main_close(struct audio_stream_out *stream __unused) {
    return 0;
}

int dolby_ms12_main_flush(struct audio_stream_out *stream __unused) {
    return 0;
}

int dolby_ms12_encoder_reconfig(struct dolby_ms12_desc *ms12 __unused) {
    return 0;
}

void dolby_ms12_app_flush()
{
    return;
}

void dolby_ms12_enable_debug()
{
    return;
}

bool is_ms12_continuous_mode(struct aml_audio_device *adev __unused)
{
    return false;
}

bool is_dolby_ms12_main_stream(struct audio_stream_out *stream __unused) {
    return false;
}

bool is_support_ms12_reset(struct audio_stream_out *stream __unused) {
    return false;
}

bool is_bypass_dolbyms12(struct audio_stream_out *stream __unused)
{
    return false;
}


bool is_audio_postprocessing_add_dolbyms12_dap(struct aml_audio_device *adev __unused)
{
    return false;
}


bool is_dolbyms12_dap_enable(struct aml_stream_out *aml_out __unused) {
    return false;
}

int dolby_ms12_hwsync_init(void) {
    return 0;
}

int dolby_ms12_hwsync_release(void) {
    return 0;
}

int dolby_ms12_hwsync_checkin_pts(int offset __unused, int apts __unused) {
    return 0;
}

int dolby_ms12_output_insert_oneframe(struct audio_stream_out *stream __unused) {
    return 0;
}

uint64_t dolby_ms12_get_main_bytes_consumed(struct audio_stream_out *stream __unused) {
    return 0;
}

uint64_t dolby_ms12_get_main_pcm_generated(struct audio_stream_out *stream __unused) {
    return 0;
}

bool is_rebuild_the_ms12_pipeline(    audio_format_t main_input_fmt __unused, audio_format_t hal_internal_format __unused)
{
    return false;
}


bool is_need_reset_ms12_continuous(struct audio_stream_out *stream __unused) {
    return false;
}

bool is_ms12_output_compatible(struct audio_stream_out *stream __unused, audio_format_t new_sink_format __unused, audio_format_t new_optical_format __unused) {
    return false;
}

int dolby_ms12_main_pipeline_latency_frames(struct audio_stream_out *stream __unused) {
    return 0;
}



int dolby_ms12_main_resume_prepare(struct audio_stream_out *stream __unused)
{
    return 0;
}


void set_ms12_set_compressor_profile(struct dolby_ms12_desc *ms12 __unused, int profile __unused)
{
    return;
}

int aml_dap_open(
    struct aml_stream_out *aml_out __unused
    , audio_format_t input_format __unused
    , audio_channel_mask_t input_channel_mask __unused
    , int input_sample_rate __unused)
{
    return 0;
}


int aml_dap_close(struct dolby_ms12_desc *ms12 __unused)
{
    return 0;
}


int aml_dap_process(
    struct audio_stream_out *stream __unused
    , const void *buffer __unused
    , size_t bytes __unused
    , size_t *use_size __unused)
{
    return 0;
}

void set_dolby_ms12_continuous_state(struct dolby_ms12_desc *ms12 __unused, int state __unused) {
    return;
}

