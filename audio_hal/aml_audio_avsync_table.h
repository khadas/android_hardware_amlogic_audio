/*
 * Copyright (C) 2020 Amlogic Corporation.
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



#ifndef  _AUDIO_AVSYNC_TABLE_H_
#define _AUDIO_AVSYNC_TABLE_H_

/* we use this table to tune the AV sync case,
 * if the value is big, it can delay video
 * if the value is small, it can advance the video
 */

/*First we need tune CVBS output, then tune HDMI PCM, then other format*/
#define DIFF_DDP_JOC_VS_DDP_LATENCY                     (-60)
#define DDP_FRAME_DEFAULT_DURATION                      (32)
#define AVSYNC_MS12_TUNNEL_DIFF_DDP_JOC_VS_DDP_LATENCY  (DIFF_DDP_JOC_VS_DDP_LATENCY + (DDP_FRAME_DEFAULT_DURATION / 2))

#define AVSYNC_MS12_TUNNEL_VIDEO_DELAY                  (-90)


#define  AVSYNC_MS12_NONTUNNEL_PCM_LATENCY               (10)
#define  AVSYNC_MS12_NONTUNNEL_DDP_LATENCY               (20)
#define  AVSYNC_MS12_NONTUNNEL_ATMOS_LATENCY             (15)
#define  AVSYNC_MS12_TUNNEL_PCM_LATENCY                  (-30)
#define  AVSYNC_MS12_TUNNEL_DD_LATENCY                   (0) //TODO, no case for ac3

/*
 *First version, the value of "vendor.media.audio.hal.ms12.tunnel.ddp" is 90.
 *The CVBS-AVSYNC result about AV-Sync_DDP_JOC_UHD_H265_MP4_50fps.mp4 is +11/+8/+7 ms
 * commit 0d78b1f789ac6178119064dd0370e392e34f228e
 *   audio: MS12 AV sync tuning [1/1]
 *   PD#SWPL-40617
 *But recent version, re-test that AVSYNC item.
 *The CVBS-AVSYNC result about AV-Sync_DDP_JOC_UHD_H265_MP4_50fps.mp4 is (-54) ms
 *so, change the value from 90 to 40ms.
 *The result is about +9ms.
 */
#define  AVSYNC_MS12_TUNNEL_DDP_CVBS_LATENCY                  (40)
/*
 *AVSYNC_MS12_TUNNEL_DDP_HDMI_LATENCY works in HDMI port:
 *      For DDP source under HDMI output,
 *      40->90, the result change from +25ms to -25ms
 */
#define  AVSYNC_MS12_TUNNEL_DDP_HDMI_LATENCY                  (90)
#define  AVSYNC_MS12_TUNNEL_ATMOS_LATENCY                (20)

#define  AVSYNC_MS12_NONTUNNEL_AC4_LATENCY               (70)
#define  AVSYNC_MS12_TUNNEL_AC4_LATENCY                  (50)

#define  AVSYNC_MS12_NONTUNNEL_BYPASS_LATENCY            (-130)

/*
 * -220 -> -250 for result as +35 -> +60
 * -220 -> -180 for result as +35 -> -5
 */
#define  AVSYNC_MS12_TUNNEL_BYPASS_LATENCY               (-180)


#define  AVSYNC_MS12_NETFLIX_NONTUNNEL_BYPASS_LATENCY            (-130)
#define  AVSYNC_MS12_NETFLIX_TUNNEL_BYPASS_LATENCY               (-185)

#define  AVSYNC_MS12_PCM_OUT_LATENCY                     (0)
#define  AVSYNC_MS12_DD_OUT_LATENCY                      (50)
/*
 * 75 -> 35 for result as +20 -> +40
 * 75 -> 115 for result as +20 -> -20
 */
#define  AVSYNC_MS12_DDP_OUT_LATENCY                     (75)

/*
 * 10 -> -20 for result as +20 -> +50
 * 10 -> 30 for result as +20 -> -10
 */
#define  AVSYNC_MS12_MAT_OUT_LATENCY                     (30)

