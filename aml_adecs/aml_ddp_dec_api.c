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

#define LOG_TAG "aml_audio_dcv_dec"
//#define LOG_NDEBUG 0

#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <sound/asound.h>
#include <cutils/log.h>
#include <tinyalsa/asoundlib.h>
#include <cutils/properties.h>
#include "aml_ddp_dec_api.h"
#include "aml_ac3_parser.h"
#include "aml_audio_report.h"

enum {
    EXITING_STATUS = -1001,
    NO_ENOUGH_DATA = -1002,
};
typedef enum ERROR_CODE
{
   AML_DDP_AUDEC_SUCCESS            =  0,
   AML_DDP_AUDEC_OPEN_FAILURE       = 10,
   AML_DDP_AUDEC_INIT_FAILURE       = 20,
   AML_DDP_AUDEC_INVALID_HDR        = 30,
   AML_DDP_AUDEC_FRM_PARAM_ERROR    = 40,
   AML_DDP_AUDEC_INVALID_FRAME      = 50,
   AML_DDP_AUDEC_INCOMPLETE_FRAME   = 60,
   AML_DDP_AUDEC_FRM_CLEAN_FAILURE  = 70,
   AML_DDP_AUDEC_FRM_CLOSE_FAILURE  = 80,
   AML_DDP_AUDEC_QUITONERR          = 90,
} ERROR_CODE;


#define     BYTESPERWRD         2
#define     BITSPERWRD          (BYTESPERWRD*8)
#define     SYNCWRD             ((short)0x0b77)
#define     MAXFSCOD            3
#define     MAXDDDATARATE       38
#define     BS_STD              8
#define     ISDD(bsid)          ((bsid) <= BS_STD)
#define     MAXCHANCFGS         8
#define     BS_AXE              16
#define     ISDDP(bsid)         ((bsid) <= BS_AXE && (bsid) > 10)
#define     BS_BITOFFSET        40
#define     PTR_HEAD_SIZE       12

#define AML_AC3_STREAM_TYPE_0       0
#define AML_AC3_STREAM_TYPE_1       1
#define AML_AC3_STREAM_TYPE_2       2
#define AML_AC3_STREAM_TYPE_3       3

#define UPDATE_THRESHOLD_MAX    (10)
#define UPDATE_THRESHOLD  (2)


#define MAX_DECODER_FRAME_LENGTH 6144
#define READ_PERIOD_LENGTH 2048
#define MAX_DDP_FRAME_LENGTH 2560
#define MAX_DDP_BUFFER_SIZE (MAX_DECODER_FRAME_LENGTH * 4 + MAX_DECODER_FRAME_LENGTH + 8)

#define DOLBY_DCV_LIB_PATH_A "/odm/lib/libHwAudio_dcvdec.so"
#define CALCULATE_BITRATE_NEED_TIME 300 //calculate bitrate in the first 300 seconds

typedef struct {
    short       *buf;
    short        bitptr;
    short        data;
} BITSTREAM;

const short chanary[MAXCHANCFGS] = { 2, 1, 2, 3, 3, 4, 4, 5 };
enum {
    MODE11 = 0,
    MODE_RSVD = 0,
    MODE10,
    MODE20,
    MODE30,
    MODE21,
    MODE31,
    MODE22,
    MODE32
};
const unsigned short msktab[] = { 0x0000, 0x8000, 0xc000, 0xe000, 0xf000, 0xf800,
                             0xfc00, 0xfe00, 0xff00, 0xff80, 0xffc0, 0xffe0, 0xfff0, 0xfff8, 0xfffc,
                             0xfffe, 0xffff
                           };
const short frmsizetab[MAXFSCOD][MAXDDDATARATE] = {
    /* 48kHz */
    {
        64, 64, 80, 80, 96, 96, 112, 112,
        128, 128, 160, 160, 192, 192, 224, 224,
        256, 256, 320, 320, 384, 384, 448, 448,
        512, 512, 640, 640, 768, 768, 896, 896,
        1024, 1024, 1152, 1152, 1280, 1280
    },
    /* 44.1kHz */
    {
        69, 70, 87, 88, 104, 105, 121, 122,
        139, 140, 174, 175, 208, 209, 243, 244,
        278, 279, 348, 349, 417, 418, 487, 488,
        557, 558, 696, 697, 835, 836, 975, 976,
        1114, 1115, 1253, 1254, 1393, 1394
    },
    /* 32kHz */
    {
        96, 96, 120, 120, 144, 144, 168, 168,
        192, 192, 240, 240, 288, 288, 336, 336,
        384, 384, 480, 480, 576, 576, 672, 672,
        768, 768, 960, 960, 1152, 1152, 1344, 1344,
        1536, 1536, 1728, 1728, 1920, 1920
    }
};

static int (*ddp_decoder_init)(int, int, void **);
static int (*ddp_decoder_cleanup)(void *);
static int (*ddp_decoder_process)(char *, int, int *, int, char *, int *, struct pcm_info *, char *, int *,void *);
static int (*ddp_decoder_config)(void *, ddp_config_type_t, ddp_config_t *);

static void *gDDPDecoderLibHandler = NULL;
static void *handle = NULL;
static int gDDPDecoderCount = 0;

static void dump_ddp_data(void *buffer, int size, char *dump_name, char *file_name)
{
   if (property_get_bool(dump_name, false)) {
        FILE *fp1 = fopen(file_name, "a+");
        if (fp1) {
            int flen = fwrite((char *)buffer, 1, size, fp1);
            ALOGI("%s buffer %p size %d flen %d\n", __FUNCTION__, buffer, size,flen);
            fclose(fp1);
        }
    }
}

static short bitstream_init(short * buf, short bitptr, BITSTREAM *p_bstrm)
{
    p_bstrm->buf = buf;
    p_bstrm->bitptr = bitptr;
    p_bstrm->data = *buf;
    return 0;
}

static short bitstream_unprj(BITSTREAM *p_bstrm, short *p_data,  short numbits)
{
    unsigned short data;
    *p_data = (short)((p_bstrm->data << p_bstrm->bitptr) & msktab[numbits]);
    p_bstrm->bitptr += numbits;
    if (p_bstrm->bitptr >= BITSPERWRD) {
        p_bstrm->buf++;
        p_bstrm->data = *p_bstrm->buf;
        p_bstrm->bitptr -= BITSPERWRD;
        data = (unsigned short) p_bstrm->data;
        *p_data |= ((data >> (numbits - p_bstrm->bitptr)) & msktab[numbits]);
    }
    *p_data = (short)((unsigned short)(*p_data) >> (BITSPERWRD - numbits));
    return 0;
}

