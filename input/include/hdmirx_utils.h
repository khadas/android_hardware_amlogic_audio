/*
 * Copyright (C) 2021 Amlogic Corporation.
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



#ifndef  _HDMIRX_UTILS_H_
#define _HDMIRX_UTILS_H_

#include <sys/types.h>

struct audio_hw_device;
struct aml_audio_device;
struct hdmi_capability_manager;

/**
 *  Audio Format Description of CEC Short Audio Descriptor
 *  CEA-861-D: Table 37. RequestShortAudioDescriptorAction.java
 */
typedef enum {
    AML_HDMI_FORMAT_RESERVED1             = 0x0,    // SAD_CODEC_RESERVED1
    AML_HDMI_FORMAT_LPCM                  = 0x1,    // SAD_CODEC_LPCM
    AML_HDMI_FORMAT_AC3                   = 0x2,    // SAD_CODEC_AC3
    AML_HDMI_FORMAT_MPEG1                 = 0x3,    // SAD_CODEC_MPEG1
    AML_HDMI_FORMAT_MP3                   = 0x4,    // SAD_CODEC_MP3
    AML_HDMI_FORMAT_MPEG2MC               = 0x5,    // SAD_CODEC_MPEG2MC
    AML_HDMI_FORMAT_AAC                   = 0x6,    // SAD_CODEC_AAC
    AML_HDMI_FORMAT_DTS                   = 0x7,    // SAD_CODEC_DTS
    AML_HDMI_FORMAT_ATRAC                 = 0x8,    // SAD_CODEC_ATRAC
    AML_HDMI_FORMAT_OBA                   = 0x9,    // SAD_CODEC_OBA
    AML_HDMI_FORMAT_DDP                   = 0xA,    // SAD_CODEC_DDP
    AML_HDMI_FORMAT_DTSHD                 = 0xB,    // SAD_CODEC_DTSHD
    AML_HDMI_FORMAT_MAT                   = 0xC,    // SAD_CODEC_MAT
    AML_HDMI_FORMAT_DST                   = 0xD,    // SAD_CODEC_DST
    AML_HDMI_FORMAT_WMAPRO                = 0xE,    // SAD_CODEC_WMAPRO
    AML_HDMI_FORMAT_RESERVED2             = 0xF,    // SAD_CODEC_RESERVED2
} AML_HDMI_FORMAT_E;

typedef enum {
    AML_HDMI_ARC_RATE_MASK_32000          = 0x1,
    AML_HDMI_ARC_RATE_MASK_44100          = 0x2,
    AML_HDMI_ARC_RATE_MASK_48000          = 0x4,
    AML_HDMI_ARC_RATE_MASK_88200          = 0x8,
    AML_HDMI_ARC_RATE_MASK_96000          = 0x10,
    AML_HDMI_ARC_RATE_MASK_176400         = 0x20,
    AML_HDMI_ARC_RATE_MASK_192000         = 0x40,
} AML_HDMI_ARC_RATE_MASK_E;

struct format_desc {
    AML_HDMI_FORMAT_E fmt;
    bool is_support;
    unsigned int max_channels;
    /*
     * bit:    6     5     4    3    2    1    0
     * rate: 192  176.4   96  88.2  48  44.1   32
     */
    unsigned int sample_rate_mask;
    unsigned int max_bit_rate;
    /* only used by dd+ format */
    bool   atmos_supported;
    /* parse Dolby Vendor-Specific Audio Data Block.
     * Dolby Audio and Dolby Atmos over HDMI Specification doc.
     * The audio_hw_profile.c fils also has the detail description.
     * Sink device supports Dolby MAT PCM decoding at 48 kHz only,
     * and does not support Dolby TrueHD decoding.
     */
    bool   MAT_PCM_48kHz_only;
    /* The Codec Vendor specific (DTS Audio) Subtype from SAD (Byte 3).
     * | EDID SAD Byte1 |   EDID SAD Byte3    |      Description         |
     * |----------------|---------------------|--------------------------|
     * |  Bit3 ~ Bit6   | Bit2 | Bit1 | Bit0  |                          |
     * |----------------|---------------------|--------------------------|
     * |     7 = DTS    |Byte3 = 0xC0 = Maximum bitrate divided by8kHz.  |
     * |----------------|------------------------------------------------|
     * |   11 = DTS-HD  |   X  |   X  |   0   |DTS-HD MA is NOT supported|
     * |   11 = DTS-HD  |   X  |   X  |   1   |DTS-HD MA is supported.   |
     * |   11 = DTS-HD  |   X  |   1  |   X   |DTS:X (P1) is supported.  |
     * |   11 = DTS-HD  |   1  |   X  |   X   |DTS:X (P2) is supported.  |
     * |-----------------------------------------------------------------|
     * DTS-HD extension substreams (with or without core) including XLL (lossless) bitstreams as know as DTS-HD MA.
     * DTS:X (P1) supports all DTS formats except DTS:X (P2).
     * DTS:X (P2) supports all DTS formats.
     */
    unsigned int dts_vsdb_byte3;
};

/*
 *A Short Audio Descriptor is used by HDMI sink devices and HDMI ARC/eARC receiver devices to indicate
 *support for an audio format (for example, Dolby Digital Plus) to a connected HDMI source device or HDMI
 *ARC/eARC transmitter device.
 */
#define EDID_ARRAY_MAX_LEN 38 /* 3 bytes for each audio format, max 30 bytes for audio edid, 8 bytes for TLV header */
struct aml_arc_hdmi_desc {
    int EDID_length;
    unsigned int avr_port;
    char target_EDID_array[EDID_ARRAY_MAX_LEN];
    bool default_edid;
    struct format_desc pcm_fmt;
    struct format_desc dts_fmt;
    struct format_desc dtshd_fmt;
    struct format_desc dd_fmt;
    struct format_desc ddp_fmt;
    struct format_desc mat_fmt;
    struct format_desc mpegh_fmt;
};

struct aml_arc_hdmi_desc *get_arc_hdmi_cap(struct aml_audio_device *adev);

void clear_arc_cached_edid(struct aml_audio_device *adev);

void set_arc_hdmi_updated(struct aml_audio_device *adev, bool enable);

bool is_arc_hdmi_updated(struct aml_audio_device *adev);

/*@ brief "set_ARC_format" for HDMIRX
 * return zero if success;
 */
int set_arc_hdmi(struct audio_hw_device *dev, char *value, size_t len);

/*@ brief "set_ARC_format" for HDMIRX
 * return zero if success;
 */
int set_arc_format(struct audio_hw_device *dev, char *value, size_t len);

/*@ brief after edited the audio sad, then update edid
 * return zero if success;
 */
int update_edid_after_edited_audio_sad(struct audio_hw_device *dev, struct format_desc *fmt_desc);

#if ANDROID_PLATFORM_SDK_VERSION > 32
/*@ brief "read_hdmi_arc_info" for v7
 */
void read_hdmi_arc_info(struct audio_hw_device *dev,
        const struct audio_extra_audio_descriptor *audio_descriptors, uint32_t size, bool connected);

void update_earc_sad(struct audio_hw_device *dev);
#endif


//new & get & release instance
struct hdmi_capability_manager *get_hdmi_capability_manager(struct aml_audio_device *adev);

int init_hdmi_capability_manager(struct aml_audio_device *adev);

void destroy_hdmi_capability_manager(struct aml_audio_device *adev);
#endif /* _HDMIRX_UTILS_H_ */

