/*
 * Copyright (C) 2010-2010 NXP Software
 * Copyright (C) 2009 The Android Open Source Project
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
#define LOG_TAG "AML_Reverb"
//#define LOG_NDEBUG 0

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <audio_utils/primitives.h>
#include <log/log.h>

#include "EffectReverb.h"
#include "LVREV.h"

/* ---- debug---- */
#define DEFAULT_SAMPLE_RATE 44100

#define ARRAY_SIZE(array) (sizeof (array) / sizeof (array)[0])
#define LVM_ERROR_CHECK(LvmStatus, callingFunc, calledFunc) {\
        if ((LvmStatus) == LVREV_NULLADDRESS) {\
            ALOGV("\tLVREV_ERROR : Parameter error - "\
                    "null pointer returned by %s in %s\n\n\n\n", callingFunc, calledFunc);\
        }\
        if ((LvmStatus) == LVREV_INVALIDNUMSAMPLES) {\
            ALOGV("\tLVREV_ERROR : Parameter error - "\
                    "bad number of samples returned by %s in %s\n\n\n\n", callingFunc, calledFunc);\
        }\
        if ((LvmStatus) == LVREV_OUTOFRANGE) {\
            ALOGV("\tLVREV_ERROR : Parameter error - "\
                    "out of range returned by %s in %s\n", callingFunc, calledFunc);\
        }\
    }

typedef struct _LPFPair_t
{
    int16_t Room_HF;
    int16_t LPF;
} LPFPair_t;

typedef enum
{
    // Parameters below are as defined in OpenSL ES specification for environmental reverb interface
    REVERB_PARAM_ROOM_LEVEL,            // in millibels,    range -6000 to 0
    REVERB_PARAM_ROOM_HF_LEVEL,         // in millibels,    range -4000 to 0
    REVERB_PARAM_DECAY_TIME,            // in milliseconds, range 100 to 20000
    REVERB_PARAM_DECAY_HF_RATIO,        // in permilles,    range 100 to 1000
    REVERB_PARAM_REFLECTIONS_LEVEL,     // in millibels,    range -6000 to 0
    REVERB_PARAM_REFLECTIONS_DELAY,     // in milliseconds, range 0 to 65
    REVERB_PARAM_REVERB_LEVEL,          // in millibels,    range -6000 to 0
    REVERB_PARAM_REVERB_DELAY,          // in milliseconds, range 0 to 65
    REVERB_PARAM_DIFFUSION,             // in permilles,    range 0 to 1000
    REVERB_PARAM_DENSITY,               // in permilles,    range 0 to 1000
    REVERB_PARAM_PROPERTIES,
    REVERB_PARAM_BYPASS
} t_env_reverb_params;

//t_reverb_settings is equal to SLEnvironmentalReverbSettings defined in OpenSL ES specification.
typedef struct s_reverb_settings {
    int16_t     roomLevel;
    int16_t     roomHFLevel;
    uint32_t    decayTime;
    int16_t     decayHFRatio;
    int16_t     reflectionsLevel;
    uint32_t    reflectionsDelay;
    int16_t     reverbLevel;
    uint32_t    reverbDelay;
    int16_t     diffusion;
    int16_t     density;
} t_reverb_settings;

enum {
    FCC_2 = 2,
    FCC_8 = 8,
};

typedef enum
{
    REVERB_PARAM_PRESET
} t_preset_reverb_params;

/************************************************************************************/
/*                                                                                  */
/* Preset definitions                                                               */
/*                                                                                  */
/************************************************************************************/

const static t_reverb_settings sReverbPresets[] = {
        // REVERB_PRESET_NONE: values are unused
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        // REVERB_PRESET_SMALLROOM
        {-400, -600, 1100, 830, -400, 5, 500, 10, 1000, 1000},
        // REVERB_PRESET_MEDIUMROOM
        {-400, -600, 1300, 830, -1000, 20, -200, 20, 1000, 1000},
        // REVERB_PRESET_LARGEROOM
        {-400, -600, 1500, 830, -1600, 5, -1000, 40, 1000, 1000},
        // REVERB_PRESET_MEDIUMHALL
        {-400, -600, 1800, 700, -1300, 15, -800, 30, 1000, 1000},
        // REVERB_PRESET_LARGEHALL
        {-400, -600, 1800, 700, -2000, 30, -1400, 60, 1000, 1000},
        // REVERB_PRESET_PLATE
        {-400, -200, 1300, 900, 0, 2, 0, 10, 1000, 750},
};

#ifdef BUILD_FLOAT
typedef     float               process_buffer_t; // process in float
#else
typedef     int32_t             process_buffer_t; // process in Q4_27
#endif // BUILD_FLOAT

struct ReverbContext{
    LVREV_Handle_t                  hInstance;
    int16_t                         SavedRoomLevel;
    int16_t                         SavedHfLevel;
    int16_t                         SavedDecayTime;
    int16_t                         SavedDecayHfRatio;
    int16_t                         SavedReverbLevel;
    int16_t                         SavedDiffusion;
    int16_t                         SavedDensity;
    bool                            bEnabled;
    LVM_Fs_en                       SampleRate;
    process_buffer_t                *InFrames;
    process_buffer_t                *OutFrames;
    size_t                          bufferSizeIn;
    size_t                          bufferSizeOut;
    bool                            auxiliary;
    bool                            preset;
    uint16_t                        curPreset;
    uint16_t                        nextPreset;
    int                             SamplesToExitCount;
    LVM_INT16                       leftVolume;
    LVM_INT16                       rightVolume;
    LVM_INT16                       prevLeftVolume;
    LVM_INT16                       prevRightVolume;
    int                             volumeMode;
};

enum {
    REVERB_VOLUME_OFF,
    REVERB_VOLUME_FLAT,
    REVERB_VOLUME_RAMP,
};

#define REVERB_DEFAULT_PRESET REVERB_PRESET_NONE

#ifdef BUILD_FLOAT
#define REVERB_SEND_LEVEL   0.75f // 0.75 in 4.12 format
#else
#define REVERB_SEND_LEVEL   (0x0C00) // 0.75 in 4.12 format
#endif
#define REVERB_UNIT_VOLUME  (0x1000) // 1.0 in 4.12 format

//--- local function prototypes
int  Reverb_init            (struct ReverbContext *pContext);
void Reverb_free            (struct ReverbContext *pContext);
int  Reverb_setParameter    (struct ReverbContext *pContext, void *pParam, void *pValue, int vsize);
int  Reverb_getParameter    (struct ReverbContext *pContext,
                             void          *pParam,
                             uint32_t      *pValueSize,
                             void          *pValue);
int Reverb_LoadPreset       (struct ReverbContext   *pContext);
int Reverb_paramValueSize   (int32_t param);

/* local functions */
#define CHECK_ARG(cond) {                     \
    if (!(cond)) {                            \
        ALOGV("\tLVM_ERROR : Invalid argument: "#cond);      \
        return -EINVAL;                       \
    }                                         \
}

