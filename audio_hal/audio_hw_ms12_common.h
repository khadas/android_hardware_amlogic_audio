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

#ifndef _AUDIO_HW_MS12_COMMON_H_
#define _AUDIO_HW_MS12_COMMON_H_

#include <tinyalsa/asoundlib.h>
#include <system/audio.h>
#include <stdbool.h>
#include <aml_audio_ms12.h>

#include "audio_hw.h"

#define AML_TIMER_ID_1                        (0)
#define AML_TIMER_ID_2                        (1)
#define AML_TIMER_ID_3                        (2)
#define AML_TIMER_ID_4                        (3)

#define AML_TIMER_MK_EVT(ID)                   ((unsigned int)1<<(unsigned int)((ID)+1))
#define AML_TIMER_ID_01_EVT                    AML_TIMER_MK_EVT(AML_TIMER_ID_1)
#define AML_TIMER_ID_02_EVT                    AML_TIMER_MK_EVT(AML_TIMER_ID_2)
#define AML_TIMER_ID_03_EVT                    AML_TIMER_MK_EVT(AML_TIMER_ID_3)
#define AML_TIMER_ID_04_EVT                    AML_TIMER_MK_EVT(AML_TIMER_ID_4)

//delay time ms
#define AML_TIMER_DELAY     (3000)

#define DISABLE_CONTINUOUS_OUTPUT "persist.vendor.audio.continuous.disable"

/*
 *@brief define enum for MS12 message type
 */
typedef enum ms12_mesg_type {
    MS12_MESG_TYPE_NONE = 0,
    MS12_MESG_TYPE_FLUSH,
    MS12_MESG_TYPE_PAUSE,
    MS12_MESG_TYPE_RESUME,
    MS12_MESG_TYPE_SET_MAIN_DUMMY,
    MS12_MESG_TYPE_UPDATE_RUNTIIME_PARAMS,
    MS12_MESG_TYPE_EXIT_THREAD,
    MS12_MESG_TYPE_SCHEDULER_STATE,
    MS12_MESG_TYPE_MAX,
}ms12_mesg_type_t;

/*
 *@brief define ms12 message struct.
 */
struct ms12_mesg_desc {
    struct listnode list;
    ms12_mesg_type_t mesg_type;
    /* define a Zero Length Arry to extend for audio data in the future. */
    //char data[0];
};

/*
 *@brief set dolby ms12 pause/resume
 */
int set_dolby_ms12_runtime_pause(struct dolby_ms12_desc *ms12, int is_pause);
int dolby_ms12_main_pause(struct audio_stream_out *stream);
int dolby_ms12_main_resume(struct audio_stream_out *stream);

/*
 *@brief send scheduler state to ms12
 */
int aml_audiohal_sch_state_2_ms12(struct dolby_ms12_desc *ms12, int sch_state);
/*
 *@brief set dolby ms12 scheduler state
 */
int aml_set_ms12_scheduler_state(struct dolby_ms12_desc *ms12);
/*
 *@brief check whether the mesg list is empty
 */
bool ms12_msg_list_is_empty(struct dolby_ms12_desc *ms12);

/*
 *@brief check whether the mesg list is empty
 */

bool ms12_msg_list_is_empty(struct dolby_ms12_desc *ms12);

/*
 *@brief Receive message from audiohal to ms12.
 */
int audiohal_send_msg_2_ms12(struct dolby_ms12_desc *ms12, ms12_mesg_type_t mesg_type);
/*
 *@brief ms12 message thread create and destory function.
 */
int ms12_mesg_thread_create(struct dolby_ms12_desc *ms12);
int ms12_mesg_thread_destroy(struct dolby_ms12_desc *ms12);

int aml_audio_timer_create(void);
void aml_audio_timer_init(void);
int aml_send_ms12_scheduler_state_2_ms12(void);
int aml_audio_timer_delete(void);
bool is_ad_data_available(int digital_audio_format);

void set_continuous_audio_mode(struct aml_audio_device *adev, int enable, int is_suspend);

/* @brief set ms12 full dap disable as full_dap_disable [0/1] */
void set_ms12_full_dap_disable(struct dolby_ms12_desc *ms12, int full_dap_disable);

/* @brief set ms12 multichanel enable [0/1] */
void set_ms12_mc_enable(struct dolby_ms12_desc *ms12, int mc_enable);

#endif //end of _AUDIO_HW_MS12_COMMON_H_
