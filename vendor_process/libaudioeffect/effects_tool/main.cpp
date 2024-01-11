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

#include "Virtualx.h"

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

//-------------Virtualx parameter--------------------------
typedef struct Virtualx_param_s {
    effect_param_t param;
    uint32_t command;
    union {
        int32_t v;
        float f;
        float params[VX_MAX_PARAM_SIZE];
    };
} Virtualx_param_t;

Virtualx_param_t gVirtualxParam[] = {
    {{0, 4, 4},     PARAM_MBHL_ENABLE_I32,                     {1}    },
    {{0, 4, 4},     PARAM_MBHL_BYPASS_GAIN_I32,                {1}    },
    {{0, 4, 4},     PARAM_MBHL_REFERENCE_LEVEL_I32,            {1}    },
    {{0, 4, 4},     PARAM_MBHL_VOLUME_I32,                     {1}    },
    {{0, 4, 4},     PARAM_MBHL_VOLUME_STEP_I32,                {100}  },
    {{0, 4, 4},     PARAM_MBHL_BALANCE_STEP_I32,               {0}    },
    {{0, 4, 4},     PARAM_MBHL_OUTPUT_GAIN_I32,                {1}    },
    {{0, 4, 4},     PARAM_MBHL_MODE_I32,                       {0}    },
    {{0, 4, 4},     PARAM_MBHL_PROCESS_DISCARD_I32,            {0}    },
    {{0, 4, 4},     PARAM_MBHL_CROSS_LOW_I32,                  {7}    },
    {{0, 4, 4},     PARAM_MBHL_CROSS_MID_I32,                  {15}   },
    {{0, 4, 4},     PARAM_MBHL_COMP_ATTACK_I32,                {5}    },
    {{0, 4, 4},     PARAM_MBHL_COMP_LOW_RELEASE_I32,           {250}  },
    {{0, 4, 4},     PARAM_MBHL_COMP_LOW_RATIO_I32,             {4}    },
    {{0, 4, 4},     PARAM_MBHL_COMP_LOW_THRESH_I32,            {0}    },
    {{0, 4, 4},     PARAM_MBHL_COMP_LOW_MAKEUP_I32,            {1}    },
    {{0, 4, 4},     PARAM_MBHL_COMP_MID_RELEASE_I32,           {250}  },
    {{0, 4, 4},     PARAM_MBHL_COMP_MID_RATIO_I32,             {4}    },
    {{0, 4, 4},     PARAM_MBHL_COMP_MID_THRESH_I32,            {0}    },
    {{0, 4, 4},     PARAM_MBHL_COMP_MID_MAKEUP_I32,            {1}    },
    {{0, 4, 4},     PARAM_MBHL_COMP_HIGH_RELEASE_I32,          {250}  },
    {{0, 4, 4},     PARAM_MBHL_COMP_HIGH_RATIO_I32,            {4}    },
    {{0, 4, 4},     PARAM_MBHL_COMP_HIGH_THRESH_I32,           {0}    },
    {{0, 4, 4},     PARAM_MBHL_COMP_HIGH_MAKEUP_I32,           {1}    },
    {{0, 4, 4},     PARAM_MBHL_BOOST_I32,                      {1}    },
    {{0, 4, 4},     PARAM_MBHL_THRESHOLD_I32,                  {1}    },
    {{0, 4, 4},     PARAM_MBHL_SLOW_OFFSET_I32,                {1}    },
    {{0, 4, 4},     PARAM_MBHL_FAST_ATTACK_I32,                {5}    },
    {{0, 4, 4},     PARAM_MBHL_FAST_RELEASE_I32,               {50}   },
    {{0, 4, 4},     PARAM_MBHL_SLOW_ATTACK_I32,                {500}  },
    {{0, 4, 4},     PARAM_MBHL_SLOW_RELEASE_I32,               {500}  },
    {{0, 4, 4},     PARAM_MBHL_DELAY_I32,                      {8}    },
    {{0, 4, 4},     PARAM_MBHL_ENVELOPE_FREQUENCY_I32,         {20}   },
    {{0, 4, 4},     PARAM_MBHL_APP_FRT_LOWCROSS_F32,           {0}    },
    {{0, 4, 4},     PARAM_MBHL_APP_FRT_MIDCROSS_F32,           {0}    },
    {{0, 4, 4},     PARAM_TBHDX_ENABLE_I32,                    {0}    },
    {{0, 4, 4},     PARAM_TBHDX_MONO_MODE_I32,                 {0}    },
    {{0, 4, 4},     PARAM_TBHDX_MAXGAIN_I32,                   {0}    },
    {{0, 4, 4},     PARAM_TBHDX_SPKSIZE_I32,                   {2}    },
    {{0, 4, 4},     PARAM_TBHDX_HP_ENABLE_I32,                 {1}    },
    {{0, 4, 4},     PARAM_TBHDX_TEMP_GAIN_I32,                 {0}    },
    {{0, 4, 4},     PARAM_TBHDX_PROCESS_DISCARD_I32,           {1}    },
    {{0, 4, 4},     PARAM_TBHDX_HPORDER_I32,                   {4}    },
    {{0, 4, 4},     PARAM_TBHDX_APP_SPKSIZE_I32,               {0}    },
    {{0, 4, 4},     PARAM_TBHDX_APP_HPRATIO_F32,               {0}    },
    {{0, 4, 4},     PARAM_TBHDX_APP_EXTBASS_F32,               {0}    },
    {{0, 4, 4},     PARAM_VX_ENABLE_I32,                       {1}    },
    {{0, 4, 4},     PARAM_VX_INPUT_MODE_I32,                   {4}    },
    {{0, 4, 4},     PARAM_VX_OUTPUT_MODE_I32,                  {0}    },
    {{0, 4, 4},     PARAM_VX_HEADROOM_GAIN_I32,                {1}    },
    {{0, 4, 4},     PARAM_VX_PROC_OUTPUT_GAIN_I32,             {1}    },
    {{0, 4, 4},     PARAM_VX_REFERENCE_LEVEL_I32,              {0}    },
    {{0, 4, 4},     PARAM_TSX_ENABLE_I32,                      {1}    },
    {{0, 4, 4},     PARAM_TSX_PASSIVEMATRIXUPMIX_ENABLE_I32,   {1}    },
    {{0, 4, 4},     PARAM_TSX_HEIGHT_UPMIX_ENABLE_I32,         {1}    },
    {{0, 4, 4},     PARAM_TSX_LPR_GAIN_I32,                    {1}    },
    {{0, 4, 4},     PARAM_TSX_CENTER_GAIN_I32,                 {1}    },
    {{0, 4, 4},     PARAM_TSX_HORIZ_VIR_EFF_CTRL_I32,          {0}    },
    {{0, 4, 4},     PARAM_TSX_HEIGHTMIX_COEFF_I32,             {1}    },
    {{0, 4, 4},     PARAM_TSX_PROCESS_DISCARD_I32,             {0}    },
    {{0, 4, 4},     PARAM_TSX_HEIGHT_DISCARD_I32,              {0}    },
    {{0, 4, 4},     PARAM_TSX_FRNT_CTRL_I32,                   {1}    },
    {{0, 4, 4},     PARAM_TSX_SRND_CTRL_I32,                   {1}    },
    {{0, 4, 4},     PARAM_VX_DC_ENABLE_I32,                    {0}    },
    {{0, 4, 4},     PARAM_VX_DC_CONTROL_I32,                   {0}    },
    {{0, 4, 4},     PARAM_VX_DEF_ENABLE_I32,                   {0}    },
    {{0, 4, 4},     PARAM_VX_DEF_CONTROL_I32,                  {0}    },
    {{0, 4, 4},     PARAM_LOUDNESS_CONTROL_ENABLE_I32,         {1}    },
    {{0, 4, 4},     PARAM_LOUDNESS_CONTROL_TARGET_LOUDNESS_I32,{-24}  },
    {{0, 4, 4},     PARAM_LOUDNESS_CONTROL_PRESET_I32,         {0}    },
    {{0, 4, 4},     PARAM_LOUDNESS_CONTROL_IO_MODE_I32,        {0}    },
    {{0, 4, 4},     PARAM_AEQ_ENABLE_I32,                      {0}    },
    {{0, 4, 4},     PARAM_AEQ_DISCARD_I32,                     {0}    },
    {{0, 4, 4},     PARAM_AEQ_INPUT_GAIN_I16,                  {1}    },
    {{0, 4, 4},     PARAM_AEQ_OUTPUT_GAIN_I16,                 {1}    },
    {{0, 4, 4},     PARAM_AEQ_BYPASS_GAIN_I16,                 {1}    },
    {{0, 4, 4},     PARAM_AEQ_LR_LINK_I32,                     {1}    },
    {{0, 4, 20},    PARAM_AEQ_BAND_Fre,                        {1}    }, // band is 5 just for example
    {{0, 4, 20},    PARAM_AEQ_BAND_Gain,                       {1}    },
    {{0, 4, 20},    PARAM_AEQ_BAND_Q,                          {1}    },
    {{0, 4, 20},    PARAM_AEQ_BAND_type,                       {1}    },
    {{0, 4, 4},     PARAM_CHANNEL_NUM,                         {1}    },
    {{0, 4, 4},     VIRTUALX_PARAM_ENABLE,                         {1}    },
    {{0, 4, 4},     VIRTUALX_PARAM_DIALOGCLARITY_MODE,             {1}    },
    {{0, 4, 4},     VIRTUALX_PARAM_SURROUND_MODE,                  {1}    },
    {{0, 4, 4},     AUDIO_PARAM_TYPE_NONE,                     {1}    },
    {{0, 4, 32},    AUDIO_PARAM_TYPE_TRU_SURROUND,             {1}    },
    {{0, 4, 32},    AUDIO_PARAM_TYPE_CC3D,                     {1}    },
    {{0, 4, 40},    AUDIO_PARAM_TYPE_TRU_BASS,                 {1}    },
    {{0, 4, 32},    AUDIO_PARAM_TYPE_TRU_DIALOG,               {1}    },
    {{0, 4, 24},    AUDIO_PARAM_TYPE_DEFINITION,               {1}    },
    {{0, 4, 12},    AUDIO_PARAM_TYPE_TRU_VOLUME,               {1}    },
    {{0, 4, 4},     AUDIO_ALL_PARAM_DUMP,                      {1}    },
};

