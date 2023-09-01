/*
 * Copyright (C) 2018 The Android Open Source Project
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

#define LOG_TAG "DPE"
#define LOG_NDEBUG 0

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <new>

#include <log/log.h>
#include <sys/param.h>

#include <cutils/str_parms.h>
#include <cutils/properties.h>


#include <effect_dynamicsprocessing.h>
#include <dsp/DPBase.h>
#include <dsp/DPFrequency.h>


//#define VERY_VERY_VERBOSE_LOGGING
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while (false)
#endif

#define DPE_FRAME_SIZE 256
#define CHANNEL_COUNT 2

#define aml_audio_realloc(x, y) realloc(x, y)

#define AM_LOGE(fmt, ...)  ALOGE("[%s:%d] " fmt, __func__,__LINE__, ##__VA_ARGS__)

#define R_CHECK_RET(ret, fmt, ...)                                                              \
    if (ret != 0) {                                                                             \
    AM_LOGE("ret:%d " fmt, ret, ##__VA_ARGS__);                                             \
    return ret;                                                                             \
    }


// union to hold command values
using value_t = union {
    int32_t i;
    float f;
};

// effect_handle_t interface implementation for DP effect
extern const struct effect_interface_s gDPInterface;

const effect_descriptor_t gDPDescriptor = {
        {0xf70bcbf4, 0x7457, 0x11ec, 0xb4f0, {0x02, 0x00, 0x17, 0x00, 0x0b, 0x7b}}, // type
        {0x3f16eef0, 0x7459, 0x11ec, 0x9272, {0x02, 0x00, 0x17, 0x00, 0x0b, 0x7b}}, // uuid
        EFFECT_CONTROL_API_VERSION,
        EFFECT_FLAG_TYPE_POST_PROC | EFFECT_FLAG_DEVICE_IND | EFFECT_FLAG_NO_PROCESS | EFFECT_FLAG_OFFLOAD_SUPPORTED,
        DPE_CUP_LOAD_ARM9E,
        DPE_MEM_USAGE,
        "DPE",
        "DPE Labs",
};

enum dp_state_e {
    DYNAMICS_PROCESSING_STATE_UNINITIALIZED,
    DYNAMICS_PROCESSING_STATE_INITIALIZED,
    DYNAMICS_PROCESSING_STATE_ACTIVE,
};

struct DynamicsProcessingContext {
    const struct effect_interface_s *mItfe;
    effect_config_t mConfig;
    uint8_t mState;

    dp_fx::DPBase * mPDynamics; //the effect (or current effect)
    int32_t mCurrentVariant;
    float mPreferredFrameDuration;

    float *in_float;
    int16_t left_bytes;
    int16_t left_process_bytes;
    /*dpe process one block 256 frames,framessize (2 bytes*2ch )*/
    int16_t left_pBuffer[256 * 4];
    int16_t left_process_pBuffer[256 * 4];
};

// The value offset of an effect parameter is computed by rounding up
// the parameter size to the next 32 bit alignment.
static inline uint32_t computeParamVOffset(const effect_param_t *p) {
    return ((p->psize + sizeof(int32_t) - 1) / sizeof(int32_t)) *
            sizeof(int32_t);
}

//--- local function prototypes
int DP_setParameter(DynamicsProcessingContext *pContext,
        uint32_t paramSize,
        void *pParam,
        uint32_t valueSize,
        void *pValue);
int DP_getParameter(DynamicsProcessingContext *pContext,
        uint32_t paramSize,
        void *pParam,
        uint32_t *pValueSize,
        void *pValue);
int DP_getParameterCmdSize(uint32_t paramSize,
        void *pParam);
void DP_expectedParamValueSizes(uint32_t paramSize,
        void *pParam,
        bool isSet,
        uint32_t *pCmdSize,
        uint32_t *pValueSize);
//
//--- Local functions (not directly used by effect interface)
//

void DP_reset(DynamicsProcessingContext *pContext)
{
    ALOGV("> DP_reset(%p)", pContext);
    if (pContext->mPDynamics != NULL) {
        pContext->mPDynamics->reset();
    } else {
        ALOGE("DP_reset(%p): null DynamicsProcessing", pContext);
    }
    pContext->left_bytes = 0;
    pContext->left_process_bytes = 0;

}

//----------------------------------------------------------------------------
// DP_setConfig()
//----------------------------------------------------------------------------
// Purpose: Set input and output audio configuration.
//
// Inputs:
//  pContext:   effect engine context
//  pConfig:    pointer to effect_config_t structure holding input and output
//      configuration parameters
//
// Outputs:
//
//----------------------------------------------------------------------------

int DP_setConfig(DynamicsProcessingContext *pContext, effect_config_t *pConfig)
{
    ALOGV("DP_setConfig(%p)", pContext);

    if (pConfig->inputCfg.samplingRate != pConfig->outputCfg.samplingRate)
        return -EINVAL;
    if (pConfig->inputCfg.channels != pConfig->outputCfg.channels)
        return -EINVAL;
    if (pConfig->inputCfg.format != pConfig->outputCfg.format)
        return -EINVAL;
    if (pConfig->inputCfg.channels != AUDIO_CHANNEL_OUT_STEREO) {
        ALOGW("%s: channels in = 0x%x channels out = 0x%x", __FUNCTION__,
            pConfig->inputCfg.channels, pConfig->outputCfg.channels);
        pConfig->inputCfg.channels = pConfig->outputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
    }
    if (pConfig->outputCfg.accessMode != EFFECT_BUFFER_ACCESS_WRITE &&
            pConfig->outputCfg.accessMode != EFFECT_BUFFER_ACCESS_ACCUMULATE)
        return -EINVAL;
    if (pConfig->inputCfg.format != AUDIO_FORMAT_PCM_16_BIT) {
        ALOGW("%s: format in = 0x%x format out = 0x%x", __FUNCTION__, pConfig->inputCfg.format, pConfig->outputCfg.format);
        pConfig->inputCfg.format = pConfig->outputCfg.format = AUDIO_FORMAT_PCM_16_BIT;
    }
    memcpy(&pContext->mConfig, pConfig, sizeof(effect_config_t));

    return 0;
}

//----------------------------------------------------------------------------
// DP_getConfig()
//----------------------------------------------------------------------------
// Purpose: Get input and output audio configuration.
//
// Inputs:
//  pContext:   effect engine context
//  pConfig:    pointer to effect_config_t structure holding input and output
//      configuration parameters
//
// Outputs:
//
//----------------------------------------------------------------------------

void DP_getConfig(DynamicsProcessingContext *pContext, effect_config_t *pConfig)
{
    *pConfig = pContext->mConfig;
}

//----------------------------------------------------------------------------
// DP_init()
//----------------------------------------------------------------------------
// Purpose: Initialize engine with default configuration.
//
// Inputs:
//  pContext:   effect engine context
//
// Outputs:
//
//----------------------------------------------------------------------------