static int Get_DD_Parameters(void *buf, int *sample_rate, int *frame_size, int *ChMask)
{
    int numch = 0;
    BITSTREAM bstrm = {NULL, 0, 0};
    BITSTREAM *p_bstrm = &bstrm;
    short tmp = 0, acmod, lfeon, fscod, frmsizecod;
    bitstream_init((short*) buf, 0, p_bstrm);

    bitstream_unprj(p_bstrm, &tmp, 16);
    if (tmp != SYNCWRD) {
        ALOGW("[%s:%d] Invalid synchronization word", __func__, __LINE__);
        return 0;
    }
    bitstream_unprj(p_bstrm, &tmp, 16);
    bitstream_unprj(p_bstrm, &fscod, 2);
    if (fscod == MAXFSCOD) {
        ALOGI("Invalid sampling rate code");
        return 0;
    }

    if (fscod == 0) {
        *sample_rate = 48000;
    } else if (fscod == 1) {
        *sample_rate = 44100;
    } else if (fscod == 2) {
        *sample_rate = 32000;
    }

    bitstream_unprj(p_bstrm, &frmsizecod, 6);
    if (frmsizecod >= MAXDDDATARATE) {
        ALOGI("Invalid frame size code");
        return 0;
    }

    *frame_size = 2 * frmsizetab[fscod][frmsizecod];

    bitstream_unprj(p_bstrm, &tmp, 5);
    if (!ISDD(tmp)) {
        ALOGI("Unsupported bitstream id");
        return 0;
    }

    bitstream_unprj(p_bstrm, &tmp, 3);
    bitstream_unprj(p_bstrm, &acmod, 3);

    if ((acmod != MODE10) && (acmod & 0x1)) {
        bitstream_unprj(p_bstrm, &tmp, 2);
    }
    if (acmod & 0x4) {
        bitstream_unprj(p_bstrm, &tmp, 2);
    }

    if (acmod == MODE20) {
        bitstream_unprj(p_bstrm, &tmp, 2);
    }
    bitstream_unprj(p_bstrm, &lfeon, 1);

    *ChMask = IndependentFrame_Acmod_Lfeon_to_ChannelMask(acmod, lfeon);
    numch = chanary[acmod];

    //*ChNum = numch + lfeon;
    //ALOGI("DEBUG:numch=%d sample_rate=%d %p [%s %d]",ChNum,sample_rate,this,__FUNCTION__,__LINE__);
    return numch;
}

static int Get_DDP_Parameters(void *buf, int *sample_rate, int *frame_size, int *ChMask, int *isDependentFrame,int *ad_substream_supported)
{
    int numch = 0;
    BITSTREAM bstrm = {NULL, 0, 0};
    BITSTREAM *p_bstrm = &bstrm;
    short tmp = 0;
    short strmtyp, substreamid, frmsiz, fscod, acmod , lfeon ;
    short acmod_d, lfeon_d , compre, compr2e, chanmape, chanmap ;
    bitstream_init((short*) buf, 0, p_bstrm);
    bitstream_unprj(p_bstrm, &tmp, 16);
    if (tmp != SYNCWRD) {
        ALOGV("Invalid synchronization word");
        return -1;
    } else {
        ALOGV("valid synchronization word");
    }

    bitstream_unprj(p_bstrm, &strmtyp, 2);
    bitstream_unprj(p_bstrm, &substreamid, 3);

    if (strmtyp == 0 && substreamid != 0) {
       *ad_substream_supported = 1;
    }

    bitstream_unprj(p_bstrm, &frmsiz, 11);
    *frame_size = 2 * (frmsiz + 1);

    bitstream_unprj(p_bstrm, &fscod, 2);
    if (fscod == 0x3) {
        ALOGI("Half sample rate unsupported");
        return -1;
    } else {
        if (fscod == 0) {
            *sample_rate = 48000;
        } else if (fscod == 1) {
            *sample_rate = 44100;
        } else if (fscod == 2) {
            *sample_rate = 32000;
        }
        bitstream_unprj(p_bstrm, &tmp, 2); //numblkscod 2bit
    }

    if (strmtyp == AML_AC3_STREAM_TYPE_3) {
        return -1;
    } else if ((strmtyp == AML_AC3_STREAM_TYPE_0) || (strmtyp == AML_AC3_STREAM_TYPE_2)) {
        bitstream_unprj(p_bstrm, &acmod, 3);
        bitstream_unprj(p_bstrm, &lfeon, 1);
        *ChMask = IndependentFrame_Acmod_Lfeon_to_ChannelMask(acmod, lfeon);
        *isDependentFrame = 0;
    } else {
        *isDependentFrame = 1;
    }

    if (*isDependentFrame == 1) {
        bitstream_unprj(p_bstrm, &acmod_d, 3);
        bitstream_unprj(p_bstrm, &lfeon_d, 1);

        bitstream_unprj(p_bstrm, &tmp, 10); // bsid 5bit, dialnorm 5bit
        bitstream_unprj(p_bstrm, &compre, 1);
        if (compre == 0x1) {
            bitstream_unprj(p_bstrm, &tmp, 8); //compr 8bit
        }
        if (acmod_d == 0x0) {
            bitstream_unprj(p_bstrm, &tmp, 5); //dialnorm2 5bit
            bitstream_unprj(p_bstrm, &compr2e, 1);
            if (compr2e == 0x1) {
                bitstream_unprj(p_bstrm, &tmp, 8); //compr2 8bit
            }
        }
        bitstream_unprj(p_bstrm, &chanmape, 1);
        if (chanmape == 0x1) {
            bitstream_unprj(p_bstrm, &chanmap, 16);
            *ChMask = DependentFrame_Chanmap_to_ChannelMask(chanmap);
        }
    }
    return 0;
}

static short bitstream_skip(BITSTREAM   *p_bstrm, short    numbits)
{
    p_bstrm->bitptr += numbits;
    while (p_bstrm->bitptr >= BITSPERWRD) {
        p_bstrm->buf++;
        p_bstrm->data = *p_bstrm->buf;
        p_bstrm->bitptr -= BITSPERWRD;
    }
    return 0;
}

