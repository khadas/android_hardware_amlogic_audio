/*
 * Copyright (C) 2017 Amlogic Corporation.
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

#define LOG_TAG "audio_hw_patch_manager"

#include <sys/types.h>
#include <sys/errno.h>
#include <pthread.h>
#include <cutils/log.h>

#include "audio_hw.h"
#include "aml_audio_stream.h"
#include "audio_hw_resource_def.h"
#include "device_patch_mgr.h"
#include "device_patch.h"
#include "dtv_patch.h"
#include "tv_patch.h"
#include "aml_ng.h"
#include "audio_hw_utils.h"
#include "audio_hw_resource_mgr.h"
#include "alsa_device_parser.h"
#include "component_picture_mode.h"
#include "component_noise_gate.h"
#include "dtv_private_object.h"
#include "tv_private_object.h"

struct patch_manager;
typedef int (*create_patch_t)(struct patch_manager *, int, audio_devices_t, audio_devices_t, int);
typedef int (*release_patch_t)(struct patch_manager *, int);

#define PATCH_TYPE_TO_OFFSET(type) (type)


typedef struct patch_manager
{
    struct aml_audio_device *adev;
    // device to device patch context
    struct aml_audio_patch *audio_patch;
    // number of patch instance
    int count;
    // patch running flag
    bool audio_patching;
    // Only used for DTV
    bool patch_start;
    // used only for real TV source
    enum patch_src_assortion patch_src;
    // Which HW inport is selected for data source
    enum IN_PORT inport;
    bool valid;
    pthread_mutex_t lock;

    // operations of patch manger
    create_patch_t create_patch;
    release_patch_t release_patch;

    //object from adev only used by dtv case
    struct dtv_private_object *dtv_obj;
    //object from adev only used by tv case
    struct tv_private_object *tv_obj;
    // Others not directly related to patch from adev
    struct component_noise_gate noise_gate;
    struct component_picture_mode pic_mode;
} patch_manager;


//Patch manger APIs
static inline void acquire_patch_mgr_lock(struct patch_manager *patch_mgr)
{
    pthread_mutex_lock(&patch_mgr->lock);
}

static inline void release_patch_mgr_lock(struct patch_manager *patch_mgr)
{
    pthread_mutex_unlock(&patch_mgr->lock);
}

struct aml_audio_patch *get_patch_from_mgr(struct patch_manager *patch_mgr)
{
    struct aml_audio_patch *dev_patch = NULL;
    dev_patch = patch_mgr->audio_patch;
    return dev_patch;
}

static const char* patch_type_to_str(int patch_type)
{
    switch (patch_type)
    {
    case PATCH_TYPE_TV:
        return "TV";
    case PATCH_TYPE_DTV:
        return "DTV";
    default:
        return "unknown";
        break;
    }
}

static inline void set_patch_to_mgr(struct patch_manager *patch_mgr, struct aml_audio_patch *audio_patch)
{
    patch_mgr->audio_patch = audio_patch;
}

static inline bool is_patch_exist_mgr(struct patch_manager *patch_mgr)
{
    return (patch_mgr->audio_patch != NULL ? true : false);
}

static inline bool is_patch_valid_mgr(struct patch_manager *patch_mgr)
{
    return (patch_mgr->valid ? true : false);
}

static inline bool is_patch_running_mgr(struct patch_manager *patch_mgr)
{
    return (patch_mgr->audio_patching ? true : false);
}

static inline void set_patch_running_mgr(struct patch_manager *patch_mgr, bool enable)
{
    patch_mgr->audio_patching = enable;
}

static inline bool is_same_patch_source_mgr(struct patch_manager *patch_mgr, enum patch_src_assortion patch_src)
{
    return (patch_mgr->patch_src == patch_src ? true : false);
}

static inline void set_patch_source_mgr(struct patch_manager *patch_mgr, enum patch_src_assortion patch_src)
{
    patch_mgr->patch_src = patch_src;
}

static inline enum patch_src_assortion get_patch_source_mgr(struct patch_manager *patch_mgr)
{
    return patch_mgr->patch_src;
}

static inline void invalidate_patch_mgr(struct patch_manager *patch_mgr)
{
    patch_mgr->valid = false;
}

static inline void validate_patch_mgr(struct patch_manager *patch_mgr)
{
    patch_mgr->valid = true;
}

static inline void start_dtv_patch_mgr(struct patch_manager *patch_mgr)
{
    patch_mgr->patch_start = true;
}

static inline void stop_dtv_patch_mgr(struct patch_manager *patch_mgr)
{
    patch_mgr->patch_start = false;
}

static inline bool is_dtv_patch_exist_mgr(struct patch_manager *patch_mgr)
{
    bool cond_1 = is_same_patch_source_mgr(patch_mgr, SRC_DTV);
    bool cond_2 = is_patch_exist_mgr(patch_mgr) && get_patch_from_mgr(patch_mgr)->is_dtv_src;
    return (cond_1 || cond_2);
}

void do_patch_source_routing(struct patch_manager *patch_mgr, audio_devices_t input_device)
{
    enum input_source input_src = android_input_dev_convert_to_hal_input_src(input_device);
    enum IN_PORT inport = INPORT_HDMIIN;
    int ret = 0;

    ret = do_input_device_routing(patch_mgr->adev, input_device, true);
    //select source signal for selected in port
    if (input_src != SRC_NA) {
        set_audio_source_routing(patch_mgr->adev, input_src);
    }
}

static int create_patch_internal(struct patch_manager *patch_mgr,
                                 int patch_src,
                                 audio_devices_t src_device,
                                 audio_devices_t sink_device,
                                 int type)
{
    int ret = 0;
    int inport;
    ALOGI("%s() type:%s patch_src:%s in_device:0x%x out_device:0x%x",__func__,
        patch_type_to_str(type), patchSrc2Str(patch_src), src_device, sink_device);

    acquire_patch_mgr_lock(patch_mgr);

    // 1.Release exist old patch
    if (is_patch_exist_mgr(patch_mgr)) {
        struct aml_audio_patch *old_patch = get_patch_from_mgr(patch_mgr);
        ALOGD("%s: patch exists, first release it", __func__);
        ALOGD("%s: new input %#x, old input %#x", __func__, src_device, old_patch->input_src);
        if (type == PATCH_TYPE_TV) {
            release_tv_patch(patch_mgr->adev);
            set_patch_running_mgr(patch_mgr, false);
        }
#ifdef ENABLE_DVB_PATCH
        else if (type == PATCH_TYPE_DTV) {
            //release_dtv_patch(patch_mgr->adev);
            set_patch_running_mgr(patch_mgr, false);
        }
#endif
    }

    ret = android_dev_convert_to_hal_dev(src_device, (int *)&inport);
    if ((patch_src == SRC_INVAL) || ret < 0) {
        ALOGD("%s() Invalid patch_src:%s in_device:0x%x return!", __func__, patchSrc2Str(patch_src), src_device);
        ret = -EINVAL;
        goto exit;
    }

    //2.set patch source
    patch_mgr->patch_src = patch_src;

    //3.do source device routing and gain setting
    set_input_device_avail(patch_mgr->adev, src_device, true);
    set_inport_gain(patch_mgr->adev, inport, 1.0);
    if (type == PATCH_TYPE_TV) {
        do_patch_source_routing(patch_mgr, src_device);
    }

    // 4.Create new request patch
    switch (type)
    {
    case PATCH_TYPE_TV:
        ret = create_tv_patch(patch_mgr->adev, src_device, sink_device);
        if (ret == 0) {
            set_patch_running_mgr(patch_mgr, true);
        }
        break;
#ifdef ENABLE_DVB_PATCH
    case PATCH_TYPE_DTV:
        ret = create_dtv_patch((struct audio_hw_device *)patch_mgr->adev, AUDIO_DEVICE_IN_TV_TUNER, AUDIO_DEVICE_OUT_SPEAKER);
        if (ret == 0) {
            set_patch_running_mgr(patch_mgr, true);
        }
        break;
#endif
    default:
        ALOGE("%s() Error! Unknown patch_type:%d", __func__, type);
        break;
    }

    // 5.init noise gate for TV if needed
    if (src_device == AUDIO_DEVICE_IN_LINE) {
        struct component_noise_gate *noise_gate = &patch_mgr->noise_gate;
        if (noise_gate->aml_ng_enable) {
            noise_gate->aml_ng_handle = init_noise_gate(noise_gate->aml_ng_level,
                                                        noise_gate->aml_ng_attack_time,
                                                        noise_gate->aml_ng_release_time);
            ALOGE("%s: init amlogic noise gate: level: %fdB, attack_time = %dms, release_time = %dms",
                  __func__, noise_gate->aml_ng_level, noise_gate->aml_ng_attack_time, noise_gate->aml_ng_release_time);
        }
    }

exit:
    release_patch_mgr_lock(patch_mgr);
    return ret;
}

int release_patch_internal(struct patch_manager *patch_mgr, int type)
{
    int ret = 0;

    ALOGI("%s() type:%s patch_src:%s ",__func__,
        patch_type_to_str(type), patchSrc2Str(patch_mgr->patch_src));

    acquire_patch_mgr_lock(patch_mgr);

    switch (type)
    {
    case PATCH_TYPE_TV:
        if (!is_same_patch_source_mgr(patch_mgr, SRC_DTV) &&
            !is_same_patch_source_mgr(patch_mgr, SRC_INVAL) &&
            is_patch_running_mgr(patch_mgr))
        {
            ret = release_tv_patch(patch_mgr->adev);
        }
        break;
    case PATCH_TYPE_DTV:
        if (is_dtv_patch_exist_mgr(patch_mgr))
        {
#ifdef ENABLE_DVB_PATCH
            release_dtv_patch(patch_mgr->adev);
#endif
        }
        break;
    default:
        ALOGE("%s() Error! Unknown patch_type:%d", __func__, type);
        break;
    }

    /* when pip mode , two audio patch alive, if one audio patch released, the patching flag will be set to  0 */
    if (!is_patch_exist_mgr(patch_mgr))
    {
        set_patch_running_mgr(patch_mgr, false);
        /* save ATV src to deal with ATV HP hotplug */
        if (!is_same_patch_source_mgr(patch_mgr, SRC_ATV)) {
            set_patch_source_mgr(patch_mgr, SRC_INVAL);
        }
    }

    release_patch_mgr_lock(patch_mgr);

    return ret;
}