//----------------------------------------------------------------------------
// process()
//----------------------------------------------------------------------------
// Purpose:
// Apply the Reverb
//
// Inputs:
//  pIn:        pointer to stereo/mono float or 16 bit input data
//  pOut:       pointer to stereo float or 16 bit output data
//  frameCount: Frames to process
//  pContext:   effect engine context
//  strength    strength to be applied
//
//  Outputs:
//  pOut:       pointer to updated stereo 16 bit output data
//
//----------------------------------------------------------------------------
int process( effect_buffer_t   *pIn,
             effect_buffer_t   *pOut,
             int           frameCount,
             struct ReverbContext *pContext) {

    int channels = 2;
    LVREV_ReturnStatus_en   LvmStatus = LVREV_SUCCESS;              /* Function call status */

    // Check that the input is either mono or stereo
    if (!(channels == 1 || channels == FCC_2) ) {
        ALOGE("\tLVREV_ERROR : process invalid PCM format");
        return -EINVAL;
    }

#ifdef BUILD_FLOAT
    size_t inSize = frameCount * sizeof(process_buffer_t) * channels;
    size_t outSize = frameCount * sizeof(process_buffer_t) * FCC_2;
    if (pContext->InFrames == NULL ||
            pContext->bufferSizeIn < inSize) {
        free(pContext->InFrames);
        pContext->bufferSizeIn = inSize;
        pContext->InFrames = (process_buffer_t *)calloc(1, pContext->bufferSizeIn);
    }
    if (pContext->OutFrames == NULL ||
            pContext->bufferSizeOut < outSize) {
        free(pContext->OutFrames);
        pContext->bufferSizeOut = outSize;
        pContext->OutFrames = (process_buffer_t *)calloc(1, pContext->bufferSizeOut);
    }
#endif

#ifndef NATIVE_FLOAT_BUFFER
    effect_buffer_t * const OutFrames16 = (effect_buffer_t *)pContext->OutFrames;
#endif

    // Check for NULL pointers
    if ((pContext->InFrames == NULL) || (pContext->OutFrames == NULL)) {
        ALOGE("\tLVREV_ERROR : process failed to allocate memory for temporary buffers ");
        return -EINVAL;
    }

    if (pContext->preset && pContext->nextPreset != pContext->curPreset) {
        ALOGI("%s change reverb preset mode from %d to %d",
            __func__, pContext->curPreset, pContext->nextPreset);
        Reverb_LoadPreset(pContext);
    }

    if (pContext->auxiliary) {
#ifdef BUILD_FLOAT
#ifdef NATIVE_FLOAT_BUFFER
        memcpy(pContext->InFrames, pIn, frameCount * channels * sizeof(*pIn));
#else
        memcpy_to_float_from_i16(
                pContext->InFrames, pIn, frameCount * channels);
#endif
#else //no BUILD_FLOAT
        for (int i = 0; i < frameCount * channels; i++) {
            pContext->InFrames[i] = (process_buffer_t)pIn[i]<<8;
        }
#endif
        } else {
        // insert reverb input is always stereo
        for (int i = 0; i < frameCount; i++) {
#ifdef BUILD_FLOAT
#ifdef NATIVE_FLOAT_BUFFER
            pContext->InFrames[2 * i] = (process_buffer_t)pIn[2 * i] * REVERB_SEND_LEVEL;
            pContext->InFrames[2 * i + 1] = (process_buffer_t)pIn[2 * i + 1] * REVERB_SEND_LEVEL;
#else
            pContext->InFrames[2 * i] =
                    (process_buffer_t)pIn[2 * i] * REVERB_SEND_LEVEL / 32768.0f;
            pContext->InFrames[2 * i + 1] =
                    (process_buffer_t)pIn[2 * i + 1] * REVERB_SEND_LEVEL / 32768.0f;
#endif
#else
            pContext->InFrames[2*i] = (pIn[2*i] * REVERB_SEND_LEVEL) >> 4; // <<8 + >>12
            pContext->InFrames[2*i+1] = (pIn[2*i+1] * REVERB_SEND_LEVEL) >> 4; // <<8 + >>12
#endif
        }
    }

    if (pContext->preset && pContext->curPreset == REVERB_PRESET_NONE) {
        memset(pContext->OutFrames, 0,
                frameCount * sizeof(*pContext->OutFrames) * FCC_2); //always stereo here
    } else {
        if (pContext->bEnabled == LVM_FALSE && pContext->SamplesToExitCount > 0) {
            memset(pContext->InFrames, 0,
                    frameCount * sizeof(*pContext->OutFrames) * channels);
            ALOGV("\tZeroing %d samples per frame at the end of call", channels);
        }

        /* Process the samples, producing a stereo output */
        LvmStatus = LVREV_Process(pContext->hInstance,      /* Instance handle */
                                  pContext->InFrames,     /* Input buffer */
                                  pContext->OutFrames,    /* Output buffer */
                                  frameCount);              /* Number of samples to read */
    }

    LVM_ERROR_CHECK(LvmStatus, "LVREV_Process", "process")
    if (LvmStatus != LVREV_SUCCESS) return -EINVAL;

    // Convert to 16 bits
    if (pContext->auxiliary) {
#ifdef BUILD_FLOAT
        // nothing to do here
#ifndef NATIVE_FLOAT_BUFFER
        // pContext->OutFrames and OutFrames16 point to the same buffer
        // make sure the float to int conversion happens in the right order.
        memcpy_to_i16_from_float(OutFrames16, pContext->OutFrames,
                (size_t)frameCount * FCC_2);
#endif
#else
        memcpy_to_i16_from_q4_27(OutFrames16, pContext->OutFrames, (size_t)frameCount * FCC_2);
#endif
    } else {
#ifdef BUILD_FLOAT
#ifdef NATIVE_FLOAT_BUFFER
        for (int i = 0; i < frameCount * FCC_2; i++) { // always stereo here
            // Mix with dry input
            pContext->OutFrames[i] += pIn[i];
        }
#else
        for (int i = 0; i < frameCount * FCC_2; i++) { // always stereo here
            // pOutputBuff and OutFrames16 point to the same buffer
            // make sure the float to int conversion happens in the right order.
            pContext->OutFrames[i] += (process_buffer_t)pIn[i] / 32768.0f;
        }
        memcpy_to_i16_from_float(OutFrames16, pContext->OutFrames,
                (size_t)frameCount * FCC_2);
#endif
#else
        for (int i=0; i < frameCount * FCC_2; i++) { // always stereo here
            OutFrames16[i] = clamp16((pContext->OutFrames[i]>>8) + (process_buffer_t)pIn[i]);
        }
#endif
        // apply volume with ramp if needed
        if ((pContext->leftVolume != pContext->prevLeftVolume ||
                pContext->rightVolume != pContext->prevRightVolume) &&
                pContext->volumeMode == REVERB_VOLUME_RAMP) {
#if defined (BUILD_FLOAT) && defined (NATIVE_FLOAT_BUFFER)
            // FIXME: still using int16 volumes.
            // For reference: REVERB_UNIT_VOLUME  (0x1000) // 1.0 in 4.12 format
            float vl = (float)pContext->prevLeftVolume / 4096;
            float incl = (((float)pContext->leftVolume / 4096) - vl) / frameCount;
            float vr = (float)pContext->prevRightVolume / 4096;
            float incr = (((float)pContext->rightVolume / 4096) - vr) / frameCount;

            for (int i = 0; i < frameCount; i++) {
                pContext->OutFrames[FCC_2 * i] *= vl;
                pContext->OutFrames[FCC_2 * i + 1] *= vr;

                vl += incl;
                vr += incr;
            }
#else
            LVM_INT32 vl = (LVM_INT32)pContext->prevLeftVolume << 16;
            LVM_INT32 incl = (((LVM_INT32)pContext->leftVolume << 16) - vl) / frameCount;
            LVM_INT32 vr = (LVM_INT32)pContext->prevRightVolume << 16;
            LVM_INT32 incr = (((LVM_INT32)pContext->rightVolume << 16) - vr) / frameCount;

            for (int i = 0; i < frameCount; i++) {
                OutFrames16[FCC_2 * i] =
                        clamp16((LVM_INT32)((vl >> 16) * OutFrames16[2*i]) >> 12);
                OutFrames16[FCC_2 * i + 1] =
                        clamp16((LVM_INT32)((vr >> 16) * OutFrames16[2*i+1]) >> 12);

                vl += incl;
                vr += incr;
            }
#endif
            pContext->prevLeftVolume = pContext->leftVolume;
            pContext->prevRightVolume = pContext->rightVolume;
        } else if (pContext->volumeMode != REVERB_VOLUME_OFF) {
            if (pContext->leftVolume != REVERB_UNIT_VOLUME ||
                pContext->rightVolume != REVERB_UNIT_VOLUME) {
                for (int i = 0; i < frameCount; i++) {
#if defined(BUILD_FLOAT) && defined(NATIVE_FLOAT_BUFFER)
                    pContext->OutFrames[FCC_2 * i] *= ((float)pContext->leftVolume / 4096);
                    pContext->OutFrames[FCC_2 * i + 1] *= ((float)pContext->rightVolume / 4096);
#else
                    OutFrames16[FCC_2 * i] =
                            clamp16((LVM_INT32)(pContext->leftVolume * OutFrames16[2*i]) >> 12);
                    OutFrames16[FCC_2 * i + 1] =
                            clamp16((LVM_INT32)(pContext->rightVolume * OutFrames16[2*i+1]) >> 12);
#endif
                }
            }
            pContext->prevLeftVolume = pContext->leftVolume;
            pContext->prevRightVolume = pContext->rightVolume;
            pContext->volumeMode = REVERB_VOLUME_RAMP;
        }
    }

    for (int i = 0; i < frameCount * FCC_2; i++) { // always stereo here
#ifndef NATIVE_FLOAT_BUFFER
        pOut[i] = clamp16((int32_t)pOut[i] + (int32_t)OutFrames16[i]);
#else
        pOut[i] += pContext->OutFrames[i];
#endif
    }

    return 0;
}    /* end process */