static short bitstream_getbsid(BITSTREAM *p_inbstrm,    short *p_bsid)
{
    BITSTREAM    bstrm;

    bitstream_init(p_inbstrm->buf, p_inbstrm->bitptr, &bstrm);
    bitstream_skip(&bstrm, BS_BITOFFSET);
    bitstream_unprj(&bstrm, p_bsid, 5);
    if (!ISDDP(*p_bsid) && !ISDD(*p_bsid)) {
        ALOGI("Unsupported bitstream id");
    }

    return 0;
}

static int  Get_Parameters(void *buf, int *sample_rate, int *frame_size, int *ChMask, int *is_aml_eac3, int *isDependentFrame, int *ad_substream_supported)
{
    BITSTREAM bstrm = {NULL, 0, 0};
    BITSTREAM *p_bstrm = &bstrm;
    short    bsid;
    int chnum = 0;
    uint8_t ptr8[PTR_HEAD_SIZE];

    memcpy(ptr8, buf, PTR_HEAD_SIZE);

    if ((ptr8[0] == 0x0b) && (ptr8[1] == 0x77)) {
        int i;
        uint8_t tmp;
        for (i = 0; i < PTR_HEAD_SIZE - 1; i += 2) {
            tmp = ptr8[i];
            ptr8[i] = ptr8[i + 1];
            ptr8[i + 1] = tmp;
        }
    }

    bitstream_init((short*) ptr8, 0, p_bstrm);
    int ret = bitstream_getbsid(p_bstrm, &bsid);
    if (ret < 0) {
        return -1;
    }
    if (ISDDP(bsid)) {
        Get_DDP_Parameters(ptr8, sample_rate, frame_size, ChMask, isDependentFrame, ad_substream_supported);
        *is_aml_eac3 = 1;
    } else if (ISDD(bsid)) {
        Get_DD_Parameters(ptr8, sample_rate, frame_size, ChMask);
        *is_aml_eac3 = 0;
    }
    return 0;
}

static  int unload_ddp_decoder_lib()
{
    if (ddp_decoder_cleanup != NULL && handle != NULL) {
        (*ddp_decoder_cleanup)(handle);
        handle = NULL;
    }
    ddp_decoder_init = NULL;
    ddp_decoder_process = NULL;
    ddp_decoder_cleanup = NULL;
    ddp_decoder_config = NULL;
    if (gDDPDecoderLibHandler != NULL) {
        dlclose(gDDPDecoderLibHandler);
        gDDPDecoderLibHandler = NULL;
    }
    return 0;
}

static int dcv_decoder_init(int decoding_mode, aml_dec_control_type_t digital_raw)
{
    int input_mode = 1;
    gDDPDecoderLibHandler = dlopen(DOLBY_DCV_LIB_PATH_A, RTLD_NOW);
    if (!gDDPDecoderLibHandler) {
        ALOGE("%s, failed to open (libstagefright_soft_dcvdec.so), %s\n", __FUNCTION__, dlerror());
        goto Error;
    } else {
        ALOGV("<%s::%d>--[gDDPDecoderLibHandler]", __FUNCTION__, __LINE__);
    }

    ddp_decoder_init = (int (*)(int, int,void **)) dlsym(gDDPDecoderLibHandler, "ddp_decoder_init");
    if (ddp_decoder_init == NULL) {
        ALOGE("%s,cant find decoder lib,%s\n", __FUNCTION__, dlerror());
        goto Error;
    } else {
        ALOGV("<%s::%d>--[ddp_decoder_init:]", __FUNCTION__, __LINE__);
    }

    ddp_decoder_process = (int (*)(char * , int , int *, int , char *, int *, struct pcm_info *, char *, int *,void *))
                          dlsym(gDDPDecoderLibHandler, "ddp_decoder_process");
    if (ddp_decoder_process == NULL) {
        ALOGE("%s,cant find decoder lib,%s\n", __FUNCTION__, dlerror());
        goto Error;
    } else {
        ALOGV("<%s::%d>--[ddp_decoder_process:]", __FUNCTION__, __LINE__);
    }

    ddp_decoder_cleanup = (int (*)(void *)) dlsym(gDDPDecoderLibHandler, "ddp_decoder_cleanup");
    if (ddp_decoder_cleanup == NULL) {
        ALOGE("%s,cant find decoder lib,%s\n", __FUNCTION__, dlerror());
        goto Error;
    } else {
        ALOGV("<%s::%d>--[ddp_decoder_cleanup:]", __FUNCTION__, __LINE__);
    }

    ddp_decoder_config = (int (*)(void *, ddp_config_type_t, ddp_config_t *)) dlsym(gDDPDecoderLibHandler, "ddp_decoder_config");
    if (ddp_decoder_config == NULL) {
        ALOGE("%s,can not find decoder config function,%s\n", __FUNCTION__, dlerror());
    } else {
        ALOGV("<%s::%d>--[ddp_decoder_config:]", __FUNCTION__, __LINE__);
    }

    (*ddp_decoder_init)(decoding_mode, digital_raw, &handle);
    gDDPDecoderCount++;
    return 0;
Error:
    unload_ddp_decoder_lib();
    return -1;
}

static int dcv_decode_process(unsigned char*input, int input_size, unsigned char *outbuf,
                              int *out_size, char *spdif_buf, int *raw_size, int nIsEc3,
                              struct pcm_info *pcm_out_info)
{
    int outputFrameSize = 0;
    int used_size = 0;
    int decoded_pcm_size = 0;
    int ret = -1;

    if (ddp_decoder_process == NULL) {
        return ret;
    }

    ret = (*ddp_decoder_process)((char *) input
                                 , input_size
                                 , &used_size
                                 , nIsEc3
                                 , (char *) outbuf
                                 , out_size
                                 , pcm_out_info
                                 , (char *) spdif_buf
                                 , (int *) raw_size
                                 ,handle);
    ALOGV("used_size %d,lpcm out_size %d,raw out size %d",used_size,*out_size,*raw_size);
    return used_size;
}

