/*
 * Copyright (C) 2022 Amlogic Corporation.
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
#define LOG_TAG "audio_heaac_parser"
//#define LOG_NDEBUG 0

#include <stdlib.h>
#include <string.h>
#include <cutils/log.h>
#include "aml_audio_heaacparser.h"
#include "aml_audio_bitsparser.h"
#include "aml_malloc_debug.h"
#ifdef ANDROID_PLATFORM_SDK_VERSION
#include <system/audio-base.h>
#else
#define AUDIO_CHANNEL_OUT_MONO          0x1u
#define AUDIO_CHANNEL_OUT_STEREO        0x3u
#define AUDIO_CHANNEL_OUT_2POINT1       0xBu
#define AUDIO_CHANNEL_OUT_SURROUND      0x107u
#define AUDIO_CHANNEL_OUT_PENTA         0x37u
#define AUDIO_CHANNEL_OUT_5POINT1       0x3Fu
#define AUDIO_CHANNEL_OUT_7POINT1       0x63Fu
#endif
/**
 *
 * This value can be used for the initialization of the AC-4 framer.
 * The value 16 kB is determined by the maximum frame size used in broadcast applications.
 * now we use 16kb * 2 to make sure it can always contain the frame
 */
#define HEAAC_MAX_FRAMESIZE     (7 * 6144 * 3) //DR_2016_341_aac_ATSC_m2v_29fps.trp max frame size is 126464

/* ISO/IEC 14496-3:2005(E)*/
#if 0
/* page51, Syntax of AudioSyncStream() */
Syntax                                                 No. of bitsMnemonic
AudioSyncStream()
{
    while (nexbits() == 0x2B7) {         /* syncword */ 11     bslbf
        audioMuxLengthBytes;                           13     uimsbf
        AudioMuxElement(1);
    }
}
#endif
#define HEAAC_LOAS_HEADER_SIZE       (4)//bytes

#if 0
/* page92, Table 1.A.6 ---- Syntax of adts_fixed_header() */
Syntax                                                 No. of bitsMnemonic
adts_fixed_header()
{
    syncword;                                          12     bslbf
    ID;                                                1      bslbf
    layer;                                             2      uimsbf
    protection_absent;                                 1      bslbf
    profile_ObjectType;                                2      uimsbf
    sampling_frequency_index;                          4      uimsbf
    private_bit;                                       1      bslbf
    channel_configuration;                             3      uimsbf
    original_copy;                                     1      bslbf
    home;                                              1      bslbf

}

Table 1.A.7 ---- Syntax of adts_variable_header()
Syntax                                                 No. of bitsMnemonic
adts_fixed_header()
{
    copyright_identification_bit;                      1      bslbf
    copyright_identification_start                     1      bslbf
    aac_frame_length;                                  13     uimsbf
    adts_buffer_fullness;                              11     bslbf
    number_of_raw_data_blocks_in_frame;                2      uimsbf
}

#endif
//ADTS: Audio Data Transport Stream
#define HEAAC_ADTS_HEADER_SIZE       (8)//bytes
#define HEAAC_HEADER_SIZE            (8)//bytes

#define HEAAC_LOAS_SYNCWORD           (0x2B7)//11bits
#define HEAAC_ADTS_SYNCWORD           (0xFFF)//12bits


enum PARSER_STATE {
    PARSER_SYNCING,
    PARSER_SYNCED,
    PARSER_LACK_DATA,
};

struct aml_heaac_parser {
    void * buf;
    int32_t buf_size;
    int32_t buf_remain;
    uint32_t status;
    struct audio_bit_parser bit_parser;
};

int aml_heaac_parser_open(void **pparser_handle)
{
    struct aml_heaac_parser *parser_handle = NULL;

    parser_handle = (struct aml_heaac_parser *)aml_audio_calloc(1, sizeof(struct aml_heaac_parser));
    if (parser_handle == NULL) {
        ALOGE("%s handle error", __func__);
        goto error;
    }

    parser_handle->buf_size   = HEAAC_MAX_FRAMESIZE;
    parser_handle->buf        = aml_audio_calloc(1, HEAAC_MAX_FRAMESIZE);
    if (parser_handle->buf == NULL) {
        ALOGE("%s data buffer error", __func__);
        aml_audio_free(parser_handle);
        parser_handle = NULL;
        goto error;
    }
    parser_handle->status     = PARSER_SYNCING;
    parser_handle->buf_remain = 0;
    *pparser_handle = parser_handle;
    ALOGI("%s exit =%p", __func__, parser_handle);
    return 0;
error:
    *pparser_handle = NULL;
    ALOGE("%s error", __func__);
    return -1;
}
int aml_heaac_parser_close(void *parser_handle)
{
    struct aml_heaac_parser *heaac_parser_handle = (struct aml_heaac_parser *)parser_handle;

    if (heaac_parser_handle) {
        if (heaac_parser_handle->buf) {
            aml_audio_free(heaac_parser_handle->buf);
            heaac_parser_handle->buf = NULL;
        }
        aml_audio_free(heaac_parser_handle);
        heaac_parser_handle = NULL;
    }
    ALOGI("%s exit", __func__);
    return 0;
}

int aml_heaac_parser_reset(void *parser_handle)
{
    struct aml_heaac_parser *heaac_parser_handle = (struct aml_heaac_parser *)parser_handle;

    if (heaac_parser_handle) {
        heaac_parser_handle->status = PARSER_SYNCING;
        heaac_parser_handle->buf_remain = 0;
    }
    ALOGI("%s exit", __func__);
    return 0;
}

#define MICROS_PER_SECOND 1000000LL //defined in clock.h

// Audio sample count constants assume the frameLengthFlag in the access unit is 0.
/**
* Number of raw audio samples that are produced per channel when decoding an AAC LC access unit.
*/
#define AAC_LC_AUDIO_SAMPLE_COUNT  1024
/**
* Number of raw audio samples that are produced per channel when decoding an AAC XHE access unit.
*/
#define AAC_XHE_AUDIO_SAMPLE_COUNT (AAC_LC_AUDIO_SAMPLE_COUNT)
/**
* Number of raw audio samples that are produced per channel when decoding an AAC HE access unit.
*/
#define AAC_HE_AUDIO_SAMPLE_COUNT 2048
/**
* Number of raw audio samples that are produced per channel when decoding an AAC LD access unit.
*/
#define AAC_LD_AUDIO_SAMPLE_COUNT 512

