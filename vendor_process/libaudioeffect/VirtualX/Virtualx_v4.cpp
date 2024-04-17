/*
 * Copyright (C) 2024 Amlogic Corporation.
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
 *      This file implements a special EQ  from Amlogic.
 *
 */

#define LOG_TAG "virtualx_V4_Effect"
//#define LOG_NDEBUG 0

#include <fcntl.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <utils/Log.h>
#include <cutils/properties.h>
#include <hardware/audio_effect.h>

#include "aml_android_hidl_utils.h"
#include "IniParser.h"
#include "Virtualx_v4.h"

using namespace android;

//#define DEBUG_VX
//#define DEBUG_VX_INI

extern "C" {

#include "../Utility/LibAudioEffect.h"

#define MODEL_NAME_DEFAULT "DEFAULT"
#define VALUE_MAX 100
#define VIRTUALX_MAX_IN_CHANNELS        ( 12 )
#define VIRTUALX_MAX_OUT_CHANNELS       ( 2 )
#define VIRTUALX_FRAME_SIZE             ( 256 )
#define VX_CUP_LOAD_ARM9E               100  /* Expressed in 0.1 MIPS*/
#define VX_MEM_USAGE                    50   /*Expressed in kB*/
#define MAX_VX_PARAM_COUNT              ( 300 )

#define AM_ARRAY_SIZE(_a)    (sizeof(_a)/sizeof((_a)[0]))

#if defined(__LP64__)
#define LIBVX_PATH_A "/vendor/lib64/soundfx/libvxv4.so"
#else
#define LIBVX_PATH_A "/vendor/lib/soundfx/libvxv4.so"
#endif


// effect_handle_t interface implementation for virtualx effect
extern const struct effect_interface_s VirtualxInterface;

// VX effect TYPE: 5112a99e-b8b9-4c5e-91fd-a804d29c36b2
// VX effect UUID: 61821587-ce3c-4aac-9122-86d874ea1fb1
const effect_descriptor_t VirtualxDescriptor = {
        {0x5112a99e, 0xb8b9, 0x4c5e, 0x91fd, {0xa8, 0x04, 0xd2, 0x9c, 0x36, 0xb2}}, // type
        {0x61821587, 0xce3c, 0x4aac, 0x9122, {0x86, 0xd8, 0x74, 0xea, 0x1f, 0xb1}}, // uuid
        EFFECT_CONTROL_API_VERSION,
        EFFECT_FLAG_TYPE_POST_PROC | EFFECT_FLAG_DEVICE_IND | EFFECT_FLAG_NO_PROCESS | EFFECT_FLAG_OFFLOAD_SUPPORTED,
        VX_CUP_LOAD_ARM9E,
        VX_MEM_USAGE,
        "VirtualX_V4",
        "DTS Labs",
};

enum Virtualx_state_e {
    VIRTUALX_STATE_UNINITIALIZED,
    VIRTUALX_STATE_INITIALIZED,
    VIRTUALX_STATE_ACTIVE,
};


typedef enum vx_internal_user_mode {
    VX_INNER_MODE_0 = 0,
    VX_INNER_MODE_1,
    VX_INNER_MODE_2,
    VX_INNER_MODE_3,
    VX_INNER_MODE_4,
    VX_INNER_MODE_5,
    VX_INNER_MODE_6,
    VX_INNER_MODE_7,
    VX_INNER_MODE_8,
    VX_INNER_MODE_MAX,
} VirtualX_InnerMode_t;


typedef struct Virtualx_V4_high_api_s {
    int (*vx_v4_init)(void **ppInstance, void *pInitData, int argc, char **argv);
    int (*vx_v4_process)(int **ppMappedInCh, int **ppMappedOutCh);
    int (*vx_v4_release)(void* pInstance);
    int (*vx_v4_reset)(void* pInstance);
    int (*vx_v4_set_param)(void* pInstance, int argc, char **argv);
    int (*vx_v4_set_inmode)(void* pInstance, int inmode);
    int (*vx_v4_set_outmode)(void* pInstance, int outmode);
    /*Sub module APIs*/
} VirtualxV4HighApi;


typedef struct cmd_param_str {
    int mode;
    int userId;
    int key;
    const char *param; //key name defined by VX V4 lib
    char *value;
    char value_store[VX_INNER_MODE_MAX][MAX_PARAM_VALUE_LEN];
    //already stored value count
    int count;
} VX_Cmd_Param;

typedef struct vxdata_s {
    int enable;

    //Current ini mode, setting by UI
    int mode;
    //successfully load ini count and it's mode
    int ini_count;
    int ini_mode[VX_INNER_MODE_MAX];

    VX_Cmd_Param *vxConfigCmds[MAX_VX_PARAM_COUNT];
    int vxParamCount;
} vxdata;


/* DTS VX V4 context */
typedef struct vxContext_s {
    const struct effect_interface_s *itfe;
    effect_config_t                 config;
    Virtualx_state_e                state;
    void                            *gVXLibHandler;
    VirtualxV4HighApi                gVirtualxapi;
    void                            *vxInst;
    int32_t                         MappedInTempBuffer[VIRTUALX_MAX_IN_CHANNELS][VIRTUALX_FRAME_SIZE];
    int32_t                         MappedOutTempBuffer[VIRTUALX_MAX_IN_CHANNELS][VIRTUALX_FRAME_SIZE];
    int32_t                         *ppMappedInCh[VIRTUALX_MAX_IN_CHANNELS];
    int32_t                         *ppMappedOutCh[VIRTUALX_MAX_IN_CHANNELS];
    /* input source PCM channel number */
    int32_t                         effect_ch_in;
    /* output pcm channel number after VX process */
    int32_t                         effect_ch_out;
    int32_t                         left_bytes;
    /*vx process one block 256 frames,framessize (4 bytes * 12ch )*/
    int32_t                         left_pBuffer[VIRTUALX_FRAME_SIZE * VIRTUALX_MAX_IN_CHANNELS];
    int32_t                         left_process_bytes;
    int32_t                         left_process_pBuffer[VIRTUALX_FRAME_SIZE * VIRTUALX_MAX_IN_CHANNELS];
    bool                            vx_effect_enable;
    vxdata                          gvxdata;
} vxContext;

/**************************** VX V4 parameters APIs***************************************/
enum vx_param_mode_t {
    PARAM_DEFAULT_MODE = 0,
};

#define CMD_NAME_POS_INIT(_param, _key)    {.param = _param, .key = _key}
#define CMD_NAME_USERID_INIT(_param, _key, _userId)    {.param = _param, .key = _key, .userId = _userId}
#define STORE_CMD_PARAM_FOR_MODE(_cmd, _value, _mode) \
   do {\
        _cmd->mode = PARAM_DEFAULT_MODE;\
        char *_dest = _cmd->value_store[_mode];\
        strncpy(_dest, _value, MAX_PARAM_VALUE_LEN);\
        _cmd->value = _cmd->value_store[_cmd->mode];\
        _cmd->count++;\
    } while(0);\

#define SWITCH_CMD_PARAM_WITH_MODE(_cmd, _mode) \
   do {\
        _cmd->mode = _mode;\
        _cmd->value = _cmd->value_store[_cmd->mode];\
    } while(0);\

//Only apply value for current mode
#define SET_PARAM_WITH_VALUE(_cmd, _value) \
   do {\
        char *_dest = _cmd->value_store[_cmd->mode];\
        if (_dest[0] == 0 ) _cmd->count++;\
        strncpy(_dest, _value, MAX_PARAM_VALUE_LEN);\
        _cmd->value = _cmd->value_store[_cmd->mode];\
    } while(0);\

//Only apply value for current mode
#define SET_PARAM_BY_KEY(_table, _key, _value) \
    ({\
        VX_Cmd_Param *tmp = &_table[_key];\
        char *_dest = tmp->value_store[tmp->mode];\
        strcpy(_dest, _value);\
        tmp->value = tmp->value_store[tmp->mode];\
        tmp;\
    })

#define STORE_PARAM_WITH_MODE(_table, _key, _value, _mode) \
    ({\
        VX_Cmd_Param *tmp = &_table[_key];\
        tmp->mode = _mode;\
        char *_dest = tmp->value_store[tmp->mode];\
        strcpy(_dest, _value);\
        tmp;\
    })

#define SWITCH_PARAM_WITH_MODE(_table, _key, _mode) \
    ({\
        VX_Cmd_Param *tmp = &_table[_key];\
        tmp->mode = _mode;\
        tmp;\
    })


static const char *gVxParamStart = "NULL";
static const char *gVxParamList[MAX_VX_PARAM_COUNT * 2];
static int gReadyParamCount = 0;

void clear_cache_param_list()
{
    //clear param cache list
    gReadyParamCount = 0;
    for (int i =0; i < MAX_VX_PARAM_COUNT * 2; i++) {
        gVxParamList[i] = NULL;
    }
    //set param start string:"NULL"
    gVxParamList[gReadyParamCount++] = gVxParamStart;
}

/*
  function: make VX parameters from VX_Cmd_Param list, the VX using parameters like:
    argv = "NULL", "--param-1", "val_1", "--param-2", "val_2", ..., "--param-n", "val-n"
    argc = n
  input:
    -params: C string pointer list, such as {"param1", "param2"}
    -pcount: list size
    -values: C string pointer list, such as {"val1", "val2"}
    -vcount: value list size
  return:
    c-string pointer list like argv
*/
char ** make_cmd_list_from_cstr(const char** params, int pcount, const char **values, int vcount)
{
    int count = 0;
    if (!params || !values || !pcount || !vcount || (pcount!= vcount)) {
        ALOGE("%s Invalid params:%p pcount:%d values:%p vcount:%d", __func__,
                params, pcount, values, vcount);
        return NULL;
    }

    clear_cache_param_list();

    for (int i = 0; i < pcount; i++) {
        gVxParamList[gReadyParamCount++] = (const char*)params[count];
        gVxParamList[gReadyParamCount++] = (const char*)values[count++];
    }
    return (char**)gVxParamList;
}

/*
  function: make VX parameters from VX_Cmd_Param list, the VX using parameters like:
    argv = "NULL", "--param-1", "val_1", "--param-2", "val_2", ..., "--param-n", "val-n"
    argc = n
  input:
    -table: prepared VX_Cmd_Param *vx_table[] tables
    -table_size: the vx_table size
    -argc: the argv size
  return:
    c-string pointer list like argv
*/
char **make_cmd_list_from_VX_CMDs(VX_Cmd_Param **table, int table_size, int *argc)
{
    if (!table || !table_size || !argc) {
        ALOGE("%s Invalid table:%p table_size:%d argc:%p", __func__,
                table, table_size, argc);
        return NULL;
    }

    clear_cache_param_list();
    for (int i=0; i < table_size; i++) {
        VX_Cmd_Param *temp = table[i];
        if (!temp) {
            ALOGW("table[i=%d] is NULL\n", i);
        }
        gVxParamList[gReadyParamCount++] = (const char*)temp->param;
        gVxParamList[gReadyParamCount++] = (const char*)temp->value;
    }
    *argc = gReadyParamCount;
    return (char**)gVxParamList;
}

VX_Cmd_Param * get_vx_cmd_from_key(VX_Cmd_Param *vxCmds, int count, int key)
{
    for (int i = 0; i < count; i++) {
        VX_Cmd_Param *temp = &vxCmds[i];
        if (temp->key == key) {
            return temp;
        }
    }
    return NULL;
}

void VX_Params_Print_label(const char *label, VX_Cmd_Param **table, int table_size)
{
    ALOGI("VX_Params_Print_label() for Label: %s", label);
    for (int i =0; i < table_size; i++) {
        VX_Cmd_Param *temp = table[i];
        if (temp) {
            if (temp->count >= 1) {
                int print_count = 0;
                ALOGI("Key:%d  %s", temp->key, temp->param);
                for (int j = 0; j < VX_INNER_MODE_MAX; j++) {
                    if ((print_count < temp->count) && (temp->value_store[j][0] != 0)) {
                        ALOGI("\t%sMode[%d] = %s", (j == temp->mode ? "+": "-"), j, temp->value_store[j]);
                        print_count++;
                    }
                }
            } else {
                //The key not defined in ini
                ALOGI("Key:%d  %s = null", temp->key, temp->param);
            }
        }
    }
}

void VX_Params_Print_mode(const char *ini_name, VX_Cmd_Param **table, int table_size, int ini_mode)
{
    ALOGI("[VX_Params_Print_mode ini: %s ini_mode: %d Start]", ini_name, ini_mode);
    for (int i =0; i < table_size; i++) {
        VX_Cmd_Param *temp = table[i];
        if (temp && temp->value_store[ini_mode][0] != 0) {
            ALOGI("%s = %s", temp->param, temp->value_store[ini_mode]);
        }
    }
    ALOGI("[VX_Params_Print_mode ini: %s End]", ini_name);
}

void vx_config_str_print(int argc, char **argv)
{
    ALOGI("%s",__func__);
    if (argc <= 0) {
        return;
    }

    for (int i = 0; i < argc - 1; i++) {
        if (i == 0) {
            ALOGI("+%s", (char*)argv[i]);
        } else {
            ALOGI("%s=%s", (char*)argv[i], (char*)argv[i + 1]);
            i++;
        }
    }
}

/*** VirtualX normal control parameters ***/
enum virtualx_force_id {
    VX_frame_size = 0,
    VX_input_pcm_stride,
    VX_output_pcm_stride,
};

static VX_Cmd_Param virtualx_force_params[] = {
    CMD_NAME_USERID_INIT("--frame-size",        VX_frame_size,          PARAM_VX_FRAME_SIZE),
    CMD_NAME_USERID_INIT("--input-pcm-stride",  VX_input_pcm_stride,    PARAM_VX_INPUT_PCM_STRIDE),
    CMD_NAME_USERID_INIT("--output-pcm-stride", VX_output_pcm_stride,   PARAM_VX_OUTPUT_PCM_STRIDE),
};

/*** VirtualX general control parameters ***/
enum virtualx_general_id {
    VIRTUALX_en = 0,
    VIRTUALX_in_mode,
    VIRTUALX_out_mode,
    VIRTUALX_headroom_gain,
    VIRTUALX_processing_output_gain,
    VIRTUALX_reference_level,
    VIRTUALX_discard,
    VIRTUALX_multirate_processing_type,
};

static VX_Cmd_Param virtualx_general_params[] = {
    CMD_NAME_USERID_INIT("--virtualx-en",                        VIRTUALX_en,                         PARAM_VX_ENABLE_I32),
    CMD_NAME_USERID_INIT("--virtualx-in-mode",                   VIRTUALX_in_mode,                    PARAM_VX_INPUT_MODE_I32),
    CMD_NAME_USERID_INIT("--virtualx-out-mode",                  VIRTUALX_out_mode,                   PARAM_VX_OUTPUT_MODE_I32),
    CMD_NAME_USERID_INIT("--virtualx-headroom-gain",             VIRTUALX_headroom_gain,              PARAM_VX_HEADROOM_GAIN_I32),
    CMD_NAME_USERID_INIT("--virtualx-processing-output-gain",    VIRTUALX_processing_output_gain,     PARAM_VX_PROC_OUTPUT_GAIN_I32),
    CMD_NAME_USERID_INIT("--virtualx-reference-level",           VIRTUALX_reference_level,            PARAM_VX_REFERENCE_LEVEL_I32),
    CMD_NAME_USERID_INIT("--virtualx-discard",                   VIRTUALX_discard,                    PARAM_VX_PROCESS_DISCARD_I32),
    CMD_NAME_USERID_INIT("--virtualx-multirate-processing-type", VIRTUALX_multirate_processing_type,  PARAM_VX_PROCESS_MULTIRATE_PROC_TYPE_I32),
};

/*** VirtualX True surroundX control parameters ***/
enum virtualx_truSurroundX_id {
    VIRTUALX_tsx_en = 0,
    VIRTUALX_tsx_pssv_mtrx_en,
    VIRTUALX_tsx_horiznt_effect_ctrl,
    VIRTUALX_tsx_phantom_ctrgain,
    VIRTUALX_tsx_ctrgain,
    VIRTUALX_tsx_heightmix_coeff,
    VIRTUALX_tsx_height_out_gain,
    VIRTUALX_tsx_precond_front,
    VIRTUALX_tsx_precond_surnd,
    VIRTUALX_tsx_vxtopspk_loc,
    VIRTUALX_tsx_frnt_wide_en,
    VIRTUALX_tsx_hght_virtualizer_en,
    VIRTUALX_tsx_frnt_srnd_en,
    VIRTUALX_tsx_hght_upmix_en,
    VIRTUALX_tsx_lrmix_ratio2ctr,
    VIRTUALX_tsx_lfe_gain,
    VIRTUALX_tsx_height_discard,
    //TSX APP
    VIRTUALX_tsx_app_lstnr_dist,
    VIRTUALX_tsx_app_bttmspktoctr_dist,
    VIRTUALX_tsx_app_topspktoctr_dist,
};

static VX_Cmd_Param virtualx_truSurroundX_params[] = {
    CMD_NAME_USERID_INIT("--virtualx-tsx-en",                    VIRTUALX_tsx_en,                   PARAM_TSX_ENABLE_I32),
    CMD_NAME_USERID_INIT("--virtualx-tsx-pssv-mtrx-en",          VIRTUALX_tsx_pssv_mtrx_en,         PARAM_TSX_PASSIVEMATRIXUPMIX_ENABLE_I32),
    CMD_NAME_USERID_INIT("--virtualx-tsx-horiznt-effect-ctrl",   VIRTUALX_tsx_horiznt_effect_ctrl,  PARAM_TSX_HORIZNT_EFFECT_CTRL),
    CMD_NAME_USERID_INIT("--virtualx-tsx-phantom-ctrgain",       VIRTUALX_tsx_phantom_ctrgain,      PARAM_TSX_PHANTOM_CTRGAIN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-ctrgain",               VIRTUALX_tsx_ctrgain,              PARAM_TSX_CTRGAIN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-heightmix-coeff",       VIRTUALX_tsx_heightmix_coeff,      PARAM_TSX_HEIGHTMIX_COEFF),
    CMD_NAME_USERID_INIT("--virtualx-tsx-height-out-gain",       VIRTUALX_tsx_height_out_gain,      PARAM_TSX_HEIGHT_OUT_GAIN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-precond-front",         VIRTUALX_tsx_precond_front,        PARAM_TSX_PRECOND_FRONT),
    CMD_NAME_USERID_INIT("--virtualx-tsx-precond-surnd",         VIRTUALX_tsx_precond_surnd,        PARAM_TSX_PRECOND_SURND),
    CMD_NAME_USERID_INIT("--virtualx-tsx-vxtopspk-loc",          VIRTUALX_tsx_vxtopspk_loc,         PARAM_TSX_VXTOPSPK_LOC),
    CMD_NAME_USERID_INIT("--virtualx-tsx-frnt-wide-en",          VIRTUALX_tsx_frnt_wide_en,         PARAM_TSX_FRNT_WIDE_EN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-hght-virtualizer-en",   VIRTUALX_tsx_hght_virtualizer_en,  PARAM_TSX_HEIGHT_VIRTUALIZER_EN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-frnt-srnd-en",          VIRTUALX_tsx_frnt_srnd_en,         PARAM_TSX_FRNT_SRND_EN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-hght-upmix-en",         VIRTUALX_tsx_hght_upmix_en,        PARAM_TSX_HEIGHT_UPMIX_ENABLE_I32),
    CMD_NAME_USERID_INIT("--virtualx-tsx-lrmix-ratio2ctr",       VIRTUALX_tsx_lrmix_ratio2ctr,      PARAM_TSX_LRMIX_RATIO2CTR),
    CMD_NAME_USERID_INIT("--virtualx-tsx-lfe-gain",              VIRTUALX_tsx_lfe_gain,             PARAM_TSX_LFE_GAIN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-height-discard",        VIRTUALX_tsx_height_discard,       PARAM_TSX_HEIGHT_DISCARD_I32),
    CMD_NAME_USERID_INIT("--virtualx-tsx-app-lstnr-dist",        VIRTUALX_tsx_app_lstnr_dist,       PARAM_TSX_APP_LSTNR_DIST),
    CMD_NAME_USERID_INIT("--virtualx-tsx-app-bttmspktoctr-dist", VIRTUALX_tsx_app_bttmspktoctr_dist,PARAM_TSX_APP_BTTM_DIST),
    CMD_NAME_USERID_INIT("--virtualx-tsx-app-topspktoctr-dist",  VIRTUALX_tsx_app_topspktoctr_dist, PARAM_TSX_APP_TOP_DIST),
};

enum virtualx_truSurroundX_DC_id {
    VIRTUALX_dialogclarity_en,
    VIRTUALX_dialogclarity_level,
    VIRTUALX_definition_en,
    VIRTUALX_definition_lvl,
    VIRTUALX_cs2to3_en,
};

static VX_Cmd_Param virtualx_truSurroundX_DC_params[] = {
    CMD_NAME_USERID_INIT("--virtualx-dialogclarity-en",       VIRTUALX_dialogclarity_en,      PARAM_VX_DC_ENABLE_I32),
    CMD_NAME_USERID_INIT("--virtualx-dialogclarity-level",    VIRTUALX_dialogclarity_level,   PARAM_VX_DC_CONTROL_I32),
    CMD_NAME_USERID_INIT("--virtualx-definition-en",          VIRTUALX_definition_en,         PARAM_VX_DEF_ENABLE_I32),
    CMD_NAME_USERID_INIT("--virtualx-definition-lvl",         VIRTUALX_definition_lvl,        PARAM_VX_DEF_CONTROL_I32),
    CMD_NAME_USERID_INIT("--virtualx-cs2to3-en",              VIRTUALX_cs2to3_en,             PARAM_VX_CS2TO3_EN),
};

/*** TBHDX control parameters ***/
enum virtual_TBHDX_id {
    tbhdx_front_en,
    tbhdx_front_proc_mode,
    tbhdx_front_spksize,
    tbhdx_front_dynms,
    tbhdx_front_hp_en,
    tbhdx_front_hp_order,
    tbhdx_front_bass_lvl,
    tbhdx_front_extbass,
    tbhdx_front_input_gain,
    tbhdx_front_bypass_gain,
    tbhdx_rear_en,
    tbhdx_rear_proc_mode,
    tbhdx_rear_spksize,
    tbhdx_rear_dynms,
    tbhdx_rear_hp_e,
    tbhdx_rear_hp_order,
    tbhdx_rear_bass_lvl,
    tbhdx_rear_extbass,
    tbhdx_rear_input_gain,
    tbhdx_rear_bypass_gain,
    tbhdx_center_en,
    tbhdx_center_dynms,
    tbhdx_center_hp_en,
    tbhdx_center_hp_order,
    tbhdx_center_bass_lvl,
    tbhdx_center_extbass,
    tbhdx_center_input_gain,
    tbhdx_center_bypass_gain,
    tbhdx_surround_en,
    tbhdx_surround_proc_mode,
    tbhdx_surround_spksize,
    tbhdx_surround_dynms,
    tbhdx_surround_hp_en,
    tbhdx_surround_hp_order,
    tbhdx_surround_bass_lvl,
    tbhdx_surround_extbass,
    tbhdx_surround_input_gain,
    tbhdx_surround_bypass_gain,
    tbhdx_delay_matching_gain,
    tbhdx_discard,
    tbhdx_front_app_spksize,
    tbhdx_front_app_tgain,
    tbhdx_front_app_hpr,
    tbhdx_front_app_extbass,
    tbhdx_center_app_spksize,
    tbhdx_center_app_tgain,
    tbhdx_center_app_hpr,
    tbhdx_center_app_extbass,
    tbhdx_surround_app_spksize,
    tbhdx_surround_app_tgain,
    tbhdx_surround_app_hpr,
    tbhdx_surround_app_extbass,
    tbhdx_rear_app_spksize,
    tbhdx_rear_app_tgain,
    tbhdx_rear_app_hpr,
    tbhdx_rear_app_extbass,
};

static VX_Cmd_Param virtualx_TBHDX_params[] = {
    CMD_NAME_USERID_INIT("--tbhdx-front-en",                tbhdx_front_en,             PARAM_TBHDX_FRONT_ENABLE),
    CMD_NAME_USERID_INIT("--tbhdx-front-proc-mode",         tbhdx_front_proc_mode,      PARAM_TBHDX_FRONT_PROC_MODE),
    CMD_NAME_USERID_INIT("--tbhdx-front-spksize",           tbhdx_front_spksize,        PARAM_TBHDX_FRONT_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-front-dynms",             tbhdx_front_dynms,          PARAM_TBHDX_FRONT_DYNMS),
    CMD_NAME_USERID_INIT("--tbhdx-front-hp-en",             tbhdx_front_hp_en,          PARAM_TBHDX_FRONT_HP_EN),
    CMD_NAME_USERID_INIT("--tbhdx-front-hp-order",          tbhdx_front_hp_order,       PARAM_TBHDX_FRONT_HP_ORDER),
    CMD_NAME_USERID_INIT("--tbhdx-front-bass-lvl",          tbhdx_front_bass_lvl,       PARAM_TBHDX_FRONT_BASS_LVL),
    CMD_NAME_USERID_INIT("--tbhdx-front-extbass",           tbhdx_front_extbass,        PARAM_TBHDX_FRONT_EXT_BASS),
    CMD_NAME_USERID_INIT("--tbhdx-front-input-gain",        tbhdx_front_input_gain,     PARAM_TBHDX_FRONT_INPUT_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-front-bypass-gain",       tbhdx_front_bypass_gain,    PARAM_TBHDX_FRONT_BYPASS_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-rear-en",                 tbhdx_rear_en,              PARAM_TBHDX_REAR_ENABLE),
    CMD_NAME_USERID_INIT("--tbhdx-rear-proc-mode",          tbhdx_rear_proc_mode,       PARAM_TBHDX_REAR_PROC_MODE),
    CMD_NAME_USERID_INIT("--tbhdx-rear-spksize",            tbhdx_rear_spksize,         PARAM_TBHDX_REAR_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-rear-dynms",              tbhdx_rear_dynms,           PARAM_TBHDX_REAR_DYNMS),
    CMD_NAME_USERID_INIT("--tbhdx-rear-hp-en",              tbhdx_rear_hp_e,            PARAM_TBHDX_REAR_HP_EN),
    CMD_NAME_USERID_INIT("--tbhdx-rear-hp-order",           tbhdx_rear_hp_order,        PARAM_TBHDX_REAR_HP_ORDER),
    CMD_NAME_USERID_INIT("--tbhdx-rear-bass-lvl",           tbhdx_rear_bass_lvl,        PARAM_TBHDX_REAR_BASS_LVL),
    CMD_NAME_USERID_INIT("--tbhdx-rear-extbass",            tbhdx_rear_extbass,         PARAM_TBHDX_REAR_EXT_BASS),
    CMD_NAME_USERID_INIT("--tbhdx-rear-input-gain",         tbhdx_rear_input_gain,      PARAM_TBHDX_REAR_INPUT_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-rear-bypass-gain",        tbhdx_rear_bypass_gain,     PARAM_TBHDX_REAR_BYPASS_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-center-en",               tbhdx_center_en,            PARAM_TBHDX_CENTER_ENABLE),
    CMD_NAME_USERID_INIT("--tbhdx-center-dynms",            tbhdx_center_dynms,         PARAM_TBHDX_CENTER_DYNMS),
    CMD_NAME_USERID_INIT("--tbhdx-center-hp-en",            tbhdx_center_hp_en,         PARAM_TBHDX_CENTER_HP_EN),
    CMD_NAME_USERID_INIT("--tbhdx-center-hp-order",         tbhdx_center_hp_order,      PARAM_TBHDX_CENTER_HP_ORDER),
    CMD_NAME_USERID_INIT("--tbhdx-center-bass-lvl",         tbhdx_center_bass_lvl,      PARAM_TBHDX_CENTER_BASS_LVL),
    CMD_NAME_USERID_INIT("--tbhdx-center-extbass",          tbhdx_center_extbass,       PARAM_TBHDX_CENTER_EXT_BASS),
    CMD_NAME_USERID_INIT("--tbhdx-center-input-gain",       tbhdx_center_input_gain,    PARAM_TBHDX_CENTER_INPUT_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-center-bypass-gain",      tbhdx_center_bypass_gain,   PARAM_TBHDX_CENTER_BYPASS_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-surround-en",             tbhdx_surround_en,          PARAM_TBHDX_SURROUND_ENABLE),
    CMD_NAME_USERID_INIT("--tbhdx-surround-proc-mode",      tbhdx_surround_proc_mode,   PARAM_TBHDX_SURD_PROC_MODE),
    CMD_NAME_USERID_INIT("--tbhdx-surround-spksize",        tbhdx_surround_spksize,     PARAM_TBHDX_SURD_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-surround-dynms",          tbhdx_surround_dynms,       PARAM_TBHDX_SURD_DYNMS),
    CMD_NAME_USERID_INIT("--tbhdx-surround-hp-en",          tbhdx_surround_hp_en,       PARAM_TBHDX_SURD_HP_EN),
    CMD_NAME_USERID_INIT("--tbhdx-surround-hp-order",       tbhdx_surround_hp_order,    PARAM_TBHDX_SURD_HP_ORDER),
    CMD_NAME_USERID_INIT("--tbhdx-surround-bass-lvl",       tbhdx_surround_bass_lvl,    PARAM_TBHDX_SURD_BASS_LVL),
    CMD_NAME_USERID_INIT("--tbhdx-surround-extbass",        tbhdx_surround_extbass,     PARAM_TBHDX_SURD_EXT_BASS),
    CMD_NAME_USERID_INIT("--tbhdx-surround-input-gain",     tbhdx_surround_input_gain,  PARAM_TBHDX_SURD_INPUT_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-surround-bypass-gain",    tbhdx_surround_bypass_gain, PARAM_TBHDX_SURD_BYPASS_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-delay-matching-gain",     tbhdx_delay_matching_gain,  PARAM_TBHDX_DELAY_MATCH_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-discard",                 tbhdx_discard,              PARAM_TBHDX_PROCESS_DISCARD_I32),
    CMD_NAME_USERID_INIT("--tbhdx-front-app-spksize",       tbhdx_front_app_spksize,    PARAM_TBHDX_FRONT_APP_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-front-app-tgain",         tbhdx_front_app_tgain,      PARAM_TBHDX_FRONT_APP_TGAIN),
    CMD_NAME_USERID_INIT("--tbhdx-front-app-hpr",           tbhdx_front_app_hpr,        PARAM_TBHDX_FRONT_APP_HPR),
    CMD_NAME_USERID_INIT("--tbhdx-front-app-extbass",       tbhdx_front_app_extbass,    PARAM_TBHDX_FRONT_APP_EXTBASS),
    CMD_NAME_USERID_INIT("--tbhdx-center-app-spksize",      tbhdx_center_app_spksize,   PARAM_TBHDX_CENTER_APP_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-center-app-tgain",        tbhdx_center_app_tgain,     PARAM_TBHDX_CENTER_APP_TGAIN),
    CMD_NAME_USERID_INIT("--tbhdx-center-app-hpr",          tbhdx_center_app_hpr,       PARAM_TBHDX_CENTER_APP_HPR),
    CMD_NAME_USERID_INIT("--tbhdx-center-app-extbass",      tbhdx_center_app_extbass,   PARAM_TBHDX_CENTER_APP_EXTBASS),
    CMD_NAME_USERID_INIT("--tbhdx-surround-app-spksize",    tbhdx_surround_app_spksize, PARAM_TBHDX_SURD_APP_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-surround-app-tgain",      tbhdx_surround_app_tgain,   PARAM_TBHDX_SURD_APP_TGAIN),
    CMD_NAME_USERID_INIT("--tbhdx-surround-app-hpr",        tbhdx_surround_app_hpr,     PARAM_TBHDX_SURD_APP_HPR),
    CMD_NAME_USERID_INIT("--tbhdx-surround-app-extbass",    tbhdx_surround_app_extbass, PARAM_TBHDX_SURD_APP_EXTBASS),
    CMD_NAME_USERID_INIT("--tbhdx-rear-app-spksize",        tbhdx_rear_app_spksize,     PARAM_TBHDX_REAR_APP_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-rear-app-tgain",          tbhdx_rear_app_tgain,       PARAM_TBHDX_REAR_APP_TGAIN),
    CMD_NAME_USERID_INIT("--tbhdx-rear-app-hpr",            tbhdx_rear_app_hpr,         PARAM_TBHDX_REAR_APP_HPR),
    CMD_NAME_USERID_INIT("--tbhdx-rear-app-extbass",        tbhdx_rear_app_extbass,     PARAM_TBHDX_REAR_APP_EXTBASS),
};

/*** loundness control parameters ***/
enum virtualx_loundness_id {
    loudness_control_en = 0,
    loudness_control_io_mode,
    loudness_control_target_loudness,
    loudness_control_preset,
    loudness_control_latency_mode,
    loudness_control_discard,
};
static VX_Cmd_Param virtualx_loundness_params[] = {
    CMD_NAME_USERID_INIT("--loudness-control-en",               loudness_control_en,                PARAM_LOUDNESS_CONTROL_ENABLE_I32),
    CMD_NAME_USERID_INIT("--loudness-control-io-mode",          loudness_control_io_mode,           PARAM_LOUDNESS_CONTROL_IO_MODE_I32),
    CMD_NAME_USERID_INIT("--loudness-control-target-loudness",  loudness_control_target_loudness,   PARAM_LOUDNESS_CONTROL_TARGET_LOUDNESS_I32),
    CMD_NAME_USERID_INIT("--loudness-control-preset",           loudness_control_preset,            PARAM_LOUDNESS_CONTROL_PRESET_I32),
    CMD_NAME_USERID_INIT("--loudness-control-latency-mode",     loudness_control_latency_mode,      PARAM_LOUDNESS_CONTROL_LATENCY_MODE_I32),
    CMD_NAME_USERID_INIT("--loudness-control-discard",          loudness_control_discard,           PARAM_LOUDNESS_CONTROL_DISCARD),
};

/*** MBHL control parameters ***/
enum virtualx_MBHL_id{
    mbhl_en = 0,
    mbhl_discard,
    mbhl_bypass_gain,
    mbhl_reference_level,
    mbhl_volume,
    mbhl_volume_step,
    mbhl_balance_step,
    mbhl_output_gain,
    mbhl_cp_enable,
    mbhl_cp_level,
    mbhl_ac_enable,
    mbhl_ac_level,
    mbhl_boost,
    mbhl_threshold,
    mbhl_slow_offset,
    mbhl_fast_attack,
    mbhl_fast_release,
    mbhl_slow_attack,
    mbhl_slow_release,
    mbhl_delay,
    mbhl_envelope_frequency,
    mbhl_mode,
    mbhl_cross_low,
    mbhl_cross_mid,
    mbhl_comp_attacks,
    mbhl_comp_low_release,
    mbhl_comp_low_ratio,
    mbhl_comp_low_thresh,
    mbhl_comp_low_makeup,
    mbhl_comp_mid_release,
    mbhl_comp_mid_ratio,
    mbhl_comp_mid_thresh,
    mbhl_comp_mid_makeup,
    mbhl_comp_high_release,
    mbhl_comp_high_ratio,
    mbhl_comp_high_thresh,
    mbhl_comp_high_makeup,
};

VX_Cmd_Param virtualx_MBHL_params[] = {
    CMD_NAME_USERID_INIT("--mbhl-en",                   mbhl_en,                    PARAM_MBHL_ENABLE_I32),
    CMD_NAME_USERID_INIT("--mbhl-discard",              mbhl_discard,               PARAM_MBHL_PROCESS_DISCARD_I32),
    CMD_NAME_USERID_INIT("--mbhl-bypass-gain",          mbhl_bypass_gain,           PARAM_MBHL_BYPASS_GAIN_I32),
    CMD_NAME_USERID_INIT("--mbhl-reference-level",      mbhl_reference_level,       PARAM_MBHL_REFERENCE_LEVEL_I32),
    CMD_NAME_USERID_INIT("--mbhl-volume",               mbhl_volume,                PARAM_MBHL_VOLUME_I32),
    CMD_NAME_USERID_INIT("--mbhl-volume-step",          mbhl_volume_step,           PARAM_MBHL_VOLUME_STEP_I32),
    CMD_NAME_USERID_INIT("--mbhl-balance-step",         mbhl_balance_step,          PARAM_MBHL_BALANCE_STEP_I32),
    CMD_NAME_USERID_INIT("--mbhl-output-gain",          mbhl_output_gain,           PARAM_MBHL_OUTPUT_GAIN_I32),
    CMD_NAME_USERID_INIT("--mbhl-cp-en",                mbhl_cp_enable,             PARAM_MBHL_CP_ENABLE_I32),
    CMD_NAME_USERID_INIT("--mbhl-cp-level",             mbhl_cp_level,              PARAM_MBHL_CP_LEVEL),
    CMD_NAME_USERID_INIT("--mbhl-ac-en",                mbhl_ac_enable,             PARAM_MBHL_AC_ENABLE_I32),
    CMD_NAME_USERID_INIT("--mbhl-ac-level",             mbhl_ac_level,              PARAM_MBHL_AC_LEVEL),
    CMD_NAME_USERID_INIT("--mbhl-boost",                mbhl_boost,                 PARAM_MBHL_BOOST_I32),
    CMD_NAME_USERID_INIT("--mbhl-threshold",            mbhl_threshold,             PARAM_MBHL_THRESHOLD_I32),
    CMD_NAME_USERID_INIT("--mbhl-slow-offset",          mbhl_slow_offset,           PARAM_MBHL_SLOW_OFFSET_I32),
    CMD_NAME_USERID_INIT("--mbhl-fast-attack",          mbhl_fast_attack,           PARAM_MBHL_FAST_ATTACK_I32),
    CMD_NAME_USERID_INIT("--mbhl-fast-release",         mbhl_fast_release,          PARAM_MBHL_FAST_RELEASE_I32),
    CMD_NAME_USERID_INIT("--mbhl-slow-attack",          mbhl_slow_attack,           PARAM_MBHL_SLOW_ATTACK_I32),
    CMD_NAME_USERID_INIT("--mbhl-slow-release",         mbhl_slow_release,          PARAM_MBHL_SLOW_RELEASE_I32),
    CMD_NAME_USERID_INIT("--mbhl-delay",                mbhl_delay,                 PARAM_MBHL_DELAY_I32),
    CMD_NAME_USERID_INIT("--mbhl-envelope-frequency",   mbhl_envelope_frequency,    PARAM_MBHL_ENVELOPE_FREQUENCY_I32),
    CMD_NAME_USERID_INIT("--mbhl-mode",                 mbhl_mode,                  PARAM_MBHL_MODE_I32),
    CMD_NAME_USERID_INIT("--mbhl-cross-low",            mbhl_cross_low,             PARAM_MBHL_CROSS_LOW_I32),
    CMD_NAME_USERID_INIT("--mbhl-cross-mid",            mbhl_cross_mid,             PARAM_MBHL_CROSS_MID_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-attacks",         mbhl_comp_attacks,          PARAM_MBHL_COMP_ATTACK_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-low-release",     mbhl_comp_low_release,      PARAM_MBHL_COMP_LOW_RELEASE_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-low-ratio",       mbhl_comp_low_ratio,        PARAM_MBHL_COMP_LOW_RATIO_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-low-thresh",      mbhl_comp_low_thresh,       PARAM_MBHL_COMP_LOW_THRESH_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-low-makeup",      mbhl_comp_low_makeup,       PARAM_MBHL_COMP_LOW_MAKEUP_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-mid-release",     mbhl_comp_mid_release,      PARAM_MBHL_COMP_MID_RELEASE_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-mid-ratio",       mbhl_comp_mid_ratio,        PARAM_MBHL_COMP_MID_RATIO_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-mid-thresh",      mbhl_comp_mid_thresh,       PARAM_MBHL_COMP_MID_THRESH_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-mid-makeup",      mbhl_comp_mid_makeup,       PARAM_MBHL_COMP_MID_MAKEUP_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-high-release",    mbhl_comp_high_release,     PARAM_MBHL_COMP_HIGH_RELEASE_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-high-ratio",      mbhl_comp_high_ratio,       PARAM_MBHL_COMP_HIGH_RATIO_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-high-thresh",     mbhl_comp_high_thresh,      PARAM_MBHL_COMP_HIGH_THRESH_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-high-makeup",     mbhl_comp_high_makeup,      PARAM_MBHL_COMP_HIGH_MAKEUP_I32),
};

/*** AEQ control parameters ***/
enum virtualx_AEQ_id {
    aeq_enable = 0,
    aeq_app_ch_link_mask,
    aeq_ch_ctrl_mask,
    aeq_input_gain,
    aeq_output_gain,
    aeq_bypass_gain,
    aeq_discard,
    aeq_app_band_num,
    aeq_app_band_en,
    aeq_app_band_freq,
    aeq_app_band_gain,
    aeq_app_band_q,
    aeq_app_band_type
};
static VX_Cmd_Param virtualx_AEQ_params[] = {
    CMD_NAME_USERID_INIT("--aeq-enable",            aeq_enable,             PARAM_AEQ_ENABLE_I32),
    CMD_NAME_USERID_INIT("--aeq-ch-ctrl-mask",      aeq_ch_ctrl_mask,       PARAM_AEQ_CH_CTRL_MASK),
    CMD_NAME_USERID_INIT("--aeq-input-gain",        aeq_input_gain,         PARAM_AEQ_INPUT_GAIN_I16),
    CMD_NAME_USERID_INIT("--aeq-output-gain",       aeq_output_gain,        PARAM_AEQ_OUTPUT_GAIN_I16),
    CMD_NAME_USERID_INIT("--aeq-bypass-gain",       aeq_bypass_gain,        PARAM_AEQ_BYPASS_GAIN_I16),
    CMD_NAME_USERID_INIT("--aeq-discard",           aeq_discard,            PARAM_AEQ_PROCESS_DISCARD_I32),
    //Band parameters
    CMD_NAME_USERID_INIT("--aeq-app-ch-link-mask",  aeq_app_ch_link_mask,   PARAM_AEQ_CH_LINK_MASK_I32),
    CMD_NAME_USERID_INIT("--aeq-app-band-num",      aeq_app_band_num,       PARAM_AEQ_BAND_Num),
    CMD_NAME_USERID_INIT("--aeq-app-band-en",       aeq_app_band_en,        PARAM_AEQ_BAND_En),
    CMD_NAME_USERID_INIT("--aeq-app-band-freq",     aeq_app_band_freq,      PARAM_AEQ_BAND_Fre),
    CMD_NAME_USERID_INIT("--aeq-app-band-gain",     aeq_app_band_gain,      PARAM_AEQ_BAND_Gain),
    CMD_NAME_USERID_INIT("--aeq-app-band-q",        aeq_app_band_q,         PARAM_AEQ_BAND_Q),
    CMD_NAME_USERID_INIT("--aeq-app-band-type",     aeq_app_band_type,      PARAM_AEQ_BAND_type),
};

/*** AEQ control parameters ***/
enum virtualx_GEQ_id {
    geq_enable = 0,
    geq_ch_ctrl_mask,
    geq_input_gain,
    geq_band0_gain,
    geq_band1_gain,
    geq_band2_gain,
    geq_band3_gain,
    geq_band4_gain,
    geq_band5_gain,
    geq_band6_gain,
    geq_band7_gain,
    geq_band8_gain,
    geq_band9_gain,
    geq_discard,
};

static VX_Cmd_Param virtualx_GEQ_params[] = {
    CMD_NAME_USERID_INIT("--geq-enable",        geq_enable,         PARAM_GEQ_En),
    CMD_NAME_USERID_INIT("--geq-ch-ctrl-mask",  geq_ch_ctrl_mask,   PARAM_GEQ_CTRL_MASK),
    CMD_NAME_USERID_INIT("--geq-input-gain",    geq_input_gain,     PARAM_GEQ_INPUT_GAIN),
    CMD_NAME_USERID_INIT("--geq-band0-gain",    geq_band0_gain,     PARAM_GEQ_BAND0_Gain),
    CMD_NAME_USERID_INIT("--geq-band1-gain",    geq_band1_gain,     PARAM_GEQ_BAND1_Gain),
    CMD_NAME_USERID_INIT("--geq-band2-gain",    geq_band2_gain,     PARAM_GEQ_BAND2_Gain),
    CMD_NAME_USERID_INIT("--geq-band3-gain",    geq_band3_gain,     PARAM_GEQ_BAND3_Gain),
    CMD_NAME_USERID_INIT("--geq-band4-gain",    geq_band4_gain,     PARAM_GEQ_BAND4_Gain),
    CMD_NAME_USERID_INIT("--geq-band5-gain",    geq_band5_gain,     PARAM_GEQ_BAND5_Gain),
    CMD_NAME_USERID_INIT("--geq-band6-gain",    geq_band6_gain,     PARAM_GEQ_BAND6_Gain),
    CMD_NAME_USERID_INIT("--geq-band7-gain",    geq_band7_gain,     PARAM_GEQ_BAND7_Gain),
    CMD_NAME_USERID_INIT("--geq-band8-gain",    geq_band8_gain,     PARAM_GEQ_BAND8_Gain),
    CMD_NAME_USERID_INIT("--geq-band9-gain",    geq_band9_gain,     PARAM_GEQ_BAND9_Gain),
    CMD_NAME_USERID_INIT("--geq-discard",       geq_discard,        PARAM_GEQ_DISCARD),
};

/*** AEQ control parameters ***/
enum virtualx_ildetect_id {
    ildetect_upmix_en = 0,
    ildetect_upmix_low_level_threshold,
    ildetect_upmix_peak_hold_count,
    ildetect_upmix_orig_lr_mix,
    ildetect_upmix_srrnd_gain,
    ildetect_upmix_srrnd_delay_en,
    ildetect_upmix_discard,
};

static VX_Cmd_Param virtualx_ildetect_params[] = {
    CMD_NAME_USERID_INIT("--ildetect-upmix-en",                    ildetect_upmix_en,                   PARAM_IL_UPMIX_EN),
    CMD_NAME_USERID_INIT("--ildetect-upmix-low-level-threshold",   ildetect_upmix_low_level_threshold,  PARAM_IL_UPMIX_LOW_LEVEL_THRES),
    CMD_NAME_USERID_INIT("--ildetect-upmix-peak-hold-count",       ildetect_upmix_peak_hold_count,      PARAM_IL_UPMIX_PEAK_HOLD_COUNT),
    CMD_NAME_USERID_INIT("--ildetect-upmix-orig-lr-mix",           ildetect_upmix_orig_lr_mix,          PARAM_IL_UPMIX_ORIG_LR_MIX),
    CMD_NAME_USERID_INIT("--ildetect-upmix-srrnd-gain",            ildetect_upmix_srrnd_gain,           PARAM_IL_UPMIX_SRRND_GAIN),
    CMD_NAME_USERID_INIT("--ildetect-upmix-srrnd-delay-en",        ildetect_upmix_srrnd_delay_en,       PARAM_IL_UPMIX_SRRND_DELAY_EN),
    CMD_NAME_USERID_INIT("--ildetect-upmix-discard",               ildetect_upmix_discard,              PARAM_IL_UPMIX_DISCARD),
};

enum param_type_t {
    PARAM_TYPE_VIRTUALX_FORCE = 0,
    PARAM_TYPE_VIRTUALX_GENERAL,
    PARAM_TYPE_TRUSURROUNDX,
    PARAM_TYPE_TRUSURROUNDX_DC,
    PARAM_TYPE_LOUNDNESS,
    PARAM_TYPE_TBHDX,
    PARAM_TYPE_MBHL,
    PARAM_TYPE_AEQ,
    PARAM_TYPE_GEQ,
    PARAM_TYPE_ILDETECT,
    PARAM_TYPE_DEBUG,
    PARAM_TYPE_AM_UI,
    PARAM_TYPE_MAX,
};

enum vx_show_config_id {
    show_config = 0,
};

static VX_Cmd_Param vx_show_config_params[] = {
    CMD_NAME_USERID_INIT("--show-config",       show_config,     AUDIO_ALL_PARAM_DUMP),
};


enum param_type_t param_name_to_param_type(const char * paramName)
{
    if (!strncmp(paramName, "--virtualx-tsx", strlen("--virtualx-tsx")))  {
        return PARAM_TYPE_TRUSURROUNDX;
    } else if (!strncmp(paramName, "--virtualx-dialogclarity", strlen("--virtualx-dialogclarity")) ||
               !strncmp(paramName, "--virtualx-definition", strlen("--virtualx-definition"))) {
        return PARAM_TYPE_TRUSURROUNDX_DC;
    } else if (!strncmp(paramName, "--virtualx", strlen("--virtualx"))) {
        return PARAM_TYPE_VIRTUALX_GENERAL;
    } else if (!strncmp(paramName, "--tbhdx", strlen("--tbhdx")))  {
        return PARAM_TYPE_TBHDX;
    } else if (!strncmp(paramName, "--loudness", strlen("--loudness"))) {
        return PARAM_TYPE_LOUNDNESS;
    }  else if (!strncmp(paramName, "--mbhl", strlen("--mbhl"))) {
        return PARAM_TYPE_MBHL;
    } else if (!strncmp(paramName, "--aeq", strlen("--aeq"))) {
        return PARAM_TYPE_AEQ;
    } else if (!strncmp(paramName, "--geq", strlen("--geq"))) {
        return PARAM_TYPE_GEQ;
    } else if (!strncmp(paramName, "--ildetect", strlen("--ildetect"))) {
        return PARAM_TYPE_ILDETECT;
    } else if (!strncmp(paramName, "--frame-size", strlen("--frame-size")) ||
               !strncmp(paramName, "--input-pcm-stride", strlen("--input-pcm-stride")) ||
               !strncmp(paramName, "--output-pcm-stride", strlen("--output-pcm-stride"))) {
        return PARAM_TYPE_VIRTUALX_FORCE;
    } else if (!strncmp(paramName, "--show", strlen("--show"))) {
        return PARAM_TYPE_DEBUG;
    } else if (!strncmp(paramName, "--am", strlen("--am"))) {
        return PARAM_TYPE_AM_UI;
    }
    else {
        return PARAM_TYPE_MAX;
    }
}

enum param_type_t user_id_to_param_type(int userId)
{
    if (userId == AUDIO_ALL_PARAM_DUMP) {
        return PARAM_TYPE_DEBUG;
    } else if ((userId >= PARAM_MBHL_ENABLE_I32) && (userId < PARAM_MBHL_PARAM_MAX)) {
        return PARAM_TYPE_MBHL;
    } else if ((userId > PARAM_TBHDX_PARAM_START) && (userId < PARAM_TBHDX_PARAM_MAX)) {
        return PARAM_TYPE_TBHDX;
    }else if ((userId > PARAM_VX_FORCE_PARAM_START) && (userId < PARAM_VX_FORCE_PARAM_MAX)) {
        return PARAM_TYPE_VIRTUALX_FORCE;
    } else if ((userId > PARAM_VX_GENERAL_PARAM_START) && (userId < PARAM_VX_GENERAL_PARAM_MAX)) {
        return PARAM_TYPE_VIRTUALX_GENERAL;
    } else if ((userId > PARAM_TSX_PARAM_START) && (userId < PARAM_TSX_PARAM_MAX)) {
        return PARAM_TYPE_TRUSURROUNDX;
    } else if ((userId > PARAM_VX_DC_PARAM_START) && (userId < PARAM_VX_DC_PARAM_MAX)) {
        return PARAM_TYPE_TRUSURROUNDX_DC;
    } else if ((userId > PARAM_LOUDNESS_PARAM_START) && (userId < PARAM_LOUDNESS_PARAM_MAX)) {
        return PARAM_TYPE_LOUNDNESS;
    } else if ((userId > PARAM_AEQ_PARAM_START) && (userId < PARAM_AEQ_PARAM_MAX)) {
        return PARAM_TYPE_AEQ;
    } else if ((userId > PARAM_GEQ_PARAM_START) && (userId < PARAM_GEQ_PARAM_MAX)) {
        return PARAM_TYPE_GEQ;
    } else if ((userId > PARAM_IL_PARAM_START) && (userId < PARAM_IL_PARAM_MAX)) {
        return PARAM_TYPE_ILDETECT;
    }
    return PARAM_TYPE_MAX;
}

int get_vx_cmd_table_from_paramType(VX_Cmd_Param **table, int *size, int paramType)
{
    if (paramType == PARAM_TYPE_MAX) {
        ALOGE("%s() Invalid paramType:%d", __func__, paramType);
        return -BAD_VALUE;
    }

    switch (paramType) {
    case PARAM_TYPE_VIRTUALX_FORCE:
       *table = virtualx_force_params;
       *size = AM_ARRAY_SIZE(virtualx_force_params);
        break;
    case PARAM_TYPE_VIRTUALX_GENERAL:
        *table = virtualx_general_params;
        *size = AM_ARRAY_SIZE(virtualx_general_params);
        break;
    case PARAM_TYPE_TRUSURROUNDX:
        *table = virtualx_truSurroundX_params;
        *size = AM_ARRAY_SIZE(virtualx_truSurroundX_params);
        break;
    case PARAM_TYPE_TRUSURROUNDX_DC:
        *table = virtualx_truSurroundX_DC_params;
        *size = AM_ARRAY_SIZE(virtualx_truSurroundX_DC_params);
        break;
    case PARAM_TYPE_LOUNDNESS:
        *table = virtualx_loundness_params;
        *size = AM_ARRAY_SIZE(virtualx_loundness_params);
        break;
    case PARAM_TYPE_TBHDX:
        *table = virtualx_TBHDX_params;
        *size = AM_ARRAY_SIZE(virtualx_TBHDX_params);
        break;
    case PARAM_TYPE_MBHL:
        *table = virtualx_MBHL_params;
        *size = AM_ARRAY_SIZE(virtualx_MBHL_params);
        break;
    case PARAM_TYPE_AEQ:
        *table = virtualx_AEQ_params;
        *size = AM_ARRAY_SIZE(virtualx_AEQ_params);
        break;
    case PARAM_TYPE_GEQ:
        *table = virtualx_GEQ_params;
        *size = AM_ARRAY_SIZE(virtualx_GEQ_params);
        break;
    case PARAM_TYPE_ILDETECT:
        *table = virtualx_ildetect_params;
        *size = AM_ARRAY_SIZE(virtualx_ildetect_params);
        break;
    case PARAM_TYPE_DEBUG:
        *table = vx_show_config_params;
        *size = AM_ARRAY_SIZE(vx_show_config_params);
        break;
    default:
        ALOGE("%s() unknown param_type:%d",__func__, paramType);
        return -BAD_VALUE;
    }
    return 0;
}

int get_userID_from_param_name(const char *paramName)
{
    if (!paramName) {
        ALOGE("%s() param name = NULL!", __func__);
        return -1;
    }

    enum param_type_t paramType = param_name_to_param_type(paramName);
    if (paramType == PARAM_TYPE_MAX) {
        ALOGE("%s() can not get param type from paramName:%s", __func__, paramName);
        return -1;
    }

    VX_Cmd_Param *vxCmdTable = NULL;
    int tableSize = 0;
    int userId = -1;
    int ret = 0;

    ret = get_vx_cmd_table_from_paramType(&vxCmdTable, &tableSize, paramType);
    if (ret < 0 || !vxCmdTable || !tableSize) {
        ALOGW("%s() Not find VX_Cmd_Param table", __func__);
        return -1;
    }

    for (int i = 0; i < tableSize; i++) {
        VX_Cmd_Param *temp = &vxCmdTable[i];
        if (strncmp(temp->param, paramName, strlen(temp->param)) == 0) {
            userId = temp->userId;
            break;
        }
    }
    return userId;
}

VX_Cmd_Param * param_index_to_vx_cmd(int userId)
{
    VX_Cmd_Param *vxCmdTable = NULL;
    int tableSize = 0;
    VX_Cmd_Param *retVxCmd = NULL;
    int ret  = 0;
    int paramType = user_id_to_param_type(userId);

    ret = get_vx_cmd_table_from_paramType(&vxCmdTable, &tableSize, paramType);
    if (ret < 0 || !vxCmdTable || !tableSize) {
        ALOGW("%s() Not find VX_Cmd_Param table", __func__);
        return NULL;
    }

    for (int i = 0; i < tableSize; i++) {
        VX_Cmd_Param *temp = &vxCmdTable[i];
        if (temp->userId == userId) {
            retVxCmd = temp;
            break;
        }
    }
    //ALOGI("%s() userId:%d paramType:%d vxCmd:%p",__func__,userId, paramType, retVxCmd);
    return retVxCmd;
}

static inline void clear_VX_Cmd_Params_group(VX_Cmd_Param *params, int count)
{
    for (int i = 0; i < count; i++) {
        VX_Cmd_Param *temp = &params[i];
        temp->mode = PARAM_DEFAULT_MODE;
        temp->count = 0;
        temp->value = NULL;
        for (int j =0; j < VX_INNER_MODE_MAX; j++) {
            temp->value_store[j][0] = 0;
        }
    }
}

void clear_all_VX_Cmd_Params()
{
    clear_VX_Cmd_Params_group(virtualx_force_params, AM_ARRAY_SIZE(virtualx_force_params));
    clear_VX_Cmd_Params_group(virtualx_general_params, AM_ARRAY_SIZE(virtualx_general_params));
    clear_VX_Cmd_Params_group(virtualx_truSurroundX_params, AM_ARRAY_SIZE(virtualx_truSurroundX_params));
    clear_VX_Cmd_Params_group(virtualx_truSurroundX_DC_params, AM_ARRAY_SIZE(virtualx_truSurroundX_DC_params));
    clear_VX_Cmd_Params_group(virtualx_TBHDX_params, AM_ARRAY_SIZE(virtualx_TBHDX_params));
    clear_VX_Cmd_Params_group(virtualx_loundness_params, AM_ARRAY_SIZE(virtualx_loundness_params));
    clear_VX_Cmd_Params_group(virtualx_MBHL_params, AM_ARRAY_SIZE(virtualx_MBHL_params));
    clear_VX_Cmd_Params_group(virtualx_AEQ_params, AM_ARRAY_SIZE(virtualx_AEQ_params));
    clear_VX_Cmd_Params_group(virtualx_GEQ_params, AM_ARRAY_SIZE(virtualx_GEQ_params));
    clear_VX_Cmd_Params_group(virtualx_ildetect_params, AM_ARRAY_SIZE(virtualx_ildetect_params));
    ALOGI("%s() done", __func__);
}


int get_show_config_CMDs(VX_Cmd_Param **table)
{
    int pos = 0;
    table[pos++] =  SET_PARAM_BY_KEY(vx_show_config_params, show_config, "1");
    return pos;
}

/*** make user specified control APIs ***/
int get_default_global_CMDs(VX_Cmd_Param **table)
{
    int pos = 0;
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_general_params, VIRTUALX_en, "1");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_general_params, VIRTUALX_in_mode, "2.0");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_general_params, VIRTUALX_out_mode, "2.0");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_general_params, VIRTUALX_headroom_gain, "1.0");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_general_params, VIRTUALX_processing_output_gain, "0.5");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_general_params, VIRTUALX_reference_level, "1.0");

    /*TSX*/
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_en, "1");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_pssv_mtrx_en, "1");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_horiznt_effect_ctrl, "1");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_phantom_ctrgain, "1.5");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_ctrgain, "0.25");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_heightmix_coeff, "1.0");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_height_out_gain, "0.5");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_precond_front, "1.0");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_precond_surnd, "1.0");
    //table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_vxtopspk_loc, );
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_frnt_wide_en, "1");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_hght_virtualizer_en, "1");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_frnt_srnd_en, "1");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_hght_upmix_en, "1");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_lrmix_ratio2ctr, "1.0");
    //..
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_general_params, VIRTUALX_discard, "0");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_DC_params, VIRTUALX_dialogclarity_en, "1");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_DC_params, VIRTUALX_dialogclarity_level, "0.2");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_DC_params, VIRTUALX_definition_en, "1");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_DC_params, VIRTUALX_definition_lvl, "1.0");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_truSurroundX_DC_params, VIRTUALX_cs2to3_en, "1");
    /* normal*/
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_force_params, VX_frame_size, "256");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_force_params, VX_input_pcm_stride, "1");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_force_params, VX_output_pcm_stride, "1");
    return pos;
}

