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
 *
 *  DESCRIPTION:
 *     brief  Audio DVB API Functions.
 *
 */

#ifndef AML_DVB_AUDIO__H
#define AML_DVB_AUDIO__H

/* refer to AudioSystemCmdManager */
typedef enum {
    AUDIO_DTV_PATCH_CMD_NULL        = 0,
    AUDIO_DTV_PATCH_CMD_START       = 1,    /* AUDIO_SERVICE_CMD_START_DECODE */
    AUDIO_DTV_PATCH_CMD_PAUSE       = 2,    /* AUDIO_SERVICE_CMD_PAUSE_DECODE */
    AUDIO_DTV_PATCH_CMD_RESUME      = 3,    /* AUDIO_SERVICE_CMD_RESUME_DECODE */
    AUDIO_DTV_PATCH_CMD_STOP        = 4,    /* AUDIO_SERVICE_CMD_STOP_DECODE */
    AUDIO_DTV_PATCH_CMD_AD_SUPPORT  = 5,    /* AUDIO_SERVICE_CMD_SET_DECODE_AD */
    AUDIO_DTV_PATCH_CMD_VOLUME  = 6,        /*AUDIO_SERVICE_CMD_VOLUME*/
    AUDIO_DTV_PATCH_CMD_MUTE    = 7,        /*AUDIO_SERVICE_CMD_MUTE*/
    AUDIO_DTV_PATCH_CMD_OUTPUT_MODE = 8,    /*AUDIO_SERVICE_CMD_OUTPUT_MODE */
    AUDIO_DTV_PATCH_CMD_PRE_GAIN  = 9,      /*AUDIO_SERVICE_CMD_PRE_GAIN */
    AUDIO_DTV_PATCH_CMD_PRE_MUTE  = 10,     /*AUDIO_SERVICE_CMD_PRE_MUTE */
    AUDIO_DTV_PATCH_CMD_OPEN        = 12,   /*AUDIO_SERVICE_CMD_OPEN_DECODER */
    AUDIO_DTV_PATCH_CMD_CLOSE       = 13,   /*AUDIO_SERVICE_CMD_CLOSE_DECODER */
    AUDIO_DTV_PATCH_CMD_DEMUX_INFO = 14,    /*AUDIO_SERVICE_CMD_SET_DEMUX_INFO ;*/
    AUDIO_DTV_PATCH_CMD_SECURITY_MEM_LEVEL = 15,/*AUDIO_SERVICE_CMD_SET_SECURITY_MEM_LEVEL*/
    AUDIO_DTV_PATCH_CMD_HAS_VIDEO   = 16,   /*AUDIO_SERVICE_CMD_HAS_VIDEO */
    AUDIO_DTV_PATCH_CMD_CONTROL       = 17,
    AUDIO_DTV_PATCH_CMD_PID       = 18,
    AUDIO_DTV_PATCH_CMD_FMT        = 19,
    AUDIO_DTV_PATCH_CMD_AD_PID      = 20,
    AUDIO_DTV_PATCH_CMD_AD_FMT      = 21,
    AUDIO_DTV_PATCH_CMD_AD_ENABLE      = 22,
    AUDIO_DTV_PATCH_CMD_AD_MIX_LEVEL   = 23,
    AUDIO_DTV_PATCH_CMD_AD_VOL_LEVEL   = 24,
    AUDIO_DTV_PATCH_CMD_MEDIA_SYNC_ID   = 25,
    AUDIO_DTV_PATCH_CMD_MEDIA_PRESENTATION_ID   = 26,
    AUDIO_DTV_PATCH_CMD_DTV_LATENCYMS_ID = 27,
    AUDIO_DTV_PATCH_CMD_MEDIA_FIRST_LANG  = 29,
    AUDIO_DTV_PATCH_CMD_MEDIA_SECOND_LANG = 30,
    AUDIO_DTV_PATCH_CMD_SPDIF_PROTECTION_MODE  = 31,
    AUDIO_DTV_PATCH_CMD_ES_PTS_DTS_FLAG  = 32,
    AUDIO_DTV_PATCH_CMD_NUM             = 33,
} AUDIO_DTV_PATCH_CMD_TYPE;


#ifdef  __cplusplus
extern "C"
{
#endif

int dvb_audio_start_decoder(int fmt, int has_video);

int dvb_audio_stop_decoder(void);

int dvb_audio_pause_decoder(void);

int dvb_audio_resume_decoder(void);

int dvb_audio_set_ad(int fmt, int pid);

int dvb_audio_set_volume(float volume);

int dvb_audio_set_mute(int mute);

int dvb_audio_set_pre_gain(int gain);

int dvb_audio_set_pre_mute(int mute);

int dvb_audio_get_latencyms(int demux_id);

int dvb_audio_get_ac4_active_pres_id(int demux_id);

int dvb_audio_set_sound_mode(int demux_id, int mode);

int dvb_audio_get_sound_mode(int demux_id);
int dvb_audio_set_param(AUDIO_DTV_PATCH_CMD_TYPE para_type, int demux_id, int val);
int dvb_audio_get_param(AUDIO_DTV_PATCH_CMD_TYPE para_type, int demux_id, int *val);
int audio_hal_get_status(void *status);//TBD

#ifdef  __cplusplus
}
#endif

#endif

