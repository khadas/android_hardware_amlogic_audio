/*****************************************************************************/
/* Copyright (C) 2021 Amlogic Corporation.
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
/** file:  audio_timer.c
 *  This file implement timer.
 *  Section: Audio Primary Hal.
 */
/*****************************************************************************/

#define LOG_TAG "audio_hw_timer"

#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <cutils/log.h>
#include <errno.h>
#include <stdbool.h>

#ifndef __USE_GNU
/* Define this to avoid a warning about implicit definition of ppoll.*/
#define __USE_GNU
#endif
#include <poll.h>

#include "aml_audio_timer.h"


/*****************************************************************************
*   Function Name:  audio_get_sys_tick_frequency
*   Description:    get system tick.
*   Parameters:     void
*   Return value:   unsigned64: tick resolution
******************************************************************************/
unsigned64 audio_get_sys_tick_frequency(void)
{
    struct timespec resolution;

    clock_getres(CLOCK_REALTIME, &resolution);
    ALOGV("func:%s  resolution.tv_sec:%ld  resolution.tv_nsec:%ld.", __func__, resolution.tv_sec, resolution.tv_nsec);

    return 1000000000L / resolution.tv_nsec;
}

/*****************************************************************************
*   Function Name:  audio_timer_start
*   Description:    start a timer.
*   Parameters:     unsigned32:timer_id, unsigned64:delay_time, bool:type
*   Return value:   void
******************************************************************************/
void audio_timer_start(unsigned32 timer_id, unsigned64 delay_time, bool type)
{
    struct itimerspec       i_timer_spec;

    if (aml_timer[timer_id].state != TIMER_STATE_ACTIVE) {
        ALOGE("func:%s timer not active, need to check.", __func__);
        return ;
    }

    i_timer_spec.it_value.tv_sec = delay_time/1000000000;
    i_timer_spec.it_value.tv_nsec = delay_time%1000000000;
    if (type) {//periodic timer

        i_timer_spec.it_interval.tv_sec = i_timer_spec.it_value.tv_sec;
        i_timer_spec.it_interval.tv_nsec = i_timer_spec.it_value.tv_nsec;
    } else {// one shot timer
        i_timer_spec.it_interval.tv_sec = 0;
        i_timer_spec.it_interval.tv_nsec = 0;
    }

    if (timer_settime(aml_timer[timer_id].timer, 0, &(i_timer_spec), NULL) == -1) {

        ALOGE("func:%s  set timer fail. errno:%d(%s)", __func__, errno, strerror(errno));
    } else {
        ALOGV("func:%s  set timer success.", __func__);
    }
}

/*****************************************************************************
*   Function Name:  audio_periodic_timer_start
*   Description:    start a periodic timer.
*   Parameters:     unsigned32:timer_id, unsigned64:delay_time
*   Return value:   void
******************************************************************************/
void audio_periodic_timer_start(unsigned32 timer_id, unsigned32 delay_time_ms)
{
    audio_timer_start(timer_id,(unsigned64)(delay_time_ms*(unsigned64)1000000), true);
}

/*****************************************************************************
*   Function Name:  audio_one_shot_timer_start
*   Description:    start a one_shot timer.
*   Parameters:     unsigned32:timer_id, unsigned64:delay_time
*   Return value:   void
******************************************************************************/
void audio_one_shot_timer_start(unsigned32 timer_id, unsigned32 delay_time_ms)
{
    audio_timer_start(timer_id,(unsigned64)(delay_time_ms*(unsigned64)1000000), false);
}

/*****************************************************************************
*   Function Name:  audio_timer_stop
*   Description:    stop a timer.
*   Parameters:     unsigned32:timer_id
*   Return value:   void
******************************************************************************/
void audio_timer_stop(unsigned32 timer_id)
{
    struct itimerspec       i_timer_spec;

    i_timer_spec.it_value.tv_sec = 0;
    i_timer_spec.it_value.tv_nsec = 0;
    i_timer_spec.it_interval.tv_sec = 0;
    i_timer_spec.it_interval.tv_nsec = 0;

    if (timer_settime(aml_timer[timer_id].timer, 0, &(i_timer_spec), NULL) == -1) {
        ALOGE("func:%s  stop timer fail. errno:%d(%s)", __func__, errno, strerror(errno));
    } else {
        ALOGV("func:%s  stop timer success. ", __func__);
    }
}

/*****************************************************************************
*   Function Name:  audio_timer_remaining_time
*   Description:    get remaining timer of a timer.
*   Parameters:     unsigned32:timer_id
*   Return value:   unsigned32: remaining time
******************************************************************************/
unsigned32 audio_timer_remaining_time(unsigned32 timer_id)
{
    struct itimerspec       i_timer_spec;
    unsigned32  remaining_time = 0;

    if (timer_gettime(aml_timer[timer_id].timer, &(i_timer_spec)) == -1) {
        ALOGE("func:%s  gettime fail. errno:%d(%s)", __func__, errno, strerror(errno));
    } else {
        ALOGV("func:%s  timer id:%u,  time tv_sec:%ld, tv_nsec:%ld ", __func__,
                timer_id, i_timer_spec.it_value.tv_sec, i_timer_spec.it_value.tv_nsec);
        remaining_time = (unsigned32)(i_timer_spec.it_value.tv_sec * 1000 + i_timer_spec.it_value.tv_nsec/1000000LL);
    }

    return remaining_time;
}


