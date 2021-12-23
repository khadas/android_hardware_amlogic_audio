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

#define LOG_TAG "aml_effects_utils"
#include "aml_effects_util.h"
#include "common/all-versions/default/EffectMap.h"

#define MAX_POSTPROCESSORS 10

bool getEffectStatus(effect_handle_t effect) {
    for (int i = 0; i < MAX_POSTPROCESSORS; i++) {
        effect_handle_t halEffect = android::EffectMap::getInstance().get(i);
        ALOGV("i %d halEffect %p effect %p",i,halEffect,effect);
        if (halEffect == effect)
            return true;
    }
    return false;
}