int get_VirtualXModeOn_CMDs(VX_Cmd_Param **table)
{
    int pos = 0;
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_general_params, VIRTUALX_en, "1");
    return pos;
};

int  get_VirtualXModeOff_CMDs(VX_Cmd_Param **table) {
    int pos = 0;
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_general_params, VIRTUALX_en, "0");
    return pos;
}

int get_SurroundModeOn_CMDs(VX_Cmd_Param **table) {
    int pos = 0;
    table[pos++] = SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_en, "1");
    return pos;
}

int  get_SurroundModeOff_CMDs(VX_Cmd_Param **table) {
    int pos = 0;
    table[pos++] = SET_PARAM_BY_KEY(virtualx_truSurroundX_params, VIRTUALX_tsx_en, "0");
    return pos;
}

int get_DialogClarityOff_CMDs(VX_Cmd_Param **table) {
    int pos = 0;
    table[pos++] = SET_PARAM_BY_KEY(virtualx_truSurroundX_DC_params, VIRTUALX_dialogclarity_en, "0");
    return pos;
}

int get_DialogClarityLow_CMDs(VX_Cmd_Param **table)
{
    int pos = 0;
    table[pos++] = SET_PARAM_BY_KEY(virtualx_truSurroundX_DC_params, VIRTUALX_dialogclarity_en, "1");
    table[pos++] = SET_PARAM_BY_KEY(virtualx_truSurroundX_DC_params, VIRTUALX_dialogclarity_level, "0.4");
    return pos;
}

