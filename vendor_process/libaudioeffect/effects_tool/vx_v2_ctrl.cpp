
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <media/AudioEffect.h>

#include "Virtualx.h"
#include "vx_ctrl.h"

#define LOG                  printf
#define AM_ARRAY_SIZE(_a)    (sizeof(_a)/sizeof((_a)[0]))
#define BITS    16
#define MAXbit  (1 << (BITS - 1))
#define INT2FLOAT(a) ((float)a / MAXbit)
#define FLOAT2INT(a) (int)((float)a * MAXbit)

const char *VXStatusstr[] = {"Disable", "Enable"};

typedef struct Virtualx_param_s {
    effect_param_t param;
    /* 3 (void *) data of param start*/
    /* 3.1 psize of low level param */
    uint32_t command;
    /* 3.2 vsize of low level value */
    union {
        int32_t v;
        float f;
        float params[VX_MAX_PARAM_SIZE];
    };
} Virtualx_param_t;

Virtualx_param_t VxParamTable[] = {
    {{0, 4, 4},     PARAM_MBHL_ENABLE_I32,                     {1}    },
    {{0, 4, 8},     PARAM_MBHL_BYPASS_GAIN_I32,                {1}    },
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
    {{0, 4, 4},     VIRTUALX_PARAM_ENABLE,                     {1}    },
    {{0, 4, 4},     VIRTUALX_PARAM_DIALOGCLARITY_MODE,         {1}    },
    {{0, 4, 4},     VIRTUALX_PARAM_SURROUND_MODE,              {1}    },
    {{0, 4, 4},     AUDIO_PARAM_TYPE_NONE,                     {1}    },
    {{0, 4, 32},    AUDIO_PARAM_TYPE_TRU_SURROUND,             {1}    },
    {{0, 4, 32},    AUDIO_PARAM_TYPE_CC3D,                     {1}    },
    {{0, 4, 40},    AUDIO_PARAM_TYPE_TRU_BASS,                 {1}    },
    {{0, 4, 32},    AUDIO_PARAM_TYPE_TRU_DIALOG,               {1}    },
    {{0, 4, 24},    AUDIO_PARAM_TYPE_DEFINITION,               {1}    },
    {{0, 4, 12},    AUDIO_PARAM_TYPE_TRU_VOLUME,               {1}    },
    {{0, 4, 4},     AUDIO_ALL_PARAM_DUMP,                      {1}    },
};


Virtualx_param_t *getVxParamInstance(Virtualx_param_t *table, int size, int paramIndex)
{
    Virtualx_param_t *retVxParam = NULL;

    for (int i = 0; i < size; i++) {
        Virtualx_param_t *temp = &table[i];
        if (temp->command == paramIndex) {
            retVxParam = temp;
            break;
        }
    }
    return retVxParam;
}

