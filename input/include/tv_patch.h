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

#ifndef _TV_PATCH_H_
#define _TV_PATCH_H_


int create_patch(struct audio_hw_device *dev, audio_devices_t input, audio_devices_t output);
int release_patch(struct aml_audio_device *aml_dev);
int set_tv_source_switch_parameters(struct audio_hw_device *dev, struct str_parms *parms);


#endif /* _TV_PATCH_H_ */

