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

#define LOG_TAG "audio_hw_hal_ms12common"
//#define LOG_NDEBUG 0
#define __USE_GNU

#include <cutils/log.h>
#include <dolby_ms12.h>
#include <dolby_ms12_config_params.h>
#include <dolby_ms12_status.h>
#include <aml_android_utils.h>
#include <sys/prctl.h>
#include <cutils/properties.h>
#include <inttypes.h>

#include "audio_hw_ms12.h"
#include "audio_hw_ms12_common.h"
#include "alsa_config_parameters.h"
#include "aml_ac3_parser.h"
#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>
#include "audio_hw.h"
#include "alsa_manager.h"
#include "aml_audio_stream.h"
#include "dolby_lib_api.h"
#include "aml_audio_timer.h"
#include "audio_virtual_buf.h"
#include "ac3_parser_utils.h"
#include "audio_hw_utils.h"
#include "alsa_device_parser.h"
#include "spdif_encoder_api.h"
#include "aml_audio_spdifout.h"
#include "aml_audio_ac3parser.h"
#include "aml_audio_spdifdec.h"
#include "aml_audio_ms12_bypass.h"
#include "aml_malloc_debug.h"
#include "audio_hwsync_wrap.h"
#include "dtv_private_object.h"
#include "audio_hw_resource_mgr.h"

/*
 *@brief
 *Convert from ms12 pointer to aml_audio_device pointer.
 */
#define ms12_to_adev(ms12_ptr)  (struct aml_audio_device *) (((char*) (ms12_ptr)) - offsetof(struct aml_audio_device, ms12))


/*
 *@brief
 *transfer mesg type to string,
 *for more easy to read log.
 */
const char *mesg_type_2_string[MS12_MESG_TYPE_MAX] = {
    "MS12_MESG_TYPE_NONE",
    "MS12_MESG_TYPE_FLUSH",
    "MS12_MESG_TYPE_PAUSE",
    "MS12_MESG_TYPE_RESUME",
    "MS12_MESG_TYPE_SET_MAIN_DUMMY",
    "MS12_MESG_TYPE_UPDATE_RUNTIME_PARAMS",
    "MS12_MESG_TYPE_RESET_MS12_ENCODER",
    "MS12_MESG_TYPE_EXIT_THREAD",
    "MS12_MESG_TYPE_SCHEDULER_STATE",
};

/*
 *@brief
 *convert scheduler state to string,
 *for more easy to check out log.
 */
const char *scheduler_state_2_string[MS12_SCHEDULER_MAX] = {
    "SCHEDULER_RUNNING",
    "SCHEDULER_STANDBY",
};

/*****************************************************************************
*   Function Name:  set_dolby_ms12_runtime_pause
*   Description:    set pause or resume to dolby ms12.
*   Parameters:     struct dolby_ms12_desc: ms12 variable pointer
*                   int: state pause or resume
*   Return value:   0: success, or else fail
******************************************************************************/
int set_dolby_ms12_runtime_pause(struct dolby_ms12_desc *ms12, int is_pause)
{
    char parm[12] = "";
    int ret = -1;

    sprintf(parm, "%s %d", "-pause", is_pause);
    if ((strlen(parm) > 0) && ms12) {
        ret = aml_ms12_update_runtime_params(ms12, parm);
    } else {
        ALOGE("%s ms12 is NULL or strlen(parm) is zero", __func__);
        ret = -1;
    }
    return ret;
}

int dolby_ms12_main_pause(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ms12_runtime_update_ret = 0;

    pthread_mutex_lock(&ms12->main_lock);
    dolby_ms12_set_pause_flag(true);
    //ms12_runtime_update_ret = aml_ms12_update_runtime_params(ms12);
    ms12_runtime_update_ret = set_dolby_ms12_runtime_pause(ms12, true);
    ms12->is_continuous_paused = true;

    ALOGV("%s  ms12_runtime_update_ret:%d", __func__, ms12_runtime_update_ret);

    //1.audio easing duration is 32ms,
    //2.one loop for schedule_run cost about 32ms(contains the hardware costing),
    //3.if [pause, flush] too short, means it need more time to do audio easing
    //so, the delay time for 32ms(pause is completed after audio easing is done) is enough.
    aml_audio_sleep(32000);
    ALOGI("%s  sleep 32ms finished", __func__);

    if (aml_out->hw_sync_mode && aml_out->tsync_status != TSYNC_STATUS_PAUSED && aml_out->hwsync) {
        ALOGI("%s end of frame =%d", __func__, aml_out->hwsync->end_of_hwsync_frame);
        /*if we are end of frame now, we don't need to pause pcr*/
        if (!aml_out->hwsync->end_of_hwsync_frame) {
            aml_hwsync_wrap_set_pause(aml_out->hwsync);
            aml_out->tsync_status = TSYNC_STATUS_PAUSED;
        }

        aml_out->hwsync->first_apts_flag = false;
        ALOGD("%s tsync pause finished", __func__);
    }
    pthread_mutex_unlock(&ms12->main_lock);

    return 0;
}

