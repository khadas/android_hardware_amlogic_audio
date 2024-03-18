
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <errno.h>
#include <media/AudioEffect.h>

#include "Virtualx_v4.h"
#include "vx_ctrl.h"


#define AM_ARRAY_SIZE(_a)       (sizeof(_a)/sizeof((_a)[0]))
#define BITS                    16
#define MAXbit                  (1 << (BITS - 1))
#define INT2FLOAT(a)            ((float)a / MAXbit)
#define FLOAT2INT(a)            (int)((float)a * MAXbit)

#define LOG(x...)               printf("[AudioEffect] " x)
#define LOGV(format,...)

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
        char str[VX_MAX_PARAM_VALUE_STR_LEN];
        float params[VX_MAX_PARAM_SIZE];
    };
} Virtualx_param_t;

typedef struct cmd_param_str {
    int userId;
    int key;
    const char *param;
} VX_Cmd_Param;


static Virtualx_param_t gVxParamInstance;
static VX_Cmd_Param*    gPVxCmdParam = NULL;

Virtualx_param_t *getVxParamInstance()
{
    return &gVxParamInstance;
}

VX_Cmd_Param *getVxCmdParamInstance()
{
    return gPVxCmdParam;
}

void setVxCmdParamInstance(VX_Cmd_Param *data)
{
    gPVxCmdParam = data;
}

/*** parameter index to parameter name map ***/
#define CMD_NAME_USERID_INIT(_param, _key, _userId)    {.param = _param, .key = _key, .userId = _userId}

/*** VirtualX normal control parameters ***/
enum virtualx_force_id {
    VX_frame_size = 0,
    VX_input_pcm_stride,
    VX_output_pcm_stride,
};

