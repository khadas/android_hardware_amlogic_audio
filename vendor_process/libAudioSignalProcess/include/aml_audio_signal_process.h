/*
 * Copyright (C) 2019 Amlogic Corporation.
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
#ifndef AML_AUDIO_SIGNAL_PROCESS_H
#define AML_AUDIO_SIGNAL_PROCESS_H
#ifdef __cplusplus
extern "C" {
#endif

#ifdef HIFIA
#define AML_FRAME_SIZE 256
#elif ARMV8
#define AML_FRAME_SIZE 256
#else
#define AML_FRAME_SIZE 256
#endif

#define AML_DATA_BIT 16
#define AML_SAMPLE_RATE 16000

//ctr label
#define ASP_AUDIO_ENERGY 0

typedef enum ChannelType
{
	MicType = 0,
	RefType
} ChannelType;

typedef struct amlAI_FRONT_PROCESS_CONFIG_S
{
	float f32MicMulValue; //(0.0, 10.0)
	int s32RefDelayMicAlignLength; //[0, SampleRate]
	int s32AdaptDelayAlign; //[0, 1]
	int s32RefreshFreqGap; //[1, 30]
	int s32RefDelayMicRange; //[0, SampleRate]
	int s32Reserved;
} AI_FRONT_PROCESS_CONFIG_S;

typedef struct amlAI_HPF_CONFIG_S
{
	int s32Order; //[1, 3]
	int s32HpfFreq; //(0, 200]
	int s32Reserved;
} AI_HPF_CONFIG_S;

typedef struct amlAI_AEC_PF1_CONFIG_S
{
	int s32BandWidth; //[64, 256]
	int s32RefreshLength; //[0, 20]
	int s32Reserved;
} AI_AEC_PF1_CONFIG_S;

typedef struct amlAI_AEC_PF2_CONFIG_S
{
	int s32Intensity; //[1, 3]
	int s32ComfortNoise; //[0, 1]
	int s32Reserved;
} AI_AEC_PF2_CONFIG_S;

typedef struct amlAI_AEC_CONFIG_S
{
	int s32Mode; //[0, 1]
	int bUsrSingleRefChannelMode; //[0, 1]
	int s32SingleChannelId; //[0, MicChannels)
	int s32EchoPathLength; //[0, SampleRate/2]
	int s32CngMode; //unsupport
	int bPf1Open;	//[0, 1]
	AI_AEC_PF1_CONFIG_S stAecPf1Cfg;
	int bPf2Open;	//[0, 1]
	AI_AEC_PF2_CONFIG_S stAecPf2Cfg;
	int bPf3Open;	//[0, 1]
	int s32Reserved;
} AI_AEC_CONFIG_S;

typedef struct amlAI_DEREVERBERATION_CONFIG_S
{
	int s32StartPoint; //[1, 10]
	int s32PathLength; //[1, 16]
	int s32Reserved;
} AI_DEREVERBERATION_CONFIG_S;

typedef struct amlAI_ANR_CONFIG_S
{
	int s32Mode; //[0, 1]
	int s32NrIntensity; //[0, 50]
	int s32Reserved;
} AI_ANR_CONFIG_S;

typedef struct amlAI_GAIN_CONFIG_S
{
	int s32Mode; //[0, 1]
	int s32FixGainDB;
	int s32AgcGainDB; //[0, +oo]
	int s32AgcGainLevel;
	int s32Reserved;
} AI_GAIN_CONFIG_S;

typedef struct amlAI_NOISE_CONFIG_S
{
	int s32Snr;
	int s32Reserved;
} AI_NOISE_CONFIG_S;

typedef struct amlAI_SPEAKER_CONFIG_S
{
	int s32SampleRate;
	int s32SpeakerChannels;
	int s32FormatDataByte;
	int s32HpfOrder;
	int s32HpfFreq;
	int s32LpfOrder;
	int s32LpfFreq;
	float f32SpeakerMulValue;
	int s32Reserved;
} AI_SPEAKER_CONFIG_S;

typedef struct amlAI_ASP_CONFIG_S
{
	int bHpfOpen; //[0, 1]
	int bAecOpen; //[0, 1]
	int bDereverberationOpen;  //[0, 1]
	int bAnrOpen; //[0, 1]
	int bGainOpen; //[0, 1]
	int bDetecteEnergyOpen; //[0, 1]
	int bNoiseOpen; //[0, 1]
	int bMisoOpen; //[0, 1]
	int bSpeakerOpen; //[0, 1]

	int SampleRate; //8k, 16k
	int FrameSample; //AML_FRAME_SIZE
	int FormatDataByte; //AML_DATA_BIT>>3

	int MicChannels;
	int RefChannels;

	AI_FRONT_PROCESS_CONFIG_S stFrontCfg;
	AI_HPF_CONFIG_S stHpfCfg;
	AI_AEC_CONFIG_S stAecCfg;
	AI_DEREVERBERATION_CONFIG_S stDereverberationCfg;
	AI_ANR_CONFIG_S stAnrCfg;
	AI_GAIN_CONFIG_S stGainCfg;
	AI_NOISE_CONFIG_S stNoiseCfg;

	AI_SPEAKER_CONFIG_S stSpeakerCfg;
	int s32Reserved;
} AI_ASP_CONFIG_S;

void *aml_asp_create(AI_ASP_CONFIG_S *config);
void aml_asp_destroy(void *handle);
int aml_asp_import(void *handle, void *in, int stride, int channel_id, ChannelType channel_type);
int aml_asp_process(void *handle);
int aml_asp_export(void *handle, void *out, int stride, int channel_id);
int aml_asp_ctr_get_value(void *handle, int label, void *value, int length);
int aml_asp_ctr_set_value(void *handle, int label, void *value, int length);
void aml_asp_speaker_process(void *handle, void *data, int size, int bypass);

#ifdef __cplusplus
}
#endif

#endif