#define  AVSYNC_MS12_HDMI_ARC_OUT_PCM_LATENCY            (0)
#define  AVSYNC_MS12_HDMI_ARC_OUT_DD_LATENCY             (0)
#define  AVSYNC_MS12_HDMI_ARC_OUT_DDP_LATENCY            (120)
#define  AVSYNC_MS12_HDMI_OUT_LATENCY                    (0)
#define  AVSYNC_MS12_SPEAKER_LATENCY                     (-25)
#define  AVSYNC_MS12_HDMI_ARC_OUT_LATENCY_PROPERTY    "vendor.media.audio.hal.ms12.hdmiarcout"
#define  AVSYNC_MS12_HDMI_LATENCY_PROPERTY            "vendor.media.audio.hal.ms12.hdmiout"
#define  AVSYNC_MS12_SPEAKER_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.speaker"


#define  AVSYNC_MS12_NONTUNNEL_PCM_LATENCY_PROPERTY      "vendor.media.audio.hal.ms12.nontunnel.pcm"
#define  AVSYNC_MS12_NONTUNNEL_DDP_LATENCY_PROPERTY      "vendor.media.audio.hal.ms12.nontunnel.ddp"
#define  AVSYNC_MS12_NONTUNNEL_ATMOS_LATENCY_PROPERTY    "vendor.media.audio.hal.ms12.nontunnel.atmos"
#define  AVSYNC_MS12_NONTUNNEL_AC4_LATENCY_PROPERTY      "vendor.media.audio.hal.ms12.nontunnel.ac4"
#define  AVSYNC_MS12_NONTUNNEL_BYPASS_LATENCY_PROPERTY   "vendor.media.audio.hal.ms12.nontunnel.bypass"
#define  AVSYNC_MS12_NETFLIX_NONTUNNEL_BYPASS_LATENCY_PROPERTY   "vendor.media.audio.hal.ms12.netflix.nontunnel.bypass"

#define  AVSYNC_MS12_TUNNEL_PCM_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.tunnel.pcm"
#define  AVSYNC_MS12_TUNNEL_DD_LATENCY_PROPERTY          "vendor.media.audio.hal.ms12.tunnel.dd"

/*
 * For DDP output, same source but through CVBS vs HDMI, the Dolby Certification Target is different.
 *    1. CVBS: [-45, +125], set the AVSYNC_MS12_TUNNEL_DDP_CVBS_LATENCY_PROPERTY as 40, then get the +25ms.
 *    2. HDMI: [-45, 0], set the AVSYNC_MS12_TUNNEL_DDP_HDMI_LATENCY_PROPERTY as 90, then get the -25ms.
 */
#define  AVSYNC_MS12_TUNNEL_DDP_HDMI_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.tunnel.ddp_hdmi"
#define  AVSYNC_MS12_TUNNEL_DDP_CVBS_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.tunnel.ddp_cvbs"

#define  AVSYNC_MS12_TUNNEL_ATMOS_LATENCY_PROPERTY       "vendor.media.audio.hal.ms12.tunnel.atmos"
#define  AVSYNC_MS12_TUNNEL_AC4_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.tunnel.ac4"
#define  AVSYNC_MS12_TUNNEL_BYPASS_LATENCY_PROPERTY      "vendor.media.audio.hal.ms12.tunnel.bypass"
#define  AVSYNC_MS12_NETFLIX_TUNNEL_BYPASS_LATENCY_PROPERTY      "vendor.media.audio.hal.ms12.netflix.tunnel.bypass"

#define  AVSYNC_MS12_TUNNEL_VIDEO_DELAY_PROPERTY         "vendor.media.audio.hal.ms12.tunnel.video.delay"

#define  AVSYNC_MS12_PCM_OUT_LATENCY_PROPERTY            "vendor.media.audio.hal.ms12.pcmout"
#define  AVSYNC_MS12_DDP_OUT_LATENCY_PROPERTY            "vendor.media.audio.hal.ms12.ddpout"
#define  AVSYNC_MS12_DD_OUT_LATENCY_PROPERTY             "vendor.media.audio.hal.ms12.ddout"
#define  AVSYNC_MS12_MAT_OUT_LATENCY_PROPERTY            "vendor.media.audio.hal.ms12.matout"