int set_param_from_cmd_line(int argc __unused, char **argv)
{
    int effectIndex = 0;
    int paramIndex = 0;
    Virtualx_param_t *vxParam = NULL;

    sscanf(argv[1], "%d", &effectIndex);
    sscanf(argv[2], "%d", &paramIndex);

    vxParam = getVxParamInstance(VxParamTable, AM_ARRAY_SIZE(VxParamTable), paramIndex);
    if (!vxParam) {
        LOG("Can not fine VxParam by paramIndex:%d", paramIndex);
        return -1;
    }

    memset(&vxParam->param, 0, sizeof(float) * VX_MAX_PARAM_SIZE);

    //------------get Virtualx parameters--------------------------------------
    if ((paramIndex >= PARAM_MBHL_BYPASS_GAIN_I32 && paramIndex <= PARAM_MBHL_VOLUME_I32)
        || paramIndex == PARAM_MBHL_OUTPUT_GAIN_I32
        || (paramIndex >= PARAM_MBHL_COMP_LOW_RATIO_I32 && paramIndex <= PARAM_MBHL_COMP_LOW_MAKEUP_I32)
        || (paramIndex >= PARAM_MBHL_COMP_MID_RATIO_I32 && paramIndex <= PARAM_MBHL_COMP_MID_MAKEUP_I32)
        || (paramIndex >= PARAM_MBHL_COMP_HIGH_RATIO_I32 && paramIndex <= PARAM_MBHL_FAST_ATTACK_I32)
        || paramIndex == PARAM_MBHL_APP_FRT_LOWCROSS_F32 || paramIndex == PARAM_MBHL_APP_FRT_MIDCROSS_F32
        || paramIndex == PARAM_TBHDX_MAXGAIN_I32
        || paramIndex == PARAM_TBHDX_TEMP_GAIN_I32 || paramIndex == PARAM_VX_HEADROOM_GAIN_I32
        || paramIndex == PARAM_TBHDX_APP_HPRATIO_F32 || paramIndex == PARAM_TBHDX_APP_EXTBASS_F32
        || paramIndex == PARAM_VX_PROC_OUTPUT_GAIN_I32 || paramIndex == PARAM_TSX_LPR_GAIN_I32
        || paramIndex == PARAM_TSX_CENTER_GAIN_I32 || paramIndex == PARAM_TSX_HEIGHTMIX_COEFF_I32
        || paramIndex == PARAM_TSX_FRNT_CTRL_I32 || paramIndex == PARAM_TSX_SRND_CTRL_I32
        || paramIndex == PARAM_VX_DC_CONTROL_I32 || paramIndex == PARAM_VX_DEF_CONTROL_I32
        || paramIndex == PARAM_AEQ_INPUT_GAIN_I16 || paramIndex == PARAM_AEQ_OUTPUT_GAIN_I16
        || paramIndex == PARAM_AEQ_BYPASS_GAIN_I16)
    {
        sscanf(argv[3], "%f", &vxParam->f);
        LOG("Main() EffectIndex:%d, ParamIndex:%d, ParamScale:%f\n", effectIndex, paramIndex, vxParam->f);
    }
    else if (paramIndex == AUDIO_PARAM_TYPE_TRU_SURROUND
            || paramIndex == AUDIO_PARAM_TYPE_CC3D
            || paramIndex == AUDIO_PARAM_TYPE_TRU_DIALOG)
    {
        for (int i = 0; i < 8; i++) {
            sscanf(argv[i + 3], "%f", &vxParam->params[i]);
            LOG("Main() EffectIndex:%d, ParamIndex:%d, grange:%f\n", effectIndex, paramIndex, vxParam->params[i]);
        }
    }
    else if (paramIndex == AUDIO_PARAM_TYPE_TRU_BASS)
    {
        for (int i = 0; i < 10; i++) {
            sscanf(argv[i + 3], "%f", &vxParam->params[i]);
            LOG("Main() EffectIndex:%d, ParamIndex:%d, grange:%f\n", effectIndex, paramIndex, vxParam->params[i]);
        }
    }
    else if (paramIndex == AUDIO_PARAM_TYPE_DEFINITION)
    {
        for (int i = 0; i < 6; i++) {
            sscanf(argv[i + 3], "%f", &vxParam->params[i]);
            LOG("Main() EffectIndex:%d, ParamIndex:%d, grange:%f\n", effectIndex, paramIndex, vxParam->params[i]);
        }
    }
    else if (paramIndex == AUDIO_PARAM_TYPE_TRU_VOLUME)
    {
        for (int i = 0; i < 3; i++) {
            sscanf(argv[i + 3], "%f", &vxParam->params[i]);
            LOG("Main() EffectIndex:%d, ParamIndex:%d, grange:%f\n", effectIndex, paramIndex, vxParam->params[i]);
        }
    }
    else if (paramIndex == PARAM_AEQ_BAND_Fre
            || paramIndex == PARAM_AEQ_BAND_Gain
            || paramIndex == PARAM_AEQ_BAND_Q
            || paramIndex == PARAM_AEQ_BAND_type)
    {
        for (int i = 0; i < 5; i++) {
            sscanf(argv[i + 3], "%f", &vxParam->params[i]);
            LOG("Main() EffectIndex:%d, ParamIndex:%d, grange:%f\n", effectIndex, paramIndex, vxParam->params[i]);
        }
    }
    else /* integer value type */
    {
        sscanf(argv[3], "%d", &vxParam->v);
        LOG("Main() EffectIndex:%d, ParamIndex:%d, ParamValue:%d \n", effectIndex, paramIndex, vxParam->v);
    }
    return paramIndex;
}

