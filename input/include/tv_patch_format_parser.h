/*
 * hardware/amlogic/audio/TvAudio/audio_format_parse.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 */

#ifndef __TV_PATCH_FORMAT_PARSER_H__
#define __TV_PATCH_FORMAT_PARSER_H__

#include <system/audio.h>
#include <tinyalsa/asoundlib.h>
#include <aml_alsa_mixer.h>
#include "aml_ac3_parser.h"

enum audio_type {
    NOT_READY = -1,
    LPCM = 0,
    AC3,
    EAC3,
    DTS,
    DTSHD,
    MAT,
    PAUSE,
    TRUEHD,
    DTSCD,
    MUTE,
    MPEGH,
};

/*
 * eARC type:
 *  0: "UNDEFINED"
 *  1: "STEREO LPCM"
 *  2: "MULTICH 2CH LPCM"
 *  3: "MULTICH 8CH LPCM"
 *  4: "MULTICH 16CH LPCM"
 *  5: "MULTICH 32CH LPCM"
 *  6: "High Bit Rate LPCM"
 *  7: "AC-3 (Dolby Digital)", Layout A
 *  8: "AC-3 (Dolby Digital Layout B)"
 *  9: "E-AC-3/DD+ (Dolby Digital Plus)"
 * 10: "MLP (Dolby TrueHD)"
 * 11: "DTS"
 * 12: "DTS-HD"
 * 13: "DTS-HD MA"
 * 14: "DSD (One Bit Audio 6CH)"
 * 15: "DSD (One Bit Audio 12CH)"
 * 16: "PAUSE"
 * 17: "E-AC-3/DD+ (Dolby Digital Plus Layout B)"
 * 18: "MLP (Dolby TrueHD Layout B)"
 * 19: "DTS Layout B"
 * 20: "DTS-HD Layout B"
 * 21: "DTS-HD MA Layout B"
 */
enum earc_audio_type {
    EARC_UNDEFINED = 0,
    EARC_STEREO_LPCM,
    EARC_MULTICH_2CH_LPCM,
    EARC_MULTICH_8CH_LPCM,
    EARC_MULTICH_16CH_LPCM,
    EARC_MULTICH_32CH_LPCM,
    EARC_HBR_LPCM,
    EARC_AC3,
    EARC_AC3_LAYOUT_B,
    EARC_EAC3,
    EARC_MLP,
    EARC_DTS,
    EARC_DTS_HD,
    EARC_DTS_HD_MA,
    EARC_SACD_6CH,
    EARC_SACD_12CH,
    EARC_PAUSE,
    EARC_EAC3_LAYOUT_B,
    EARC_MLP_LAYOUT_B,
    EARC_DTS_LAYOUT_B,
    EARC_DTS_HD_LAYOUT_B,
    EARC_DTS_HD_MA_LAYOUT_B,
};

enum audio_sample {
    HW_NONE = 0,
    HW_32K,
    HW_44K,
    HW_48K,
    HW_88K,
    HW_96K,
    HW_176K,
    HW_192K,
};

typedef enum hdmiin_audio_packet {
    AUDIO_PACKET_NONE,
    AUDIO_PACKET_AUDS,
    AUDIO_PACKET_OBA,
    AUDIO_PACKET_DST,
    AUDIO_PACKET_HBR,
    AUDIO_PACKET_OBM,
    AUDIO_PACKET_MAS
} hdmiin_audio_packet_t;

#define PARSER_DEFAULT_PERIOD_SIZE  (1024)

/*Period of data burst in IEC60958 frames*/
//#define AC3_PERIOD_SIZE  (6144)
//#define EAC3_PERIOD_SIZE (24576)
//#define MAT_PERIOD_SIZE  (61440)

#define DTS1_PERIOD_SIZE (2048)
#define DTS2_PERIOD_SIZE (4096)
#define DTS3_PERIOD_SIZE (8192)
/*min DTSHD Period 2048; max DTSHD Period 65536*/
#define DTSHD_PERIOD_SIZE   (512*8)
#define DTSHD_PERIOD_SIZE_1 (512*32)
#define DTSHD_PERIOD_SIZE_2 (512*48)
#define DTSCD_VALID_COUNT   (2)
#define DTSHD_TYPE_IV_PERIOD_SIZE   (512*4)

typedef struct audio_type_parse {
    struct pcm_config config_in;
    struct pcm *in;
    struct aml_mixer_handle *mixer_handle;
    unsigned int card;
    unsigned int device;
    unsigned int flags;
    int soft_parser;
    hdmiin_audio_packet_t hdmi_packet;
    hdmiin_audio_packet_t last_reconfig_hdmi_packet;

    int period_bytes;
    char *parse_buffer;

    int audio_type;
    int cur_audio_type;
    bool reset_input;

    audio_channel_mask_t audio_ch_mask;

    int read_bytes;
    int package_size;
    int audio_samplerate;
    int pre_audio_samplerate;

    int running_flag;
    audio_devices_t input_dev;
    bool fmt_change;
    bool pcpd_monitor_flag;
} audio_type_parse_t;

int create_pthread_for_audio_type_parse(
    pthread_t *audio_type_parse_ThreadID,
                     void **status,
                     struct aml_mixer_handle *mixer,
                     audio_devices_t input_dev);
void exit_pthread_for_audio_type_parse(
    pthread_t audio_type_parse_ThreadID,
    void **status);

/*
 *@brief convert the audio type to android audio format
 */
audio_format_t audio_type_convert_to_android_audio_format_t(int codec_type);

/*
 *@brief convert the audio type to string format
 */
char* audio_type_convert_to_string(int s32AudioType);

/*
 *@brief convert android audio format to the audio type
 */
int android_audio_format_t_convert_to_audio_type(audio_format_t format);
/*
 *@brief get current android audio format from audio parser thread
 */
audio_format_t audio_parse_get_audio_type(audio_type_parse_t *status);
/*
 *@brief get current audio channel mask from audio parser thread
 */
audio_channel_mask_t audio_parse_get_audio_channel_mask(audio_type_parse_t *status);
/*
 *@brief get current audio packet type from audio parser thread
 */
hdmiin_audio_packet_t audio_parse_get_audio_packet_type(audio_type_parse_t *status);
/*
 *@brief get current audio format from audio parser thread
 */
int audio_parse_get_audio_type_direct(audio_type_parse_t *status);
/*
 *@brief parse the channels in the undecoded DTS stream
 */
int get_dts_stream_channels(const char *buffer, size_t bytes);
/*
 *@brief get current audio type from buffer data
 */
int audio_type_parse(void *buffer, size_t bytes, int *package_size,
        audio_channel_mask_t *cur_ch_mask);
int audio_raw_data_parse(audio_type_parse_t *status, void *buffer, size_t bytes);

int audio_parse_get_audio_samplerate(audio_type_parse_t *status);

int eArcIn_coding_type_detection(struct aml_mixer_handle *mixer_handle);

bool eArcIn_get_cs_mute(struct aml_mixer_handle *mixer_handle);

void audio_pcpd_format_detect(audio_type_parse_t *status);

int pcm_coding_type_to_channels(enum earc_audio_type type);
int non_pcm_coding_type_to_codec(enum earc_audio_type type);

#endif