/******************************************************************************************************/
/*below DDP tunning is for NonMS12*/
/******************************************************************************************************/
#define  AVSYNC_NONMS12_TUNNEL_TV_PCM_LATENCY                  (0)
#define  AVSYNC_NONMS12_TUNNEL_TV_DDP_LATENCY                  (-32)

#define  AVSYNC_NONMS12_TUNNEL_TV_PCM_LATENCY_PROPERTY         "vendor.media.audio.hal.nonms12.tunnel.tv.pcm"
#define  AVSYNC_NONMS12_TUNNEL_TV_DDP_LATENCY_PROPERTY         "vendor.media.audio.hal.nonms12.tunnel.tv.ddp"

#define  AVSYNC_NONMS12_TUNNEL_STB_PCM_LATENCY                          (0)
#define  AVSYNC_NONMS12_TUNNEL_STB_DDP_CVBS_LATENCY                     (0)//through CVBS, target is  [-30, 100]
/*
 * For Display 4K30Hz vs 4K60Hz, the UHD_2997 has different avsync result.
 * This is TODO, which wait for the Video Delay API.
 *
 * after manually modify the 60->40, the result:
 * PCM_UHD_5994 > -15 ~ -21
 * PCM_UDH_2997_Display_4K30Hz > -27 ~ -47 (detail results:-28, -47, -34, -42, -36, -27, -34, -41, -28, -31)
 * PCM_FHD_2997 > -7 ~ -23
 * PCM_FHD_5994 > -10 ~ -24
 */
#define  AVSYNC_NONMS12_TUNNEL_STB_DDP_HDMI_LATENCY                     (40)//through HDMI,  target is [-45, 0]

#define  AVSYNC_NONMS12_TUNNEL_STB_PCM_LATENCY_PROPERTY             "vendor.media.audio.hal.nonms12.tunnel.stb.pcm"
#define  AVSYNC_NONMS12_TUNNEL_STB_DDP_CVBS_LATENCY_PROPERTY        "vendor.media.audio.hal.nonms12.tunnel.stb.ddp_cvbs"
#define  AVSYNC_NONMS12_TUNNEL_STB_DDP_HDMI_LATENCY_PROPERTY        "vendor.media.audio.hal.nonms12.tunnel.stb.ddp_hdmi"


#define  AVSYNC_NONMS12_HDMI_ARC_OUT_PCM_LATENCY            (0)
#define  AVSYNC_NONMS12_HDMI_ARC_OUT_DD_LATENCY             (50)
#define  AVSYNC_NONMS12_HDMI_ARC_OUT_DDP_LATENCY            (50)
#define  AVSYNC_NONMS12_HDMI_OUT_LATENCY                    (0)
#define  AVSYNC_NONMS12_HDMI_SPEAKER_LATENCY                (0)

/* for different output format */
#define  AVSYNC_NONMS12_STB_PCMOUT_LATENCY                     (0)
#define  AVSYNC_NONMS12_STB_DDOUT_LATENCY                      (0)
#define  AVSYNC_NONMS12_STB_DDPOUT_LATENCY                     (50)

#define  AVSYNC_NONMS12_STB_PCMOUT_LATENCY_PROPERTY            "vendor.media.audio.hal.nonms12.stb.pcmout"
#define  AVSYNC_NONMS12_STB_DDOUT_LATENCY_PROPERTY             "vendor.media.audio.hal.nonms12.stb.ddout"
#define  AVSYNC_NONMS12_STB_DDPOUT_LATENCY_PROPERTY            "vendor.media.audio.hal.nonms12.stb.ddpout"



/******************************************************************************************************/
/* MS12 and Dolby Vision tunning part*/
/******************************************************************************************************/
/* different input formats: PCM/DDP/AC4 */
#define  AVSYNC_MS12_DV_TUNNEL_PCM_LATENCY                  (0)//won't change it
#define  AVSYNC_MS12_DV_TUNNEL_DDP_LATENCY                  (30)
#define  AVSYNC_MS12_DV_TUNNEL_AC4_LATENCY                  (0)//todo