int getIntData (int *data)
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

int getFloatData (float *data)
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

//Only support part of paramIndex setting
//The param value type must be: single integer or single float input
int set_param_from_scanf(sp<AudioEffect>& gAudioEffect __unused, int effectIndex)
{
    Virtualx_param_t *vxParam = NULL;
    int paramIndex = -1;

    if (getIntData(&paramIndex) < 0) {
        LOG("scanf() ParamIndex:%d Failed!\n", paramIndex);
        return -1;
    }

    vxParam = getVxParamInstance(VxParamTable, AM_ARRAY_SIZE(VxParamTable), paramIndex);
    if (!vxParam) {
        LOG("Can not fine VxParam by paramIndex:%d", paramIndex);
        return -1;
    }

    memset(&vxParam->param, 0, sizeof(float) * VX_MAX_PARAM_SIZE);

    //------------get Virtualx parameters--------------------------------------
    if ((paramIndex >= PARAM_MBHL_BYPASS_GAIN_I32 && paramIndex <= PARAM_MBHL_VOLUME_I32)
        || paramIndex == PARAM_MBHL_OUTPUT_GAIN_I32
        || (paramIndex >= PARAM_MBHL_COMP_LOW_RATIO_I32 && paramIndex <= PARAM_MBHL_COMP_LOW_MAKEUP_I32)
        || (paramIndex >= PARAM_MBHL_COMP_MID_RATIO_I32 && paramIndex <= PARAM_MBHL_COMP_MID_MAKEUP_I32)
        || (paramIndex >= PARAM_MBHL_COMP_HIGH_RATIO_I32 && paramIndex <= PARAM_MBHL_FAST_ATTACK_I32)
        || paramIndex == PARAM_MBHL_APP_FRT_LOWCROSS_F32 || paramIndex == PARAM_MBHL_APP_FRT_MIDCROSS_F32
        || paramIndex == PARAM_TBHDX_MAXGAIN_I32
        || paramIndex == PARAM_TBHDX_TEMP_GAIN_I32 || paramIndex == PARAM_VX_HEADROOM_GAIN_I32
        || paramIndex == PARAM_TBHDX_APP_HPRATIO_F32 || paramIndex == PARAM_TBHDX_APP_EXTBASS_F32
        || paramIndex == PARAM_VX_PROC_OUTPUT_GAIN_I32 || paramIndex == PARAM_TSX_LPR_GAIN_I32
        || paramIndex == PARAM_TSX_CENTER_GAIN_I32 || paramIndex == PARAM_TSX_HEIGHTMIX_COEFF_I32
        || paramIndex == PARAM_TSX_FRNT_CTRL_I32 || paramIndex == PARAM_TSX_SRND_CTRL_I32
        || paramIndex == PARAM_VX_DC_CONTROL_I32 || paramIndex == PARAM_VX_DEF_CONTROL_I32
        || paramIndex == PARAM_AEQ_INPUT_GAIN_I16 || paramIndex == PARAM_AEQ_OUTPUT_GAIN_I16
        || paramIndex == PARAM_AEQ_BYPASS_GAIN_I16)
    {
        if (getFloatData(&vxParam->f) < 0) {
            goto Retry_input;
        }
        LOG("scanf() EffectIndex:%d, ParamIndex:%d, ParamScale:%f\n", effectIndex, paramIndex, vxParam->f);
    }
    else {
        if (getIntData(&vxParam->v) < 0) {
            goto Retry_input;
        }
        LOG("scanf() EffectIndex:%d, ParamIndex:%d, ParamInt:%d\n", effectIndex, paramIndex, vxParam->v);
    }

    return paramIndex;

Retry_input:
    LOG("Read parameters from scanf fail!, Retry!\n");
    return -1;
}


