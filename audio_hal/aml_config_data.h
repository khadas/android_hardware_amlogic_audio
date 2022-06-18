/*
 * Copyright (C) 2019 Amlogic Corporation.
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
#ifndef _AML_CONFIG_DATA_H_
#define _AML_CONFIG_DATA_H_

#include <cJSON.h>
#include <stdbool.h>

#include "aml_config_parser.h"

int aml_audio_config_parser();
int aml_get_jason_int_value(char* key,int defvalue);
bool aml_get_codec_support(char* aformat);

#endif