int dolby_ms12_main_resume(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ms12_runtime_update_ret = 0;

    /*fly audio of NTS appear freeze ~1.5s fail, as send the resume
    **message to ms12 in flush/close_stream interface when exit stream.
    **here do tsync resume, this lead to video pcr not pause.
    **so add ms12_resume_state to distinguish resume/flush/close resume message.
    **In addition, just only do tsync resume from resume interface message.
    */
    /*coverity[missing_lock]*/
    if (aml_out->hw_sync_mode
        && (ms12->ms12_resume_state == MS12_RESUME_FROM_RESUME)) {
        aml_hwsync_wrap_set_resume(aml_out->hwsync);
        aml_out->tsync_status = TSYNC_STATUS_RUNNING;
        ALOGV("%s(), tsync resume finished", __func__);
    }

    dolby_ms12_set_pause_flag(false);
    //ms12_runtime_update_ret = aml_ms12_update_runtime_params(ms12);
    ms12_runtime_update_ret = set_dolby_ms12_runtime_pause(ms12, false);
    ms12->is_continuous_paused = false;
    ALOGI("%s  ms12_runtime_update_ret:%d", __func__, ms12_runtime_update_ret);

    return 0;
}

/*****************************************************************************
*   Function Name:  ms12_msg_list_is_empty
*   Description:    check whether the msg list is empty
*   Parameters:     struct dolby_ms12_desc: ms12 variable pointer
*   Return value:   0: not empty, 1 empty
******************************************************************************/
bool ms12_msg_list_is_empty(struct dolby_ms12_desc *ms12)
{
    bool is_empty = true;
    if (0 != ms12->ms12_mesg_threadID) {
        pthread_mutex_lock(&ms12->mutex);
        if (!list_empty(&ms12->mesg_list)) {
            is_empty = false;
        }
        pthread_mutex_unlock(&ms12->mutex);
    }
    return is_empty;
}

/*****************************************************************************
*   Function Name:  audiohal_send_msg_2_ms12
*   Description:    receive message from audio hardware.
*   Parameters:     struct dolby_ms12_desc: ms12 variable pointer
*                   ms12_mesg_type_t: message type
*   Return value:   0: success, or else fail
******************************************************************************/
int audiohal_send_msg_2_ms12(struct dolby_ms12_desc *ms12, ms12_mesg_type_t mesg_type)
{
    int ret = -1;
    if (0 != ms12->ms12_mesg_threadID) {
        struct ms12_mesg_desc *mesg_p = aml_audio_calloc(1, sizeof(struct ms12_mesg_desc));

        if (NULL == mesg_p) {
            ALOGE("%s calloc fail, errno:%s", __func__, strerror(errno));
            return -ENOMEM;
        }

        ALOGI("%s mesg_type:%s entry", __func__, mesg_type_2_string[mesg_type]);
        pthread_mutex_lock(&ms12->mutex);
        mesg_p->mesg_type = mesg_type;
        ALOGV("%s add mesg item", __func__);
        list_add_tail(&ms12->mesg_list, &mesg_p->list);
        pthread_mutex_unlock(&ms12->mutex);
        pthread_cond_signal(&ms12->cond);
        ALOGI("%s mesg_type:%s exit", __func__, mesg_type_2_string[mesg_type]);
        ret = 0;
    } else {
        ALOGE("%s ms12_mesg_threadID is 0, exit directly as ms12_message_thread had some issues!", __func__);
        ret = -1;
    }
    return ret;
}