#define  AVSYNC_MS12_DV_TUNNEL_PCM_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12_dv.tunnel.pcm"
#define  AVSYNC_MS12_DV_TUNNEL_DDP_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12_dv.tunnel.ddp"
#define  AVSYNC_MS12_DV_TUNNEL_AC4_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12_dv.tunnel.ac4"

/* different output formats: PCM/DDP/MAT */

/*
 *MS12 HDMI+PCM, target is [-45,   0]
 *DV HDMI+PCM,   target is [-20, +30]
 */
#define  AVSYNC_MS12_DV_TUNNEL_PCMOUT_LATENCY                  (-10)
/*
 * MS12 HDMI+PCM, target is [-100,   0]
 * DV HDMI+PCM,   target is [-20,  +30]
 *
 * MS12+DV result is [-38, 0] for Bitstream.
 *
 * 0 -> -25 wish the result change as [-38, 0] -> [-13,  25]
 * 0 ->  25 wish the result change as [-38, 0] -> [-63, -25]
 */

#define  AVSYNC_MS12_DV_TUNNEL_DDPOUT_LATENCY                  (-25)
#define  AVSYNC_MS12_DV_TUNNEL_MATOUT_LATENCY                  (0)//todo

#define  AVSYNC_MS12_DV_TUNNEL_PCMOUT_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12_dv.tunnel.pcmout"
#define  AVSYNC_MS12_DV_TUNNEL_DDPOUT_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12_dv.tunnel.ddpout"
#define  AVSYNC_MS12_DV_TUNNEL_MATOUT_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12_dv.tunnel.matout"



/******************************************************************************************************/
/* NONMS12 and Dolby Vision tunning part*/
/******************************************************************************************************/
/* different input formats: PCM/DDP */
#define  AVSYNC_NONMS12_DV_TUNNEL_PCM_LATENCY               (0)//won't change it
#define  AVSYNC_NONMS12_DV_TUNNEL_DDP_LATENCY               (0)

#define  AVSYNC_NONMS12_DV_TUNNEL_PCM_LATENCY_PROPERTY      "vendor.media.audio.hal.nonms12_dv.tunnel.pcm"
#define  AVSYNC_NONMS12_DV_TUNNEL_DDP_LATENCY_PROPERTY      "vendor.media.audio.hal.nonms12_dv.tunnel.ddp"

#define  AVSYNC_NONMS12_DV_TUNNEL_PCMOUT_LATENCY            (0)
#define  AVSYNC_NONMS12_DV_TUNNEL_DDPOUT_LATENCY            (-25)

#define  AVSYNC_NONMS12_DV_TUNNEL_PCMOUT_LATENCY_PROPERTY   "vendor.media.audio.hal.nonms12_dv.tunnel.pcmout"
#define  AVSYNC_NONMS12_DV_TUNNEL_DDPOUT_LATENCY_PROPERTY   "vendor.media.audio.hal.nonms12_dv.tunnel.ddpout"



/******************************************************************************************************/
/* NETFLIX tunning part*/
/******************************************************************************************************/
// right offset. 10-->30
#define  AVSYNC_MS12_NETFLIX_NONTUNNEL_PCM_LATENCY       (10)
// right offset. 20-->40-->30
#define  AVSYNC_MS12_NETFLIX_NONTUNNEL_DDP_LATENCY       (30)
#define  AVSYNC_MS12_NETFLIX_NONTUNNEL_ATMOS_LATENCY     (-18) /*for atmos we remove 32ms at the beginning*/
// right offset. -10-->20
#define  AVSYNC_MS12_NETFLIX_TUNNEL_PCM_LATENCY          (10)
// right offset. 65-->95-->75-->65
#define  AVSYNC_MS12_NETFLIX_TUNNEL_DDP_LATENCY          (65)
#define  AVSYNC_MS12_NETFLIX_TUNNEL_ATMOS_LATENCY        (-5)

#define  AVSYNC_MS12_NETFLIX_PCM_OUT_LATENCY             (0)
#define  AVSYNC_MS12_NETFLIX_DD_OUT_LATENCY              (0)
// left offset. 40-->15
#define  AVSYNC_MS12_NETFLIX_DDP_OUT_LATENCY             (15)
#define  AVSYNC_MS12_NETFLIX_MAT_OUT_LATENCY             (0)
#define  AVSYNC_MS12_NETFLIX_DDP_OUT_TUNNEL_TUNNING      (15)