//----------------------------------------------------------------------------
// Reverb_free()
//----------------------------------------------------------------------------
// Purpose: Free all memory associated with the Bundle.
//
// Inputs:
//  pContext:   effect engine context
//
// Outputs:
//
//----------------------------------------------------------------------------

void Reverb_free(struct ReverbContext *pContext) {

    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;         /* Function call status */
    LVREV_MemoryTable_st      MemTab;

    /* Free the algorithm memory */
    LvmStatus = LVREV_GetMemoryTable(pContext->hInstance,
                                   &MemTab,
                                   LVM_NULL);

    LVM_ERROR_CHECK(LvmStatus, "LVM_GetMemoryTable", "Reverb_free")

    for (int i=0; i<LVM_NR_MEMORY_REGIONS; i++) {
        if (MemTab.Region[i].Size != 0) {
            if (MemTab.Region[i].pBaseAddress != NULL) {
                ALOGV("\tfree() - START freeing %" PRIu32 " bytes for region %u at %p\n",
                        MemTab.Region[i].Size, i, MemTab.Region[i].pBaseAddress);

                free(MemTab.Region[i].pBaseAddress);

                ALOGV("\tfree() - END   freeing %" PRIu32 " bytes for region %u at %p\n",
                        MemTab.Region[i].Size, i, MemTab.Region[i].pBaseAddress);
            } else {
                ALOGV("\tLVM_ERROR : free() - trying to free with NULL pointer %" PRIu32 " bytes "
                        "for region %u at %p ERROR\n",
                        MemTab.Region[i].Size, i, MemTab.Region[i].pBaseAddress);
            }
        }
    }
}    /* end Reverb_free */

//----------------------------------------------------------------------------
// Reverb_init()
//----------------------------------------------------------------------------
// Purpose: Initialize engine with default configuration
//
// Inputs:
//  pContext:   effect engine context
//
// Outputs:
//
//----------------------------------------------------------------------------

int Reverb_init(struct ReverbContext *pContext) {
    ALOGV("\tReverb_init start");

    CHECK_ARG(pContext != NULL);

    if (pContext->hInstance != NULL) {
        Reverb_free(pContext);
    }

    pContext->leftVolume = REVERB_UNIT_VOLUME;
    pContext->rightVolume = REVERB_UNIT_VOLUME;
    pContext->prevLeftVolume = REVERB_UNIT_VOLUME;
    pContext->prevRightVolume = REVERB_UNIT_VOLUME;
    pContext->volumeMode = REVERB_VOLUME_FLAT;

    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;        /* Function call status */
    LVREV_ControlParams_st    params;                         /* Control Parameters */
    LVREV_InstanceParams_st   InstParams;                     /* Instance parameters */
    LVREV_MemoryTable_st      MemTab;                         /* Memory allocation table */
    bool                      bMallocFailure = LVM_FALSE;

    /* Set the capabilities */
    InstParams.MaxBlockSize  = MAX_CALL_SIZE;
    InstParams.SourceFormat  = LVM_STEREO;          // Max format, could be mono during process
    InstParams.NumDelays     = LVREV_DELAYLINES_4;

    /* Allocate memory, forcing alignment */
    LvmStatus = LVREV_GetMemoryTable(LVM_NULL,
                                  &MemTab,
                                  &InstParams);

    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetMemoryTable", "Reverb_init")
    if (LvmStatus != LVREV_SUCCESS) return -EINVAL;

    ALOGV("\tCreateInstance Succesfully called LVM_GetMemoryTable\n");

    /* Allocate memory */
    for (int i=0; i<LVM_NR_MEMORY_REGIONS; i++) {
        if (MemTab.Region[i].Size != 0) {
            MemTab.Region[i].pBaseAddress = malloc(MemTab.Region[i].Size);

            if (MemTab.Region[i].pBaseAddress == LVM_NULL) {
                ALOGV("\tLVREV_ERROR :Reverb_init CreateInstance Failed to allocate %" PRIu32
                        " bytes for region %u\n", MemTab.Region[i].Size, i );
                bMallocFailure = LVM_TRUE;
            } else {
                ALOGV("\tReverb_init CreateInstance allocate %" PRIu32
                        " bytes for region %u at %p\n",
                        MemTab.Region[i].Size, i, MemTab.Region[i].pBaseAddress);
            }
        }
    }

    /* If one or more of the memory regions failed to allocate, free the regions that were
     * succesfully allocated and return with an error
     */
    if (bMallocFailure == LVM_TRUE) {
        for (int i=0; i<LVM_NR_MEMORY_REGIONS; i++) {
            if (MemTab.Region[i].pBaseAddress == LVM_NULL) {
                ALOGV("\tLVM_ERROR :Reverb_init CreateInstance Failed to allocate %" PRIu32
                        " bytes for region %u - Not freeing\n", MemTab.Region[i].Size, i );
            } else {
                ALOGV("\tLVM_ERROR :Reverb_init CreateInstance Failed: but allocated %" PRIu32
                        " bytes for region %u at %p- free\n",
                        MemTab.Region[i].Size, i, MemTab.Region[i].pBaseAddress);
                free(MemTab.Region[i].pBaseAddress);
            }
        }
        return -EINVAL;
    }
    ALOGV("\tReverb_init CreateInstance Succesfully malloc'd memory\n");

    /* Initialise */
    pContext->hInstance = LVM_NULL;

    /* Init sets the instance handle */
    LvmStatus = LVREV_GetInstanceHandle(&pContext->hInstance,
                                        &MemTab,
                                        &InstParams);

    LVM_ERROR_CHECK(LvmStatus, "LVM_GetInstanceHandle", "Reverb_init")
    if (LvmStatus != LVREV_SUCCESS) return -EINVAL;

    ALOGV("\tReverb_init CreateInstance Succesfully called LVM_GetInstanceHandle\n");

    /* Set the initial process parameters */
    /* General parameters */
    params.OperatingMode  = LVM_MODE_ON;
    params.SampleRate     = LVM_FS_44100;
    pContext->SampleRate  = LVM_FS_44100;

    if (InstParams.SourceFormat == LVM_MONO) {
        params.SourceFormat   = LVM_MONO;
    } else {
        params.SourceFormat   = LVM_STEREO;
    }

    /* Reverb parameters */
    params.Level          = 0;
    params.LPF            = 23999;
    params.HPF            = 50;
    params.T60            = 1490;
    params.Density        = 100;
    params.Damping        = 21;
    params.RoomSize       = 100;

    pContext->SamplesToExitCount = (params.T60 * DEFAULT_SAMPLE_RATE)/1000;

    /* Saved strength is used to return the exact strength that was used in the set to the get
     * because we map the original strength range of 0:1000 to 1:15, and this will avoid
     * quantisation like effect when returning
     */
    pContext->SavedRoomLevel    = -6000;
    pContext->SavedHfLevel      = 0;
    pContext->bEnabled          = LVM_FALSE;
    pContext->SavedDecayTime    = params.T60;
    pContext->SavedDecayHfRatio = params.Damping*20;
    pContext->SavedDensity      = params.RoomSize*10;
    pContext->SavedDiffusion    = params.Density*10;
    pContext->SavedReverbLevel  = -6000;

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance,
                                         &params);

    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "Reverb_init")
    if (LvmStatus != LVREV_SUCCESS) return -EINVAL;

    ALOGV("\tReverb_init CreateInstance Succesfully called LVREV_SetControlParameters\n");
    ALOGV("\tReverb_init End");
    return 0;
}   /* end Reverb_init */