int dcv_decoder_init_patch(aml_dec_t ** ppaml_dec, aml_dec_config_t * dec_config)
{
    struct dolby_ddp_dec *ddp_dec = NULL;
    aml_dec_t  *aml_dec = NULL;
    aml_dcv_config_t *dcv_config = &dec_config->dcv_config;
    int ret = 0;

    if (dcv_config->decoding_mode != DDP_DECODE_MODE_SINGLE &&
        dcv_config->decoding_mode != DDP_DECODE_MODE_AD_DUAL &&
        dcv_config->decoding_mode != DDP_DECODE_MODE_AD_SUBSTREAM) {
        ALOGE("wrong decoder mode =%d", dcv_config->decoding_mode);
        goto error;
    }

    ddp_dec = aml_audio_calloc(1, sizeof(struct dolby_ddp_dec));
    if (ddp_dec == NULL) {
        ALOGE("malloc ddp_dec failed\n");
        goto error;
    }

    aml_dec = &ddp_dec->aml_dec;

    dec_data_info_t * dec_pcm_data = &aml_dec->dec_pcm_data;
    dec_data_info_t * dec_raw_data = &aml_dec->dec_raw_data;
    dec_data_info_t * raw_in_data  = &aml_dec->raw_in_data;

    ddp_dec->decoding_mode = dcv_config->decoding_mode;
    ddp_dec->digital_raw   = dcv_config->digital_raw;
    ddp_dec->nIsEc3        = dcv_config->nIsEc3;
    ddp_dec->is_iec61937   = dcv_config->is_iec61937;

    aml_dec->format = dcv_config->format;
    ret = dcv_decoder_init(ddp_dec->decoding_mode, ddp_dec->digital_raw);
    ALOGI("dcv_decoder_init decoding mode =%d, ddp_dec->digital_raw=%d ret =%d", ddp_dec->decoding_mode, ddp_dec->digital_raw, ret);
    if (ret < 0) {
        goto error;
    }

    ddp_dec->status = 1;
    ddp_dec->remain_size = 0;
    ddp_dec->outlen_pcm = 0;
    ddp_dec->outlen_raw = 0;
    ddp_dec->curFrmSize = 0;
    ddp_dec->inbuf = NULL;

    ddp_dec->dcv_pcm_writed = 0;
    ddp_dec->dcv_decoded_samples = 0;
    ddp_dec->dcv_decoded_errcount = 0;
    //memset(ddp_dec->sysfs_buf, 0, sizeof(ddp_dec->sysfs_buf));

    memset(&ddp_dec->pcm_out_info, 0, sizeof(struct pcm_info));
    //memset(&ddp_dec->aml_resample, 0, sizeof(struct resample_para));
    ddp_dec->inbuf_size = MAX_DECODER_FRAME_LENGTH * 4 * 4;
    ddp_dec->inbuf = (unsigned char*) aml_audio_calloc(1, ddp_dec->inbuf_size);

    if (!ddp_dec->inbuf) {
        ALOGE("malloc buffer failed\n");
        ddp_dec->inbuf_size = 0;

        goto error;
    }

    ddp_dec->total_raw_size = 0;
    ddp_dec->total_time = 0;
    ddp_dec->bit_rate = 0;
    memset(&(ddp_dec->stream_info), 0x00, sizeof(aml_dec_stream_info_t));
    dec_pcm_data->buf_size = MAX_DECODER_FRAME_LENGTH;
    dec_pcm_data->buf = (unsigned char*) aml_audio_calloc(1, dec_pcm_data->buf_size);
    if (!dec_pcm_data->buf) {
        ALOGE("malloc buffer failed\n");
        goto error;
    }

    dec_raw_data->buf_size = MAX_DECODER_FRAME_LENGTH * 4 + 8;
    dec_raw_data->buf = (unsigned char*) aml_audio_calloc(1, dec_raw_data->buf_size);
    if (!dec_raw_data->buf) {
        ALOGE("malloc buffer failed\n");
        goto error;
    }

    raw_in_data->buf_size = MAX_DECODER_FRAME_LENGTH * 2;
    raw_in_data->buf = (unsigned char*) aml_audio_calloc(1, raw_in_data->buf_size);
    if (!raw_in_data->buf) {
        ALOGE("malloc buffer failed\n");
        goto error;
    }

    dcv_decoder_config(aml_dec, AML_DEC_CONFIG_MIXER_LEVEL, dec_config);
    dcv_decoder_config(aml_dec, AML_DEC_CONFIG_MIXING_ENABLE, dec_config);
    dcv_decoder_config(aml_dec, AML_DEC_CONFIG_AD_VOL, dec_config);

    * ppaml_dec = (aml_dec_t *)ddp_dec;
    ALOGI("%s success", __func__);
    return 0;

error:
    if (ddp_dec) {
        if (ddp_dec->inbuf) {
            aml_audio_free(ddp_dec->inbuf);
        }

        if (dec_pcm_data->buf) {
            aml_audio_free(dec_pcm_data->buf);
        }

        if (dec_raw_data->buf) {
            aml_audio_free(dec_raw_data->buf);
        }

        if (raw_in_data->buf) {
            aml_audio_free(raw_in_data->buf);
        }

        aml_audio_free(ddp_dec);
    }
    * ppaml_dec = NULL;
    ALOGE("%s failed", __func__);
    return -1;
}

int dcv_decoder_release_patch(aml_dec_t * aml_dec)
{
    struct dolby_ddp_dec *ddp_dec = (struct dolby_ddp_dec *)aml_dec;

    if (aml_dec == NULL) {
        ALOGE("%s aml_dec NULL", __func__);
        return -1;
    }

    dec_data_info_t * dec_pcm_data = &aml_dec->dec_pcm_data;
    dec_data_info_t * dec_raw_data = &aml_dec->dec_raw_data;
    dec_data_info_t * raw_in_data  = &aml_dec->raw_in_data;

    // switch movieplayer to hdmiin by HOME key, hdmiin init dcv decoder before movieplayer
    // release dcv decoder, so handle would be NULL by movieplayer release, and hdmiin cause error
    gDDPDecoderCount--;
    if (ddp_decoder_cleanup != NULL && handle != NULL && (gDDPDecoderCount <= 0)) {
        (*ddp_decoder_cleanup)(handle);
        handle = NULL;
    }

    if (ddp_dec && ddp_dec->status == 1) {
        ddp_dec->status = 0;
        ddp_dec->inbuf_size = 0;
        ddp_dec->remain_size = 0;
        ddp_dec->outlen_pcm = 0;
        ddp_dec->outlen_raw = 0;
        ddp_dec->nIsEc3 = 0;
        ddp_dec->curFrmSize = 0;
        ddp_dec->decoding_mode = DDP_DECODE_MODE_SINGLE;
        ddp_dec->mixer_level = 0;
        ddp_dec->dcv_pcm_writed = 0;
        ddp_dec->dcv_decoded_samples = 0;
        ddp_dec->dcv_decoded_errcount = 0;
        //memset(ddp_dec->sysfs_buf, 0, sizeof(ddp_dec->sysfs_buf));
        aml_audio_free(ddp_dec->inbuf);
        ddp_dec->inbuf = NULL;
        ddp_dec->get_parameters = NULL;
        ddp_dec->decoder_process = NULL;
        memset(&ddp_dec->pcm_out_info, 0, sizeof(struct pcm_info));
        //memset(&ddp_dec->aml_resample, 0, sizeof(struct resample_para));

        if (dec_pcm_data->buf) {
            aml_audio_free(dec_pcm_data->buf);
            dec_pcm_data->buf = NULL;
        }

        if (dec_raw_data->buf) {
            aml_audio_free(dec_raw_data->buf);
            dec_raw_data->buf = NULL;
        }

        if (raw_in_data->buf) {
            aml_audio_free(raw_in_data->buf);
            raw_in_data->buf = NULL;
        }

        aml_audio_free(ddp_dec);
    }
    ALOGI("%s exit", __func__);
    return 0;
}