#define  AVSYNC_MS12_NETFLIX_HDMI_ARC_OUT_PCM_LATENCY    (0)
#define  AVSYNC_MS12_NETFLIX_HDMI_ARC_OUT_DD_LATENCY     (0)
#define  AVSYNC_MS12_NETFLIX_HDMI_ARC_OUT_DDP_LATENCY    (120)
#define  AVSYNC_MS12_NETFLIX_HDMI_OUT_LATENCY            (0)
#define  AVSYNC_MS12_NETFLIX_SPEAKER_LATENCY             (-25)

#define  AVSYNC_MS12_NETFLIX_HDMI_ARC_OUT_LATENCY_PROPERTY    "vendor.media.audio.hal.ms12.netflix.hdmiarcout"
#define  AVSYNC_MS12_NETFLIX_HDMI_LATENCY_PROPERTY            "vendor.media.audio.hal.ms12.netflix.hdmiout"
#define  AVSYNC_MS12_NETFLIX_SPEAKER_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.netflix.speaker"

#define  AVSYNC_MS12_NETFLIX_PCM_OUT_LATENCY_PROPERTY    "vendor.media.audio.hal.ms12.netflix.pcmout"
#define  AVSYNC_MS12_NETFLIX_DDP_OUT_LATENCY_PROPERTY    "vendor.media.audio.hal.ms12.netflix.ddpout"
#define  AVSYNC_MS12_NETFLIX_DD_OUT_LATENCY_PROPERTY     "vendor.media.audio.hal.ms12.netflix.ddout"
#define  AVSYNC_MS12_NETFLIX_MAT_OUT_LATENCY_PROPERTY    "vendor.media.audio.hal.ms12.netflix.matout"

#define  AVSYNC_MS12_NETFLIX_NONTUNNEL_PCM_LATENCY_PROPERTY      "vendor.media.audio.hal.ms12.netflix.nontunnel.pcm"
#define  AVSYNC_MS12_NETFLIX_NONTUNNEL_DDP_LATENCY_PROPERTY      "vendor.media.audio.hal.ms12.netflix.nontunnel.ddp"
#define  AVSYNC_MS12_NETFLIX_NONTUNNEL_ATMOS_LATENCY_PROPERTY    "vendor.media.audio.hal.ms12.netflix.nontunnel.atmos"


#define  AVSYNC_MS12_NETFLIX_TUNNEL_PCM_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.netflix.tunnel.pcm"
#define  AVSYNC_MS12_NETFLIX_TUNNEL_DDP_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.netflix.tunnel.ddp"
#define  AVSYNC_MS12_NETFLIX_TUNNEL_ATMOS_LATENCY_PROPERTY       "vendor.media.audio.hal.ms12.netflix.tunnel.atmos"


/*below DDP tunning is for NonMS12*/
#define  AVSYNC_NONMS12_NETFLIX_TUNNEL_PCM_LATENCY          (0)
#define  AVSYNC_NONMS12_NETFLIX_TUNNEL_DDP_LATENCY          (54)  // 30 -> 54

#define  AVSYNC_NONMS12_NETFLIX_TUNNEL_PCM_LATENCY_PROPERTY         "vendor.media.audio.hal.nonms12.netflix.tunnel.pcm"
#define  AVSYNC_NONMS12_NETFLIX_TUNNEL_DDP_LATENCY_PROPERTY         "vendor.media.audio.hal.nonms12.netflix.tunnel.ddp"


/*below DDP tunning is for roku tv*/
#define  AVSYNC_DDP_NONTUNNEL_PCM_LATENCY               (0)
#define  AVSYNC_DDP_NONTUNNEL_RAW_LATENCY               (0)
#define  AVSYNC_DDP_TUNNEL_PCM_LATENCY                  (0)
#define  AVSYNC_DDP_TUNNEL_RAW_LATENCY                  (0)