// Maximum bitrates for AAC profiles from the Fraunhofer FDK AAC encoder documentation:
// https://cs.android.com/android/platform/superproject/+/android-9.0.0_r8:external/aac/libAACenc/include/aacenc_lib.h;l=718
/** Maximum rate for an AAC LC audio stream, in bytes per second. */
#define AAC_LC_MAX_RATE_BYTES_PER_SECOND (800 * 1000 / 8)
/** Maximum rate for an AAC HE V1 audio stream, in bytes per second. */
#define AAC_HE_V1_MAX_RATE_BYTES_PER_SECOND (128 * 1000 / 8)
/** Maximum rate for an AAC HE V2 audio stream, in bytes per second. */
#define AAC_HE_V2_MAX_RATE_BYTES_PER_SECOND (56 * 1000 / 8)
/**
* Maximum rate for an AAC XHE audio stream, in bytes per second.
*
* <p>Fraunhofer documentation says "500 kbit/s and above" for stereo, so we use a rate generously
* above the 500 kbit/s level.
*/
#define AAC_XHE_MAX_RATE_BYTES_PER_SECOND (2048 * 1000 / 8)
/**
* Maximum rate for an AAC ELD audio stream, in bytes per second.
*
* <p>Fraunhofer documentation shows AAC-ELD as useful for up to ~ 64 kbit/s so we use this value.
*/
#define AAC_ELD_MAX_RATE_BYTES_PER_SECOND (64 * 1000 / 8)

#define AUDIO_SPECIFIC_CONFIG_FREQUENCY_INDEX_ARBITRARY 0xF
static int AUDIO_SPECIFIC_CONFIG_SAMPLING_RATE_TABLE[] ={
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350
  };
#define AUDIO_SPECIFIC_CONFIG_CHANNEL_CONFIGURATION_INVALID (-1)
/**
* In the channel configurations below, &lt;A&gt; indicates a single channel element; (A, B)
* indicates a channel pair element; and [A] indicates a low-frequency effects element. The
* speaker mapping short forms used are:
*
* <ul>
*   <li>FC: front center
*   <li>BC: back center
*   <li>FL/FR: front left/right
*   <li>FCL/FCR: front center left/right
*   <li>FTL/FTR: front top left/right
*   <li>SL/SR: back surround left/right
*   <li>BL/BR: back left/right
*   <li>LFE: low frequency effects
* </ul>
*/
static int AUDIO_SPECIFIC_CONFIG_CHANNEL_COUNT_TABLE[] = {
    0,
    1, /* mono: <FC> */
    2, /* stereo: (FL, FR) */
    3, /* 3.0: <FC>, (FL, FR) */
    4, /* 4.0: <FC>, (FL, FR), <BC> */
    5, /* 5.0 back: <FC>, (FL, FR), (SL, SR) */
    6, /* 5.1 back: <FC>, (FL, FR), (SL, SR), <BC>, [LFE] */
    8, /* 7.1 wide back: <FC>, (FCL, FCR), (FL, FR), (SL, SR), [LFE] */
    AUDIO_SPECIFIC_CONFIG_CHANNEL_CONFIGURATION_INVALID,
    AUDIO_SPECIFIC_CONFIG_CHANNEL_CONFIGURATION_INVALID,
    AUDIO_SPECIFIC_CONFIG_CHANNEL_CONFIGURATION_INVALID,
    7, /* 6.1: <FC>, (FL, FR), (SL, SR), <RC>, [LFE] */
    8, /* 7.1: <FC>, (FL, FR), (SL, SR), (BL, BR), [LFE] */
    AUDIO_SPECIFIC_CONFIG_CHANNEL_CONFIGURATION_INVALID,
    8, /* 7.1 top: <FC>, (FL, FR), (SL, SR), [LFE], (FTL, FTR) */
    AUDIO_SPECIFIC_CONFIG_CHANNEL_CONFIGURATION_INVALID
  };


// Advanced Audio Coding Low-Complexity profile.
#define AUDIO_OBJECT_TYPE_AAC_LC 2
// Spectral Band Replication.
#define AUDIO_OBJECT_TYPE_AAC_SBR 5
// Error Resilient Bit-Sliced Arithmetic Coding.
#define AUDIO_OBJECT_TYPE_AAC_ER_BSAC 22
// Enhanced low delay.
#define AUDIO_OBJECT_TYPE_AAC_ELD 23
// Parametric Stereo.
#define AUDIO_OBJECT_TYPE_AAC_PS 29
// Escape code for extended audio object types.
#define AUDIO_OBJECT_TYPE_ESCAPE 31
// Extended high efficiency.
#define AUDIO_OBJECT_TYPE_AAC_XHE 42

#if 0
  /**
   * Valid AAC Audio object types. One of {@link #AUDIO_OBJECT_TYPE_AAC_LC}, {@link
   * #AUDIO_OBJECT_TYPE_AAC_SBR}, {@link #AUDIO_OBJECT_TYPE_AAC_ER_BSAC}, {@link
   * #AUDIO_OBJECT_TYPE_AAC_ELD}, {@link #AUDIO_OBJECT_TYPE_AAC_PS} or {@link
   * #AUDIO_OBJECT_TYPE_AAC_XHE}.
   */
enum AacAudioObjectType {
    AUDIO_OBJECT_TYPE_AAC_LC=2,
    AUDIO_OBJECT_TYPE_AAC_SBR=5,
    AUDIO_OBJECT_TYPE_AAC_ER_BSAC=22,
    AUDIO_OBJECT_TYPE_AAC_ELD=23,
    AUDIO_OBJECT_TYPE_AAC_PS=29,
    AUDIO_OBJECT_TYPE_AAC_XHE=42
};
#endif

//static char *buildAacLcAudioSpecificConfig(int sampleRate, int channelCount);
//static char *buildAudioSpecificConfig(int audioObjectType, int sampleRateIndex, int channelConfig);
static int getEncodingForAudioObjectType(int audioObjectType);
static int getAudioObjectType(struct audio_bit_parser *bit_parser);
static int getSamplingFrequency(struct audio_bit_parser *bit_parser);
static void parseGaSpecificConfig(
    struct audio_bit_parser *bit_parser, int audioObjectType, int channelConfiguration);

static int parseLatmAudioSpecificConfig(struct audio_bit_parser *bit_parser, struct heaac_parser_info *heaac_info);
static void parseFrameLength(struct audio_bit_parser *bit_parser, struct heaac_parser_info *heaac_info);
static long latmGetValue(struct audio_bit_parser *bit_parser);
static int parseStreamMuxConfig(struct audio_bit_parser *bit_parser, struct heaac_parser_info *heaac_info);
static void parsePayloadMux(struct audio_bit_parser *bit_parser, int muxLengthBytes);
static int parsePayloadLengthInfo(struct audio_bit_parser *bit_parser, struct heaac_parser_info *heaac_info);


/**
   * Parses an AAC AudioSpecificConfig, as defined in ISO 14496-3 1.6.2.1
   *
   * @param bitArray A {@link ParsableBitArray} containing the AudioSpecificConfig to parse. The
   *     position is advanced to the end of the AudioSpecificConfig.
   * @param forceReadToEnd Whether the entire AudioSpecificConfig should be read. Required for
   *     knowing the length of the configuration payload.
   * @return The parsed configuration.
   * @throws ParserException If the AudioSpecificConfig cannot be parsed as it's not supported.
   */
