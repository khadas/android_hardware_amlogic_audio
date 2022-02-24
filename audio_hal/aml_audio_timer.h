/*
 * Copyright (C) 2021 Amlogic Corporation.
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
#ifndef __AUDIO_TIMER_H__
#define __AUDIO_TIMER_H__

#include <stdint.h>

#define MSEC_PER_SEC    1000L
#define USEC_PER_MSEC   1000L
#define NSEC_PER_USEC   1000L
#define NSEC_PER_MSEC   1000000L
#define USEC_PER_SEC    1000000L
#define NSEC_PER_SEC    1000000000LL
#define FSEC_PER_SEC    1000000000000000LL

/* typedefs */
typedef long long            signed64;
typedef unsigned long long  unsigned64;
typedef int                    signed32;
typedef unsigned int        unsigned32;
typedef short                signed16;
typedef unsigned short        unsigned16;
typedef signed char            signed8;
typedef unsigned char        unsigned8;

void audio_timer_init(void);
int audio_timer_create(unsigned32 aml_timer_id);
void audio_timer_stop(unsigned32 aml_timer_id);
void audio_periodic_timer_start(unsigned32 aml_timer_id, unsigned32 delay_time_ms);
void audio_one_shot_timer_start(unsigned32 aml_timer_id, unsigned32 delay_time_ms);
int audio_timer_delete(unsigned32 aml_timer_id);
unsigned32 audio_timer_remaining_time(unsigned32 aml_timer_id);

int aml_audio_sleep(uint64_t us);
uint64_t aml_audio_get_systime(void);
uint64_t aml_audio_get_systime_ns(void);
int64_t calc_time_interval_us(struct timespec *ts_start, struct timespec *ts_end);

#endif /* __AUDIO_TIMER_H__ */