int get_DialogClarityHigh_CMDs(VX_Cmd_Param **table)
{
    int pos = 0;
    table[pos++] = SET_PARAM_BY_KEY(virtualx_truSurroundX_DC_params, VIRTUALX_dialogclarity_en, "1");
    table[pos++] = SET_PARAM_BY_KEY(virtualx_truSurroundX_DC_params, VIRTUALX_dialogclarity_level, "1.0");
    return pos;
}

int get_TruVolumeOn_CMDs(VX_Cmd_Param **table)
{
    int pos = 0;
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_loundness_params, loudness_control_discard, "0");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_loundness_params, loudness_control_en, "1");
    return pos;
}

int get_TruVolumeOff_CMDs(VX_Cmd_Param **table)
{
    int pos = 0;
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_loundness_params, loudness_control_discard, "1");
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_loundness_params, loudness_control_en, "0");
    return pos;
}

int get_MbhlMode_CMDs_by_int(VX_Cmd_Param **table, int value)
{
    int pos = 0;
    char iBuff[8] = {0};
    snprintf(iBuff, 8, "%d", value);
    table[pos++] =  SET_PARAM_BY_KEY(virtualx_MBHL_params, mbhl_mode, iBuff);
    return pos;
}

int get_Loudness_ctl_io_mode_CMDs_by_int(VX_Cmd_Param **table, int value)
{
    int pos = 0;
    char iBuff[8] = {0};
    snprintf(iBuff, 8, "%d", value);
    table[pos++] = SET_PARAM_BY_KEY(virtualx_loundness_params, loudness_control_io_mode, iBuff);
    return pos;
}