static int parseAudioSpecificConfig_ReadToEnd(struct audio_bit_parser *bit_parser, struct heaac_parser_info *heaac_info, bool forceReadToEnd)
{
    ALOGV("%s line %d", __func__, __LINE__);
    int audioObjectType = getAudioObjectType(bit_parser);
    int sampleRateHz = getSamplingFrequency(bit_parser);
    int channelConfiguration = aml_audio_bitparser_getBits(bit_parser, 4);
    heaac_info->audioObjectType = audioObjectType;
    if (audioObjectType == AUDIO_OBJECT_TYPE_AAC_SBR
        || audioObjectType == AUDIO_OBJECT_TYPE_AAC_PS) {
        // For an AAC bitstream using spectral band replication (SBR) or parametric stereo (PS) with
        // explicit signaling, we return the extension sampling frequency as the sample rate of the
        // content; this is identical to the sample rate of the decoded output but may differ from
        // the sample rate set above.
        // Use the extensionSamplingFrequencyIndex.
        sampleRateHz = getSamplingFrequency(bit_parser);
        audioObjectType = getAudioObjectType(bit_parser);
        if (audioObjectType == AUDIO_OBJECT_TYPE_AAC_ER_BSAC) {
            // Use the extensionChannelConfiguration.
            channelConfiguration = aml_audio_bitparser_getBits(bit_parser, 4);
        }
    }

    if (forceReadToEnd) {
        switch (audioObjectType) {
            case 1:
            case 2:
            case 3:
            case 4:
            case 6:
            case 7:
            case 17:
            case 19:
            case 20:
            case 21:
            case 22:
            case 23:
                parseGaSpecificConfig(bit_parser, audioObjectType, channelConfiguration);
                break;
            default:
                ALOGE("Unsupported audio object type: %d", audioObjectType);
                return -1;
        }
        switch (audioObjectType) {
            case 17:
            case 19:
            case 20:
            case 21:
            case 22:
            case 23:
            {
                int epConfig = aml_audio_bitparser_getBits(bit_parser, 2);
                if (epConfig == 2 || epConfig == 3) {
                    ALOGE("Unsupported epConfig: %d", epConfig);
                    return -1;
                }
                break;
            }
            default:
                break;
        }
    }
    // For supported containers, bits_to_decode() is always 0.
    int channelCount = AUDIO_SPECIFIC_CONFIG_CHANNEL_COUNT_TABLE[channelConfiguration];
    if (channelCount == AUDIO_SPECIFIC_CONFIG_CHANNEL_CONFIGURATION_INVALID) {
        ALOGE("Unsupported channelCount: %d", channelCount);
        return -1;
    }
    heaac_info->audioObjectType = audioObjectType;
    heaac_info->sampleRateHz = sampleRateHz;
    heaac_info->channelCount = channelCount;
    return 0;
}

#if 0// tbd
  /**
   * Builds a simple AAC LC AudioSpecificConfig, as defined in ISO 14496-3 1.6.2.1
   *
   * @param sampleRate The sample rate in Hz.
   * @param channelCount The channel count.
   * @return The AudioSpecificConfig.
   */
static char *buildAacLcAudioSpecificConfig(int sampleRate, int channelCount) {
    int sampleRateIndex = 0;
    for (int i = 0; i < sizeof(AUDIO_SPECIFIC_CONFIG_SAMPLING_RATE_TABLE); ++i) {
        if (sampleRate == AUDIO_SPECIFIC_CONFIG_SAMPLING_RATE_TABLE[i]) {
            sampleRateIndex = i;
        }
    }
    int channelConfig = 0;
    for (int i = 0; i < sizeof(AUDIO_SPECIFIC_CONFIG_CHANNEL_COUNT_TABLE); ++i) {
        if (channelCount == AUDIO_SPECIFIC_CONFIG_CHANNEL_COUNT_TABLE[i]) {
            channelConfig = i;
        }
    }
    if (sampleRate == 0 || channelConfig == 0) {
        ALOGE("Invalid sample rate %d or number of channels: %d\n", sampleRate, channelCount);
    }
    return buildAudioSpecificConfig(AUDIO_OBJECT_TYPE_AAC_LC, sampleRateIndex, channelConfig);
}

  /**
   * Builds a simple AudioSpecificConfig, as defined in ISO 14496-3 1.6.2.1
   *
   * @param audioObjectType The audio object type.
   * @param sampleRateIndex The sample rate index.
   * @param channelConfig The channel configuration.
   * @return The AudioSpecificConfig.
   */
static char *buildAudioSpecificConfig(int audioObjectType, int sampleRateIndex, int channelConfig) {
    char specificConfig[2] = {};
    specificConfig[0] = (char) (((audioObjectType << 3) & 0xF8) | ((sampleRateIndex >> 1) & 0x07));
    specificConfig[1] = (char) (((sampleRateIndex << 7) & 0x80) | ((channelConfig << 3) & 0x78));
    return specificConfig;
}
#endif

// keep these values in sync with AudioFormat.java
#define ENCODING_PCM_16BIT      2
#define ENCODING_PCM_8BIT       3
#define ENCODING_PCM_FLOAT      4
#define ENCODING_AC3            5
#define ENCODING_E_AC3          6
#define ENCODING_DTS            7
#define ENCODING_DTS_HD         8
#define ENCODING_MP3            9
#define ENCODING_AAC_LC         10
#define ENCODING_AAC_HE_V1      11
#define ENCODING_AAC_HE_V2      12
#define ENCODING_IEC61937       13
#define ENCODING_DOLBY_TRUEHD   14
#define ENCODING_AAC_ELD        15
#define ENCODING_AAC_XHE        16
#define ENCODING_AC4            17
#define ENCODING_E_AC3_JOC      18
#define ENCODING_DOLBY_MAT      19
#define ENCODING_OPUS           20

#define ENCODING_INVALID    0
#define ENCODING_DEFAULT    1

  /** Returns the encoding for a given AAC audio object type. */
static int getEncodingForAudioObjectType(int audioObjectType) {
    ALOGV("%s line %d", __func__, __LINE__);
    switch (audioObjectType) {
    case AUDIO_OBJECT_TYPE_AAC_LC:
        return ENCODING_AAC_LC;
    case AUDIO_OBJECT_TYPE_AAC_SBR:
        return ENCODING_AAC_HE_V1;
    case AUDIO_OBJECT_TYPE_AAC_PS:
        return ENCODING_AAC_HE_V2;
    case AUDIO_OBJECT_TYPE_AAC_XHE:
        return ENCODING_AAC_XHE;
    case AUDIO_OBJECT_TYPE_AAC_ELD:
        return ENCODING_AAC_ELD;
    default:
        return ENCODING_INVALID;
    }
}

  /**
   * Returns the AAC audio object type as specified in 14496-3 (2005) Table 1.14.
   *
   * @param bitArray The bit array containing the audio specific configuration.
   * @return The audio object type.
   */