int DP_init(DynamicsProcessingContext *pContext)
{
    ALOGV("DP_init(%p)", pContext);

    pContext->mItfe = &gDPInterface;
    pContext->mPDynamics = NULL;
    pContext->mState = DYNAMICS_PROCESSING_STATE_UNINITIALIZED;

    pContext->mConfig.inputCfg.accessMode = EFFECT_BUFFER_ACCESS_READ;
    pContext->mConfig.inputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
    pContext->mConfig.inputCfg.format = AUDIO_FORMAT_PCM_FLOAT;
    pContext->mConfig.inputCfg.samplingRate = 48000;
    pContext->mConfig.inputCfg.bufferProvider.getBuffer = NULL;
    pContext->mConfig.inputCfg.bufferProvider.releaseBuffer = NULL;
    pContext->mConfig.inputCfg.bufferProvider.cookie = NULL;
    pContext->mConfig.inputCfg.mask = EFFECT_CONFIG_ALL;
    pContext->mConfig.outputCfg.accessMode = EFFECT_BUFFER_ACCESS_ACCUMULATE;
    pContext->mConfig.outputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
    pContext->mConfig.outputCfg.format = AUDIO_FORMAT_PCM_FLOAT;
    pContext->mConfig.outputCfg.samplingRate = 48000;
    pContext->mConfig.outputCfg.bufferProvider.getBuffer = NULL;
    pContext->mConfig.outputCfg.bufferProvider.releaseBuffer = NULL;
    pContext->mConfig.outputCfg.bufferProvider.cookie = NULL;
    pContext->mConfig.outputCfg.mask = EFFECT_CONFIG_ALL;

    pContext->mCurrentVariant = -1; //none
    pContext->mPreferredFrameDuration = 0; //none

    pContext->in_float = (float*)malloc(DPE_FRAME_SIZE * sizeof(float) * CHANNEL_COUNT);
    if (pContext->in_float == NULL) {
        return -EINVAL;
    }

    DP_setConfig(pContext, &pContext->mConfig);
    pContext->mState = DYNAMICS_PROCESSING_STATE_INITIALIZED;
    pContext->left_bytes = 0;
    pContext->left_process_bytes = 0;
    ALOGD("%s: successful", __FUNCTION__);

    return 0;
}

void DP_changeVariant(DynamicsProcessingContext *pContext, int newVariant) {
    ALOGV("DP_changeVariant from %d to %d", pContext->mCurrentVariant, newVariant);
    switch (newVariant) {
    case VARIANT_FAVOR_FREQUENCY_RESOLUTION: {
        pContext->mCurrentVariant = VARIANT_FAVOR_FREQUENCY_RESOLUTION;
        delete pContext->mPDynamics;
        pContext->mPDynamics = new dp_fx::DPFrequency();
        break;
    }
    default: {
        pContext->mCurrentVariant = -1;
        ALOGW("DynamicsProcessing variant %d not available for creation", newVariant);
        break;
    }
    } //switch
}

void DP_configureVariant(DynamicsProcessingContext *pContext, int newVariant) {
    ALOGV("DP_configureVariant %d", newVariant);
    switch (newVariant) {
    case VARIANT_FAVOR_FREQUENCY_RESOLUTION: {
        int32_t minBlockSize = (int32_t)dp_fx::DPFrequency::getMinBockSize();
        int32_t desiredBlock = pContext->mPreferredFrameDuration *
                pContext->mConfig.inputCfg.samplingRate / 1000.0f;
        int32_t currentBlock = desiredBlock;
        ALOGV(" sampling rate: %d, desiredBlock size %0.2f (%d) samples",
                pContext->mConfig.inputCfg.samplingRate, pContext->mPreferredFrameDuration,
                desiredBlock);
        if (desiredBlock < minBlockSize) {
            currentBlock = minBlockSize;
        } else if (!powerof2(desiredBlock)) {
            //find next highest power of 2.
            currentBlock = 1 << (32 - __builtin_clz(desiredBlock));
        }
        ((dp_fx::DPFrequency*)pContext->mPDynamics)->configure(currentBlock,
                currentBlock/2,
                pContext->mConfig.inputCfg.samplingRate);
        break;
    }
    default: {
        ALOGE("DynamicsProcessing variant %d not available to configure", newVariant);
        break;
    }
    }
}

//
//--- Effect Library Interface Implementation
//

int DPLib_Release(effect_handle_t handle) {
    DynamicsProcessingContext * pContext = (DynamicsProcessingContext *)handle;

    ALOGV("DPLib_Release %p", handle);
    if (pContext == NULL) {
        return -EINVAL;
    }

    if (pContext->in_float != NULL) {
        free(pContext->in_float);
    }

    delete pContext->mPDynamics;
    delete pContext;

    return 0;
}

int DPLib_Create(const effect_uuid_t *uuid,
                         int32_t sessionId __unused,
                         int32_t ioId __unused,
                         effect_handle_t *pHandle) {
    ALOGV("DPLib_Create()");

    if (pHandle == NULL || uuid == NULL) {
        return -EINVAL;
    }

    if (memcmp(uuid, &gDPDescriptor.uuid, sizeof(*uuid)) != 0) {
        return -EINVAL;
    }

    DynamicsProcessingContext *pContext = new DynamicsProcessingContext;
    *pHandle = (effect_handle_t)pContext;
    int ret = DP_init(pContext);
    if (ret < 0) {
        ALOGW("DPLib_Create() init failed");
        DPLib_Release(*pHandle);
        return ret;
    }

    ALOGV("DPLib_Create context is %p", pContext);
    return 0;
}

int DPLib_Create_3_1(const effect_uuid_t *uuid,
                         int32_t sessionId,
                         int32_t ioId,
                         int32_t deviceId __unused,
                         effect_handle_t *pHandle) {
    return DPLib_Create(uuid, sessionId, ioId, pHandle);
}


int DPLib_GetDescriptor(const effect_uuid_t *uuid,
                                effect_descriptor_t *pDescriptor) {

    if (pDescriptor == NULL || uuid == NULL) {
        ALOGE("DPLib_GetDescriptor() called with NULL pointer");
        return -EINVAL;
    }

    if (memcmp(uuid, &gDPDescriptor.uuid, sizeof(*uuid)) == 0) {
        *pDescriptor = gDPDescriptor;
        return 0;
    }

    return -EINVAL;
} /* end DPLib_GetDescriptor */


int getprop_bool(const char * path)
{
    char buf[92];
    int ret = -1;

    ret = property_get(path, buf, NULL);
    if (ret > 0) {
        if (strcasecmp(buf, "true") == 0 || strcmp(buf, "1") == 0)
            return 1;
    }

    return 0;
}


float float_from_i16(int16_t ival)
{
    /* The scale factor is the reciprocal of the nominal 16 bit integer
     * half-sided range (32768).
     *
     * Since the scale factor is a power of 2, the scaling is exact, and there
     * is no rounding due to the multiplication - the bit pattern is preserved.
     */
    static const float scale = 1. / (float)(1UL << 15);

    return ival * scale;
}


int16_t clamp16_from_float(float f)
{
#if 0
    /* Offset is used to expand the valid range of [-1.0, 1.0) into the 16 lsbs of the
     * floating point significand. The normal shift is 3<<22, but the -15 offset
     * is used to multiply by 32768.
     */
    static const float offset = (float)(3 << (22 - 15));
    /* zero = (0x10f << 22) =  0x43c00000 (not directly used) */
    static const int32_t limneg = (0x10f << 22) /*zero*/ - 32768; /* 0x43bf8000 */
    static const int32_t limpos = (0x10f << 22) /*zero*/ + 32767; /* 0x43c07fff */

    union {
        float f;
        int32_t i;
    } u;

    u.f = f + offset; /* recenter valid range */
    /* Now the valid range is represented as integers between [limneg, limpos].
     * Clamp using the fact that float representation (as an integer) is an ordered set.
     */
    if (u.i < limneg)
        u.i = -32768;
    else if (u.i > limpos)
        u.i = 32767;      return u.i; /* Return lower 16 bits, the part of interest in the significand. */
#else
    static const float scale = 1 << 15;
    return roundf(fmaxf(fminf(f * scale, scale - 1.f), -scale));
#endif
}


void memcpy_to_i16_from_float(int16_t *dst, const float *src, size_t count)
{
    for (; count > 0; --count) {
        *dst++ = clamp16_from_float(*src++);
    }
}

void memcpy_to_float_from_i16(float *dst, const int16_t *src, size_t count)
{
    dst += count;
    src += count;
    for (; count > 0; --count) {
        *--dst = float_from_i16(*--src);
    }
}