//----------------------------------------------------------------------------
// ReverbConvertLevel()
//----------------------------------------------------------------------------
// Purpose:
// Convert level from OpenSL ES format to LVM format
//
// Inputs:
//  level       level to be applied
//
//----------------------------------------------------------------------------

int16_t ReverbConvertLevel(int16_t level) {
    static int16_t LevelArray[101] =
    {
       -12000, -4000,  -3398,  -3046,  -2796,  -2603,  -2444,  -2310,  -2194,  -2092,
       -2000,  -1918,  -1842,  -1773,  -1708,  -1648,  -1592,  -1540,  -1490,  -1443,
       -1398,  -1356,  -1316,  -1277,  -1240,  -1205,  -1171,  -1138,  -1106,  -1076,
       -1046,  -1018,  -990,   -963,   -938,   -912,   -888,   -864,   -841,   -818,
       -796,   -775,   -754,   -734,   -714,   -694,   -675,   -656,   -638,   -620,
       -603,   -585,   -568,   -552,   -536,   -520,   -504,   -489,   -474,   -459,
       -444,   -430,   -416,   -402,   -388,   -375,   -361,   -348,   -335,   -323,
       -310,   -298,   -286,   -274,   -262,   -250,   -239,   -228,   -216,   -205,
       -194,   -184,   -173,   -162,   -152,   -142,   -132,   -121,   -112,   -102,
       -92,    -82,    -73,    -64,    -54,    -45,    -36,    -27,    -18,    -9,
       0
    };
    int16_t i;

    for (i = 0; i < 101; i++)
    {
       if (level <= LevelArray[i])
           break;
    }
    return i;
}

//----------------------------------------------------------------------------
// ReverbConvertHFLevel()
//----------------------------------------------------------------------------
// Purpose:
// Convert level from OpenSL ES format to LVM format
//
// Inputs:
//  level       level to be applied
//
//----------------------------------------------------------------------------

int16_t ReverbConvertHfLevel(int16_t Hflevel) {
    int16_t i;

    static LPFPair_t LPFArray[97] =
    {   // Limit range to 50 for LVREV parameter range
        {-10000, 50}, { -5000, 50 }, { -4000, 50},  { -3000, 158}, { -2000, 502},
        {-1000, 1666},{ -900, 1897}, { -800, 2169}, { -700, 2496}, { -600, 2895},
        {-500, 3400}, { -400, 4066}, { -300, 5011}, { -200, 6537}, { -100,  9826},
        {-99, 9881 }, { -98, 9937 }, { -97, 9994 }, { -96, 10052}, { -95, 10111},
        {-94, 10171}, { -93, 10231}, { -92, 10293}, { -91, 10356}, { -90, 10419},
        {-89, 10484}, { -88, 10549}, { -87, 10616}, { -86, 10684}, { -85, 10753},
        {-84, 10823}, { -83, 10895}, { -82, 10968}, { -81, 11042}, { -80, 11117},
        {-79, 11194}, { -78, 11272}, { -77, 11352}, { -76, 11433}, { -75, 11516},
        {-74, 11600}, { -73, 11686}, { -72, 11774}, { -71, 11864}, { -70, 11955},
        {-69, 12049}, { -68, 12144}, { -67, 12242}, { -66, 12341}, { -65, 12443},
        {-64, 12548}, { -63, 12654}, { -62, 12763}, { -61, 12875}, { -60, 12990},
        {-59, 13107}, { -58, 13227}, { -57, 13351}, { -56, 13477}, { -55, 13607},
        {-54, 13741}, { -53, 13878}, { -52, 14019}, { -51, 14164}, { -50, 14313},
        {-49, 14467}, { -48, 14626}, { -47, 14789}, { -46, 14958}, { -45, 15132},
        {-44, 15312}, { -43, 15498}, { -42, 15691}, { -41, 15890}, { -40, 16097},
        {-39, 16311}, { -38, 16534}, { -37, 16766}, { -36, 17007}, { -35, 17259},
        {-34, 17521}, { -33, 17795}, { -32, 18081}, { -31, 18381}, { -30, 18696},
        {-29, 19027}, { -28, 19375}, { -27, 19742}, { -26, 20129}, { -25, 20540},
        {-24, 20976}, { -23, 21439}, { -22, 21934}, { -21, 22463}, { -20, 23031},
        {-19, 23643}, { -18, 23999}
    };

    for (i = 0; i < 96; i++)
    {
        if (Hflevel <= LPFArray[i].Room_HF)
            break;
    }
    return LPFArray[i].LPF;
}

//----------------------------------------------------------------------------
// ReverbSetRoomHfLevel()
//----------------------------------------------------------------------------
// Purpose:
// Apply the HF level to the Reverb. Must first be converted to LVM format
//
// Inputs:
//  pContext:   effect engine context
//  level       level to be applied
//
//----------------------------------------------------------------------------

void ReverbSetRoomHfLevel(struct ReverbContext *pContext, int16_t level) {
    //ALOGV("\tReverbSetRoomHfLevel start (%d)", level);

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetRoomHfLevel")
    //ALOGV("\tReverbSetRoomHfLevel Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbSetRoomHfLevel() just Got -> %d\n", ActiveParams.LPF);

    ActiveParams.LPF = ReverbConvertHfLevel(level);

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetRoomHfLevel")
    //ALOGV("\tReverbSetRoomhfLevel() just Set -> %d\n", ActiveParams.LPF);
    pContext->SavedHfLevel = level;
    //ALOGV("\tReverbSetHfRoomLevel end.. saving %d", pContext->SavedHfLevel);
    return;
}

//----------------------------------------------------------------------------
// ReverbGetRoomHfLevel()
//----------------------------------------------------------------------------
// Purpose:
// Get the level applied to the Revervb. Must first be converted to LVM format
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int16_t ReverbGetRoomHfLevel(struct ReverbContext *pContext) {
    int16_t level;
    //ALOGV("\tReverbGetRoomHfLevel start, saved level is %d", pContext->SavedHfLevel);

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetRoomHfLevel")
    //ALOGV("\tReverbGetRoomHfLevel Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbGetRoomHfLevel() just Got -> %d\n", ActiveParams.LPF);

    level = ReverbConvertHfLevel(pContext->SavedHfLevel);

    //ALOGV("\tReverbGetRoomHfLevel() ActiveParams.LPFL %d, pContext->SavedHfLevel: %d, "
    //     "converted level: %d\n", ActiveParams.LPF, pContext->SavedHfLevel, level);

    if ((int16_t)ActiveParams.LPF != level) {
        ALOGV("\tLVM_ERROR : (ignore at start up) ReverbGetRoomHfLevel() has wrong level -> %d %d\n",
               ActiveParams.Level, level);
    }

    //ALOGV("\tReverbGetRoomHfLevel end");
    return pContext->SavedHfLevel;
}

//----------------------------------------------------------------------------
// ReverbSetReverbLevel()
//----------------------------------------------------------------------------
// Purpose:
// Apply the level to the Reverb. Must first be converted to LVM format
//
// Inputs:
//  pContext:   effect engine context
//  level       level to be applied
//
//----------------------------------------------------------------------------

void ReverbSetReverbLevel(struct ReverbContext *pContext, int16_t level) {
    //ALOGV("\n\tReverbSetReverbLevel start (%d)", level);

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */
    LVM_INT32                 CombinedLevel;             // Sum of room and reverb level controls

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetReverbLevel")
    //ALOGV("\tReverbSetReverbLevel Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbSetReverbLevel just Got -> %d\n", ActiveParams.Level);

    // needs to subtract max levels for both RoomLevel and ReverbLevel
    CombinedLevel = (level + pContext->SavedRoomLevel)-LVREV_MAX_REVERB_LEVEL;
    //ALOGV("\tReverbSetReverbLevel() CombinedLevel is %d = %d + %d\n",
    //      CombinedLevel, level, pContext->SavedRoomLevel);

    ActiveParams.Level = ReverbConvertLevel(CombinedLevel);

    //ALOGV("\tReverbSetReverbLevel() Trying to set -> %d\n", ActiveParams.Level);

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetReverbLevel")
    //ALOGV("\tReverbSetReverbLevel() just Set -> %d\n", ActiveParams.Level);

    pContext->SavedReverbLevel = level;
    //ALOGV("\tReverbSetReverbLevel end pContext->SavedReverbLevel is %d\n\n",
    //     pContext->SavedReverbLevel);
    return;
}

