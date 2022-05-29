/*
 * Copyright (C) 2010 Amlogic Corporation.
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

#ifndef _AUDIO_HWSYNC_WRAP_H_
#define _AUDIO_HWSYNC_WRAP_H_

#include <stdbool.h>


int aml_hwsync_get_tsync_pcr(audio_hwsync_t *p_hwsync, uint64_t *value);
int aml_hwsync_wrap_get_tsync_pts_by_handle(int fd, uint64_t *pts);
int aml_hwsync_wrap_set_tsync_open(void);
void aml_hwsync_wrap_set_tsync_close(int fd);

void aml_hwsync_wrap_set_tsync_init(audio_hwsync_t *p_hwsync);
int aml_hwsync_wrap_get_vpts(audio_hwsync_t *p_hwsync, uint32_t *pts);
int aml_hwsync_wrap_get_firstvpts(audio_hwsync_t *p_hwsync, uint32_t *pts);
void aml_hwsync_wrap_set_pause(audio_hwsync_t *p_hwsync);
void aml_hwsync_wrap_set_resume(audio_hwsync_t *p_hwsync);
void aml_hwsync_wrap_set_stop(audio_hwsync_t *p_hwsync);
int aml_hwsync_wrap_set_start_pts(audio_hwsync_t *p_hwsync, uint32_t pts);
int aml_hwsync_wrap_set_start_pts64(audio_hwsync_t *p_hwsync,uint64_t pts);
int aml_hwsync_wrap_get_pts(audio_hwsync_t *p_hwsync, uint64_t *pts);
int aml_hwsync_wrap_reset_pcrscr(audio_hwsync_t *p_hwsync, uint64_t pts);
bool aml_hwsync_wrap_get_id(void *mediasync, int32_t* id);
bool aml_hwsync_wrap_set_id(audio_hwsync_t *p_hwsync, uint32_t id);
bool aml_hwsync_wrap_release(audio_hwsync_t *p_hwsync);

void aml_hwsync_wrap_wait_video_start(audio_hwsync_t *p_hwsync, uint32_t wait_count);
void aml_hwsync_wrap_wait_video_drop(audio_hwsync_t *p_hwsync, uint64_t cur_pts, uint32_t wait_count);
void aml_hwsync_wait_video_start(audio_hwsync_t *p_hwsync);
void aml_hwsync_wait_video_drop(audio_hwsync_t *p_hwsync, uint64_t cur_pts);

bool aml_audio_hwsync_update_threshold(audio_hwsync_t *p_hwsync);

void* aml_hwsync_wrap_mediasync_create(void);
bool check_support_mediasync(void);
void* aml_hwsync_mediasync_create(void);

int aml_audio_start_trigger(void *stream);


#endif