int aml_audio_check_and_realloc(void** pointer, size_t* cur_size, size_t need_size)
{
    if (pointer == NULL || cur_size == NULL) {
        ALOGE("[%s:%d] pointer:%p or cur_size:%p is null", __func__, __LINE__, pointer, cur_size);
        return -1;
    }

    if (*cur_size < need_size || *pointer == NULL) {
        void *p = aml_audio_realloc(*pointer, need_size);
        if (p == NULL) {
            ALOGE("[%s:%d] realloc buffer failed size:%zu", __func__, __LINE__, need_size);
            return -ENOMEM;
        } else {
            memset(p, 0, need_size);
            *pointer = p;
        }
        *cur_size = need_size;
    }
    return 0;
}

//
//--- Effect Control Interface Implementation
//
int DP_process(effect_handle_t self, audio_buffer_t *inBuffer,
        audio_buffer_t *outBuffer) {
    DynamicsProcessingContext * pContext = (DynamicsProcessingContext *)self;

    if (pContext == NULL) {
        ALOGE("DP_process() called with NULL context");
        return -EINVAL;
    }

    //ALOGE("inBuffer->frameCount: %zu", inBuffer->frameCount);

    if (inBuffer == NULL || inBuffer->raw == NULL ||
        outBuffer == NULL || outBuffer->raw == NULL ||
        inBuffer->frameCount != outBuffer->frameCount ||
        inBuffer->frameCount == 0) {
        ALOGE("inBuffer or outBuffer are NULL or have problems with frame count");
        return -EINVAL;
    }

    /*
    if (pContext->mState != DYNAMICS_PROCESSING_STATE_ACTIVE) {
        ALOGE("mState is not DYNAMICS_PROCESSING_STATE_ACTIVE. Current mState %d",
                pContext->mState);
        return -ENODATA;
    }
    */

    int16_t  *in   = (int16_t *)inBuffer->raw;
    int16_t  *out  = (int16_t *)outBuffer->raw;
    int32_t  byte_counter;
    int dpe_frameCount = inBuffer->frameCount;

    int32_t channelCount = (int32_t)audio_channel_count_from_out_mask(
        pContext->mConfig.inputCfg.channels);

    /*
    bool DPE_prop = getprop_bool("vendor.media.dpe.audiohal.outdump");
    if (DPE_prop) {
        FILE *dump_fp_in = fopen("/data/audio/audio_dpe_in.pcm", "a+");
        if (dump_fp_in != NULL) {
            fwrite(in, 2, inBuffer->frameCount * 2, dump_fp_in);
            fclose(dump_fp_in);
        }
    }
    */

    //if dynamics exist...
    if (pContext->mPDynamics != NULL && pContext->mCurrentVariant == 0) {

        byte_counter = inBuffer->frameCount * sizeof(int16_t) * 2;
        if (pContext->left_bytes > 0) {
            memmove((int8_t *)inBuffer->raw + pContext->left_bytes, inBuffer->raw ,byte_counter);
            memcpy((int8_t *)inBuffer->raw, pContext->left_pBuffer ,pContext->left_bytes);
            inBuffer->frameCount += pContext->left_bytes / (sizeof(int16_t) * channelCount);
            byte_counter =  inBuffer->frameCount * sizeof(int16_t) * channelCount;
        }
        int32_t blockSize = DPE_FRAME_SIZE;
        int32_t blockCount = inBuffer->frameCount / blockSize;
        pContext->left_bytes = byte_counter - sizeof(int16_t) * channelCount *  blockCount * blockSize;
        if (pContext->left_bytes > 0) {
            memcpy(pContext->left_pBuffer, (int8_t *)inBuffer->raw + (byte_counter - pContext->left_bytes), pContext->left_bytes);
        }
        outBuffer->frameCount = DPE_FRAME_SIZE *  blockCount;

        for (int i = 0; i < blockCount; i++) {
            memcpy_to_float_from_i16(pContext->in_float, in + i * DPE_FRAME_SIZE * channelCount,
                DPE_FRAME_SIZE * channelCount);

            pContext->mPDynamics->processSamples(pContext->in_float, pContext->in_float,
                DPE_FRAME_SIZE * channelCount);

            if (inBuffer->raw != outBuffer->raw) {
                if (pContext->mConfig.outputCfg.accessMode != EFFECT_BUFFER_ACCESS_ACCUMULATE) {
                    memcpy(outBuffer->raw, inBuffer->raw,
                        DPE_FRAME_SIZE * channelCount * sizeof(float));
                }
            }

            memcpy_to_i16_from_float(out + i * DPE_FRAME_SIZE * channelCount, pContext->in_float,
                DPE_FRAME_SIZE * channelCount);
        }

        if (outBuffer->frameCount < dpe_frameCount && pContext->left_process_bytes <= 0) {
             int tmp_byte = dpe_frameCount * sizeof(int16_t) * 2 - outBuffer->frameCount * sizeof(int16_t) * 2;
             memmove((int8_t *)out + tmp_byte, out ,outBuffer->frameCount * sizeof(int16_t) * 2);
             memset((int8_t *)out, 0 , tmp_byte);
        }

        if (pContext->left_process_bytes > 0) {
            memmove((int8_t *)out + pContext->left_process_bytes, out, inBuffer->frameCount * sizeof(int16_t) * 2);
            memcpy((int8_t *)out, pContext->left_process_pBuffer ,pContext->left_process_bytes);
            outBuffer->frameCount += pContext->left_process_bytes / (sizeof(int16_t) * channelCount);
        }

        pContext->left_process_bytes = outBuffer->frameCount * sizeof(int16_t) * 2 - dpe_frameCount * sizeof(int16_t) * 2;

        if (pContext->left_process_bytes > 0) {
            memcpy(pContext->left_process_pBuffer, (int8_t *)out + (dpe_frameCount * sizeof(int16_t) * 2), pContext->left_process_bytes);
        }

        outBuffer->frameCount = dpe_frameCount;

       /*
        if (DPE_prop) {
            FILE *dump_fp_out = fopen("/data/audio/audio_dpe_out.pcm", "a+");
            if (dump_fp_out != NULL) {
                fwrite(out, 2, outBuffer->frameCount * 2, dump_fp_out);
                fclose(dump_fp_out);
            }
        }
        */

    } else {
        //do nothing. no effect created yet. warning.
        for (size_t i = 0; i < inBuffer->frameCount; i++) {
            *out++ = *in++;
            *out++ = *in++;
        }
    }
    return 0;
}



//helper function
bool DP_checkSizesInt(uint32_t paramSize, uint32_t valueSize, uint32_t expectedParams,
        uint32_t expectedValues) {
    if (paramSize < expectedParams * sizeof(int32_t)) {
        ALOGE("Invalid paramSize: %u expected %u", paramSize,
                (uint32_t)(expectedParams * sizeof(int32_t)));
        return false;
    }
    if (valueSize < expectedValues * sizeof(int32_t)) {
        ALOGE("Invalid valueSize %u expected %u", valueSize,
                (uint32_t)(expectedValues * sizeof(int32_t)));
        return false;
    }
    return true;
}

static dp_fx::DPChannel* DP_getChannel(DynamicsProcessingContext *pContext,
        int32_t channel) {
    if (pContext->mPDynamics == NULL) {
        return NULL;
    }
    dp_fx::DPChannel *pChannel = pContext->mPDynamics->getChannel(channel);
    ALOGE_IF(pChannel == NULL, "DPChannel NULL. invalid channel %d", channel);
    return pChannel;
}

static dp_fx::DPEq* DP_getEq(DynamicsProcessingContext *pContext, int32_t channel,
        int32_t eqType) {
    dp_fx::DPChannel *pChannel = DP_getChannel(pContext, channel);
    if (pChannel == NULL) {
        return NULL;
    }
    dp_fx::DPEq *pEq = (eqType == DP_PARAM_PRE_EQ ? pChannel->getPreEq() :
            (eqType == DP_PARAM_POST_EQ ? pChannel->getPostEq() : NULL));
    ALOGE_IF(pEq == NULL,"DPEq NULL invalid eq");
    return pEq;
}