static VX_Cmd_Param virtualx_force_params[] = {
    CMD_NAME_USERID_INIT("--frame-size", VX_frame_size, PARAM_VX_FRAME_SIZE),
    CMD_NAME_USERID_INIT("--input-pcm-stride", VX_input_pcm_stride, PARAM_VX_INPUT_PCM_STRIDE),
    CMD_NAME_USERID_INIT("--output-pcm-stride", VX_output_pcm_stride, PARAM_VX_OUTPUT_PCM_STRIDE),
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
    CMD_NAME_USERID_INIT("--virtualx-en", VIRTUALX_en, PARAM_VX_ENABLE_I32),
    CMD_NAME_USERID_INIT("--virtualx-in-mode", VIRTUALX_in_mode, PARAM_VX_INPUT_MODE_I32),
    CMD_NAME_USERID_INIT("--virtualx-out-mode", VIRTUALX_out_mode, PARAM_VX_OUTPUT_MODE_I32),
    CMD_NAME_USERID_INIT("--virtualx-headroom-gain", VIRTUALX_headroom_gain, PARAM_VX_HEADROOM_GAIN_I32),
    CMD_NAME_USERID_INIT("--virtualx-processing-output-gain", VIRTUALX_processing_output_gain, PARAM_VX_PROC_OUTPUT_GAIN_I32),
    CMD_NAME_USERID_INIT("--virtualx-reference-level", VIRTUALX_reference_level, PARAM_VX_REFERENCE_LEVEL_I32),
    CMD_NAME_USERID_INIT("--virtualx-discard", VIRTUALX_discard, PARAM_VX_PROCESS_DISCARD_I32),
    CMD_NAME_USERID_INIT("--virtualx-multirate-processing-type", VIRTUALX_multirate_processing_type, PARAM_VX_PROCESS_MULTIRATE_PROC_TYPE_I32),
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
    CMD_NAME_USERID_INIT("--virtualx-tsx-en", VIRTUALX_tsx_en, PARAM_TSX_ENABLE_I32),
    CMD_NAME_USERID_INIT("--virtualx-tsx-pssv-mtrx-en", VIRTUALX_tsx_pssv_mtrx_en, PARAM_TSX_PASSIVEMATRIXUPMIX_ENABLE_I32),
    CMD_NAME_USERID_INIT("--virtualx-tsx-horiznt-effect-ctrl", VIRTUALX_tsx_horiznt_effect_ctrl, PARAM_TSX_HORIZNT_EFFECT_CTRL),
    CMD_NAME_USERID_INIT("--virtualx-tsx-phantom-ctrgain",VIRTUALX_tsx_phantom_ctrgain, PARAM_TSX_PHANTOM_CTRGAIN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-ctrgain", VIRTUALX_tsx_ctrgain, PARAM_TSX_CTRGAIN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-heightmix-coeff", VIRTUALX_tsx_heightmix_coeff, PARAM_TSX_HEIGHTMIX_COEFF),
    CMD_NAME_USERID_INIT("--virtualx-tsx-height-out-gain", VIRTUALX_tsx_height_out_gain, PARAM_TSX_HEIGHT_OUT_GAIN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-precond-front", VIRTUALX_tsx_precond_front, PARAM_TSX_PRECOND_FRONT),
    CMD_NAME_USERID_INIT("--virtualx-tsx-precond-surnd", VIRTUALX_tsx_precond_surnd, PARAM_TSX_PRECOND_SURND),
    CMD_NAME_USERID_INIT("--virtualx-tsx-vxtopspk-loc", VIRTUALX_tsx_vxtopspk_loc, PARAM_TSX_VXTOPSPK_LOC),
    CMD_NAME_USERID_INIT("--virtualx-tsx-frnt-wide-en", VIRTUALX_tsx_frnt_wide_en, PARAM_TSX_FRNT_WIDE_EN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-hght-virtualizer-en", VIRTUALX_tsx_hght_virtualizer_en, PARAM_TSX_HEIGHT_VIRTUALIZER_EN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-frnt-srnd-en", VIRTUALX_tsx_frnt_srnd_en, PARAM_TSX_FRNT_SRND_EN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-hght-upmix-en", VIRTUALX_tsx_hght_upmix_en, PARAM_TSX_HEIGHT_UPMIX_ENABLE_I32),
    CMD_NAME_USERID_INIT("--virtualx-tsx-lrmix-ratio2ctr", VIRTUALX_tsx_lrmix_ratio2ctr, PARAM_TSX_LRMIX_RATIO2CTR),
    CMD_NAME_USERID_INIT("--virtualx-tsx-lfe-gain", VIRTUALX_tsx_lfe_gain, PARAM_TSX_LFE_GAIN),
    CMD_NAME_USERID_INIT("--virtualx-tsx-height-discard", VIRTUALX_tsx_height_discard, PARAM_TSX_HEIGHT_DISCARD_I32),
    CMD_NAME_USERID_INIT("--virtualx-tsx-app-lstnr-dist", VIRTUALX_tsx_app_lstnr_dist, PARAM_TSX_APP_LSTNR_DIST),
    CMD_NAME_USERID_INIT("--virtualx-tsx-app-bttmspktoctr-dist", VIRTUALX_tsx_app_bttmspktoctr_dist, PARAM_TSX_APP_BTTM_DIST),
    CMD_NAME_USERID_INIT("--virtualx-tsx-app-topspktoctr-dist", VIRTUALX_tsx_app_topspktoctr_dist, PARAM_TSX_APP_TOP_DIST),
};


enum virtualx_truSurroundX_DC_id {
    VIRTUALX_dialogclarity_en,
    VIRTUALX_dialogclarity_level,
    VIRTUALX_definition_en,
    VIRTUALX_definition_lvl,
    VIRTUALX_cs2to3_en,
};

static VX_Cmd_Param virtualx_truSurroundX_DC_params[] = {
    CMD_NAME_USERID_INIT("--virtualx-dialogclarity-en", VIRTUALX_dialogclarity_en, PARAM_VX_DC_ENABLE_I32),
    CMD_NAME_USERID_INIT("--virtualx-dialogclarity-level", VIRTUALX_dialogclarity_level, PARAM_VX_DC_CONTROL_I32),
    CMD_NAME_USERID_INIT("--virtualx-definition-en", VIRTUALX_definition_en, PARAM_VX_DEF_ENABLE_I32),
    CMD_NAME_USERID_INIT("--virtualx-definition-lvl", VIRTUALX_definition_lvl, PARAM_VX_DEF_CONTROL_I32),
    CMD_NAME_USERID_INIT("--virtualx-cs2to3-en", VIRTUALX_cs2to3_en, PARAM_VX_CS2TO3_EN),
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
    CMD_NAME_USERID_INIT("--tbhdx-front-en", tbhdx_front_en, PARAM_TBHDX_FRONT_ENABLE),
    CMD_NAME_USERID_INIT("--tbhdx-front-proc-mode", tbhdx_front_proc_mode, PARAM_TBHDX_FRONT_PROC_MODE),
    CMD_NAME_USERID_INIT("--tbhdx-front-spksize", tbhdx_front_spksize, PARAM_TBHDX_FRONT_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-front-dynms", tbhdx_front_dynms, PARAM_TBHDX_FRONT_DYNMS),
    CMD_NAME_USERID_INIT("--tbhdx-front-hp-en", tbhdx_front_hp_en, PARAM_TBHDX_FRONT_HP_EN),
    CMD_NAME_USERID_INIT("--tbhdx-front-hp-order", tbhdx_front_hp_order, PARAM_TBHDX_FRONT_HP_ORDER),
    CMD_NAME_USERID_INIT("--tbhdx-front-bass-lvl", tbhdx_front_bass_lvl, PARAM_TBHDX_FRONT_BASS_LVL),
    CMD_NAME_USERID_INIT("--tbhdx-front-extbass", tbhdx_front_extbass, PARAM_TBHDX_FRONT_EXT_BASS),
    CMD_NAME_USERID_INIT("--tbhdx-front-input-gain", tbhdx_front_input_gain, PARAM_TBHDX_FRONT_INPUT_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-front-bypass-gain", tbhdx_front_bypass_gain, PARAM_TBHDX_FRONT_BYPASS_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-rear-en", tbhdx_rear_en, PARAM_TBHDX_REAR_ENABLE),
    CMD_NAME_USERID_INIT("--tbhdx-rear-proc-mode", tbhdx_rear_proc_mode, PARAM_TBHDX_REAR_PROC_MODE),
    CMD_NAME_USERID_INIT("--tbhdx-rear-spksize", tbhdx_rear_spksize, PARAM_TBHDX_REAR_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-rear-dynms", tbhdx_rear_dynms, PARAM_TBHDX_REAR_DYNMS),
    CMD_NAME_USERID_INIT("--tbhdx-rear-hp-en", tbhdx_rear_hp_e, PARAM_TBHDX_REAR_HP_EN),
    CMD_NAME_USERID_INIT("--tbhdx-rear-hp-order", tbhdx_rear_hp_order, PARAM_TBHDX_REAR_HP_ORDER),
    CMD_NAME_USERID_INIT("--tbhdx-rear-bass-lvl", tbhdx_rear_bass_lvl, PARAM_TBHDX_REAR_BASS_LVL),
    CMD_NAME_USERID_INIT("--tbhdx-rear-extbass", tbhdx_rear_extbass, PARAM_TBHDX_REAR_EXT_BASS),
    CMD_NAME_USERID_INIT("--tbhdx-rear-input-gain", tbhdx_rear_input_gain, PARAM_TBHDX_REAR_INPUT_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-rear-bypass-gain", tbhdx_rear_bypass_gain, PARAM_TBHDX_REAR_BYPASS_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-center-en", tbhdx_center_en, PARAM_TBHDX_CENTER_ENABLE),
    CMD_NAME_USERID_INIT("--tbhdx-center-dynms", tbhdx_center_dynms, PARAM_TBHDX_CENTER_DYNMS),
    CMD_NAME_USERID_INIT("--tbhdx-center-hp-en", tbhdx_center_hp_en, PARAM_TBHDX_CENTER_HP_EN),
    CMD_NAME_USERID_INIT("--tbhdx-center-hp-order", tbhdx_center_hp_order, PARAM_TBHDX_CENTER_HP_ORDER),
    CMD_NAME_USERID_INIT("--tbhdx-center-bass-lvl", tbhdx_center_bass_lvl, PARAM_TBHDX_CENTER_BASS_LVL),
    CMD_NAME_USERID_INIT("--tbhdx-center-extbass", tbhdx_center_extbass, PARAM_TBHDX_CENTER_EXT_BASS),
    CMD_NAME_USERID_INIT("--tbhdx-center-input-gain", tbhdx_center_input_gain, PARAM_TBHDX_CENTER_INPUT_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-center-bypass-gain", tbhdx_center_bypass_gain, PARAM_TBHDX_CENTER_BYPASS_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-surround-en", tbhdx_surround_en, PARAM_TBHDX_SURROUND_ENABLE),
    CMD_NAME_USERID_INIT("--tbhdx-surround-proc-mode", tbhdx_surround_proc_mode, PARAM_TBHDX_SURD_PROC_MODE),
    CMD_NAME_USERID_INIT("--tbhdx-surround-spksize", tbhdx_surround_spksize, PARAM_TBHDX_SURD_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-surround-dynms", tbhdx_surround_dynms, PARAM_TBHDX_SURD_DYNMS),
    CMD_NAME_USERID_INIT("--tbhdx-surround-hp-en", tbhdx_surround_hp_en, PARAM_TBHDX_SURD_HP_EN),
    CMD_NAME_USERID_INIT("--tbhdx-surround-hp-order", tbhdx_surround_hp_order, PARAM_TBHDX_SURD_HP_ORDER),
    CMD_NAME_USERID_INIT("--tbhdx-surround-bass-lvl", tbhdx_surround_bass_lvl, PARAM_TBHDX_SURD_BASS_LVL),
    CMD_NAME_USERID_INIT("--tbhdx-surround-extbass", tbhdx_surround_extbass, PARAM_TBHDX_SURD_EXT_BASS),
    CMD_NAME_USERID_INIT("--tbhdx-surround-input-gain", tbhdx_surround_input_gain, PARAM_TBHDX_SURD_INPUT_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-surround-bypass-gain", tbhdx_surround_bypass_gain, PARAM_TBHDX_SURD_BYPASS_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-delay-matching-gain", tbhdx_delay_matching_gain, PARAM_TBHDX_DELAY_MATCH_GAIN),
    CMD_NAME_USERID_INIT("--tbhdx-discard", tbhdx_discard, PARAM_TBHDX_PROCESS_DISCARD_I32),
    CMD_NAME_USERID_INIT("--tbhdx-front-app-spksize", tbhdx_front_app_spksize, PARAM_TBHDX_FRONT_APP_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-front-app-tgain", tbhdx_front_app_tgain, PARAM_TBHDX_FRONT_APP_TGAIN),
    CMD_NAME_USERID_INIT("--tbhdx-front-app-hpr", tbhdx_front_app_hpr, PARAM_TBHDX_FRONT_APP_HPR),
    CMD_NAME_USERID_INIT("--tbhdx-front-app-extbass", tbhdx_front_app_extbass, PARAM_TBHDX_FRONT_APP_EXTBASS),
    CMD_NAME_USERID_INIT("--tbhdx-center-app-spksize", tbhdx_center_app_spksize, PARAM_TBHDX_CENTER_APP_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-center-app-tgain", tbhdx_center_app_tgain, PARAM_TBHDX_CENTER_APP_TGAIN),
    CMD_NAME_USERID_INIT("--tbhdx-center-app-hpr", tbhdx_center_app_hpr, PARAM_TBHDX_CENTER_APP_HPR),
    CMD_NAME_USERID_INIT("--tbhdx-center-app-extbass", tbhdx_center_app_extbass, PARAM_TBHDX_CENTER_APP_EXTBASS),
    CMD_NAME_USERID_INIT("--tbhdx-surround-app-spksize", tbhdx_surround_app_spksize, PARAM_TBHDX_SURD_APP_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-surround-app-tgain", tbhdx_surround_app_tgain, PARAM_TBHDX_SURD_APP_TGAIN),
    CMD_NAME_USERID_INIT("--tbhdx-surround-app-hpr", tbhdx_surround_app_hpr, PARAM_TBHDX_SURD_APP_HPR),
    CMD_NAME_USERID_INIT("--tbhdx-surround-app-extbass", tbhdx_surround_app_extbass, PARAM_TBHDX_SURD_APP_EXTBASS),
    CMD_NAME_USERID_INIT("--tbhdx-rear-app-spksize", tbhdx_rear_app_spksize, PARAM_TBHDX_REAR_APP_SPKSIZE),
    CMD_NAME_USERID_INIT("--tbhdx-rear-app-tgain", tbhdx_rear_app_tgain, PARAM_TBHDX_REAR_APP_TGAIN),
    CMD_NAME_USERID_INIT("--tbhdx-rear-app-hpr", tbhdx_rear_app_hpr, PARAM_TBHDX_REAR_APP_HPR),
    CMD_NAME_USERID_INIT("--tbhdx-rear-app-extbass", tbhdx_rear_app_extbass, PARAM_TBHDX_REAR_APP_EXTBASS),
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
    CMD_NAME_USERID_INIT("--loudness-control-en", loudness_control_en, PARAM_LOUDNESS_CONTROL_ENABLE_I32),
    CMD_NAME_USERID_INIT("--loudness-control-io-mode", loudness_control_io_mode, PARAM_LOUDNESS_CONTROL_IO_MODE_I32),
    CMD_NAME_USERID_INIT("--loudness-control-target-loudness", loudness_control_target_loudness, PARAM_LOUDNESS_CONTROL_TARGET_LOUDNESS_I32),
    CMD_NAME_USERID_INIT("--loudness-control-preset", loudness_control_preset, PARAM_LOUDNESS_CONTROL_PRESET_I32),
    CMD_NAME_USERID_INIT("--loudness-control-latency-mode", loudness_control_latency_mode, PARAM_LOUDNESS_CONTROL_LATENCY_MODE_I32),
    CMD_NAME_USERID_INIT("--loudness-control-discard", loudness_control_discard, PARAM_LOUDNESS_CONTROL_DISCARD),
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
    CMD_NAME_USERID_INIT("--mbhl-en",  mbhl_en, PARAM_MBHL_ENABLE_I32),
    CMD_NAME_USERID_INIT("--mbhl-discard",  mbhl_discard, PARAM_MBHL_PROCESS_DISCARD_I32),
    CMD_NAME_USERID_INIT("--mbhl-bypass-gain",  mbhl_bypass_gain, PARAM_MBHL_BYPASS_GAIN_I32),
    CMD_NAME_USERID_INIT("--mbhl-reference-level",  mbhl_reference_level, PARAM_MBHL_REFERENCE_LEVEL_I32),
    CMD_NAME_USERID_INIT("--mbhl-volume",  mbhl_volume, PARAM_MBHL_VOLUME_I32),
    CMD_NAME_USERID_INIT("--mbhl-volume-step",  mbhl_volume_step, PARAM_MBHL_VOLUME_STEP_I32),
    CMD_NAME_USERID_INIT("--mbhl-balance-step", mbhl_balance_step, PARAM_MBHL_BALANCE_STEP_I32),
    CMD_NAME_USERID_INIT("--mbhl-output-gain",  mbhl_output_gain, PARAM_MBHL_OUTPUT_GAIN_I32),
    CMD_NAME_USERID_INIT("--mbhl-cp-en",  mbhl_cp_enable, PARAM_MBHL_CP_ENABLE_I32),
    CMD_NAME_USERID_INIT("--mbhl-cp-level",  mbhl_cp_level, PARAM_MBHL_CP_LEVEL),
    CMD_NAME_USERID_INIT("--mbhl-ac-en",  mbhl_ac_enable, PARAM_MBHL_AC_ENABLE_I32),
    CMD_NAME_USERID_INIT("--mbhl-ac-level",  mbhl_ac_level, PARAM_MBHL_AC_LEVEL),
    CMD_NAME_USERID_INIT("--mbhl-boost",  mbhl_boost, PARAM_MBHL_BOOST_I32),
    CMD_NAME_USERID_INIT("--mbhl-threshold",  mbhl_threshold, PARAM_MBHL_THRESHOLD_I32),
    CMD_NAME_USERID_INIT("--mbhl-slow-offset",  mbhl_slow_offset, PARAM_MBHL_SLOW_OFFSET_I32),
    CMD_NAME_USERID_INIT("--mbhl-fast-attack",  mbhl_fast_attack, PARAM_MBHL_FAST_ATTACK_I32),
    CMD_NAME_USERID_INIT("--mbhl-fast-release",  mbhl_fast_release, PARAM_MBHL_FAST_RELEASE_I32),
    CMD_NAME_USERID_INIT("--mbhl-slow-attack",  mbhl_slow_attack, PARAM_MBHL_SLOW_ATTACK_I32),
    CMD_NAME_USERID_INIT("--mbhl-slow-release",  mbhl_slow_release, PARAM_MBHL_SLOW_RELEASE_I32),
    CMD_NAME_USERID_INIT("--mbhl-delay",  mbhl_delay, PARAM_MBHL_DELAY_I32),
    CMD_NAME_USERID_INIT("--mbhl-envelope-frequency",  mbhl_envelope_frequency, PARAM_MBHL_ENVELOPE_FREQUENCY_I32),
    CMD_NAME_USERID_INIT("--mbhl-mode",  mbhl_mode, PARAM_MBHL_MODE_I32),
    CMD_NAME_USERID_INIT("--mbhl-cross-low",  mbhl_cross_low, PARAM_MBHL_CROSS_LOW_I32),
    CMD_NAME_USERID_INIT("--mbhl-cross-mid",  mbhl_cross_mid, PARAM_MBHL_CROSS_MID_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-attacks",  mbhl_comp_attacks, PARAM_MBHL_COMP_ATTACK_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-low-release",  mbhl_comp_low_release, PARAM_MBHL_COMP_LOW_RELEASE_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-low-ratio",  mbhl_comp_low_ratio, PARAM_MBHL_COMP_LOW_RATIO_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-low-thresh",  mbhl_comp_low_thresh, PARAM_MBHL_COMP_LOW_THRESH_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-low-makeup",  mbhl_comp_low_makeup, PARAM_MBHL_COMP_LOW_MAKEUP_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-mid-release",  mbhl_comp_mid_release, PARAM_MBHL_COMP_MID_RELEASE_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-mid-ratio",  mbhl_comp_mid_ratio, PARAM_MBHL_COMP_MID_RATIO_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-mid-thresh",  mbhl_comp_mid_thresh, PARAM_MBHL_COMP_MID_THRESH_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-mid-makeup",  mbhl_comp_mid_makeup, PARAM_MBHL_COMP_MID_MAKEUP_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-high-release",  mbhl_comp_high_release, PARAM_MBHL_COMP_HIGH_RELEASE_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-high-ratio",  mbhl_comp_high_ratio, PARAM_MBHL_COMP_HIGH_RATIO_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-high-thresh",  mbhl_comp_high_thresh, PARAM_MBHL_COMP_HIGH_THRESH_I32),
    CMD_NAME_USERID_INIT("--mbhl-comp-high-makeup",  mbhl_comp_high_makeup, PARAM_MBHL_COMP_HIGH_MAKEUP_I32),
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
    CMD_NAME_USERID_INIT("--aeq-enable", aeq_enable, PARAM_AEQ_ENABLE_I32),
    CMD_NAME_USERID_INIT("--aeq-app-ch-link-mask", aeq_app_ch_link_mask, PARAM_AEQ_CH_LINK_MASK_I32),
    CMD_NAME_USERID_INIT("--aeq-ch-ctrl-mask", aeq_ch_ctrl_mask, PARAM_AEQ_CH_CTRL_MASK),
    CMD_NAME_USERID_INIT("--aeq-input-gain",   aeq_input_gain, PARAM_AEQ_INPUT_GAIN_I16),
    CMD_NAME_USERID_INIT("--aeq-output-gain",  aeq_output_gain, PARAM_AEQ_OUTPUT_GAIN_I16),
    CMD_NAME_USERID_INIT("--aeq-bypass-gain",  aeq_bypass_gain, PARAM_AEQ_BYPASS_GAIN_I16),
    CMD_NAME_USERID_INIT("--aeq-discard", aeq_discard, PARAM_AEQ_PROCESS_DISCARD_I32),
    //Band parameters
    CMD_NAME_USERID_INIT("--aeq-app-band-num", aeq_app_band_num, PARAM_AEQ_BAND_Num),
    CMD_NAME_USERID_INIT("--aeq-app-band-en", aeq_app_band_en, PARAM_AEQ_BAND_En),
    CMD_NAME_USERID_INIT("--aeq-app-band-freq", aeq_app_band_freq, PARAM_AEQ_BAND_Fre),
    CMD_NAME_USERID_INIT("--aeq-app-band-gain", aeq_app_band_gain, PARAM_AEQ_BAND_Gain),
    CMD_NAME_USERID_INIT("--aeq-app-band-q", aeq_app_band_q, PARAM_AEQ_BAND_Q),
    CMD_NAME_USERID_INIT("--aeq-app-band-type", aeq_app_band_type, PARAM_AEQ_BAND_type),
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
    CMD_NAME_USERID_INIT("--geq-enable", geq_enable, PARAM_GEQ_En),
    CMD_NAME_USERID_INIT("--geq-ch-ctrl-mask", geq_ch_ctrl_mask, PARAM_GEQ_CTRL_MASK),
    CMD_NAME_USERID_INIT("--geq-input-gain", geq_input_gain, PARAM_GEQ_INPUT_GAIN),
    CMD_NAME_USERID_INIT("--geq-band0-gain", geq_band0_gain, PARAM_GEQ_BAND0_Gain),
    CMD_NAME_USERID_INIT("--geq-band1-gain", geq_band1_gain, PARAM_GEQ_BAND1_Gain),
    CMD_NAME_USERID_INIT("--geq-band2-gain", geq_band2_gain, PARAM_GEQ_BAND2_Gain),
    CMD_NAME_USERID_INIT("--geq-band3-gain", geq_band3_gain, PARAM_GEQ_BAND3_Gain),
    CMD_NAME_USERID_INIT("--geq-band4-gain", geq_band4_gain, PARAM_GEQ_BAND4_Gain),
    CMD_NAME_USERID_INIT("--geq-band5-gain", geq_band5_gain, PARAM_GEQ_BAND5_Gain),
    CMD_NAME_USERID_INIT("--geq-band6-gain", geq_band6_gain, PARAM_GEQ_BAND6_Gain),
    CMD_NAME_USERID_INIT("--geq-band7-gain", geq_band7_gain, PARAM_GEQ_BAND7_Gain),
    CMD_NAME_USERID_INIT("--geq-band8-gain", geq_band8_gain, PARAM_GEQ_BAND8_Gain),
    CMD_NAME_USERID_INIT("--geq-band9-gain", geq_band9_gain, PARAM_GEQ_BAND9_Gain),
    CMD_NAME_USERID_INIT("--geq-discard", geq_discard, PARAM_GEQ_DISCARD),
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
    CMD_NAME_USERID_INIT("--ildetect-upmix-en", ildetect_upmix_en, PARAM_IL_UPMIX_EN),
    CMD_NAME_USERID_INIT("--ildetect-upmix-low-level-threshold", ildetect_upmix_low_level_threshold, PARAM_IL_UPMIX_LOW_LEVEL_THRES),
    CMD_NAME_USERID_INIT("--ildetect-upmix-peak-hold-count", ildetect_upmix_peak_hold_count, PARAM_IL_UPMIX_PEAK_HOLD_COUNT),
    CMD_NAME_USERID_INIT("--ildetect-upmix-orig-lr-mix", ildetect_upmix_orig_lr_mix, PARAM_IL_UPMIX_ORIG_LR_MIX),
    CMD_NAME_USERID_INIT("--ildetect-upmix-srrnd-gain", ildetect_upmix_srrnd_gain, PARAM_IL_UPMIX_SRRND_GAIN),
    CMD_NAME_USERID_INIT("--ildetect-upmix-srrnd-delay-en", ildetect_upmix_srrnd_delay_en, PARAM_IL_UPMIX_SRRND_DELAY_EN),
    CMD_NAME_USERID_INIT("--ildetect-upmix-discard", ildetect_upmix_discard, PARAM_IL_UPMIX_DISCARD),
};

enum vx_show_config_id {
    show_config = 0,
};

static VX_Cmd_Param vx_show_config_params[] = {
    CMD_NAME_USERID_INIT("--show-config",  show_config, AUDIO_ALL_PARAM_DUMP),
};

enum vx_ui_interface_id {
    ui_vx_enable = 0,
    ui_vx_in_channel_num,
    ui_vx_out_channel_num,
    ui_vx_surround_mode,
    ui_vx_dc_mode,
    ui_vx_tru_vol,
    ui_vx_loudness_en,
    ui_vx_loudness_io_mode,
    ui_vx_aeq_en,
    ui_vx_effect_en,
    vx_user_mode,
};

static VX_Cmd_Param vx_ui_interface_params[] = {
    CMD_NAME_USERID_INIT("--am-ui-vx-en",  ui_vx_enable, VIRTUALX_PARAM_ENABLE),
    CMD_NAME_USERID_INIT("--am-ui-vx-in-ch",  ui_vx_in_channel_num, PARAM_CHANNEL_NUM),
    CMD_NAME_USERID_INIT("--am-ui-vx-out-ch",  ui_vx_out_channel_num, PARAM_OUT_CHANNEL_NUM),
    CMD_NAME_USERID_INIT("--am-ui-vx-surd-mode",  ui_vx_surround_mode, VIRTUALX_PARAM_SURROUND_MODE),
    CMD_NAME_USERID_INIT("--am-ui-vx-surd-mode",  ui_vx_dc_mode, VIRTUALX_PARAM_DIALOGCLARITY_MODE),
    CMD_NAME_USERID_INIT("--am-ui-vx-tru-vol",  ui_vx_tru_vol, AUDIO_PARAM_TYPE_TRU_VOLUME),
    CMD_NAME_USERID_INIT("--am-ui-vx-loudness-ctrl",  ui_vx_loudness_en, PARAM_LOUDNESS_CONTROL_ENABLE_I32),
    CMD_NAME_USERID_INIT("--am-ui-vx-loudness-io-mode",  ui_vx_loudness_io_mode, PARAM_LOUDNESS_CONTROL_IO_MODE_I32),
    CMD_NAME_USERID_INIT("--am-ui-vx-aeq-en",  ui_vx_aeq_en, PARAM_AEQ_ENABLE_I32),
    CMD_NAME_USERID_INIT("--am-ui-vx-effect-en",  ui_vx_effect_en, VIRTUALX_EFFECT_ENABLE),
    CMD_NAME_USERID_INIT("--am-ui-vx-mode",  vx_user_mode, VIRTUALX_USER_MODE),
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

enum param_type_t param_index_to_param_type(int userId)
{
    if (userId == AUDIO_ALL_PARAM_DUMP) {
        return PARAM_TYPE_DEBUG;
    } else if ((userId == PARAM_CHANNEL_NUM) ||
        (userId == PARAM_OUT_CHANNEL_NUM) ||
        (userId == VIRTUALX_PARAM_ENABLE) ||
        (userId == VIRTUALX_PARAM_SURROUND_MODE) ||
        (userId == VIRTUALX_PARAM_DIALOGCLARITY_MODE) ||
        (userId == AUDIO_PARAM_TYPE_TRU_VOLUME) ||
        (userId == PARAM_LOUDNESS_CONTROL_ENABLE_I32) ||
        (userId == PARAM_LOUDNESS_CONTROL_IO_MODE_I32) ||
        (userId == PARAM_AEQ_ENABLE_I32) ||
        (userId == VIRTUALX_EFFECT_ENABLE) ||
        (userId == VIRTUALX_USER_MODE)) {
        return PARAM_TYPE_AM_UI;
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
    LOG("%s() Error, Unsupport paramIndex:%d\n",__func__, userId);
    return PARAM_TYPE_MAX;
}

int get_vx_cmd_table_from_paramType(VX_Cmd_Param **table, int *size, int paramType)
{
    if (paramType == PARAM_TYPE_MAX) {
        LOG("%s() Invalid paramType:%d", __func__, paramType);
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
    case PARAM_TYPE_AM_UI:
        *table = vx_ui_interface_params;
        *size = AM_ARRAY_SIZE(vx_ui_interface_params);
        break;
    default:
         LOG("%s() unknown paramType:%d \n", __func__, paramType);
        return -BAD_VALUE;
    }
    return 0;
}

VX_Cmd_Param * get_VXCmd_from_param_name(const char *paramName)
{
    if (!paramName) {
        LOG("%s() param name = NULL!\n", __func__);
        return NULL;
    }

    enum param_type_t paramType = param_name_to_param_type(paramName);
    if (paramType == PARAM_TYPE_MAX) {
        LOG("%s() can not get param_type from paramName:%s\n", __func__, paramName);
        return NULL;
    }

    VX_Cmd_Param *vxCmdTable = NULL;
    int tableSize = 0;
    VX_Cmd_Param *retVxCmd = NULL;
    int ret = 0;

    ret = get_vx_cmd_table_from_paramType(&vxCmdTable, &tableSize, paramType);
    if (ret < 0 || !vxCmdTable || !tableSize) {
        LOG("%s() Not find VX_Cmd_Param table\n", __func__);
        return NULL;
    }

    for (int i = 0; i < tableSize; i++) {
        VX_Cmd_Param *temp = &vxCmdTable[i];
        if (strncmp(temp->param, paramName, strlen(temp->param)) == 0) {
            retVxCmd = temp;
            break;
        }
    }
    return retVxCmd;
}

VX_Cmd_Param * get_VXCmd_from_param_index(int paramIndex)
{
    VX_Cmd_Param *vxCmdTable = NULL;
    int tableSize = 0;
    VX_Cmd_Param *retVxCmd = NULL;
    int ret  = 0;
    int paramType = param_index_to_param_type(paramIndex);

    ret = get_vx_cmd_table_from_paramType(&vxCmdTable, &tableSize, paramType);
    if (ret < 0 || !vxCmdTable || !tableSize) {
        LOG("%s() Can't find VX_Cmd_Param table!\n", __func__);
        return NULL;
    }

    for (int i = 0; i < tableSize; i++) {
        VX_Cmd_Param *temp = &vxCmdTable[i];
        if (temp->userId == paramIndex) {
            retVxCmd = temp;
            break;
        }
    }

    return retVxCmd;
}

/******************************** End ********************************************/




void init_effect_param_data(Virtualx_param_t *vxParam, int paramIndex, const char *vbuf)
{
    if (paramIndex < 0 || !vbuf) {
        return;
    }

    vxParam->param.psize = 4;
    vxParam->command = paramIndex;

    /* init effect_param_t with integer type value */
    if ((paramIndex == PARAM_CHANNEL_NUM) ||
        (paramIndex == PARAM_OUT_CHANNEL_NUM) ||
        (paramIndex == VIRTUALX_PARAM_ENABLE) ||
        (paramIndex == VIRTUALX_PARAM_SURROUND_MODE) ||
        (paramIndex == VIRTUALX_PARAM_DIALOGCLARITY_MODE) ||
        (paramIndex == AUDIO_PARAM_TYPE_TRU_VOLUME) ||
        (paramIndex == PARAM_LOUDNESS_CONTROL_ENABLE_I32) ||
        (paramIndex == PARAM_LOUDNESS_CONTROL_IO_MODE_I32) ||
        (paramIndex == PARAM_AEQ_ENABLE_I32) ||
        (paramIndex == VIRTUALX_EFFECT_ENABLE) ||
        (paramIndex == VIRTUALX_USER_MODE))
    {
        vxParam->param.vsize = sizeof(int);
        sscanf(vbuf, "%d", &vxParam->v);
        LOGV(">>[Line: %d] ParamIndex:%d, ParamValueInt:%d \n", __LINE__, paramIndex, vxParam->v);
    }
    else /* c-string type value */
    {
        vxParam->param.vsize = MAX_PARAM_VALUE_LEN;
        strncpy(vxParam->str, vbuf, MAX_PARAM_VALUE_LEN);
        LOGV(">>[Line: %d] ParamIndex:%d, ParamValueBuf:%s \n", __LINE__, paramIndex, vxParam->str);
    }
}


int set_param_from_cmd_line(int argc __unused, char **argv)
{
    int effectIndex = 0;
    int paramIndex = -1;
    char paramIndexBuf[128] = {0};
    Virtualx_param_t *vxParam  = getVxParamInstance();
    VX_Cmd_Param *vxCmd = getVxCmdParamInstance();

    memset(vxParam, 0, sizeof(Virtualx_param_t));
    setVxCmdParamInstance(NULL);

    sscanf(argv[1], "%d", &effectIndex);
    strncpy(paramIndexBuf, argv[2], 128);

    if (strstr(paramIndexBuf, "--") != NULL)
    {
        vxCmd = get_VXCmd_from_param_name(paramIndexBuf);
        if (!vxCmd) {
            LOG(">>main() parse <ParamIndex> Failed! paramIndexBuf:%s\n", paramIndexBuf);
            return -1;
        }

        setVxCmdParamInstance(vxCmd);
        paramIndex = vxCmd->userId;
    }
    else
    {
        if (sscanf(paramIndexBuf, "%d", &paramIndex) != 1) {
            LOG(">>main() [%d] <ParamIndex> Failed!\n",__LINE__);
            return -1;
        }

        vxCmd = get_VXCmd_from_param_index(paramIndex);
        if (!vxCmd) {
            LOG(">>main() [%d] Not find VXCmd for ParamIndex:%d\n",__LINE__, paramIndex);
            return -1;
        }

        setVxCmdParamInstance(vxCmd);
    }

    //-------------------set parameter value by paramIndex integer -----------------------------
    init_effect_param_data(vxParam, paramIndex, argv[3]);
    LOGV(">>main() [%d] EffectIndex:%d, ParamIndex:%d, ParamValueBuf:%s\n",__LINE__, effectIndex, paramIndex, argv[3]);
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

int getCStrData (char *data, char *value)
{
    int status = scanf("%s%s", data, value);
    if (status == 0) {
        goto _exit_clear;
    }

    return 0;

_exit_clear:
    int status_temp = scanf("%*s");
    if (status_temp < 0) {
        LOG("Erorr input! Pls Retry!\n");
    }
    return -1;
}

void removeSpaces_for_key(char str[]) {
    int i = 0, j = 0;
    while (str[i] != '\0') {
        if (str[i] != ' ') {
            str[j++] = str[i];
        }
        i++;
    }
    str[j] = '\0';
}

void removeSpaces_for_value(char str[]) {
    int i = 0, j = 0;
    while (str[i] != '\0') {
        if (str[i] != ' ') {
            str[j++] = str[i];
        }
        i++;
    }
    str[j] = '\0';

    j = strlen(str);;
    while (j-- > 0) {
        if (!isdigit(str[j])) {
            str[j] = 0;
        } else {
            break;
        }
    }
}

void split_string(char *str, char *key, char *value)
{
    char *token;
    token = strtok(str, "=");
    strcpy(key, token);
    token = strtok(NULL, "=");
    strcpy(value, token);
}

int set_param_for_file_cmd_line(sp<AudioEffect>& gAudioEffect, char *line_buf)
{
    if (!line_buf || strlen(line_buf) < 4) {
        LOG("%s() Invalid setting line:%s\n", __func__, line_buf);
        return -1;
    }

    char key[64] = {0};
    char value[16] = {0};

    split_string(line_buf, key, value);
    removeSpaces_for_key(key);
    removeSpaces_for_value(value);

    VX_Cmd_Param *vxCmd = NULL;
    Virtualx_param_t vxParamInstance;
    Virtualx_param_t *vxParam = &vxParamInstance;
    int paramIndex = 0;
    int ret = 0;

    memset(vxParam, 0, sizeof(Virtualx_param_t));

    vxCmd = get_VXCmd_from_param_name(key);
    if (!vxCmd) {
        LOG("[Failed] Not find VxCmd for ParamName:%s\n", key);
        return -1;
    }
    paramIndex = vxCmd->userId;
    init_effect_param_data(vxParam, paramIndex, value);

    if (gAudioEffect->setParameter(&vxParam->param) != 0) {
        ret = -1;
    }

    usleep(5000);
    LOG("[setParameter] [%s] [%s=%s]\n", (ret == 0 ? "OK" : "NG"), key, value);
    return ret;
}

void load_ini_session(sp<AudioEffect>& gAudioEffect, const char *iniName)
{
    ssize_t read;
    char *line = NULL;
    size_t len = 0;
    char filePathName[128] = {0};
    int caseNum = -1;
    int found = 0;
    int found_common = 0;
    int load_common_setting = 0;
    int param_count = 0;

    snprintf(filePathName, 128, "/mnt/vendor/odm_ext/etc/tvconfig/audio/%s", iniName);
    FILE* fp = fopen(filePathName, "r");
    if (!fp) {
        LOG("[Failed] fopen() %s\n", filePathName);
        return;
    }

    LOG("Please enter Test case Num: ");
    if (getIntData (&caseNum) < 0) {
        LOG("[Failed] Invalid case Num, Exit!\n");
        return;
    }

    LOG("[Test case %d] Start ...\n", caseNum);
    while ((read = getline(&line, &len, fp)) > 0)
    {
        if (load_common_setting == 0) {
            if (found_common == 0) {
                if (strstr(line, "common") != NULL) {
                    found_common = 1;
                    continue;
                }
            }
            else
            {
                if (strstr(line, "#")) { //one case setting end tag is "#"
                    load_common_setting = 1;
                    LOG("[Test case %d] load common setting done!\n", caseNum);
                    continue;;
                }
                set_param_for_file_cmd_line(gAudioEffect, line);
            }
            continue;
        }

        if (caseNum == 666) //load all setting without case
        {
            set_param_for_file_cmd_line(gAudioEffect, line);
        }
        else
        { //load specified case number setting
            if (found == 0) {
                char *temp = NULL;
                if ((temp = strstr(line, "case_")) != NULL) {
                    int temp_case_num = atoi(temp + 5);
                    if (temp_case_num == caseNum) {
                        found = 1;
                        continue;
                    }
                }
            }

            if (found) {
                if (strstr(line, "#")) { //one case setting end tag is "#"
                    break;
                }

                set_param_for_file_cmd_line(gAudioEffect, line);
                param_count++;
            }
        }
    }
    LOG("[Test case %d] Done! Apply params:%d\n", caseNum, param_count);
}

//Only support part of paramIndex setting
//The param value type must be: single integer or single float input
int set_param_from_scanf(sp<AudioEffect>& gAudioEffect, int effectIndex __unused)
{
    int paramIndex = -1;
    char paramIndexBuf[128] = {0};
    char valueBuf[MAX_PARAM_VALUE_LEN] = {0};
    Virtualx_param_t *vxParam = getVxParamInstance();
    VX_Cmd_Param *vxCmd = getVxCmdParamInstance();

    memset(vxParam, 0, sizeof(Virtualx_param_t));
    setVxCmdParamInstance(NULL);

    if (getCStrData(paramIndexBuf, valueBuf) < 0) {
        LOG(">>scanf() [%d] <ParamIndex> Failed!\n", __LINE__);
        return -1;
    }

    if (strstr(paramIndexBuf, "--") != NULL)
    {
        VX_Cmd_Param *vxCmd = get_VXCmd_from_param_name(paramIndexBuf);
        if (!vxCmd) {
            LOG(">>scanf() [%d] Can't find <ParamIndex> for ParamName:%s\n",__LINE__, paramIndexBuf);
            return -1;
        }
        setVxCmdParamInstance(vxCmd);
        paramIndex = vxCmd->userId;

        if (paramIndex == DTS_PARAM_READE_INI) {
            goto _load_ini;
        }
    }
    else
    {
        if (sscanf(paramIndexBuf, "%d", &paramIndex) != 1) {
            LOG(">>scanf() [%d] <ParamIndex> Failed!\n",__LINE__);
            return -1;
        }

        if (paramIndex == DTS_PARAM_READE_INI) {
            goto _load_ini;
        }

        vxCmd = get_VXCmd_from_param_index(paramIndex);
        if (!vxCmd) {
            LOG(">>scanf() [%d] Not find VXCmd for ParamIndex:%d\n",__LINE__, paramIndex);
            return -1;
        }
        setVxCmdParamInstance(vxCmd);
    }

    //-------------------set parameter value by paramIndex integer -----------------------------
    init_effect_param_data(vxParam, paramIndex, valueBuf);

    LOGV(">>scanf() [%d] EffectIndex:%d, ParamIndex:%d, ParamValueBuf:%s\n",__LINE__, effectIndex, paramIndex, vxParam->str);
    return paramIndex;

_load_ini:
    load_ini_session(gAudioEffect, valueBuf);
    return -1;
}


int Virtualx_effect_func(sp<AudioEffect>& gAudioEffect, int paramIndex)
{
    VX_Cmd_Param *vxCmd = getVxCmdParamInstance();
    if (!vxCmd) {
        LOG("Warning, VxCmd is NULL!\n");
        return -1;
    }

    if (gAudioEffect == NULL) {
        LOG("Invalid gAudioEffect = NULL\n");
        return -1;
    }

    Virtualx_param_t *vxParam = getVxParamInstance();
    int ret = 0;

    if (gAudioEffect->setParameter(&vxParam->param) != 0) {
        ret = -1;
    }

    if ((paramIndex == PARAM_CHANNEL_NUM) ||
        (paramIndex == PARAM_OUT_CHANNEL_NUM) ||
        (paramIndex == VIRTUALX_PARAM_ENABLE) ||
        (paramIndex == VIRTUALX_PARAM_SURROUND_MODE) ||
        (paramIndex == VIRTUALX_PARAM_DIALOGCLARITY_MODE) ||
        (paramIndex == AUDIO_PARAM_TYPE_TRU_VOLUME) ||
        (paramIndex == PARAM_LOUDNESS_CONTROL_ENABLE_I32) ||
        (paramIndex == PARAM_LOUDNESS_CONTROL_IO_MODE_I32) ||
        (paramIndex == PARAM_AEQ_ENABLE_I32) ||
        (paramIndex == VIRTUALX_EFFECT_ENABLE))
    {
        LOG("->[%s] ParamIndex:%d ParamValType:Int %s = %d\n", (ret == 0? "OK" : "NG"), vxParam->command, vxCmd->param, vxParam->v);
    }
    else
    {
        LOG("->[%s] ParamIndex:%d ParamValType:CStr %s = %s\n", (ret == 0? "OK" : "NG"), vxParam->command, vxCmd->param, vxParam->str);
    }
    //gAudioEffect->getParameter(&vxParam->param);
    //LOG("\tParameter: ParamIndex:%d ParamValueBuf:%s\n", paramIndex, vxParam->str);
    return 0;
}

void printf_vx_help(char *name)
{
    LOG("*********************************Virtualx***********************************\n");
    LOG("VirtualX EffectIndex: %d\n", 6);
    LOG("Usage: %s %d <ParamIndex> <ParamValue/ParamScale/gParamBand>\n", name, 6);
    LOG("------------Multi band hard limiter----ParamIndex(from %d to %d)-----\n",
        (int)PARAM_MBHL_ENABLE_I32, (int)PARAM_MBHL_PARAM_MAX -1);
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
        (int)PARAM_TBHDX_PARAM_START + 1, (int)PARAM_TBHDX_PARAM_MAX -1);

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
        (int)PARAM_TSX_PARAM_START +1, (int)PARAM_TSX_PARAM_MAX -1);
    LOG("ParamIndex: %d -> TSX Enable\n", (int)PARAM_TSX_ENABLE_I32);
    LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
    LOG("ParamIndex: %d -> TSX Passive Matrix Upmixer Enable\n", (int)PARAM_TSX_PASSIVEMATRIXUPMIX_ENABLE_I32);
    LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
    LOG("ParamIndex: %d -> TSX Height Upmixer Enable\n", (int)PARAM_TSX_HEIGHT_UPMIX_ENABLE_I32);
    LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
    LOG("ParamIndex: %d -> TSX Height Mix Coeff\n", (int)PARAM_TSX_HEIGHTMIX_COEFF_I32);
    LOG("ParamScale: 0.5 ~ 2.0\n");
    LOG("ParamIndex: %d -> TSX Process Discard\n", (int)PARAM_TSX_PROCESS_DISCARD_I32);
    LOG("ParamValue: 0 -> Disable   1 -> Enable\n");
    LOG("ParamIndex: %d -> TSX Height Discard\n", (int)PARAM_TSX_HEIGHT_DISCARD_I32);
    LOG("ParamValue: 0 -> Disable   1 -> Enable\n");

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
    LOG("ParamIndex: %d -> AEQ  input gain\n", (int)PARAM_AEQ_INPUT_GAIN_I16);
    LOG("ParamScale: 0.000 ~ 1.0\n");
    LOG("ParamIndex: %d -> AEQ  output gain\n", (int)PARAM_AEQ_OUTPUT_GAIN_I16);
    LOG("ParamScale: 0.000 ~ 1.0\n");
    LOG("ParamIndex: %d -> AEQ  bypass gain\n", (int)PARAM_AEQ_BYPASS_GAIN_I16);
    LOG("ParamScale: 0.000 ~ 1.0\n");
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

