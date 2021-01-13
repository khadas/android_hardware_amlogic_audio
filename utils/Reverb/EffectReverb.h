/*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef ANDROID_EFFECTREVERB_H_
#define ANDROID_EFFECTREVERB_H_

#define MAX_NUM_BANDS           5
#define MAX_CALL_SIZE           512
#define LVREV_MAX_T60           7000
#define LVREV_MAX_REVERB_LEVEL  2000
#define LVREV_MAX_FRAME_SIZE    5120
#define LVREV_CUP_LOAD_ARM9E    470    // Expressed in 0.1 MIPS
#define LVREV_MEM_USAGE         (71+(LVREV_MAX_FRAME_SIZE>>7))     // Expressed in kB

typedef enum
{
    REVERB_PRESET_NONE,
    REVERB_PRESET_SMALLROOM,
    REVERB_PRESET_MEDIUMROOM,
    REVERB_PRESET_LARGEROOM,
    REVERB_PRESET_MEDIUMHALL,
    REVERB_PRESET_LARGEHALL,
    REVERB_PRESET_PLATE,
    REVERB_PRESET_LAST = REVERB_PRESET_PLATE
} t_reverb_presets;

int AML_Reverb_Init(void **reverb_handle);
int AML_Reverb_Process(void *reverb_handle, int16_t *inBuffer, int16_t *outBuffer, int frameCount);
int AML_Reverb_Release(void *reverb_handle);
void Set_AML_Reverb_Mode(void *reverb_handle, t_reverb_presets mode);

#endif /*ANDROID_EFFECTREVERB_H_*/
