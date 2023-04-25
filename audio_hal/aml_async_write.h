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

#ifndef _AML_ASYNC_WRITE_H_
#define _AML_ASYNC_WRITE_H_


#define AML_ASYNC_WRITE_TRY_WITH_COMPRESS    0x100


int create_async_write_thread(void);

void aml_async_dump_data(const void *data_ptr, int data_size, const char *file_name);

void aml_async_dump_1ch_16bit_pcm(const void *data_ptr, int data_size, audio_format_t format,
                                 int channel_num, int channel_select, const char *file_name);

void aml_async_dump_iec_payload(const void *data_ptr, int data_size, const char *file_name);

void aml_async_remove_file(const char *file_name);

#endif