int get_AEQ_enable_CMDs_by_int(VX_Cmd_Param **table, int value)
{
    int pos = 0;
    char iBuff[8] = {0};
    snprintf(iBuff, 8, "%d", value);
    table[pos++] = SET_PARAM_BY_KEY(virtualx_AEQ_params, aeq_enable, iBuff);
    return pos;
}

static int Virtualx_get_model_name(char *model_name, int size)
{
    int ret = -1;
    char node[PROPERTY_VALUE_MAX];

    ret = property_get("vendor.tv.model_name", node, NULL);

    if (ret < 0)
        snprintf(model_name, size, "FHD");
    else
        snprintf(model_name, size, "%s", node);

    ALOGD("%s: Model Name -> %s", __FUNCTION__, model_name);

    return ret;
}

static int Virtualx_get_ini_file(char *ini_name, int size)
{
    int result = -1;
    char model_name[50] = {0};
    IniParser* pIniParser = NULL;
    const char *ini_value = NULL;
    const char *filename = MODEL_SUM_DEFAULT_PATH;

    Virtualx_get_model_name(model_name, sizeof(model_name));
    pIniParser = new IniParser();
    if (pIniParser->parse(filename) < 0) {
        ALOGW("%s: Load INI file -> %s Failed", __FUNCTION__, filename);
        goto exit;
    }

    ini_value = pIniParser->GetString(model_name, "AMLOGIC_AUDIO_EFFECT_INI_PATH", "null");
    if (ini_value == NULL || access(ini_value, F_OK) == -1) {
        ALOGD("%s: INI File is not exist", __FUNCTION__);
        goto exit;
    }

    ALOGD("%s: INI File -> %s", __FUNCTION__, ini_value);
    strncpy(ini_name, ini_value, size);

    result = 0;

exit:
    delete pIniParser;
    pIniParser = NULL;
    return result;
}