static int getAudioObjectType(struct audio_bit_parser *bit_parser) {
    ALOGV("%s line %d", __func__, __LINE__);
    int audioObjectType = aml_audio_bitparser_getBits(bit_parser, 5);
    if (audioObjectType == AUDIO_OBJECT_TYPE_ESCAPE) {
        audioObjectType = 32 + aml_audio_bitparser_getBits(bit_parser, 6);
    }
    return audioObjectType;
}

  /**
   * Returns the AAC sampling frequency (or extension sampling frequency) as specified in 14496-3
   * (2005) Table 1.13.
   *
   * @param bitArray The bit array containing the audio specific configuration.
   * @return The sampling frequency.
   */
static int getSamplingFrequency(struct audio_bit_parser *bit_parser) {
    ALOGV("%s line %d", __func__, __LINE__);
    int samplingFrequency;
    int frequencyIndex = aml_audio_bitparser_getBits(bit_parser, 4);
    if (frequencyIndex == AUDIO_SPECIFIC_CONFIG_FREQUENCY_INDEX_ARBITRARY) {
        samplingFrequency = aml_audio_bitparser_getBits(bit_parser, 24);
    } else {
        if (frequencyIndex >= 13) {
            ALOGE("frequencyIndex %d exceed the range!\n", frequencyIndex);
            return -1;
        }
        samplingFrequency = AUDIO_SPECIFIC_CONFIG_SAMPLING_RATE_TABLE[frequencyIndex];
    }
    return samplingFrequency;
}

static void parseGaSpecificConfig(
    struct audio_bit_parser *bit_parser, int audioObjectType, int channelConfiguration) {
    ALOGV("%s line %d", __func__, __LINE__);
    bool frameLengthFlag = aml_audio_bitparser_getBits(bit_parser, 1);
    if (frameLengthFlag) {
        ALOGW("Unexpected frameLengthFlag = 1");
    }
    bool dependsOnCoreDecoder = aml_audio_bitparser_getBits(bit_parser, 1);
    if (dependsOnCoreDecoder) {
        aml_audio_bitparser_skipBits(bit_parser, 14); // coreCoderDelay.
    }
    bool extensionFlag = aml_audio_bitparser_getBits(bit_parser, 1);
    if (channelConfiguration == 0) {
        //throw new UnsupportedOperationException(); // TODO: Implement programConfigElement();
        ALOGE("channelConfiguration %d illegal\n", channelConfiguration);
        return ;
    }
    if (audioObjectType == 6 || audioObjectType == 20) {
        aml_audio_bitparser_skipBits(bit_parser, 3); // layerNr.
    }
    if (extensionFlag) {
        if (audioObjectType == 22) {
            aml_audio_bitparser_skipBits(bit_parser, 16); // numOfSubFrame (5), layer_length(11).
        }
        if (audioObjectType == 17
            || audioObjectType == 19
            || audioObjectType == 20
            || audioObjectType == 23) {
            // aacSectionDataResilienceFlag, aacScalefactorDataResilienceFlag,
            // aacSpectralDataResilienceFlag.
            aml_audio_bitparser_skipBits(bit_parser, 3);
        }
        aml_audio_bitparser_skipBits(bit_parser, 1); // extensionFlag3.
    }
}


/**
 * Parses an AudioMuxElement as defined in 14496-3:2009, Section 1.7.3.1, Table 1.41.
 *
 * @param data A {@link ParsableBitArray} containing the AudioMuxElement's bytes.
 */
static int parseAudioMuxElement(struct audio_bit_parser *bit_parser, struct heaac_parser_info *heaac_info)
{
    ALOGV("%s line %d", __func__, __LINE__);
    bool useSameStreamMux = aml_audio_bitparser_getBits(bit_parser, 1);
    int ret = 0;
    if (!useSameStreamMux) {
        heaac_info->streamMuxRead = true;
        ret = parseStreamMuxConfig(bit_parser, heaac_info);
        if (ret) {
            ALOGE("parseStreamMuxConfig ret %d\n", ret);
            return ret;
        }
    } else if (!heaac_info->streamMuxRead) {
        ALOGE("%s line %d useSameStreamMux %d streamMuxRead %d bit offset %d\n", __func__, __LINE__, useSameStreamMux, heaac_info->streamMuxRead, aml_audio_bitparser_getPosition(bit_parser));
        return -1; // Parsing cannot continue without StreamMuxConfig information.
    }

    if (heaac_info->audioMuxVersionA == 0) {
        if (heaac_info->numSubframes != 0) {
            //throw new ParserException();
            ALOGE("numSubframes %d illegal\n", heaac_info->numSubframes);
            return -1;
        }
        int muxSlotLengthBytes = parsePayloadLengthInfo(bit_parser, heaac_info);
        parsePayloadMux(bit_parser, muxSlotLengthBytes);
        if (heaac_info->otherDataPresent) {
            aml_audio_bitparser_getBits(bit_parser, (int) heaac_info->otherDataLenBits);
        }
        return 0;
    } else {
        //throw new ParserException(); // Not defined by ISO/IEC 14496-3:2009.
        ALOGE("audioMuxVersionA %d illegal\n", heaac_info->audioMuxVersionA);
        return -1;
    }
}

