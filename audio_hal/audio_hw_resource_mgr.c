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

#define LOG_TAG "audio_hw_hal_resourcemgr"

#include <stdio.h>
#include <sys/types.h>
#include <pthread.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <system/audio.h>
#include <audio_route/audio_route.h>

#include "audio_hw_resource_mgr.h"
#include "audio_hw_resource_def.h"
#include "aml_alsa_mixer.h"
#include "audio_hw_utils.h"
#include "audio_hw.h"
#include "alsa_device_parser.h"

#define MIXER_XML_PATH "/vendor/etc/mixer_paths.xml"
//#define DEBUG_DUMP_PORT_INFO 1

enum chip_type_e {
    CHIP_ID_T5 = 0,
    CHIP_ID_T5D,
};

//confirm platform type when adev_open
struct platform_type {
    bool is_TV;
    bool is_BDS;
    bool is_SBR;
    bool is_STB;
};

typedef struct port_info_map {
    //KEY: IN_PORT or OUT_PORT
    int port;

    //VALUE
    audio_devices_t device;
    float gain;
    bool mute;
    bool fade_mute;
    //whether device connected or routed
    bool enable;
    //routed or connected count
    int count;
} port_info_map;

typedef struct audio_hw_resource_mgr
{
    struct aml_audio_device *adev;
    struct audio_route *ar;
    //ALSA mixer ctrl handle
    struct aml_mixer_handle *mixer_ctrl;
    struct platform_type platform_types;
    //TODO:
    enum chip_type_e chip_type;

    /*user may update device to primary audio HAL,
      but not do routing, such as BT,A2DP or multi-output.
      x_avail_devices: available devices for Audio primary
      x_routed_devices: devices routed by Audio primary
      x_devices_map: record all devices add or routing count and other info
    */
    //input devices routing description
    audio_devices_t in_avail_devices;
    audio_devices_t in_routed_devices;
    enum IN_PORT active_inport;
    port_info_map in_devices_map[INPORT_MAX];
    //input devices routing description
    audio_devices_t out_avail_devices;
    audio_devices_t out_routed_devices;
    enum OUT_PORT active_outport;
    port_info_map out_devices_map[OUTPORT_MAX];
    //hdmi connected flags
    bool bHDMIARCon;
    bool bHDMIConnected;
    bool bHDMIConnected_update;
    //do force routing ignore already routed count
    bool force_routing;
    pthread_mutex_t lock;
} audio_hw_resource_mgr;

struct audio_hw_resource_mgr *get_hw_resource_manger(struct aml_audio_device *adev);

static inline struct port_info_map * get_in_port_info_map(struct audio_hw_resource_mgr *mgr, int port_id)
{
    if (port_id > INPORT_MAX) {
        return NULL;
    }
    return &mgr->in_devices_map[port_id];
}

static inline struct port_info_map * get_out_port_info_map(struct audio_hw_resource_mgr *mgr, int port_id)
{
    if (port_id > OUTPORT_MAX) {
        return NULL;
    }
    return &mgr->out_devices_map[port_id];
}

static inline void set_port_enable(struct port_info_map *port_info, audio_devices_t device, bool enable)
{
    if (enable) {
        port_info->device |= device;
        port_info->count++;
        port_info->enable = true;
    } else {
        port_info->count--;
        if (port_info->count == 0) {
            port_info->enable = false;
            port_info->device &= ~device;
        }
    }
}

static inline bool is_port_enable(struct port_info_map *port_info)
{
    return port_info->enable;
}

static inline void dump_port_info(struct port_info_map *port)
{
    AM_LOGI("--- Dump device port info ---");
    AM_LOGI("\tport:%s device:0x%x enable:%d count:%d gain:%.4f",
        outputPort2Str(port->port), port->device, port->enable, port->count, port->gain);
}

static inline int get_device_enable_count(struct audio_hw_resource_mgr *mgr, audio_devices_t device)
{
    int dev_port;
    int ret = android_dev_convert_to_hal_dev(device, &dev_port);
    if (ret < 0 || (device == AUDIO_DEVICE_NONE)) {
        AM_LOGE("Warning! Not support android input device:0x%x routing!", device);
        return 0;
    }

    struct port_info_map * port_info = get_in_port_info_map(mgr, dev_port);
    return port_info->count;
}