void add_prepared_param_to_vx_data(vxdata *data, VX_Cmd_Param *cmdParam)
{
    if (!data || !cmdParam) {
        ALOGE("%s() Invalid data:%p cmdParam:%p",__func__, data, cmdParam);
        return;
    }

    bool found = false;
    for (int i = 0; i < data->vxParamCount; i++) {
        const VX_Cmd_Param *temp = data->vxConfigCmds[i];
        if (temp == cmdParam) {
            found = true;
            break;
        }
    }

    if (!found) {
        data->vxConfigCmds[data->vxParamCount++] = cmdParam;
    }
}

static int set_vx_param_by_cmd_ptrs(vxContext *pContext, VX_Cmd_Param **cmd_table, int count)
{
    char **param_argv = NULL;
    int param_argc = 0;
    int ret = -EINVAL;

    param_argv = make_cmd_list_from_VX_CMDs(cmd_table, count, &param_argc);
    if (param_argv && param_argc) {
        ret = (pContext->gVirtualxapi.vx_v4_set_param)(pContext->vxInst, param_argc, param_argv);
    }

    ALOGD("set_vx_params() argc = %d  argv:", param_argc);
    for (int i = 0; i < param_argc; i++) {
        ALOGD("set_vx_params() argv[%d] = %s", i, param_argv[i]);
    }
    return ret;
}

int do_vx_setting_for_mode(vxContext *pContext, int mode)
{
    vxdata *pData = &pContext->gvxdata;
    VX_Cmd_Param *vxChangeCmds[MAX_VX_PARAM_COUNT] = {NULL};
    int vxChangeNum = 0;
    int old_mode = pData->mode;
    int ret = 0;
    int found = 0;

    ALOGW("+%s() mode:%d",__func__, mode);

    if (!pData->enable || !pContext->gVXLibHandler) {
        ALOGW("%s() Fail! enable:%d vxHandler:%p return!", __func__, pData->enable, pContext->gVXLibHandler);
        return -1;
    }

    if (mode == old_mode) {
        ALOGW("%s() mode:%d not changed, do nothing!", __func__, mode);
        return -1;
    }

    //whether system has loaded request ini by mode
    for (int i = 0; i < pData->ini_count; i++) {
        if (pData->ini_mode[i] == mode) {
            found = 1;
            break;
        }
    }
    if (!found) {
        ALOGW("%s() Warning! INI not loaded for Mode:%d", __func__, mode);
        return -1;
    }

    for (int i = 0; i < pData->vxParamCount; i++) {
        VX_Cmd_Param *tmpVxCmd = pData->vxConfigCmds[i];
        if ((tmpVxCmd->value_store[mode][0] != 0) &&
            (strncmp(tmpVxCmd->value_store[old_mode], tmpVxCmd->value_store[mode], MAX_PARAM_VALUE_LEN) != 0)) {
            SWITCH_CMD_PARAM_WITH_MODE(tmpVxCmd, mode);
            vxChangeCmds[vxChangeNum++] = tmpVxCmd;
        }
    }

    if (vxChangeNum == 0) {
        ALOGW("%s() Not found VX_Cmd change From old_mode:%d to new_mode:%d", __func__, old_mode, mode);
        return 0;
    }

    ret = set_vx_param_by_cmd_ptrs(pContext, vxChangeCmds, vxChangeNum);
    pData->mode = (VirtualX_InnerMode_t) mode;
    ALOGW("-%s() mode:%d old_mode:%d ParamCount:%d", __func__, mode, old_mode, vxChangeNum);
    return ret;
}


/*
    function: parse parameter's value from INI by prefix: label
    label: VX sub module name which defined in INI file
*/
static int parse_vx_param_by_table(vxContext *pContext,
                                  IniParser* pIniParser,
                                  const char *label,
                                  VX_Cmd_Param *table,
                                  int table_size,
                                  int mode)
{
#ifdef DEBUG_VX_LABEL
    int match_count = 0;
    VX_Cmd_Param *matchVXCmds[MAX_VX_PARAM_COUNT] = {NULL};
#endif

    if (!pContext || !label || !pIniParser || !table || !table_size || (mode > VX_INNER_MODE_MAX)) {
        ALOGE("%s() Invalid parameters, return BAD_VAL", __func__);
        return -BAD_VALUE;
    }

    for (int i = 0; i < table_size; i++) {
        VX_Cmd_Param *temp = &table[i];
        const char* ini_value =  pIniParser->GetString(label, temp->param, "");
        int value_len = strlen(ini_value);
        if (!ini_value || (value_len < 1 || value_len >= MAX_PARAM_VALUE_LEN)) {
#ifdef DEBUG_VX_LABEL
            ALOGW("%s() Invalid value %s = %s ", __func__, temp->param, ini_value);
#endif
            continue;
        }

        STORE_CMD_PARAM_FOR_MODE(temp, ini_value, mode);

        add_prepared_param_to_vx_data(&pContext->gvxdata, temp);

#ifdef DEBUG_VX_LABEL
        //only add sub module already initial cmds
        matchVXCmds[match_count++] = temp;
#endif
    }

#ifdef DEBUG_VX_LABEL
    VX_Params_Print_label(label, matchVXCmds, match_count);
#endif
    return 0;
}

int load_vx_ini(vxContext *pContext, int mode, const char *path, const char *name)
{
    char ini_path_name[128] = {0};
    int result = 0;
    vxdata *pData = &pContext->gvxdata;

    snprintf(ini_path_name, 128, "%s/%s", path, name);

    IniParser* pIniParser = new IniParser();
    if (!pIniParser) {
        ALOGE("%s() pIniParser is NULL!", __func__);
        return -1;
    }
    if (pIniParser->parse((const char *)ini_path_name) < 0) {
        ALOGE("%s() [%s] load Fail!", __FUNCTION__, name);
        delete pIniParser;
        return -1;
    }

    //virtualx general parse
    result = parse_vx_param_by_table(pContext, pIniParser, "VX_CLI_PARAM",
                                    virtualx_general_params,
                                    AM_ARRAY_SIZE(virtualx_general_params),
                                    mode);
    if (result < 0) {
        ALOGE("%s parse [VX_CLI_PARAM] fail!", __func__);
        goto error;
    }

    //virtualx TSX general parse
    result = parse_vx_param_by_table(pContext, pIniParser, "VX_TSX_CLI_PARAM",
                                    virtualx_truSurroundX_params,
                                    AM_ARRAY_SIZE(virtualx_truSurroundX_params),
                                    mode);
    if (result < 0) {
        ALOGE("%s parse [VX_TSX_CLI_PARAM] fail!", __func__);
        goto error;
    }

    //virtualx DC parse
    result = parse_vx_param_by_table(pContext, pIniParser, "VX_DC_CLI_PARAM",
                                    virtualx_truSurroundX_DC_params,
                                    AM_ARRAY_SIZE(virtualx_truSurroundX_DC_params),
                                    mode);
    if (result < 0) {
        ALOGE("%s parse [VX_DC_CLI_PARAM] fail!", __func__);
        goto error;
    }

    //tbhd parse
    result = parse_vx_param_by_table(pContext, pIniParser, "TBHD_CLI_PARAM",
                                    virtualx_TBHDX_params,
                                    AM_ARRAY_SIZE(virtualx_TBHDX_params),
                                    mode);
    if (result < 0) {
        ALOGE("%s parse [TBHD_CLI_PARAM] fail!", __func__);
        goto error;
    }

    //mbhl parse
    result = parse_vx_param_by_table(pContext, pIniParser, "MBHL_CLI_PARAM",
                                    virtualx_MBHL_params,
                                    AM_ARRAY_SIZE(virtualx_MBHL_params),
                                    mode);
    if (result < 0) {
        ALOGE("%s [parse MBHL_CLI_PARAM] fail!", __func__);
        goto error;
    }

    //loundness parse
    result = parse_vx_param_by_table(pContext, pIniParser, "TVHD_CLI_PARAM",
                                    virtualx_loundness_params,
                                    AM_ARRAY_SIZE(virtualx_loundness_params),
                                    mode);
    if (result < 0) {
        ALOGE("%s [parse TVHD_CLI_PARAM] fail!", __func__);
        goto error;
    }

    //AEQ parse
    result = parse_vx_param_by_table(pContext, pIniParser, "AEQ_CLI_PARAM",
                                    virtualx_AEQ_params,
                                    AM_ARRAY_SIZE(virtualx_AEQ_params),
                                    mode);
    if (result < 0) {
        ALOGE("%s [parse AEQ_CLI_PARAM] fail!", __func__);
        goto error;
    }

    //GEQ parse
    result = parse_vx_param_by_table(pContext, pIniParser, "GEQ_CLI_PARAM",
                                    virtualx_GEQ_params,
                                    AM_ARRAY_SIZE(virtualx_GEQ_params),
                                    mode);
    if (result < 0) {
        ALOGE("%s [parse GEQ_CLI_PARAM] fail!", __func__);
        goto error;
    }

    //ILDETECT parse
    result = parse_vx_param_by_table(pContext, pIniParser, "ILD_CLI_PARAM",
                                    virtualx_ildetect_params,
                                    AM_ARRAY_SIZE(virtualx_ildetect_params),
                                    mode);
    if (result < 0) {
        ALOGE("%s [parse ILD_CLI_PARAM] fail!", __func__);
        goto error;
    }

#ifdef DEBUG_VX_INI
    VX_Params_Print_mode(name, pData->vxConfigCmds, pData->vxParamCount, mode);
#endif

error:
    if (result == 0) {
        pData->ini_mode[pData->ini_count] = mode;
        pData->ini_count++;
    }
    ALOGD("%s() [%s] load %s, mode:%d, ParamCount:%d", __func__, name, (!result ? "Successful" : "Failed"), mode, pData->vxParamCount);
    delete pIniParser;
    pIniParser = NULL;
    return result;
}

