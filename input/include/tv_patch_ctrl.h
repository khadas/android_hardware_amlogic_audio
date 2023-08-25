/*
* Copyright 2023 Amlogic Inc. All rights reserved.
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

#ifndef _TV_PATCH_CTRL_H_
#define _TV_PATCH_CTRL_H_


/*==================================input commands=========================================*/
int input_stream_channels_adjust(struct audio_stream_in *stream, void* buffer, size_t bytes);

bool is_HBR_stream(struct audio_stream_in *stream);
bool is_game_mode(struct aml_audio_device *aml_dev);
void aml_check_pic_mode(struct aml_audio_patch *patch);
bool signal_status_check(audio_devices_t in_device, int *mute_time,
                         struct audio_stream_in *stream);

/*
*@brief check tv signal need to mute or not
* return false if signal need to mute
*/
bool check_tv_stream_signal (struct audio_stream_in *stream);

/*
*@brief check digital-in signal need to mute(PAUSE/MUTE) or not
* return false if signal need to mute
*/
bool check_digital_in_stream_signal(struct audio_stream_in *stream);

/*
 * @brief set HDMIIN audio mode: "SPDIF", "I2S", "TDM"
 * return negative if fails.
 */
int set_hdmiin_audio_mode(struct aml_mixer_handle *mixer_handle, char *mode);

enum hdmiin_audio_mode {
    HDMIIN_MODE_SPDIF = 0,
    HDMIIN_MODE_I2S   = 1,
    HDMIIN_MODE_TDM   = 2
};
enum hdmiin_audio_mode get_hdmiin_audio_mode(struct aml_mixer_handle *mixer_handle);


void audio_raw_data_continuous_check(struct aml_audio_device *aml_dev, audio_type_parse_t *status, char *buffer, int size);
int reconfig_read_param_through_hdmiin(struct aml_audio_device *aml_dev,
                                       struct aml_stream_in *stream_in,
                                       ring_buffer_t *ringbuffer, int buffer_size);
int stream_check_reconfig_param(struct audio_stream_out *stream);


/*==================================mixer control commands=========================================*/
int set_audio_source(struct aml_mixer_handle *mixer_handle,
        enum input_source audio_source, bool is_auge);
int enable_HW_resample(struct aml_mixer_handle *mixer_handle, int enable_sr);
int get_spdifin_samplerate(struct aml_mixer_handle *mixer_handle);
int get_hdmiin_samplerate(struct aml_mixer_handle *mixer_handle);
int get_hdmiin_channel(struct aml_mixer_handle *mixer_handle);
hdmiin_audio_packet_t get_hdmiin_audio_packet(struct aml_mixer_handle *mixer_handle);
int set_resample_source(struct aml_mixer_handle *mixer_handle, enum ResampleSource source);
int set_spdifin_pao(struct aml_mixer_handle *mixer_handle,int enable);

/*@brief check the hdmi rx audio stability by HW register */
bool is_hdmi_in_stable_hw(struct audio_stream_in *stream);
bool is_spdif_in_stable_hw(struct audio_stream_in *stream);
/*@brief check the hdmi rx audio format stability by SW parser */
bool is_hdmi_in_stable_sw(struct audio_stream_in *stream);
/*@brief check the ATV audio stability by HW register */
bool is_atv_in_stable_hw(struct audio_stream_in *stream);
/*@brief check the AV audio stability by HW register */
bool is_av_in_stable_hw(struct audio_stream_in *stream);
bool is_hdmi_in_sample_rate_changed(struct audio_stream_in *stream);
bool is_hdmi_in_hw_format_change(struct audio_stream_in *stream);


#endif /* _TV_PATCH_CTRL_H_ */