#define IEC61937_HEADER_SIZE 8
int dcv_decoder_get_framesize(unsigned char*buffer, int bytes, int* p_head_offset)
{
    int offset = 0;
    unsigned char *read_pointer = buffer;
    int mSample_rate = 0;
    int mFrame_size = 0;
    int mChNum = 0;
    int mChMask = 0;
    int isDependentFrame = 0;
    int is__aml_eac3 = 0;
    int ad_substream_supported = 0;
    ALOGV("%s %x %x\n", __FUNCTION__, read_pointer[0], read_pointer[1]);

    while (offset <  bytes -1) {
        if ((read_pointer[0] == 0x0b && read_pointer[1] == 0x77) || \
                    (read_pointer[0] == 0x77 && read_pointer[1] == 0x0b)) {
            Get_Parameters(read_pointer, &mSample_rate, &mFrame_size, &mChMask, &is__aml_eac3, &isDependentFrame, &ad_substream_supported);
            mChNum = popcount(mChMask);
            *p_head_offset = offset;
            ALOGV("%s mFrame_size %d offset %d\n", __FUNCTION__, mFrame_size, offset);
            return mFrame_size;
        }
        offset++;
        read_pointer++;
    }
    return 0;
}

int is_ad_substream_supported(unsigned char *buffer,int write_len) {
    int offset = 0;
    unsigned char *read_pointer = buffer;
    int mSample_rate = 0;
    int mFrame_size = 0;
    int mChNum = 0;
    int is__aml_eac3 = 0;
    int mChMask = 0;
    int isDependentFrame = 0;
    int ad_substream_supported = 0;
    ALOGV("%s %x %x\n", __FUNCTION__, read_pointer[0], read_pointer[1]);

    while (offset <  write_len -1) {
        if ((read_pointer[0] == 0x0b && read_pointer[1] == 0x77) || \
                    (read_pointer[0] == 0x77 && read_pointer[1] == 0x0b)) {
            Get_Parameters(read_pointer, &mSample_rate, &mFrame_size, &mChMask, &is__aml_eac3, &isDependentFrame, &ad_substream_supported);
            mChNum = popcount(mChMask);
            ALOGV("%s ad_substream_supported %d offset %d\n", __FUNCTION__, ad_substream_supported, offset);
            if (ad_substream_supported)
               return ad_substream_supported;
        }
        offset++;
        read_pointer++;
    }
    return 0;
}