/******************************************
 * patch APIs wrap for adev
 ********************************************/

struct component_picture_mode *get_pic_mode_instance(struct aml_audio_device *adev)
{
    return &adev->patch_manager->pic_mode;
}

struct component_noise_gate *get_noise_gate_instance(struct aml_audio_device *adev)
{
    return &adev->patch_manager->noise_gate;
}

struct dtv_private_object *get_dtv_object(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = adev->patch_manager->dtv_obj;
    if (!dtv_obj) {
        dtv_obj = aml_audio_calloc(1, sizeof(struct dtv_private_object));
        adev->patch_manager->dtv_obj = dtv_obj;
        if (!dtv_obj) {
            ALOGE("%s() error, No memory!", __func__);
        }
    }
    return dtv_obj;
}

struct tv_private_object *get_tv_object(struct aml_audio_device *adev)
{
    struct tv_private_object *tv_obj = adev->patch_manager->tv_obj;
    if (!tv_obj) {
        tv_obj = aml_audio_calloc(1, sizeof(struct tv_private_object));
        adev->patch_manager->tv_obj = tv_obj;
        if (!tv_obj) {
            ALOGE("%s() Error, No memory!", __func__);
        }
    }
    return tv_obj;
}

bool is_dev_patch_exist(struct aml_audio_device *adev)
{
    return is_patch_exist_mgr(adev->patch_manager);
}