/** Parses a StreamMuxConfig as defined in ISO/IEC 14496-3:2009 Section 1.7.3.1, Table 1.42. */
static int parseStreamMuxConfig(struct audio_bit_parser *bit_parser, struct heaac_parser_info *heaac_info) {
    ALOGV("%s line %d", __func__, __LINE__);
    int audioMuxVersion = aml_audio_bitparser_getBits(bit_parser, 1);
    heaac_info->audioMuxVersionA = audioMuxVersion == 1 ? aml_audio_bitparser_getBits(bit_parser, 1) : 0;
    if (heaac_info->audioMuxVersionA == 0) {
        if (audioMuxVersion == 1) {
            latmGetValue(bit_parser); // Skip taraBufferFullness.
        }
        if (!aml_audio_bitparser_getBits(bit_parser, 1)) {
            //throw new ParserException();
            ALOGE("allStreamSameTimeFraming(1bit) is 0 as illegal!\n");
            return -1;
        }
        heaac_info->numSubframes = aml_audio_bitparser_getBits(bit_parser, 6);
        int numProgram = aml_audio_bitparser_getBits(bit_parser, 4);
        int numLayer = aml_audio_bitparser_getBits(bit_parser, 3);
        if (numProgram != 0 || numLayer != 0) {
            //throw new ParserException();
            ALOGE("numProgram %d or numLayer %d illegal\n", numProgram, numLayer);
            return -1;
        }
        if (audioMuxVersion == 0) {
            int startPosition = aml_audio_bitparser_getPosition(bit_parser);
            int readBits = parseLatmAudioSpecificConfig(bit_parser, heaac_info);
            aml_audio_bitparser_setPosition(bit_parser, startPosition);
            /*
            byte[] initData = new byte[(readBits + 7) / 8];
            data.readBits(initData, 0, readBits);
            Format format =
            new Format.Builder()
                .setId(formatId)
                .setSampleMimeType(MimeTypes.AUDIO_AAC)
                .setCodecs(codecs)
                .setChannelCount(channelCount)
                .setSampleRate(sampleRateHz)
                .setInitializationData(Collections.singletonList(initData))
                .setLanguage(language)
                .build();
            if (!format.equals(this.format)) {
                this.format = format;
                sampleDurationUs = (MICROS_PER_SECOND * 1024) / format.sampleRate;
                output.format(format);
            }
            */
        } else {
            int ascLen = (int) latmGetValue(bit_parser);
            int bitsRead = parseLatmAudioSpecificConfig(bit_parser, heaac_info);
            aml_audio_bitparser_skipBits(bit_parser, (ascLen - bitsRead));
        }
        parseFrameLength(bit_parser, heaac_info);
        heaac_info->otherDataPresent = aml_audio_bitparser_getBits(bit_parser, 1);
        heaac_info->otherDataLenBits = 0;
        if (heaac_info->otherDataPresent) {
            if (audioMuxVersion == 1) {
                heaac_info->otherDataLenBits = latmGetValue(bit_parser);
            } else {
                bool otherDataLenEsc;
                do {
                    otherDataLenEsc = aml_audio_bitparser_getBits(bit_parser, 1);
                    heaac_info->otherDataLenBits = (heaac_info->otherDataLenBits << 8) + aml_audio_bitparser_getBits(bit_parser, 8);
                } while (otherDataLenEsc);
            }
        }
        bool crcCheckPresent = aml_audio_bitparser_getBits(bit_parser, 1);
        if (crcCheckPresent) {
            aml_audio_bitparser_skipBits(bit_parser, 8); // crcCheckSum.
        }
        return 0;
    } else {
        //throw new ParserException(); // This is not defined by ISO/IEC 14496-3:2009.
        ALOGE("audioMuxVersionA %d illegal\n", heaac_info->audioMuxVersionA);
        return -1;
    }
}

static void parseFrameLength(struct audio_bit_parser *bit_parser, struct heaac_parser_info *heaac_info) {
    ALOGV("%s line %d", __func__, __LINE__);
    heaac_info->frameLengthType = aml_audio_bitparser_getBits(bit_parser, 3);
    switch (heaac_info->frameLengthType) {
    case 0:
        aml_audio_bitparser_getBits(bit_parser, 8); // latmBufferFullness.
        break;
    case 1:
        aml_audio_bitparser_getBits(bit_parser, 9); // frameLength.
        break;
    case 3:
    case 4:
    case 5:
        aml_audio_bitparser_getBits(bit_parser, 6); // CELPframeLengthTableIndex.
        break;
    case 6:
    case 7:
        aml_audio_bitparser_getBits(bit_parser, 1); // HVXCframeLengthTableIndex.
        break;
    default:
        ALOGE("%s line %d unsupported frameLengthType %d!\n", __func__, __LINE__, heaac_info->frameLengthType);
        break;
    }
}

static int parseLatmAudioSpecificConfig(struct audio_bit_parser *bit_parser, struct heaac_parser_info *heaac_info)
{
    ALOGV("%s line %d", __func__, __LINE__);
    int bitsLeft = aml_audio_bitparser_bitsLeft(bit_parser);
    if (parseAudioSpecificConfig_ReadToEnd(bit_parser, heaac_info, /* forceReadToEnd= */ true)) {
        ALOGE("%s line %d sampleRateHz %d channelCount %d\n", __func__, __LINE__, heaac_info->sampleRateHz, heaac_info->channelCount);
    }

    return bitsLeft - aml_audio_bitparser_bitsLeft(bit_parser);
}

static int parsePayloadLengthInfo(struct audio_bit_parser *bit_parser, struct heaac_parser_info *heaac_info)
{
    ALOGV("%s line %d", __func__, __LINE__);
    int muxSlotLengthBytes = 0;
    // Assuming single program and single layer.
    if (heaac_info->frameLengthType == 0) {
        int tmp;
        do {
            tmp =  aml_audio_bitparser_getBits(bit_parser, 8);
            muxSlotLengthBytes += tmp;
        } while (tmp == 255);
        return muxSlotLengthBytes;
    } else {
        //throw new ParserException();
        ALOGE("frameLengthType %d illegal\n", heaac_info->frameLengthType);
        return 0;
    }
}


static void parsePayloadMux(struct audio_bit_parser *bit_parser, int muxLengthBytes) {
    ALOGV("%s line %d", __func__, __LINE__);
    // The start of sample data in
    int bitPosition = aml_audio_bitparser_getPosition(bit_parser);
    ALOGV("%s line %d bitPosition %d", __func__, __LINE__, bitPosition);
    return ;
    #if 0
    if ((bitPosition & 0x07) == 0) {
        // Sample data is byte-aligned. We can output it directly.
        sampleDataBuffer.setPosition(bitPosition >> 3);
    } else {
        // Sample data is not byte-aligned and we need align it ourselves before outputting.
        // Byte alignment is needed because LATM framing is not supported by MediaCodec.
        //data.readBits(sampleDataBuffer.getData(), 0, muxLengthBytes * 8);
        aml_audio_bitparser_readBits_to_buffer(bit_parser, sampleDataBuffer.getData(), 0, muxLengthBytes * 8)
        sampleDataBuffer.setPosition(0);
    }
    output.sampleData(sampleDataBuffer, muxLengthBytes);
    output.sampleMetadata(timeUs, C.BUFFER_FLAG_KEY_FRAME, muxLengthBytes, 0, null);
    timeUs += sampleDurationUs;
    #endif
}


static long latmGetValue(struct audio_bit_parser *bit_parser) {
    ALOGV("%s line %d", __func__, __LINE__);
    int bytesForValue = aml_audio_bitparser_getBits(bit_parser, 2);
    return aml_audio_bitparser_getBits(bit_parser, (bytesForValue + 1) * 8);
}


static int seek_heaac_loas_sync_word(char *buffer, int size)
{
    int i = -1;
    uint16_t syncword = 0;

    if (size < 2) {
        return -1;
    }

    for (i = 0; i < (size - 1); i++) {
        syncword = ((uint16_t)buffer[i + 0]<<8) + ((uint16_t)buffer[i + 1]);
        if ((syncword >> 5) == HEAAC_LOAS_SYNCWORD) {
            ALOGV("%s line %d return %d", __func__, __LINE__, i);
            return i;
        }
    }

    return -1;
}