#define  AVSYNC_DDP_NONTUNNEL_PCM_LATENCY_PROPERTY      "vendor.media.audio.hal.ddp.nontunnel.pcm"
#define  AVSYNC_DDP_NONTUNNEL_RAW_LATENCY_PROPERTY      "vendor.media.audio.hal.ddp.nontunnel.raw"
#define  AVSYNC_DDP_TUNNEL_PCM_LATENCY_PROPERTY         "vendor.media.audio.hal.ddp.tunnel.pcm"
#define  AVSYNC_DDP_TUNNEL_RAW_LATENCY_PROPERTY         "vendor.media.audio.hal.ddp.tunnel.raw"

/* following property, used for DTV avsync with MS12 processing */
/*
 * MS12 SDK v2.6.1, AV-Sync test for CVBS&HDMI output.
 * Here give an simple guide to tune the property to meet the target.
 * first step: test the CVBS output, to confirm the <different input format> property
 *          target [-45, 125]
 *          MACRO:AVSYNC_MS12_DTV_PCM_LATENCY_PROPERTY <PCM/DD/DDP/AC4>
 *          Property:"vendor.media.audio.hal.ms12.dtv.pcm" <pcm/dd/ddp/ac4>
 *          Value:AVSYNC_MS12_DTV_PCM_LATENCY <PCM/DD/DDP/AC4>
 *          current values are 0.
 *
 * second step: test the HDMI output, to confirm the PCM/DDP/MAT OUTPUT
 *          PCM target [-45, 0]
 *          DDP target [-100, 0]
 *          MAT target [-80, 0]
 *          MACRO:AVSYNC_MS12_DTV_PCM_OUT_LATENCY_PROPERTY <PCM/DD/DDP/MAT>
 *          Property:"vendor.media.audio.hal.ms12.dtv.pcmout" <pcm/dd/ddp/mat>
 *          Value:AVSYNC_MS12_DTV_PCM_OUT_LATENCY <PCM/DD/DDP/MAT>
 *          current values are DDP(-64=<26 (DDP Encoder Node) + 38 tuning>)/MAT(0).
 *
 * third step: test the Passthrough output, to confirm the DDP Passthrough
 *          Target DDP [-100, 0]
 *          MACRO:AVSYNC_MS12_DTV_BYPASS_LATENCY_PROPERTY
 *          Property:"vendor.media.audio.hal.ms12.dtv.bypass"
 *          Value:AVSYNC_MS12_DTV_BYPASS_LATENCY
 *          current values are 100(passthrough should consider ddp out,
 *          its value is AVSYNC_MS12_DTV_DDP_OUT_LATENCY)
 */
/* for different port and different format */
#define  AVSYNC_MS12_DTV_HDMI_ARC_OUT_PCM_LATENCY            (100)
#define  AVSYNC_MS12_DTV_HDMI_ARC_OUT_DD_LATENCY             (0)
#define  AVSYNC_MS12_DTV_HDMI_ARC_OUT_DDP_LATENCY            (0)
#define  AVSYNC_MS12_DTV_HDMI_OUT_PCM_LATENCY                (-30) /* if 0,  result locats at [-10, 10] */
#define  AVSYNC_MS12_DTV_HDMI_OUT_DD_LATENCY                 (0)
#define  AVSYNC_MS12_DTV_HDMI_OUT_DDP_LATENCY                (0)
#define  AVSYNC_MS12_DTV_HDMI_OUT_MAT_LATENCY                (0)
#define  AVSYNC_MS12_DTV_SPEAKER_LATENCY                     (0)
#define  AVSYNC_MS12_TV_DTV_SPEAKER_LATENCY                  (110)