bool is_dtv_patch_exist(struct aml_audio_device *adev)
{
    return is_dtv_patch_exist_mgr(adev->patch_manager);
}

struct aml_audio_patch *get_dev_patch(struct aml_audio_device *adev)
{
    return get_patch_from_mgr(adev->patch_manager);
}

void set_dev_patch(struct aml_audio_device *adev, struct aml_audio_patch *audio_patch)
{
    set_patch_to_mgr(adev->patch_manager, audio_patch);
}

bool is_dev_patch_running(struct aml_audio_device *adev)
{
    return is_patch_running_mgr(adev->patch_manager);
}

void set_dev_patch_running(struct aml_audio_device *adev, bool enable)
{
    set_patch_running_mgr(adev->patch_manager, enable);
}

void set_dev_patch_src(struct aml_audio_device *adev, enum patch_src_assortion patch_src)
{
    set_patch_source_mgr(adev->patch_manager, patch_src);
}

int get_dev_patch_src(struct aml_audio_device *adev)
{
    return get_patch_source_mgr(adev->patch_manager);
};

bool is_same_patch_src(struct aml_audio_device *adev, enum patch_src_assortion patch_src)
{
    return is_same_patch_source_mgr(adev->patch_manager, patch_src);
}

bool is_dev_patch_valid(struct aml_audio_device *adev)
{
    return is_patch_valid_mgr(adev->patch_manager);
}