static int Virtualx_load_ini_file(vxContext *pContext)
{
    enum {
            MIN_INI_FILE_NAME_LEN = 4,
            MAX_INI_FILE_NAME_LEN = 32,
    };
    int result = -1;
    int vx_ini_num = 0;
    char ini_name[100] = {0};
    const char *ini_value = NULL;
    char *endlabel = NULL;
    vxdata *data = &pContext->gvxdata;
    IniParser* pIniParser = NULL;
    char vx_ini_list[VX_INNER_MODE_MAX][MAX_INI_FILE_NAME_LEN];
    int vx_ini_count = 0;

    if (Virtualx_get_ini_file(ini_name, sizeof(ini_name)) < 0) {
        goto _error_exit;
    }
    pIniParser = new IniParser();
    if (pIniParser->parse((const char *)ini_name) < 0) {
        ALOGD("%s: %s load failed", __FUNCTION__, ini_name);
        goto _error_exit;
    }

    //parse common parameters
    ini_value = pIniParser->GetString("Virtualx", "enable", "1");
    if (ini_value == NULL) {
        goto _error_exit;
    }
    data->enable = atoi(ini_value);
    ALOGD("%s: enable -> %d", __FUNCTION__, data->enable);

    clear_all_VX_Cmd_Params();

    //parse user specified VX INI file name list
    for (int vx_mode = 0; vx_mode < VX_INNER_MODE_MAX; vx_mode++) {
        int len = 0;
        char ini_mode[MAX_INI_FILE_NAME_LEN] = {0};
        snprintf(ini_mode, MAX_INI_FILE_NAME_LEN, "%s_%d", "ini_mode", vx_mode);

        ini_value =  pIniParser->GetString("VX_INI_LIST", ini_mode, "");
        if (!ini_value) {
            ALOGW("%s() Invalid ini_value is NULL for %s on [VX_INI_LIST] ", __func__, ini_mode);
            continue;
        }
        len = strlen(ini_value);
        if (len < MIN_INI_FILE_NAME_LEN || len > MAX_INI_FILE_NAME_LEN) {
            ALOGD("%s() Not defined %s on [VX_INI_LIST]", __func__, ini_mode);
            continue;
        }

        strncpy(vx_ini_list[vx_ini_count++], ini_value, MAX_INI_FILE_NAME_LEN);
        ALOGI("%s() %s = %s",__func__, ini_mode, ini_value);
    }
    if (vx_ini_count == 0) {
        ALOGE("%s() Error, Not defined vx_ini on [VX_INI_LIST]!", __func__);
        result = -1;
        goto _error_exit;
    }

    //parse the ini file one by one
    endlabel = strrchr(ini_name, '/');
    ini_name[endlabel - ini_name] = 0;
    for (int i = 0; i < vx_ini_count; i++) {
        result = load_vx_ini(pContext, i, ini_name, vx_ini_list[i]);
        if (result < 0) {
            break;
        } else {
            vx_ini_num++;
        }
    }

_error_exit:
    /* if parser ini failed, use default value */
    ALOGD("%s() Successfully load ini_file count:%d", __FUNCTION__, vx_ini_num);
    delete pIniParser;
    pIniParser = NULL;
    return 0;
}

static int Virtualx_load_lib(vxContext *pContext)
{
    pContext->gVXLibHandler = dlopen(LIBVX_PATH_A, RTLD_NOW);
    if (!pContext->gVXLibHandler) {
        ALOGE("%s: dlopen failed", __func__);
        return -EINVAL;
    }

    pContext->gVirtualxapi.vx_v4_init = (int (*)(void **, void *, int, char **)) dlsym(pContext->gVXLibHandler, "VX_V4_init");
    if (!pContext->gVirtualxapi.vx_v4_init) {
        ALOGE("%s: find func vx_v4_init() failed\n", __func__);
        goto Virtualx_load_lib_error;
    }

    pContext->gVirtualxapi.vx_v4_process = (int (*)(int **, int **)) dlsym(pContext->gVXLibHandler, "VX_V4_process");
    if (!pContext->gVirtualxapi.vx_v4_process) {
        ALOGE("%s: find func vx_v4_process() failed\n", __func__);
        goto Virtualx_load_lib_error;
    }

    pContext->gVirtualxapi.vx_v4_release = (int (*)(void *)) dlsym(pContext->gVXLibHandler, "VX_V4_release");
    if (!pContext->gVirtualxapi.vx_v4_release) {
        ALOGE("%s: find func vx_v4_release() failed\n", __func__);
        goto Virtualx_load_lib_error;
    }

    pContext->gVirtualxapi.vx_v4_reset = (int (*)(void *)) dlsym(pContext->gVXLibHandler, "VX_V4_reset");
    if (!pContext->gVirtualxapi.vx_v4_reset) {
        ALOGE("%s: find func vx_v4_reset() failed\n", __func__);
        goto Virtualx_load_lib_error;
    }

    pContext->gVirtualxapi.vx_v4_set_param = (int (*)(void *, int, char **)) dlsym(pContext->gVXLibHandler, "VX_V4_set_param");
    if (!pContext->gVirtualxapi.vx_v4_set_param) {
        ALOGE("%s: find func vx_v4_set_param() failed\n", __func__);
        goto Virtualx_load_lib_error;
    }

    pContext->gVirtualxapi.vx_v4_set_inmode = (int (*)(void *, int)) dlsym(pContext->gVXLibHandler, "VX_V4_set_inmode");
    if (!pContext->gVirtualxapi.vx_v4_set_inmode) {
        ALOGE("%s: find func vx_v4_set_inmode() failed\n", __func__);
        goto Virtualx_load_lib_error;
    }

    pContext->gVirtualxapi.vx_v4_set_outmode = (int (*)(void *, int)) dlsym(pContext->gVXLibHandler, "VX_V4_set_outmode");
    if (!pContext->gVirtualxapi.vx_v4_set_outmode) {
        ALOGE("%s: find func vx_v4_set_outmode() failed\n", __func__);
        goto Virtualx_load_lib_error;
    }

    ALOGI("%s: successful", __func__);
    return 0;

Virtualx_load_lib_error:
    memset(&pContext->gVirtualxapi, 0, sizeof(pContext->gVirtualxapi));
    dlclose(pContext->gVXLibHandler);
    pContext->gVXLibHandler = NULL;
    return -EINVAL;
}

static int unload_Virtualx_lib(vxContext *pContext)
{
    memset(&pContext->gVirtualxapi, 0, sizeof(pContext->gVirtualxapi));
    if (pContext->gVXLibHandler) {
        dlclose(pContext->gVXLibHandler);
        pContext->gVXLibHandler = NULL;
    }
    return 0;
}


static int Virtualx_init(vxContext *pContext)
{
    vxdata *data = &pContext->gvxdata;
    int rc = 0;

    pContext->config.inputCfg.accessMode = EFFECT_BUFFER_ACCESS_READ;
    pContext->config.inputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
    pContext->config.inputCfg.format = AUDIO_FORMAT_PCM_32_BIT;
    pContext->config.inputCfg.samplingRate = 48000;
    pContext->config.inputCfg.bufferProvider.getBuffer = NULL;
    pContext->config.inputCfg.bufferProvider.releaseBuffer = NULL;
    pContext->config.inputCfg.bufferProvider.cookie = NULL;
    pContext->config.inputCfg.mask = EFFECT_CONFIG_ALL;
    pContext->config.outputCfg.accessMode = EFFECT_BUFFER_ACCESS_ACCUMULATE;
    pContext->config.outputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
    pContext->config.outputCfg.format = AUDIO_FORMAT_PCM_32_BIT;
    pContext->config.outputCfg.samplingRate = 48000;
    pContext->config.outputCfg.bufferProvider.getBuffer = NULL;
    pContext->config.outputCfg.bufferProvider.releaseBuffer = NULL;
    pContext->config.outputCfg.bufferProvider.cookie = NULL;
    pContext->config.outputCfg.mask = EFFECT_CONFIG_ALL;

    //malloc memory for ppMappedInCh[3]~ppMappedInCh[11] to fix crash (null pointer dereference)
    for (int i = 0; i < VIRTUALX_MAX_IN_CHANNELS; i++) {
        pContext->ppMappedInCh[i]  = pContext->MappedInTempBuffer[i];
        pContext->ppMappedOutCh[i] = pContext->MappedOutTempBuffer[i];
    }

    /* config VX library work parameters */
    if (pContext->gVXLibHandler) {
        if (data->vxParamCount > 0) {
            char **param_argv = NULL;
            int param_argc = 0;
            VX_Cmd_Param *defaultVxCMDs[MAX_VX_PARAM_COUNT] = {0};
            int default_count =0;
            //select valid parameters from vxConfigCmds
            for (int i = 0; i < data->vxParamCount; i++) {
                VX_Cmd_Param *tempVxCmd = data->vxConfigCmds[i];
                if (tempVxCmd->value && tempVxCmd->value[0] != 0) {
                    defaultVxCMDs[default_count++] = tempVxCmd;
                }
            }

            param_argv = make_cmd_list_from_VX_CMDs(defaultVxCMDs, default_count, &param_argc);
            rc = (pContext->gVirtualxapi.vx_v4_init)(&pContext->vxInst, NULL, param_argc, param_argv);

#ifdef DEBUG_VX
            vx_config_str_print(param_argc, param_argv);
#endif
            data->mode = VX_INNER_MODE_0;
            ALOGI("%s() config libvx with INI, vxParamCount:%d argc:%d\n",__func__, data->vxParamCount, param_argc);
        } else {
            char **param_argv = NULL;
            int param_argc = 0;
            VX_Cmd_Param *defaultVxCMDs[50] = {NULL};
            int count = get_default_global_CMDs(defaultVxCMDs);
            param_argv = make_cmd_list_from_VX_CMDs(defaultVxCMDs, count, &param_argc);
            rc = (pContext->gVirtualxapi.vx_v4_init)(&pContext->vxInst, NULL, param_argc, param_argv);
#ifdef DEBUG_VX
            vx_config_str_print(param_argc, param_argv);
#endif
            ALOGI("%s() config VX with default, argc:%d\n", __func__, param_argc);
        }

        if (rc != 0) {
            ALOGE("%s() config VX failed:%d", __func__, rc);
            return rc;
        }
    }

    pContext->effect_ch_in = 2;
    pContext->effect_ch_out = 2;
    pContext->left_bytes = 0;
    pContext->left_process_bytes = 0;

    //set DTS X working on auto mode when load libvx OK and vx processing flag is enabled in INI
    if (pContext->gVXLibHandler && data->enable) {
        /* set dts decoder output mode: */
        /* mode 0: auto output depending stream content */
        /* mode 2: force 2ch output */
        setParameters(String8("VX_SET_DTS_Mode=0"));
    } else {
        setParameters(String8("VX_SET_DTS_Mode=2"));
    }

    ALOGD("%s() mode:%d successful", __FUNCTION__, data->mode);
    return 0;
}

static int Virtualx_reset(vxContext *pContext)
{
    /* Don't reset all, only clear buffer */
    if (pContext->gVXLibHandler) {
        (*pContext->gVirtualxapi.vx_v4_reset)(pContext->vxInst);
    }
    memset(pContext->MappedInTempBuffer, 0, sizeof(int32_t) * VIRTUALX_MAX_IN_CHANNELS * VIRTUALX_FRAME_SIZE);
    memset(pContext->MappedOutTempBuffer, 0, sizeof(int32_t) * VIRTUALX_MAX_IN_CHANNELS * VIRTUALX_FRAME_SIZE);
    pContext->left_bytes = 0;
    pContext->left_process_bytes = 0;
    return 0;
}

static int Virtualx_configure(vxContext *pContext, effect_config_t *pConfig)
{
    if (pConfig->inputCfg.samplingRate != pConfig->outputCfg.samplingRate)
        return -EINVAL;

    if (pConfig->inputCfg.format != pConfig->outputCfg.format)
        return -EINVAL;
    /*
    if (pConfig->inputCfg.channels != AUDIO_CHANNEL_OUT_STEREO) {
        ALOGW("%s: channels in = 0x%x channels out = 0x%x", __FUNCTION__,
            pConfig->inputCfg.channels, pConfig->outputCfg.channels);
        pConfig->inputCfg.channels = pConfig->outputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
    }
    */
    if (pConfig->outputCfg.accessMode != EFFECT_BUFFER_ACCESS_WRITE &&
                pConfig->outputCfg.accessMode != EFFECT_BUFFER_ACCESS_ACCUMULATE)
        return -EINVAL;

    if (pConfig->inputCfg.format != AUDIO_FORMAT_PCM_32_BIT) {
        //ALOGW("%s: format in = 0x%x format out = 0x%x", __FUNCTION__,
        //    pConfig->inputCfg.format, pConfig->outputCfg.format);
        pConfig->inputCfg.format = pConfig->outputCfg.format = AUDIO_FORMAT_PCM_32_BIT;
    }
    memcpy(&pContext->config, pConfig, sizeof(effect_config_t));

    return 0;
}