int dcv_decoder_process_patch(aml_dec_t * aml_dec, unsigned char *buffer, int bytes)
{
    int mSample_rate = 0;
    int mFrame_size = 0;
    int mChNum = 0;
    int is__aml_eac3 = 0;
    int in_sync = 0;
    int used_size = 0;
    int i = 0;
    unsigned char *read_pointer = NULL;
    size_t main_frame_deficiency = 0;
    int ret = AML_DEC_RETURN_TYPE_OK;
    unsigned char temp;
    int outPCMLen = 0;
    int outRAWLen = 0;
    int total_size = 0;
    int read_offset = 0;
    int total_used_size = 0;
    int mChMask = 0;
    int isDependentFrame = 0;
    int ad_substream_supported = 0;
    struct dolby_ddp_dec *ddp_dec = (struct dolby_ddp_dec *)aml_dec;
    int last_remain_size = ddp_dec->remain_size;
    int decoder_frame = 0;
    ddp_dec->outlen_pcm = 0;
    ddp_dec->outlen_raw = 0;
    int bit_width = 16;
    int n_bytes_frame = 0;

    dec_data_info_t * dec_pcm_data = &aml_dec->dec_pcm_data;
    dec_data_info_t * dec_raw_data = &aml_dec->dec_raw_data;
    dec_data_info_t * raw_in_data  = &aml_dec->raw_in_data;

    dec_pcm_data->data_len = 0;
    dec_raw_data->data_len = 0;
    raw_in_data->data_len  = 0;

    /* dual input is from dtv, and it is similar with MS12,
     * main and associate is packaged with IEC61937
     */
    if (ddp_dec->decoding_mode == DDP_DECODE_MODE_AD_DUAL) {
        int dual_decoder_used_bytes = 0;
        int dual_input_ret = 0;
        void *main_frame_buffer = NULL;
        int main_frame_size = 0;
        void *associate_frame_buffer = NULL;
        int associate_frame_size = 0;
        dual_input_ret = scan_dolby_main_associate_frame(buffer
                 , bytes
                 , &dual_decoder_used_bytes
                 , &main_frame_buffer
                 , &main_frame_size
                 , &associate_frame_buffer
                 , &associate_frame_size);
        if (dual_input_ret) {
            ALOGE("%s used size %d dont find the iec61937 format header, rescan next time!\n", __FUNCTION__, dual_decoder_used_bytes);
            goto EXIT;
        }
        ALOGV("main frame size =%d ad frame size =%d", main_frame_size, associate_frame_size);
        if ((main_frame_size + associate_frame_size) > ddp_dec->inbuf_size) {
            ALOGE("too big frame size =%d %d", main_frame_size, associate_frame_size);
            goto EXIT;
        }
        /* copy main data */
        memcpy((char *)ddp_dec->inbuf, main_frame_buffer, main_frame_size);
        /* copy ad data */
        memcpy((char *)ddp_dec->inbuf + main_frame_size, associate_frame_buffer, associate_frame_size);
        ddp_dec->remain_size = main_frame_size + associate_frame_size;
        mFrame_size = main_frame_size + associate_frame_size;
        n_bytes_frame = mFrame_size; // not used currently
        read_pointer = ddp_dec->inbuf;
        read_offset = 0;
        in_sync = 1;
        total_used_size = bytes;
    } else {
        //check if the buffer overflow
        if ((ddp_dec->remain_size + bytes) > ddp_dec->inbuf_size) {
            ALOGE("too big input size =%d %d", ddp_dec->remain_size, bytes);
            goto EXIT;
        }
        memcpy((char *)ddp_dec->inbuf + ddp_dec->remain_size, (char *)buffer, bytes);
        ddp_dec->remain_size += bytes;
        total_size = ddp_dec->remain_size;
        read_pointer = ddp_dec->inbuf;

        //check the sync word of dolby frames
        if (ddp_dec->is_iec61937 == false) {
            while (ddp_dec->remain_size > 16) {
                if ((read_pointer[0] == 0x0b && read_pointer[1] == 0x77) || \
                    (read_pointer[0] == 0x77 && read_pointer[1] == 0x0b)) {
                    Get_Parameters(read_pointer, &mSample_rate, &mFrame_size, &mChMask, &is__aml_eac3, &isDependentFrame, &ad_substream_supported);
                    mChNum = popcount(mChMask);
                    if ((mFrame_size == 0) || (mFrame_size < PTR_HEAD_SIZE) || \
                        (mChNum == 0) || (mSample_rate == 0)) {
                    } else {
                        in_sync = 1;
                        break;
                    }
                }
                ddp_dec->remain_size--;
                read_pointer++;
                total_used_size++;
            }
        } else {
           while (ddp_dec->remain_size > 16) {
                if ((read_pointer[0] == 0x72 && read_pointer[1] == 0xf8 && read_pointer[2] == 0x1f && read_pointer[3] == 0x4e)||
                    (read_pointer[0] == 0x4e && read_pointer[1] == 0x1f && read_pointer[2] == 0xf8 && read_pointer[3] == 0x72)) {
                    unsigned int pcpd = *(uint32_t*)(read_pointer  + 4);
                    int pc = (pcpd & 0x1f);
                    if (pc == 0x15) {
                        mFrame_size = (pcpd >> 16);
                        in_sync = 1;
                        break;
                    } else if (pc == 0x1) {
                        mFrame_size = (pcpd >> 16) / 8;
                        in_sync = 1;
                        break;
                    }
                }
                ddp_dec->remain_size--;
                read_pointer++;
                total_used_size++;
            }
            read_offset = 8;
            if (in_sync) {
                int frame_size = 0;
                /* this 'frame_size' is the size of one frame. but In a IEC61937 package,
                   sometimes there are multi frames in one package */
                Get_Parameters(read_pointer + read_offset, &mSample_rate, &mFrame_size, &mChMask, &is__aml_eac3, &isDependentFrame, &ad_substream_supported);
                mChNum = popcount(mChMask);
            }
        }
    }
    ALOGV("remain %d, frame size %d, in sync %d\n", ddp_dec->remain_size, mFrame_size, in_sync);
    //we do not have one complete dolby frames.we need cache the
    //data and combine with the next input data.
    if (ddp_dec->remain_size < mFrame_size || in_sync == 0) {
        //ALOGI("remain %d,frame size %d, read more\n",remain_size,mFrame_size);
        memcpy(ddp_dec->inbuf, read_pointer, ddp_dec->remain_size);
        return AML_DEC_RETURN_TYPE_CACHE_DATA;
    }
    ddp_dec->curFrmSize = mFrame_size;
    read_pointer += read_offset;
    ddp_dec->remain_size -= read_offset;
    total_used_size += read_offset;

    //do the endian conversion
    if (read_pointer[0] == 0x77 && read_pointer[1] == 0x0b) {
        for (i = 0; i < mFrame_size / 2; i++) {
            temp = read_pointer[2 * i + 1];
            read_pointer[2 * i + 1] = read_pointer[2 * i];
            read_pointer[2 * i] = temp;
        }
    }

    parse_report_info_samplerate_channelnum(read_pointer, ddp_dec, mFrame_size);

    if (raw_in_data->buf_size >= mFrame_size) {
        raw_in_data->data_len = mFrame_size;
        memcpy(raw_in_data->buf, read_pointer, mFrame_size);
    } else {
        ALOGE("too big frame size = %d", mFrame_size);
    }

    n_bytes_frame = mFrame_size; // to record the frame size
    while (mFrame_size > 0) {
        outPCMLen = 0;
        outRAWLen = 0;
        int current_size = 0;
        ALOGV("ddp_dec->outlen_pcm=%d raw len=%d in =%p dec_pcm_data->buf=%p dec_raw_data->buf=%p",
            ddp_dec->outlen_pcm, ddp_dec->outlen_raw, read_pointer, dec_pcm_data->buf, dec_raw_data->buf);
        decoder_frame = current_size = dcv_decode_process((unsigned char*)read_pointer + used_size,
                                             mFrame_size,
                                             (unsigned char *)dec_pcm_data->buf + ddp_dec->outlen_pcm,
                                             &outPCMLen,
                                             (char *)dec_raw_data->buf + ddp_dec->outlen_raw,
                                             &outRAWLen,
                                             ddp_dec->nIsEc3,
                                             &ddp_dec->pcm_out_info);
        if (AML_DDP_AUDEC_SUCCESS == decoder_frame) {
            ddp_dec->stream_info.stream_decode_num++;
        } else if (AML_DDP_AUDEC_INVALID_FRAME == decoder_frame) {
            ddp_dec->stream_info.stream_error_num++;
            ddp_dec->stream_info.stream_drop_num++;
        }
        used_size += current_size;
        ddp_dec->outlen_pcm += outPCMLen;
        ddp_dec->outlen_raw += outRAWLen;
        if (used_size > 0)
            mFrame_size -= current_size;
    }

    /* dump decoded pcm data */
    dump_ddp_data(dec_pcm_data->buf, ddp_dec->outlen_pcm,
                  "vendor.audio.ddp.outputdump", "/data/audio/dolby_pcm.pcm");

    if (ddp_dec->decoding_mode != DDP_DECODE_MODE_AD_DUAL) {
        total_used_size += used_size;
        if (used_size > n_bytes_frame) {
            ALOGE("%s line %d used_size %d n_bytes_frame %d\n", __func__, __LINE__, used_size, n_bytes_frame);
        }
    }

    if (ddp_dec->outlen_pcm > 0) {
        dec_pcm_data->data_format = AUDIO_FORMAT_PCM_16_BIT;
        dec_pcm_data->data_ch     = 2;
        dec_pcm_data->data_sr     = ddp_dec->pcm_out_info.sample_rate;
        dec_pcm_data->data_len    = ddp_dec->outlen_pcm;
    }

    if (ddp_dec->outlen_raw > 0) {
        dec_raw_data->data_format = AUDIO_FORMAT_IEC61937;
        if (aml_dec->format == AUDIO_FORMAT_E_AC3 && (ddp_dec->digital_raw == AML_DEC_CONTROL_CONVERT)) {
            dec_raw_data->sub_format = AUDIO_FORMAT_AC3;
        } else {
            dec_raw_data->sub_format = aml_dec->format;
        }
        dec_raw_data->data_ch     = 2;
        dec_raw_data->data_sr     = ddp_dec->pcm_out_info.sample_rate;
        dec_raw_data->data_len    = ddp_dec->outlen_raw;
    } else {
        ddp_dec->dcv_decoded_errcount++;
    }
    /*it stores the input raw data*/
    if (raw_in_data->data_len > 0) {
        raw_in_data->data_format = aml_dec->format;
        raw_in_data->sub_format  = aml_dec->format;
        raw_in_data->data_sr     = mSample_rate;
        raw_in_data->data_ch     = 2;
        /*we don't support 32k ddp*/
        if (raw_in_data->data_sr == 32000) {
            raw_in_data->data_len = 0;
        }
    }
    ddp_dec->dcv_pcm_writed += ddp_dec->outlen_pcm;
    ddp_dec->dcv_decoded_samples = (ddp_dec->dcv_pcm_writed * 8 ) / (2 * bit_width);

    //sprintf(ddp_dec->sysfs_buf, "decoded_frames %llu", ddp_dec->dcv_decoded_samples);
    //sysfs_set_sysfs_str(REPORT_DECODED_INFO, ddp_dec->sysfs_buf);
    int use_raw_size = ((total_used_size - last_remain_size) > bytes) ? bytes : (total_used_size - last_remain_size);
    ddp_dec->total_raw_size += use_raw_size;

    ddp_dec->remain_size = 0;
    /* Fixme: sometimes here total_used_size - last_remain_size is larger than bytes about 8bytes. */
    ret = ((total_used_size - last_remain_size) > bytes) ? bytes : (total_used_size - last_remain_size);
    return ret;

EXIT:
    return AML_DEC_RETURN_TYPE_FAIL;
}

