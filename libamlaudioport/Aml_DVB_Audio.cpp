#define LOG_TAG "Aml_DVB_Audio"
#include <utils/Log.h>
#include <unistd.h>
#include <cutils/str_parms.h>
#include <AmlAudioOutPort.h>
#include <Aml_DVB_Audio.h>

using namespace android;

sp<AmlAudioOutPort> aml_audioport = new AmlAudioOutPort();
int dvb_audio_start_decoder(int fmt, int has_video) {
   char temp_buf[64] = {0};
   int ret = aml_audioport->createAudioPatch();
   if (ret == NO_ERROR) {
      aml_audioport->setParameters(String8("hal_param_tuner_in=dtv"));
      sprintf (temp_buf, "hal_param_dtv_audio_fmt=%d", fmt);
      aml_audioport->setParameters(String8(temp_buf));
      memset(temp_buf,0,sizeof(temp_buf));
      sprintf (temp_buf, "hal_param_has_dtv_video=%d", has_video);
      aml_audioport->setParameters(String8(temp_buf));
      aml_audioport->setParameters(String8("hal_param_dtv_patch_cmd=1"));
   } else {
      ALOGI("createAudioPatch fail");
      return -1;
   }
   return NO_ERROR;
}

int dvb_audio_stop_decoder(void) {
   aml_audioport->setParameters(String8("hal_param_dtv_patch_cmd=2"));
   return aml_audioport->releaseAudioPatch();
}

int dvb_audio_pause_decoder(void) {
   return aml_audioport->setParameters(String8("hal_param_dtv_patch_cmd=3"));
}

int dvb_audio_resume_decoder(void) {

  return aml_audioport->setParameters(String8("hal_param_dtv_patch_cmd=4"));
}

int audio_hal_set_ad(int fmt, int pid) {
  ALOGI("fmt %d pid %d",fmt,pid);
  return NO_ERROR;
}

int dvb_audio_set_volume(float volume) {
    char temp_buf[64] = {0};
    sprintf (temp_buf, "SOURCE_GAIN=%f %f %f %f ", 1.0,volume,1.0,1.0);
    return aml_audioport->setParameters(String8("temp_buf"));

}

int dvb_audio_set_mute(int mute) {
   if (mute) {
       return aml_audioport->setParameters(String8("hal_param_tv_mute=1"));
   } else {
       return aml_audioport->setParameters(String8("hal_param_tv_mute=0"));
   }

}

int dvb_audio_set_pre_gain(int gain) {
    ALOGI("gain %d",gain);
    return NO_ERROR;
}

int dvb_audio_set_pre_mute(int mute) {
    if (mute ) {
       return aml_audioport->setParameters(String8("hal_param_tv_mute=1"));
    } else {
       return aml_audioport->setParameters(String8("hal_param_tv_mute=0"));
    }
}

int dvb_audio_get_latencyms(int demux_id) {
    ALOGV("demux_id %d",demux_id);
    struct str_parms *parms;
    int latencyms = 0;
    int ret = 0;
    char temp_buf[64] = {0};
    sprintf (temp_buf, "hal_param_dtv_latencyms_id=%d", demux_id);
    aml_audioport->setParameters(String8(temp_buf));
    String8 mString = aml_audioport->getParameters(String8("hal_param_dtv_latencyms"));
    if (!mString.empty()) {
        parms = str_parms_create_str(mString.c_str());
        ret = str_parms_get_int(parms, "hal_param_dtv_latencyms", &latencyms);
        if (ret < 0)
            ALOGE("str_parms_get_int is error ");
        str_parms_destroy (parms);
        mString.clear();
        ALOGI("dvb_latencyms:%d ", latencyms);
        return latencyms;
    } else {
         mString.clear();
         return -1;
    }
}

int dvb_audio_get_ac4_active_pres_id(int demux_id) {
    ALOGV("demux_id %d",demux_id);
    int ac4_active_pres_id = -1;
    int ret = 0;
    struct str_parms *parms;
    String8 mString = aml_audioport->getParameters(String8("get_ac4_active_pres_id"));
    if (!mString.empty()) {
        parms = str_parms_create_str(mString.c_str());
        ret = str_parms_get_int(parms, "ac4_active_pres_id", &ac4_active_pres_id);
        if (ret < 0)
            ALOGE("str_parms_get_int is error ");
        str_parms_destroy (parms);
        mString.clear();
        ALOGI("ac4_active_pres_id:%d ", ac4_active_pres_id);
        return ac4_active_pres_id;
    } else {
         mString.clear();
         return -1;
    }
}

int dvb_audio_set_sound_mode(int demux_id, int mode) {
    ALOGV("demux_id %d",demux_id);
    char temp_buf[64] = {0};
    sprintf (temp_buf, "hal_param_audio_output_mode=%d", mode);
    return aml_audioport->setParameters(String8(temp_buf));
}

int dvb_audio_get_sound_mode(int demux_id) {
    ALOGV("demux_id %d",demux_id);
    int dtv_sound_mode = 0;
    int ret = 0;
    struct str_parms *parms;
    String8 mString = aml_audioport->getParameters(String8("hal_param_audio_output_mode"));
    if (!mString.isEmpty()) {
       parms = str_parms_create_str(mString.c_str());
       ret = str_parms_get_int(parms, "hal_param_audio_output_mode", &dtv_sound_mode);
       if (ret < 0)
           ALOGE("str_parms_get_int is error ");
       str_parms_destroy (parms);
       mString.clear();
       ALOGI("dtv_sound_mode:%d ", dtv_sound_mode);
       return dtv_sound_mode;
    } else {
        mString.clear();
        return dtv_sound_mode;
    }

}

int dvb_audio_set_param(AUDIO_DTV_PATCH_CMD_TYPE para_type, int demux_id, int val)
{
  switch (para_type) {
      case AUDIO_DTV_PATCH_CMD_OUTPUT_MODE:
        dvb_audio_set_sound_mode(demux_id, val);
        break;
      default:
          ALOGI("unsupport para_type %d", para_type);
    }
    return 0;

}
int dvb_audio_get_param(AUDIO_DTV_PATCH_CMD_TYPE para_type, int demux_id, int *val)
{
    switch (para_type) {
      case AUDIO_DTV_PATCH_CMD_OUTPUT_MODE:
         *val = dvb_audio_get_sound_mode(demux_id);
        break;
      default:
          ALOGI("unsupport para_type %d", para_type);
    }
    return 0;
}

int dvb_audio_get_status(void *status) {
    ALOGI("status %p",status);
    return NO_ERROR;
}//TBD
