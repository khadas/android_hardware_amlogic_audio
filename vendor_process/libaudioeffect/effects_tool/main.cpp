/*
 * Copyright (C) 2018 Amlogic Corporation.
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
 *      This file implements a special EQ  from Amlogic.
 *
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <utils/Log.h>
#include <errno.h>
#include <media/AudioEffect.h>
#include <media/AudioSystem.h>
#include <media/AudioParameter.h>
#include <system/audio_effect.h>
#ifdef USE_IDENTITY_CREATE_AUDIOEFFECT
#include <android/content/AttributionSourceState.h>
#include <binder/MemoryDealer.h>
#include <media/AidlConversion.h>
#endif

#include "vx_ctrl.h"

#ifdef LOG
#undef LOG
#endif
#define LOG(x...) printf("[AudioEffect] " x)
#define LSR (1)

using namespace android;
//-----------Balance parameters-------------------------------
//Warning:balance is used for 51 bands

#define BALANCE_MAX_BANDS 51
#define BALANCE_MAX_LEVEL 25

#define BITS    16
#define MAXbit  (1 << (BITS - 1))
#define INT2FLOAT(a) ((float)a / MAXbit)
#define FLOAT2INT(a) (int)((float)a * MAXbit)

extern "C" {

typedef struct Balance_param_s {
    effect_param_t param;
    uint32_t command;
    union {
        int32_t v;
        float f;
        float index[BALANCE_MAX_BANDS];
    };
} Balance_param_t;

typedef enum {
    BALANCE_PARAM_LEVEL = 0,
    BALANCE_PARAM_ENABLE,
    BALANCE_PARAM_LEVEL_NUM,
    BALANCE_PARAM_INDEX,
    BALANCE_PARAM_USB,
    BALANCE_PARAM_BT,
} Balance_params;

Balance_param_t gBalanceParam[] = {
    {{0, 4, 4},                     BALANCE_PARAM_LEVEL,       {BALANCE_MAX_LEVEL}  },
    {{0, 4, 4},                     BALANCE_PARAM_ENABLE,      {1}                  },
    {{0, 4, 4},                     BALANCE_PARAM_LEVEL_NUM,   {BALANCE_MAX_BANDS}  },
    {{0, 4, BALANCE_MAX_BANDS * 4}, BALANCE_PARAM_INDEX,       {0}                  },
    {{0, 4, 4},                     BALANCE_PARAM_USB,         {0}                  },
    {{0, 4, 4},                     BALANCE_PARAM_BT,          {0}                  },
};

struct balance_gain {
   float right_gain;
   float left_gain;
};

int balance_level_num = 0;
float index1[BALANCE_MAX_BANDS] = {0};

const char *BalanceStatusstr[] = {"Disable", "Enable"};

//-------------TrebleBass parameters--------------------------
typedef struct TrebleBass_param_s {
    effect_param_t param;
    uint32_t command;
    union {
        uint32_t v;
        float f;
    };
} TrebleBass_param_t;

typedef enum {
    TREBLEBASS_PARAM_BASS_LEVEL = 0,
    TREBLEBASS_PARAM_TREBLE_LEVEL,
    TREBLEBASS_PARAM_ENABLE,
} TrebleBass_params;

TrebleBass_param_t gTrebleBassParam[] = {
    {{0, 4, 4},     TREBLEBASS_PARAM_BASS_LEVEL,       {0}  },
    {{0, 4, 4},     TREBLEBASS_PARAM_TREBLE_LEVEL,     {0}  },
    {{0, 4, 4},     TREBLEBASS_PARAM_ENABLE,           {1}  },
};

const char *TREBLEBASSStatusstr[] = {"Disable", "Enable"};

//-------------HPEQ parameters--------------------------
typedef struct HPEQ_param_s {
    effect_param_t param;
    uint32_t command;
    union {
        uint32_t v;
        float f;
        signed char band[5];
    };
} HPEQ_param_t;

typedef enum {
    HPEQ_PARAM_ENABLE = 0,
    HPEQ_PARAM_EFFECT_MODE,
    HPEQ_PARAM_EFFECT_CUSTOM,
} HPEQ_params;

HPEQ_param_t gHPEQParam[] = {
    {{0, 4, 4},     HPEQ_PARAM_ENABLE,         {1}  },
    {{0, 4, 4},     HPEQ_PARAM_EFFECT_MODE,    {0}  },
    {{0, 4, 5},     HPEQ_PARAM_EFFECT_CUSTOM,  {0}  },
};

const char *HPEQStatusstr[] = {"Disable", "Enable"};

//-------------GEQ parameters--------------------------
typedef struct GEQ_param_s {
    effect_param_t param;
    uint32_t command;
    union {
        uint32_t v;
        float f;
        signed char band[9];
    };
} GEQ_param_t;

typedef enum {
    GEQ_PARAM_ENABLE = 0,
    GEQ_PARAM_EFFECT_MODE,
    GEQ_PARAM_EFFECT_CUSTOM,
} GEQ_params;

GEQ_param_t gGEQParam[] = {
    {{0, 4, 4},     GEQ_PARAM_ENABLE,          {1}  },
    {{0, 4, 4},     GEQ_PARAM_EFFECT_MODE,     {0}  },
    {{0, 4, 9},     GEQ_PARAM_EFFECT_CUSTOM,   {0}  },
};

const char *GEQStatusstr[] = {"Disable", "Enable"};

//-------UUID------------------------------------------
typedef enum {
    EFFECT_BALANCE = 0,
    EFFECT_TREBLEBASS,
    EFFECT_HPEQ,
    EFFECT_GEQ,
    EFFECT_VIRTUALX,
    EFFECT_MAX,
} EFFECT_params;

effect_uuid_t gEffectStr[] = {
    {0x6f33b3a0, 0x578e, 0x11e5, 0x892f, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}, // 0:Balance
    {0x76733af0, 0x2889, 0x11e2, 0x81c1, {0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66}}, // 1:TrebleBass
    {0x049754aa, 0xc4cf, 0x439f, 0x897e, {0x37, 0xdd, 0x0c, 0x38, 0x11, 0x20}}, // 2:Hpeq
    {0x2e2a5fa6, 0xcae8, 0x45f5, 0xbb70, {0xa2, 0x9c, 0x1f, 0x30, 0x74, 0xb2}}, // 3:Geq
    {0x61821587, 0xce3c, 0x4aac, 0x9122, {0x86, 0xd8, 0x74, 0xea, 0x1f, 0xb1}}, // 4:Virtualx
};

static int Balance_effect_func(sp<AudioEffect>& gAudioEffect, int gParamIndex, int gParamValue)
{
    balance_gain blrg;
    String8 keyValuePairs = String8("");
    String8 mString = String8("");
    char value_string[64] = {0};
    AudioParameter param = AudioParameter();
    audio_io_handle_t handle = AUDIO_IO_HANDLE_NONE;
    int32_t value = 0;
    if (balance_level_num == 0) {
        gAudioEffect->getParameter(&gBalanceParam[BALANCE_PARAM_LEVEL_NUM].param);
        balance_level_num = gBalanceParam[BALANCE_PARAM_LEVEL_NUM].v;
        LOG("Balance: Level size = %d\n", balance_level_num);
    }
    gAudioEffect->getParameter(&gBalanceParam[BALANCE_PARAM_INDEX].param);
    for (int i = 0; i < balance_level_num; i++) {
        index1[i] = gBalanceParam[BALANCE_PARAM_INDEX].index[i];
        //LOG("Balance: index = %f\n", index1[i]);
    }
    switch (gParamIndex) {
    case BALANCE_PARAM_LEVEL:
        if (gParamValue  < 0 || gParamValue > ((balance_level_num - 1) << 1)) {
            LOG("Balance: Level gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gBalanceParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gBalanceParam[gParamIndex].param);
        gAudioEffect->getParameter(&gBalanceParam[gParamIndex].param);
        LOG("Balance: Level is %d -> %d\n", gParamValue, gBalanceParam[gParamIndex].v);
        return 0;
    case BALANCE_PARAM_ENABLE:
        if (gParamValue < 0 || gParamValue > 1) {
            LOG("Balance: Status gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gBalanceParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gBalanceParam[gParamIndex].param);
        gAudioEffect->getParameter(&gBalanceParam[gParamIndex].param);
        LOG("Balance: Status is %d -> %s\n", gParamValue, BalanceStatusstr[gBalanceParam[gParamIndex].v]);
        return 0;
    case BALANCE_PARAM_USB:
        gBalanceParam[gParamIndex].v = gParamValue;
        value = (((int)gBalanceParam[gParamIndex].v) >> LSR);
        if (value >= balance_level_num)
           value = balance_level_num;
        else if (value < 0)
            value = 0;
        if (value < (balance_level_num >> LSR)) {
           //right process
           blrg.left_gain = 1;
           blrg.right_gain = index1[value];
           snprintf(value_string, 40, "%f %f", blrg.right_gain,blrg.left_gain);
           param.add(String8("USB_GAIN_RIGHT"), String8(value_string));
           keyValuePairs = param.toString();
           if (AudioSystem::setParameters(handle, keyValuePairs) != NO_ERROR) {
             LOG("setusbgain: Set gain failed\n");
             return -1;
           }
         param.remove(String8("USB_GAIN_RIGHT"));
        } else {
          //left process
           blrg.right_gain = 1;
           blrg.left_gain = index1[value];
           snprintf(value_string, 40, "%f %f", blrg.left_gain,blrg.right_gain);
           param.add(String8("USB_GAIN_LEFT"), String8(value_string));
           keyValuePairs = param.toString();
           if (AudioSystem::setParameters(handle, keyValuePairs) != NO_ERROR) {
             LOG("setusbgain: Set gain failed\n");
             return -1;
          }
           param.remove(String8("USB_GAIN_LEFT"));
       }
        return 0;
    case BALANCE_PARAM_BT:
        gBalanceParam[gParamIndex].v = gParamValue;
        value = (((int)gBalanceParam[gParamIndex].v) >> LSR);
        if (value >= balance_level_num)
           value = balance_level_num;
        else if (value < 0)
            value = 0;
        if (value < (balance_level_num >> LSR)) {
           //right process
           blrg.left_gain = 1;
           blrg.right_gain = index1[value];
           LOG("blrg.left_gain is %f right gain is %f",blrg.left_gain,blrg.right_gain);
           snprintf(value_string, 40, "%f %f", blrg.right_gain,blrg.left_gain);
           param.add(String8("BT_GAIN_RIGHT"), String8(value_string));
           keyValuePairs = param.toString();
           if (AudioSystem::setParameters(handle, keyValuePairs) != NO_ERROR) {
             LOG("setbtgain: Set gain failed\n");
             return -1;
           }
         param.remove(String8("BT_GAIN_RIGHT"));
        } else {
          //left process
           blrg.right_gain = 1;
           blrg.left_gain = index1[value];
           LOG("blrg.left_gain is %f right gain is %f",blrg.left_gain,blrg.right_gain);
           snprintf(value_string, 40, "%f %f", blrg.left_gain,blrg.right_gain);
           param.add(String8("BT_GAIN_LEFT"), String8(value_string));
           keyValuePairs = param.toString();
           if (AudioSystem::setParameters(handle, keyValuePairs) != NO_ERROR) {
             LOG("setbtgain: Set gain failed\n");
             return -1;
           }
           param.remove(String8("BT_GAIN_LEFT"));
        }
        return 0;
    default:
        LOG("Balance: ParamIndex = %d invalid\n", gParamIndex);
        return -1;
    }
}

static int TrebleBass_effect_func(sp<AudioEffect>& gAudioEffect, int gParamIndex, int gParamValue)
{
    switch (gParamIndex) {
    case TREBLEBASS_PARAM_BASS_LEVEL:
        if (gParamValue < 0 || gParamValue > 100) {
            LOG("TrebleBass: Bass gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gTrebleBassParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gTrebleBassParam[gParamIndex].param);
        gAudioEffect->getParameter(&gTrebleBassParam[gParamIndex].param);
        LOG("TrebleBass: Bass is %d -> %d level\n", gParamValue, gTrebleBassParam[gParamIndex].v);
        return 0;
    case TREBLEBASS_PARAM_TREBLE_LEVEL:
        if (gParamValue < 0 || gParamValue > 100) {
            LOG("TrebleBass: Treble gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gTrebleBassParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gTrebleBassParam[gParamIndex].param);
        gAudioEffect->getParameter(&gTrebleBassParam[gParamIndex].param);
        LOG("TrebleBass: Treble is %d -> %d level\n", gParamValue, gTrebleBassParam[gParamIndex].v);
        return 0;
    case TREBLEBASS_PARAM_ENABLE:
        if (gParamValue < 0 || gParamValue > 1) {
            LOG("TrebleBass: Status gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gTrebleBassParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gTrebleBassParam[gParamIndex].param);
        gAudioEffect->getParameter(&gTrebleBassParam[gParamIndex].param);
        LOG("TrebleBass: Status is %d -> %s\n", gParamValue, TREBLEBASSStatusstr[gTrebleBassParam[gParamIndex].v]);
        return 0;
    default:
        LOG("TrebleBass: ParamIndex = %d invalid\n", gParamIndex);
        return -1;
    }
}

static int HPEQ_effect_func(sp<AudioEffect>& gAudioEffect, int gParamIndex, int gParamValue, signed char gParamBand[5])
{
    switch (gParamIndex) {
    case HPEQ_PARAM_ENABLE:
         if (gParamValue < 0 || gParamValue > 1) {
            LOG("HPEQ: Status gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gHPEQParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gHPEQParam[gParamIndex].param);
        gAudioEffect->getParameter(&gHPEQParam[gParamIndex].param);
        LOG("HPEQ: Status is %d -> %s\n", gParamValue, HPEQStatusstr[gHPEQParam[gParamIndex].v]);
        return 0;
    case HPEQ_PARAM_EFFECT_MODE:
        if (gParamValue < 0 || gParamValue > 6) {
            LOG("Hpeq:gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gHPEQParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gHPEQParam[gParamIndex].param);
        gAudioEffect->getParameter(&gHPEQParam[gParamIndex].param);
        LOG("HPEQ: mode is %d -> %d\n", gParamValue, gHPEQParam[gParamIndex].v);
        return 0;
    case HPEQ_PARAM_EFFECT_CUSTOM:
        for (int i = 0; i < 5; i++) {
           if (gParamBand[i]< -10 || gParamBand[i] >10) {
              LOG("Hpeq:gParamBand[%d] = %d invalid\n",i, gParamBand[i]);
              return -1;
           }
        }
        gHPEQParam[gParamIndex].band[0] = gParamBand[0];
        gHPEQParam[gParamIndex].band[1] = gParamBand[1];
        gHPEQParam[gParamIndex].band[2] = gParamBand[2];
        gHPEQParam[gParamIndex].band[3] = gParamBand[3];
        gHPEQParam[gParamIndex].band[4] = gParamBand[4];
        gAudioEffect->setParameter(&gHPEQParam[gParamIndex].param);
        gAudioEffect->getParameter(&gHPEQParam[gParamIndex].param);
        return 0;
    default:
        LOG("HPEQ: ParamIndex = %d invalid\n", gParamIndex);
        return -1;
    }
}

static int GEQ_effect_func(sp<AudioEffect>& gAudioEffect, int gParamIndex, int gParamValue, signed char gParamBands[9])
{
    switch (gParamIndex) {
    case GEQ_PARAM_ENABLE:
         if (gParamValue < 0 || gParamValue > 1) {
            LOG("GEQ: Status gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gGEQParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gGEQParam[gParamIndex].param);
        gAudioEffect->getParameter(&gGEQParam[gParamIndex].param);
        LOG("GEQ: Status is %d -> %s\n", gParamValue, GEQStatusstr[gGEQParam[gParamIndex].v]);
        return 0;
    case GEQ_PARAM_EFFECT_MODE:
        if (gParamValue < 0 || gParamValue > 6) {
            LOG("Hpeq:gParamValue = %d invalid\n", gParamValue);
            return -1;
        }
        gGEQParam[gParamIndex].v = gParamValue;
        gAudioEffect->setParameter(&gGEQParam[gParamIndex].param);
        gAudioEffect->getParameter(&gGEQParam[gParamIndex].param);
        LOG("GEQ: mode is %d -> %d\n", gParamValue, gGEQParam[gParamIndex].v);
        return 0;
    case GEQ_PARAM_EFFECT_CUSTOM:
        for (int i = 0; i < 9; i++) {
           if (gParamBands[i]< -10 || gParamBands[i] >10) {
              LOG("Geq:gParamBands[%d] = %d invalid\n",i, gParamBands[i]);
              return -1;
           }
        }
        gGEQParam[gParamIndex].band[0] = gParamBands[0];
        gGEQParam[gParamIndex].band[1] = gParamBands[1];
        gGEQParam[gParamIndex].band[2] = gParamBands[2];
        gGEQParam[gParamIndex].band[3] = gParamBands[3];
        gGEQParam[gParamIndex].band[4] = gParamBands[4];
        gGEQParam[gParamIndex].band[5] = gParamBands[5];
        gGEQParam[gParamIndex].band[6] = gParamBands[6];
        gGEQParam[gParamIndex].band[7] = gParamBands[7];
        gGEQParam[gParamIndex].band[8] = gParamBands[8];
        gAudioEffect->setParameter(&gGEQParam[gParamIndex].param);
        gAudioEffect->getParameter(&gGEQParam[gParamIndex].param);
        return 0;
    default:
        LOG("GEQ: ParamIndex = %d invalid\n", gParamIndex);
        return -1;
    }
}

class AudioEffectCallback : public AudioEffect::IAudioEffectCallback {
  public:
    bool receivedFramesProcessed = false;

    void onFramesProcessed(int32_t framesProcessed) override {
        LOG("number of frames processed %d", framesProcessed);
        receivedFramesProcessed = true;
    }
};

#ifdef USE_IDENTITY_CREATE_AUDIOEFFECT
static const char* gPackageName = "AudioEffectTool";
static sp<AudioEffect> create_audio_effect(String16 name16 __unused, int index)
#else
static sp<AudioEffect> create_audio_effect(String16 name16, int index)
#endif
{
    status_t status = NO_ERROR;
    audio_session_t gSessionId = AUDIO_SESSION_OUTPUT_MIX;
    sp<AudioEffectCallback> cb;
#ifdef USE_IDENTITY_CREATE_AUDIOEFFECT
    std::string packageName{gPackageName};
    AttributionSourceState attributionSource;
    attributionSource.packageName = packageName;
    attributionSource.uid = VALUE_OR_FATAL(legacy2aidl_uid_t_int32_t(getuid()));
    attributionSource.pid = getpid();
    attributionSource.token = sp<BBinder>::make();
    sp<AudioEffect> pAudioEffect = new AudioEffect(attributionSource);
    cb = sp<AudioEffectCallback>::make();
#else
    sp<AudioEffect> pAudioEffect = new AudioEffect(name16);
    cb = nullptr;
#endif
    if (!pAudioEffect) {
        LOG("create audio effect object failed\n");
        return nullptr;
    }

    status = pAudioEffect->set(nullptr,
                            &(gEffectStr[index]), // specific uuid
                            0, // priority,
                            cb,
                            gSessionId,
                            AUDIO_IO_HANDLE_NONE,
                            {}, //default output device
                            false,
                            (cb != nullptr));
    if (status != NO_ERROR) {
        LOG("set effect parameters failed\n");
        return nullptr;
    }

    status = pAudioEffect->initCheck();
    if (status != NO_ERROR) {
        LOG("init audio effect failed\n");
        return nullptr;
    }

    pAudioEffect->setEnabled(true);
    LOG("effect %d is %s\n", index, pAudioEffect->getEnabled()?"enabled":"disabled");
    return pAudioEffect;
}

int GetIntData (int *data)
{
    int status = scanf("%d", data);
    if (status == 0) {
        int status_temp = scanf("%*s");
        if (status_temp < 0)
            LOG("Erorr input! Pls Retry!\n");
        return -1;
    }
    return 0;
}

int GetFloatData (float *data)
{
    int status = scanf("%f", data);
    if (status == 0) {
        int status_temp = scanf("%*s");
        if (status_temp < 0)
            LOG("Erorr input! Pls Retry!\n");
        return -1;
    }
    return 0;
}

void PrintHelp(int gEffectIndex, char *name)
{
    if (gEffectIndex == EFFECT_BALANCE) {
        LOG("**********************************Balance***********************************\n");
        LOG("Amlogic Balance EffectIndex: %d\n", (int)EFFECT_BALANCE);
        LOG("Usage: %s %d <ParamIndex> <ParamValue/ParamScale/gParamBand>\n", name, (int)EFFECT_BALANCE);
        LOG("ParamIndex: 0 -> Level\n");
        LOG("ParamValue: 0 ~ 100\n");
        LOG("ParamIndex: 1 -> Enable\n");
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: 4 -> Usb_Balance\n");
        LOG("ParamValue: 0 ~ 100\n");
        LOG("ParamIndex: 5 -> Bt_Balance\n");
        LOG("ParamValue: 0 ~ 100\n");
        LOG("****************************************************************************\n\n");
    }  else if (gEffectIndex == EFFECT_TREBLEBASS) {
        LOG("*********************************TrebleBass*********************************\n");
        LOG("Amlogic Treble/Bass EffectIndex: %d\n", (int)EFFECT_TREBLEBASS);
        LOG("Usage: %s %d <ParamIndex> <ParamValue/ParamScale/gParamBand>\n", name, (int)EFFECT_TREBLEBASS);
        LOG("ParamIndex: 0 -> Bass\n");
        LOG("ParamValue: 0 ~ 100\n");
        LOG("ParamIndex: 1 -> Treble\n");
        LOG("ParamValue: 0 ~ 100\n");
        LOG("ParamIndex: 2 -> Enable\n");
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("****************************************************************************\n\n");
    } else if (gEffectIndex == EFFECT_HPEQ) {
        LOG("*********************************HPEQ***************************************\n");
        LOG("Amlogic HPEQ EffectIndex: %d\n", (int)EFFECT_HPEQ);
        LOG("Usage: %s %d <ParamIndex> <ParamValue/ParamScale/gParamBand>\n", name, (int)EFFECT_HPEQ);
        LOG("ParamIndex: 0 -> Enable\n");
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: 1 -> Mode\n");
        LOG("ParamValue: 0 -> Standard  1 -> Music   2 -> news  3 -> movie   4 -> game   5->user\n");
        LOG("ParamIndex: 2 -> custom\n");
        LOG("ParamValue: -10 ~10 \n");
        LOG("****************************************************************************\n\n");
    } else if (gEffectIndex == EFFECT_GEQ) {
        LOG("*********************************GEQ****************************************\n");
        LOG("Amlogic GEQ EffectIndex: %d\n", (int)EFFECT_GEQ);
        LOG("Usage: %s %d <ParamIndex> <ParamValue/ParamScale/gParamBand>\n", name, (int)EFFECT_GEQ);
        LOG("ParamIndex: 0 -> Enable\n");
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: 1 -> Mode\n");
        LOG("ParamValue: 0 -> Standard  1 -> Music   2 -> news  3 -> movie   4 -> game   5->user\n");
        LOG("ParamIndex: 2 -> custom\n");
        LOG("ParamValue: -10 ~10 \n");
        LOG("****************************************************************************\n\n");
    }
}

int main(int argc,char **argv)
{
    int i;
    int ret = -1;
    int gEffectIndex = 0;
    int gParamIndex = 0;
    int gParamValue = 0;
    signed char gParamBand[5] = {0};
    signed char gParamBands[9] = {0};
    String16 name16[EFFECT_MAX] = {String16("AudioEffectEQTest"), String16("AudioEffectHPEQTest"),
        String16("AudioEffectGEQTest"),String16("AudioEffectVirtualxTest")};
    sp<AudioEffect> gAudioEffect[EFFECT_MAX] = {0};

    if (argc < 4) {
        if (argc == 1) {
            LOG("********************Audio Effect Tuning Tool help*****************************\n");
            LOG("Choice an audio effect to get detail help!\n");
            LOG("EffectIndex: 0: Amlogic BALANCE\n");
            LOG("             1: Amlogic TREBLEBASS\n");
            LOG("             2: Amlogic HPEQ\n");
            LOG("             3: Amlogic GEQ\n");
            LOG("             4: VIRTUALX\n");
            LOG("Usage: %s <EffectIndex>\n", argv[0]);
            LOG("******************************************************************************\n");
            return 0;
        }
        sscanf(argv[1], "%d", &gEffectIndex);
        PrintHelp(gEffectIndex, argv[0]);
        return 0;
    }

    LOG("start...\n");
    sscanf(argv[1], "%d", &gEffectIndex);
    //parse ParamIndex in VX ctrl file
    if (gEffectIndex != EFFECT_VIRTUALX) {
        sscanf(argv[2], "%d", &gParamIndex);
    }

    switch (gEffectIndex) {
    case EFFECT_BALANCE:
        //------------get Balance parameters---------------------------------------
        sscanf(argv[3], "%d", &gParamValue);
        LOG("EffectIndex:%d, ParamIndex:%d, Paramvalue:%d\n", gEffectIndex, gParamIndex, gParamValue);
        break;
    case EFFECT_TREBLEBASS:
        //------------get TrebleBass parameters------------------------------------
        sscanf(argv[3], "%d", &gParamValue);
        LOG("EffectIndex:%d, ParamIndex:%d, Paramvalue:%d\n", gEffectIndex, gParamIndex, gParamValue);
        break;
    case EFFECT_HPEQ:
        //------------get HPEQ parameters------------------------------------------
        if (gParamIndex == HPEQ_PARAM_EFFECT_CUSTOM) {
            for (int i = 0; i < 5; i++) {
                int ParamBand = 0;
                sscanf(argv[i + 3], "%d", &ParamBand);
                gParamBand[i] = (char)ParamBand;
                LOG("EffectIndex:%d, ParamIndex:%d, ParamBand:%d\n", gEffectIndex, gParamIndex, gParamBand[i]);
            }
        } else {
            sscanf(argv[3], "%d", &gParamValue);
            LOG("EffectIndex:%d, ParamIndex:%d, Paramvalue:%d\n", gEffectIndex, gParamIndex, gParamValue);
        }
        break;
    case EFFECT_GEQ:
        //------------get GEQ parameters------------------------------------------
        if (gParamIndex == GEQ_PARAM_EFFECT_CUSTOM) {
            for (int i = 0; i < 9; i++) {
                int ParamBand = 0;
                sscanf(argv[i + 3], "%d", &ParamBand);
                gParamBands[i] = (char)ParamBand;
                LOG("EffectIndex:%d, ParamIndex:%d, ParamBand:%d\n", gEffectIndex, gParamIndex, (int)gParamBands[i]);
            }
        } else {
            sscanf(argv[3], "%d", &gParamValue);
            LOG("EffectIndex:%d, ParamIndex:%d, Paramvalue:%d\n", gEffectIndex, gParamIndex, gParamValue);
        }
        break;
    case EFFECT_VIRTUALX:
        ret = set_param_from_cmd_line(argc, argv);
        if (ret < 0) {
            LOG("main() Virtualx parse cmd line for VX Param fail!\n");
        }
        gParamIndex = ret;
        break;
    default:
        LOG("EffectIndex = %d is invalid\n", gEffectIndex);
        return -1;
    }

    sp<AudioEffect> new_effect;
    while (1) {
        switch (gEffectIndex) {
        case EFFECT_BALANCE:
            new_effect = create_audio_effect(name16[EFFECT_BALANCE], EFFECT_BALANCE);
            if (new_effect == NULL) {
                LOG("create Balance effect failed\n");
                goto Error;
            }
            //------------set Balance parameters---------------------------------------
            if (Balance_effect_func(new_effect, gParamIndex, gParamValue) < 0) {
                LOG("Balance Test failed\n");
                goto Error;
            }
            gAudioEffect[EFFECT_BALANCE] = new_effect;
            break;
        case EFFECT_TREBLEBASS:
            new_effect = create_audio_effect(name16[EFFECT_TREBLEBASS], EFFECT_TREBLEBASS);
            if (new_effect == NULL) {
                LOG("create TrebleBass effect failed\n");
                goto Error;
            }
            //------------set TrebleBass parameters------------------------------------
            if (TrebleBass_effect_func(new_effect, gParamIndex, gParamValue) < 0) {
                LOG("TrebleBass Test failed\n");
                goto Error;
            }
            gAudioEffect[EFFECT_TREBLEBASS] = new_effect;
            break;
        case EFFECT_HPEQ:
            new_effect =  create_audio_effect(name16[EFFECT_HPEQ], EFFECT_HPEQ);
            if (new_effect == NULL) {
                LOG("create Hpeq effect failed\n");
                goto Error;
            }
            //------------set HPEQ parameters------------------------------------------
            if (HPEQ_effect_func(new_effect, gParamIndex, gParamValue, gParamBand) < 0) {
                LOG("HPEQ Test failed\n");
                goto Error;
            }
            gAudioEffect[EFFECT_HPEQ] = new_effect;
            break;
         case EFFECT_GEQ:
            new_effect = create_audio_effect(name16[EFFECT_GEQ], EFFECT_GEQ);
            if (new_effect == NULL) {
                 LOG("create Geq effect failed\n");
                  goto Error;
            }
            //------------set GEQ parameters------------------------------------------
            if (GEQ_effect_func(new_effect, gParamIndex, gParamValue,gParamBands) < 0) {
                LOG("GEQ Test failed\n");
            }
            gAudioEffect[EFFECT_GEQ] = new_effect;
            break;
         case EFFECT_VIRTUALX:
            new_effect = create_audio_effect(name16[EFFECT_VIRTUALX], EFFECT_VIRTUALX);
            if (new_effect == NULL) {
                LOG("create Virtualx effect failed\n");
                goto Error;
            }
             gAudioEffect[EFFECT_VIRTUALX] = new_effect;
            //------------set Virtualx parameters-------------------------------------------
            if (Virtualx_effect_func(gAudioEffect[gEffectIndex], gParamIndex) < 0) {
                LOG("Virtualx Test failed\n");
            }
            break;
        default:
            break;
        }

Retry_input:
        LOG("Please enter param: <EffectIndex> <ParamIndex> <ParamValue/ParamScale>\n");

        /*sleep 500ms to wait new input*/
        usleep(500*1000);

        if (GetIntData(&gEffectIndex) < 0)
            goto Retry_input;

        //parse ParamIndex in VX ctrl file
        if (gEffectIndex != EFFECT_VIRTUALX) {
            if (GetIntData(&gParamIndex) < 0) {
                goto Retry_input;
            }
        }

        switch (gEffectIndex) {
        case EFFECT_BALANCE:
            //------------get Balance parameters---------------------------------------
            if (GetIntData(&gParamValue) < 0)
                goto Retry_input;
            LOG("EffectIndex:%d, ParamIndex:%d, Paramvalue:%d\n", gEffectIndex, gParamIndex, gParamValue);
            break;
        case EFFECT_TREBLEBASS:
            //------------get TrebleBass parameters------------------------------------
            if (GetIntData(&gParamValue) < 0)
                goto Retry_input;
            LOG("EffectIndex:%d, ParamIndex:%d, Paramvalue:%d\n", gEffectIndex, gParamIndex, gParamValue);
            break;
        case EFFECT_HPEQ:
            //------------get HPEQ parameters------------------------------------------
            if (gParamIndex == HPEQ_PARAM_EFFECT_CUSTOM) {
                for (int i = 0; i < 5; i++) {
                    int ParamBand = 0;
                    if (GetIntData(&ParamBand) < 0)
                        goto Retry_input;
                    gParamBand[i] = (char)ParamBand;
                    LOG("EffectIndex:%d, ParamIndex:%d, ParamBand:%d\n", gEffectIndex, gParamIndex, (int)gParamBand[i]);
                }
            } else {
                if (GetIntData(&gParamValue) < 0)
                    goto Retry_input;
                LOG("EffectIndex:%d, ParamIndex:%d, Paramvalue:%d\n", gEffectIndex, gParamIndex, gParamValue);
            }
            break;
        case EFFECT_GEQ:
            //------------get GEQ parameters------------------------------------------
            if (gParamIndex == GEQ_PARAM_EFFECT_CUSTOM) {
                for (int i = 0; i < 9; i++) {
                    int ParamBand = 0;
                    if (GetIntData(&ParamBand) < 0)
                        goto Retry_input;
                    gParamBand[i] = (char)ParamBand;
                    LOG("EffectIndex:%d, ParamIndex:%d, ParamBand:%d\n", gEffectIndex, gParamIndex, (int)gParamBands[i]);
                }
            } else {
                if (GetIntData(&gParamValue) < 0)
                    goto Retry_input;
                LOG("EffectIndex:%d, ParamIndex:%d, Paramvalue:%d\n", gEffectIndex, gParamIndex, gParamValue);
            }
            break;
        case EFFECT_VIRTUALX:
            //------------get Virtualx parameters--------------------------------------
            ret = set_param_from_scanf(gAudioEffect[gEffectIndex], EFFECT_VIRTUALX);
            if (ret < 0) {
                goto Retry_input;
            }
            gParamIndex = ret;
            break;
        default:
            LOG("EffectIndex = %d is invalid\n", gEffectIndex);
            break;
        }
    }

    ret = 0;
Error:
    for (i = 0; i < EFFECT_MAX; i++) {
        if (gAudioEffect[i] != NULL) {
            //delete gAudioEffect[i];
            gAudioEffect[i]->setEnabled(false);
            gAudioEffect[i] = NULL;
        }
    }
    return ret;
}

}