int dcv_decoder_config(aml_dec_t * aml_dec, aml_dec_config_type_t config_type, aml_dec_config_t * dec_config)
{
    int ret = -1;
    struct dolby_ddp_dec *ddp_dec = (struct dolby_ddp_dec *)aml_dec;

    if (ddp_decoder_config == NULL || handle == NULL) {
        return ret;
    }
    switch (config_type) {
    case AML_DEC_CONFIG_MIXER_LEVEL: {
        int  mixer_level = dec_config->mixer_level;
        if (!dec_config->ad_mixing_enable)
            mixer_level = -32;
        ALOGI("dec_config->mixer_level %d", mixer_level);
        ret = (*ddp_decoder_config)(handle, DDP_CONFIG_MIXER_LEVEL, (ddp_config_t *)&mixer_level);
        break;
    }
    case AML_DEC_CONFIG_AD_VOL: {
        int  advol_level = dec_config->advol_level;
        ALOGI("advol_level %d",advol_level);
        ret = (*ddp_decoder_config)(handle, DDP_CONFIG_AD_PCMSCALE, (ddp_config_t *)&advol_level);
        break;
    }
    case AML_DEC_CONFIG_MIXING_ENABLE: {
        int  mixer_level = dec_config->mixer_level;
        if (!dec_config->ad_mixing_enable)
            mixer_level = -32;
        ALOGI("dec_config->mixer_level %d",mixer_level);
        ret = (*ddp_decoder_config)(handle, DDP_CONFIG_MIXER_LEVEL, (ddp_config_t *)&mixer_level);
        break;
    }
    default:
        ALOGI("config_type %d not supported", config_type);
    }

    return ret;
}

int dcv_decoder_info(aml_dec_t *aml_dec, aml_dec_info_type_t info_type, aml_dec_info_t * dec_info)
{
    int ret = -1;
    struct dolby_ddp_dec *ddp_dec = (struct dolby_ddp_dec *)aml_dec;

    switch (info_type) {
    case AML_DEC_REMAIN_SIZE:
        dec_info->remain_size = ddp_dec->remain_size;
        return 0;
    case AML_DEC_STREMAM_INFO:
        memset(&dec_info->dec_info, 0x00, sizeof(aml_dec_stream_info_t));
        memcpy(&dec_info->dec_info, &ddp_dec->stream_info, sizeof(aml_dec_stream_info_t));
        if (ddp_dec->stream_info.stream_sr != 0 && ddp_dec->total_time < CALCULATE_BITRATE_NEED_TIME) { //we only calculate bitrate in the first five minutes
            ddp_dec->total_time = ddp_dec->dcv_decoded_samples/ddp_dec->stream_info.stream_sr;
            if (ddp_dec->total_time != 0) {
                ddp_dec->bit_rate = (int)(ddp_dec->total_raw_size/ddp_dec->total_time);
            }
        }
        dec_info->dec_info.stream_bitrate = ddp_dec->bit_rate;
        return 0;
    default:
        break;
    }
    return ret;
}

