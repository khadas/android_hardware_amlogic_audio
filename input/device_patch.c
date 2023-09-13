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

#include <system/audio.h>

#include "device_patch.h"
#include "audio_hw_utils.h"

void create_tvin_buffer(struct aml_audio_patch *patch)
{
    int ret;

    if (patch->is_dtv_src) {
        /* dtv case: buffer len = 32 * 4 ms */
        ret = ring_buffer_init(&patch->tvin_ringbuffer, (32 * 4) * (48 * 4));
    } else {
        ret = ring_buffer_init(&patch->tvin_ringbuffer, 4 * 48 * 64);
    }
    ALOGI("[%s] aring_buffer_init ret=%d\n", __FUNCTION__, ret);
    if (ret == 0) {
        patch->tvin_buffer_inited = 1;
    }

}

void release_tvin_buffer(struct aml_audio_patch *patch)
{
    if (patch->tvin_buffer_inited == 1) {
        patch->tvin_buffer_inited = 0;
        ring_buffer_release(&(patch->tvin_ringbuffer));
    }
}


/************************ dump patch info ***************************/
//1.1 dump one registered audio patch information
void aml_audio_port_config_dump(struct audio_port_config *port_config, int fd)
{
    if (port_config == NULL)
        return;

    dprintf(fd, "\t-id(%d), role(%s), type(%s)\n", port_config->id, audioPortRole2Str(port_config->role), audioPortType2Str(port_config->type));
    switch (port_config->type) {
    case AUDIO_PORT_TYPE_DEVICE:
        dprintf(fd, "\t-port device: type(%#x) addr(%s)\n",
               port_config->ext.device.type, port_config->ext.device.address);
        break;
    case AUDIO_PORT_TYPE_MIX:
        dprintf(fd, "\t-port mix: io handle(%d)\n", port_config->ext.mix.handle);
        break;
    default:
        break;
    }
}

void audio_patch_dump(struct audio_patch *patch, int fd)
{
    int i = 0;

    dprintf(fd, " handle %d\n", patch->id);
    for (i = 0; i < patch->num_sources; i++) {
        dprintf(fd, "    [src  %d]\n", i);
        aml_audio_port_config_dump(&patch->sources[i], fd);
    }

    for (i = 0; i < patch->num_sinks; i++) {
        dprintf(fd, "    [sink %d]\n", i);
        aml_audio_port_config_dump(&patch->sinks[i], fd);
    }
}

//1.2 dump all registered android audio patch
void audio_patch_list_dump(struct aml_audio_device* aml_dev, int fd)
{
    struct audio_patch_set *patch_set = NULL;
    struct audio_patch *patch = NULL;
    struct listnode *node = NULL;
    int i = 0;

    dprintf(fd, "\nAML Audio Patches:\n");
    list_for_each(node, &aml_dev->patch_list) {
        dprintf(fd, "  patch %d:", i);
        patch_set = node_to_item (node, struct audio_patch_set, list);
        if (patch_set) {
            audio_patch_dump(&patch_set->audio_patch, fd);
        }
        i++;
    }
}

//2. dump amlogic audio patch (device to device) information
void aml_device_patch_dump(struct aml_audio_device* aml_dev, int fd)
{
    struct aml_audio_patch *pstPatch = get_dev_patch(aml_dev);
    if (NULL == pstPatch) {
        dprintf(fd, "-------------[AML_HAL] audio patch [not create]-----------\n");
        return;
    }
    dprintf(fd, "-------------[AML_HAL] audio patch [%p]---------------\n", pstPatch);
    if (pstPatch->aml_ringbuffer.size != 0) {
        uint32_t u32FreeBuffer = get_buffer_write_space(&pstPatch->aml_ringbuffer);
        dprintf(fd, "[AML_HAL]      RingBuf   size: %10d Byte|  UnusedBuf:%10d Byte(%d%%)\n",
        pstPatch->aml_ringbuffer.size, u32FreeBuffer, u32FreeBuffer* 100 / pstPatch->aml_ringbuffer.size);
    } else {
        dprintf(fd, "[AML_HAL]      patch  RingBuf    : buffer size is 0\n");
    }
    if (pstPatch->audio_parse_para) {
        int s32AudioType = ((audio_type_parse_t*)pstPatch->audio_parse_para)->audio_type;
        dprintf(fd, "[AML_HAL]      Hal audio Type: [0x%x]%-10s| Src Format:%#10x\n", s32AudioType,
            audio_type_convert_to_string(s32AudioType),  pstPatch->aformat);
    }

    dprintf(fd, "[AML_HAL]      IN_SRC        : %#10x     | OUT_SRC   :%#10x\n", pstPatch->input_src, pstPatch->output_src);
    dprintf(fd, "[AML_HAL]      IN_Format     : %#10x     | OUT_Format:%#10x\n", pstPatch->aformat, pstPatch->out_format);
    dprintf(fd, "[AML_HAL]      sink format: %#x\n", aml_dev->sink_format);
    if ((aml_dev->cur_out_devices & AUDIO_DEVICE_OUT_HDMI_ARC) != 0) {
        struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(aml_dev);
        bool dd_is_support = hdmi_desc->dd_fmt.is_support;
        bool ddp_is_support = hdmi_desc->ddp_fmt.is_support;
        bool mat_is_support = hdmi_desc->mat_fmt.is_support;

        dprintf(fd, "[AML_HAL]      -dd: %d, ddp: %d, mat: %d\n",
                dd_is_support, ddp_is_support, mat_is_support);
    }
}

/*
    Function:dump all audio patch's information
    Details:
        1) android audio patch list register in audio hal
        2) amlogic device patches created dynamicly by audio hal
*/
void adev_audio_patches_dump(struct aml_audio_device* aml_dev, int fd)
{
    audio_patch_list_dump(aml_dev, fd);
    aml_device_patch_dump(aml_dev, fd);
}