#define  AVSYNC_MS12_DTV_HDMI_ARC_OUT_PCM_LATENCY_PROPERTY            "vendor.media.audio.hal.ms12.dtv.arc.pcm"
#define  AVSYNC_MS12_DTV_HDMI_ARC_OUT_DD_LATENCY_PROPERTY             "vendor.media.audio.hal.ms12.dtv.arc.dd"
#define  AVSYNC_MS12_DTV_HDMI_ARC_OUT_DDP_LATENCY_PROPERTY            "vendor.media.audio.hal.ms12.dtv.arc.ddp"
#define  AVSYNC_MS12_DTV_HDMI_OUT_PCM_LATENCY_PROPERTY                "vendor.media.audio.hal.ms12.dtv.hdmi.pcm"
#define  AVSYNC_MS12_DTV_HDMI_OUT_DD_LATENCY_PROPERTY                 "vendor.media.audio.hal.ms12.dtv.hdmi.dd"
#define  AVSYNC_MS12_DTV_HDMI_OUT_DDP_LATENCY_PROPERTY                "vendor.media.audio.hal.ms12.dtv.hdmi.ddp"
#define  AVSYNC_MS12_DTV_HDMI_OUT_MAT_LATENCY_PROPERTY                "vendor.media.audio.hal.ms12.dtv.hdmi.mat"
#define  AVSYNC_MS12_DTV_SPEAKER_LATENCY_PROPERTY                     "vendor.media.audio.hal.ms12.dtv.speaker"
#define  AVSYNC_MS12_TV_DTV_SPEAKER_LATENCY_PROPERTY                  "vendor.media.audio.hal.ms12.tv.dtv.speaker"


/* for different output format */
#define  AVSYNC_MS12_DTV_PCM_OUT_LATENCY                     (0)
#define  AVSYNC_MS12_DTV_DD_OUT_LATENCY                      (0)
/*
 * if set "vendor.media.audio.hal.ms12.dtv.ddpout" -100,
 * AUTO( DDP ) results located at [-93, -67]
 * after set property with (-50), results located at [-51, -22]
 */
#define  AVSYNC_MS12_DTV_DDP_OUT_LATENCY                     (-50)
/*
 * if set "vendor.media.audio.hal.ms12.dtv.matout" -55,
 * AUTO( MAT ) result located at [-90, -50]
 * after set property with (-30), results located at [-43, -19]
 */
#define  AVSYNC_MS12_DTV_MAT_OUT_LATENCY                     (-30)

#define  AVSYNC_MS12_DTV_PCM_OUT_LATENCY_PROPERTY            "vendor.media.audio.hal.ms12.dtv.pcmout"
#define  AVSYNC_MS12_DTV_DDP_OUT_LATENCY_PROPERTY            "vendor.media.audio.hal.ms12.dtv.ddpout"
#define  AVSYNC_MS12_DTV_DD_OUT_LATENCY_PROPERTY             "vendor.media.audio.hal.ms12.dtv.ddout"
#define  AVSYNC_MS12_DTV_MAT_OUT_LATENCY_PROPERTY            "vendor.media.audio.hal.ms12.dtv.matout"

/* for different input format */
#define  AVSYNC_MS12_DTV_PCM_LATENCY                    (0)
#define  AVSYNC_MS12_DTV_DD_LATENCY                     (0)
#define  AVSYNC_MS12_DTV_DDP_LATENCY                    (0)
#define  AVSYNC_MS12_DTV_AC4_LATENCY                    (0)

#define  AVSYNC_MS12_DTV_PCM_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.dtv.pcm"
#define  AVSYNC_MS12_DTV_DD_LATENCY_PROPERTY          "vendor.media.audio.hal.ms12.dtv.dd"
#define  AVSYNC_MS12_DTV_DDP_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.dtv.ddp"
#define  AVSYNC_MS12_DTV_AC4_LATENCY_PROPERTY         "vendor.media.audio.hal.ms12.dtv.ac4"

/* for passthrough(bypass) mode */
#define  AVSYNC_MS12_DTV_BYPASS_LATENCY                 (-90) /* if set 0, range is [48 ~ 82] */

#define  AVSYNC_MS12_DTV_BYPASS_LATENCY_PROPERTY      "vendor.media.audio.hal.ms12.dtv.bypass"

/* DTV is_TV mode, for offset latency */
#define  AVSYNC_DTV_TV_MODE_LATENCY                   (30)
#define  AVSYNC_DTV_TV_MODE_LATENCY_PROPERTY          "vendor.media.audio.hal.dtv.tv.latency"