int Virtualx_effect_func(sp<AudioEffect>& gAudioEffect, int paramIndex)
{
    if (!gAudioEffect) {
        LOG("Invalid gAudioEffect = NULL!\n");
        return -1;
    }

    int rc = 0;
    Virtualx_param_t *vxParam = NULL;

    vxParam = getVxParamInstance(VxParamTable, AM_ARRAY_SIZE(VxParamTable), paramIndex);
    if (!vxParam) {
        LOG("Can not fine VxParam by paramIndex:%d", paramIndex);
        return -1;
    }

    switch (paramIndex) {
        case VIRTUALX_PARAM_ENABLE:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: Status vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }
            if (!gAudioEffect->setParameter(&vxParam->param))
                LOG("Virtualx: Successful\n");
            else
                LOG("Virtualx: Failed\n");
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("Virtualx: Status is %d -> %s, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, VXStatusstr[vxParam->v], FLOAT2INT(vxParam->v));
            return 0;
       case PARAM_MBHL_ENABLE_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: MBHL ENABLE vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            LOG("mbhl enable valInt:%d\n", vxParam->v);
            return 0;
        case PARAM_MBHL_BYPASS_GAIN_I32:
            if (vxParam->f < 0 || vxParam->f > 1.0) {
                LOG("Vritualx: mbhl bypass gain vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            LOG("mbhl bypass gain:%f\n", vxParam->f);
            return 0;
        case PARAM_MBHL_REFERENCE_LEVEL_I32:
            if (vxParam->f < 0.0009 || vxParam->f > 1.0) {
                LOG("Vritualx: mbhl reference level vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl reference level is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_VOLUME_I32:
            if (vxParam->f < 0 || vxParam->f > 1.0) {
                LOG("Vritualx: mbhl volume vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl volume is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_VOLUME_STEP_I32:
            if (vxParam->v < 0 || vxParam->v > 100) {
                LOG("Vritualx: mbhl volumestep vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl volume step is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_BALANCE_STEP_I32:
            if (vxParam->v < -10 || vxParam->v > 10) {
                LOG("Vritualx: mbhl banlance step vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl balance step is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_OUTPUT_GAIN_I32:
            if (vxParam->f < 0 || vxParam->f > 1.0) {
                LOG("Vritualx: mbhl output gain vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            // gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl output gain is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_BOOST_I32:
            if (vxParam->f < 0.001 || vxParam->f > 1000) {
                LOG("Vritualx: mbhl boost  vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl boost is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_THRESHOLD_I32:
            if (vxParam->f < 0.064 || vxParam->f > 1.0) {
                LOG("Vritualx: mbhl threshold  vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl threshold is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_SLOW_OFFSET_I32:
            if (vxParam->f < 0.3170 || vxParam->f > 3.1619) {
                LOG("Vritualx: mbhl slow offset  vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl slow offset is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_FAST_ATTACK_I32:
            if (vxParam->f < 0 || vxParam->f > 10) {
                LOG("Vritualx: mbhl fast attack  vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl fast attack is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_FAST_RELEASE_I32:
            if (vxParam->v < 10 || vxParam->v > 500) {
                LOG("Vritualx: mbhl fast release vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl fast release is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_SLOW_ATTACK_I32:
            if (vxParam->v < 100 || vxParam->v > 1000) {
                LOG("Vritualx: mbhl slow attack vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl slow attack is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_SLOW_RELEASE_I32:
            if (vxParam->v < 100 || vxParam->v > 2000) {
                LOG("Vritualx: mbhl slow release vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl slow release is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_DELAY_I32:
            if (vxParam->v < 1 || vxParam->v > 16) {
                LOG("Vritualx: mbhl delay vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl delay is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_ENVELOPE_FREQUENCY_I32:
            if (vxParam->v < 5 || vxParam->v > 500) {
                LOG("Vritualx: mbhl envelope freq vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl envelope freq is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_MODE_I32:
            if (vxParam->v < 0 || vxParam->v > 4) {
                LOG("Vritualx: mbhl mode vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl mode is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_PROCESS_DISCARD_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: mbhl process discard vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl process discard is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_CROSS_LOW_I32:
            if (vxParam->v < 0 || vxParam->v > 20) {
                LOG("Vritualx: mbhl cross low vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl cross low  is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_CROSS_MID_I32:
            if (vxParam->v < 0 || vxParam->v > 20) {
                LOG("Vritualx: mbhl cross mid vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl cross mid is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_COMP_ATTACK_I32:
            if (vxParam->v < 0 || vxParam->v > 100) {
                LOG("Vritualx: mbhl compressor attack time vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor attack time is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_COMP_LOW_RELEASE_I32:
            if (vxParam->v < 50 || vxParam->v > 2000) {
                LOG("Vritualx: mbhl compressor low release time vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor low release time is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_COMP_LOW_RATIO_I32:
            if (vxParam->f < 1.0 || vxParam->f > 20.0) {
                LOG("Vritualx: mbhl compressor low ratio vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor low ratio is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_COMP_LOW_THRESH_I32:
            if (vxParam->f < 0.0640 || vxParam->f > 15.8479) {
                LOG("Vritualx: mbhl compressor low threshold vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor low threshold is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_COMP_LOW_MAKEUP_I32:
            if (vxParam->f < 0.0640 || vxParam->f > 15.8479) {
                LOG("Vritualx: mbhl compressor low makeup vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor low makeup is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_COMP_MID_RELEASE_I32:
            if (vxParam->v < 50 || vxParam->v > 2000) {
                LOG("Vritualx: mbhl compressor mid release time vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor mid release time is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_COMP_MID_RATIO_I32:
             if (vxParam->f < 1.0 || vxParam->f > 20.0) {
                LOG("Vritualx: mbhl compressor mid ratio vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor mid ratio is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_COMP_MID_THRESH_I32:
            if (vxParam->f < 0.0640 || vxParam->f > 15.8479) {
                LOG("Vritualx: mbhl compressor mid threshold vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor mid threshold is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_COMP_MID_MAKEUP_I32:
             if (vxParam->f < 0.0640 || vxParam->f > 15.8479) {
                LOG("Vritualx: mbhl compressor mid makeup vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor mid makeup is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_COMP_HIGH_RELEASE_I32:
            if (vxParam->v < 50 || vxParam->v > 2000) {
                LOG("Vritualx: mbhl compressor high release time vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor high release time is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_MBHL_COMP_HIGH_RATIO_I32:
            if (vxParam->f < 1.0 || vxParam->f > 20.0) {
                LOG("Vritualx: mbhl compressor high ratio vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor high ratio is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_COMP_HIGH_THRESH_I32:
            if (vxParam->f < 0.0640 || vxParam->f > 15.8479) {
                LOG("Vritualx: mbhl compressor high threshold vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor high threshold is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_COMP_HIGH_MAKEUP_I32:
             if (vxParam->f < 0.0640 || vxParam->f > 15.8479) {
                LOG("Vritualx: mbhl compressor high makeup vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("mbhl compressor high makeup is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_TBHDX_ENABLE_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: tbhdx enable vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tbhdx enable is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_TBHDX_MONO_MODE_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: tbhdx mono mode vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tbhdx mono mode is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_TBHDX_SPKSIZE_I32:
            if (vxParam->v < 0 || vxParam->v > 12) {
                LOG("Vritualx: tbhdx spksize vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tbhdx spksize is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_TBHDX_TEMP_GAIN_I32:
            if (vxParam->f < 0.0 || vxParam->f > 1.0) {
                LOG("Vritualx: tbhdx temp gain vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tbhdx temp gain is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_TBHDX_MAXGAIN_I32:
            if (vxParam->f < 0.0 || vxParam->f > 1.0) {
                LOG("Vritualx: tbhdx max gain vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tbhdx max gain is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_TBHDX_PROCESS_DISCARD_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: tbhdx process discard vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
           //gAudioEffect->getParameter(&vxParam->param);
            LOG("tbhdx process discard is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_TBHDX_HPORDER_I32:
            if (vxParam->v < 0 || vxParam->v > 8) {
                LOG("Vritualx: tbhdx high pass filter order vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tbhdx high pass filter order is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_TBHDX_HP_ENABLE_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: tbhdx high pass enable vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tbhdx high pass enable is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_VX_ENABLE_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: vxlib1 enable vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("vxlib1 enable is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_VX_INPUT_MODE_I32:
            if (vxParam->v < 0 || vxParam->v > 4) {
                LOG("Vritualx: vxlib1 input mode vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("vxlib1 input mode is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_VX_HEADROOM_GAIN_I32:
            if (vxParam->f < 0.1250 || vxParam->f > 1.0) {
                LOG("Vritualx: vxlib1 headroom gain vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("vxlib1 headroom gain is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_VX_PROC_OUTPUT_GAIN_I32:
            if (vxParam->f < 0.5 || vxParam->f > 4.0) {
                LOG("Vritualx: vxlib1 output gain vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("vxlib1 output gain is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_TSX_ENABLE_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: tsx enable vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("vxlib1 tsx enable is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_TSX_PASSIVEMATRIXUPMIX_ENABLE_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: tsx passive matrix upmixer enable vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("vxlib1 tsx passive matrix upmixer enable is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_TSX_HORIZ_VIR_EFF_CTRL_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: tsx horizontal Effect vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("vxlib1 tsx horizontal Effect is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_TSX_FRNT_CTRL_I32:
            if (vxParam->f < 0.5 || vxParam->f > 2.0) {
                LOG("Vritualx: tsx frnt ctrl vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tsx frnt ctrl is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_TSX_SRND_CTRL_I32:
            if (vxParam->f < 0.5 || vxParam->f > 2.0) {
                LOG("Vritualx: tsx srnd ctrl vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tsx srnd ctrl is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_TSX_LPR_GAIN_I32:
             if (vxParam->f < 0.0 || vxParam->f > 2.0) {
                LOG("Vritualx: tsx lprtoctr mix gain vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tsx lprtoctr mix gain is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_TSX_HEIGHTMIX_COEFF_I32:
             if (vxParam->f < 0.5 || vxParam->f > 2.0) {
                LOG("Vritualx: tsx heightmix coeff vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tsx heightmix coeff is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_TSX_CENTER_GAIN_I32:
             if (vxParam->f < 1.0 || vxParam->f > 2.0) {
                LOG("Vritualx: tsx center gain vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tsx center gain is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_TSX_HEIGHT_DISCARD_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: tsx height discard vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tsx height discard is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_TSX_PROCESS_DISCARD_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: tsx process discard vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tsx process discard is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_TSX_HEIGHT_UPMIX_ENABLE_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: tsx height upmix enable vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tsx height upmix enable is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_VX_DC_ENABLE_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: tsx dc enable vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tsx dc enable is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_VX_DC_CONTROL_I32:
             if (vxParam->f < 0.0 || vxParam->f > 1.0) {
                LOG("Vritualx: tsx dc level vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tsx dc level is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_VX_DEF_ENABLE_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: tsx def enable vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tsx def enable is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_VX_DEF_CONTROL_I32:
             if (vxParam->f < 0.0 || vxParam->f > 1.0) {
                LOG("Vritualx: tsx def level vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("tsx def level is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_LOUDNESS_CONTROL_ENABLE_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("loudness control = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("loudness control is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_LOUDNESS_CONTROL_TARGET_LOUDNESS_I32:
            if (vxParam->v < -40 || vxParam->v > 0) {
                LOG("loudness control target = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("loudness control target is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_LOUDNESS_CONTROL_PRESET_I32:
            if (vxParam->v < 0 || vxParam->v > 2) {
                LOG("loudness control preset = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            //gAudioEffect->getParameter(&vxParam->param);
            LOG("loudness control preset is %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_TBHDX_APP_SPKSIZE_I32:
            if (vxParam->v < 40 || vxParam->v > 600) {
                LOG("app spksize = %d invalid\n",vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            LOG("app spksize %d, 16bit normalized data ----> 0x%08X\n",
                vxParam->v, FLOAT2INT(vxParam->v));
            return 0;
        case PARAM_TBHDX_APP_HPRATIO_F32:
            if (vxParam->f < 0.0 || vxParam->f > 1.0) {
                LOG("app hpratio = %f invalid\n",vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            LOG("app hpratio is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_TBHDX_APP_EXTBASS_F32:
            if (vxParam->f < 0.0 || vxParam->f > 1.0) {
                LOG("app extbass = %f invalid\n",vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            LOG("app extbass is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_APP_FRT_LOWCROSS_F32:
            if (vxParam->f < 40 || vxParam->f > 8000.0) {
                LOG("app low freq = %f invalid\n",vxParam->f);
                return -1;
            }

            rc =gAudioEffect->setParameter(&vxParam->param);
            LOG("rc is %d\n",rc);
            LOG("app low freq is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_MBHL_APP_FRT_MIDCROSS_F32:
            if (vxParam->f < 40.0 || vxParam->f > 8000.0) {
                LOG("app mid freq = %f invalid\n",vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            LOG("app mid freq is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case AUDIO_PARAM_TYPE_TRU_SURROUND:
            for (int i = 0; i < 8; i++) {
                LOG("vxParam.params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, vxParam->params[i], FLOAT2INT(vxParam->params[i]));
            }
            rc = gAudioEffect->setParameter(&vxParam->param);
            LOG("rc is %d\n",rc);
            return 0;
        case AUDIO_PARAM_TYPE_CC3D:
            for (int i = 0; i < 8; i++) {
                LOG("vxParam.params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, vxParam->params[i], FLOAT2INT(vxParam->params[i]));
            }
            rc = gAudioEffect->setParameter(&vxParam->param);
            LOG("rc is %d\n",rc);
            return 0;
        case AUDIO_PARAM_TYPE_TRU_BASS:
            for (int i = 0; i < 10; i++) {
                LOG("vxParam.params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, vxParam->params[i], FLOAT2INT(vxParam->params[i]));
            }
            rc = gAudioEffect->setParameter(&vxParam->param);
            LOG("rc is %d\n",rc);
            return 0;
        case AUDIO_PARAM_TYPE_TRU_DIALOG:
            for (int i = 0; i < 8; i++) {
                LOG("vxParam.params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, vxParam->params[i], FLOAT2INT(vxParam->params[i]));
            }
            rc = gAudioEffect->setParameter(&vxParam->param);
            LOG("rc is %d\n",rc);
            return 0;
        case AUDIO_PARAM_TYPE_DEFINITION:
            for (int i = 0; i < 6; i++) {
                LOG("vxParam.params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, vxParam->params[i], FLOAT2INT(vxParam->params[i]));
            }
            rc = gAudioEffect->setParameter(&vxParam->param);
            LOG("rc is %d\n",rc);
            return 0;
        case AUDIO_PARAM_TYPE_TRU_VOLUME:
            for (int i = 0; i < 3; i++) {
                LOG("vxParam.params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, vxParam->params[i], FLOAT2INT(vxParam->params[i]));
            }
            rc = gAudioEffect->setParameter(&vxParam->param);
            LOG("rc is %d\n",rc);
            return 0;
        case PARAM_AEQ_ENABLE_I32:
             if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: aeq  enable vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            LOG("aeq enable is %d\n",vxParam->v);
            return 0;
        case PARAM_AEQ_DISCARD_I32:
             if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: aeq  discard vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            LOG("aeq discard is %d\n",vxParam->v);
            return 0;
        case PARAM_AEQ_INPUT_GAIN_I16:
            if (vxParam->f < 0 || vxParam->f > 1.0) {
                LOG("Vritualx: aeq input gain vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            LOG("aeq input gain is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_AEQ_OUTPUT_GAIN_I16:
            if (vxParam->f < 0 || vxParam->f > 1.0) {
                LOG("Vritualx: aeq output gain vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            LOG("aeq output gain is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_AEQ_BYPASS_GAIN_I16:
            if (vxParam->f < 0 || vxParam->f > 1.0) {
                LOG("Vritualx: aeq bypass gain vxParam->v = %f invalid\n", vxParam->f);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            LOG("aeq bypass gain is %f, 16bit normalized data ----> 0x%08X\n",
                vxParam->f, FLOAT2INT(vxParam->f));
            return 0;
        case PARAM_AEQ_LR_LINK_I32:
            if (vxParam->v < 0 || vxParam->v > 1) {
                LOG("Vritualx: aeq lr link vxParam->v = %d invalid\n", vxParam->v);
                return -1;
            }

            gAudioEffect->setParameter(&vxParam->param);
            LOG("aeq lr link flag is %d\n",vxParam->v);
            return 0;
        case PARAM_AEQ_BAND_Fre:
            for (int i = 0; i < 5; i++) {
                LOG("vxParam.params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, vxParam->params[i], FLOAT2INT(vxParam->params[i]));
            }
            rc = gAudioEffect->setParameter(&vxParam->param);
            LOG("rc is %d\n",rc);
            return 0;
        case PARAM_AEQ_BAND_Gain:
            for (int i = 0; i < 5; i++) {
                LOG("vxParam.params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, vxParam->params[i], FLOAT2INT(vxParam->params[i]));
            }
            rc = gAudioEffect->setParameter(&vxParam->param);
            LOG("rc is %d\n",rc);
            return 0;
        case PARAM_AEQ_BAND_Q:
            for (int i = 0; i < 5; i++) {
                LOG("vxParam.params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, vxParam->params[i], FLOAT2INT(vxParam->params[i]));
            }
            rc = gAudioEffect->setParameter(&vxParam->param);
            LOG("rc is %d\n",rc);
            return 0;
        case PARAM_AEQ_BAND_type:
            for (int i = 0; i < 5; i++) {
                LOG("vxParam.params[%d] is %f, 16bit normalized data ----> 0x%08X\n",
                    i, vxParam->params[i], FLOAT2INT(vxParam->params[i]));
            }
            rc = gAudioEffect->setParameter(&vxParam->param);
            LOG("rc is %d\n",rc);
            return 0;
        case PARAM_CHANNEL_NUM:
            gAudioEffect->setParameter(&vxParam->param);
            LOG("set dts channel num is %d\n",vxParam->v);
            return 0;
        case AUDIO_ALL_PARAM_DUMP:
            gAudioEffect->setParameter(&vxParam->param);
            LOG("get all VX parameters %d\n",vxParam->v);
            return 0;
    default:
        LOG("Virtualx: ParamIndex = %d invalid\n", paramIndex);
        return -1;
    }
}

void printf_vx_help(char *name)
{
    LOG("*********************************Virtualx***********************************\n");
    LOG("VirtualX EffectIndex: %d\n", 4);
    LOG("Usage: %s %d <ParamIndex> <ParamValue/ParamScale/gParamBand>\n", name, 4);
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
    LOG("------------DTSEQ-------ParamIndex(from %d to %d)--\n",
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
    LOG("ParamIndex: %d -> DTS source channel num\n", (int)PARAM_CHANNEL_NUM);
    LOG("ParamValue: 2 | 6 \n");
    LOG("------------Debug interface------------\n");
    LOG("ParamIndex: %d -> Get all parameters from VX lib\n", (int)AUDIO_ALL_PARAM_DUMP);
    LOG("ParamValue: 0 \n");
    LOG("****************************************************************************\n\n");
}