static int seek_heaac_adts_sync_word(char *buffer, int size)
{
    int i = -1;
    uint16_t syncword = 0;

    if (size < 2) {
        return -1;
    }

    for (i = 0; i < (size - 1); i++) {
        syncword = ((uint16_t)buffer[i + 0]<<8) + ((uint16_t)buffer[i + 1]);
        if ((syncword >>  4) == HEAAC_ADTS_SYNCWORD) {
            ALOGV("%s line %d return %d", __func__, __LINE__, i);
            return i;
        }
    }

    return -1;
}

static int32_t readVariableBits(struct audio_bit_parser * bit_parser, int32_t nbits)
{
    int32_t value = 0;
    int32_t more_bits = 1;
    while (more_bits) {
        value += aml_audio_bitparser_getBits(bit_parser, nbits);
        more_bits = aml_audio_bitparser_getBits(bit_parser, 1);
        if (!more_bits) {
            break;
        }
        value++;
        value <<= nbits;
    }
    return value;
}

//Table 1.16 Sampling Frequency Index
static int convert_sampling_frequency_index_to_samplerate(int index)
{
    switch (index) {
        case 0x0:
            return 96000;
        case 0x1:
            return 88200;
        case 0x2:
            return 64000;
        case 0x3:
            return 48000;
        case 0x4:
            return 44100;
        case 0x5:
            return 32000;
        case 0x6:
            return 24000;
        case 0x7:
            return 22050;
        case 0x8:
            return 16000;
        case 0x9:
            return 12000;
        case 0xa:
            return 11025;
        case 0xb:
            return 8000;
        case 0xc:
            return 7350;
        case 0xd:
        case 0xe:
            ALOGE("%s line %d reserved index %d\n", __func__, __LINE__, index);
            return -1;
        case 0xf:
        default:
            ALOGE("%s line %d escape value %d\n", __func__, __LINE__, index);
            return -1;
    }
}

//Table 1.17 Channel Configuration
static int convert_channel_configuration_to_channelmask(int channel_configuration)
{
    switch (channel_configuration) {
        case 0:
            return -1;//defined in GASpecificConfig
        case 0x1:
            return AUDIO_CHANNEL_OUT_MONO; //center front speaker
        case 0x2:
            return AUDIO_CHANNEL_OUT_STEREO; //left, right front speakers
        case 0x3:
            return AUDIO_CHANNEL_OUT_2POINT1; //center front speaker, left, right front speakers
        case 0x4:
            return AUDIO_CHANNEL_OUT_SURROUND; //center front speaker, left, right front speakers,rear surround speakers
        case 0x5:
            return AUDIO_CHANNEL_OUT_PENTA; //center front speaker, left, right front speakers, left surround, right surround rear speakers
        case 0x6:
            return AUDIO_CHANNEL_OUT_5POINT1;//center front speaker, left, right front speakers, left surround, right surround rear speakers, front low frequency effects speaker
        case 0x7:
            return AUDIO_CHANNEL_OUT_7POINT1;//center front speaker, left, right front speakers, left surround, right surround rear speakers, front low frequency effects speaker, left, right outside front speakers,
        default:
            //8-15 reserved!
            ALOGE("%s line %d reserved channel_configuration %d\n", __func__, __LINE__, channel_configuration);
            return -1;
    }

}

static int parse_heaac_adts_frame_header(struct audio_bit_parser * bit_parser, const unsigned char *frameBuf, int length, struct heaac_parser_info * heaac_info)
{
    ALOGD("%s line %d", __func__, __LINE__);
    uint32_t frame_size = 0;
    uint32_t head_offset = 0;
    uint32_t sync_word = 0; //12bits
    uint32_t id = false; //1bit
    uint32_t layer = 0; //2bits
    uint32_t protection_absent = 0; //1bit
    uint32_t profile_object_type = 0; //2bits
    uint32_t sampling_frequency_index = 0; //4bits
    uint32_t private_bit = 0; //1bit
    uint32_t channel_configuration = 0; //3bit
    uint32_t original_copy = 0; //1bit
    uint32_t home = 0; //1bit
    uint32_t copyright_identification_bit = 0; //1bit
    uint32_t copyright_identification_start = 0; //1bit
    uint32_t aac_frame_length = 0; //13bits
    uint32_t adts_buffer_fullness = 0; //11bits
    uint32_t number_of_raw_data_blocks_in_frame = 0; //2bits

    if (length < HEAAC_HEADER_SIZE) {
        return -1;
    }

    aml_audio_bitparser_init(bit_parser, frameBuf, length);

    /* ETSI TS 103 190-2 V1.1.1 Annex C*/
    sync_word = aml_audio_bitparser_getBits(bit_parser, 12);
    if (sync_word != HEAAC_ADTS_SYNCWORD) {
        return -1;
    }
    head_offset += 12;

    id = aml_audio_bitparser_getBits(bit_parser, 1);
    head_offset += 1;

    layer = aml_audio_bitparser_getBits(bit_parser, 2);
    head_offset += 2;

    protection_absent = aml_audio_bitparser_getBits(bit_parser, 1);
    head_offset += 1;

    profile_object_type = aml_audio_bitparser_getBits(bit_parser, 2);
    head_offset += 2;

    sampling_frequency_index = aml_audio_bitparser_getBits(bit_parser, 4);
    head_offset += 4;

    private_bit = aml_audio_bitparser_getBits(bit_parser, 1);
    head_offset += 1;

    channel_configuration = aml_audio_bitparser_getBits(bit_parser, 3);
    head_offset += 3;

    original_copy = aml_audio_bitparser_getBits(bit_parser, 1);
    head_offset += 1;

    home = aml_audio_bitparser_getBits(bit_parser, 1);
    head_offset += 1;

    copyright_identification_bit = aml_audio_bitparser_getBits(bit_parser, 1);
    head_offset += 1;

    copyright_identification_start = aml_audio_bitparser_getBits(bit_parser, 1);
    head_offset += 1;

    aac_frame_length = aml_audio_bitparser_getBits(bit_parser, 13);
    head_offset += 13;

    adts_buffer_fullness = aml_audio_bitparser_getBits(bit_parser, 11);
    head_offset += 11;

    number_of_raw_data_blocks_in_frame = aml_audio_bitparser_getBits(bit_parser, 2);
    head_offset += 2;

    frame_size = aac_frame_length;
    if ((frame_size == 0) || (frame_size >= 8191)) {
        ALOGE("Invalid HEAAC ADTS frame size 0");
        return -1;
    }

    heaac_info->frame_size = frame_size;
    heaac_info->sample_rate = convert_sampling_frequency_index_to_samplerate(sampling_frequency_index);
    heaac_info->channel_mask = convert_channel_configuration_to_channelmask(channel_configuration);
    if (heaac_info->debug_print) {
        ALOGD("heaac adts sampling_frequency_index=%d sample rate=%d\n", sampling_frequency_index, heaac_info->sample_rate);
        ALOGD("heaac adts channel_configuration=%d channel_mask=%d\n", channel_configuration, heaac_info->channel_mask);
        ALOGD("heaac adts frame size=%d sample rate=%d\n", heaac_info->frame_size, heaac_info->sample_rate);
    }
    return 0;
}

