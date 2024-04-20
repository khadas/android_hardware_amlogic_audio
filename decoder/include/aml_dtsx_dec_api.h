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

#ifndef _AML_DTSX_DEC_API_H_
#define _AML_DTSX_DEC_API_H_

#include <hardware/audio.h>
#include <cutils/str_parms.h>
#include "aml_ringbuffer.h"
#include "aml_audio_types_def.h"
#include "aml_dec_api.h"
#include "aml_audio_resample_manager.h"

#define DTSX_BITS(x) (1 << x)

#define DTSX_PARAM_STRING_LEN 64
#define DTSX_PARAM_COUNT_MAX 64

struct dtsx_syncword_info {
    unsigned int syncword;
    int syncword_pos;
    int check_pos;
};

struct dtsx_frame_info {
    unsigned int syncword;
    int syncword_pos;
    int check_pos;
    bool is_little_endian;
    int dts_type4_subtype;  // According to IEC61937-5, typeIV subtypes include DTS-HD and DTS-UHD.
    int iec61937_data_type;
    int size;
};

/** \brief Decoder output bus IDs */
typedef enum {
    DTSX_OUTPUT_SPK = 0,    /**< Multi-channel Bus 0 (for feeding the Virtual-X / Headphone-X processor) */
    DTSX_OUTPUT_RAW = 1,    /**< Multi-channel Bus 1 (for feeding transcoder) */
    DTSX_OUTPUT_HP  = 2,    /**< Stereo downmix Bus (downmix from Multi-channel Bus 1) */
    DTSX_OUTPUT_MAX = 3
} AML_DTSX_OUTPUT_TYPE;


///< Keep the members of dtsx_stream_info_t same as dtsxp1_bitstream_info_t in dtsx_aml_wrapper.h which is in dtx lib
typedef struct dtsx_stream_info_s
{
    unsigned int channel_num;         /**< Total number of channels of backwards compatible layout */
    unsigned int channel_mask;        /**< Channel mask of backwards compatible layout. If SpeakerActivitymask from Asset descriptor not available, m_nChannelMask will be zero*/
    unsigned int representation_type; /**< Representation type, #DTS_REPTYPE */
    unsigned int sample_rate;         /**< Maximum sampling rate of the bitstream */
    unsigned int stream_type;         /**< Indicates stream type through which we can distinguish legacy streams,DTS-X streams etc */
    unsigned int is_has_heights;      /* Indicates height channel(s) present or not */
    unsigned int is_drc_metadata_present; /* Value 1 Indicates presence of DRC metadata in stream */
    unsigned int is_dialognorm_metadata_present; /* Indicates Dialog Normalization parameters are present or not present in stream */
    unsigned int is_type1CC_flag_present; /* This 1-bit flag when TRUE it indicates that content is created according to Type1 certified process*/
    int reserved_padding[11];         /**< The reserved size of dtsxp1_bitstream_info_t is 80 bytes and needs to be filled to the corresponding size. */
} dtsx_stream_info_t;

///< Keep the members of dtsxParameterType_t same as dtsxParameterType_t in dtsx_aml_wrapper.h which is in dtx lib
typedef enum  {
    /* Get Type1 Certified Content processing mode value.
       0 - OFF mode. Type1-CC processing turned off even if Type1-CC flag is present in bitstream.
       1 - AUTO mode. The processing is controlled by the Type1-CC flag in the bitstream.
    */
    DTSX_TYPE1CC_MODE     = 0,
    /* Get the Type1-CC processing state.
       Type1-CC process state is get updated based on Type1-CC process mode (which is configured
       by user) and Type1-CC flag decoded from bit-stream.
    */
    DTSX_TYPE1CC_STATE    = 1,
    /* Get Type1-CC flag value from input bit-stream.
       0 - Type1-CC flag is not present in stream.
       1 - Type1-CC flag is present in stream.
    */
    DTSX_TYPE1CC_FLAG     = 2,
    /* Gets the status of parma(The latest upmixing and spatial remapping technology from DTS, a.k.a Neural:X).
       0 - Bypassed.
       1 - Running.
    */
    DTSX_PARMA_STATUS     = 3,    // Get the status (true: running; false: bypassed) of parma.

    /* Gets the bitstream info which stored in #dtsStreamInfo
    */
    DTSX_BITSTREAM_INFO   = 4,

    DTSX_PARAM_TYPE_MAX,
} dtsxParameterType_t;

typedef enum {
    DTSX_INITED = DTSX_BITS(0),   // dca decoder init or not.
    DTSX_PROCESS_HALF_FRAME = DTSX_BITS(1),
} DTSX_DECODER_STATUS;

typedef struct dtsx_dec_s {
    ///< Control
    aml_dec_t aml_dec;
    void *p_dtsx_dec_inst;
    void *p_dtsx_pp_inst;
    aml_audio_resample_t *resample_handle;

    ///< Information
    int status;
    int remain_size;
    int half_frame_remain_size;
    int half_frame_used_size;
    unsigned int outlen_pcm;
    unsigned int outlen_raw;
    int stream_type;    ///< enum audio_hal_format
    bool is_headphone_x;    // DTS Headphone:X stream.
    bool is_t1cc;   // T1-CC stands for a special category of content type referred to as Type-1 Certified Content.
    struct pcm_info core1_pcm_out_info;
    struct pcm_info core2_pcm_out_info;
    struct dtsx_frame_info frame_info;   ///< for frame parsing

    ///< Parameter
    char *init_argv[DTSX_PARAM_COUNT_MAX];
    int init_argc;
    bool is_dtscd;
    bool is_iec61937;
    int sink_dev_type;
    int passthroug_enable;
    int auto_config_out_for_vx;
    int loudness_enable;
    int output_bitwidth;
    unsigned char *inbuf;
    unsigned int inbuf_size;
    unsigned char *a_dtsx_pp_output[3];
    unsigned int a_dtsx_pp_output_size[3];
    aml_dec_control_type_t digital_raw;
    int is_hdmi_output;
    ring_buffer_t input_ring_buf;
    ring_buffer_t spdif_ring_buffer;
    unsigned char *sample_convert_buf;
    int sample_convert_buf_size;
    int device_type;
} dtsx_dec_t;

int dtsx_decoder_init_patch(aml_dec_t **ppaml_dec, aml_dec_config_t * dec_config);
int dtsx_decoder_release_patch(aml_dec_t *aml_dec);
int dtsx_decoder_process_patch(aml_dec_t *aml_dec, unsigned char*buffer, int bytes);

/**
* @brief Get dtsx decoder output channel(internal use).
* @param None
* @return [success]: 0 decoder not init.
*         [success]: 1 ~ 8, output channel number
*            [fail]: -1 get output channel fail.
*/
int dtsx_get_out_ch_internal(dtsx_dec_t *dtsx_dec);
/**
* @brief Set dtsx decoder output channel(internal use).
* @param ch_num: The num of channels you want the decoder to output
*              0: Default setting, decoder configs output channel automatically.
*          1 ~ 8: The decoder outputs the specified number of channels
*                (At present, @ch_num only supports 2-ch and 6-ch).
* @return [success]: 0
*            [fail]: -1 set output channel fail.
*/
int dtsx_set_out_ch_internal(dtsx_dec_t *dtsx_dec, int ch_num);
int aml_dtsx_update_runtime_params(dtsx_dec_t *dtsx_dec, struct str_parms *parms);

extern aml_dec_func_t aml_dtsx_func;

#endif