static int dump(vxContext *pContext)
{
    if (pContext == NULL || !pContext->gVXLibHandler) {
        ALOGD("%s: Invalid, pContext is Null!", __FUNCTION__);
        return 0;
    }

    VX_Cmd_Param *pVXCmdParamTable[1] = {NULL};
    int count = get_show_config_CMDs(pVXCmdParamTable);
    set_vx_param_by_cmd_ptrs(pContext, pVXCmdParamTable, count);
    ALOGD("%s: dump all parmameters from VX lib!", __FUNCTION__);
    return 0;
}

static int Virtualx_setParameter(vxContext *pContext, void *pParam, void *pValue)
{
    vxdata *pData = &pContext->gvxdata;
    int32_t paramIndex = *(int32_t *)pParam;
    int32_t value = 0;
    int ret = 0;
    //setting param in switch case
    bool set_done = false;

    ALOGD("+%s() paramIndex = %u", __FUNCTION__, paramIndex);

    switch (paramIndex) {
        case PARAM_CHANNEL_NUM:
        {
            if (!pContext->gVXLibHandler) {
                ALOGW("%s() gVXLibHandler = NULL", __func__);
                return 0;
            }
            set_done = true;
            value = *(int32_t *)pValue;
            int inmode = 0;
            if (value == 2) {
                inmode = 0;
            } else if (value == 6) {
                inmode = 1;
            } else if (value == 8) {
                inmode = 2;
            } else if (value == 10) {
                inmode = 4;
            } else if (value == 12) {
                inmode = 5;
            } else {
                value = 2;
                inmode = 0;
            }
            ret = (pContext->gVirtualxapi.vx_v4_set_inmode)(pContext->vxInst, inmode);
            if (ret != 0) {
                ALOGE("Set VX input channel:%d failed:%d\n", value, ret);
                pContext->effect_ch_in = 2;
                ret = pContext->effect_ch_in;
            } else {
                pContext->effect_ch_in = value;
                ret = pContext->effect_ch_in;
            }
            ALOGI("Set VX input channel:%d inmode:%d\n", value, inmode);
            break;
        }
        case PARAM_OUT_CHANNEL_NUM:
        {
            if (!pContext->gVXLibHandler) {
                ALOGW("%s() gVXLibHandler = NULL", __func__);
                return 0;
            }
            set_done = true;
            value = *(int32_t *)pValue;
            int outmode = 0;
            if (value == 2) {
                outmode = 0;
            } else if (value == 3) {
                outmode = 1;
            } else if (value == 6) {
                outmode = 7;
            } else if (value == 8) {
                outmode = 8;
            } else if (value >= 10) {
                outmode = 14;
                value = 10;     // VirtualX max output is 5.1.4 channel.
            } else {
                outmode = 0;
                value = 2;
            }
            ret = (pContext->gVirtualxapi.vx_v4_set_outmode)(pContext->vxInst, outmode);
            if (ret != 0) {
                ALOGE("Set VX output channel:%d failed:%d\n", value, ret);
                pContext->effect_ch_out = 2;
                ret = -1;
            } else {
                pContext->effect_ch_out = value;
                ret = 0;
            }
            ALOGI("Set VX output channel:%d outmode:%d\n", value, outmode);
            break;
        }
        case VIRTUALX_PARAM_ENABLE:
        {
            if (!pContext->gVXLibHandler) {
                ALOGW("%s() gVXLibHandler = NULL", __func__);
                return 0;
            }
            set_done = true;
            value = *(int32_t *)pValue;
            pData->enable = value;
            if (value) {
                VX_Cmd_Param *virtualXModeOnTable[5] = {NULL};
                int count = get_VirtualXModeOn_CMDs(virtualXModeOnTable);
                ret = set_vx_param_by_cmd_ptrs(pContext, virtualXModeOnTable, count);
            } else {
                VX_Cmd_Param *virtualXModeOffTable[5] = {NULL};
                int count = get_VirtualXModeOn_CMDs(virtualXModeOffTable);
                ret = set_vx_param_by_cmd_ptrs(pContext, virtualXModeOffTable, count);
            }
            if (pData->enable) {
                /* set dts decoder output mode: */
                /* mode 0: auto output depending stream content */
                /* mode 2: force 2ch output */
                setParameters(String8("VX_SET_DTS_Mode=0"));
            } else {
                pContext->effect_ch_in = 2;
                setParameters(String8("VX_SET_DTS_Mode=2"));
            }

            ALOGI("Set VX %s ret:%d", value ? "Enable" : "Disable", ret);
            break;
        }
        case VIRTUALX_PARAM_SURROUND_MODE:
        {
            if (!pContext->gVXLibHandler) {
                ALOGW("%s() gVXLibHandler = NULL", __func__);
                return 0;
            }
            set_done = true;
            value = *(int32_t *)pValue;
            if (value) {
                VX_Cmd_Param *pSurroundModeOn[5] = {NULL};
                int count = get_SurroundModeOn_CMDs(pSurroundModeOn);
                ret = set_vx_param_by_cmd_ptrs(pContext, pSurroundModeOn, count);
            } else {
                VX_Cmd_Param *pSurroundModeOff[5] = {NULL};
                int count = get_SurroundModeOff_CMDs(pSurroundModeOff);
                ret = set_vx_param_by_cmd_ptrs(pContext, pSurroundModeOff, count);
            }
            ALOGI("Set VX surround mode %s ret:%d", value ? "Enable" : "Disable", ret);
            break;
        }
        case VIRTUALX_PARAM_DIALOGCLARITY_MODE:
        {
            if (!pContext->gVXLibHandler) {
                ALOGW("%s() gVXLibHandler = NULL", __func__);
                return 0;
            }
            set_done = true;
            value = *(int32_t *)pValue;
            if (value == 0) {
                VX_Cmd_Param *pDialogClarityOff[5] = {NULL};
                int count = get_DialogClarityOff_CMDs(pDialogClarityOff);
                ret = set_vx_param_by_cmd_ptrs(pContext, pDialogClarityOff, count);
            } else if (value == 1) {
                VX_Cmd_Param *pDialogClarityLow[5] = {NULL};
                int count = get_DialogClarityLow_CMDs(pDialogClarityLow);
                ret = set_vx_param_by_cmd_ptrs(pContext, pDialogClarityLow, count);
            } else if (value == 2) {
                VX_Cmd_Param *pDialogClarityHigh[5] = {NULL};
                int count = get_DialogClarityHigh_CMDs(pDialogClarityHigh);
                ret = set_vx_param_by_cmd_ptrs(pContext, pDialogClarityHigh, count);
            } else {
                ALOGI("-Unsupport Dialog Clarity mode:%d\n", value);
            }

            ALOGI("Set Dialog Clarity mode:%d ret:%d\n", value, ret);
            break;
        }
        case AUDIO_PARAM_TYPE_TRU_VOLUME:
        case PARAM_LOUDNESS_CONTROL_ENABLE_I32:
        {
            if (!pContext->gVXLibHandler) {
                ALOGW("%s() gVXLibHandler = NULL", __func__);
                return 0;
            }
            set_done = true;
            value = *(int32_t *)pValue;
            if (value) {
                VX_Cmd_Param *pTruVolumeOn[5] = {NULL};
                int count = get_TruVolumeOn_CMDs(pTruVolumeOn);
                ret = set_vx_param_by_cmd_ptrs(pContext, pTruVolumeOn, count);
            } else {
                VX_Cmd_Param *pTruVolumeOff[5] = {NULL};
                int count = get_TruVolumeOff_CMDs(pTruVolumeOff);
                ret = set_vx_param_by_cmd_ptrs(pContext, pTruVolumeOff, count);
            }
            ALOGI("Set Loudness Conrol(TruVolume) %s ret:%d\n", value ? "Enable" : "Disable", ret);
            break;
        }
        case PARAM_LOUDNESS_CONTROL_IO_MODE_I32:
        {
            if (!pContext->gVXLibHandler) {
                ALOGW("%s() gVXLibHandler = NULL", __func__);
                return 0;
            }
            set_done = true;
            value = *(int32_t *)pValue;
            if (value >= 0 && value <= 8) {
                VX_Cmd_Param *pVXCmdParam[5] = {NULL};
                int count = get_Loudness_ctl_io_mode_CMDs_by_int(pVXCmdParam, value);
                ret = set_vx_param_by_cmd_ptrs(pContext, pVXCmdParam, count);
            }
            ALOGI("Set Loudness Conrol IO Mode %d ret:%d\n", value, ret);
            break;
        }
        case PARAM_AEQ_ENABLE_I32:
        {
            if (!pContext->gVXLibHandler) {
                ALOGW("%s() gVXLibHandler = NULL", __func__);
                return 0;
            }
            set_done = true;
            value = *(int32_t *)pValue;
            if (value == 0 || value == 1) {
                VX_Cmd_Param *pVXCmdParam[5] = {NULL};
                int count = get_AEQ_enable_CMDs_by_int(pVXCmdParam, value);
                ret = set_vx_param_by_cmd_ptrs(pContext, pVXCmdParam, count);
            }
            ALOGI("Set AEQ %s ret:%d\n", value ? "Enable" : "Disable", ret);
            break;
        }
        case VIRTUALX_EFFECT_ENABLE:
        {
            set_done = true;
            pContext->vx_effect_enable = *(bool *)pValue;
            if (pContext->vx_effect_enable) {
                setParameters(String8("Effect_enable=VX_ON"));
            } else {
                setParameters(String8("Effect_enable=VX_OFF"));
                pContext->effect_ch_in = 2;
            }
            ALOGD("%s: Virtual effect enable: %d", __FUNCTION__, pContext->vx_effect_enable);
        }
        break;
        case VIRTUALX_USER_MODE:
        {
            value = *(int32_t *)pValue;
            if (!pContext->gVXLibHandler) {
                ALOGW("%s() line:%d gVXLibHandler = NULL", __func__, __LINE__);
                return 0;
            }

            set_done = true;
            int user_mode = value;
            ret = do_vx_setting_for_mode(pContext, user_mode);
            ALOGI("%s() set VX user_mode:%d ret:%d", __func__, user_mode, ret);
        }
        break;
        case AUDIO_ALL_PARAM_DUMP:
        {
            if (!pContext->gVXLibHandler) {
                ALOGW("%s() gVXLibHandler = NULL", __func__);
                return 0;
            }
            set_done = true;
            ret = dump(pContext);
            break;
        }
    }

    if (set_done) {
        ALOGE("-%s() ParamIndex:%d ret:%d", __func__, paramIndex, ret);
        return ret;
    }

    //Signel user cmd setting
    if ((paramIndex >= PARAM_MBHL_ENABLE_I32 && paramIndex < PARAM_MBHL_PARAM_MAX) ||
        (paramIndex > PARAM_TBHDX_PARAM_START && paramIndex < PARAM_TBHDX_PARAM_MAX) ||
        (paramIndex > PARAM_VX_FORCE_PARAM_START && paramIndex < PARAM_VX_FORCE_PARAM_MAX) ||
        (paramIndex > PARAM_VX_GENERAL_PARAM_START && paramIndex < PARAM_VX_GENERAL_PARAM_MAX) ||
        (paramIndex > PARAM_TSX_PARAM_START && paramIndex < PARAM_TSX_PARAM_MAX) ||
        (paramIndex > PARAM_VX_DC_PARAM_START && paramIndex < PARAM_VX_DC_PARAM_MAX) ||
        (paramIndex > PARAM_LOUDNESS_PARAM_START && paramIndex < PARAM_LOUDNESS_PARAM_MAX) ||
        (paramIndex > PARAM_AEQ_PARAM_START && paramIndex < PARAM_AEQ_PARAM_MAX) ||
        (paramIndex > PARAM_GEQ_PARAM_START && paramIndex < PARAM_GEQ_PARAM_MAX) ||
        (paramIndex > PARAM_IL_PARAM_START && paramIndex < PARAM_IL_PARAM_MAX)) {
        if (!pContext->gVXLibHandler) {
            ALOGW("%s() gVXLibHandler = NULL", __func__);
            return 0;
        }
        const char *pValueStr = (const char *)pValue;
        char valueBuf[VX_MAX_PARAM_VALUE_STR_LEN] = {0};
        strncpy(valueBuf, pValueStr, VX_MAX_PARAM_VALUE_STR_LEN);
        VX_Cmd_Param *vxCmdParam = param_index_to_vx_cmd(paramIndex);

        if (vxCmdParam != NULL) {
            SET_PARAM_WITH_VALUE(vxCmdParam, valueBuf);
            VX_Cmd_Param *pVXCmdParamTable[1] = {vxCmdParam};
            ret = set_vx_param_by_cmd_ptrs(pContext, pVXCmdParamTable, AM_ARRAY_SIZE(pVXCmdParamTable));
            ALOGE("-%s() Mode:%d ParamIndex:%d %s", __func__, pData->mode, paramIndex, (ret == 0 ? "Successful!" : "Failed!"));
        } else {
            ALOGE("-%s() Line:%d ParamIndex:%d Failed! Cant find VxCmd", __func__, __LINE__, paramIndex);
            ret = -EINVAL;
        }

        return ret;
    }

    ALOGE("-%s() Warning! Unsupport ParamIndex:%d", __func__, paramIndex);
    return ret;
}

static int Virtualx_getParameter(vxContext *pContext, void *pParam, size_t *pValueSize, void *pValue)
{
    uint32_t param = *(uint32_t *)pParam;
    int32_t value = 0;
    vxdata *data = &pContext->gvxdata;

    if (*pValueSize < sizeof(uint32_t)) {
        *pValueSize = 0;
        return -EINVAL;
    }
    if (!pContext->gVXLibHandler) {
        return 0;
    }

    //TODO
    if (param || value || data || pValue) {
        return 0;
    }

    return 0;
}