//dynamic timer.
signed32 audio_timer_delete(unsigned32 timer_id)
{
    int ret = 0;
    ret = timer_delete(aml_timer[timer_id].timer);
    if (ret < 0) {
        ALOGE("func:%s  delete timer.%d fail. errno:%d(%s)", __func__, timer_id, errno, strerror(errno));
    } else {
        ALOGD("func:%s  delete timer.%d success.", __func__, timer_id);
    }

    return ret;
}

/*****************************************************************************
*   Function Name:  aml_audio_timer_delete
*   Description:    delete a timer.
*   Parameters:     unsigned32:timer_id
*   Return value:   0: Success, -1:Error
******************************************************************************/
int aml_audio_timer_delete(unsigned int timer_id)
{
    int ret = 0;
    int err = 0;

    if (timer_id >= AML_TIMER_ID_NUM) {
        ALOGE("func:%s invalid timer_id:%u",__func__, timer_id);
        return -1;
    }

    if (aml_timer[timer_id].state == TIMER_STATE_ACTIVE) {
        ret = audio_timer_delete(timer_id);
    }
    if (ret < 0) {
        ALOGE("func:%s timer_id:%d fail",__func__, timer_id);
        return -1;
    }

    aml_timer[timer_id].timer = 0;
    aml_timer[timer_id].id = AML_TIMER_ID_INVALID;
    aml_timer[timer_id].state = TIMER_STATE_INACTIVE;

    return timer_id;
}

/*****************************************************************************
*   Function Name:  aml_audio_all_timer_delete
*   Description:    delete all timer.
*   Parameters:     void
*   Return value:   0: Success, -1:Error
******************************************************************************/
int aml_audio_all_timer_delete(void)
{
    unsigned int timer_id = 0;
    int ret = 0;
    int err = 0;

    for (timer_id = 0; timer_id < AML_TIMER_ID_NUM; timer_id++)
    {
        ret = aml_audio_timer_delete(timer_id);
        if (ret < 0)
        {
            ALOGE("func:%s timer_id:%d fail",__func__, timer_id);
            err = -1;
            break;
        }
    }

    return err;
}

static int audio_timer_create(unsigned32 timer_id, func_timer_callback_handler cb_handler)
{
    struct sigevent sig_event;
    int ret = 0;

    memset(&sig_event, 0, sizeof(sig_event));
    sig_event.sigev_notify = SIGEV_THREAD;
    sig_event.sigev_notify_function = cb_handler;
    sig_event.sigev_value.sival_int = timer_id; /*callback function can get the value.*/
    //sig_event.sigev_value.sival_ptr = pointer;

    ret = timer_create(CLOCK_MONOTONIC, &sig_event, &aml_timer[timer_id].timer);
    if (ret < 0) {
        ALOGE("func:%s  create timer.%d fail. errno:%d(%s)", __func__, timer_id, errno, strerror(errno));
    } else {
        ALOGD("func:%s  create timer.%d success.", __func__, timer_id);
    }

    return ret;
}

/*****************************************************************************
*   Function Name:  aml_audio_timer_create
*   Description:    create timer for audio hal.
*   Parameters:     func_timer_callback_handler: callback function handler.
*   Return value:   0: timer_id, -1: error
******************************************************************************/
int aml_audio_timer_create(func_timer_callback_handler cb_handler)
{
    unsigned int timer_id = 0;
    int ret = 0;
    int err = 0;

    for (timer_id = 0; timer_id < AML_TIMER_ID_NUM; timer_id++) {
        if (aml_timer[timer_id].state == TIMER_STATE_INACTIVE) {
            //lookup and get a timer that is not used.
            break;
        }
    }
    if (timer_id >= AML_TIMER_ID_NUM) {
        ALOGE("func:%s no valid timer for use, create fail",__func__);
        return -1;
    }

    aml_timer[timer_id].timer = 0;
    ret = audio_timer_create(timer_id, cb_handler);
    if (ret < 0) {
        ALOGE("func:%s timer_id:%d fail",__func__, timer_id);
        return -1;
    }

    aml_timer[timer_id].id = timer_id;
    aml_timer[timer_id].state = TIMER_STATE_ACTIVE;

    return timer_id;
}



/* these code below are moved from the old aml_audio_timer.c. */
uint64_t aml_audio_get_systime(void)
{
    struct timespec ts;
    uint64_t sys_time;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    sys_time = (uint64_t)ts.tv_sec * 1000000LL + (uint64_t)ts.tv_nsec / 1000LL;

    return sys_time;
}

uint64_t aml_audio_get_systime_ns(void)
{
    struct timespec ts;
    uint64_t sys_time;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    sys_time = (uint64_t)ts.tv_sec * 1000*1000*1000LL + (uint64_t)ts.tv_nsec;

    return sys_time;
}

struct timespec aml_audio_ns_to_time(uint64_t ns)
{
    struct timespec time;
    time.tv_sec = ns / 1000000000;
    time.tv_nsec = ns - (time.tv_sec * 1000000000);
    return time;
}

int aml_audio_sleep(uint64_t us)
{
    int ret = -1;
    struct timespec ts;
    if (us == 0) {
        return 0;
    }

    ts.tv_sec = (long)us / 1000000ULL;
    ts.tv_nsec = (long)(us - ts.tv_sec) * 1000;

    ret = ppoll(NULL, 0, &ts, NULL);
    return ret;
}


int64_t calc_time_interval_us(struct timespec *ts_start, struct timespec *ts_end)
{
    int64_t start_us, end_us;
    int64_t interval_us;


    start_us = ts_start->tv_sec * 1000000LL +
               ts_start->tv_nsec / 1000LL;

    end_us   = ts_end->tv_sec * 1000000LL +
               ts_end->tv_nsec / 1000LL;

    interval_us = end_us - start_us;

    return interval_us;
}