static inline void update_routed_device_l(struct audio_hw_resource_mgr *mgr, audio_devices_t device, bool enable)
{
    int dev_port;
    int ret = android_dev_convert_to_hal_dev(device, &dev_port);
    if (ret < 0 || (device == AUDIO_DEVICE_NONE)) {
        AM_LOGE("Warning! Not support android input device:0x%x routing!", device);
        return;
    }

    struct port_info_map * port_info = get_in_port_info_map(mgr, dev_port);
    if (enable)
    {
        set_port_enable(port_info, device, enable);

        if (audio_is_input_device(device)) {
            mgr->in_routed_devices |= device;
            mgr->in_avail_devices |= device;
        } else if (audio_is_output_device(device)) {
            mgr->out_routed_devices |= device;
            mgr->out_avail_devices |= device;
        }
    }
    else
    {
        set_port_enable(port_info, device, enable);

        if (audio_is_input_device(device)) {
            mgr->in_routed_devices &= ~device;
            if (!is_port_enable(port_info)) {
                mgr->in_avail_devices &= ~device;
            }
        } else if (audio_is_output_device(device)) {
            mgr->out_routed_devices &= ~device;
            if (!is_port_enable(port_info)) {
                mgr->out_avail_devices &= ~device;
            }
        }
    }
}

static inline void update_avail_device_l(struct audio_hw_resource_mgr *mgr, audio_devices_t device, bool enable)
{
    int dev_port;
    int ret = android_dev_convert_to_hal_dev(device, &dev_port);
    if (ret < 0 || (device == AUDIO_DEVICE_NONE)) {
        AM_LOGE("Warning! Not support android input device:0x%x routing!", device);
        return;
    }

    struct port_info_map * port_info = get_in_port_info_map(mgr, dev_port);
    if (enable) {
        set_port_enable(port_info, device, enable);

        if (audio_is_input_device(device)) {
            mgr->in_avail_devices |= device;
        } else if (audio_is_output_device(device)) {
            mgr->out_avail_devices |= device;
        }
    } else {
        set_port_enable(port_info, device, enable);

        if (audio_is_input_device(device)) {
            if (!is_port_enable(port_info)) {
                mgr->in_avail_devices &= ~device;
            }
        } else if (audio_is_output_device(device)) {
            if (!is_port_enable(port_info)) {
                mgr->out_avail_devices &= ~device;
            }
        }
    }
}

enum IN_PORT get_active_inport(struct aml_audio_device *adev)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    return mgr->active_inport;
}

audio_devices_t get_avail_in_devices(struct aml_audio_device *adev)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    return mgr->in_avail_devices;
}

audio_devices_t get_avail_out_devices(struct aml_audio_device *adev)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    return mgr->out_avail_devices;
}


void set_inport_gain(struct aml_audio_device *adev, enum IN_PORT port, float gain)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    struct port_info_map * port_info = get_in_port_info_map(mgr, port);
    if (!port_info) {
        AM_LOGW("Waring,  port_info = NULL for in_port:%d", port);
        return;
    }
    port_info->gain = gain;
    AM_LOGI("port:%s gain:%0.4f",  inputPort2Str(port), port_info->gain);
}

float get_inport_gain(struct aml_audio_device *adev, enum IN_PORT port)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    struct port_info_map * port_info = get_in_port_info_map(mgr, port);
    if (!port_info) {
        AM_LOGW("Waring, port_info = NULL for in_port:%d return 0.0", port);
        return 0.0;
    }
    AM_LOGI("port:%s gain:%0.4f", inputPort2Str(port), port_info->gain);
    return port_info->gain;
}

float get_active_inport_gain(struct aml_audio_device *adev)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    enum IN_PORT port = mgr->active_inport;
    if (port == INPORT_NULL) {
        AM_LOGW("Waring! active_inport = NULL return 0.0");
        return 0.0;
    }

    struct port_info_map * port_info = get_in_port_info_map(mgr, port);
    if (!port_info) {
        AM_LOGW("Waring, port_info = NULL for in_port:%d return 0.0", port);
        return 0.0;
    }
    AM_LOGI("port:%s gain:%0.4f", inputPort2Str(port), port_info->gain);
    return port_info->gain;
}