static int parse_heaac_loas_frame_header(struct audio_bit_parser * bit_parser, const unsigned char *frameBuf, int length, struct heaac_parser_info * heaac_info)
{
    uint32_t frame_size = 0;
    uint32_t head_size = 0;
    uint32_t head_offset = 0;

    uint32_t syncword = 0;
    uint32_t audio_mux_length_bytes = 0;
    uint32_t frame_length = 0;
    int ret = 0;
    if (length < HEAAC_HEADER_SIZE) {
        return -1;
    }

    aml_audio_bitparser_init(bit_parser, frameBuf, length);

    syncword = aml_audio_bitparser_getBits(bit_parser, 11);
    if (syncword != HEAAC_LOAS_SYNCWORD) {
        ALOGE("%s line %d syncword 0x%x", __func__, __LINE__, syncword);
        return -1;
    }
    head_offset += 11;

    audio_mux_length_bytes = aml_audio_bitparser_getBits(bit_parser, 13);
    head_offset += 13;


    frame_size= audio_mux_length_bytes + head_offset / 8;
    heaac_info->frame_size = frame_size;
    ret = parseAudioMuxElement(bit_parser, heaac_info);
    if (ret) {
        ALOGE("%s line %d parse_AudioMuxElement ret %d frame 4bytes 0x%x 0x%x 0x%x 0x%x\n", __func__, __LINE__, ret, frameBuf[0], frameBuf[1], frameBuf[2], frameBuf[3]);
        return -1;
    }
    //heaac_info->sample_rate = heaac_info->sampleRateHz; // todo
    //heaac_info->channel_mask = heaac_info->channelCount; // todo

    if (heaac_info->debug_print) {
        ALOGD("heaac loas frame size %d sampleRateHz %d channelCount %d\n", heaac_info->frame_size, heaac_info->sampleRateHz, heaac_info->channelCount);
    }

    return 0;
}


