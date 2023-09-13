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

#ifndef AML_DEVICE_PATCH_MGR_H_
#define AML_DEVICE_PATCH_MGR_H_

#include <system/audio.h>
#include <sys/types.h>

#include "device_patch.h"

struct aml_audio_patch;
struct patch_manager;
struct component_noise_gate;
struct component_picture_mode;
struct dtv_private_object;
struct tv_private_object;

//get components defined in patch manager
struct component_noise_gate *get_noise_gate_instance(struct aml_audio_device *adev);
struct component_picture_mode *get_pic_mode_instance(struct aml_audio_device *adev);
struct dtv_private_object *get_dtv_object(struct aml_audio_device *adev);
struct tv_private_object *get_tv_object(struct aml_audio_device *adev);

bool is_dev_patch_exist(struct aml_audio_device *adev);

bool is_dtv_patch_exist(struct aml_audio_device *adev);

struct aml_audio_patch *get_dev_patch(struct aml_audio_device *adev);

void set_dev_patch(struct aml_audio_device *adev, struct aml_audio_patch *audio_patch);

bool is_dev_patch_running(struct aml_audio_device *adev);

void set_dev_patch_running(struct aml_audio_device *adev, bool enable);

void set_dev_patch_src(struct aml_audio_device *adev, enum patch_src_assortion patch_src);

int get_dev_patch_src(struct aml_audio_device *adev);

bool is_same_patch_src(struct aml_audio_device *adev, enum patch_src_assortion patch_src);

bool is_dev_patch_valid(struct aml_audio_device *adev);

/*
  Function: set patch_mgr::valid flag = false
  Description:
    audio_patch used in mult-thread ENV, other thread can't use patch
    when release patch happen.
*/
void invalidate_dev_patch(struct aml_audio_device *adev);

/*
  Function: set patch_mgr::valid flag = false
  Description:
    audio_patch used in mult-thread ENV, other thread can use patch
    when create patch done.
*/
void validate_dev_patch(struct aml_audio_device *adev);

void start_dtv_patch(struct aml_audio_device *adev);

void stop_dtv_patch(struct aml_audio_device *adev);

void acquire_dev_patch_lock(struct aml_audio_device *adev);

void release_dev_patch_lock(struct aml_audio_device *adev);

enum patch_src_assortion get_patch_source(struct aml_audio_device *adev, audio_devices_t src_device, int route_type __unused);

int set_tv_source_switch_parameters(struct audio_hw_device *dev, struct str_parms *parms);

/*
  Function: create a device to device audio patch
  Patch graph:
    input device -> patch -> output device
  Parameters:
   - patch_source: where is the input HW signal from
   - input: input device type (android)
   - output: input device type (android)
   - type: patch type
*/
int patch_mgr_create_patch(struct aml_audio_device *adev,
                           int patch_source,
                           audio_devices_t input,
                           audio_devices_t output,
                           int type);

// Function: destroy a device to device audio patch
int patch_mgr_release_patch(struct aml_audio_device *adev, int type);

// Function: get patch_manger instance
struct patch_manager *get_patch_manager(struct aml_audio_device *adev);

// Function: create & init atch manger at adev_open
int init_patch_manager(struct aml_audio_device *adev);

// Function: release all resource hold by patch manger
void destroy_patch_manager(struct aml_audio_device *adev);

#endif
