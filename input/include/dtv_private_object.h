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


#ifndef DTV_PRIVATE_OBJECT_H_
#define DTV_PRIVATE_OBJECT_H_

#include <sys/types.h>
#include <pthread.h>

struct dtv_private_object {
    int dtv_aformat;
    unsigned int dtv_i2s_clock;
    unsigned int dtv_spdif_clock;
    unsigned int dtv_droppcm_size;

    void *aml_dtv_audio_instances;
    pthread_mutex_t dtv_lock;
    /*
      AudioHalWrapper set volume , dtv_volume range [0, 1]
      set process: TsPlayer::SetAudioVolume(volume) -> dtv_volume
    */
    float dtv_volume; // Todo: This parameter is not used yet
    //int dtvsync_enable;
    //float output_speed;
    //int sub_apid;
    //int sub_afmt;
    //int pid;
    //int demux_id;
    bool is_multi_demux;
    int dtv_sound_mode;
    /*
      four variable used for when audio discontinue and underrun,
      whether mute output
    */
    int discontinue_mute_flag;
    int audio_discontinue;
    int no_underrun_count;
    int no_underrun_max;

    int start_mute_flag;
    int start_mute_count;
    int start_mute_max;
    int underrun_mute_flag;
    int ad_start_enable;
    // mute flag after insert policy
    bool insert_mute_flag;
};
typedef struct checkoutptsoffset {
    uint64_t offset;
    uint64_t pts_90k;
    uint64_t pts_64;
} checkout_pts_offset;
struct aml_audio_device;
struct aml_dtv_audio_instances;

int get_dtv_aformat(struct aml_audio_device *adev);
void set_dtv_aformat(struct aml_audio_device *adev, int format);

uint32_t get_dtv_i2s_clock(struct aml_audio_device *adev);
void set_dtv_i2s_clock(struct aml_audio_device *adev, uint32_t i2s_clock);

uint32_t get_dtv_spdif_clock(struct aml_audio_device *adev);
void set_dtv_spdif_clock(struct aml_audio_device *ade, uint32_t spdif_clock);

uint32_t get_dtv_droppcm_size(struct aml_audio_device *adev);
void set_dtv_droppcm_size(struct aml_audio_device *adev, uint32_t drop_size);

struct aml_dtv_audio_instances * get_dtv_audio_instance(struct aml_audio_device *adev);

int get_dtv_sound_mode(struct aml_audio_device *adev);
void set_dtv_sound_mode(struct aml_audio_device *adev, int sound_mode);

void set_dtv_volume(struct aml_audio_device *adev, float volume);
float get_dtv_volume(struct aml_audio_device *adev);

void enable_dtv_multi_demux(struct aml_audio_device *adev, int enable);
bool is_dtv_multi_demux(struct aml_audio_device *adev);

void enable_dtv_discontinue_mute(struct aml_audio_device *adev, bool enable);
bool is_dtv_discontinue_mute(struct aml_audio_device *adev);

void enable_dtv_audio_discontinue(struct aml_audio_device *adev, bool enable);
bool is_dtv_audio_discontinue(struct aml_audio_device *adev);

int get_dtv_no_underrun_count(struct aml_audio_device *adev);
void set_dtv_no_underrun_count(struct aml_audio_device *adev, int count);
int inc_dtv_no_underrun_count(struct aml_audio_device *adev);

int get_dtv_no_underrun_max(struct aml_audio_device *adev);
void set_dtv_no_underrun_max(struct aml_audio_device *adev, int max);

bool is_dtv_start_mute(struct aml_audio_device *adev);
void enable_dtv_start_mute_flag(struct aml_audio_device *adev, bool enable);
int get_dtv_start_mute_count(struct aml_audio_device *adev);
void set_dtv_start_mute_count(struct aml_audio_device *adev, int mute_count);
int get_dtv_start_mute_max(struct aml_audio_device *adev);
void set_dtv_start_mute_max(struct aml_audio_device *adev, int max);

void enable_dtv_underrun_mute(struct aml_audio_device *adev, bool enable);
bool is_dtv_underrun_mute(struct aml_audio_device *adev);

void enable_dtv_ad_start(struct aml_audio_device *adev, bool enable);
bool is_dtv_ad_start(struct aml_audio_device *adev);

void enable_dtv_insert_mute(struct aml_audio_device *adev, bool enable);
bool is_dtv_insert_mute(struct aml_audio_device *adev);

//lock & unlock
void acquire_dtv_mutex_lock(struct aml_audio_device *adev);
void release_dtv_mutex_lock(struct aml_audio_device *adev);
//new & init & release instance
int init_dtv_object(struct aml_audio_device *adev);
int destroy_dtv_object(struct aml_audio_device *adev);
#endif