/*****************************************************************************
*   Function Name:  ms12_message_threadloop
*   Description:    ms12 receive message thread,
*                   it always loop until receive exit message.
*   Parameters:     void *data
*   Return value:   NULL pointer
******************************************************************************/
static void *ms12_message_threadloop(void *data)
{
    struct dolby_ms12_desc *ms12 = (struct dolby_ms12_desc *)data;
    struct aml_audio_device *adev = NULL;

    ALOGI("%s entry.", __func__);
    if (ms12 == NULL) {
        ALOGE("%s ms12 pointer invalid!", __func__);
        goto Error;
    }

    adev = ms12_to_adev(ms12);
    prctl(PR_SET_NAME, (unsigned long)"MS12_CommThread");
    aml_set_thread_priority("ms12_message_thread", pthread_self());

    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    CPU_SET(2, &cpuSet);
    CPU_SET(3, &cpuSet);
    int set_affinity = sched_setaffinity(0, sizeof(cpu_set_t), &cpuSet);
    if (set_affinity) {
        ALOGW("%s(), failed to set cpu affinity", __func__);
    }

    do {
        struct ms12_mesg_desc *mesg_p = NULL;
        struct listnode *item = NULL;

        pthread_mutex_lock(&ms12->mutex);
        if (list_empty(&ms12->mesg_list)) {
            ALOGD("%s  mesg_list is empty, loop waiting......", __func__);
            pthread_cond_wait(&ms12->cond, &ms12->mutex);
        }

Repop_Mesg:
        if (list_empty(&ms12->mesg_list)) {
            ALOGD("%s list is empty", __func__);
            pthread_mutex_unlock(&ms12->mutex);
            continue;
        }
        item = list_head(&ms12->mesg_list);
        mesg_p = (struct ms12_mesg_desc *)item;
        if (mesg_p->mesg_type > MS12_MESG_TYPE_MAX) {
            ALOGE("%s wrong message type =%d", __func__, mesg_p->mesg_type);
            mesg_p->mesg_type = MS12_MESG_TYPE_NONE;
        }
        if (mesg_p->mesg_type < MS12_MESG_TYPE_MAX) {
            ALOGD("%s(), msg type: %s", __func__, mesg_type_2_string[mesg_p->mesg_type]);
        }
        pthread_mutex_unlock(&ms12->mutex);

        if (ms12->CommThread_ExitFlag) {
            goto Error;
        }
        ALOGV("%s  ms12_out:%p, ==> ms12_main_stream_out:%p", __func__,adev->ms12_out,ms12->ms12_main_stream_out);
        /*coverity[missing_lock]*/
        switch (mesg_p->mesg_type) {
            case MS12_MESG_TYPE_FLUSH:
                if (ms12->ms12_main_stream_out != NULL)
                    dolby_ms12_main_flush(&ms12->ms12_main_stream_out->stream);//&adev->ms12_out->stream
                break;
            case MS12_MESG_TYPE_PAUSE:
                if (ms12->ms12_main_stream_out != NULL)
                    dolby_ms12_main_pause(&ms12->ms12_main_stream_out->stream);
                break;
            case MS12_MESG_TYPE_RESUME:
                if (ms12->ms12_main_stream_out != NULL)
                    dolby_ms12_main_resume(&ms12->ms12_main_stream_out->stream);
                break;
            case MS12_MESG_TYPE_SET_MAIN_DUMMY:
                break;
            case MS12_MESG_TYPE_UPDATE_RUNTIME_PARAMS:
                break;
            case MS12_MESG_TYPE_RESET_MS12_ENCODER:
                dolby_ms12_encoder_reconfig(ms12);
                break;
            case MS12_MESG_TYPE_EXIT_THREAD:
                ALOGD("%s mesg exit thread.", __func__);
                break;
            case MS12_MESG_TYPE_SCHEDULER_STATE:
                //aml_set_ms12_scheduler_state(&ms12->ms12_main_stream_out->stream);
                //ALOGD("%s scheduler state.", __func__);
                break;
            default:
                ALOGD("%s  msg type not support.", __func__);
        }

        pthread_mutex_lock(&ms12->mutex);
        list_remove(&mesg_p->list);
        aml_audio_free(mesg_p);
        if (!list_empty(&ms12->mesg_list)) {
            ALOGD("%s  list no empty and Repop_Mesg again.", __func__);
            goto Repop_Mesg;
        }
        pthread_mutex_unlock(&ms12->mutex);

    } while(true != ms12->CommThread_ExitFlag);

Error:
    ALOGI("%s  exit.", __func__);
    return ((void *)0);
}