//----------------------------------------------------------------------------
// ReverbGetReverbLevel()
//----------------------------------------------------------------------------
// Purpose:
// Get the level applied to the Revervb. Must first be converted to LVM format
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int16_t ReverbGetReverbLevel(struct ReverbContext *pContext) {
    int16_t level;
    //ALOGV("\tReverbGetReverbLevel start");

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */
    LVM_INT32                 CombinedLevel;             // Sum of room and reverb level controls

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetReverbLevel")
    //ALOGV("\tReverbGetReverbLevel Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbGetReverbLevel() just Got -> %d\n", ActiveParams.Level);

    // needs to subtract max levels for both RoomLevel and ReverbLevel
    CombinedLevel = (pContext->SavedReverbLevel + pContext->SavedRoomLevel)-LVREV_MAX_REVERB_LEVEL;

    //ALOGV("\tReverbGetReverbLevel() CombinedLevel is %d = %d + %d\n",
    //CombinedLevel, pContext->SavedReverbLevel, pContext->SavedRoomLevel);
    level = ReverbConvertLevel(CombinedLevel);

    //ALOGV("\tReverbGetReverbLevel(): ActiveParams.Level: %d, pContext->SavedReverbLevel: %d, "
    //"pContext->SavedRoomLevel: %d, CombinedLevel: %d, converted level: %d\n",
    //ActiveParams.Level, pContext->SavedReverbLevel,pContext->SavedRoomLevel, CombinedLevel,level);

    if (ActiveParams.Level != level) {
        ALOGV("\tLVM_ERROR : (ignore at start up) ReverbGetReverbLevel() has wrong level -> %d %d\n",
                ActiveParams.Level, level);
    }

    //ALOGV("\tReverbGetReverbLevel end\n");

    return pContext->SavedReverbLevel;
}

//----------------------------------------------------------------------------
// ReverbSetRoomLevel()
//----------------------------------------------------------------------------
// Purpose:
// Apply the level to the Reverb. Must first be converted to LVM format
//
// Inputs:
//  pContext:   effect engine context
//  level       level to be applied
//
//----------------------------------------------------------------------------

void ReverbSetRoomLevel(struct ReverbContext *pContext, int16_t level) {
    //ALOGV("\tReverbSetRoomLevel start (%d)", level);

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */
    LVM_INT32                 CombinedLevel;             // Sum of room and reverb level controls

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetRoomLevel")
    //ALOGV("\tReverbSetRoomLevel Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbSetRoomLevel() just Got -> %d\n", ActiveParams.Level);

    // needs to subtract max levels for both RoomLevel and ReverbLevel
    CombinedLevel = (level + pContext->SavedReverbLevel)-LVREV_MAX_REVERB_LEVEL;
    ActiveParams.Level = ReverbConvertLevel(CombinedLevel);

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetRoomLevel")
    //ALOGV("\tReverbSetRoomLevel() just Set -> %d\n", ActiveParams.Level);

    pContext->SavedRoomLevel = level;
    //ALOGV("\tReverbSetRoomLevel end");
    return;
}

//----------------------------------------------------------------------------
// ReverbGetRoomLevel()
//----------------------------------------------------------------------------
// Purpose:
// Get the level applied to the Revervb. Must first be converted to LVM format
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int16_t ReverbGetRoomLevel(struct ReverbContext *pContext) {
    int16_t level;
    //ALOGV("\tReverbGetRoomLevel start");

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */
    LVM_INT32                 CombinedLevel;             // Sum of room and reverb level controls

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetRoomLevel")
    //ALOGV("\tReverbGetRoomLevel Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbGetRoomLevel() just Got -> %d\n", ActiveParams.Level);

    // needs to subtract max levels for both RoomLevel and ReverbLevel
    CombinedLevel = (pContext->SavedRoomLevel + pContext->SavedReverbLevel-LVREV_MAX_REVERB_LEVEL);
    level = ReverbConvertLevel(CombinedLevel);

    //ALOGV("\tReverbGetRoomLevel, Level = %d, pContext->SavedRoomLevel = %d, "
    //     "pContext->SavedReverbLevel = %d, CombinedLevel = %d, level = %d",
    //     ActiveParams.Level, pContext->SavedRoomLevel,
    //     pContext->SavedReverbLevel, CombinedLevel, level);

    if (ActiveParams.Level != level) {
        ALOGV("\tLVM_ERROR : (ignore at start up) ReverbGetRoomLevel() has wrong level -> %d %d\n",
              ActiveParams.Level, level);
    }

    //ALOGV("\tReverbGetRoomLevel end");
    return pContext->SavedRoomLevel;
}

//----------------------------------------------------------------------------
// ReverbSetDecayTime()
//----------------------------------------------------------------------------
// Purpose:
// Apply the decay time to the Reverb.
//
// Inputs:
//  pContext:   effect engine context
//  time        decay to be applied
//
//----------------------------------------------------------------------------

void ReverbSetDecayTime(struct ReverbContext *pContext, uint32_t time) {
    //ALOGV("\tReverbSetDecayTime start (%d)", time);

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetDecayTime")
    //ALOGV("\tReverbSetDecayTime Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbSetDecayTime() just Got -> %d\n", ActiveParams.T60);

    if (time <= LVREV_MAX_T60) {
        ActiveParams.T60 = (LVM_UINT16)time;
    }
    else {
        ActiveParams.T60 = LVREV_MAX_T60;
    }

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetDecayTime")
    //ALOGV("\tReverbSetDecayTime() just Set -> %d\n", ActiveParams.T60);

    pContext->SamplesToExitCount = (ActiveParams.T60 * DEFAULT_SAMPLE_RATE)/1000;
    //ALOGV("\tReverbSetDecayTime() just Set SamplesToExitCount-> %d\n",pContext->SamplesToExitCount);
    pContext->SavedDecayTime = (int16_t)time;
    //ALOGV("\tReverbSetDecayTime end");
    return;
}

//----------------------------------------------------------------------------
// ReverbGetDecayTime()
//----------------------------------------------------------------------------
// Purpose:
// Get the decay time applied to the Revervb.
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

uint32_t ReverbGetDecayTime(struct ReverbContext *pContext) {
    //ALOGV("\tReverbGetDecayTime start");

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetDecayTime")
    //ALOGV("\tReverbGetDecayTime Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbGetDecayTime() just Got -> %d\n", ActiveParams.T60);

    if (ActiveParams.T60 != pContext->SavedDecayTime) {
        // This will fail if the decay time is set to more than 7000
        ALOGV("\tLVM_ERROR : ReverbGetDecayTime() has wrong level -> %d %d\n",
         ActiveParams.T60, pContext->SavedDecayTime);
    }

    //ALOGV("\tReverbGetDecayTime end");
    return (uint32_t)ActiveParams.T60;
}

//----------------------------------------------------------------------------
// ReverbSetDecayHfRatio()
//----------------------------------------------------------------------------
// Purpose:
// Apply the HF decay ratio to the Reverb.
//
// Inputs:
//  pContext:   effect engine context
//  ratio       ratio to be applied
//
//----------------------------------------------------------------------------

void ReverbSetDecayHfRatio(struct ReverbContext *pContext, int16_t ratio) {
    //ALOGV("\tReverbSetDecayHfRatioe start (%d)", ratio);

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;   /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetDecayHfRatio")
    //ALOGV("\tReverbSetDecayHfRatio Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbSetDecayHfRatio() just Got -> %d\n", ActiveParams.Damping);

    ActiveParams.Damping = (LVM_INT16)(ratio/20);

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetDecayHfRatio")
    //ALOGV("\tReverbSetDecayHfRatio() just Set -> %d\n", ActiveParams.Damping);

    pContext->SavedDecayHfRatio = ratio;
    //ALOGV("\tReverbSetDecayHfRatio end");
    return;
}