int aml_heaac_parser_process(void *parser_handle, const void *in_buffer, int32_t numBytes, int32_t *used_size, void **output_buf, int32_t *out_size, struct heaac_parser_info * heaac_info)
{
    struct aml_heaac_parser *heaac_parser_handle = (struct aml_heaac_parser *)parser_handle;
    size_t remain = 0;
    uint8_t *buffer = (uint8_t *)in_buffer;
    uint8_t * parser_buf = NULL;
    int32_t sync_word_offset = -1;
    int32_t buf_left = 0;
    int32_t buf_offset = 0;
    int32_t need_size = 0;

    int32_t ret = 0;
    int32_t data_valid = 0;
    int32_t new_buf_size = 0;
    int32_t loop_cnt = 0;
    int32_t frame_size = 0;
    int32_t frame_offset = 0;

    if (heaac_parser_handle == NULL) {
        ALOGE("error heaac_parser_handle is NULL");
        goto error;
    }

    if (heaac_info == NULL) {
        ALOGE("error heaac_info is NULL");
        goto error;
    }
    bool is_loas = heaac_info->is_loas;
    bool is_adts = heaac_info->is_adts;

    heaac_info->frame_size = 0;
    heaac_info->sample_rate = 0;
    heaac_info->channel_mask = 0;

    parser_buf = heaac_parser_handle->buf;
    buf_left   = numBytes;

    if (heaac_info->debug_print) {
        ALOGD("%s input buf size %d status %d is_loas %d is_adts %d\n", __func__, numBytes, heaac_parser_handle->status, is_loas, is_adts);
    }

    /*we need at least HEAAC_HEADER_SIZE bytes*/
    if (heaac_parser_handle->buf_remain < HEAAC_HEADER_SIZE) {
        need_size = HEAAC_HEADER_SIZE - heaac_parser_handle->buf_remain;
        /*input data is not enough, just copy to internal buf*/
        if (buf_left < need_size) {
            memcpy(parser_buf + heaac_parser_handle->buf_remain, buffer + buf_offset, buf_left);
            heaac_parser_handle->buf_remain += buf_left;
            goto error;
        }
        /*make sure the remain buf has HEAAC_HEADER_SIZE bytes*/
        memcpy(parser_buf + heaac_parser_handle->buf_remain, buffer + buf_offset, need_size);
        heaac_parser_handle->buf_remain += need_size;
        buf_offset += need_size;
        buf_left   = numBytes - buf_offset;

    }

    if (heaac_parser_handle->status == PARSER_SYNCING) {
        sync_word_offset = -1;
        while (sync_word_offset < 0) {
            /*sync the header, we have at least period bytes*/
            if (heaac_parser_handle->buf_remain < HEAAC_HEADER_SIZE) {
                ALOGE("we should not get there");
                heaac_parser_handle->buf_remain = 0;
                goto error;
            }
            if (is_loas) {
                sync_word_offset = seek_heaac_loas_sync_word((char*)parser_buf, heaac_parser_handle->buf_remain);
                if (heaac_info->debug_print) {
                    ALOGD("%s line %d sync_word_offset %d", __func__, __LINE__, sync_word_offset);
                    if (sync_word_offset >= 0) {
                        ALOGD("%s line %d frame 4bytes 0x%x 0x%x 0x%x 0x%x\n",
                            __func__, __LINE__, buffer[sync_word_offset], buffer[sync_word_offset + 1], buffer[sync_word_offset + 2], buffer[sync_word_offset + 3]);
                    }
                }
            }
            else if (is_adts) {
                sync_word_offset = seek_heaac_adts_sync_word((char*)parser_buf, heaac_parser_handle->buf_remain);
            }
            /*if we don't find the header in period bytes, move the last 1 bytes to header*/
            if (sync_word_offset < 0) {
                memmove(parser_buf, parser_buf + heaac_parser_handle->buf_remain - 1, 1);
                heaac_parser_handle->buf_remain = 1;
                need_size = HEAAC_HEADER_SIZE - heaac_parser_handle->buf_remain;
                /*input data is not enough, just copy to internal buf*/
                if (buf_left < need_size) {
                    memcpy(parser_buf + heaac_parser_handle->buf_remain, buffer + buf_offset, buf_left);
                    heaac_parser_handle->buf_remain += buf_left;
                    /*don't find the header, and there is no enough data*/
                    goto error;
                }
                /*make the buf has HEAAC_HEADER_SIZE bytes*/
                memcpy(parser_buf + heaac_parser_handle->buf_remain, buffer + buf_offset, need_size);
                heaac_parser_handle->buf_remain += need_size;
                buf_offset += need_size;
                buf_left = numBytes - buf_offset;
            }
            loop_cnt++;
        }
        /*got here means we find the sync word*/
        heaac_parser_handle->status = PARSER_SYNCED;

        data_valid = heaac_parser_handle->buf_remain - sync_word_offset;
        /*move the header to the beginning of buf*/
        if (sync_word_offset != 0) {
            memmove(parser_buf, parser_buf + sync_word_offset, data_valid);
        }
        heaac_parser_handle->buf_remain = data_valid;

        need_size = HEAAC_HEADER_SIZE - data_valid;
        /*get some bytes to make sure it is at least HEAAC_HEADER_SIZE bytes*/
        if (need_size > 0) {
            /*check if input has enough data*/
            if (buf_left < need_size) {
                memcpy(parser_buf + heaac_parser_handle->buf_remain, buffer + buf_offset, buf_left);
                heaac_parser_handle->buf_remain += buf_left;
                goto error;
            }
            /*make sure the remain buf has HEAAC_HEADER_SIZE bytes*/
            memcpy(parser_buf + heaac_parser_handle->buf_remain, buffer + buf_offset , need_size);
            heaac_parser_handle->buf_remain += need_size;
            buf_offset += need_size;
            buf_left = numBytes - buf_offset;
        }


    }

    /*double check here*/
    if (is_loas) {
        sync_word_offset = seek_heaac_loas_sync_word((char*)parser_buf, heaac_parser_handle->buf_remain);
        if (heaac_info->debug_print) {
            ALOGD("%s line %d sync_word_offset %d", __func__, __LINE__, sync_word_offset);
            if (sync_word_offset >= 0) {
                ALOGD("%s line %d frame 4bytes 0x%x 0x%x 0x%x 0x%x\n",
                    __func__, __LINE__, buffer[sync_word_offset], buffer[sync_word_offset + 1], buffer[sync_word_offset + 2], buffer[sync_word_offset + 3]);
            }
        }
    }
    else if (is_adts) {
        sync_word_offset = seek_heaac_adts_sync_word((char*)parser_buf, heaac_parser_handle->buf_remain);
    }

    if (sync_word_offset != 0) {
        ALOGE("we can't get here remain=%d,resync dolby header", heaac_parser_handle->buf_remain);
        heaac_parser_handle->buf_remain = 0;
        heaac_parser_handle->status = PARSER_SYNCING;
        goto error;
    }
    /* we got here means we find the heaac header and
     * it is at the beginning of  parser buf and
     * it has at least HEAAC_HEADER_SIZE bytes, we can parse it
     */
    if (is_loas) {
        ret = parse_heaac_loas_frame_header(&heaac_parser_handle->bit_parser, parser_buf, heaac_parser_handle->buf_remain, heaac_info);
    }
    else if (is_adts) {
        ret = parse_heaac_adts_frame_header(&heaac_parser_handle->bit_parser, parser_buf, heaac_parser_handle->buf_remain, heaac_info);
    }

    /*check whether the input data has a complete heaac frame*/
    if (ret != 0 || heaac_info->frame_size == 0) {
        ALOGE("%s wrong frame size=%d", __func__, heaac_info->frame_size);
        heaac_parser_handle->buf_remain = 0;
        heaac_parser_handle->status = PARSER_SYNCING;
        goto error;
    }
    frame_size = heaac_info->frame_size;

    /*we have a complete payload*/
    if ((heaac_parser_handle->buf_remain + buf_left) >= frame_size) {
        need_size = frame_size - (heaac_parser_handle->buf_remain);
        if (need_size >= 0) {
            new_buf_size = heaac_parser_handle->buf_remain + need_size;
            if (new_buf_size > heaac_parser_handle->buf_size) {
                void * new_buf = aml_audio_realloc(heaac_parser_handle->buf, new_buf_size);
                if (new_buf == NULL) {
                    ALOGE("%s realloc buf failed =%d", __func__, new_buf_size);
                    heaac_parser_handle->buf_remain = 0;
                    heaac_parser_handle->status = PARSER_SYNCING;
                    goto error;
                }
                heaac_parser_handle->buf      = new_buf;
                heaac_parser_handle->buf_size = new_buf_size;
                parser_buf = heaac_parser_handle->buf;
                ALOGI("%s realloc buf =%d", __func__, new_buf_size);
            }

            memcpy(parser_buf + heaac_parser_handle->buf_remain, buffer + buf_offset, need_size);
            buf_offset += need_size;
            buf_left = numBytes - buf_offset;

            *output_buf = (void*)(parser_buf);
            *out_size   = frame_size;
            *used_size  = buf_offset;
            ALOGV("OK framesize =%d used size=%d loop_cnt=%d", frame_size, buf_offset, loop_cnt);
            /*one frame has complete, need find next one*/
            heaac_parser_handle->buf_remain = 0;
            heaac_parser_handle->status = PARSER_SYNCING;
        } else {
            /*internal buf has more data than framesize, we only need part of it*/
            *output_buf = (void*)(parser_buf);
            *out_size   = frame_size;
            /*move need_size bytes back to original buf*/
            *used_size  = buf_offset + need_size;
            ALOGV("wrap frame size=%d used size=%d back size =%d loop_cnt=%d", frame_size, buf_offset, need_size, loop_cnt);
            if (*used_size <= 0) {
                ALOGE("%s wrong used size =%d", __func__, *used_size);
                heaac_parser_handle->buf_remain = 0;
                heaac_parser_handle->status = PARSER_SYNCING;
                goto error;
            }
            /*one frame has complete, need find next one*/
            heaac_parser_handle->buf_remain = 0;
            heaac_parser_handle->status = PARSER_SYNCING;
        }
    } else {
        /*check whether the input buf size is big enough*/
        new_buf_size = heaac_parser_handle->buf_remain + buf_left;
        if (new_buf_size > heaac_parser_handle->buf_size) {
            void * new_buf = aml_audio_realloc(heaac_parser_handle->buf, new_buf_size);
            if (new_buf == NULL) {
                ALOGE("%s realloc buf failed =%d", __func__, new_buf_size);
                heaac_parser_handle->buf_remain = 0;
                heaac_parser_handle->status = PARSER_SYNCING;
                goto error;
            }
            heaac_parser_handle->buf      = new_buf;
            heaac_parser_handle->buf_size = new_buf_size;
            parser_buf = heaac_parser_handle->buf;
            ALOGI("%s realloc buf =%d", __func__, new_buf_size);
        }
        memcpy(parser_buf + heaac_parser_handle->buf_remain, buffer + buf_offset, buf_left);
        heaac_parser_handle->buf_remain += buf_left;
        heaac_parser_handle->status = PARSER_LACK_DATA;
        goto error;
    }
    if (heaac_info->debug_print) {
        ALOGD("%s line %d parser_handle %p return success!\n", __func__, __LINE__, heaac_parser_handle);
    }
    return 0;

error:
    *output_buf = NULL;
    *out_size   = 0;
    *used_size = numBytes;
    if (heaac_info->debug_print) {
        ALOGD("%s line %d parser_handle %p return error!\n", __func__, __LINE__, heaac_parser_handle);
    }
    return 0;
}