int set_input_device_avail(struct aml_audio_device *adev, audio_devices_t in_device, bool enable)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    int inport;
    int ret = android_dev_convert_to_hal_dev(in_device, &inport);
    if (ret < 0) {
        AM_LOGE("Warning! Not support android input device:0x%x routing!",  in_device);
        return -EINVAL;
    }

    pthread_mutex_lock(&mgr->lock);

    mgr->active_inport = inport;
    update_avail_device_l(mgr, in_device, enable);

    pthread_mutex_unlock(&mgr->lock);
    AM_LOGD("device:0x%x avail_in_devices:%x routed_in_device:0x%x",
           in_device, mgr->in_avail_devices, mgr->in_routed_devices);
    return 0;
}

int set_output_device_avail(struct aml_audio_device *adev, audio_devices_t device, bool enable)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    int inport;
    int ret = android_dev_convert_to_hal_dev(device, &inport);
    if (ret < 0) {
        AM_LOGE("Warning! Not support android input device:0x%x routing!", device);
        return -EINVAL;
    }

    pthread_mutex_lock(&mgr->lock);
    update_avail_device_l(mgr, device, enable);

    if (device & AUDIO_DEVICE_OUT_HDMI_ARC) {
        mgr->bHDMIConnected = enable;
        mgr->bHDMIARCon = enable;
        mgr->bHDMIConnected_update = true;
    } else if (device == AUDIO_DEVICE_OUT_HDMI) {
        mgr->bHDMIConnected = enable;
        mgr->bHDMIConnected_update = true;
    }

    pthread_mutex_unlock(&mgr->lock);

    AM_LOGD("device:0x%x avail_out_devices:%x routed_out_devices:0x%x",
           device, mgr->out_avail_devices, mgr->out_routed_devices);
    return 0;
}

bool is_HDMI_connected(struct aml_audio_device *adev)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    return (mgr->bHDMIConnected ? true : false);
}

bool is_HDMI_reconnected(struct aml_audio_device *adev)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    return mgr->bHDMIConnected_update;
}

void set_HDMI_reconnected_flag(struct aml_audio_device *adev, bool enable)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    mgr->bHDMIConnected_update = enable;
}

bool is_arc_connected(struct aml_audio_device *adev)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    return mgr->bHDMIARCon;
}

//which source DMA is selected for input port
int set_audio_source_routing(struct aml_audio_device *adev, enum input_source audio_source)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    int ret = 0;
    bool is_auge = alsa_device_is_auge();
    int src = audio_source;
    if (is_auge)
    {
        switch (audio_source)
        {
        case LINEIN:
            src = TDMIN_A;
            break;
        case ATV:
            src = FRATV;
            break;
        case HDMIIN:
            src = FRHDMIRX;
            break;
        case ARCIN:
            src = EARCRX_DMAC;
            break;
        case SPDIFIN:
            src = SPDIFIN_AUGE;
            break;
        default:
            AM_LOGW("src: %d not support",  src);
            src = FRHDMIRX;
            break;
        }
    }

    pthread_mutex_lock(&mgr->lock);
    ret = aml_mixer_ctrl_set_int(mgr->mixer_ctrl, AML_MIXER_ID_AUDIO_IN_SRC, src);
    pthread_mutex_unlock(&mgr->lock);

    return ret;
}

void enable_device_force_routing(struct aml_audio_device *adev, bool enable)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    pthread_mutex_lock(&mgr->lock);
    mgr->force_routing = enable;
    pthread_mutex_unlock(&mgr->lock);
}

