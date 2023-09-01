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

#ifndef NEU_IVA_SCENE_HCS_COMMON_H
#define NEU_IVA_SCENE_HCS_COMMON_H

#define ASC_INVALID_LABEL (-100)

typedef struct scene_class_context_struct *scene_class_ctx_pd_handle_t;

/**
 * audio scene classification mode
 */
typedef enum
{
    NEU_IVA_SCENE_CLASS_TYPE = 0x1
} SCENE_CLASS_MODE;

/**
 * struct of output result
 */
typedef struct
{
    int inference_enable; // 1: inference a time: 0: not inference
    int instant_label;    // instant label: 0~10 (note: current num of classification)
    float instant_score;  // instant score

    int gradual_label;    // gradual label
    float gradual_score;  // gradual score
} neu_iva_scene_class_result_t;

/**
 * struct of input parameter
 */
typedef struct
{
    int post_used_size; // size used by the postprocessing
    int post_hop_size;  // interval size for the postprocessing
} scene_class_parameter_t;

/**
 * struct of input parameter
 */
typedef struct neu_iva_scene_class_parameter
{
    scene_class_parameter_t t_scene_class_param;
} neu_iva_scene_class_parameter_t;

/**
 * module结构体，该结构体由上层创建，主要包含模型句柄以及各个模式的上下文context
 */
typedef struct
{
    scene_class_ctx_pd_handle_t ctx_pd; // audio scene classification 功能上下文
} neu_iva_scene_class_struct_t, *neu_iva_scene_class_handle_t;

#endif