/*****************************************************************************
*   Function Name:  ms12_mesg_thread_create
*   Description:    ms12 message thread create, so need to do create work.
*   Parameters:     struct dolby_ms12_desc: ms12 variable pointer
*   Return value:   0: success, or fail
******************************************************************************/
int ms12_mesg_thread_create(struct dolby_ms12_desc *ms12)
{
    int ret = 0;

    list_init(&ms12->mesg_list);
    ms12->CommThread_ExitFlag = false;
    if ((ret = pthread_mutex_init (&ms12->mutex, NULL)) != 0) {
        ALOGE("%s  pthread_mutex_init fail, errno:%s", __func__, strerror(errno));
    } else if((ret = pthread_cond_init(&ms12->cond, NULL)) != 0) {
        ALOGE("%s  pthread_cond_init fail, errno:%s", __func__, strerror(errno));
    } else if((ret = pthread_create(&(ms12->ms12_mesg_threadID), NULL, &ms12_message_threadloop, (void *)ms12)) != 0) {
        ALOGE("%s  pthread_create fail, errno:%s", __func__, strerror(errno));
    } else {
        ALOGD("%s ms12 thread init & create successful, ms12_mesg_threadID:%#lx ret:%d", __func__, ms12->ms12_mesg_threadID, ret);
    }

    return ret;
}

/*****************************************************************************
*   Function Name:  ms12_mesg_thread_destroy
*   Description:    ms12 message thread exit, so need to do release work.
*   Parameters:     struct dolby_ms12_desc: ms12 variable pointer
*   Return value:   0: success
******************************************************************************/
int ms12_mesg_thread_destroy(struct dolby_ms12_desc *ms12)
{
    int ret = 0;

    ALOGD("%s entry, ms12_mesg_threadID:%#lx", __func__, ms12->ms12_mesg_threadID);
    if (ms12->ms12_mesg_threadID != 0) {
        if (!list_empty(&ms12->mesg_list)) {
            struct ms12_mesg_desc *mesg_p = NULL;
            struct listnode *item = NULL;
            struct listnode *temp = NULL;

            list_for_each_safe(item, temp, &ms12->mesg_list){
                item = list_head(&ms12->mesg_list);
                mesg_p = (struct ms12_mesg_desc *)item;
                list_remove(&mesg_p->list);
                aml_audio_free(mesg_p);
            }
        }

        ms12->CommThread_ExitFlag = true;
        ret = audiohal_send_msg_2_ms12(ms12, MS12_MESG_TYPE_EXIT_THREAD);

        pthread_join(ms12->ms12_mesg_threadID, NULL);
        ms12->ms12_mesg_threadID = 0;

        /* destroy cond & mutex*/
        pthread_cond_destroy(&ms12->cond);
        pthread_mutex_destroy(&ms12->mutex);
        ALOGD("%s() ms12_mesg_threadID reset to %ld\n", __FUNCTION__, ms12->ms12_mesg_threadID);
    }

    return ret;
}

/*****************************************************************************
*   Function Name:  aml_send_ms12_scheduler_state_2_ms12
*   Description:    send scheduler state to ms12 after timer expiration.
*   Parameters:     void
*   Return value:   0: success, -1: error
******************************************************************************/
int aml_send_ms12_scheduler_state_2_ms12(void)
{
    struct aml_audio_device *adev = aml_adev_get_handle();
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int sch_state = MS12_SCHEDULER_NONE;
    pthread_mutex_lock(&ms12->lock);
    sch_state = ms12->ms12_scheduler_state;
    if (sch_state <= MS12_SCHEDULER_NONE ||  sch_state >= MS12_SCHEDULER_MAX) {
           ALOGE("%s  sch_state:%d is an invalid scheduler state.", __func__, sch_state);
           pthread_mutex_unlock(&ms12->lock);
           return -1;
    } else {
        dolby_ms12_set_scheduler_state(ms12->ms12_scheduler_state);
        ALOGD("%s adev:%p, sch_state:%d(%s) ", __func__, adev, sch_state, scheduler_state_2_string[sch_state]);
    }
    pthread_mutex_unlock(&ms12->lock);

    return 0;
}

void ms12_timer_callback_handler(union sigval sigv)
{
    ALOGD("func:%s sigv:%d ~~~~~~~~~~", __func__, sigv.sival_int);
    aml_send_ms12_scheduler_state_2_ms12();
    return ;
}