int do_input_device_routing(struct aml_audio_device *adev, audio_devices_t in_device, bool enable)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    struct port_info_map *port_info = NULL;
    int inport;
    int ret = android_dev_convert_to_hal_dev(in_device, &inport);
    if (ret < 0 || (in_device == AUDIO_DEVICE_NONE)) {
        AM_LOGE("Warning! [%s] un-support device:0x%x do nothing!", (enable? "ADD" : "RM"), in_device);
        return -EINVAL;
    }

    port_info = get_in_port_info_map(mgr, inport);
    if (!port_info) {
        AM_LOGE("Warning! [%s] get port_info fail! device:0x%x",(enable? "ADD" : "RM"), in_device);
        return -EINVAL;
    }

    pthread_mutex_lock(&mgr->lock);

    bool is_routed = in_device & mgr->in_routed_devices;
    if (enable && is_routed) {
        AM_LOGD("Warning! [%s] already routed device:0x%x, do nothing!",(enable? "ADD" : "RM"), in_device);
        goto routing_done;
    }

    if (!enable && !is_routed) {
        AM_LOGD("Warning! [%s] not routed device:0x%x, do nothing!",(enable? "ADD" : "RM"), in_device);
        goto routing_done;
    }

    switch (in_device)
    {
    case AUDIO_DEVICE_IN_BUILTIN_MIC:
    case AUDIO_DEVICE_IN_BACK_MIC:
        if (enable) {
            audio_route_apply_path(mgr->ar, "main_mic");
            //TODO:if do device routing then update it as active port
        } // Note: input device no un-routing mixer ctrl
        break;
    case AUDIO_DEVICE_IN_WIRED_HEADSET:
        if (enable) {
            audio_route_apply_path(mgr->ar, "headset-mic");
        }
        break;
    case AUDIO_DEVICE_IN_HDMI:
        if (enable) {
            audio_route_apply_path(mgr->ar, "hdmirx_in");
        }
        break;
    case AUDIO_DEVICE_IN_LINE:
        if (enable) {
            audio_route_apply_path(mgr->ar, "line_in");
        }
        break;
    default:
        AM_LOGW("Warning! un-support device:0x%x", in_device);
        ret = -EINVAL;
        break;
    }

    if (ret == 0) {
        update_routed_device_l(mgr, in_device, enable);
        audio_route_update_mixer(mgr->ar);
    }

#ifdef DEBUG_DUMP_PORT_INFO
    dump_port_info(port_info);
#endif
    AM_LOGD("[%s] device:0x%x avail_in_devices:%x routed_in_device:0x%x", (enable? "ADD" : "RM"),
           in_device, mgr->in_avail_devices, mgr->in_routed_devices);

routing_done:
    pthread_mutex_unlock(&mgr->lock);
    return ret;
}

int do_output_device_routing(struct aml_audio_device *adev, audio_devices_t out_device, bool enable)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    int outport;
    int ret = android_dev_convert_to_hal_dev(out_device, &outport);
    if (ret < 0 || (out_device == AUDIO_DEVICE_NONE)) {
        AM_LOGE("Warning! [%s] un-support device:0x%x routing!", (enable? "ADD" : "RM"), out_device);
        return -EINVAL;
    }

    struct port_info_map *port_info = get_out_port_info_map(mgr, outport);
    if (!port_info) {
        AM_LOGE("Warning! [%s] get port_info fail! device:0x%x", (enable? "ADD" : "RM"), out_device);
        return -EINVAL;
    }

    pthread_mutex_lock(&mgr->lock);

    if (!mgr->force_routing) {
        bool is_routed = out_device & mgr->out_routed_devices;
        if (enable && is_routed) {
            AM_LOGD("Warning! [%s] already routed device:0x%x do nothing!", (enable? "ADD" : "RM"), out_device);
            goto routing_done;
        }

        if (!enable && !is_routed) {
            AM_LOGW("Warning! [%s] not routed device:0x%x  do nothing!",  (enable? "ADD" : "RM"), out_device);
            goto routing_done;
        }
    }

    switch (out_device)
    {
    // case AUDIO_DEVICE_OUT_HDMI:
    case AUDIO_DEVICE_OUT_AUX_DIGITAL:
        if (enable) {
            audio_route_apply_path(mgr->ar, "hdmi");
        } else {
            audio_route_apply_path(mgr->ar, "hdmi_off");
        }
        break;
    case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
        if (enable) {
            audio_route_apply_path(mgr->ar, "headphone");
        } else{
            audio_route_apply_path(mgr->ar, "headphone_off");
        }
        break;
    case AUDIO_DEVICE_OUT_SPEAKER:
    case AUDIO_DEVICE_OUT_EARPIECE:
        if (enable) {
            audio_route_apply_path(mgr->ar, "speaker");
        } else {
            audio_route_apply_path(mgr->ar, "speaker_off");
        }
        break;
    default:
        AM_LOGW("Warning! un-support device:0x%x", out_device);
        ret = -EINVAL;
        break;
    }

    if (ret == 0) {
        if (!mgr->force_routing) {
            update_routed_device_l(mgr, out_device, enable);
        }
        audio_route_update_mixer(mgr->ar);
    }