//----------------------------------------------------------------------------
// ReverbGetDecayHfRatio()
//----------------------------------------------------------------------------
// Purpose:
// Get the HF decay ratio applied to the Revervb.
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int32_t ReverbGetDecayHfRatio(struct ReverbContext *pContext) {
    //ALOGV("\tReverbGetDecayHfRatio start");

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;   /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetDecayHfRatio")
    //ALOGV("\tReverbGetDecayHfRatio Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbGetDecayHfRatio() just Got -> %d\n", ActiveParams.Damping);

    if (ActiveParams.Damping != (LVM_INT16)(pContext->SavedDecayHfRatio / 20)) {
        ALOGV("\tLVM_ERROR : ReverbGetDecayHfRatio() has wrong level -> %d %d\n",
         ActiveParams.Damping, pContext->SavedDecayHfRatio);
    }

    //ALOGV("\tReverbGetDecayHfRatio end");
    return pContext->SavedDecayHfRatio;
}

//----------------------------------------------------------------------------
// ReverbSetDiffusion()
//----------------------------------------------------------------------------
// Purpose:
// Apply the diffusion to the Reverb.
//
// Inputs:
//  pContext:   effect engine context
//  level        decay to be applied
//
//----------------------------------------------------------------------------

void ReverbSetDiffusion(struct ReverbContext *pContext, int16_t level) {
    //ALOGV("\tReverbSetDiffusion start (%d)", level);

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetDiffusion")
    //ALOGV("\tReverbSetDiffusion Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbSetDiffusion() just Got -> %d\n", ActiveParams.Density);

    ActiveParams.Density = (LVM_INT16)(level/10);

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetDiffusion")
    //ALOGV("\tReverbSetDiffusion() just Set -> %d\n", ActiveParams.Density);

    pContext->SavedDiffusion = level;
    //ALOGV("\tReverbSetDiffusion end");
    return;
}

//----------------------------------------------------------------------------
// ReverbGetDiffusion()
//----------------------------------------------------------------------------
// Purpose:
// Get the decay time applied to the Revervb.
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int32_t ReverbGetDiffusion(struct ReverbContext *pContext) {
    //ALOGV("\tReverbGetDiffusion start");

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */
    LVM_INT16                 Temp;

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetDiffusion")
    //ALOGV("\tReverbGetDiffusion Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbGetDiffusion just Got -> %d\n", ActiveParams.Density);

    Temp = (LVM_INT16)(pContext->SavedDiffusion/10);

    if (ActiveParams.Density != Temp) {
        ALOGV("\tLVM_ERROR : ReverbGetDiffusion invalid value %d %d", Temp, ActiveParams.Density);
    }

    //ALOGV("\tReverbGetDiffusion end");
    return pContext->SavedDiffusion;
}

//----------------------------------------------------------------------------
// ReverbSetDensity()
//----------------------------------------------------------------------------
// Purpose:
// Apply the density level the Reverb.
//
// Inputs:
//  pContext:   effect engine context
//  level        decay to be applied
//
//----------------------------------------------------------------------------

void ReverbSetDensity(struct ReverbContext *pContext, int16_t level) {
    //ALOGV("\tReverbSetDensity start (%d)", level);

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetDensity")
    //ALOGV("\tReverbSetDensity Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbSetDensity just Got -> %d\n", ActiveParams.RoomSize);

    ActiveParams.RoomSize = (LVM_INT16)(((level * 99) / 1000) + 1);

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetDensity")
    //ALOGV("\tReverbSetDensity just Set -> %d\n", ActiveParams.RoomSize);

    pContext->SavedDensity = level;
    //ALOGV("\tReverbSetDensity end");
    return;
}

//----------------------------------------------------------------------------
// ReverbGetDensity()
//----------------------------------------------------------------------------
// Purpose:
// Get the density level applied to the Revervb.
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int32_t ReverbGetDensity(struct ReverbContext *pContext) {
    //ALOGV("\tReverbGetDensity start");

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */
    LVM_INT16                 Temp;
    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetDensity")
    //ALOGV("\tReverbGetDensity Succesfully returned from LVM_GetControlParameters\n");
    //ALOGV("\tReverbGetDensity() just Got -> %d\n", ActiveParams.RoomSize);


    Temp = (LVM_INT16)(((pContext->SavedDensity * 99) / 1000) + 1);

    if (Temp != ActiveParams.RoomSize) {
        ALOGV("\tLVM_ERROR : ReverbGetDensity invalid value %d %d", Temp, ActiveParams.RoomSize);
    }

    //ALOGV("\tReverbGetDensity end");
    return pContext->SavedDensity;
}

//----------------------------------------------------------------------------
// Reverb_LoadPreset()
//----------------------------------------------------------------------------
// Purpose:
// Load a the next preset
//
// Inputs:
//  pContext         - handle to instance data
//
// Outputs:
//
// Side Effects:
//
//----------------------------------------------------------------------------
int Reverb_LoadPreset(struct ReverbContext *pContext)
{
    //TODO: add reflections delay, level and reverb delay when early reflections are
    // implemented
    pContext->curPreset = pContext->nextPreset;

    if (pContext->curPreset != REVERB_PRESET_NONE) {
        const t_reverb_settings *preset = &sReverbPresets[pContext->curPreset];
        ReverbSetRoomLevel(pContext, preset->roomLevel);
        ReverbSetRoomHfLevel(pContext, preset->roomHFLevel);
        ReverbSetDecayTime(pContext, preset->decayTime);
        ReverbSetDecayHfRatio(pContext, preset->decayHFRatio);
        //reflectionsLevel
        //reflectionsDelay
        ReverbSetReverbLevel(pContext, preset->reverbLevel);
        // reverbDelay
        ReverbSetDiffusion(pContext, preset->diffusion);
        ReverbSetDensity(pContext, preset->density);
    }

    return 0;
}


//----------------------------------------------------------------------------
// Reverb_getParameter()
//----------------------------------------------------------------------------
// Purpose:
// Get a Reverb parameter
//
// Inputs:
//  pContext         - handle to instance data
//  pParam           - pointer to parameter
//  pValue           - pointer to variable to hold retrieved value
//  pValueSize       - pointer to value size: maximum size as input
//
// Outputs:
//  *pValue updated with parameter value
//  *pValueSize updated with actual value size
//
//
// Side Effects:
//
//----------------------------------------------------------------------------

