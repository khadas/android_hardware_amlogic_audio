/*
 * Copyright (C) 2022 Amlogic Corporation.
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

#ifndef _AML_AUDIO_HEAACPARSER_H_
#define _AML_AUDIO_HEAACPARSER_H_

struct heaac_parser_info {
    int frame_size;     /*bitsteam size for both adts&loas*/
    int sample_rate;    /*sample rate for adts not for loas*/
    int channel_mask;   /*channel_mask for adts not for loas*/
    int is_loas;
    int is_adts;
    int debug_print;
    int streamMuxRead;
    int audioMuxVersionA;
    int numSubframes;
    int otherDataPresent;
    int otherDataLenBits;
    int frameLengthType;
    int audioObjectType;
    int sampleRateHz;
    int channelCount;
};


int aml_heaac_parser_open(void **pparser_handle);
int aml_heaac_parser_close(void *parser_handle);
int aml_heaac_parser_process(void *parser_handle, const void *buffer, int32_t numBytes, int32_t *used_size, void **output_buf, int32_t *out_size, struct heaac_parser_info * heaac_info);
int aml_heaac_parser_reset(void *parser_handle);


#endif