/* following property, used for DTV avsync with DDP(nonms12) processing */
/* for different port and different format */
#define  AVSYNC_NONMS12_DTV_HDMI_ARC_OUT_PCM_LATENCY            (0)
#define  AVSYNC_NONMS12_DTV_HDMI_ARC_OUT_DD_LATENCY             (0)
#define  AVSYNC_NONMS12_DTV_HDMI_ARC_OUT_DDP_LATENCY            (0)
#define  AVSYNC_NONMS12_DTV_HDMI_OUT_PCM_LATENCY                (-30)
#define  AVSYNC_NONMS12_DTV_HDMI_OUT_DD_LATENCY                 (0)
#define  AVSYNC_NONMS12_DTV_HDMI_OUT_DDP_LATENCY                (0)
#define  AVSYNC_NONMS12_DTV_SPEAKER_LATENCY                     (0)

#define  AVSYNC_NONMS12_DTV_HDMI_ARC_OUT_PCM_LATENCY_PROPERTY            "vendor.media.audio.hal.nonms12.dtv.arc.pcm"
#define  AVSYNC_NONMS12_DTV_HDMI_ARC_OUT_DD_LATENCY_PROPERTY             "vendor.media.audio.hal.nonms12.dtv.arc.dd"
#define  AVSYNC_NONMS12_DTV_HDMI_ARC_OUT_DDP_LATENCY_PROPERTY            "vendor.media.audio.hal.nonms12.dtv.arc.ddp"
#define  AVSYNC_NONMS12_DTV_HDMI_OUT_PCM_LATENCY_PROPERTY                "vendor.media.audio.hal.nonms12.dtv.hdmi.pcm"
#define  AVSYNC_NONMS12_DTV_HDMI_OUT_DD_LATENCY_PROPERTY                 "vendor.media.audio.hal.nonms12.dtv.hdmi.dd"
#define  AVSYNC_NONMS12_DTV_HDMI_OUT_DDP_LATENCY_PROPERTY                "vendor.media.audio.hal.nonms12.dtv.hdmi.ddp"
#define  AVSYNC_NONMS12_DTV_SPEAKER_LATENCY_PROPERTY                     "vendor.media.audio.hal.nonms12.dtv.speaker"


/* for different output format */
#define  AVSYNC_NONMS12_DTV_PCM_OUT_LATENCY                     (0)
#define  AVSYNC_NONMS12_DTV_DD_OUT_LATENCY                      (0)
#define  AVSYNC_NONMS12_DTV_DDP_OUT_LATENCY                     (-64)
#define  AVSYNC_NONMS12_DTV_MAT_OUT_LATENCY                     (0)

#define  AVSYNC_NONMS12_DTV_PCM_OUT_LATENCY_PROPERTY            "vendor.media.audio.hal.nonms12.dtv.pcmout"
#define  AVSYNC_NONMS12_DTV_DDP_OUT_LATENCY_PROPERTY            "vendor.media.audio.hal.nonms12.dtv.ddpout"
#define  AVSYNC_NONMS12_DTV_DD_OUT_LATENCY_PROPERTY             "vendor.media.audio.hal.nonms12.dtv.ddout"
#define  AVSYNC_NONMS12_DTV_MAT_OUT_LATENCY_PROPERTY            "vendor.media.audio.hal.nonms12.dtv.matout"

/* for different input format */
#define  AVSYNC_NONMS12_DTV_PCM_LATENCY                    (0)
#define  AVSYNC_NONMS12_DTV_DD_LATENCY                     (0)
#define  AVSYNC_NONMS12_DTV_DDP_LATENCY                    (0)
#define  AVSYNC_NONMS12_DTV_AC4_LATENCY                    (0)

#define  AVSYNC_NONMS12_DTV_PCM_LATENCY_PROPERTY         "vendor.media.audio.hal.nonms12.dtv.pcm"
#define  AVSYNC_NONMS12_DTV_DD_LATENCY_PROPERTY          "vendor.media.audio.hal.nonms12.dtv.dd"
#define  AVSYNC_NONMS12_DTV_DDP_LATENCY_PROPERTY         "vendor.media.audio.hal.nonms12.dtv.ddp"
#define  AVSYNC_NONMS12_DTV_AC4_LATENCY_PROPERTY         "vendor.media.audio.hal.nonms12.dtv.ac4"


#endif