/*****************************************************************************
*   Function Name:  aml_set_ms12_scheduler_state
*   Description:    send scheduler state to ms12 or start a timer to delay.
*   Parameters:     struct dolby_ms12_desc:ms12 pointer
*   Return value:   0: success, -1: error
******************************************************************************/
int aml_set_ms12_scheduler_state(struct dolby_ms12_desc *ms12)
{
    struct aml_audio_device *adev = aml_adev_get_handle();
    int sch_state = ms12->ms12_scheduler_state;
    bool is_arc_connecting = is_HDMI_connected(adev);/*(adev->active_outport == OUTPORT_HDMI_ARC);*/
    bool is_netflix = adev->is_netflix;
    unsigned int remaining_time = 0;

    if (sch_state <= MS12_SCHEDULER_NONE ||  sch_state >= MS12_SCHEDULER_MAX) {
          ALOGE("%s  sch_state:%d is an invalid scheduler state.", __func__, sch_state);
          return -1;
    } else if (ms12->last_scheduler_state == sch_state) {
       ALOGW("%s  sch_state:%d %s, ms12 scheduler state not changed.", __func__, sch_state, scheduler_state_2_string[sch_state]);
       return 0;
    }
    if (!is_arc_connecting && !is_netflix) {
        remaining_time = audio_timer_remaining_time(ms12->ms12_timer_id);
        if (remaining_time > 0) {
            audio_timer_stop(ms12->ms12_timer_id);
        }

        if (sch_state == MS12_SCHEDULER_STANDBY) {
            //audio_one_shot_timer_start(AML_TIMER_ID_1, AML_TIMER_DELAY);
            audio_one_shot_timer_start(ms12->ms12_timer_id, AML_TIMER_DELAY);
        } else {
            dolby_ms12_set_scheduler_state(sch_state);
        }

        ALOGI("%s  ms12_scheduler_state:%d, sch_state:%d %s is sent to ms12", __func__,
            ms12->ms12_scheduler_state, sch_state, scheduler_state_2_string[sch_state]);
    } else {
        remaining_time = audio_timer_remaining_time(ms12->ms12_timer_id);
        if (remaining_time > 0) {
            audio_timer_stop(ms12->ms12_timer_id);
        }

        sch_state = MS12_SCHEDULER_RUNNING;
        dolby_ms12_set_scheduler_state(sch_state);
        ALOGI("%s  is_arc_connecting:%d, is_netflix:%d, sch_state:%d %s is sent to ms12", __func__,
            is_arc_connecting, is_netflix, sch_state, scheduler_state_2_string[sch_state]);
    }
    ms12->last_scheduler_state = sch_state;

    return 0;
}

/*****************************************************************************
*   Function Name:  aml_audiohal_sch_state_2_ms12
*   Description:    audio hal send message to ms12.
*   Parameters:     struct dolby_ms12_desc:ms12 pointer
*   Return value:   0: success, -1: error
******************************************************************************/
int aml_audiohal_sch_state_2_ms12(struct dolby_ms12_desc *ms12, int sch_state)
{
    if (ms12->dolby_ms12_enable) {
        pthread_mutex_lock(&ms12->lock);
        ms12->ms12_scheduler_state = sch_state;
        aml_set_ms12_scheduler_state(ms12);
        pthread_mutex_unlock(&ms12->lock);
    }

    return 0;
}

/*
 * if dual_decoder_support is true;
 * when passthrough mode(BYPASS), audio_hw_dtv do not send the ad data;
 * when AUTO/PCM mode(PCM/AUTO), audio_hw_dtv send the main&ad data;
 */
bool is_ad_data_available(int digital_audio_mode)
{
    if (digital_audio_mode != AML_DIGITAL_AUDIO_MODE_BYPASS) {
        return true;
    }
    else {
        return false;
    }
}