static dp_fx::DPEqBand* DP_getEqBand(DynamicsProcessingContext *pContext, int32_t channel,
        int32_t eqType, int32_t band) {
    dp_fx::DPEq *pEq = DP_getEq(pContext, channel, eqType);
    if (pEq == NULL) {
        return NULL;
    }
    dp_fx::DPEqBand *pEqBand = pEq->getBand(band);
    ALOGE_IF(pEqBand == NULL, "DPEqBand NULL. invalid band %d", band);
    return pEqBand;
}

static dp_fx::DPMbc* DP_getMbc(DynamicsProcessingContext *pContext, int32_t channel) {
    dp_fx::DPChannel * pChannel = DP_getChannel(pContext, channel);
    if (pChannel == NULL) {
        return NULL;
    }
    dp_fx::DPMbc *pMbc = pChannel->getMbc();
    ALOGE_IF(pMbc == NULL, "DPMbc NULL invalid MBC");
    return pMbc;
}

static dp_fx::DPMbcBand* DP_getMbcBand(DynamicsProcessingContext *pContext, int32_t channel,
        int32_t band) {
    dp_fx::DPMbc *pMbc = DP_getMbc(pContext, channel);
    if (pMbc == NULL) {
        return NULL;
    }
    dp_fx::DPMbcBand *pMbcBand = pMbc->getBand(band);
    ALOGE_IF(pMbcBand == NULL, "pMbcBand NULL. invalid band %d", band);
    return pMbcBand;
}

int DP_command(effect_handle_t self, uint32_t cmdCode, uint32_t cmdSize,
        void *pCmdData, uint32_t *replySize, void *pReplyData) {

    DynamicsProcessingContext * pContext = (DynamicsProcessingContext *)self;

    if (pContext == NULL || pContext->mState == DYNAMICS_PROCESSING_STATE_UNINITIALIZED) {
        ALOGE("DP_command() called with NULL context or uninitialized state.");
        return -EINVAL;
    }

    ALOGV("DP_command command %d cmdSize %d",cmdCode, cmdSize);
    switch (cmdCode) {
    case EFFECT_CMD_INIT:
        if (pReplyData == NULL || *replySize != sizeof(int)) {
            ALOGE("EFFECT_CMD_INIT wrong replyData or repySize");
            return -EINVAL;
        }
        *(int *) pReplyData = DP_init(pContext);
        break;
    case EFFECT_CMD_SET_CONFIG:
        if (pCmdData == NULL || cmdSize != sizeof(effect_config_t)
                || pReplyData == NULL || replySize == NULL || *replySize != sizeof(int)) {
            ALOGE("EFFECT_CMD_SET_CONFIG error with pCmdData, cmdSize, pReplyData or replySize");
            return -EINVAL;
        }
        *(int *) pReplyData = DP_setConfig(pContext,
                (effect_config_t *) pCmdData);
        break;
    case EFFECT_CMD_GET_CONFIG:
        if (pReplyData == NULL ||
            *replySize != sizeof(effect_config_t)) {
            ALOGE("EFFECT_CMD_GET_CONFIG wrong replyData or repySize");
            return -EINVAL;
        }
        DP_getConfig(pContext, (effect_config_t *)pReplyData);
        break;
    case EFFECT_CMD_RESET:
        DP_reset(pContext);
        break;
    case EFFECT_CMD_ENABLE:
        if (pReplyData == NULL || replySize == NULL || *replySize != sizeof(int)) {
            ALOGE("EFFECT_CMD_ENABLE wrong replyData or repySize");
            return -EINVAL;
        }
        if (pContext->mState != DYNAMICS_PROCESSING_STATE_INITIALIZED) {
            ALOGE("EFFECT_CMD_ENABLE state not initialized");
            return -ENOSYS;
        }
        pContext->mState = DYNAMICS_PROCESSING_STATE_ACTIVE;
        ALOGV("EFFECT_CMD_ENABLE() OK");
        *(int *)pReplyData = 0;
        break;
    case EFFECT_CMD_DISABLE:
        if (pReplyData == NULL || replySize == NULL || *replySize != sizeof(int)) {
            ALOGE("EFFECT_CMD_DISABLE wrong replyData or repySize");
            return -EINVAL;
        }
        if (pContext->mState != DYNAMICS_PROCESSING_STATE_ACTIVE) {
            ALOGE("EFFECT_CMD_DISABLE state not active");
            return -ENOSYS;
        }
        pContext->mState = DYNAMICS_PROCESSING_STATE_INITIALIZED;
        ALOGV("EFFECT_CMD_DISABLE() OK");
        *(int *)pReplyData = 0;
        break;
    case EFFECT_CMD_GET_PARAM: {
        if (pCmdData == NULL || pReplyData == NULL || replySize == NULL) {
            ALOGE("null pCmdData or pReplyData or replySize");
            return -EINVAL;
        }
        effect_param_t *pEffectParam = (effect_param_t *) pCmdData;
        uint32_t expectedCmdSize = DP_getParameterCmdSize(pEffectParam->psize,
                pEffectParam->data);
        if (cmdSize != expectedCmdSize || *replySize < expectedCmdSize) {
            ALOGE("error cmdSize: %d, expetedCmdSize: %d, replySize: %d",
                    cmdSize, expectedCmdSize, *replySize);
            return -EINVAL;
        }

        ALOGVV("DP_command expectedCmdSize: %d", expectedCmdSize);
        memcpy(pReplyData, pCmdData, expectedCmdSize);
        effect_param_t *p = (effect_param_t *)pReplyData;

        uint32_t voffset = computeParamVOffset(p);

        p->status = DP_getParameter(pContext,
                p->psize,
                p->data,
                &p->vsize,
                p->data + voffset);
        *replySize = sizeof(effect_param_t) + voffset + p->vsize;

        ALOGVV("DP_command replysize %u, status %d" , *replySize, p->status);
        break;
    }
    case EFFECT_CMD_SET_PARAM: {
        if (pCmdData == NULL ||
                cmdSize < (sizeof(effect_param_t) + sizeof(int32_t) + sizeof(int32_t)) ||
                pReplyData == NULL || replySize == NULL || *replySize != sizeof(int32_t)) {
            ALOGE("\tLVM_ERROR : DynamicsProcessing cmdCode Case: "
                    "EFFECT_CMD_SET_PARAM: ERROR");
            return -EINVAL;
        }

        effect_param_t * const p = (effect_param_t *) pCmdData;
        const uint32_t voffset = computeParamVOffset(p);

        *(int *)pReplyData = DP_setParameter(pContext,
                p->psize,
                (void *)p->data,
                p->vsize,
                p->data + voffset);
        break;
    }
    case EFFECT_CMD_SET_VOLUME: {
        ALOGV("EFFECT_CMD_SET_VOLUME");
        // if pReplyData is NULL, VOL_CTRL is delegated to another effect
        if (pReplyData == NULL || replySize == NULL || *replySize < ((int)sizeof(int32_t) * 2)) {
            ALOGV("no VOLUME data to return");
            break;
        }
        if (pCmdData == NULL || cmdSize < ((int)sizeof(uint32_t) * 2)) {
            ALOGE("\tLVM_ERROR : DynamicsProcessing EFFECT_CMD_SET_VOLUME ERROR");
            return -EINVAL;
        }

        const int32_t unityGain = 1 << 24;
        //channel count
        int32_t channelCount = (int32_t)audio_channel_count_from_out_mask(
                pContext->mConfig.inputCfg.channels);
        for (int32_t ch = 0; ch < channelCount; ch++) {

            dp_fx::DPChannel * pChannel = DP_getChannel(pContext, ch);
            if (pChannel == NULL) {
                ALOGE("%s EFFECT_CMD_SET_VOLUME invalid channel %d", __func__, ch);
                return -EINVAL;
                break;
            }

            int32_t offset = ch;
            if (ch > 1) {
                // FIXME: limited to 2 unique channels. If more channels present, use value for
                // first channel
                offset = 0;
            }
            const float gain = (float)*((uint32_t *)pCmdData + offset) / unityGain;
            const float gainDb = linearToDb(gain);
            ALOGVV("%s EFFECT_CMD_SET_VOLUME channel %d, engine outputlevel %f (%0.2f dB)",
                    __func__, ch, gain, gainDb);
            pChannel->setOutputGain(gainDb);
        }

        const int32_t  volRet[2] = {unityGain, unityGain}; // Apply no volume before effect.
        memcpy(pReplyData, volRet, sizeof(volRet));
        break;
    }
    case EFFECT_CMD_OFFLOAD:
        *(int *)pReplyData = 0;
        break;
    case EFFECT_CMD_SET_DEVICE:
    case EFFECT_CMD_SET_AUDIO_MODE:
        break;

    default:
        ALOGW("DP_command invalid command %d",cmdCode);
        return -EINVAL;
    }

    return 0;
}