#ifdef DEBUG_DUMP_PORT_INFO
    dump_port_info(port_info);
#endif
    AM_LOGD("[%s] device:0x%x avail_out_devices:%x routed_out_device:0x%x is_force:%d", (enable? "ADD" : "RM"),
           out_device, mgr->out_avail_devices, mgr->out_routed_devices, mgr->force_routing);

routing_done:
    pthread_mutex_unlock(&mgr->lock);
    return ret;
}


int set_output_device_mute(struct aml_audio_device *adev, audio_devices_t device, bool enable, bool use_fade)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    int outport;
    int ret = android_dev_convert_to_hal_dev(device, &outport);
    if (ret < 0 || (device == AUDIO_DEVICE_NONE)) {
        AM_LOGE("Warning! [%s] un-support device:0x%x", (enable? "mute" : "un-mute"), device);
        return -EINVAL;
    }

    struct port_info_map* port_info = get_in_port_info_map(mgr, outport);

    int extern_arc = 0;
    if (check_chip_name("t5", 3, mgr->mixer_ctrl) || check_chip_name("t5d", 3, mgr->mixer_ctrl)) {
        extern_arc = 1;
    }

    pthread_mutex_lock(&mgr->lock);

    switch (device)
    {
    case AUDIO_DEVICE_OUT_SPDIF:
        if (extern_arc) {
            aml_mixer_ctrl_set_int(mgr->mixer_ctrl, AML_MIXER_ID_SPDIF_B_MUTE, enable);
        } else{
            aml_mixer_ctrl_set_int(mgr->mixer_ctrl, AML_MIXER_ID_SPDIF_MUTE, enable);
        }
        port_info->mute = enable;
        break;
    case AUDIO_DEVICE_OUT_HDMI_ARC:
    case AUDIO_DEVICE_OUT_HDMI:
        if (extern_arc) {
            aml_mixer_ctrl_set_int(mgr->mixer_ctrl, AML_MIXER_ID_SPDIF_MUTE, enable);
        } else {
            if (is_earc_descrpt())
                aml_mixer_ctrl_set_int(mgr->mixer_ctrl, AML_MIXER_ID_ARC_EARC_SPDIFOUT_REG_MUTE, enable);
            else
                aml_mixer_ctrl_set_int(mgr->mixer_ctrl, AML_MIXER_ID_HDMI_ARC_AUDIO_ENABLE, !enable);
        }
        port_info->mute = enable;
        break;
    case AUDIO_DEVICE_OUT_SPEAKER:
        if (!use_fade) { //mute/un-mute speaker by routing
            if (enable) {
                audio_route_apply_path(mgr->ar, "speaker_off");
            } else {
                audio_route_apply_path(mgr->ar, "speaker");
            }
            audio_route_update_mixer(mgr->ar);
        } else { //mute/un-mute speaker by fade HW
            if (enable) {
                // Need reset fading status, keep alsa mixer and audio route same status.
                audio_route_apply_path(mgr->ar, "speaker_fadein");
                audio_route_update_mixer(mgr->ar);
                audio_route_apply_path(mgr->ar, "speaker_fadeout");
            } else {
                audio_route_apply_path(mgr->ar, "speaker_fadein");
            }
            audio_route_update_mixer(mgr->ar);
            port_info->fade_mute = enable;
        }
        port_info->mute = enable;
        break;
    default:
        ret = -EINVAL;
        AM_LOGW("Warning, un-support device:0x%x", device);
        break;
    }

    pthread_mutex_unlock(&mgr->lock);
    AM_LOGI("device:0x%x mute:%d port_mute:%d using_fade:%d", device, enable, port_info->mute, use_fade);
    return ret;
}

