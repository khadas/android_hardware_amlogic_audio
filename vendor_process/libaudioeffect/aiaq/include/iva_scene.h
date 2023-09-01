/*
* Copyright (C) 2023 Amlogic Corporation.
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

#ifndef NEU_IVA_SCENE_NCNN_H
#define NEU_IVA_SCENE_NCNN_H

//#include <vector>
// #include <glib.h>
// #include <stdint.h>

#include "iva.h"
#include "iva_scene_common.h"

#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * [neu_iva_scene_class_init 根据用户选择的MODE进行模型初始化及加载]
     * @param  mode              [模式，上层可选择多种模式传入，目前支持NEU_IVA_SCENE_CLASS_TYPE]
     * @param  model_path        [模型存放路径，设置为NULL即可]
     * @return                   [module输入参数结构体指针，由上层创建]
     */
    neu_iva_scene_class_handle_t neu_iva_scene_init(unsigned int mode, const char *model_path);

    /**
     * [neu_iva_scene_class_process 一帧处理]
     * @param  iva_scene_handle  [module句柄指针，由上层创建]
     * @param  samples           [输入一帧音频PCM样本buf指针]
     * @param  sampleCount       [输入一帧音频PCM样本长度]
     * @param  results           [输出一帧音频PCM样本处理结果的结构指针]
     * @return                   [返回执行状态]
     */
    IVA_STATUS_E neu_iva_scene_class_process(neu_iva_scene_class_handle_t iva_scene_handle, int16_t *samples, int sampleCount, neu_iva_scene_class_result_t *results);

    /**
     * [neu_iva_scene_class_deinit 析构模型与上下文context]
     * @param  iva_scene_handle  [module句柄指针，由上层创建]
     * @param  mode              [模式，上层可选择多种模式传入，目前支持NEU_IVA_SCENE_CLASS_TYPE]
     * @return                   [返回执行状态]
     */
    IVA_STATUS_E neu_iva_scene_class_deinit(neu_iva_scene_class_handle_t iva_scene_handle, unsigned int mode);

    /**
     * [neu_iva_scene_class_param_set 设置相关参数或阈值]
     * @param  iva_scene_class_handle [module句柄指针，由上层创建]
     * @param  params                 [用户设置的参数阈值]
     * @return                        [返回执行状态]
     */
    IVA_STATUS_E neu_iva_scene_class_param_set(neu_iva_scene_class_handle_t iva_scene_class_handle, const neu_iva_scene_class_parameter_t *params);

    /**
     * [neu_iva_scene_class_param_get 获取相关参数或阈值]
     * @param  iva_scene_class_handle [module句柄指针，由上层创建]
     * @param  params                 [返回给上层的参数阈值]
     * @return                        [返回执行状态]
     */
    IVA_STATUS_E neu_iva_scene_class_param_get(neu_iva_scene_class_handle_t iva_scene_class_handle, neu_iva_scene_class_parameter_t *params);

    /**
     * [neu_iva_scene_class_reset     复位]
     * @param  iva_scene_class_handle [module句柄指针，由上层创建]
     * @return                        [返回执行状态]
     */
    IVA_STATUS_E neu_iva_scene_class_reset(neu_iva_scene_class_handle_t iva_scene_handle);

#ifdef __cplusplus
}
#endif

#endif