//register expected cmd size
int DP_getParameterCmdSize(uint32_t paramSize,
        void *pParam) {
    if (paramSize < sizeof(int32_t)) {
        return 0;
    }
    int32_t param = *(int32_t*)pParam;
    switch (param) {
    case DP_PARAM_GET_CHANNEL_COUNT: //param cmd
    case DP_PARAM_ENGINE_ARCHITECTURE:
        //effect + param
        return (int)(sizeof(effect_param_t) + sizeof(uint32_t));
    case DP_PARAM_INPUT_GAIN: //param cmd + param
    case DP_PARAM_LIMITER:
    case DP_PARAM_PRE_EQ:
    case DP_PARAM_POST_EQ:
    case DP_PARAM_MBC:
        //effect + param
        return (int)(sizeof(effect_param_t) + 2 * sizeof(uint32_t));
    case DP_PARAM_PRE_EQ_BAND:
    case DP_PARAM_POST_EQ_BAND:
    case DP_PARAM_MBC_BAND:
        return (int)(sizeof(effect_param_t) + 3 * sizeof(uint32_t));
    }
    return 0;
}

int DP_getParameter(DynamicsProcessingContext *pContext,
                           uint32_t paramSize,
                           void *pParam,
                           uint32_t *pValueSize,
                           void *pValue) {
    int status = 0;
    int32_t *params = (int32_t *)pParam;
    static_assert(sizeof(float) == sizeof(int32_t) && sizeof(float) == sizeof(value_t) &&
            alignof(float) == alignof(int32_t) && alignof(float) == alignof(value_t),
            "Size/alignment mismatch for float/int32_t/value_t");
    value_t *values = reinterpret_cast<value_t*>(pValue);

    ALOGVV("%s start", __func__);
#ifdef VERY_VERY_VERBOSE_LOGGING
    for (size_t i = 0; i < paramSize/sizeof(int32_t); i++) {
        ALOGVV("Param[%zu] %d", i, params[i]);
    }
#endif
    if (paramSize < sizeof(int32_t)) {
        ALOGE("%s invalid paramSize: %u", __func__, paramSize);
        return -EINVAL;
    }
    const int32_t command = params[0];
    switch (command) {
    case DP_PARAM_GET_CHANNEL_COUNT: {
        if (!DP_checkSizesInt(paramSize,*pValueSize, 1 /*params*/, 1 /*values*/)) {
            ALOGE("%s DP_PARAM_GET_CHANNEL_COUNT (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }
        *pValueSize = sizeof(uint32_t);
        *(uint32_t *)pValue = (uint32_t)audio_channel_count_from_out_mask(
                pContext->mConfig.inputCfg.channels);
        ALOGVV("%s DP_PARAM_GET_CHANNEL_COUNT channels %d", __func__, *(int32_t *)pValue);
        break;
    }
    case DP_PARAM_ENGINE_ARCHITECTURE: {
        ALOGVV("engine architecture param size: %d value size %d",paramSize, *pValueSize);
        if (!DP_checkSizesInt(paramSize, *pValueSize, 1 /*params*/, 9 /*values*/)) {
            ALOGE("%s DP_PARAM_ENGINE_ARCHITECTURE (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }
//        Number[] params = { PARAM_ENGINE_ARCHITECTURE };
//        Number[] values = { 0 /*0 variant */,
//                0.0f /* 1 preferredFrameDuration */,
//                0 /*2 preEqInUse */,
//                0 /*3 preEqBandCount */,
//                0 /*4 mbcInUse */,
//                0 /*5 mbcBandCount*/,
//                0 /*6 postEqInUse */,
//                0 /*7 postEqBandCount */,
//                0 /*8 limiterInUse */};
        if (pContext->mPDynamics == NULL) {
            ALOGE("%s DP_PARAM_ENGINE_ARCHITECTURE error mPDynamics is NULL", __func__);
            status = -EINVAL;
            break;
        }
        values[0].i = pContext->mCurrentVariant;
        values[1].f = pContext->mPreferredFrameDuration;
        values[2].i = pContext->mPDynamics->isPreEQInUse();
        values[3].i = pContext->mPDynamics->getPreEqBandCount();
        values[4].i = pContext->mPDynamics->isMbcInUse();
        values[5].i = pContext->mPDynamics->getMbcBandCount();
        values[6].i = pContext->mPDynamics->isPostEqInUse();
        values[7].i = pContext->mPDynamics->getPostEqBandCount();
        values[8].i = pContext->mPDynamics->isLimiterInUse();

        *pValueSize = sizeof(value_t) * 9;

        ALOGVV(" variant %d, preferredFrameDuration: %f, preEqInUse %d, bands %d, mbcInUse %d,"
                "mbc bands %d, postEqInUse %d, bands %d, limiterInUse %d",
                values[0].i, values[1].f, values[2].i, values[3].i, values[4].i, values[5].i,
                values[6].i, values[7].i, values[8].i);
        break;
    }
    case DP_PARAM_INPUT_GAIN: {
        ALOGVV("engine get PARAM_INPUT_GAIN param size: %d value size %d",paramSize, *pValueSize);
        if (!DP_checkSizesInt(paramSize, *pValueSize, 2 /*params*/, 1 /*values*/)) {
            ALOGE("%s get PARAM_INPUT_GAIN invalid sizes.", __func__);
            status = -EINVAL;
            break;
        }

        const int32_t channel = params[1];
        dp_fx::DPChannel * pChannel = DP_getChannel(pContext, channel);
        if (pChannel == NULL) {
            ALOGE("%s get PARAM_INPUT_GAIN invalid channel %d", __func__, channel);
            status = -EINVAL;
            break;
        }
        values[0].f = pChannel->getInputGain();
        *pValueSize = sizeof(value_t) * 1;

        ALOGVV(" channel: %d, input gain %f\n", channel, values[0].f);
        break;
    }
    case DP_PARAM_PRE_EQ:
    case DP_PARAM_POST_EQ: {
        ALOGVV("engine get PARAM_*_EQ param size: %d value size %d",paramSize, *pValueSize);
        if (!DP_checkSizesInt(paramSize, *pValueSize, 2 /*params*/, 3 /*values*/)) {
            ALOGE("%s get PARAM_*_EQ (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }
//        Number[] params = {paramSet == PARAM_PRE_EQ ? PARAM_PRE_EQ : PARAM_POST_EQ,
//                       channelIndex};
//               Number[] values = {0 /*0 in use */,
//                                   0 /*1 enabled*/,
//                                   0 /*2 band count */};
        const int32_t channel = params[1];

        dp_fx::DPEq *pEq = DP_getEq(pContext, channel, command);
        if (pEq == NULL) {
            ALOGE("%s get PARAM_*_EQ invalid eq", __func__);
            status = -EINVAL;
            break;
        }
        values[0].i = pEq->isInUse();
        values[1].i = pEq->isEnabled();
        values[2].i = pEq->getBandCount();
        *pValueSize = sizeof(value_t) * 3;

        ALOGVV(" %s channel: %d, inUse::%d, enabled:%d, bandCount:%d\n",
                (command == DP_PARAM_PRE_EQ ? "preEq" : "postEq"), channel,
                values[0].i, values[1].i, values[2].i);
        break;
    }
    case DP_PARAM_PRE_EQ_BAND:
    case DP_PARAM_POST_EQ_BAND: {
        ALOGVV("engine get PARAM_*_EQ_BAND param size: %d value size %d",paramSize, *pValueSize);
        if (!DP_checkSizesInt(paramSize, *pValueSize, 3 /*params*/, 3 /*values*/)) {
            ALOGE("%s get PARAM_*_EQ_BAND (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }
//        Number[] params = {paramSet,
//                channelIndex,
//                bandIndex};
//        Number[] values = {(eqBand.isEnabled() ? 1 : 0),
//              eqBand.getCutoffFrequency(),
//              eqBand.getGain()};
        const int32_t channel = params[1];
        const int32_t band = params[2];
        int eqCommand = (command == DP_PARAM_PRE_EQ_BAND ? DP_PARAM_PRE_EQ : DP_PARAM_POST_EQ);

        dp_fx::DPEqBand *pEqBand = DP_getEqBand(pContext, channel, eqCommand, band);
        if (pEqBand == NULL) {
            ALOGE("%s get PARAM_*_EQ_BAND invalid channel %d or band %d", __func__, channel, band);
            status = -EINVAL;
            break;
        }

        values[0].i = pEqBand->isEnabled();
        values[1].f = pEqBand->getCutoffFrequency();
        values[2].f = pEqBand->getGain();
        *pValueSize = sizeof(value_t) * 3;

        ALOGVV("%s channel: %d, band::%d, enabled:%d, cutoffFrequency:%f, gain%f\n",
                (command == DP_PARAM_PRE_EQ_BAND ? "preEqBand" : "postEqBand"), channel, band,
                values[0].i, values[1].f, values[2].f);
        break;
    }
    case DP_PARAM_MBC: {
        ALOGVV("engine get PDP_PARAM_MBC param size: %d value size %d",paramSize, *pValueSize);
        if (!DP_checkSizesInt(paramSize, *pValueSize, 2 /*params*/, 3 /*values*/)) {
            ALOGE("%s get PDP_PARAM_MBC (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }

//           Number[] params = {PARAM_MBC,
//                    channelIndex};
//            Number[] values = {0 /*0 in use */,
//                                0 /*1 enabled*/,
//                                0 /*2 band count */};

        const int32_t channel = params[1];

        dp_fx::DPMbc *pMbc = DP_getMbc(pContext, channel);
        if (pMbc == NULL) {
            ALOGE("%s get PDP_PARAM_MBC invalid MBC", __func__);
            status = -EINVAL;
            break;
        }

        values[0].i = pMbc->isInUse();
        values[1].i = pMbc->isEnabled();
        values[2].i = pMbc->getBandCount();
        *pValueSize = sizeof(value_t) * 3;

        ALOGVV("DP_PARAM_MBC channel: %d, inUse::%d, enabled:%d, bandCount:%d\n", channel,
                values[0].i, values[1].i, values[2].i);
        break;
    }
    case DP_PARAM_MBC_BAND: {
        ALOGVV("engine get DP_PARAM_MBC_BAND param size: %d value size %d",paramSize, *pValueSize);
        if (!DP_checkSizesInt(paramSize, *pValueSize, 3 /*params*/, 11 /*values*/)) {
            ALOGE("%s get DP_PARAM_MBC_BAND (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }
//        Number[] params = {PARAM_MBC_BAND,
//                        channelIndex,
//                        bandIndex};
//                Number[] values = {0 /*0 enabled */,
//                        0.0f /*1 cutoffFrequency */,
//                        0.0f /*2 AttackTime */,
//                        0.0f /*3 ReleaseTime */,
//                        0.0f /*4 Ratio */,
//                        0.0f /*5 Threshold */,
//                        0.0f /*6 KneeWidth */,
//                        0.0f /*7 NoiseGateThreshold */,
//                        0.0f /*8 ExpanderRatio */,
//                        0.0f /*9 PreGain */,
//                        0.0f /*10 PostGain*/};

        const int32_t channel = params[1];
        const int32_t band = params[2];

        dp_fx::DPMbcBand *pMbcBand = DP_getMbcBand(pContext, channel, band);
        if (pMbcBand == NULL) {
            ALOGE("%s get PARAM_MBC_BAND invalid channel %d or band %d", __func__, channel, band);
            status = -EINVAL;
            break;
        }

        values[0].i = pMbcBand->isEnabled();
        values[1].f = pMbcBand->getCutoffFrequency();
        values[2].f = pMbcBand->getAttackTime();
        values[3].f = pMbcBand->getReleaseTime();
        values[4].f = pMbcBand->getRatio();
        values[5].f = pMbcBand->getThreshold();
        values[6].f = pMbcBand->getKneeWidth();
        values[7].f = pMbcBand->getNoiseGateThreshold();
        values[8].f = pMbcBand->getExpanderRatio();
        values[9].f = pMbcBand->getPreGain();
        values[10].f = pMbcBand->getPostGain();

        *pValueSize = sizeof(value_t) * 11;
        ALOGVV(" mbcBand channel: %d, band::%d, enabled:%d, cutoffFrequency:%f, attackTime:%f,"
                "releaseTime:%f, ratio:%f, threshold:%f, kneeWidth:%f, noiseGateThreshold:%f,"
                "expanderRatio:%f, preGain:%f, postGain:%f\n", channel, band, values[0].i,
                values[1].f, values[2].f, values[3].f, values[4].f, values[5].f, values[6].f,
                values[7].f, values[8].f, values[9].f, values[10].f);
        break;
    }
    case DP_PARAM_LIMITER: {
        ALOGVV("engine get DP_PARAM_LIMITER param size: %d value size %d",paramSize, *pValueSize);
        if (!DP_checkSizesInt(paramSize, *pValueSize, 2 /*params*/, 8 /*values*/)) {
            ALOGE("%s DP_PARAM_LIMITER (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }

        int32_t channel = params[1];
//      Number[] values = {0 /*0 in use (int)*/,
//              0 /*1 enabled (int)*/,
//              0 /*2 link group (int)*/,
//              0.0f /*3 attack time (float)*/,
//              0.0f /*4 release time (float)*/,
//              0.0f /*5 ratio (float)*/,
//              0.0f /*6 threshold (float)*/,
//              0.0f /*7 post gain(float)*/};
        dp_fx::DPChannel * pChannel = DP_getChannel(pContext, channel);
        if (pChannel == NULL) {
            ALOGE("%s DP_PARAM_LIMITER invalid channel %d", __func__, channel);
            status = -EINVAL;
            break;
        }
        dp_fx::DPLimiter *pLimiter = pChannel->getLimiter();
        if (pLimiter == NULL) {
            ALOGE("%s DP_PARAM_LIMITER null LIMITER", __func__);
            status = -EINVAL;
            break;
        }
        values[0].i = pLimiter->isInUse();
        values[1].i = pLimiter->isEnabled();
        values[2].i = pLimiter->getLinkGroup();
        values[3].f = pLimiter->getAttackTime();
        values[4].f = pLimiter->getReleaseTime();
        values[5].f = pLimiter->getRatio();
        values[6].f = pLimiter->getThreshold();
        values[7].f = pLimiter->getPostGain();

        *pValueSize = sizeof(value_t) * 8;

        ALOGVV(" Limiter channel: %d, inUse::%d, enabled:%d, linkgroup:%d attackTime:%f,"
                "releaseTime:%f, ratio:%f, threshold:%f, postGain:%f\n",
                channel, values[0].i/*inUse*/, values[1].i/*enabled*/, values[2].i/*linkGroup*/,
                values[3].f/*attackTime*/, values[4].f/*releaseTime*/,
                values[5].f/*ratio*/, values[6].f/*threshold*/,
                values[7].f/*postGain*/);
        break;
    }
    default:
        ALOGE("%s invalid param %d", __func__, params[0]);
        status = -EINVAL;
        break;
    }

    ALOGVV("%s end param: %d, status: %d", __func__, params[0], status);
    return status;
} /* end DP_getParameter */

int DP_setParameter(DynamicsProcessingContext *pContext,
                           uint32_t paramSize,
                           void *pParam,
                           uint32_t valueSize,
                           void *pValue) {
    int status = 0;
    int32_t *params = (int32_t *)pParam;
    static_assert(sizeof(float) == sizeof(int32_t) && sizeof(float) == sizeof(value_t) &&
            alignof(float) == alignof(int32_t) && alignof(float) == alignof(value_t),
            "Size/alignment mismatch for float/int32_t/value_t");
    value_t *values = reinterpret_cast<value_t*>(pValue);

    ALOGV("%s start", __func__);
    if (paramSize < sizeof(int32_t)) {
        ALOGE("%s invalid paramSize: %u", __func__, paramSize);
        return -EINVAL;
    }
    const int32_t command = params[0];
    switch (command) {
    case DP_PARAM_ENGINE_ARCHITECTURE: {
        ALOGV("engine architecture param size: %d value size %d",paramSize, valueSize);
        if (!DP_checkSizesInt(paramSize, valueSize, 1 /*params*/, 9 /*values*/)) {
            ALOGE("%s DP_PARAM_ENGINE_ARCHITECTURE (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }
//        Number[] params = { PARAM_ENGINE_ARCHITECTURE };
//        Number[] values = { variant /* variant */,
//                preferredFrameDuration,
//                (preEqInUse ? 1 : 0),
//                preEqBandCount,
//                (mbcInUse ? 1 : 0),
//                mbcBandCount,
//                (postEqInUse ? 1 : 0),
//                postEqBandCount,
//                (limiterInUse ? 1 : 0)};
        const int32_t variant = values[0].i;
        const float preferredFrameDuration = values[1].f;
        const int32_t preEqInUse = values[2].i;
        const int32_t preEqBandCount = values[3].i;
        const int32_t mbcInUse = values[4].i;
        const int32_t mbcBandCount = values[5].i;
        const int32_t postEqInUse = values[6].i;
        const int32_t postEqBandCount = values[7].i;
        const int32_t limiterInUse = values[8].i;

        ALOGV("preferredFrameDuration : %f", preferredFrameDuration);

        ALOGV("variant %d, preEqInuse %d, bands %d, mbcInUse %d, mbc bands %d, postEqInUse %d,"
                "bands %d, limiterInUse %d", variant, preEqInUse, preEqBandCount, mbcInUse,
                mbcBandCount, postEqInUse, postEqBandCount, limiterInUse);

        //set variant (instantiate effect)
        //initArchitecture for effect
        DP_changeVariant(pContext, variant);
        if (pContext->mPDynamics == NULL) {
            ALOGE("%s DP_PARAM_ENGINE_ARCHITECTURE error setting variant %d", __func__, variant);
            status = -EINVAL;
            break;
        }
        pContext->mPreferredFrameDuration = preferredFrameDuration;
        pContext->mPDynamics->init((uint32_t)audio_channel_count_from_out_mask(
                pContext->mConfig.inputCfg.channels),
                preEqInUse != 0, (uint32_t)preEqBandCount,
                mbcInUse != 0, (uint32_t)mbcBandCount,
                postEqInUse != 0, (uint32_t)postEqBandCount,
                limiterInUse != 0);

        DP_configureVariant(pContext, variant);
        break;
    }
    case DP_PARAM_INPUT_GAIN: {
        ALOGV("engine DP_PARAM_INPUT_GAIN param size: %d value size %d",paramSize, valueSize);
        if (!DP_checkSizesInt(paramSize, valueSize, 2 /*params*/, 1 /*values*/)) {
            ALOGE("%s DP_PARAM_INPUT_GAIN invalid sizes.", __func__);
            status = -EINVAL;
            break;
        }

        const int32_t channel = params[1];
        dp_fx::DPChannel * pChannel = DP_getChannel(pContext, channel);
        if (pChannel == NULL) {
            ALOGE("%s DP_PARAM_INPUT_GAIN invalid channel %d", __func__, channel);
            status = -EINVAL;
            break;
        }
        const float gain = values[0].f;
        ALOGV("%s DP_PARAM_INPUT_GAIN channel %d, level %f", __func__, channel, gain);
        pChannel->setInputGain(gain);
        break;
    }
    case DP_PARAM_PRE_EQ:
    case DP_PARAM_POST_EQ: {
        ALOGV("engine DP_PARAM_*_EQ param size: %d value size %d",paramSize, valueSize);
        if (!DP_checkSizesInt(paramSize, valueSize, 2 /*params*/, 3 /*values*/)) {
            ALOGE("%s DP_PARAM_*_EQ (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }
//        Number[] params = {paramSet,
//                channelIndex};
//        Number[] values = { (eq.isInUse() ? 1 : 0),
//                (eq.isEnabled() ? 1 : 0),
//                bandCount};
        const int32_t channel = params[1];

        const int32_t enabled = values[1].i;
        const int32_t bandCount = values[2].i;
        ALOGV(" %s channel: %d, inUse::%d, enabled:%d, bandCount:%d\n",
                (command == DP_PARAM_PRE_EQ ? "preEq" : "postEq"), channel, values[0].i,
                values[2].i, bandCount);

        dp_fx::DPEq *pEq = DP_getEq(pContext, channel, command);
        if (pEq == NULL) {
            ALOGE("%s set PARAM_*_EQ invalid channel %d or command %d", __func__, channel,
                    command);
            status = -EINVAL;
            break;
        }

        pEq->setEnabled(enabled != 0);
        //fail if bandcount is different? maybe.
        if ((int32_t)pEq->getBandCount() != bandCount) {
            ALOGW("%s warning, trying to set different bandcount from %d to %d", __func__,
                    pEq->getBandCount(), bandCount);
        }
        break;
    }
    case DP_PARAM_PRE_EQ_BAND:
    case DP_PARAM_POST_EQ_BAND: {
        ALOGV("engine set PARAM_*_EQ_BAND param size: %d value size %d",paramSize, valueSize);
        if (!DP_checkSizesInt(paramSize, valueSize, 3 /*params*/, 3 /*values*/)) {
            ALOGE("%s PARAM_*_EQ_BAND (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }
//        Number[] values = { channelIndex,
//                bandIndex,
//                (eqBand.isEnabled() ? 1 : 0),
//                eqBand.getCutoffFrequency(),
//                eqBand.getGain()};

//        Number[] params = {paramSet,
//                channelIndex,
//                bandIndex};
//        Number[] values = {(eqBand.isEnabled() ? 1 : 0),
//              eqBand.getCutoffFrequency(),
//              eqBand.getGain()};

        const int32_t channel = params[1];
        const int32_t band = params[2];

        const int32_t enabled = values[0].i;
        const float cutoffFrequency = values[1].f;
        const float gain = values[2].f;


        ALOGV(" %s channel: %d, band::%d, enabled:%d, cutoffFrequency:%f, gain%f\n",
                (command == DP_PARAM_PRE_EQ_BAND ? "preEqBand" : "postEqBand"), channel, band,
                enabled, cutoffFrequency, gain);

        int eqCommand = (command == DP_PARAM_PRE_EQ_BAND ? DP_PARAM_PRE_EQ : DP_PARAM_POST_EQ);
        dp_fx::DPEq *pEq = DP_getEq(pContext, channel, eqCommand);
        if (pEq == NULL) {
            ALOGE("%s set PARAM_*_EQ_BAND invalid channel %d or command %d", __func__, channel,
                    command);
            status = -EINVAL;
            break;
        }

        dp_fx::DPEqBand eqBand;
        eqBand.init(enabled != 0, cutoffFrequency, gain);
        pEq->setBand(band, eqBand);
        break;
    }
    case DP_PARAM_MBC: {
        ALOGV("engine DP_PARAM_MBC param size: %d value size %d",paramSize, valueSize);
        if (!DP_checkSizesInt(paramSize, valueSize, 2 /*params*/, 3 /*values*/)) {
            ALOGE("%s DP_PARAM_MBC (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }
//            Number[] params = { PARAM_MBC,
//                    channelIndex};
//            Number[] values = {(mbc.isInUse() ? 1 : 0),
//                    (mbc.isEnabled() ? 1 : 0),
//                    bandCount};
        const int32_t channel = params[1];

        const int32_t enabled = values[1].i;
        const int32_t bandCount = values[2].i;
        ALOGV("MBC channel: %d, inUse::%d, enabled:%d, bandCount:%d\n", channel, values[0].i,
                enabled, bandCount);

        dp_fx::DPMbc *pMbc = DP_getMbc(pContext, channel);
        if (pMbc == NULL) {
            ALOGE("%s set DP_PARAM_MBC invalid channel %d ", __func__, channel);
            status = -EINVAL;
            break;
        }

        pMbc->setEnabled(enabled != 0);
        //fail if bandcount is different? maybe.
        if ((int32_t)pMbc->getBandCount() != bandCount) {
            ALOGW("%s warning, trying to set different bandcount from %d to %d", __func__,
                    pMbc->getBandCount(), bandCount);
        }
        break;
    }
    case DP_PARAM_MBC_BAND: {
        ALOGV("engine set DP_PARAM_MBC_BAND param size: %d value size %d ",paramSize, valueSize);
        if (!DP_checkSizesInt(paramSize, valueSize, 3 /*params*/, 11 /*values*/)) {
            ALOGE("%s DP_PARAM_MBC_BAND: (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }
//        Number[] params = { PARAM_MBC_BAND,
//                channelIndex,
//                bandIndex};
//        Number[] values = {(mbcBand.isEnabled() ? 1 : 0),
//                mbcBand.getCutoffFrequency(),
//                mbcBand.getAttackTime(),
//                mbcBand.getReleaseTime(),
//                mbcBand.getRatio(),
//                mbcBand.getThreshold(),
//                mbcBand.getKneeWidth(),
//                mbcBand.getNoiseGateThreshold(),
//                mbcBand.getExpanderRatio(),
//                mbcBand.getPreGain(),
//                mbcBand.getPostGain()};

        const int32_t channel = params[1];
        const int32_t band = params[2];

        const int32_t enabled = values[0].i;
        const float cutoffFrequency = values[1].f;
        const float attackTime = values[2].f;
        const float releaseTime = values[3].f;
        const float ratio = values[4].f;
        const float threshold = values[5].f;
        const float kneeWidth = values[6].f;
        const float noiseGateThreshold = values[7].f;
        const float expanderRatio = values[8].f;
        const float preGain = values[9].f;
        const float postGain = values[10].f;

        ALOGV(" mbcBand channel: %d, band::%d, enabled:%d, cutoffFrequency:%f, attackTime:%f,"
                "releaseTime:%f, ratio:%f, threshold:%f, kneeWidth:%f, noiseGateThreshold:%f,"
                "expanderRatio:%f, preGain:%f, postGain:%f\n",
                channel, band, enabled, cutoffFrequency, attackTime, releaseTime, ratio,
                threshold, kneeWidth, noiseGateThreshold, expanderRatio, preGain, postGain);

        dp_fx::DPMbc *pMbc = DP_getMbc(pContext, channel);
        if (pMbc == NULL) {
            ALOGE("%s set DP_PARAM_MBC_BAND invalid channel %d", __func__, channel);
            status = -EINVAL;
            break;
        }

        dp_fx::DPMbcBand mbcBand;
        mbcBand.init(enabled != 0, cutoffFrequency, attackTime, releaseTime, ratio, threshold,
                kneeWidth, noiseGateThreshold, expanderRatio, preGain, postGain);
        pMbc->setBand(band, mbcBand);
        break;
    }
    case DP_PARAM_LIMITER: {
        ALOGV("engine DP_PARAM_LIMITER param size: %d value size %d",paramSize, valueSize);
        if (!DP_checkSizesInt(paramSize, valueSize, 2 /*params*/, 8 /*values*/)) {
            ALOGE("%s DP_PARAM_LIMITER (cmd %d) invalid sizes.", __func__, command);
            status = -EINVAL;
            break;
        }
//            Number[] params = { PARAM_LIMITER,
//                             channelIndex};
//                     Number[] values = {(limiter.isInUse() ? 1 : 0),
//                             (limiter.isEnabled() ? 1 : 0),
//                             limiter.getLinkGroup(),
//                             limiter.getAttackTime(),
//                             limiter.getReleaseTime(),
//                             limiter.getRatio(),
//                             limiter.getThreshold(),
//                             limiter.getPostGain()};

        const int32_t channel = params[1];

        const int32_t inUse = values[0].i;
        const int32_t enabled = values[1].i;
        const int32_t linkGroup = values[2].i;
        const float attackTime = values[3].f;
        const float releaseTime = values[4].f;
        const float ratio = values[5].f;
        const float threshold = values[6].f;
        const float postGain = values[7].f;

        ALOGV(" Limiter channel: %d, inUse::%d, enabled:%d, linkgroup:%d attackTime:%f,"
                "releaseTime:%f, ratio:%f, threshold:%f, postGain:%f\n", channel, inUse,
                enabled, linkGroup, attackTime, releaseTime, ratio, threshold, postGain);

        dp_fx::DPChannel * pChannel = DP_getChannel(pContext, channel);
        if (pChannel == NULL) {
            ALOGE("%s DP_PARAM_LIMITER invalid channel %d", __func__, channel);
            status = -EINVAL;
            break;
        }
        dp_fx::DPLimiter limiter;
        limiter.init(inUse != 0, enabled != 0, linkGroup, attackTime, releaseTime, ratio,
                threshold, postGain);
        pChannel->setLimiter(limiter);
        break;
    }
    default:
        ALOGE("%s invalid param %d", __func__, params[0]);
        status = -EINVAL;
        break;
    }

    ALOGV("%s end param: %d, status: %d", __func__, params[0], status);
    return status;
} /* end DP_setParameter */

/* Effect Control Interface Implementation: get_descriptor */
int DP_getDescriptor(effect_handle_t self,
        effect_descriptor_t *pDescriptor)
{
    DynamicsProcessingContext * pContext = (DynamicsProcessingContext *) self;

    if (pContext == NULL || pDescriptor == NULL) {
        ALOGE("DP_getDescriptor() invalid param");
        return -EINVAL;
    }

    *pDescriptor = gDPDescriptor;

    return 0;
} /* end DP_getDescriptor */


// effect_handle_t interface implementation for Dynamics Processing effect
const struct effect_interface_s gDPInterface = {
        DP_process,
        DP_command,
        DP_getDescriptor,
        NULL,
};

extern "C" {
// This is the only symbol that needs to be exported
__attribute__ ((visibility ("default")))
audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM = {
    .tag = AUDIO_EFFECT_LIBRARY_TAG,
    .version = EFFECT_LIBRARY_API_VERSION_3_1,
    .name = "DPE",
    .implementor = "DPE Labs",
    .create_effect = DPLib_Create,
    .create_effect_3_1 = DPLib_Create_3_1,
    .release_effect = DPLib_Release,
    .get_descriptor = DPLib_GetDescriptor,
};

}; // extern "C"