int Reverb_getParameter(struct ReverbContext *pContext,
                        void          *pParam,
                        uint32_t      *pValueSize,
                        void          *pValue) {
    int status = 0;
    int32_t *pParamTemp = (int32_t *)pParam;
    int32_t param = *pParamTemp++;
    t_reverb_settings *pProperties;

    //ALOGV("\tReverb_getParameter start");
    if (pContext->preset) {
        if (param != REVERB_PARAM_PRESET || *pValueSize < sizeof(uint16_t)) {
            return -EINVAL;
        }

        *(uint16_t *)pValue = pContext->nextPreset;
        ALOGV("get REVERB_PARAM_PRESET, preset %d", pContext->nextPreset);
        return 0;
    }

    switch (param) {
        case REVERB_PARAM_ROOM_LEVEL:
            if (*pValueSize != sizeof(int16_t)) {
                ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid pValueSize1 %d", *pValueSize);
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_ROOM_HF_LEVEL:
            if (*pValueSize != sizeof(int16_t)) {
                ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid pValueSize12 %d", *pValueSize);
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_DECAY_TIME:
            if (*pValueSize != sizeof(uint32_t)) {
                ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid pValueSize3 %d", *pValueSize);
                return -EINVAL;
            }
            *pValueSize = sizeof(uint32_t);
            break;
        case REVERB_PARAM_DECAY_HF_RATIO:
            if (*pValueSize != sizeof(int16_t)) {
                ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid pValueSize4 %d", *pValueSize);
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_REFLECTIONS_LEVEL:
            if (*pValueSize != sizeof(int16_t)) {
                ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid pValueSize5 %d", *pValueSize);
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_REFLECTIONS_DELAY:
            if (*pValueSize != sizeof(uint32_t)) {
                ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid pValueSize6 %d", *pValueSize);
                return -EINVAL;
            }
            *pValueSize = sizeof(uint32_t);
            break;
        case REVERB_PARAM_REVERB_LEVEL:
            if (*pValueSize != sizeof(int16_t)) {
                ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid pValueSize7 %d", *pValueSize);
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_REVERB_DELAY:
            if (*pValueSize != sizeof(uint32_t)) {
                ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid pValueSize8 %d", *pValueSize);
                return -EINVAL;
            }
            *pValueSize = sizeof(uint32_t);
            break;
        case REVERB_PARAM_DIFFUSION:
            if (*pValueSize != sizeof(int16_t)) {
                ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid pValueSize9 %d", *pValueSize);
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_DENSITY:
            if (*pValueSize != sizeof(int16_t)) {
                ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid pValueSize10 %d", *pValueSize);
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_PROPERTIES:
            if (*pValueSize != sizeof(t_reverb_settings)) {
                ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid pValueSize11 %d", *pValueSize);
                return -EINVAL;
            }
            *pValueSize = sizeof(t_reverb_settings);
            break;

        default:
            ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid param %d", param);
            return -EINVAL;
    }

    pProperties = (t_reverb_settings *) pValue;

    switch (param) {
        case REVERB_PARAM_PROPERTIES:
            pProperties->roomLevel = ReverbGetRoomLevel(pContext);
            pProperties->roomHFLevel = ReverbGetRoomHfLevel(pContext);
            pProperties->decayTime = ReverbGetDecayTime(pContext);
            pProperties->decayHFRatio = ReverbGetDecayHfRatio(pContext);
            pProperties->reflectionsLevel = 0;
            pProperties->reflectionsDelay = 0;
            pProperties->reverbDelay = 0;
            pProperties->reverbLevel = ReverbGetReverbLevel(pContext);
            pProperties->diffusion = ReverbGetDiffusion(pContext);
            pProperties->density = ReverbGetDensity(pContext);

            ALOGV("\tReverb_getParameter() REVERB_PARAM_PROPERTIES Value is roomLevel        %d",
                pProperties->roomLevel);
            ALOGV("\tReverb_getParameter() REVERB_PARAM_PROPERTIES Value is roomHFLevel      %d",
                pProperties->roomHFLevel);
            ALOGV("\tReverb_getParameter() REVERB_PARAM_PROPERTIES Value is decayTime        %d",
                pProperties->decayTime);
            ALOGV("\tReverb_getParameter() REVERB_PARAM_PROPERTIES Value is decayHFRatio     %d",
                pProperties->decayHFRatio);
            ALOGV("\tReverb_getParameter() REVERB_PARAM_PROPERTIES Value is reflectionsLevel %d",
                pProperties->reflectionsLevel);
            ALOGV("\tReverb_getParameter() REVERB_PARAM_PROPERTIES Value is reflectionsDelay %d",
                pProperties->reflectionsDelay);
            ALOGV("\tReverb_getParameter() REVERB_PARAM_PROPERTIES Value is reverbDelay      %d",
                pProperties->reverbDelay);
            ALOGV("\tReverb_getParameter() REVERB_PARAM_PROPERTIES Value is reverbLevel      %d",
                pProperties->reverbLevel);
            ALOGV("\tReverb_getParameter() REVERB_PARAM_PROPERTIES Value is diffusion        %d",
                pProperties->diffusion);
            ALOGV("\tReverb_getParameter() REVERB_PARAM_PROPERTIES Value is density          %d",
                pProperties->density);
            break;

        case REVERB_PARAM_ROOM_LEVEL:
            *(int16_t *)pValue = ReverbGetRoomLevel(pContext);

            //ALOGV("\tReverb_getParameter() REVERB_PARAM_ROOM_LEVEL Value is %d",
            //        *(int16_t *)pValue);
            break;
        case REVERB_PARAM_ROOM_HF_LEVEL:
            *(int16_t *)pValue = ReverbGetRoomHfLevel(pContext);

            //ALOGV("\tReverb_getParameter() REVERB_PARAM_ROOM_HF_LEVEL Value is %d",
            //        *(int16_t *)pValue);
            break;
        case REVERB_PARAM_DECAY_TIME:
            *(uint32_t *)pValue = ReverbGetDecayTime(pContext);

            //ALOGV("\tReverb_getParameter() REVERB_PARAM_DECAY_TIME Value is %d",
            //        *(int32_t *)pValue);
            break;
        case REVERB_PARAM_DECAY_HF_RATIO:
            *(int16_t *)pValue = ReverbGetDecayHfRatio(pContext);

            //ALOGV("\tReverb_getParameter() REVERB_PARAM_DECAY_HF_RATION Value is %d",
            //        *(int16_t *)pValue);
            break;
        case REVERB_PARAM_REVERB_LEVEL:
             *(int16_t *)pValue = ReverbGetReverbLevel(pContext);

            //ALOGV("\tReverb_getParameter() REVERB_PARAM_REVERB_LEVEL Value is %d",
            //        *(int16_t *)pValue);
            break;
        case REVERB_PARAM_DIFFUSION:
            *(int16_t *)pValue = ReverbGetDiffusion(pContext);

            //ALOGV("\tReverb_getParameter() REVERB_PARAM_DECAY_DIFFUSION Value is %d",
            //        *(int16_t *)pValue);
            break;
        case REVERB_PARAM_DENSITY:
            *(uint16_t *)pValue = 0;
            *(int16_t *)pValue = ReverbGetDensity(pContext);
            //ALOGV("\tReverb_getParameter() REVERB_PARAM_DENSITY Value is %d",
            //        *(uint32_t *)pValue);
            break;
        case REVERB_PARAM_REFLECTIONS_LEVEL:
            *(uint16_t *)pValue = 0;
        case REVERB_PARAM_REFLECTIONS_DELAY:
            *(uint32_t *)pValue = 0;
        case REVERB_PARAM_REVERB_DELAY:
            *(uint32_t *)pValue = 0;
            break;

        default:
            ALOGV("\tLVM_ERROR : Reverb_getParameter() invalid param %d", param);
            status = -EINVAL;
            break;
    }

    //ALOGV("\tReverb_getParameter end");
    return status;
} /* end Reverb_getParameter */

//----------------------------------------------------------------------------
// Reverb_setParameter()
//----------------------------------------------------------------------------
// Purpose:
// Set a Reverb parameter
//
// Inputs:
//  pContext         - handle to instance data
//  pParam           - pointer to parameter
//  pValue           - pointer to value
//  vsize            - value size
//
// Outputs:
//
//----------------------------------------------------------------------------

int Reverb_setParameter(struct ReverbContext *pContext, void *pParam, void *pValue, int vsize) {
    int status = 0;
    int16_t level;
    int16_t ratio;
    uint32_t time;
    t_reverb_settings *pProperties;
    int32_t *pParamTemp = (int32_t *)pParam;
    int32_t param = *pParamTemp++;

    //ALOGV("\tReverb_setParameter start");
    if (pContext->preset) {
        if (param != REVERB_PARAM_PRESET) {
            return -EINVAL;
        }
        if (vsize < (int)sizeof(uint16_t)) {
            android_errorWriteLog(0x534e4554, "67647856");
            return -EINVAL;
        }

        uint16_t preset = *(uint16_t *)pValue;
        ALOGV("set REVERB_PARAM_PRESET, preset %d", preset);
        if (preset > REVERB_PRESET_LAST) {
            return -EINVAL;
        }
        pContext->nextPreset = preset;
        return 0;
    }

    if (vsize < Reverb_paramValueSize(param)) {
        android_errorWriteLog(0x534e4554, "63526567");
        return -EINVAL;
    }

    switch (param) {
        case REVERB_PARAM_PROPERTIES:
            ALOGV("\tReverb_setParameter() REVERB_PARAM_PROPERTIES");
            pProperties = (t_reverb_settings *) pValue;
            ReverbSetRoomLevel(pContext, pProperties->roomLevel);
            ReverbSetRoomHfLevel(pContext, pProperties->roomHFLevel);
            ReverbSetDecayTime(pContext, pProperties->decayTime);
            ReverbSetDecayHfRatio(pContext, pProperties->decayHFRatio);
            ReverbSetReverbLevel(pContext, pProperties->reverbLevel);
            ReverbSetDiffusion(pContext, pProperties->diffusion);
            ReverbSetDensity(pContext, pProperties->density);
            break;
        case REVERB_PARAM_ROOM_LEVEL:
            level = *(int16_t *)pValue;
            //ALOGV("\tReverb_setParameter() REVERB_PARAM_ROOM_LEVEL value is %d", level);
            //ALOGV("\tReverb_setParameter() Calling ReverbSetRoomLevel");
            ReverbSetRoomLevel(pContext, level);
            //ALOGV("\tReverb_setParameter() Called ReverbSetRoomLevel");
           break;
        case REVERB_PARAM_ROOM_HF_LEVEL:
            level = *(int16_t *)pValue;
            //ALOGV("\tReverb_setParameter() REVERB_PARAM_ROOM_HF_LEVEL value is %d", level);
            //ALOGV("\tReverb_setParameter() Calling ReverbSetRoomHfLevel");
            ReverbSetRoomHfLevel(pContext, level);
            //ALOGV("\tReverb_setParameter() Called ReverbSetRoomHfLevel");
           break;
        case REVERB_PARAM_DECAY_TIME:
            time = *(uint32_t *)pValue;
            //ALOGV("\tReverb_setParameter() REVERB_PARAM_DECAY_TIME value is %d", time);
            //ALOGV("\tReverb_setParameter() Calling ReverbSetDecayTime");
            ReverbSetDecayTime(pContext, time);
            //ALOGV("\tReverb_setParameter() Called ReverbSetDecayTime");
           break;
        case REVERB_PARAM_DECAY_HF_RATIO:
            ratio = *(int16_t *)pValue;
            //ALOGV("\tReverb_setParameter() REVERB_PARAM_DECAY_HF_RATIO value is %d", ratio);
            //ALOGV("\tReverb_setParameter() Calling ReverbSetDecayHfRatio");
            ReverbSetDecayHfRatio(pContext, ratio);
            //ALOGV("\tReverb_setParameter() Called ReverbSetDecayHfRatio");
            break;
         case REVERB_PARAM_REVERB_LEVEL:
            level = *(int16_t *)pValue;
            //ALOGV("\tReverb_setParameter() REVERB_PARAM_REVERB_LEVEL value is %d", level);
            //ALOGV("\tReverb_setParameter() Calling ReverbSetReverbLevel");
            ReverbSetReverbLevel(pContext, level);
            //ALOGV("\tReverb_setParameter() Called ReverbSetReverbLevel");
           break;
        case REVERB_PARAM_DIFFUSION:
            ratio = *(int16_t *)pValue;
            //ALOGV("\tReverb_setParameter() REVERB_PARAM_DECAY_DIFFUSION value is %d", ratio);
            //ALOGV("\tReverb_setParameter() Calling ReverbSetDiffusion");
            ReverbSetDiffusion(pContext, ratio);
            //ALOGV("\tReverb_setParameter() Called ReverbSetDiffusion");
            break;
        case REVERB_PARAM_DENSITY:
            ratio = *(int16_t *)pValue;
            //ALOGV("\tReverb_setParameter() REVERB_PARAM_DECAY_DENSITY value is %d", ratio);
            //ALOGV("\tReverb_setParameter() Calling ReverbSetDensity");
            ReverbSetDensity(pContext, ratio);
            //ALOGV("\tReverb_setParameter() Called ReverbSetDensity");
            break;
           break;
        case REVERB_PARAM_REFLECTIONS_LEVEL:
        case REVERB_PARAM_REFLECTIONS_DELAY:
        case REVERB_PARAM_REVERB_DELAY:
            break;
        default:
            ALOGV("\tLVM_ERROR : Reverb_setParameter() invalid param %d", param);
            break;
    }

    //ALOGV("\tReverb_setParameter end");
    return status;
} /* end Reverb_setParameter */


/**
 * returns the size in bytes of the value of each environmental reverb parameter
 */
int Reverb_paramValueSize(int32_t param) {
    switch (param) {
    case REVERB_PARAM_ROOM_LEVEL:
    case REVERB_PARAM_ROOM_HF_LEVEL:
    case REVERB_PARAM_REFLECTIONS_LEVEL:
    case REVERB_PARAM_REVERB_LEVEL:
        return sizeof(int16_t); // millibel
    case REVERB_PARAM_DECAY_TIME:
    case REVERB_PARAM_REFLECTIONS_DELAY:
    case REVERB_PARAM_REVERB_DELAY:
        return sizeof(uint32_t); // milliseconds
    case REVERB_PARAM_DECAY_HF_RATIO:
    case REVERB_PARAM_DIFFUSION:
    case REVERB_PARAM_DENSITY:
        return sizeof(int16_t); // permille
    case REVERB_PARAM_PROPERTIES:
        return sizeof(t_reverb_settings); // struct of all reverb properties
    }
    return sizeof(int32_t);
}

int ANDROID_Reverb_Init(void **reverb_handle)
{
    struct ReverbContext *pContext;
    int ret;

    pContext = calloc(1, sizeof(struct ReverbContext));
    if (!pContext) {
        ALOGE("%s, malloc error\n", __func__);
        return -EINVAL;
    }
    pContext->hInstance = NULL;
    /* auxiliary or insert*/
    pContext->auxiliary = false;
    /* preset or enviromental*/
    pContext->preset = true;
    pContext->curPreset = REVERB_PRESET_LAST + 1;
    pContext->nextPreset = REVERB_DEFAULT_PRESET;

    ret = Reverb_init(pContext);
    if (ret < 0) {
        ALOGV("%s init failed\n", __func__);
        free(pContext);
        return ret;
    }

    *reverb_handle = (void *)pContext;

    pContext->bufferSizeIn = LVREV_MAX_FRAME_SIZE * sizeof(int32_t) * FCC_2;
    pContext->bufferSizeOut = LVREV_MAX_FRAME_SIZE * sizeof(int32_t) * FCC_2;
    pContext->InFrames  = (int32_t *)calloc(pContext->bufferSizeIn, 1);
    pContext->OutFrames = (int32_t *)calloc(pContext->bufferSizeOut, 1);

    return 0;
}

int ANDROID_Reverb_Process(void *reverb_handle, int16_t *inBuffer, int16_t *outBuffer, int frameCount)
{
    struct ReverbContext *pContext = (struct ReverbContext *)reverb_handle;
    int    status = 0;

    if (pContext == NULL) {
        ALOGV("%s called with NULL pointer", __func__);
        return -EINVAL;
    }

    if (inBuffer == NULL || outBuffer == NULL || frameCount <= 0) {
        ALOGV("%s invalid parameter!", __func__);
        return -EINVAL;
    }

    status = process(inBuffer, outBuffer, frameCount, pContext);

    if (pContext->bEnabled == LVM_FALSE) {
        if (pContext->SamplesToExitCount > 0) {
            // signed - unsigned will trigger integer overflow if result becomes negative.
            ALOGV("%s warning: frameCount = %d, SamplesToExitCount = %d",
                __func__, frameCount, pContext->SamplesToExitCount);
        } else {
            status = -ENODATA;
        }
    }

    return status;
}

int ANDROID_Reverb_Release(void *reverb_handle)
{
    struct ReverbContext *pContext = (struct ReverbContext *)reverb_handle;

    if (pContext == NULL) {
        ALOGV("%s called with NULL pointer", __func__);
        return -EINVAL;
    }

    free(pContext->InFrames);
    free(pContext->OutFrames);
    pContext->bufferSizeIn = 0;
    pContext->bufferSizeOut = 0;
    Reverb_free(pContext);
    free(pContext);

    return 0;
}

void ANDROID_Reverb_Set_Mode(void *reverb_handle, t_reverb_presets mode)
{
    struct ReverbContext *pContext = (struct ReverbContext *)reverb_handle;

    if (mode > REVERB_PRESET_LAST || pContext->nextPreset == mode) {
        return;
    }

    pContext->nextPreset = mode;

    return;
}