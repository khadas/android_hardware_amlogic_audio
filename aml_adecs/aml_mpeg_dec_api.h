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

#ifndef _AML_MAD_DEC_API_H_
#define _AML_MAD_DEC_API_H_

extern aml_dec_func_t aml_mad_func;

/*temp code, we will remove it later*/
#if ANDROID_PLATFORM_SDK_VERSION < 31
/*S/T already has such enum, R doesn't have it*/
typedef enum {
    AUDIO_FORMAT_MPEGH = 0x2C000000u,
    AUDIO_FORMAT_MPEGH_SUB_BL_L3 = 0x13u,
    AUDIO_FORMAT_MPEGH_SUB_BL_L4 = 0x14u,
    AUDIO_FORMAT_MPEGH_SUB_LC_L3 = 0x23u,
    AUDIO_FORMAT_MPEGH_SUB_LC_L4 = 0x24u,
    AUDIO_FORMAT_MPEGH_BL_L3 = 0x2C000013u,
    AUDIO_FORMAT_MPEGH_BL_L4 = 0x2C000014u,
    AUDIO_FORMAT_MPEGH_LC_L3 = 0x2C000023u,
    AUDIO_FORMAT_MPEGH_LC_L4 = 0x2C000024u,
} audio_format_Ext_t;
#endif

#endif