/*****************************************************************************
*   Function Name:  set_continuous_audio_mode
*   Description:    set continuous_audio_mode for audio hal
*   Parameters:     struct aml_audio_device : audio hw pointer
*                   int : enable/disable continuous_audio_mode
*                   int : is suspend(resume) case
*   Return value:   void
******************************************************************************/
void set_continuous_audio_mode(struct aml_audio_device *adev, int enable, int is_suspend)
{
    int ret = 0;
    int disable_continuous = !enable;

    // if exit netflix, we need disable atmos lock
    if (disable_continuous) {
        adev->atoms_lock_flag = false;
        set_ms12_atmos_lock(&(adev->ms12), adev->atoms_lock_flag);
        ALOGI("exit netflix, set atmos lock as 0");
    }
    {
        bool acmod2ch_lock = !enable;
        //when in netflix, we should always keep ddp5.1, exit netflix we can output ddp2ch
        set_ms12_acmod2ch_lock(&(adev->ms12), acmod2ch_lock);
    }

    ALOGI("%s ignore the continuous_audio_mode!\n", __func__ );
    /*in netflix case, we enable atmos drop at the beginning*/
    dolby_ms12_enable_atmos_drop(enable);

    // Early suspend case, need to response right now.
    if (is_suspend) {
        ALOGI("%s continuous_audio_mode set to %d\n", __func__ , enable);
        char buf[PROPERTY_VALUE_MAX] = {0};
        ret = property_get(DISABLE_CONTINUOUS_OUTPUT, buf, NULL);
        if (ret > 0) {
            sscanf(buf, "%d", &disable_continuous);
            ALOGI("%s[%s] disable_continuous %d\n", DISABLE_CONTINUOUS_OUTPUT, buf, disable_continuous);
        }
        if (disable_continuous) {
            adev_ms12_cleanup((struct audio_hw_device *)adev);
        } else {
            adev_ms12_prepare((struct audio_hw_device *)adev);
        }
    }
}

void set_ms12_full_dap_disable(struct dolby_ms12_desc *ms12, int full_dap_disable)
{
    char parm[64] = "";

    sprintf(parm, "%s %d", "-full_dap_disable", full_dap_disable);
    if ((strlen(parm)) > 0 && ms12)
        aml_ms12_update_runtime_params(ms12, parm);
}

void set_ms12_mc_enable(struct dolby_ms12_desc *ms12, int mc_enable)
{
    char parm[64] = "";
    if (!ms12) {
        ALOGE("set_ms12_mc_enable ms12 is null");
        return ;
    }
    if (!(ms12->output_config & MS12_OUTPUT_MASK_MC)) {
        return;
    }
    sprintf(parm, "%s %d", "-mc", mc_enable);
    if ((strlen(parm)) > 0 )
        aml_ms12_update_runtime_params(ms12, parm);
}

void set_ms12_ac4_1st_preferred_language_code(struct dolby_ms12_desc *ms12, char *lang_iso639_code)
{
    char parm[64] = "";
    sprintf(parm, "%s %s", "-lang", lang_iso639_code);
    ALOGI("%s line %d %c%c%C\n", __func__, __LINE__, lang_iso639_code[0], lang_iso639_code[1], lang_iso639_code[2]);
    if ((strlen(parm)) > 0 && ms12)
        aml_ms12_update_runtime_params(ms12, parm);
}

void set_ms12_ac4_2nd_preferred_language_code(struct dolby_ms12_desc *ms12, char *lang_iso639_code)
{
    char parm[64] = "";
    sprintf(parm, "%s %s", "-lang2", lang_iso639_code);
    ALOGI("%s line %d %c%c%C\n", __func__, __LINE__, lang_iso639_code[0], lang_iso639_code[1], lang_iso639_code[2]);
    if ((strlen(parm)) > 0 && ms12)
        aml_ms12_update_runtime_params(ms12, parm);
}

void set_ms12_ac4_prefer_presentation_selection_by_associated_type_over_language(struct dolby_ms12_desc *ms12, int prefer_selection_type)
{
    char parm[64] = "";
    sprintf(parm, "%s %d", "-pat", prefer_selection_type);
    ALOGI("%s line %d prefer_selection_type %d\n", __func__, __LINE__, prefer_selection_type);
    if ((strlen(parm)) > 0 && ms12)
        aml_ms12_update_runtime_params(ms12, parm);
}

void set_ms12_ac4_short_prog_identifier(struct dolby_ms12_desc *ms12, int short_program_identifier)
{
    char parm[64] = "";
    sprintf(parm, "%s %d", "-ac4_short_prog_id", short_program_identifier);
    if ((strlen(parm)) > 0 && ms12)
        aml_ms12_update_runtime_params(ms12, parm);
}