static int Virtualx_command(effect_handle_t self, uint32_t cmdCode, uint32_t cmdSize,
        void *pCmdData, uint32_t *replySize, void *pReplyData)
{
    vxContext * pContext = (vxContext *)self;
    effect_param_t *p;
    int voffset;

    ALOGD("%s: cmd = %u", __FUNCTION__, cmdCode);

    if (pContext == NULL || pContext->state == VIRTUALX_STATE_UNINITIALIZED) {
        return -EINVAL;
    }

    switch (cmdCode) {
    case EFFECT_CMD_INIT:
        if (pReplyData == NULL || replySize == NULL || *replySize != sizeof(int))
            return -EINVAL;
        *(int *) pReplyData = Virtualx_init(pContext);
        break;
    case EFFECT_CMD_SET_CONFIG:
        if (pCmdData == NULL || cmdSize != sizeof(effect_config_t) || pReplyData == NULL || replySize == NULL || *replySize != sizeof(int))
            return -EINVAL;
        *(int *) pReplyData = Virtualx_configure(pContext,(effect_config_t *) pCmdData);
        break;
    case EFFECT_CMD_RESET:
        Virtualx_reset(pContext);
        break;
    case EFFECT_CMD_ENABLE:
        if (pReplyData == NULL || replySize == NULL || *replySize != sizeof(int))
            return -EINVAL;
        if (pContext->state != VIRTUALX_STATE_INITIALIZED)
            return -ENOSYS;
        pContext->state = VIRTUALX_STATE_ACTIVE;
        *(int *)pReplyData = 0;
        break;
    case EFFECT_CMD_DISABLE:
        if (pReplyData == NULL || replySize == NULL || *replySize != sizeof(int))
            return -EINVAL;
        if (pContext->state != VIRTUALX_STATE_ACTIVE)
            return -ENOSYS;
        pContext->state = VIRTUALX_STATE_INITIALIZED;
        *(int *)pReplyData = 0;
        break;
    case EFFECT_CMD_GET_PARAM:
        if (pCmdData == NULL ||
            cmdSize != (int)(sizeof(effect_param_t) + sizeof(uint32_t)) ||
            pReplyData == NULL || replySize == NULL ||
            (*replySize < (int)(sizeof(effect_param_t) + sizeof(uint32_t) + sizeof(uint32_t)) &&
            *replySize < (int)(sizeof(effect_param_t) + sizeof(uint32_t) + 8 * sizeof(float)) &&
            *replySize < (int)(sizeof(effect_param_t) + sizeof(uint32_t) + 10 * sizeof(float)) &&
            *replySize < (int)(sizeof(effect_param_t) + sizeof(uint32_t) + 6 * sizeof(float)) &&
            *replySize < (int)(sizeof(effect_param_t) + sizeof(uint32_t) + 3 * sizeof(float))))
            return -EINVAL;
        p = (effect_param_t *)pCmdData;
        memcpy(pReplyData, pCmdData, sizeof(effect_param_t) + p->psize);
        p = (effect_param_t *)pReplyData;

        voffset = ((p->psize - 1) / sizeof(int32_t) + 1) * sizeof(int32_t);

        p->status = Virtualx_getParameter(pContext, p->data, (size_t  *)&p->vsize, p->data + voffset);
        *replySize = sizeof(effect_param_t) + voffset + p->vsize;
        break;
    case EFFECT_CMD_SET_PARAM:
        if (pCmdData == NULL || pReplyData == NULL || replySize == NULL || *replySize != sizeof(int32_t)) {
            ALOGE("%s: EFFECT_CMD_SET_PARAM cmd size error!", __FUNCTION__);
            return -EINVAL;
        }

        p = (effect_param_t *)pCmdData;
        *(int *)pReplyData = Virtualx_setParameter(pContext, (void *)p->data, p->data + p->psize);
        break;
    case EFFECT_CMD_OFFLOAD:
        *(int *)pReplyData = 0;
        break;
    case EFFECT_CMD_SET_DEVICE:
    case EFFECT_CMD_SET_VOLUME:
    case EFFECT_CMD_SET_AUDIO_MODE:
        break;
    case EFFECT_CMD_DUMP:
        dump(pContext);
        break;
    default:
        ALOGE("%s: invalid command %d", __FUNCTION__, cmdCode);
        return -EINVAL;
    }

    return 0;
}

static int Virtualx_release(vxContext *pContext)
{
    if (pContext->gVXLibHandler) {
       (*pContext->gVirtualxapi.vx_v4_release)(pContext->vxInst);
    }
    return 0;
}

#ifdef DEBUG_VX
static int getprop_bool(const char *path)
{
    char buf[PROPERTY_VALUE_MAX];
    int ret = -1;

    ret = property_get(path, buf, NULL);
    if (ret > 0) {
        if (strcasecmp(buf, "true") == 0 || strcmp(buf, "1") == 0) {
            return 1;
        }
    }
    return 0;
}
#endif

//-------------------Effect Control Interface Implementation--------------------------
static int Virtualx_process(effect_handle_t self, audio_buffer_t *inBuffer, audio_buffer_t *outBuffer)
{
    vxContext *pContext = (vxContext *)self;
    if (pContext == NULL) {
        return -EINVAL;
    }

    if (inBuffer == NULL || inBuffer->raw == NULL ||
        outBuffer == NULL || outBuffer->raw == NULL ||
        inBuffer->frameCount != outBuffer->frameCount ||
        inBuffer->frameCount == 0) {
        return -EINVAL;
    }

    int32_t *in  = (int32_t *)inBuffer->raw;
    int32_t *out = (int32_t *)outBuffer->raw;
    int32_t vx_frameCount = inBuffer->frameCount;
    int32_t byte_counter = inBuffer->frameCount * sizeof(int32_t) * pContext->effect_ch_in;
    vxdata  *data = &pContext->gvxdata;

#ifdef DEBUG_VX
    bool VX_prop = getprop_bool("vendor.media.VX.debug");
    if (VX_prop) {
        FILE *dump_fp = fopen("/data/vendor/audiohal/audio_vx_input.pcm", "a+");
        if (dump_fp != NULL) {
            void *input = inBuffer->raw;
            fwrite(input, 1, byte_counter, dump_fp);
            fclose(dump_fp);
        }
    }
#endif

    if (!data->enable || !pContext->gVXLibHandler) {
        if (inBuffer->raw == outBuffer->raw) {
            return 0;
        }

        for (size_t i = 0; i < inBuffer->frameCount; i++) {
            *out++ = *in++;
            *out++ = *in++;
        }
        return 0;
    }

    if (pContext->left_bytes > 0) {
        ALOGV("%s: in_ch_num=%d, out_ch_num=%d in_frameCount=%zu, out_frameCount=%zu, left_bytes=%d", __FUNCTION__,
            pContext->effect_ch_in, pContext->effect_ch_out, inBuffer->frameCount, outBuffer->frameCount, pContext->left_bytes);
    }

    if (pContext->left_bytes > 0) {
        memmove((int8_t *)inBuffer->raw + pContext->left_bytes, inBuffer->raw ,byte_counter);
        memcpy((int8_t *)inBuffer->raw, pContext->left_pBuffer ,pContext->left_bytes);
        inBuffer->frameCount += pContext->left_bytes / (sizeof(int32_t) * pContext->effect_ch_in);
        byte_counter =  inBuffer->frameCount * sizeof(int32_t) * pContext->effect_ch_in;
    }
    int32_t blockSize = VIRTUALX_FRAME_SIZE;
    int32_t blockCount = inBuffer->frameCount / blockSize;
    pContext->left_bytes = byte_counter - sizeof(int32_t) * pContext->effect_ch_in *  blockCount * blockSize;
    if (pContext->left_bytes > 0) {
        memcpy(pContext->left_pBuffer, (int8_t *)inBuffer->raw + (byte_counter - pContext->left_bytes), pContext->left_bytes);
    }
    outBuffer->frameCount = VIRTUALX_FRAME_SIZE * blockCount;

    for (int i = 0; i < blockCount; i++) {
        for (int sampleCount = 0; sampleCount < VIRTUALX_FRAME_SIZE; sampleCount++) {
            if (pContext->effect_ch_in == 2 /*for 2ch process*/) {
                pContext->ppMappedInCh[0][sampleCount] = *in++; // L & R
                pContext->ppMappedInCh[1][sampleCount] = *in++;
                for (int j = 2; j < VIRTUALX_MAX_IN_CHANNELS; j++) {
                    pContext->ppMappedInCh[j][sampleCount] = 0;
                }
            } else if (pContext->effect_ch_in > 2 /*for multi-channel ch process*/) {
                int j = 0;
                for (j = 0; j < pContext->effect_ch_in; j++) {
                    pContext->ppMappedInCh[j][sampleCount] = *in++;
                }
                for (j = pContext->effect_ch_in; j < VIRTUALX_MAX_IN_CHANNELS; j++) {
                    pContext->ppMappedInCh[j][sampleCount] = 0;
                }
            } else {
                ALOGW("Not support source channel:%d", pContext->effect_ch_in);
            }
        }

        (pContext->gVirtualxapi.vx_v4_process)(pContext->ppMappedInCh, pContext->ppMappedOutCh);

#ifdef DEBUG_VX
        int32_t *output = out;
#endif
        for (int sampleCount = 0; sampleCount < VIRTUALX_FRAME_SIZE; sampleCount++) {
            //TODO: should match different output channel
            *out++  = pContext->ppMappedOutCh[0][sampleCount];
            *out++  = pContext->ppMappedOutCh[1][sampleCount];
        }

#ifdef DEBUG_VX
        if (VX_prop) {
            FILE *dump_fp = fopen("/data/vendor/audiohal/audio_vx_output.pcm", "a+");
            if (dump_fp != NULL) {
                fwrite((void*)output, 1, VIRTUALX_FRAME_SIZE * 2 * sizeof(int32_t), dump_fp);
                fclose(dump_fp);
            }
        }
#endif
    }

    out = (int32_t *)outBuffer->raw;
    if (outBuffer->frameCount < vx_frameCount && pContext->left_process_bytes <= 0) {
        int tmp_byte = vx_frameCount * sizeof(int32_t) * pContext->effect_ch_out - outBuffer->frameCount * sizeof(int32_t) * pContext->effect_ch_out;
        memmove((int8_t *)out + tmp_byte, out ,outBuffer->frameCount * sizeof(int32_t) * pContext->effect_ch_out);
        memset((int8_t *)out, 0 , tmp_byte);
    }

    if (pContext->left_process_bytes > 0) {
        memmove((int8_t *)out + pContext->left_process_bytes, out, outBuffer->frameCount * sizeof(int32_t) * pContext->effect_ch_out);
        memcpy((int8_t *)out, pContext->left_process_pBuffer ,pContext->left_process_bytes);
        outBuffer->frameCount += pContext->left_process_bytes / (sizeof(int32_t) * pContext->effect_ch_out);
    }

    pContext->left_process_bytes = outBuffer->frameCount * sizeof(int32_t) * pContext->effect_ch_out - vx_frameCount * sizeof(int32_t) * pContext->effect_ch_out;

    if (pContext->left_process_bytes > 0) {
        memcpy(pContext->left_process_pBuffer, (int8_t *)out + (vx_frameCount * sizeof(int32_t) * pContext->effect_ch_out), pContext->left_process_bytes);
    }

    outBuffer->frameCount = vx_frameCount;
    return 0;
}

//-------------------- Effect Library Interface Implementation------------------------

static int Virtualx_getDescriptor(effect_handle_t self, effect_descriptor_t *pDescriptor)
{
    vxContext * pContext = (vxContext *) self;

    if (pContext == NULL || pDescriptor == NULL) {
        ALOGE("%s: invalid param", __FUNCTION__);
        return -EINVAL;
    }

    *pDescriptor = VirtualxDescriptor;

    return 0;
}

int VirtualxLib_Create(const effect_uuid_t *uuid, int32_t sessionId __unused, int32_t ioId __unused, effect_handle_t *pHandle)
{
    if (pHandle == NULL || uuid == NULL)
        return -EINVAL;

    if (memcmp(uuid, &VirtualxDescriptor.uuid, sizeof(effect_uuid_t)) != 0)
        return -EINVAL;

    vxContext *pContext = new vxContext;
    if (!pContext) {
        ALOGE("%s: alloc vxContext failed", __FUNCTION__);
        return -EINVAL;
    }
    memset(pContext, 0, sizeof(vxContext));

    if (Virtualx_load_ini_file(pContext) < 0) {
        ALOGE("%s: Load INI File failed, use default param", __FUNCTION__);
    }

    if (Virtualx_load_lib(pContext) < 0) {
        ALOGE("%s: Load Library File failed", __FUNCTION__);
    }

    pContext->itfe = &VirtualxInterface;
    pContext->state = VIRTUALX_STATE_UNINITIALIZED;

    *pHandle = (effect_handle_t)pContext;

    pContext->state = VIRTUALX_STATE_INITIALIZED;
    pContext->vx_effect_enable = true;
    setParameters(String8("Effect_enable=VX_ON"));

    ALOGD("%s: %p  OK", __FUNCTION__, pContext);
    return 0;
}


int VirtualxLib_Create_3_1(const effect_uuid_t *uuid, int32_t sessionId, int32_t ioId, int32_t deviceId __unused, effect_handle_t *pHandle)
{
    return VirtualxLib_Create(uuid, sessionId, ioId, pHandle);
}

int VirtualxLib_Release(effect_handle_t handle)
{
    vxContext * pContext = (vxContext *)handle;
    if (pContext == NULL)
        return -EINVAL;
    Virtualx_release(pContext);
    unload_Virtualx_lib(pContext);
    pContext->state = VIRTUALX_STATE_UNINITIALIZED;
    pContext->vx_effect_enable = false;
    setParameters(String8("Effect_enable=VX_OFF"));
    delete pContext;
    ALOGD("VirtualxLib_Release");
    return 0;
}

int VirtualxLib_GetDescriptor(const effect_uuid_t *uuid, effect_descriptor_t *pDescriptor)
{
    if (pDescriptor == NULL || uuid == NULL) {
        ALOGE("%s: called with NULL pointer", __FUNCTION__);
        return -EINVAL;
    }
    if (memcmp(uuid, &VirtualxDescriptor.uuid, sizeof(effect_uuid_t)) == 0) {
        *pDescriptor = VirtualxDescriptor;
        return 0;
    }
    return  -EINVAL;
}

// effect_handle_t interface implementation for Virtualx effect
const struct effect_interface_s VirtualxInterface = {
        Virtualx_process,
        Virtualx_command,
        Virtualx_getDescriptor,
        NULL,
};

audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM = {
    .tag = AUDIO_EFFECT_LIBRARY_TAG,
    .version = EFFECT_LIBRARY_API_VERSION_3_1,
    .name = "VirtualX_V4",
    .implementor = "DTS Labs",
    .create_effect = VirtualxLib_Create,
    .create_effect_3_1 = VirtualxLib_Create_3_1,
    .release_effect = VirtualxLib_Release,
    .get_descriptor = VirtualxLib_GetDescriptor,
};

};//extern C