void invalidate_dev_patch(struct aml_audio_device *adev)
{
    invalidate_patch_mgr(adev->patch_manager);
}

void validate_dev_patch(struct aml_audio_device *adev)
{
    validate_patch_mgr(adev->patch_manager);
}

void start_dtv_patch(struct aml_audio_device *adev)
{
    start_dtv_patch_mgr(adev->patch_manager);
}

void stop_dtv_patch(struct aml_audio_device *adev)
{
    stop_dtv_patch_mgr(adev->patch_manager);
}

void acquire_dev_patch_lock(struct aml_audio_device *adev)
{
    acquire_patch_mgr_lock(adev->patch_manager);
}

void release_dev_patch_lock(struct aml_audio_device *adev)
{
    release_patch_mgr_lock(adev->patch_manager);
}

enum patch_src_assortion get_patch_source(struct aml_audio_device *adev, audio_devices_t src_device, int route_type __unused)
{
    enum patch_src_assortion patch_source;
    int inport;
    int ret;

    ret = android_dev_convert_to_hal_dev(src_device, (int *)&inport);
    if (ret < 0) {
        ALOGD("%s() Not support src_device:0x%x return!", __func__, src_device);
        return SRC_INVAL;
    }

    if (inport == INPORT_TUNER) {
        if (is_TV(adev)) {
            patch_source = SRC_ATV;
        } else {
            patch_source = SRC_DTV;
        }
    } else if (inport != INPORT_ECHO_REFERENCE && inport != INPORT_BUILTIN_MIC) {
        patch_source = android_input_dev_convert_to_hal_patch_src(src_device);
    } else {
        patch_source = SRC_INVAL;
        ALOGW("%s() Warning! Not support inport:%s", __func__, inputPort2Str(inport));
    }

    return patch_source;
}

//impl but not used
int get_patch_type(struct aml_audio_device *adev __unused, int inport, enum patch_src_assortion patch_src, enum patch_route_e route_type)
{
    int patch_type;

    switch (route_type) {
    case PATCH_ROUTE_DEV_DEV:
        if ((inport != INPORT_TUNER) || ((inport == INPORT_TUNER) && (patch_src == SRC_ATV))) {
            patch_type = PATCH_TYPE_TV;
        } else if ((inport == INPORT_TUNER) && (patch_src == SRC_DTV)) {
            patch_type = PATCH_TYPE_DTV;
        } else {
            patch_type = PATCH_TYPE_INVAL;
        }
        break;
    case PATCH_ROUTE_DEV_MIX:
        if (inport == INPORT_HDMIIN ||
            inport == INPORT_ARCIN  ||
            inport == INPORT_SPDIF  ||
            inport == INPORT_LINEIN ||
            ((inport == INPORT_TUNER) && (patch_src == SRC_ATV))) {
            patch_type = PATCH_TYPE_TV;
        } else if ((inport == INPORT_TUNER) && (patch_src == SRC_DTV)){
            patch_type = PATCH_TYPE_DTV;
        } else {
            patch_type = PATCH_TYPE_INVAL;
        }
        break;
    default:
        ALOGI("%s() Warning, unsupport patch_src:%s ");
        patch_type = PATCH_TYPE_INVAL;
        break;
    }

    return patch_type;
}

struct patch_manager *get_patch_manager(struct aml_audio_device *adev)
{
    if (adev->patch_manager == NULL)
    {
        struct patch_manager *mgr = aml_audio_calloc(1, sizeof(struct patch_manager));
        adev->patch_manager = mgr;
    }

    return adev->patch_manager;
}