void dtv_set_ms12_volume_on_non_TV_device(struct aml_stream_out *aml_out)
{
    struct aml_audio_device *adev = aml_out->dev;

    float out_gain = 1.0f;

    /* For dev->mix case, eg: dtv -> usb card. We control the volume in in_read function. */
    if (!adev->dev2mix_patch) {
        out_gain = adev->sink_gain[get_output_by_devices(adev->cur_out_devices)];
    }
    if (adev->tv_mute && is_dev_patch_exist(adev)) {
        out_gain = 0.0f;
    }
    /*
    for tv case, volume control it in audio_hal_data_processing
    for non tv case, dtv stream vol control in dolby_ms12_set_main_volume
    */
    if (!is_TV(adev)) {
        if (is_dev_patch_exist(adev) && is_same_patch_src(adev, SRC_DTV)) {
            //when Dolby MS12 use not 1.0 volume "-sys_prim_mixgain <3 int>
            //the PCM Render can not output at a same volume for both DDP and AC4.
            //AC4 should use the 1.0 volume and control the volume through the PCM output.
            //After add this patch, the Bitstream output volume will always 1.0,
            //its volume should be controled by the Sink Device.
            if (!is_AC4_stream_with_pcm_sink_on_stb(aml_out)) {
                out_gain *= get_dtv_volume(adev);
            }
            else {
                out_gain = 1.0f;
            }
            set_ms12_main_volume(&adev->ms12, out_gain);
            aml_out->ms12_vol_ctrl = true;
        }
    }
}

void set_ms12_ext_pcm_acmod_lfe(struct dolby_ms12_desc *ms12, audio_channel_mask_t channel_mask)
{
    char param[64];
    int acmod = 0;
    int lfe = 0;

    acmod = dolby_ms12_get_channel_config(channel_mask);
    lfe = dolby_ms12_get_lfe_config(channel_mask);
    if (acmod < 0 || lfe < 0) {
        ALOGE("%s invalid channel_mask 0x%x, acmod %d, lfe %d", __func__, channel_mask, acmod, lfe);
        return;
    }

    memset(param, 0, sizeof(param));
    sprintf(param, "%s %d", "-chp", acmod);
    if ((strlen(param)) > 0 && ms12) {
        aml_ms12_update_runtime_params(ms12, param);
    }

    memset(param, 0, sizeof(param));
    sprintf(param, "%s %d", "-lp", lfe);
    if ((strlen(param)) > 0 && ms12) {
        aml_ms12_update_runtime_params(ms12, param);
    }
}

void set_ms12_sys_pcm_acmod_lfe(struct dolby_ms12_desc *ms12, audio_channel_mask_t channel_mask)
{
    char param[64];
    int acmod = 0;
    int lfe = 0;

    acmod = dolby_ms12_get_channel_config(channel_mask);
    lfe = dolby_ms12_get_lfe_config(channel_mask);
    if (acmod < 0 || lfe < 0) {
        ALOGE("%s invalid channel_mask 0x%x, acmod %d, lfe %d", __func__, channel_mask, acmod, lfe);
        return;
    }

    memset(param, 0, sizeof(param));
    sprintf(param, "%s %d", "-chs", acmod);
    if ((strlen(param)) > 0 && ms12) {
        aml_ms12_update_runtime_params(ms12, param);
    }

    memset(param, 0, sizeof(param));
    sprintf(param, "%s %d", "-ls", lfe);
    if ((strlen(param)) > 0 && ms12) {
        aml_ms12_update_runtime_params(ms12, param);
    }
}


void set_ms12_app_pcm_acmod_lfe(struct dolby_ms12_desc *ms12, audio_channel_mask_t channel_mask)
{
    char param[64];
    int acmod = 0;
    int lfe = 0;

    acmod = dolby_ms12_get_channel_config(channel_mask);
    lfe = dolby_ms12_get_lfe_config(channel_mask);
    if (acmod < 0 || lfe < 0) {
        ALOGE("%s invalid channel_mask 0x%x, acmod %d, lfe %d", __func__, channel_mask, acmod, lfe);
        return;
    }

    memset(param, 0, sizeof(param));
    sprintf(param, "%s %d", "-chas", acmod);
    if ((strlen(param)) > 0 && ms12) {
        aml_ms12_update_runtime_params(ms12, param);
    }

    memset(param, 0, sizeof(param));
    sprintf(param, "%s %d", "-las", lfe);
    if ((strlen(param)) > 0 && ms12) {
        aml_ms12_update_runtime_params(ms12, param);
    }
}