bool is_output_device_muted(struct aml_audio_device *adev,
        audio_devices_t device, bool fade_mute)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    struct port_info_map* port_info = NULL;
    int outport = 0, extern_arc = 0, ret = 0;
    bool muted = false;

    ret = android_dev_convert_to_hal_dev(device, &outport);
    if (ret < 0 || (device == AUDIO_DEVICE_NONE)) {
        AM_LOGE("Warning! un-support device:%#x", device);
        return false;
    }

    port_info = get_in_port_info_map(mgr, outport);
    AM_LOGI("device:%#x port_mute:%d, fade_mute:%d",
        device, port_info->mute, port_info->fade_mute);

    if (fade_mute)
        muted = port_info->fade_mute;
    else
        muted = port_info->mute;

    return muted;
}

struct audio_hw_resource_mgr *get_hw_resource_manger(struct aml_audio_device *adev)
{
    if (adev->hw_resource_mgr == NULL)
    {
        audio_hw_resource_mgr *mgr = aml_audio_calloc(1, sizeof(struct audio_hw_resource_mgr));
        adev->hw_resource_mgr = mgr;
    }
    return adev->hw_resource_mgr;
}


bool is_TV(struct aml_audio_device *adev)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    return mgr->platform_types.is_TV;
}

bool is_BDS(struct aml_audio_device *adev)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    return mgr->platform_types.is_BDS;
}

bool is_SBR(struct aml_audio_device *adev)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    return mgr->platform_types.is_SBR;
}

bool is_STB(struct aml_audio_device *adev)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    return mgr->platform_types.is_STB;
}

void confirm_platform_type(audio_hw_resource_mgr *mgr)
{
    /*[SEI-2018-10-29] add for HBG remote audio support } */
#if defined(TV_AUDIO_OUTPUT)
    mgr->platform_types.is_TV = true;
    /* by default, BDS will share the same audio feature as TV */
    mgr->platform_types.is_BDS = /*check_chip_name("t7", 2, &adev->alsa_mixer) ? true : */false;
    /*Now SoundBar type is depending on TV audio as only tv support multi-channel LPCM output*/
    mgr->platform_types.is_SBR = aml_audio_check_sbr_product();
    AM_LOGI("TV platform,soundbar platform %d", mgr->platform_types.is_SBR);
#else
    mgr->platform_types.is_STB = property_get_bool("ro.vendor.platform.is.stb", false);
    AM_LOGI("OTT platform");
#endif
}

/*TODO
int confirm_chip_type(audio_hw_resource_mgr *mgr)
{

}
*/

int init_audio_hw_resource_mgr(struct aml_audio_device *adev, struct aml_mixer_handle *mixer_ctrl)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    if (mixer_ctrl == NULL) {
        AM_LOGE("Error, alsa mixer ctrl = NULL, return!");
        return -EINVAL;
    }

    if (!mgr) {
        AM_LOGE("Error, hw_source_mgr = NULL!");
        return -EINVAL;
    }

    int ret = 0;

    mgr->adev = adev;
    mgr->mixer_ctrl = mixer_ctrl;
    mgr->ar = audio_route_init(adev->card, MIXER_XML_PATH);
    /* some external codec init time last longer, wait 1s before timeout */
    int retry_count = 0;
    while (mgr->ar == NULL)
    {
        usleep(20 * 1000); // 20MS
        mgr->ar = audio_route_init(adev->card, MIXER_XML_PATH);
        retry_count++;
        if (retry_count > 50)
            break;
    }

    if (mgr->ar == NULL) {
        AM_LOGE("audio route init failed");
        ret = -EINVAL;
    }

    mgr->active_inport = INPORT_NULL;
    mgr->active_outport = OUTPORT_NULL;

    pthread_mutex_init(&mgr->lock, NULL);

    audio_route_reset(mgr->ar);
    audio_route_update_mixer(mgr->ar);

    confirm_platform_type(mgr);
    AM_LOGI("OK");
    return ret;
}

void destroy_hw_resource_mgr(struct aml_audio_device *adev)
{
    audio_hw_resource_mgr *mgr = get_hw_resource_manger(adev);
    if (!mgr) {
        AM_LOGE("Error, hw_source_mgr = NULL!");
        return;
    }

    if (mgr->ar) {
        audio_route_free(mgr->ar);
        mgr->ar = NULL;
    }

    pthread_mutex_destroy(&mgr->lock);
    free(mgr);
    adev->hw_resource_mgr = NULL;
    AM_LOGI("done");
}