int init_patch_manager(struct aml_audio_device *adev)
{
    int ret = 0;
    struct patch_manager *patch_mgr = get_patch_manager(adev);
    if (!patch_mgr)
    {
        ALOGW("%s() error! patch_mgr = NULL!", __func__);
        return -EINVAL;
    }

    //new & init tv_private_object
    ret = init_tv_object(adev);
    if (ret != 0) {
        goto err_exit;
    }

    ret = init_dtv_object(adev);
    if (ret != 0) {
        goto err_exit;
    }

    patch_mgr->adev = adev;
    patch_mgr->audio_patch = NULL;
    patch_mgr->audio_patching = false;
    patch_mgr->patch_start = false;
    patch_mgr->patch_src = SRC_INVAL;
    patch_mgr->valid = false;
    patch_mgr->create_patch = create_patch_internal;
    patch_mgr->release_patch = release_patch_internal;
    pthread_mutex_init(&patch_mgr->lock, NULL);

    ALOGI("%s() OK", __func__);
    return 0;

err_exit:
    destroy_tv_object(adev);
    destroy_dtv_object(adev);
    free(patch_mgr);
    adev->patch_manager = NULL;
    ALOGE("%s() Fail!", __func__);
    return -EINVAL;
}

void destroy_patch_manager(struct aml_audio_device *adev)
{
    struct patch_manager *patch_mgr = get_patch_manager(adev);
    if (!patch_mgr)
    {
        ALOGW("%s() error! patch_mgr = NULL!", __func__);
        return;
    }

    destroy_tv_object(adev);
    destroy_dtv_object(adev);

    deinit_noise_gate_wrap(adev);

    patch_mgr->valid = false;
    patch_mgr->audio_patching = false;
    patch_mgr->audio_patch = NULL;
    pthread_mutex_destroy(&patch_mgr->lock);
    free(patch_mgr);
    adev->patch_manager = NULL;
    ALOGI("%s() done!", __func__);
}

int patch_mgr_create_patch(struct aml_audio_device *adev,
                           int patch_source,
                           audio_devices_t input,
                           audio_devices_t output,
                           int type)
{
    int ret = 0;
    patch_manager *patch_mgr = adev->patch_manager;
    ret = patch_mgr->create_patch(patch_mgr, patch_source, input, output, type);
    return ret;
}

int patch_mgr_release_patch(struct aml_audio_device *adev, int type)
{
    int ret = 0;
    patch_manager *patch_mgr = adev->patch_manager;
    ret = patch_mgr->release_patch(patch_mgr, type);
    return ret;
}

