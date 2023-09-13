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

#ifndef AML_AUDIO_HW_RESOURCE_MANAGER_H_
#define AML_AUDIO_HW_RESOURCE_MANAGER_H_

#include <sys/types.h>
#include <system/audio.h>

#include "audio_hw_resource_def.h"

struct aml_audio_device;
struct aml_mixer_handle;
struct audio_hw_device;
struct audio_hw_resource_mgr;
struct aml_mixer_handle;

/* set or get inport gain */
enum IN_PORT get_active_inport(struct aml_audio_device *adev);
void set_inport_gain(struct aml_audio_device *adev, enum IN_PORT port, float gain);
float get_inport_gain(struct aml_audio_device *adev, enum IN_PORT port);
float get_active_inport_gain(struct aml_audio_device *adev);

/* check HDMI & ARC connected flag */
bool is_HDMI_connected(struct aml_audio_device *adev);
bool is_HDMI_reconnected(struct aml_audio_device *adev);
void set_HDMI_reconnected_flag(struct aml_audio_device *adev, bool enable);
bool is_arc_connected(struct aml_audio_device *adev);

/* get already routed or connected devices in audio hal */
audio_devices_t get_avail_in_devices(struct aml_audio_device *adev);
audio_devices_t get_avail_out_devices(struct aml_audio_device *adev);

void enable_device_force_routing(struct aml_audio_device *adev, bool enable);

/*
* Function: enable or disable input device routing
*  - do new device routing
*  - add new device to routing list
*  - add new device to available list
*/
int do_output_device_routing(struct aml_audio_device *adev, audio_devices_t out_device, bool enable);

/*
* Function: enable or disable output device routing
*   - do new device routing
*   - add new device to routing list
*   - add new device to available list
*/
int do_input_device_routing(struct aml_audio_device *adev, audio_devices_t in_device, bool enable);

int set_audio_source_routing(struct aml_audio_device *adev, enum input_source audio_source);

/*
* Function: add or remove new input device to Audio HAL
* Note: only update available input device list, Not do routing
*/
int set_input_device_avail(struct aml_audio_device *adev, audio_devices_t in_device, bool enable);

/*
* Function: add or remove new output device to Audio HAL
* Note: only update available output device list, Not do routing
*/
int set_output_device_avail(struct aml_audio_device *adev, audio_devices_t device, bool enable);

/*
* Function: set output device mute or un-mute
*  Parameters:
*   -enable: true (mute), false (un-mute)
*   -use_fade: Using HW fade in/out unit impl device mute effect
*/
int set_output_device_mute(struct aml_audio_device *adev, audio_devices_t device, bool enable, bool use_fade);

/* get platform type */
bool is_TV(struct aml_audio_device *adev);
bool is_BDS(struct aml_audio_device *adev);
bool is_SBR(struct aml_audio_device *adev);
bool is_STB(struct aml_audio_device *adev);

/* new & init & release & get uniq instance */
struct audio_hw_resource_mgr *get_hw_resource_manger(struct aml_audio_device *adev);
int init_audio_hw_resource_mgr(struct aml_audio_device *adev, struct aml_mixer_handle *mixer_ctrl);
void destroy_hw_resource_mgr(struct aml_audio_device *adev);

#endif
