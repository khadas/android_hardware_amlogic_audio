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

#ifndef AML_HW_RESOURCE_H_
#define AML_HW_RESOURCE_H_

enum patch_src_assortion {
    SRC_DTV                     = 0,
    SRC_ATV                     = 1,
    SRC_LINEIN                  = 2,
    SRC_HDMIIN                  = 3,
    SRC_SPDIFIN                 = 4,
    SRC_REMOTE_SUBMIXIN         = 5,
    SRC_WIRED_HEADSETIN         = 6,
    SRC_BUILTIN_MIC             = 7,
    SRC_BT_SCO_HEADSET_MIC      = 8,
    SRC_ECHO_REFERENCE          = 9,
    SRC_ARCIN                   = 10,
    SRC_USB                     = 11,
    SRC_LOOPBACK                = 12,
    SRC_OTHER                   = 13,
    SRC_INVAL                   = 14
};

enum OUT_PORT {
    OUTPORT_SPEAKER             = 0,
    OUTPORT_HDMI_ARC            = 1,
    OUTPORT_HDMI                = 2,
    OUTPORT_SPDIF               = 3,
    OUTPORT_AUX_LINE            = 4,
    OUTPORT_HEADPHONE           = 5,
    OUTPORT_REMOTE_SUBMIX       = 6,
    OUTPORT_A2DP                = 7,
    OUTPORT_BT_SCO              = 8,
    OUTPORT_BT_SCO_HEADSET      = 9,
    OUTPORT_USB_HEADSET         = 10,
    OUTPORT_FM                  = 11,
    OUTPORT_ANLG_DOCK_HEADSET   = 12,
    OUTPORT_BUS                 = 13,
    /*if the audio_hal_primary unsupport the output devices, we need to route to OUTPUT_NULL*/
    OUTPORT_NULL                = 14,
    OUTPORT_MAX                 = 15,
};

enum IN_PORT {
    INPORT_TUNER                = 0,
    INPORT_HDMIIN               = 1,
    INPORT_SPDIF                = 2,
    INPORT_LINEIN               = 3,
    INPORT_REMOTE_SUBMIXIN      = 4,
    INPORT_WIRED_HEADSETIN      = 5,
    INPORT_BUILTIN_MIC          = 6,
    INPORT_BT_SCO_HEADSET_MIC   = 7,
    INPORT_ECHO_REFERENCE       = 8,
    INPORT_ARCIN                = 9,
    INPORT_USB                  = 10,
    INPORT_LOOPBACK             = 11,
    INPORT_BUS                  = 12,
    /*if the audio_hal_primary unsupport the input devices, we need to route to INPORT_NULL*/
    INPORT_NULL                 = 13,
    INPORT_MAX                  = 14
};

/* sync with tinymix before TXL */
enum input_source {
    SRC_NA = -1,
    LINEIN  = 0,
    ATV     = 1,
    HDMIIN  = 2,
    SPDIFIN = 3,
    ARCIN   = 4,
};

/* sync with tinymix after auge */
enum auge_input_source {
    TDMIN_A = 0,
    TDMIN_B = 1,
    TDMIN_C = 2,
    SPDIFIN_AUGE = 3,
    PDMIN = 4,
    FRATV = 5,
    TDMIN_LB    = 6,
    LOOPBACK_A  = 7,
    FRHDMIRX    = 8,
    LOOPBACK_B  = 9,
    SPDIFIN_LB  = 10,
    EARCRX_DMAC = 11,
    RESERVED_0  = 12,
    RESERVED_1  = 13,
    RESERVED_2  = 14,
    VAD     = 15,
};

#endif