int set_tv_source_switch_parameters(struct audio_hw_device *dev, struct str_parms *parms)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int ret = -1;
    char value[64] = {'\0'};

    /*----ATV <-> DTV switch----*/
    ret = str_parms_get_str(parms, "hal_param_tuner_in", value, sizeof(value));
    // tuner_in=atv: tuner_in=dtv
    if (ret >= 0 && is_TV(adev)) {
        if (strncmp(value, "dtv", 3) == 0) {
#ifdef ENABLE_DVB_PATCH
            // no audio patching in dtv
            if (is_dev_patch_running(adev) && is_same_patch_src(adev, SRC_ATV)) {
                // this is to handle atv->dtv case
                ret = patch_mgr_release_patch(adev, PATCH_TYPE_TV);
                ALOGI("%s, atv->dtv ret:%d", __func__, ret);
            }
            ALOGI("%s, now the audio patch src is %s, the audio_patching is %d ", __func__,
                patchSrc2Str(get_dev_patch_src(adev)), is_dev_patch_running(adev));

            if (is_same_patch_src(adev, SRC_DTV) && is_dev_patch_running(adev)) {
                ALOGI("[audiohal_kpi] %s dtv patch exit do nothing\n ", __func__);
            } else {
                ALOGI("[audiohal_kpi] %s, now create the dtv patch now\n ", __func__);
                ret = patch_mgr_create_patch(adev,
                                            SRC_DTV,
                                            AUDIO_DEVICE_IN_TV_TUNER,
                                            AUDIO_DEVICE_OUT_SPEAKER,
                                            PATCH_TYPE_DTV);
                ALOGI("[audiohal_kpi] %s, now end create dtv patch the audio_patching is %d ", __func__, is_dev_patch_running(adev));
            }
#endif
        } else if (strncmp(value, "atv", 3) == 0) {
#ifdef ENABLE_DVB_PATCH
            // need create patching
            if (is_same_patch_src(adev,SRC_DTV) && is_dev_patch_running(adev)) {
                ALOGI("[audiohal_kpi] %s, release dtv patching", __func__);
                ret = patch_mgr_release_patch(adev, PATCH_TYPE_DTV);
            }
#endif

            if (!is_dev_patch_running(adev)) {
                ALOGI("[audiohal_kpi] %s, create atv patching", __func__);
                ret = patch_mgr_create_patch(adev,
                        SRC_ATV,
                        AUDIO_DEVICE_IN_TV_TUNER,
                        AUDIO_DEVICE_OUT_SPEAKER,
                        PATCH_TYPE_TV);
            }
            set_dev_patch_src(adev, SRC_ATV);
        } else if (strncmp(value, "broadband", 9) == 0) {
#ifdef ENABLE_DVB_PATCH
            if (is_same_patch_src(adev, SRC_DTV) && is_dev_patch_running(adev)) {
                ALOGI("[audiohal_kpi] %s, release dtv patching", __func__);
                ret = patch_mgr_release_patch(adev, PATCH_TYPE_DTV);
            }
            set_dev_patch_src(adev, SRC_INVAL);
#endif
        }
        goto exit;
    }

    /*----HDMIIN <-> LINEIN switch----*/
    ret = str_parms_get_str(parms, "audio", value, sizeof(value));
    if (ret >= 0) {
        /*
         * This is a work around when plug in HDMI-DVI connector
         * first time application only recognize it as HDMI input device
         * then it can know it's DVI in, and then send "audio=linein" message to audio hal
         */
        struct audio_patch *pAudPatchTmp = NULL;
        if (strncmp(value, "linein", 6) == 0) {
            get_audio_patch_by_src_dev(dev, AUDIO_DEVICE_IN_HDMI, &pAudPatchTmp);
            if (pAudPatchTmp == NULL) {
                ALOGE("%s,There is no audio patch using HDMI as input", __func__);
                goto exit;
            }
            if (pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_HDMI) {
                ALOGE("%s, pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_HDMI", __func__);
                goto exit;
            }

            // dev->dev (example: HDMI in-> speaker out)
            if (pAudPatchTmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE
                && pAudPatchTmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
                // This "adev->audio_patch" will be created in create_patch() function
                if (is_dev_patch_exist(adev) && is_same_patch_src(adev, SRC_HDMIIN)) {
                    ALOGI("%s, create hdmi-dvi patching dev->dev", __func__);

                    patch_mgr_release_patch(adev, PATCH_TYPE_TV);

                    patch_mgr_create_patch(adev,
                                    SRC_LINEIN,
                                    AUDIO_DEVICE_IN_LINE,
                                    pAudPatchTmp->sinks[0].ext.device.type,
                                    PATCH_TYPE_TV);
                }
            }

            set_dev_patch_src(adev, SRC_LINEIN);
            pAudPatchTmp->sources[0].ext.device.type = AUDIO_DEVICE_IN_LINE;
            set_audio_source_routing(adev, LINEIN);
        } else if (strncmp(value, "hdmi", 4) == 0 && is_dev_patch_exist(adev)) {

            get_audio_patch_by_src_dev(dev, AUDIO_DEVICE_IN_LINE, &pAudPatchTmp);
            if (pAudPatchTmp == NULL) {
                ALOGE("%s,There is no audio patch using LINEIN as input", __func__);
                goto exit;
            }
            if (pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_LINE) {
                ALOGE("%s, pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_HDMI", __func__);
                goto exit;
            }

            // dev->dev (example: LINE in -> speaker out)
            if (pAudPatchTmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE
                && pAudPatchTmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
                // This "adev->audio_patch" will be created in create_patch() function
                if (is_dev_patch_exist(adev) && is_same_patch_src(adev, SRC_LINEIN)) {
                    ALOGI("%s, create dvi-hdmi patching dev->dev", __func__);
                    patch_mgr_release_patch(adev, PATCH_TYPE_TV);

                    patch_mgr_create_patch(adev,
                            SRC_HDMIIN,
                            AUDIO_DEVICE_IN_HDMI,
                            pAudPatchTmp->sinks[0].ext.device.type,
                            PATCH_TYPE_TV);
                }
            }

            set_dev_patch_src(adev, SRC_HDMIIN);
            pAudPatchTmp->sources[0].ext.device.type = AUDIO_DEVICE_IN_HDMI;
            set_audio_source_routing(adev, HDMIIN);
        }
        goto exit;
    }

exit:
    return ret;
}
