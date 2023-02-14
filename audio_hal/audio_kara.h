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

/** Audio-kara (karaoke for line-in+usb-in)
 *
 * This module is designed for H-customer or other BDS projects
 *
 * It support mixing mic input with output stream
 * Output stream playback to HDMI-TX/speaker
 * Its product is amplifier or like karaoke feature
 * It support LINE-in mic or customized USB-in mic
 *
 * Output stream is MS12 continuous output stream, It's 48K,32bit,8ch in general
 * MS12 output task is important base of this module, otherwise we may need
 * support cons nous output by self
 * Customized USB-in mic is 48K,16bit,2ch format
 *
 * The output stream is recorded for echo_reference feature
 *
 * The echo_ref stream also support mixing with general USB-in mic
 *
 * Its key feature is low latency of mixing (from LINE-in to HDMI-TX/speaker)
 * Design goal is 20ms, How to achieve this goal?
 * - well-tuning pcm config (low period_size, consistent intermediated buffer)
 *      - passing output config from audio_kara_open's main_cfg param
 *      - That's why main_cfg's type is pcm_config, instead of aml_pcm_config_t only
 * - CPU affinity, priority, schedule config, isolation & etc
 *      - config MS12 output thread
 * - other way
 *
 */
/** Interface Part
 * adev_set_parameters
 *      output_mix_source="linein","usbin","null"(default)
 *      output_mix_source="usbin";customized_usb_card=[card];customized_usb_device=[device]
 *
 * record general usb mic:  AUDIO_SOURCE_MIC
 * record echo reference:   AUDIO_SOURCE_ECHO_REFERENCE
 * record echo_ref+usb mic: AUDIO_SOURCE_ECHO_REFERENCE
 *                          input_mix_source=usbin
 *
 */
/*

                                          +----------+
                       +-------/-------<--| LINE in  |
                       |                  +----------+
                       |                  +-------------+
                       |  +----/-------<--| CM USB mic1 |
                       |  |               +-------------+
                       V  V
  System               |  |               +-----------------+
  ====>===[1]==========+==+==[2]====+==>==| HDMI TX/speaker |
  HDMI RX                           |     +-----------------+
                                    V
                                    |
                                    |
  ----<---[2+3]---------------------+
  echo_ref[+usb_in]                 |
                                    /
                                    |     +----------+
  ----<---[3]-----------------------+--<--| USB mic2 |
  mic                                     +----------+
 */

#ifndef _AUDIO_KARA_H_
#define _AUDIO_KARA_H_

// for enum pcm_format
#include "tinyalsa/asoundlib.h"
/** use aml_pcm_config_t as basic config struct */
/** other candidates: audio_config from android */
#include "aml_dec_api.h"
/**
   typedef struct aml_pcm_config {
    audio_format_t pcm_format;
    int samplerate;
    int channel;
    int max_out_channels;
   } aml_pcm_config_t;
 */

enum {
    MIX_SRC_NIL = 0, // default
    MIX_SRC_LINEIN = 1,
    MIX_SRC_USBIN = 2,
};

/**
 * @brief open audio_kara with specific mix_src
 *
 * @param mix_src MIX_SRC_NIL,LINEIN,USBIN
 * @param card    line-in's alsa card or usb-in's alsa card
 * @param device  line-in's alsa device or usb-in's alsa device
 * line-in, it may be TDM-A(card=0,device=0)
 * usb-in, it may be USB mic device(card=1,device=0)
 * @param cfg     config of pcm_format, sample rate, channel
 *
 * @return handle of audio_kara
 */
void *audio_kara_open(int mix_src, int card, int device, struct pcm_config *main_cfg);

void audio_kara_close(void *kara);

/**
 * @brief mix stream with line-in/usb-in data
 *
 * input data is original stream ([1] in picture)
 * output data is mixed stream, it may mixed with line-in/usb-in or nothing(nil)
 * mixed stream is [2] in picture
 *
 * @param kara kara instance
 * @param data audio data, it will be mixed from MIX_SRC data
 * @param bytes audio data's size
 *
 * @return mixed audio data's size
 * 0: fail to mix MIX_SRC data
 */
size_t audio_kara_mix(void *kara, void *data, size_t bytes);

/**
 * @brief return kara's mix_src config
 *
 * @param kara kara instance
 *
 * @return mix_src config
 */
int audio_kara_get_src(void *kara);

/** helper function for audio_hw layer */
/**
 * @brief switch kara followed user's config
 *
 * @param stream stream
 */
void check_switch_audio_kara(struct audio_stream_out *stream);

/**
 * @brief check and set kara-related parameters
 *
 * @param dev   audio_hw_device
 * @param parms param
 *
 * @return 0, succ; -1, doesn't find kara-related parameters
 */
int set_param_kara(struct audio_hw_device *dev, struct str_parms *parms);
#endif //  _AUDIO_KARA_H_