int IndependentFrame_Acmod_Lfeon_to_ChannelMask(short Acmod, short Lfeon) {
    int Channel_mask = 0;
    switch (Acmod) {
        case 0:
            Channel_mask = AUDIO_CHANNEL_MONO_1 | AUDIO_CHANNEL_MONO_2;
            break;
        case 1:
            Channel_mask = AUDIO_CHANNEL_C;
            break;
        case 2:
            Channel_mask = AUDIO_CHANNEL_L | AUDIO_CHANNEL_R;
            break;
        case 3:
            Channel_mask = AUDIO_CHANNEL_L | AUDIO_CHANNEL_C | AUDIO_CHANNEL_R;
            break;
        case 4:
            Channel_mask = AUDIO_CHANNEL_L | AUDIO_CHANNEL_R | AUDIO_CHANNEL_S;
            break;
        case 5:
            Channel_mask = AUDIO_CHANNEL_L | AUDIO_CHANNEL_C | AUDIO_CHANNEL_R | AUDIO_CHANNEL_S;
            break;
        case 6:
            Channel_mask = AUDIO_CHANNEL_L | AUDIO_CHANNEL_R | AUDIO_CHANNEL_LS | AUDIO_CHANNEL_RS;
            break;
        case 7:
            Channel_mask = AUDIO_CHANNEL_L | AUDIO_CHANNEL_C | AUDIO_CHANNEL_R | AUDIO_CHANNEL_LS | AUDIO_CHANNEL_RS;
            break;
        default:
            ALOGE("error");
            break;
    }
    if (Lfeon == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_LFE;
    }
    return Channel_mask;
}

int DependentFrame_Chanmap_to_ChannelMask(short Chanmap) {
    int Channel_mask = 0;
    if (((Chanmap >> 15) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_L;
    }
    if (((Chanmap >> 14) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_C;
    }
    if (((Chanmap >> 13) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_R;
    }
    if (((Chanmap >> 12) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_LS;
    }
    if (((Chanmap >> 11) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_RS;
    }
    if (((Chanmap >> 10) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_Lc | AUDIO_CHANNEL_Rc;
    }
    if (((Chanmap >> 9) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_Lrs | AUDIO_CHANNEL_Rrs;
    }
    if (((Chanmap >> 8) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_Cs;
    }
    if (((Chanmap >> 7) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_Ts;
    }
    if (((Chanmap >> 6) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_Lsd | AUDIO_CHANNEL_Rsd;
    }
    if (((Chanmap >> 5) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_Lw | AUDIO_CHANNEL_Rw;
    }
    if (((Chanmap >> 4) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_Vhl | AUDIO_CHANNEL_Vhr;
    }
    if (((Chanmap >> 3) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_Vhc;
    }
    if (((Chanmap >> 2) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_Lts | AUDIO_CHANNEL_Rts;
    }
    if (((Chanmap >> 1) & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_LFE2;
    }
    if ((Chanmap & 0x1) == 1) {
        Channel_mask = Channel_mask | AUDIO_CHANNEL_LFE;
    }
    return Channel_mask;
}

int parse_report_info_samplerate_channelnum (unsigned char *read_pointer, struct dolby_ddp_dec *ddp_dec, int mFrame_size) {
    int ChannelNum_temp = 0;
    int SampleRate = 0;
    int SampleRate_temp = 0;
    int channel_mask_temp = 0;
    int FrameSize = 0;
    int is_DependentFrame = 0;
    int is__aml_eac3 = 0;
    int isAdStream = 0;
    unsigned char *ReadPointer = NULL;
    ReadPointer = (unsigned char *)malloc(PTR_HEAD_SIZE);
    if (ReadPointer == NULL) {
        ALOGE("Failed to malloc ReadPointer");
        return -1;
    }
    for (int i = 0; i < mFrame_size - PTR_HEAD_SIZE - 1; i++) {
        if ((read_pointer[i] == 0x0b && read_pointer[i + 1] == 0x77) || (read_pointer[i] == 0x77 && read_pointer[i + 1] == 0x0b)) {
            int offset_temp = i;
            for (int j = 0; j < PTR_HEAD_SIZE; j++) {
                ReadPointer[j] = read_pointer[offset_temp];
                offset_temp = offset_temp + 1;
            }
            Get_Parameters(ReadPointer, &SampleRate, &FrameSize, &channel_mask_temp, &is__aml_eac3, &is_DependentFrame, &isAdStream);

            SampleRate_temp = ddp_dec->Sample_Rate;
            ddp_dec->Sample_Rate = SampleRate;
            if (SampleRate_temp != ddp_dec->Sample_Rate) {
                ddp_dec->Same_SampleRate_Count = 0;
            }
            if ((SampleRate_temp == ddp_dec->Sample_Rate) && (ddp_dec->Same_SampleRate_Count < UPDATE_THRESHOLD_MAX)) {
                ddp_dec->Same_SampleRate_Count ++;
            }

            if (is_DependentFrame == 0) {
                ddp_dec->channel_mask_independent_frame = channel_mask_temp;
            } else {
                ddp_dec->channel_mask_dependent_frame = channel_mask_temp;
            }

            ddp_dec->Frame_Count ++;
            if (ddp_dec->Frame_Count == 2) {
                ddp_dec->channel_mask_all = ddp_dec->channel_mask_independent_frame | ddp_dec->channel_mask_dependent_frame;
                ChannelNum_temp = ddp_dec->ChannelNum;
                ddp_dec->ChannelNum = popcount(ddp_dec->channel_mask_all);

                if ((ChannelNum_temp != ddp_dec->ChannelNum) ) {
                    ddp_dec->Same_ChNum_Count = 0;
                }

                if ((ChannelNum_temp == ddp_dec->ChannelNum) && (ddp_dec->Same_ChNum_Count < UPDATE_THRESHOLD_MAX)) {
                    ddp_dec->Same_ChNum_Count++;
                }

                if ((ddp_dec->Same_ChNum_Count > UPDATE_THRESHOLD) && (ddp_dec->Same_SampleRate_Count > UPDATE_THRESHOLD) ) {
                    ddp_dec->stream_info.stream_ch = ddp_dec->ChannelNum;
                    ddp_dec->stream_info.stream_sr = ddp_dec->Sample_Rate;
                    ALOGI("ddp_dec->ChannelNum = %d, ddp_dec->Sample_Rate = %d", ddp_dec->ChannelNum, ddp_dec->Sample_Rate);
                }

                ddp_dec->Frame_Count = 0;
                ddp_dec->channel_mask_independent_frame = 0;
                ddp_dec->channel_mask_dependent_frame = 0;
            }

        }

    }

    if (ReadPointer) {
        free(ReadPointer);
        ReadPointer = NULL;
    }

    return 0;

}


aml_dec_func_t aml_dcv_func = {
    .f_init                 = dcv_decoder_init_patch,
    .f_release              = dcv_decoder_release_patch,
    .f_process              = dcv_decoder_process_patch,
    .f_config               = dcv_decoder_config,
    .f_info                 = dcv_decoder_info,
};