const char *VXStatusstr[] = {"Disable", "Enable"};

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

static int Virtualx_effect_func(sp<AudioEffect>& gAudioEffect, int gParamIndex, int gParamValue,float gParamScale,float gParaRange[VX_MAX_PARAM_SIZE])
{
    int rc = 0;
    switch (gParamIndex) {
        case VIRTUALX_PARAM_ENABLE:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: Status gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            if (!gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param))
                LOG("Virtualx: Successful\n");
            else
                LOG("Virtualx: Failed\n");
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("Virtualx: Status is %d -> %s, 16bit normalized data ----> 0x%08X\n",
                gParamValue, VXStatusstr[gVirtualxParam[gParamIndex].v], FLOAT2INT(gParamValue));
            return 0;
       case PARAM_MBHL_ENABLE_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: MBHL ENABLE gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl enable is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_BYPASS_GAIN_I32:
            if (gParamScale < 0 || gParamScale > 1.0) {
                LOG("Vritualx: mbhl bypass gain gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl bypassgain is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_REFERENCE_LEVEL_I32:
            if (gParamScale < 0.0009 || gParamScale > 1.0) {
                LOG("Vritualx: mbhl reference level gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl reference level is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_VOLUME_I32:
            if (gParamScale < 0 || gParamScale > 1.0) {
                LOG("Vritualx: mbhl volume gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl volume is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_VOLUME_STEP_I32:
            if (gParamValue < 0 || gParamValue > 100) {
                LOG("Vritualx: mbhl volumestep gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl volume step is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_BALANCE_STEP_I32:
            if (gParamValue < -10 || gParamValue > 10) {
                LOG("Vritualx: mbhl banlance step gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl balance step is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_OUTPUT_GAIN_I32:
            if (gParamScale < 0 || gParamScale > 1.0) {
                LOG("Vritualx: mbhl output gain gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            // gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl output gain is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_BOOST_I32:
            if (gParamScale < 0.001 || gParamScale > 1000) {
                LOG("Vritualx: mbhl boost  gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl boost is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_THRESHOLD_I32:
            if (gParamScale < 0.064 || gParamScale > 1.0) {
                LOG("Vritualx: mbhl threshold  gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl threshold is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_SLOW_OFFSET_I32:
            if (gParamScale < 0.3170 || gParamScale > 3.1619) {
                LOG("Vritualx: mbhl slow offset  gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl slow offset is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_FAST_ATTACK_I32:
            if (gParamScale < 0 || gParamScale > 10) {
                LOG("Vritualx: mbhl fast attack  gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl fast attack is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_FAST_RELEASE_I32:
            if (gParamValue < 10 || gParamValue > 500) {
                LOG("Vritualx: mbhl fast release gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl fast release is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_SLOW_ATTACK_I32:
            if (gParamValue < 100 || gParamValue > 1000) {
                LOG("Vritualx: mbhl slow attack gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl slow attack is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_SLOW_RELEASE_I32:
            if (gParamValue < 100 || gParamValue > 2000) {
                LOG("Vritualx: mbhl slow release gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl slow release is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_DELAY_I32:
            if (gParamValue < 1 || gParamValue > 16) {
                LOG("Vritualx: mbhl delay gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl delay is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_ENVELOPE_FREQUENCY_I32:
            if (gParamValue < 5 || gParamValue > 500) {
                LOG("Vritualx: mbhl envelope freq gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl envelope freq is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_MODE_I32:
            if (gParamValue < 0 || gParamValue > 4) {
                LOG("Vritualx: mbhl mode gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl mode is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_PROCESS_DISCARD_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: mbhl process discard gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl process discard is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_CROSS_LOW_I32:
            if (gParamValue < 0 || gParamValue > 20) {
                LOG("Vritualx: mbhl cross low gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl cross low  is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_CROSS_MID_I32:
            if (gParamValue < 0 || gParamValue > 20) {
                LOG("Vritualx: mbhl cross mid gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl cross mid is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_COMP_ATTACK_I32:
            if (gParamValue < 0 || gParamValue > 100) {
                LOG("Vritualx: mbhl compressor attack time gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor attack time is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_COMP_LOW_RELEASE_I32:
            if (gParamValue < 50 || gParamValue > 2000) {
                LOG("Vritualx: mbhl compressor low release time gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor low release time is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_COMP_LOW_RATIO_I32:
            if (gParamScale < 1.0 || gParamScale > 20.0) {
                LOG("Vritualx: mbhl compressor low ratio gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor low ratio is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_COMP_LOW_THRESH_I32:
            if (gParamScale < 0.0640 || gParamScale > 15.8479) {
                LOG("Vritualx: mbhl compressor low threshold gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor low threshold is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_COMP_LOW_MAKEUP_I32:
            if (gParamScale < 0.0640 || gParamScale > 15.8479) {
                LOG("Vritualx: mbhl compressor low makeup gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor low makeup is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_COMP_MID_RELEASE_I32:
            if (gParamValue < 50 || gParamValue > 2000) {
                LOG("Vritualx: mbhl compressor mid release time gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor mid release time is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_COMP_MID_RATIO_I32:
             if (gParamScale < 1.0 || gParamScale > 20.0) {
                LOG("Vritualx: mbhl compressor mid ratio gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor mid ratio is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_COMP_MID_THRESH_I32:
            if (gParamScale < 0.0640 || gParamScale > 15.8479) {
                LOG("Vritualx: mbhl compressor mid threshold gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor mid threshold is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_COMP_MID_MAKEUP_I32:
             if (gParamScale < 0.0640 || gParamScale > 15.8479) {
                LOG("Vritualx: mbhl compressor mid makeup gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor mid makeup is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_COMP_HIGH_RELEASE_I32:
            if (gParamValue < 50 || gParamValue > 2000) {
                LOG("Vritualx: mbhl compressor high release time gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor high release time is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_MBHL_COMP_HIGH_RATIO_I32:
            if (gParamScale < 1.0 || gParamScale > 20.0) {
                LOG("Vritualx: mbhl compressor high ratio gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor high ratio is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_COMP_HIGH_THRESH_I32:
            if (gParamScale < 0.0640 || gParamScale > 15.8479) {
                LOG("Vritualx: mbhl compressor high threshold gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor high threshold is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_COMP_HIGH_MAKEUP_I32:
             if (gParamScale < 0.0640 || gParamScale > 15.8479) {
                LOG("Vritualx: mbhl compressor high makeup gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("mbhl compressor high makeup is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_TBHDX_ENABLE_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: tbhdx enable gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tbhdx enable is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_TBHDX_MONO_MODE_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: tbhdx mono mode gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tbhdx mono mode is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_TBHDX_SPKSIZE_I32:
            if (gParamValue < 0 || gParamValue > 12) {
                LOG("Vritualx: tbhdx spksize gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tbhdx spksize is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_TBHDX_TEMP_GAIN_I32:
            if (gParamScale < 0.0 || gParamScale > 1.0) {
                LOG("Vritualx: tbhdx temp gain gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tbhdx temp gain is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_TBHDX_MAXGAIN_I32:
            if (gParamScale < 0.0 || gParamScale > 1.0) {
                LOG("Vritualx: tbhdx max gain gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tbhdx max gain is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_TBHDX_PROCESS_DISCARD_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: tbhdx process discard gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
           //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tbhdx process discard is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_TBHDX_HPORDER_I32:
            if (gParamValue < 0 || gParamValue > 8) {
                LOG("Vritualx: tbhdx high pass filter order gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tbhdx high pass filter order is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_TBHDX_HP_ENABLE_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: tbhdx high pass enable gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tbhdx high pass enable is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_VX_ENABLE_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: vxlib1 enable gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("vxlib1 enable is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_VX_INPUT_MODE_I32:
            if (gParamValue < 0 || gParamValue > 4) {
                LOG("Vritualx: vxlib1 input mode gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("vxlib1 input mode is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_VX_HEADROOM_GAIN_I32:
            if (gParamScale < 0.1250 || gParamScale > 1.0) {
                LOG("Vritualx: vxlib1 headroom gain gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("vxlib1 headroom gain is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_VX_PROC_OUTPUT_GAIN_I32:
            if (gParamScale < 0.5 || gParamScale > 4.0) {
                LOG("Vritualx: vxlib1 output gain gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("vxlib1 output gain is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_TSX_ENABLE_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: tsx enable gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("vxlib1 tsx enable is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_TSX_PASSIVEMATRIXUPMIX_ENABLE_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: tsx passive matrix upmixer enable gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("vxlib1 tsx passive matrix upmixer enable is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_TSX_HORIZ_VIR_EFF_CTRL_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: tsx horizontal Effect gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("vxlib1 tsx horizontal Effect is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_TSX_FRNT_CTRL_I32:
            if (gParamScale < 0.5 || gParamScale > 2.0) {
                LOG("Vritualx: tsx frnt ctrl gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tsx frnt ctrl is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_TSX_SRND_CTRL_I32:
            if (gParamScale < 0.5 || gParamScale > 2.0) {
                LOG("Vritualx: tsx srnd ctrl gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tsx srnd ctrl is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_TSX_LPR_GAIN_I32:
             if (gParamScale < 0.0 || gParamScale > 2.0) {
                LOG("Vritualx: tsx lprtoctr mix gain gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tsx lprtoctr mix gain is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_TSX_HEIGHTMIX_COEFF_I32:
             if (gParamScale < 0.5 || gParamScale > 2.0) {
                LOG("Vritualx: tsx heightmix coeff gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tsx heightmix coeff is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_TSX_CENTER_GAIN_I32:
             if (gParamScale < 1.0 || gParamScale > 2.0) {
                LOG("Vritualx: tsx center gain gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tsx center gain is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_TSX_HEIGHT_DISCARD_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: tsx height discard gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tsx height discard is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_TSX_PROCESS_DISCARD_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: tsx process discard gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tsx process discard is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_TSX_HEIGHT_UPMIX_ENABLE_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: tsx height upmix enable gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tsx height upmix enable is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_VX_DC_ENABLE_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: tsx dc enable gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tsx dc enable is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_VX_DC_CONTROL_I32:
             if (gParamScale < 0.0 || gParamScale > 1.0) {
                LOG("Vritualx: tsx dc level gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tsx dc level is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_VX_DEF_ENABLE_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: tsx def enable gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tsx def enable is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_VX_DEF_CONTROL_I32:
             if (gParamScale < 0.0 || gParamScale > 1.0) {
                LOG("Vritualx: tsx def level gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("tsx def level is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_LOUDNESS_CONTROL_ENABLE_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("loudness control = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("loudness control is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_LOUDNESS_CONTROL_TARGET_LOUDNESS_I32:
            if (gParamValue < -40 || gParamValue > 0) {
                LOG("loudness control target = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("loudness control target is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_LOUDNESS_CONTROL_PRESET_I32:
            if (gParamValue < 0 || gParamValue > 2) {
                LOG("loudness control preset = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            //gAudioEffect->getParameter(&gVirtualxParam[gParamIndex].param);
            LOG("loudness control preset is %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_TBHDX_APP_SPKSIZE_I32:
            if (gParamValue < 40 || gParamValue > 600) {
                LOG("app spksize = %d invalid\n",gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("app spksize %d, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].v, FLOAT2INT(gVirtualxParam[gParamIndex].v));
            return 0;
        case PARAM_TBHDX_APP_HPRATIO_F32:
            if (gParamScale < 0.0 || gParamScale > 1.0) {
                LOG("app hpratio = %f invalid\n",gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("app hpratio is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_TBHDX_APP_EXTBASS_F32:
            if (gParamScale < 0.0 || gParamScale > 1.0) {
                LOG("app extbass = %f invalid\n",gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("app extbass is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_APP_FRT_LOWCROSS_F32:
            if (gParamScale < 40 || gParamScale > 8000.0) {
                LOG("app low freq = %f invalid\n",gParamScale);
                return -1;
            }
             gVirtualxParam[gParamIndex].f = gParamScale;
            rc =gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("rc is %d\n",rc);
            LOG("app low freq is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_MBHL_APP_FRT_MIDCROSS_F32:
            if (gParamScale < 40.0 || gParamScale > 8000.0) {
                LOG("app mid freq = %f invalid\n",gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("app mid freq is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case AUDIO_PARAM_TYPE_TRU_SURROUND:
            for (int i = 0; i < 8; i++) {
                gVirtualxParam[gParamIndex].params[i] = gParaRange[i];
                LOG("gVirtualxParam[gParamIndex].params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, gParaRange[i], FLOAT2INT(gParaRange[i]));
            }
            rc = gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("rc is %d\n",rc);
            return 0;
        case AUDIO_PARAM_TYPE_CC3D:
            for (int i = 0; i < 8; i++) {
                gVirtualxParam[gParamIndex].params[i] = gParaRange[i];
                LOG("gVirtualxParam[gParamIndex].params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, gParaRange[i], FLOAT2INT(gParaRange[i]));
            }
            rc = gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("rc is %d\n",rc);
            return 0;
        case AUDIO_PARAM_TYPE_TRU_BASS:
            for (int i = 0; i < 10; i++) {
                gVirtualxParam[gParamIndex].params[i] = gParaRange[i];
                LOG("gVirtualxParam[gParamIndex].params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, gParaRange[i], FLOAT2INT(gParaRange[i]));
            }
            rc = gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("rc is %d\n",rc);
            return 0;
        case AUDIO_PARAM_TYPE_TRU_DIALOG:
            for (int i = 0; i < 8; i++) {
                gVirtualxParam[gParamIndex].params[i] = gParaRange[i];
                LOG("gVirtualxParam[gParamIndex].params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, gParaRange[i], FLOAT2INT(gParaRange[i]));
            }
            rc = gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("rc is %d\n",rc);
            return 0;
        case AUDIO_PARAM_TYPE_DEFINITION:
            for (int i = 0; i < 6; i++) {
                gVirtualxParam[gParamIndex].params[i] = gParaRange[i];
                LOG("gVirtualxParam[gParamIndex].params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, gParaRange[i], FLOAT2INT(gParaRange[i]));
            }
            rc = gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("rc is %d\n",rc);
            return 0;
        case AUDIO_PARAM_TYPE_TRU_VOLUME:
            for (int i = 0; i < 3; i++) {
                gVirtualxParam[gParamIndex].params[i] = gParaRange[i];
                LOG("gVirtualxParam[gParamIndex].params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, gParaRange[i], FLOAT2INT(gParaRange[i]));
            }
            rc = gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("rc is %d\n",rc);
            return 0;
        case PARAM_AEQ_ENABLE_I32:
             if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: aeq  enable gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("aeq enable is %d\n",gVirtualxParam[gParamIndex].v);
            return 0;
        case PARAM_AEQ_DISCARD_I32:
             if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: aeq  discard gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("aeq discard is %d\n",gVirtualxParam[gParamIndex].v);
            return 0;
        case PARAM_AEQ_INPUT_GAIN_I16:
            if (gParamScale < 0 || gParamScale > 1.0) {
                LOG("Vritualx: aeq input gain gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("aeq input gain is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_AEQ_OUTPUT_GAIN_I16:
            if (gParamScale < 0 || gParamScale > 1.0) {
                LOG("Vritualx: aeq output gain gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("aeq output gain is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_AEQ_BYPASS_GAIN_I16:
            if (gParamScale < 0 || gParamScale > 1.0) {
                LOG("Vritualx: aeq bypass gain gParamValue = %f invalid\n", gParamScale);
                return -1;
            }
            gVirtualxParam[gParamIndex].f = gParamScale;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("aeq bypass gain is %f, 16bit normalized data ----> 0x%08X\n",
                gVirtualxParam[gParamIndex].f, FLOAT2INT(gVirtualxParam[gParamIndex].f));
            return 0;
        case PARAM_AEQ_LR_LINK_I32:
            if (gParamValue < 0 || gParamValue > 1) {
                LOG("Vritualx: aeq lr link gParamValue = %d invalid\n", gParamValue);
                return -1;
            }
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("aeq lr link flag is %d\n",gVirtualxParam[gParamIndex].v);
            return 0;
        case PARAM_AEQ_BAND_Fre:
            for (int i = 0; i < 5; i++) {
                gVirtualxParam[gParamIndex].params[i] = gParaRange[i];
                LOG("gVirtualxParam[gParamIndex].params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, gParaRange[i], FLOAT2INT(gParaRange[i]));
            }
            rc = gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("rc is %d\n",rc);
            return 0;
        case PARAM_AEQ_BAND_Gain:
            for (int i = 0; i < 5; i++) {
                gVirtualxParam[gParamIndex].params[i] = gParaRange[i];
                LOG("gVirtualxParam[gParamIndex].params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, gParaRange[i], FLOAT2INT(gParaRange[i]));
            }
            rc = gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("rc is %d\n",rc);
            return 0;
        case PARAM_AEQ_BAND_Q:
            for (int i = 0; i < 5; i++) {
                gVirtualxParam[gParamIndex].params[i] = gParaRange[i];
                LOG("gVirtualxParam[gParamIndex].params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, gParaRange[i], FLOAT2INT(gParaRange[i]));
            }
            rc = gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("rc is %d\n",rc);
            return 0;
        case PARAM_AEQ_BAND_type:
            for (int i = 0; i < 5; i++) {
                gVirtualxParam[gParamIndex].params[i] = gParaRange[i];
                LOG("gVirtualxParam[gParamIndex].params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, gParaRange[i], FLOAT2INT(gParaRange[i]));
            }
            rc = gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("rc is %d\n",rc);
            return 0;
        case PARAM_CHANNEL_NUM:
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("set dts channel num is %d\n",gVirtualxParam[gParamIndex].v);
            return 0;
        case AUDIO_ALL_PARAM_DUMP:
            gVirtualxParam[gParamIndex].v = gParamValue;
            gAudioEffect->setParameter(&gVirtualxParam[gParamIndex].param);
            LOG("get all VX parameters %d\n",gVirtualxParam[gParamIndex].v);
            return 0;
    default:
        LOG("Virtualx: ParamIndex = %d invalid\n", gParamIndex);
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
    } else if (gEffectIndex == EFFECT_VIRTUALX) {
        LOG("*********************************Virtualx***********************************\n");
        LOG("VirtualX EffectIndex: %d\n", (int)EFFECT_VIRTUALX);
        LOG("Usage: %s %d <ParamIndex> <ParamValue/ParamScale/gParamBand>\n", name, (int)EFFECT_VIRTUALX);
        LOG("------------Multi band hard limiter----ParamIndex(from %d to %d)-----\n",
            (int)PARAM_MBHL_ENABLE_I32, (int)PARAM_MBHL_APP_FRT_MIDCROSS_F32);
        LOG("ParamIndex: %d -> Mbhl Enable\n", (int)PARAM_MBHL_ENABLE_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> Mbhl Bypass Gain\n", (int)PARAM_MBHL_BYPASS_GAIN_I32);
        LOG("ParamScale: 0.0 ~ 1.0\n");
        LOG("ParamIndex: %d -> Mbhl Reference Level\n", (int)PARAM_MBHL_REFERENCE_LEVEL_I32);
        LOG("ParamScale: 0.0009 ~ 1.0\n");
        LOG("ParamIndex: %d -> Mbhl Volume\n", (int)PARAM_MBHL_VOLUME_I32);
        LOG("ParamScale: 0.0 ~ 1.0\n");
        LOG("ParamIndex: %d -> Mbhl Volume Step\n", (int)PARAM_MBHL_VOLUME_STEP_I32);
        LOG("ParamValue: 0 ~ 100\n");
        LOG("ParamIndex: %d -> Mbhl Balance Step\n", (int)PARAM_MBHL_BALANCE_STEP_I32);
        LOG("ParamValue: -10 ~ 10\n");
        LOG("ParamIndex: %d -> Mbhl Output Gain\n", (int)PARAM_MBHL_OUTPUT_GAIN_I32);
        LOG("ParamScale: 0.0 ~ 1.0\n");
        LOG("ParamIndex: %d -> Mbhl Mode\n", (int)PARAM_MBHL_MODE_I32);
        LOG("ParamValue: 0 ~ 4\n");
        LOG("ParamIndex: %d -> Mbhl process Discard\n", (int)PARAM_MBHL_PROCESS_DISCARD_I32);
        LOG("ParamValue: 0 ~ 1\n");
        LOG("ParamIndex: %d -> Mbhl Cross Low\n", (int)PARAM_MBHL_CROSS_LOW_I32);
        LOG("ParamValue: 0 ~ 20\n");
        LOG("ParamIndex: %d -> Mbhl Cross Mid\n", (int)PARAM_MBHL_CROSS_MID_I32);
        LOG("ParamValue: 0 ~ 20\n");
        LOG("ParamIndex: %d -> Mbhl Comp Attack\n", (int)PARAM_MBHL_COMP_ATTACK_I32);
        LOG("ParamValue: 0 ~ 100\n");
        LOG("ParamIndex: %d -> Mbhl Comp Low Release\n", (int)PARAM_MBHL_COMP_LOW_RELEASE_I32);
        LOG("ParamValue: 50 ~ 2000\n");
        LOG("ParamIndex: %d -> Mbhl Comp Low Ratio\n", (int)PARAM_MBHL_COMP_LOW_RATIO_I32);
        LOG("ParamScale: 1.0 ~ 20.0\n");
        LOG("ParamIndex: %d -> Mbhl Comp Low Thresh\n", (int)PARAM_MBHL_COMP_LOW_THRESH_I32);
        LOG("ParamScale: 0.0640 ~ 15.8479\n");
        LOG("ParamIndex: %d -> Mbhl Comp Low Makeup\n", (int)PARAM_MBHL_COMP_LOW_MAKEUP_I32);
        LOG("ParamScale: 0.0640 ~ 15.8479\n");
        LOG("ParamIndex: %d -> Mbhl Comp Mid Release\n", (int)PARAM_MBHL_COMP_MID_RELEASE_I32);
        LOG("ParamValue: 50 ~ 2000\n");
        LOG("ParamIndex: %d -> Mbhl Comp Mid Ratio\n", (int)PARAM_MBHL_COMP_MID_RATIO_I32);
        LOG("ParamScale: 1.0 ~ 20.0\n");
        LOG("ParamIndex: %d -> Mbhl Comp Mid Thresh\n", (int)PARAM_MBHL_COMP_MID_THRESH_I32);
        LOG("ParamScale: 0.0640 ~ 15.8479\n");
        LOG("ParamIndex: %d -> Mbhl Comp Mid Makeup\n", (int)PARAM_MBHL_COMP_MID_MAKEUP_I32);
        LOG("ParamScale: 0.0640 ~ 15.8479\n");
        LOG("ParamIndex: %d -> Mbhl Comp High Release\n", (int)PARAM_MBHL_COMP_HIGH_RELEASE_I32);
        LOG("ParamValue: 50 ~ 2000\n");
        LOG("ParamIndex: %d -> Mbhl Comp High Ratio\n", (int)PARAM_MBHL_COMP_HIGH_RATIO_I32);
        LOG("ParamScale: 1.0 ~ 20.0\n");
        LOG("ParamIndex: %d -> Mbhl Comp High Thresh\n", (int)PARAM_MBHL_COMP_HIGH_THRESH_I32);
        LOG("ParamScale: 0.0640 ~ 15.8479\n");
        LOG("ParamIndex: %d -> Mbhl Comp High Makeup\n", (int)PARAM_MBHL_COMP_HIGH_MAKEUP_I32);
        LOG("ParamScale: 0.0640 ~ 15.8479\n");
        LOG("ParamIndex: %d -> Mbhl Boost\n", (int)PARAM_MBHL_BOOST_I32);
        LOG("ParamScale: 0.001 ~ 1000\n");
        LOG("ParamIndex: %d -> Mbhl Threshold\n", (int)PARAM_MBHL_THRESHOLD_I32);
        LOG("ParamScale: 0.0640 ~ 1.0\n");
        LOG("ParamIndex: %d -> Mbhl Slow Offset\n", (int)PARAM_MBHL_SLOW_OFFSET_I32);
        LOG("ParamScale: 0.317 ~ 3.1619\n");
        LOG("ParamIndex: %d -> Mbhl Fast Attack\n", (int)PARAM_MBHL_FAST_ATTACK_I32);
        LOG("ParamScale: 0 ~ 10\n");
        LOG("ParamIndex: %d -> Mbhl Fast Release\n", (int)PARAM_MBHL_FAST_RELEASE_I32);
        LOG("ParamValue: 10 ~ 500\n");
        LOG("ParamIndex: %d -> Mbhl Slow Attack\n", (int)PARAM_MBHL_SLOW_ATTACK_I32);
        LOG("ParamValue: 100 ~ 1000\n");
        LOG("ParamIndex: %d -> Mbhl Slow Release\n", (int)PARAM_MBHL_SLOW_RELEASE_I32);
        LOG("ParamValue: 100 ~ 2000\n");
        LOG("ParamIndex: %d -> Mbhl Delay\n", (int)PARAM_MBHL_DELAY_I32);
        LOG("ParamValue: 0 ~ 16\n");
        LOG("ParamIndex: %d -> Mbhl Envelope Freq\n", (int)PARAM_MBHL_ENVELOPE_FREQUENCY_I32);
        LOG("ParamValue: 5 ~ 500\n");
        LOG("ParamIndex: %d -> Mbhl frt lowcross\n", (int)PARAM_MBHL_APP_FRT_LOWCROSS_F32);
        LOG("ParamScale  40 ~ 8000\n");
        LOG("ParamIndex: %d -> Mbhl frt midcross\n", (int)PARAM_MBHL_APP_FRT_MIDCROSS_F32);
        LOG("ParamScale  40 ~ 8000\n");
        LOG("------------TruBassHDX-------ParamIndex(from %d to %d)--\n",
            (int)PARAM_TBHDX_ENABLE_I32, (int)PARAM_TBHDX_APP_EXTBASS_F32);
        LOG("ParamIndex: %d -> TBHDX Enable\n", (int)PARAM_TBHDX_ENABLE_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> TBHDX Mono Mode\n", (int)PARAM_TBHDX_MONO_MODE_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> TBHDX Max Gain\n", (int)PARAM_TBHDX_MAXGAIN_I32);
        LOG("ParamScale: 0.0 ~ 1.0\n");
        LOG("ParamIndex: %d -> TBHDX Spk Size\n", (int)PARAM_TBHDX_SPKSIZE_I32);
        LOG("ParamValue: 0 ~ 12\n");
        LOG("ParamIndex: %d -> TBHDX HP Enable\n", (int)PARAM_TBHDX_HP_ENABLE_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> TBHDX Temp Gain\n", (int)PARAM_TBHDX_TEMP_GAIN_I32);
        LOG("ParamScale: 0.0 ~ 1.0\n");
        LOG("ParamIndex: %d -> TBHDX  Process Discard\n", (int)PARAM_TBHDX_PROCESS_DISCARD_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> TBHDX HP Order\n", (int)PARAM_TBHDX_HPORDER_I32);
        LOG("ParamValue: 1 ~ 8\n");
        LOG("ParamIndex: %d -> TBHDX app spksize\n", (int)PARAM_TBHDX_APP_SPKSIZE_I32);
        LOG("ParamValue 40 ~ 600 \n");
        LOG("ParamIndex: %d -> TBHDX app hpratio\n", (int)PARAM_TBHDX_APP_HPRATIO_F32);
        LOG("ParamScale 0 ~ 1.0\n");
        LOG("ParamIndex: %d -> TBHDX app extbass\n", (int)PARAM_TBHDX_APP_EXTBASS_F32);
        LOG("ParamScale 0 ~ 1.0\n");
        LOG("------------General Setting-------ParamIndex(from %d to %d)--\n",
            (int)PARAM_VX_ENABLE_I32, (int)PARAM_VX_REFERENCE_LEVEL_I32);
        LOG("ParamIndex: %d -> VX Enable\n", (int)PARAM_VX_ENABLE_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> VX Input Mode\n", (int)PARAM_VX_INPUT_MODE_I32);
        LOG("ParamValue: 0 ~ 4\n");
        LOG("ParamIndex: %d -> VX Output Mode\n", (int)PARAM_VX_OUTPUT_MODE_I32);
        LOG("ParamValue: Can't be set!\n");
        LOG("ParamIndex: %d -> VX Head Room Gain\n", (int)PARAM_VX_HEADROOM_GAIN_I32);
        LOG("ParamScale: 0.125 ~ 1.0\n");
        LOG("ParamIndex: %d -> VX Proc Output Gain\n", (int)PARAM_VX_PROC_OUTPUT_GAIN_I32);
        LOG("ParamScale: 0.5 ~ 4.0\n");
        LOG("ParamIndex: %d -> VX Reference level\n", (int)PARAM_VX_REFERENCE_LEVEL_I32);
        LOG("ParamValue: Can't be set!\n");
        LOG("------------TrusurroundX-------ParamIndex(from %d to %d)--\n",
            (int)PARAM_TSX_ENABLE_I32, (int)PARAM_TSX_SRND_CTRL_I32);
        LOG("ParamIndex: %d -> TSX Enable\n", (int)PARAM_TSX_ENABLE_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> TSX Passive Matrix Upmixer Enable\n", (int)PARAM_TSX_PASSIVEMATRIXUPMIX_ENABLE_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> TSX Height Upmixer Enable\n", (int)PARAM_TSX_HEIGHT_UPMIX_ENABLE_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> TSX Lpr Gain\n", (int)PARAM_TSX_LPR_GAIN_I32);
        LOG("ParamScale: 0.000 ~ 2.0\n");
        LOG("ParamIndex: %d -> TSX Center Gain\n", (int)PARAM_TSX_CENTER_GAIN_I32);
        LOG("ParamScale: 1.0 ~ 2.0\n");
        LOG("ParamIndex: %d -> TSX Horiz Vir Effect Ctrl\n", (int)PARAM_TSX_HORIZ_VIR_EFF_CTRL_I32);
        LOG("ParamValue: 0 -> default   1 -> mild\n");
        LOG("ParamIndex: %d -> TSX Height Mix Coeff\n", (int)PARAM_TSX_HEIGHTMIX_COEFF_I32);
        LOG("ParamScale: 0.5 ~ 2.0\n");
        LOG("ParamIndex: %d -> TSX Process Discard\n", (int)PARAM_TSX_PROCESS_DISCARD_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> TSX Height Discard\n", (int)PARAM_TSX_HEIGHT_DISCARD_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> TSX Frnt Ctrl\n", (int)PARAM_TSX_FRNT_CTRL_I32);
        LOG("ParamScale: 0.5 ~ 2.0\n");
        LOG("ParamIndex: %d -> TSX Srnd Ctrl\n", (int)PARAM_TSX_SRND_CTRL_I32);
        LOG("ParamScale: 0.5 ~ 2.0\n");
        LOG("------------Dialog Clarty-------ParamIndex(from %d to %d)--\n",
            (int)PARAM_VX_DC_ENABLE_I32, (int)PARAM_VX_DC_CONTROL_I32);
        LOG("ParamIndex: %d -> VX DC Enable\n", (int)PARAM_VX_DC_ENABLE_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> VX DC Control\n", (int)PARAM_VX_DC_CONTROL_I32);
        LOG("ParamScale: 0.0 ~ 1.0\n");
        LOG("------------Definition-------ParamIndex(from %d to %d)--\n",
            (int)PARAM_VX_DEF_ENABLE_I32, (int)PARAM_VX_DEF_CONTROL_I32);
        LOG("ParamIndex: %d -> VX DEF Enable\n", (int)PARAM_VX_DEF_ENABLE_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> VX DEF Control\n", (int)PARAM_VX_DEF_CONTROL_I32);
        LOG("ParamScale: 0.0 ~ 1.0\n");
        LOG("------------TruVolume-------ParamIndex(from %d to %d)--\n",
            (int)PARAM_LOUDNESS_CONTROL_ENABLE_I32, (int)PARAM_LOUDNESS_CONTROL_IO_MODE_I32);
        LOG("ParamIndex: %d -> Loudness Control Enable\n", (int)PARAM_LOUDNESS_CONTROL_ENABLE_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> Loudness Control Target Loudness\n", (int)PARAM_LOUDNESS_CONTROL_TARGET_LOUDNESS_I32);
        LOG("ParamValue: -40 ~ 0\n");
        LOG("ParamIndex: %d -> Loudness Control Preset\n", (int)PARAM_LOUDNESS_CONTROL_PRESET_I32);
        LOG("ParamValue: 0 -> light 1 -> mid 2 -> Aggressive \n");
        LOG("ParamIndex: %d -> Loudness Control Mode\n", (int)PARAM_LOUDNESS_CONTROL_IO_MODE_I32);
        LOG("ParamValue: 0 ~ 4 \n");
        LOG("****************************************************************************\n\n");
        LOG("------------EQ-------ParamIndex(from %d to %d)--\n",
            (int)PARAM_AEQ_ENABLE_I32, (int)PARAM_AEQ_BAND_type);
        LOG("ParamIndex: %d -> AEQ  Enable\n", (int)PARAM_AEQ_ENABLE_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> AEQ  discard\n", (int)PARAM_AEQ_DISCARD_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> AEQ  input gain\n", (int)PARAM_AEQ_INPUT_GAIN_I16);
        LOG("ParamScale: 0.000 ~ 1.0\n");
        LOG("ParamIndex: %d -> AEQ  output gain\n", (int)PARAM_AEQ_OUTPUT_GAIN_I16);
        LOG("ParamScale: 0.000 ~ 1.0\n");
        LOG("ParamIndex: %d -> AEQ  bypass gain\n", (int)PARAM_AEQ_BYPASS_GAIN_I16);
        LOG("ParamScale: 0.000 ~ 1.0\n");
        LOG("ParamIndex: %d -> AEQ  LR Link\n", (int)PARAM_AEQ_LR_LINK_I32);
        LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
        LOG("ParamIndex: %d -> AEQ  fre\n", (int)PARAM_AEQ_BAND_Fre);
        LOG("ParamValue: 20 ~ 20000\n");
        LOG("ParamIndex: %d -> AEQ  band gain\n", (int)PARAM_AEQ_BAND_Gain);
        LOG("ParamValue: -12db ~ 12db\n");
        LOG("ParamIndex: %d -> AEQ  band Q\n", (int)PARAM_AEQ_BAND_Q);
        LOG("ParamValue: 0.25 ~ 16\n");
        LOG("ParamIndex: %d -> AEQ  band type\n", (int)PARAM_AEQ_BAND_type);
        LOG("ParamValue: 0:Traditional | 1:LowShelf | 2:High Shelf | 9:Null ]\n");
        LOG("ParamIndex: %d -> source channel num\n", (int)PARAM_CHANNEL_NUM);
        LOG("ParamValue: 2 | 6 \n");
        LOG("------------Debug interface------------\n");
        LOG("ParamIndex: %d -> Get all parameters from VX lib\n", (int)AUDIO_ALL_PARAM_DUMP);
        LOG("ParamValue: 0 \n");
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
    float gParamScale = 0.0f;
    signed char gParamBand[5] = {0};
    signed char gParamBands[9] = {0};
    float grange[VX_MAX_PARAM_SIZE] = {0};
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
    sscanf(argv[2], "%d", &gParamIndex);

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
        //------------get Virtualx parameters--------------------------------------
        if ((gParamIndex >= PARAM_MBHL_BYPASS_GAIN_I32 && gParamIndex <= PARAM_MBHL_VOLUME_I32)
            || gParamIndex == PARAM_MBHL_OUTPUT_GAIN_I32
            || (gParamIndex >= PARAM_MBHL_COMP_LOW_RATIO_I32 && gParamIndex <= PARAM_MBHL_COMP_LOW_MAKEUP_I32)
            || (gParamIndex >= PARAM_MBHL_COMP_MID_RATIO_I32 && gParamIndex <= PARAM_MBHL_COMP_MID_MAKEUP_I32)
            || (gParamIndex >= PARAM_MBHL_COMP_HIGH_RATIO_I32 && gParamIndex <= PARAM_MBHL_FAST_ATTACK_I32)
            || gParamIndex == PARAM_MBHL_APP_FRT_LOWCROSS_F32 || gParamIndex == PARAM_MBHL_APP_FRT_MIDCROSS_F32
            || gParamIndex == PARAM_TBHDX_MAXGAIN_I32
            || gParamIndex == PARAM_TBHDX_TEMP_GAIN_I32 || gParamIndex == PARAM_VX_HEADROOM_GAIN_I32
            || gParamIndex == PARAM_TBHDX_APP_HPRATIO_F32 || gParamIndex == PARAM_TBHDX_APP_EXTBASS_F32
            || gParamIndex == PARAM_VX_PROC_OUTPUT_GAIN_I32 || gParamIndex == PARAM_TSX_LPR_GAIN_I32
            || gParamIndex == PARAM_TSX_CENTER_GAIN_I32 || gParamIndex == PARAM_TSX_HEIGHTMIX_COEFF_I32
            || gParamIndex == PARAM_TSX_FRNT_CTRL_I32 || gParamIndex == PARAM_TSX_SRND_CTRL_I32
            || gParamIndex == PARAM_VX_DC_CONTROL_I32 || gParamIndex == PARAM_VX_DEF_CONTROL_I32
            || gParamIndex == PARAM_AEQ_INPUT_GAIN_I16 || gParamIndex == PARAM_AEQ_OUTPUT_GAIN_I16
            || gParamIndex == PARAM_AEQ_BYPASS_GAIN_I16)  {
            sscanf(argv[3], "%f", &gParamScale);
            LOG("EffectIndex:%d, ParamIndex:%d, ParamScale:%f\n", gEffectIndex, gParamIndex, gParamScale);
        } else if (gParamIndex == AUDIO_PARAM_TYPE_TRU_SURROUND || gParamIndex == AUDIO_PARAM_TYPE_CC3D ||
            gParamIndex == AUDIO_PARAM_TYPE_TRU_DIALOG) {
            for (int i = 0; i < 8; i++) {
                sscanf(argv[i + 3], "%f", &grange[i]);
                LOG("EffectIndex:%d, ParamIndex:%d, grange:%f\n", gEffectIndex, gParamIndex, grange[i]);
            }
        } else if (gParamIndex == AUDIO_PARAM_TYPE_TRU_BASS) {
            for (int i = 0; i < 10; i++) {
                sscanf(argv[i + 3], "%f", &grange[i]);
                LOG("EffectIndex:%d, ParamIndex:%d, grange:%f\n", gEffectIndex, gParamIndex, grange[i]);
            }
        } else if (gParamIndex == AUDIO_PARAM_TYPE_DEFINITION) {
            for (int i = 0; i < 6; i++) {
                sscanf(argv[i + 3], "%f", &grange[i]);
                LOG("EffectIndex:%d, ParamIndex:%d, grange:%f\n", gEffectIndex, gParamIndex, grange[i]);
            }
        } else if (gParamIndex == AUDIO_PARAM_TYPE_TRU_VOLUME) {
            for (int i = 0; i < 3; i++) {
                sscanf(argv[i + 3], "%f", &grange[i]);
                LOG("EffectIndex:%d, ParamIndex:%d, grange:%f\n", gEffectIndex, gParamIndex, grange[i]);
            }
        } else if (gParamIndex == PARAM_AEQ_BAND_Fre || gParamIndex == PARAM_AEQ_BAND_Gain ||
            gParamIndex == PARAM_AEQ_BAND_Q || gParamIndex == PARAM_AEQ_BAND_type) {
            for (int i = 0; i < 5; i++) {
                sscanf(argv[i + 3], "%f", &grange[i]);
                LOG("EffectIndex:%d, ParamIndex:%d, grange:%f\n", gEffectIndex, gParamIndex, grange[i]);
            }
        } else {
            sscanf(argv[3], "%d", &gParamValue);
            LOG("EffectIndex:%d, ParamIndex:%d, Paramvalue:%d\n", gEffectIndex, gParamIndex, gParamValue);
        }
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
            //------------set Virtualx parameters-------------------------------------------
            if (Virtualx_effect_func(new_effect, gParamIndex, gParamValue,gParamScale,grange) < 0) {
                LOG("Virtualx Test failed\n");
            }
            gAudioEffect[EFFECT_VIRTUALX] = new_effect;
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

        if (GetIntData(&gParamIndex) < 0)
            goto Retry_input;

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
            if ((gParamIndex >= PARAM_MBHL_BYPASS_GAIN_I32 && gParamIndex <= PARAM_MBHL_VOLUME_I32)
                || gParamIndex == PARAM_MBHL_OUTPUT_GAIN_I32
                || (gParamIndex >= PARAM_MBHL_COMP_LOW_RATIO_I32 && gParamIndex <= PARAM_MBHL_COMP_LOW_MAKEUP_I32)
                || (gParamIndex >= PARAM_MBHL_COMP_MID_RATIO_I32 && gParamIndex <= PARAM_MBHL_COMP_MID_MAKEUP_I32)
                || (gParamIndex >= PARAM_MBHL_COMP_HIGH_RATIO_I32 && gParamIndex <= PARAM_MBHL_FAST_ATTACK_I32)
                || gParamIndex == PARAM_MBHL_APP_FRT_LOWCROSS_F32 || gParamIndex == PARAM_MBHL_APP_FRT_MIDCROSS_F32
                || gParamIndex == PARAM_TBHDX_MAXGAIN_I32
                || gParamIndex == PARAM_TBHDX_TEMP_GAIN_I32 || gParamIndex == PARAM_VX_HEADROOM_GAIN_I32
                || gParamIndex == PARAM_TBHDX_APP_HPRATIO_F32 || gParamIndex == PARAM_TBHDX_APP_EXTBASS_F32
                || gParamIndex == PARAM_VX_PROC_OUTPUT_GAIN_I32 || gParamIndex == PARAM_TSX_LPR_GAIN_I32
                || gParamIndex == PARAM_TSX_CENTER_GAIN_I32 || gParamIndex == PARAM_TSX_HEIGHTMIX_COEFF_I32
                || gParamIndex == PARAM_TSX_FRNT_CTRL_I32 || gParamIndex == PARAM_TSX_SRND_CTRL_I32
                || gParamIndex == PARAM_VX_DC_CONTROL_I32 || gParamIndex == PARAM_VX_DEF_CONTROL_I32
                || gParamIndex == PARAM_AEQ_INPUT_GAIN_I16 || gParamIndex == PARAM_AEQ_OUTPUT_GAIN_I16
                || gParamIndex == PARAM_AEQ_BYPASS_GAIN_I16)  {
                if (GetFloatData(&gParamScale) < 0)
                    goto Retry_input;
                LOG("EffectIndex:%d, ParamIndex:%d, ParamScale:%f\n", gEffectIndex, gParamIndex, gParamScale);
            } else {
                if (GetIntData(&gParamValue) < 0)
                    goto Retry_input;
                LOG("EffectIndex:%d, ParamIndex:%d, Paramvalue:%d\n", gEffectIndex, gParamIndex, gParamValue);
            }
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
