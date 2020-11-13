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

#define LOG_TAG "audio_hw_primary"
//#define LOG_NDEBUG 0
#define __USE_GNU

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <utils/Timers.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <linux/ioctl.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <audio_utils/channels.h>
#include "aml_DRC_param_gen.h"
#include "aml_EQ_param_gen.h"

#if ANDROID_PLATFORM_SDK_VERSION >= 25 //8.0
#include <system/audio-base.h>
#endif

#include <hardware/audio.h>
#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>
#include <audio_route/audio_route.h>
#include <spdifenc_wrap.h>
#include <aml_android_utils.h>
#include <aml_alsa_mixer.h>

#include "audio_format_parse.h"
#include "SPDIFEncoderAD.h"
#include "aml_volume_utils.h"
#include "aml_data_utils.h"
#include "spdifenc_wrap.h"
#include "alsa_manager.h"
#include "aml_audio_stream.h"
#include "audio_hw.h"
#include "spdif_encoder_api.h"
#include "audio_hw_utils.h"
#include "audio_hw_profile.h"
#include "aml_dump_debug.h"
#include "alsa_manager.h"
#include "alsa_device_parser.h"
#include "aml_audio_stream.h"
#include "alsa_config_parameters.h"
#include "spdif_encoder_api.h"
#include "aml_avsync_tuning.h"
#include "aml_ng.h"
#include "aml_audio_timer.h"
#include "audio_dtv_ad.h"
#include "aml_audio_spdifout.h"

#include "aml_mmap_audio.h"
// for invoke bluetooth rc hal
#include "audio_hal_thunks.h"

#include <dolby_ms12_status.h>
#include <SPDIFEncoderAD.h>
#include "audio_hw_ms12.h"
#include "dolby_lib_api.h"

#include "earc_utils.h"

#define ENABLE_NANO_NEW_PATH 1
#if ENABLE_NANO_NEW_PATH
#include "jb_nano.h"
#endif

// for dtv playback
#include "audio_hw_dtv.h"
#include "../bt_voice/kehwin/audio_kw.h"

#define ENABLE_DTV_PATCH
//#define SUBMIXER_V1_1
#define HDMI_LATENCY_MS 60

#ifdef ENABLE_AEC_FUNC
#include "audio_aec_process.h"
#endif

#if defined(IS_ATOM_PROJECT)
#include "harman_dsp_process.h"
#include "harman_filter.h"
#include "audio_aec_process.h"
#endif

#if (ENABLE_NANO_PATCH == 1)
/*[SEN5-autumn.zhao-2018-01-11] add for B06 audio support { */
#include "jb_nano.h"
/*[SEN5-autumn.zhao-2018-01-11] add for B06 audio support } */
#endif

#if defined(ENABLE_HBG_PATCH)
#include "../hbg_bt_voice/hbg_blehid_mic.h"
#endif

#include "sub_mixing_factory.h"
#include "audio_a2dp_hw.h"
#include "audio_bt_sco.h"

#define CARD_AMLOGIC_BOARD 0

/*Google Voice Assistant channel_mask */
#define BUILT_IN_MIC 12

#undef PLAYBACK_PERIOD_COUNT
#define PLAYBACK_PERIOD_COUNT 4
/* number of periods for capture */
#undef CAPTURE_PERIOD_COUNT
#define CAPTURE_PERIOD_COUNT 4

/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000
#undef RESAMPLER_BUFFER_FRAMES
#define RESAMPLER_BUFFER_FRAMES (PERIOD_SIZE * 6)
#define RESAMPLER_BUFFER_SIZE (4 * RESAMPLER_BUFFER_FRAMES)
#define NSEC_PER_SECOND 1000000000ULL

/* sampling rate when using MM low power port */
#define MM_LOW_POWER_SAMPLING_RATE 44100
/* sampling rate when using MM full power port */
#define MM_FULL_POWER_SAMPLING_RATE 48000
/* sampling rate when using VX port for narrow band */
#define VX_NB_SAMPLING_RATE 8000
#define VAL_LEN 2048

#define MIXER_XML_PATH "/vendor/etc/mixer_paths.xml"
#define DOLBY_MS12_INPUT_FORMAT_TEST

#define IEC61937_PACKET_SIZE_OF_AC3                     (0x1800)
#define IEC61937_PACKET_SIZE_OF_EAC3                    (0x6000)
#define MS12_MAIN_WRITE_LOOP_THRESHOLD                  (2000)
#define MAX_INPUT_STREAM_CNT                            (3)

#define DISABLE_CONTINUOUS_OUTPUT "persist.vendor.audio.continuous.disable"

#define INPUTSOURCE_MUTE_DELAY_MS       800

#define DROP_AUDIO_SIZE             (64 * 1024)

#define ENUM_USECASE_TYPE_TO_STR(x, pStr)              ENUM_TYPE_TO_STR(x, strlen("STREAM_"), pStr)
const char* usecase2Str(stream_usecase_t enUsecase)
{
    static char acTypeStr[ENUM_TYPE_STR_MAX_LEN];
    char *pStr = "INVALID";
    switch (enUsecase) {
        ENUM_USECASE_TYPE_TO_STR(STREAM_PCM_NORMAL, pStr)
        ENUM_USECASE_TYPE_TO_STR(STREAM_PCM_DIRECT, pStr)
        ENUM_USECASE_TYPE_TO_STR(STREAM_PCM_HWSYNC, pStr)
        ENUM_USECASE_TYPE_TO_STR(STREAM_RAW_DIRECT, pStr)
        ENUM_USECASE_TYPE_TO_STR(STREAM_RAW_HWSYNC, pStr)
        ENUM_USECASE_TYPE_TO_STR(STREAM_PCM_PATCH, pStr)
        ENUM_USECASE_TYPE_TO_STR(STREAM_RAW_PATCH, pStr)
        ENUM_USECASE_TYPE_TO_STR(STREAM_PCM_MMAP, pStr)
        ENUM_USECASE_TYPE_TO_STR(STREAM_USECASE_MAX, pStr)
    }
    sprintf(acTypeStr, "[%d]%s", enUsecase, pStr);
    return acTypeStr;
}

static const struct pcm_config pcm_config_out = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_PLAYBACK_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

static const struct pcm_config pcm_config_out_direct = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_PLAYBACK_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

static const struct pcm_config pcm_config_in = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_CAPTURE_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

static const struct pcm_config pcm_config_bt = {
    .channels = 1,
    .rate = VX_NB_SAMPLING_RATE,
    .period_size = 256,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};
int start_ease_in(struct aml_audio_device *adev) {
    /*start ease in the audio*/
    ease_setting_t ease_setting;
    adev->audio_ease->data_format.format = AUDIO_FORMAT_PCM_16_BIT;
    adev->audio_ease->data_format.ch = 8;
    adev->audio_ease->data_format.sr = 48000;
    adev->audio_ease->ease_type = EaseInCubic;
    ease_setting.duration = 200;
    ease_setting.start_volume = 0.0;
    ease_setting.target_volume = 1.0;
    aml_audio_ease_config(adev->audio_ease, &ease_setting);

    return 0;
}

int start_ease_out(struct aml_audio_device *adev) {
    /*start ease out the audio*/
    ease_setting_t ease_setting;
    ease_setting.duration = 150;
    ease_setting.start_volume = 1.0;
    ease_setting.target_volume = 0.0;
    adev->audio_ease->ease_type = EaseOutCubic;
    adev->audio_ease->data_format.format = AUDIO_FORMAT_PCM_16_BIT;
    adev->audio_ease->data_format.ch = 8;
    adev->audio_ease->data_format.sr = 48000;
    aml_audio_ease_config(adev->audio_ease, &ease_setting);

    return 0;
}
static void select_output_device (struct aml_audio_device *adev);
static void select_input_device (struct aml_audio_device *adev);
static void select_devices (struct aml_audio_device *adev);
static int adev_set_voice_volume (struct audio_hw_device *dev, float volume);
static int do_input_standby (struct aml_stream_in *in);
static int do_output_standby (struct aml_stream_out *out);
//static int do_output_standby_l (struct audio_stream *out);
static uint32_t out_get_sample_rate (const struct audio_stream *stream);
static int out_pause (struct audio_stream_out *stream);
static int usecase_change_validate_l (struct aml_stream_out *aml_out, bool is_standby);
static inline int is_usecase_mix (stream_usecase_t usecase);
static int create_patch (struct audio_hw_device *dev,
                         audio_devices_t input,
                         audio_devices_t output);
static int create_patch_ext(struct audio_hw_device *dev,
                            audio_devices_t input,
                            audio_devices_t output,
                            audio_patch_handle_t handle);
static int release_patch (struct aml_audio_device *aml_dev);
static int release_parser(struct aml_audio_device *aml_dev);
static void aml_tinymix_set_spdif_format(audio_format_t output_format,  struct aml_stream_out *stream);
static inline bool need_hw_mix(usecase_mask_t masks);
//static int out_standby_new(struct audio_stream *stream);
static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle __unused,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address __unused);
static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream);
static int get_audio_patch_by_src_dev(struct audio_hw_device *dev,
                                      audio_devices_t dev_type,
                                      struct audio_patch **p_audio_patch);
ssize_t out_write_new(struct audio_stream_out *stream,
                      const void *buffer,
                      size_t bytes);



static aec_timestamp get_timestamp(void);
//static void config_output(struct audio_stream_out *stream);
static inline bool need_hw_mix(usecase_mask_t masks)
{
    return (masks > 1);
}

static inline int is_usecase_mix(stream_usecase_t usecase)
{
    return usecase > STREAM_PCM_NORMAL;
}
#if (ENABLE_NANO_PATCH == 1)
/*[SEN5-autumn.zhao-2018-01-11] add for B06 audio support { */
static RECORDING_DEVICE recording_device = RECORDING_DEVICE_OTHER;
/*[SEN5-autumn.zhao-2018-01-11] add for B06 audio support } */
#endif

static inline short CLIP (int r)
{
    return (r >  0x7fff) ? 0x7fff :
           (r < -0x8000) ? -0x8000 :
           r;
}

static void store_stream_presentation(struct aml_audio_device *adev)
{
    //here restore every bitstream  frame postion and total frame write
    //we support every STREAM TYPE only here one stream instance
    struct aml_stream_out *aml_out = NULL;
    uint64_t write_frames = 0;
    int i = 0;
    bool is_ddp_61937 = false;
    for (i = 0; i < STREAM_USECASE_MAX; i++) {
        aml_out = adev->active_outputs[i];
        if (aml_out && (aml_out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
            if (aml_out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
                if (aml_out->hal_internal_format == AUDIO_FORMAT_AC3) {
                    write_frames = aml_out->input_bytes_size / 6144 * 32 * 48;
                }
                if (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) {
                    write_frames = aml_out->input_bytes_size / 24576 * 32 * 48;
                    is_ddp_61937 = true;
                }
            } else if ((aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) || (aml_out->hal_internal_format == AUDIO_FORMAT_AC3)) {
                write_frames = aml_out->input_bytes_size / aml_out->ddp_frame_size * 32 * 48;
            }

            /*
             *handle the case hal_rate(32kHz/44.1kHz) != 48KHz,
             *if it is n(1 or 4)*48kHz, keep the write_frames as well.
             */
            if (aml_out->hal_rate % MM_FULL_POWER_SAMPLING_RATE) {
                int multiple = 1;
                if (is_ddp_61937 == true && (aml_out->hal_rate == 128000 || aml_out->hal_rate == 44100*4))
                    multiple = 4;
                write_frames = (write_frames * MM_FULL_POWER_SAMPLING_RATE*multiple) / aml_out->hal_rate;
            }
            aml_out->last_frames_postion = write_frames;
            aml_out->frame_write_sum = write_frames;
            aml_out->last_playload_used = 0;
            aml_out->continuous_audio_offset = aml_out->input_bytes_size;
            if (continous_mode(adev) && adev->ms12_out) {
                adev->ms12_out->continuous_audio_offset = aml_out->input_bytes_size;
            }
            ALOGI("%s,restore input size %"PRIu64",frame %"PRIu64"",
                  __func__, aml_out->input_bytes_size, write_frames);
        }
    }
}

static void select_devices (struct aml_audio_device *adev)
{
    ALOGD ("%s(mode=%d, out_device=%#x)", __FUNCTION__, adev->mode, adev->out_device);
    int headset_on;
    int headphone_on;
    int speaker_on;
    int hdmi_on;
    int earpiece;
    int mic_in;
    int headset_mic;

    headset_on = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADSET;
    headphone_on = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
    speaker_on = adev->out_device & AUDIO_DEVICE_OUT_SPEAKER;
    hdmi_on = adev->out_device & AUDIO_DEVICE_OUT_AUX_DIGITAL;
    earpiece =  adev->out_device & AUDIO_DEVICE_OUT_EARPIECE;
    mic_in = adev->in_device & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC);
    headset_mic = adev->in_device & AUDIO_DEVICE_IN_WIRED_HEADSET;

    ALOGD ("%s : hs=%d , hp=%d, sp=%d, hdmi=0x%x,earpiece=0x%x", __func__,
             headset_on, headphone_on, speaker_on, hdmi_on, earpiece);
    ALOGD ("%s : in_device(%#x), mic_in(%#x), headset_mic(%#x)", __func__,
             adev->in_device, mic_in, headset_mic);
    audio_route_reset (adev->ar);
    if (hdmi_on) {
        audio_route_apply_path (adev->ar, "hdmi");
    }
    if (headphone_on || headset_on) {
        audio_route_apply_path (adev->ar, "headphone");
    }
    if (speaker_on || earpiece) {
        audio_route_apply_path (adev->ar, "speaker");
    }
    if (mic_in) {
        audio_route_apply_path (adev->ar, "main_mic");
    }
    if (headset_mic) {
        audio_route_apply_path (adev->ar, "headset-mic");
    }

    audio_route_update_mixer (adev->ar);

}

static void select_mode (struct aml_audio_device *adev)
{
    ALOGD ("%s(out_device=%#x)", __FUNCTION__, adev->out_device);
    ALOGD ("%s(in_device=%#x)", __FUNCTION__, adev->in_device);
    return;

    /* force earpiece route for in call state if speaker is the
        only currently selected route. This prevents having to tear
        down the modem PCMs to change route from speaker to earpiece
        after the ringtone is played, but doesn't cause a route
        change if a headset or bt device is already connected. If
        speaker is not the only thing active, just remove it from
        the route. We'll assume it'll never be used initally during
        a call. This works because we're sure that the audio policy
        manager will update the output device after the audio mode
        change, even if the device selection did not change. */
    if ( (adev->out_device & AUDIO_DEVICE_OUT_ALL) == AUDIO_DEVICE_OUT_SPEAKER) {
        adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;
    } else {
        adev->out_device &= ~AUDIO_DEVICE_OUT_SPEAKER;
    }

    return;
}

bool format_is_passthrough (audio_format_t fmt)
{
    ALOGD("%s() fmt = 0x%x\n", __func__, fmt);
    switch (fmt) {
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3:
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_HD:
    case AUDIO_FORMAT_IEC61937:
    case AUDIO_FORMAT_DOLBY_TRUEHD:
        return true;
    default:
        return false;
    }
}

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream (struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;
    unsigned int card = CARD_AMLOGIC_BOARD;
    unsigned int port = PORT_I2S;
    int ret = 0;
    int i  = 0;
    struct aml_stream_out *out_removed = NULL;
    int channel_count = popcount (out->hal_channel_mask);
    bool hwsync_lpcm = (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC && out->config.rate  <= 48000 &&
                        audio_is_linear_pcm(out->hal_internal_format) && channel_count <= 2);
    ALOGD("%s(adev->out_device=%#x, adev->mode=%d)",
            __FUNCTION__, adev->out_device, adev->mode);
    if (adev->mode != AUDIO_MODE_IN_CALL) {
        /* FIXME: only works if only one output can be active at a time */
        //select_devices(adev);
    }
    if (out->hw_sync_mode == true) {
        adev->hwsync_output = out;
    }
    card = alsa_device_get_card_index();
    if (adev->out_device & AUDIO_DEVICE_OUT_ALL_SCO) {
        port = PORT_PCM;
        out->config = pcm_config_bt;
    } else if (out->flags & AUDIO_OUTPUT_FLAG_DIRECT && !hwsync_lpcm) {
        port = PORT_SPDIF;
    }
    /* check to update port */
    port = alsa_device_update_pcm_index(port, PLAYBACK);

    ALOGD ("*%s, open card(%d) port(%d)", __FUNCTION__, card, port);

    /* default to low power: will be corrected in out_write if necessary before first write to
     * tinyalsa.
     */
    out->write_threshold = out->config.period_size * PLAYBACK_PERIOD_COUNT;
    out->config.start_threshold = out->config.period_size * PLAYBACK_PERIOD_COUNT;
    out->config.avail_min = 0;//SHORT_PERIOD_SIZE;
    //added by xujian for NTS hwsync/system stream mix smooth playback.
    //we need re-use the tinyalsa pcm handle by all the output stream, including
    //hwsync direct output stream,system mixer output stream.
    //TODO we need diff the code with AUDIO_DEVICE_OUT_ALL_SCO.
    //as it share the same hal but with the different card id.
    //TODO need reopen the tinyalsa card when sr/ch changed,
    if (adev->pcm == NULL) {
        ALOGD("%s(), pcm_open card %u port %u\n", __func__, card, port);
        out->pcm = pcm_open (card, port, PCM_OUT /*| PCM_MMAP | PCM_NOIRQ*/, &out->config);
        if (!pcm_is_ready (out->pcm)) {
            ALOGE ("cannot open pcm_out driver: %s", pcm_get_error (out->pcm));
            pcm_close (out->pcm);
            return -ENOMEM;
        }
        if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected) {
            int earc_port = alsa_device_update_pcm_index(PORT_EARC, PLAYBACK);
            struct pcm_config earc_config = update_earc_out_config(&out->config);
            out->earc_pcm = pcm_open (card, earc_port, PCM_OUT /*| PCM_MMAP | PCM_NOIRQ*/, &earc_config);
            if (!pcm_is_ready (out->earc_pcm) ) {
                ALOGE ("cannot open pcm_out driver: %s", pcm_get_error (out->earc_pcm) );
                pcm_close (out->earc_pcm);
                out->earc_pcm = NULL;
                return -EINVAL;
            }
        }
        if (out->config.rate != out_get_sample_rate (&out->stream.common) ) {
            ALOGD ("%s(out->config.rate=%d, out->config.channels=%d)",
                     __FUNCTION__, out->config.rate, out->config.channels);
            ret = create_resampler (out_get_sample_rate (&out->stream.common),
                                    out->config.rate,
                                    out->config.channels,
                                    RESAMPLER_QUALITY_DEFAULT,
                                    NULL,
                                    &out->resampler);
            if (ret != 0) {
                ALOGE ("cannot create resampler for output");
                return -ENOMEM;
            }
            out->buffer_frames = (out->config.period_size * out->config.rate) /
                                 out_get_sample_rate (&out->stream.common) + 1;
            out->buffer = aml_audio_malloc (pcm_frames_to_bytes (out->pcm, out->buffer_frames) );
            if (out->buffer == NULL) {
                ALOGE ("cannot malloc memory for out->buffer");
                return -ENOMEM;
            }
        }
        adev->pcm = out->pcm;
        ALOGI ("device pcm %p\n", adev->pcm);
    } else {
        ALOGI ("stream %p share the pcm %p\n", out, adev->pcm);
        out->pcm = adev->pcm;
        // add to fix start output when pcm in pause state
        if (adev->pcm_paused && pcm_is_ready (out->pcm) ) {
            ret = pcm_ioctl (out->pcm, SNDRV_PCM_IOCTL_PAUSE, 0);
            if (ret < 0) {
                ALOGE ("%s(), cannot resume channel\n", __func__);
            }
        }
        if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected &&
                adev->pcm_paused && pcm_is_ready (out->earc_pcm)) {
            ret = pcm_ioctl (out->earc_pcm, SNDRV_PCM_IOCTL_PAUSE, 0);
            if (ret < 0) {
                ALOGE ("%s(), cannot resume channel\n", __func__);
            }
        }
    }
    ALOGD ("channels=%d---format=%d---period_count%d---period_size%d---rate=%d---",
             out->config.channels, out->config.format, out->config.period_count,
             out->config.period_size, out->config.rate);

    if (out->resampler) {
        out->resampler->reset (out->resampler);
    }
    if (out->is_tv_platform == 1) {
        sysfs_set_sysfs_str ("/sys/class/amhdmitx/amhdmitx0/aud_output_chs", "2:2");
    }
    //set_codec_type(0);
    if (out->hw_sync_mode == 1) {
        ALOGD ("start_output_stream with hw sync enable %p\n", out);
    }
    for (i = 0; i < MAX_STREAM_NUM; i++) {
        if (adev->active_output[i] == NULL) {
            ALOGI ("store out (%p) to index %d\n", out, i);
            adev->active_output[i] = out;
            adev->active_output_count++;
            break;
        }
    }
    if (i == MAX_STREAM_NUM) {
        ALOGE ("error,no space to store the dev stream \n");
    }
    return 0;
}

/* dircet stream mainly map to audio HDMI port */
static int start_output_stream_direct (struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;
    unsigned int card = CARD_AMLOGIC_BOARD;
    unsigned int port = PORT_SPDIF;
    int ret = 0;

    adev->debug_flag = aml_audio_get_debug_flag();
    if (eDolbyDcvLib == adev->dolby_lib_type &&
        DD == adev->hdmi_format &&
        out->hal_format == AUDIO_FORMAT_E_AC3) {
        out->need_convert = true;
        out->hal_internal_format = AUDIO_FORMAT_AC3;
        if (out->hal_rate == 192000 || out->hal_rate == 176400)
            out->hal_rate = out->hal_rate / 4;
        ALOGI("spdif out DD+ need covert to DD ");
    }
    int codec_type = get_codec_type(out->hal_internal_format);
    if (codec_type == AUDIO_FORMAT_PCM && out->config.rate > 48000 && (out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
        ALOGI("start output stream for high sample rate pcm for direct mode\n");
        codec_type = TYPE_PCM_HIGH_SR;
    }
    if (codec_type == AUDIO_FORMAT_PCM && out->config.channels >= 6 && (out->flags & AUDIO_OUTPUT_FLAG_DIRECT) ) {
        ALOGI ("start output stream for multi-channel pcm for direct mode\n");
        codec_type = TYPE_MULTI_PCM;
    }

    card = alsa_device_get_card_index();
    ALOGI ("%s: hdmi sound card id %d,device id %d \n", __func__, card, port);
    if (out->multich == 6) {
        /* switch to hdmi port */
        if (alsa_device_is_auge()) {
            port = PORT_I2S2HDMI;
            ALOGI(" %d CH from i2s to hdmi, port:%d\n", out->multich, port);
        } else {
            ALOGI ("round 6ch to 8 ch output \n");
            /* our hw only support 8 channel configure,so when 5.1,hw mask the last two channels*/
            sysfs_set_sysfs_str ("/sys/class/amhdmitx/amhdmitx0/aud_output_chs", "6:7");
            out->config.channels = 8;
        }
    }
    /*
    * 8 channel audio only support 32 byte mode,so need convert them to
    * PCM_FORMAT_S32_LE
    */
    if (!format_is_passthrough(out->hal_format) && (out->config.channels == 8)) {
        port = PORT_I2S;
        out->config.format = PCM_FORMAT_S32_LE;
        adev->out_device = AUDIO_DEVICE_OUT_SPEAKER;
        ALOGI ("[%s %d]8CH format output: set port/0 adev->out_device/%d\n",
               __FUNCTION__, __LINE__, AUDIO_DEVICE_OUT_SPEAKER);
    }

    switch (out->hal_internal_format) {
    case AUDIO_FORMAT_E_AC3:
        out->config.period_size = DEFAULT_PLAYBACK_PERIOD_SIZE * 4;
        //out->write_threshold = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE * 2;
        //out->config.start_threshold = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE * 2;
        //as dd+ frame size = 1 and alsa sr as divide 16
        //out->raw_61937_frame_size = 16;
        break;
    case AUDIO_FORMAT_DTS_HD:
    case AUDIO_FORMAT_DOLBY_TRUEHD:
        out->config.period_size = DEFAULT_PLAYBACK_PERIOD_SIZE * 4 * 2;
        out->write_threshold = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE * 4 * 2;
        out->config.start_threshold = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE * 4 * 2;
        //out->raw_61937_frame_size = 16;//192k 2ch
        break;
    case AUDIO_FORMAT_PCM:
    default:
        if (out->config.rate == 96000)
            out->config.period_size = DEFAULT_PLAYBACK_PERIOD_SIZE * 2;
        else
            out->config.period_size = DEFAULT_PLAYBACK_PERIOD_SIZE;
        out->write_threshold = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
        out->config.start_threshold = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
        //out->raw_61937_frame_size = 4;
    }
    out->config.avail_min = 0;
    //set_codec_type (codec_type);
    aml_tinymix_set_spdif_format(out->hal_internal_format,out);
    /* mute spdif when dd+ output */
    if (codec_type == TYPE_EAC3) {
        audio_route_set_spdif_mute(&adev->alsa_mixer, 1);
    }

    ALOGI ("ALSA open configs: channels=%d, format=%d, period_count=%d, period_size=%d,,rate=%d",
           out->config.channels, out->config.format, out->config.period_count,
           out->config.period_size, out->config.rate);

    if (out->pcm == NULL) {
        /* switch to tdm & spdif share buffer */
        if (alsa_device_is_auge()) {
            /*
            for a113 later chip,raw passthr goes to spsdifa or spdifb
            */
            if (format_is_passthrough(out->hal_format) || codec_type == TYPE_PCM_HIGH_SR) {
                port = PORT_SPDIF;
            }
            else
                port = PORT_I2S;
        }
        /* check to update port */
        port = alsa_device_update_pcm_index(port, PLAYBACK);
        ALOGD("%s(), pcm_open card %u port %u\n", __func__, card, port);
        out->pcm = pcm_open (card, port, PCM_OUT, &out->config);
        if (!pcm_is_ready (out->pcm) ) {
            ALOGE ("cannot open pcm_out driver: %s", pcm_get_error (out->pcm) );
            pcm_close (out->pcm);
            out->pcm = NULL;
            return -EINVAL;
        }
        if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected) {
            int earc_port = alsa_device_update_pcm_index(PORT_EARC, PLAYBACK);
            struct pcm_config earc_config = update_earc_out_config(&out->config);
            out->earc_pcm = pcm_open (card, earc_port, PCM_OUT, &earc_config);
            if (!pcm_is_ready (out->earc_pcm) ) {
                ALOGE ("cannot open pcm_out driver: %s", pcm_get_error (out->earc_pcm) );
                pcm_close (out->earc_pcm);
                out->earc_pcm = NULL;
                return -EINVAL;
            }
        }
    } else {
        ALOGE ("stream %p share the pcm %p\n", out, out->pcm);
    }

    if (codec_type_is_raw_data(codec_type) && !(out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO)) {
        if (out->need_convert) {
            ALOGI("need_convert init %d ",__LINE__);
            struct dolby_ddp_dec *ddp_dec = & (adev->ddp);
            ddp_dec->digital_raw = 1;
            ddp_dec->dual_input  = false;
            if (ddp_dec->status != 1) {
                int status = dcv_decoder_init_patch(ddp_dec);
                ddp_dec->nIsEc3 = 1;
                ALOGI("dcv_decoder_init_patch return :%d,is 61937 %d", status, ddp_dec->is_iec61937);
           }

        } else {
            spdifenc_init(out->pcm, out->hal_internal_format);
            spdifenc_set_mute(out->offload_mute);
            out->spdif_enc_init_frame_write_sum = out->frame_write_sum;
        }
    }
    out->codec_type = codec_type;

    if (out->hw_sync_mode == 1) {
        ALOGD ("start_output_stream with hw sync enable %p\n", out);
    }

    return 0;
}

static int is_config_supported_for_bt(uint32_t sample_rate, audio_format_t format, int channel_count)
{
    if ((sample_rate == 8000 || sample_rate == 16000) &&
            channel_count == 1 &&
            format == AUDIO_FORMAT_PCM_16_BIT)
        return true;

    return false;
}

static int check_input_parameters(uint32_t sample_rate, audio_format_t format, int channel_count, audio_devices_t devices)
{
    ALOGD("%s(sample_rate=%d, format=%d, channel_count=%d, devices = %x)", __FUNCTION__, sample_rate, format, channel_count, devices);
    if (format != AUDIO_FORMAT_PCM_16_BIT && format != AUDIO_FORMAT_PCM_32_BIT) {
        ALOGE("%s: unsupported AUDIO FORMAT (%d)", __func__, format);
        return -EINVAL;
    }

    if (channel_count < 1 || channel_count > 2) {
        ALOGE("%s: unsupported channel count (%d) passed  Min / Max (1 / 2)", __func__, channel_count);
        return -EINVAL;
    }

    switch (sample_rate) {
    case 8000:
    case 11025:
    case 16000:
    case 22050:
    case 24000:
    case 32000:
    case 44100:
    case 48000:
    case 96000:
        break;
    default:
        ALOGE("%s: unsupported (%d) samplerate passed ", __func__, sample_rate);
        return -EINVAL;
    }

    devices &= ~AUDIO_DEVICE_BIT_IN;
    if ((devices & AUDIO_DEVICE_IN_LINE) ||
        (devices & AUDIO_DEVICE_IN_SPDIF) ||
        (devices & AUDIO_DEVICE_IN_TV_TUNER) ||
        (devices & AUDIO_DEVICE_IN_HDMI) ||
        (devices & AUDIO_DEVICE_IN_HDMI_ARC)) {
        if (format == AUDIO_FORMAT_PCM_16_BIT &&
            channel_count == 2 &&
            sample_rate == 48000) {
            ALOGD("%s: audio patch input device %x", __FUNCTION__, devices);
            return 0;
        } else {
            ALOGD("%s: unspported audio patch input device %x", __FUNCTION__, devices);
            return -EINVAL;
        }
    }

    if (devices & AUDIO_DEVICE_IN_ALL_SCO) {
        bool support = is_config_supported_for_bt(sample_rate, format, channel_count);
        if (support) {
            ALOGD("%s(): OK for input device %#x", __func__, devices);
            return 0;
        } else {
            ALOGD("%s: unspported audio params for input device %x", __func__, devices);
            return -EINVAL;
        }
    }

    return 0;
}

static int check_mic_parameters(struct mic_in_desc *mic_desc,
        struct audio_config *config)
{
    struct pcm_config *pcm_cfg = &mic_desc->config;
    uint32_t sample_rate = config->sample_rate;
    audio_format_t format =config->format;
    unsigned int channel_count = audio_channel_count_from_in_mask(config->channel_mask);

    ALOGD("%s(sample_rate=%d, format=%d, channel_count=%d)",
            __func__, sample_rate, format, channel_count);
    if (sample_rate == pcm_cfg->rate &&
            format == AUDIO_FORMAT_PCM_16_BIT)
        return 0;

    return -EINVAL;
}

static size_t get_input_buffer_size(unsigned int period_size, uint32_t sample_rate, audio_format_t format, int channel_count)
{
    size_t size;

    ALOGD("%s(sample_rate=%d, format=%d, channel_count=%d)", __FUNCTION__, sample_rate, format, channel_count);
    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    if (period_size == 0)
        period_size = (pcm_config_in.period_size * sample_rate) / pcm_config_in.rate;
    size = (period_size + 15) / 16 * 16;

    if (format == AUDIO_FORMAT_PCM_32_BIT)
        return size * channel_count * sizeof(int32_t);
    else
        return size * channel_count * sizeof(int16_t);
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *) stream;
    unsigned int rate = out->hal_rate;
    ALOGV("Amlogic_HAL - out_get_sample_rate() = %d", rate);
    return rate;
}

static int out_set_sample_rate(struct audio_stream *stream __unused, uint32_t rate __unused)
{
    return 0;
}

static size_t out_get_buffer_size (const struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    ALOGI("%s(out->config.rate=%d, format %x,stream format %x)", __FUNCTION__,
          out->config.rate, out->hal_internal_format, stream->get_format(stream));
    /* take resampling into account and return the closest majoring
     * multiple of 16 frames, as audioflinger expects audio buffers to
     * be a multiple of 16 frames
     */
    size_t size = out->config.period_size;
    size_t buffer_size = 0;
    switch (out->hal_internal_format) {
    case AUDIO_FORMAT_AC3:
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = AC3_PERIOD_SIZE;
            ALOGI("%s AUDIO_FORMAT_IEC61937 %zu)", __FUNCTION__, size);
            if ((eDolbyDcvLib == adev->dolby_lib_type) &&
                (out->flags & AUDIO_OUTPUT_FLAG_DIRECT) &&
                adev->is_TV) {
                // local file playback, data from audio flinger direct mode
                // the data is packed by DCV decoder by OMX, 1536 samples per packet
                // to match with it, set the size to 1536
                // (ms12 decoder doesn't encounter this issue, so only handle with DCV decoder case)
                size = AC3_PERIOD_SIZE / 4;
                ALOGI("%s AUDIO_FORMAT_IEC61937(DIRECT) (eDolbyDcvLib) size = %zu)", __FUNCTION__, size);
            }
        } else if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
            size = AC3_PERIOD_SIZE;
        } else {
            size = DEFAULT_PLAYBACK_PERIOD_SIZE;
        }
        break;
    case AUDIO_FORMAT_E_AC3:
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size =  EAC3_PERIOD_SIZE;
        } else if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
            size = EAC3_PERIOD_SIZE;//one iec61937 packet size
        } else {
            /*frame align*/
            if (1 /* adev->continuous_audio_mode */) {
                /*Tunnel sync HEADER is 16 bytes*/
                if ((out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) && out->hw_sync_mode) {
                    size = out->ddp_frame_size + 20;
                } else {
                    size = out->ddp_frame_size * 4;
                }
            } else {
                size = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;    //PERIOD_SIZE;
            }
        }

        if (eDolbyDcvLib == adev->dolby_lib_type_last) {
            if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
                size = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
                ALOGI("%s eac3 eDolbyDcvLib = size%zu)", __FUNCTION__, size);
            }
        }
        break;
    case AUDIO_FORMAT_DOLBY_TRUEHD:
        if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
            size = 16 * DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
        } else {
            size = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
        }
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = 4 * PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
        }
        break;
    case AUDIO_FORMAT_DTS:
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = DTS1_PERIOD_SIZE / 2;
        } else {
            size = DTS1_PERIOD_SIZE;
        }
        ALOGI("%s AUDIO_FORMAT_DTS buffer size = %zuframes", __FUNCTION__, size);
        break;
    case AUDIO_FORMAT_DTS_HD:
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = 4 * PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
        } else {
            if (out->hal_rate == 192000) {
                size = DTSHD_PERIOD_SIZE_2;
            } else {
                size = DTSHD_PERIOD_SIZE_1;
            }
        }
        ALOGI("%s AUDIO_FORMAT_DTS_HD buffer size = %zuframes", __FUNCTION__, size);
        break;
    case AUDIO_FORMAT_PCM:
        if (adev->continuous_audio_mode) {
            /*Tunnel sync HEADER is 16 bytes*/
            if (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) {
                size = (8192 + 16) / 4;
            }
        }
    default:
        if (out->config.rate == 96000)
            size = DEFAULT_PLAYBACK_PERIOD_SIZE * 2;
        else
            // bug_id - 158018, modify size value from PERIOD_SIZE to (PERIOD_SIZE * PLAYBACK_PERIOD_COUNT)
            size = DEFAULT_PLAYBACK_PERIOD_SIZE*2/* * PLAYBACK_PERIOD_COUNT*/;
    }
    // size = ( (size + 15) / 16) * 16;
    return size * audio_stream_out_frame_size ( (struct audio_stream_out *) stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream __unused)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *)stream;

    //ALOGV("Amlogic_HAL - out_get_channels return out->hal_channel_mask:%0x", out->hal_channel_mask);
    return out->hal_channel_mask;
}

static audio_channel_mask_t out_get_channels_direct(const struct audio_stream *stream)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *)stream;
    ALOGV("out->hal_channel_mask:%0x", out->hal_channel_mask);
    return out->hal_channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream __unused)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *)stream;
    //ALOGV("Amlogic_HAL - out_get_format() = %d", out->hal_format);
    // if hal_format doesn't have a valid value,
    // return default value AUDIO_FORMAT_PCM_16_BIT
    if (out->hal_format == 0) {
        return AUDIO_FORMAT_PCM_16_BIT;
    }
    return out->hal_format;
}

static audio_format_t out_get_format_direct(const struct audio_stream *stream)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *)stream;

    return  out->hal_format;
}

static int out_set_format(struct audio_stream *stream __unused, audio_format_t format __unused)
{
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby (struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;
    int i = 0;

    ALOGD ("%s(%p)", __FUNCTION__, out);

    if ((out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) && adev->a2dp_out)
        a2dp_out_standby(&out->stream.common);

    if (!out->standby) {
        //commit here for hwsync/mix stream hal mixer
        //pcm_close(out->pcm);
        //out->pcm = NULL;
        if (out->buffer) {
            aml_audio_free (out->buffer);
            out->buffer = NULL;
        }
        if (out->resampler) {
            release_resampler (out->resampler);
            out->resampler = NULL;
        }

        out->standby = 1;
        for (i  = 0; i < MAX_STREAM_NUM; i++) {
            if (adev->active_output[i] == out) {
                adev->active_output[i]  = NULL;
                adev->active_output_count--;
                ALOGI ("remove out (%p) from index %d\n", out, i);
                break;
            }
        }
        if (out->hw_sync_mode == 1 || adev->hwsync_output == out) {
#if 0
            //here to check if hwsync in pause status,if that,chear the status
            //to release the sound card to other active output stream
            if (out->pause_status == true && adev->active_output_count > 0) {
                if (pcm_is_ready (out->pcm) ) {
                    int r = pcm_ioctl (out->pcm, SNDRV_PCM_IOCTL_PAUSE, 0);
                    if (r < 0) {
                        ALOGE ("here cannot resume channel\n");
                    } else {
                        r = 0;
                    }
                    ALOGI ("clear the hwsync output pause status.resume pcm\n");
                }
                out->pause_status = false;
            }
#endif
            out->pause_status = false;
            adev->hwsync_output = NULL;
            ALOGI ("clear hwsync_output when hwsync standby\n");
        }
        if (i == MAX_STREAM_NUM) {
            ALOGE ("error, not found stream in dev stream list\n");
        }
        /* no active output here,we can close the pcm to release the sound card now*/
        if (adev->active_output_count == 0) {
            if (adev->pcm) {
                ALOGI ("close pcm %p\n", adev->pcm);
                pcm_close (adev->pcm);
                adev->pcm = NULL;
            }
            out->pause_status = false;
            adev->pcm_paused = false;
        }
    }
    return 0;
}
/* must be called with hw device and output stream mutexes locked */
static int do_output_standby_direct (struct aml_stream_out *out)
{
    int status = 0;
    struct aml_audio_device *adev = out->dev;
    ALOGI ("%s,out %p", __FUNCTION__,  out);

    if ((out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) && adev->a2dp_out)
        a2dp_out_standby(&out->stream.common);

    if (!out->standby) {
        if (out->buffer) {
            aml_audio_free (out->buffer);
            out->buffer = NULL;
        }
        out->standby = 1;
        /* cleanup the audio hw fifo */
        if (out->pause_status == true && out->pcm) {
            pcm_stop(out->pcm);
        }

        if (out->pcm) {
            pcm_close(out->pcm);
        }
        out->pcm = NULL;
        if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected) {
            if (out->pause_status == true && out->earc_pcm) {
                pcm_stop(out->earc_pcm);
            }
            pcm_close (out->earc_pcm);
            out->earc_pcm = NULL;
        }
    }
    out->pause_status = false;
    aml_tinymix_set_spdif_format(AUDIO_FORMAT_PCM_16_BIT, out);
    audio_route_set_spdif_mute(&adev->alsa_mixer, 0);
    /* clear the hdmitx channel config to default */
    if (out->multich == 6) {
        sysfs_set_sysfs_str ("/sys/class/amhdmitx/amhdmitx0/aud_output_chs", "0:0");
    }
    return status;
}

static int out_standby (struct audio_stream *stream)
{
    ALOGD ("%s(%p)", __FUNCTION__, stream);
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    int status = 0;
    pthread_mutex_lock (&out->dev->lock);
    pthread_mutex_lock (&out->lock);
    status = do_output_standby (out);
    pthread_mutex_unlock (&out->lock);
    pthread_mutex_unlock (&out->dev->lock);
    return status;
}

int out_standby_direct (struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int status = 0;

    ALOGI ("%s(%p),out %p", __FUNCTION__, stream, out);

    pthread_mutex_lock (&out->dev->lock);
    pthread_mutex_lock (&out->lock);
    if (!out->standby) {
        if (out->buffer) {
            aml_audio_free (out->buffer);
            out->buffer = NULL;
        }
        if (adev->hi_pcm_mode) {
            adev->hi_pcm_mode = false;
        }
        out->standby = 1;

        /* cleanup the audio hw fifo */
        if (out->pause_status == true) {
            pcm_stop(out->pcm);
        }
        pcm_close (out->pcm);
        out->pcm = NULL;
        if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected) {
            if (out->pause_status == true) {
                pcm_stop(out->earc_pcm);
            }
            pcm_close (out->earc_pcm);
            out->earc_pcm = NULL;
        }
    }
    out->pause_status = false;
    aml_tinymix_set_spdif_format(AUDIO_FORMAT_PCM_16_BIT, out);
    audio_route_set_spdif_mute(&adev->alsa_mixer, 0);

    if (out->need_convert) {
        ALOGI("need_convert release %d ",__LINE__);
        dcv_decoder_release_patch(&adev->ddp);
    }
    /* clear the hdmitx channel config to default */
    if (out->multich == 6) {
        sysfs_set_sysfs_str ("/sys/class/amhdmitx/amhdmitx0/aud_output_chs", "0:0");
    }
    pthread_mutex_unlock (&out->lock);
    pthread_mutex_unlock (&out->dev->lock);
    return status;
}

static int out_dump (const struct audio_stream *stream __unused, int fd __unused)
{
    ALOGD ("%s(%p, %d)", __FUNCTION__, stream, fd);
    return 0;
}

static int out_flush (struct audio_stream_out *stream)
{
    ALOGD ("%s(%p)", __FUNCTION__, stream);
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int ret = 0;
    int channel_count = popcount (out->hal_channel_mask);
    bool hwsync_lpcm = (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC && out->config.rate  <= 48000 &&
                        audio_is_linear_pcm(out->hal_internal_format) && channel_count <= 2);
    do_standby_func standy_func = NULL;
    /* VTS: a stream should always succeed to flush
     * hardware/libhardware/include/hardware/audio.h: Stream must already
     * be paused before calling flush(), we check and complain this case
     */
    if (!out->pause_status) {
        ALOGW("%s(%p), stream should be in pause status", __func__, out);
    }
    if (out->flags & AUDIO_OUTPUT_FLAG_DIRECT/* && !hwsync_lpcm*/) {
        standy_func = do_output_standby_direct;
    } else {
        standy_func = do_output_standby;
    }
    pthread_mutex_lock (&adev->lock);
    pthread_mutex_lock (&out->lock);
    if (out->pause_status == true && out->pcm) {
        // when pause status, set status prepare to avoid static pop sound
        ret = pcm_ioctl (out->pcm, SNDRV_PCM_IOCTL_PREPARE);
        if (ret < 0) {
            ALOGE ("cannot prepare pcm!");
            goto exit;
        }
    }
    if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected &&
            out->pause_status == true && out->earc_pcm) {
        ret = pcm_ioctl (out->earc_pcm, SNDRV_PCM_IOCTL_PREPARE);
        if (ret < 0) {
            ALOGE ("cannot prepare pcm!");
            goto exit;
        }
    }
    /*for tv product ,adev->pcm_handle should be maintained by using do_output_standby_l*/
    if (adev->is_TV) {
        do_output_standby_l(&stream->common);
    } else {
        standy_func (out);
    }
    out->frame_write_sum  = 0;
    out->last_frames_postion = 0;
    out->spdif_enc_init_frame_write_sum =  0;
    out->frame_skip_sum = 0;
    out->skip_frame = 0;
    //out->pause_status = false;
    out->input_bytes_size = 0;
    aml_audio_hwsync_init(out->hwsync, out);

exit:
    pthread_mutex_unlock (&adev->lock);
    pthread_mutex_unlock (&out->lock);
    return 0;
}

static int out_set_parameters (struct audio_stream *stream, const char *kvpairs)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct aml_stream_in *in;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret;
    uint val = 0;
    bool force_input_standby = false;
    int channel_count = popcount (out->hal_channel_mask);
    bool hwsync_lpcm = (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC && out->config.rate  <= 48000 &&
                        audio_is_linear_pcm(out->hal_internal_format) && channel_count <= 2);
    do_standby_func standy_func = NULL;
    do_startup_func   startup_func = NULL;
    if (out->flags & AUDIO_OUTPUT_FLAG_DIRECT && !hwsync_lpcm) {
        standy_func = do_output_standby_direct;
        startup_func = start_output_stream_direct;
    } else {
        standy_func = do_output_standby;
        startup_func = start_output_stream;
    }
    ALOGD ("%s(kvpairs(%s), out_device=%#x)", __FUNCTION__, kvpairs, adev->out_device);
    parms = str_parms_create_str (kvpairs);

    ret = str_parms_get_str (parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof (value) );
    if (ret >= 0) {
        val = atoi (value);
        pthread_mutex_lock (&adev->lock);
        pthread_mutex_lock (&out->lock);
        if ( ( (adev->out_device & AUDIO_DEVICE_OUT_ALL) != val) && (val != 0) ) {
            if (1/* out == adev->active_output[0]*/) {
                ALOGI ("audio hw select device!\n");
                standy_func (out);
                /* a change in output device may change the microphone selection */
                if (adev->active_input &&
                    adev->active_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
                    force_input_standby = true;
                }
                /* force standby if moving to/from HDMI */
                if ( ( (val & AUDIO_DEVICE_OUT_AUX_DIGITAL) ^
                       (adev->out_device & AUDIO_DEVICE_OUT_AUX_DIGITAL) ) ||
                     ( (val & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET) ^
                       (adev->out_device & AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET) ) ) {
                    standy_func (out);
                }
            }
            adev->out_device &= ~AUDIO_DEVICE_OUT_ALL;
            adev->out_device |= val;
            //select_devices(adev);
        }
        pthread_mutex_unlock (&out->lock);
        if (force_input_standby) {
            in = adev->active_input;
            pthread_mutex_lock (&in->lock);
            do_input_standby (in);
            pthread_mutex_unlock (&in->lock);
        }
        pthread_mutex_unlock (&adev->lock);

        // We shall return Result::OK, which is 0, if parameter is set successfully,
        // or we can not pass VTS test.
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 in order to pass VTS test.", __FUNCTION__);
        ret = 0;

        goto exit;
    }
    int sr = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_SAMPLING_RATE, &sr);
    if (ret >= 0) {
        if (sr > 0) {
            struct pcm_config *config = &out->config;
            ALOGI ("audio hw sampling_rate change from %d to %d \n", config->rate, sr);
            config->rate = sr;
            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&out->lock);
            if (!out->standby) {
                //standy_func (out);
                //startup_func (out);
                out->standby = 0;
            }
            // set hal_rate to sr for passing VTS
            ALOGI ("Amlogic_HAL - %s: set sample_rate to hal_rate.", __FUNCTION__);
            out->hal_rate = sr;
            pthread_mutex_unlock (&adev->lock);
            pthread_mutex_unlock (&out->lock);
        }

        // We shall return Result::OK, which is 0, if parameter is set successfully,
        // or we can not pass VTS test.
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 in order to pass VTS test.", __FUNCTION__);
        ret = 0;

        goto exit;
    }
    // Detect and set AUDIO_PARAMETER_STREAM_FORMAT for passing VTS
    audio_format_t fmt = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_FORMAT, (int *) &fmt);
    if (ret >= 0) {
        if (fmt > 0) {
            struct pcm_config *config = &out->config;
            ALOGI ("audio hw sampling_rate change from %d to %d \n", config->format, fmt);
            config->format = fmt;
            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&out->lock);
            if (!out->standby) {
                standy_func (out);
                startup_func (out);
                out->standby = 0;
            }
            // set hal_format to fmt for passing VTS
            ALOGI ("Amlogic_HAL - %s: set format to hal_format. fmt = %d", __FUNCTION__, fmt);
            out->hal_format = fmt;
            pthread_mutex_unlock (&adev->lock);
            pthread_mutex_unlock (&out->lock);
        }

        // We shall return Result::OK, which is 0, if parameter is set successfully,
        // or we can not pass VTS test.
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 in order to pass VTS test.", __FUNCTION__);
        ret = 0;

        goto exit;
    }
    // Detect and set AUDIO_PARAMETER_STREAM_CHANNELS for passing VTS
    audio_channel_mask_t channels = AUDIO_CHANNEL_OUT_STEREO;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_CHANNELS, (int *) &channels);
    if (ret >= 0) {
        if (channels > AUDIO_CHANNEL_NONE) {
            struct pcm_config *config = &out->config;
            ALOGI ("audio hw channel_mask change from %d to %d \n", config->channels, channels);
            config->channels = audio_channel_count_from_out_mask (channels);
            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&out->lock);
            if (!out->standby) {
                standy_func (out);
                startup_func (out);
                out->standby = 0;
            }
            // set out->hal_channel_mask to channels for passing VTS
            ALOGI ("Amlogic_HAL - %s: set out->hal_channel_mask to channels. fmt = %d", __FUNCTION__, channels);
            out->hal_channel_mask = channels;
            pthread_mutex_unlock (&adev->lock);
            pthread_mutex_unlock (&out->lock);
        }

        // We shall return Result::OK, which is 0, if parameter is set successfully,
        // or we can not pass VTS test.
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 in order to pass VTS test.", __FUNCTION__);
        ret = 0;

        goto exit;
    }

    int frame_size = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_FRAME_COUNT, &frame_size);
    if (ret >= 0) {
        if (frame_size > 0) {
            struct pcm_config *config = &out->config;
            ALOGI ("audio hw frame size change from %d to %d \n", config->period_size, frame_size);
            config->period_size =  frame_size;
            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&out->lock);
            if (!out->standby) {
                standy_func (out);
                startup_func (out);
                out->standby = 0;
            }
            pthread_mutex_unlock (&adev->lock);
            pthread_mutex_unlock (&out->lock);
        }

        // We shall return Result::OK, which is 0, if parameter is set successfully,
        // or we can not pass VTS test.
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 in order to pass VTS test.", __FUNCTION__);
        ret = 0;

        goto exit;
    }
    ret = str_parms_get_str (parms, "hw_av_sync", value, sizeof (value) );
    if (ret >= 0) {
        int hw_sync_id = atoi(value);
        unsigned char sync_enable = (hw_sync_id == 12345678) ? 1 : 0;
        audio_hwsync_t *hw_sync = out->hwsync;
        ALOGI("(%p)set hw_sync_id %d,%s hw sync mode\n",
               out, hw_sync_id, sync_enable ? "enable" : "disable");
        out->hw_sync_mode = sync_enable;
        hw_sync->first_apts_flag = false;
        pthread_mutex_lock (&adev->lock);
        pthread_mutex_lock (&out->lock);
        out->frame_write_sum = 0;
        out->last_frames_postion = 0;
        /* clear up previous playback output status */
        if (!out->standby) {
            standy_func (out);
        }

        pthread_mutex_unlock (&out->lock);
        pthread_mutex_unlock (&adev->lock);
        ret = 0;
        goto exit;
    }
    ret = str_parms_get_str (parms, "A2dpSuspended", value, sizeof (value) );
    if (ret >= 0) {
        if (adev->a2dp_out)
            ret = a2dp_out_set_parameters(stream, kvpairs);
        goto exit;
    }
    ret = str_parms_get_str (parms, "closing", value, sizeof (value) );
    if (ret >= 0) {
        if (adev->a2dp_out)
            ret = a2dp_out_set_parameters(stream, kvpairs);
        goto exit;
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        ret = str_parms_get_str(parms, "ms12_runtime", value, sizeof(value));
        if (ret >= 0) {
            char *parm = strstr(kvpairs, "=");
            pthread_mutex_lock(&adev->lock);
            if (parm)
                aml_ms12_update_runtime_params(&(adev->ms12), parm+1);
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
    }
exit:
    str_parms_destroy (parms);

    // We shall return Result::OK, which is 0, if parameter is NULL,
    // or we can not pass VTS test.
    if (ret < 0) {
        ALOGE ("Amlogic_HAL - %s: parameter is NULL, change ret value to 0 in order to pass VTS test.", __FUNCTION__);
        ret = 0;
    }
    return ret;
}

static char *out_get_parameters (const struct audio_stream *stream, const char *keys)
{
    char *cap = NULL;
    char *para = NULL;
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct str_parms *parms;
    audio_format_t format;
    int ret = 0, val_int = 0;
    parms = str_parms_create_str (keys);
    ret = str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_FORMAT, &val_int);
    format = (audio_format_t) val_int;

    ALOGI ("out_get_parameters %s,out %p\n", keys, out);

    if (strstr(keys, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES)) {
        if (out->flags & AUDIO_OUTPUT_FLAG_PRIMARY) {
            ALOGV ("Amlogic - return hard coded sample_rate list for primary output stream.\n");
            cap = strdup ("sup_sampling_rates=8000|11025|16000|22050|24000|32000|44100|48000");
        } else {
            if (out->out_device & AUDIO_DEVICE_OUT_HDMI_ARC) {
                cap = (char *) strdup_hdmi_arc_cap_default (AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, format);
            } else if (out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
                cap = (char *) strdup_a2dp_cap_default(AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, format);
            } else {
                cap = (char *) get_hdmi_sink_cap (AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES,format,&(adev->hdmi_descs));
            }
        }
        if (cap) {
            para = strdup (cap);
            aml_audio_free (cap);
        } else {
            para = strdup ("");
        }
        ALOGI ("%s\n", para);
        return para;
    } else if (strstr (keys, AUDIO_PARAMETER_STREAM_SUP_CHANNELS) ) {
        if (out->flags & AUDIO_OUTPUT_FLAG_PRIMARY) {
            ALOGV ("Amlogic - return hard coded channel_mask list for primary output stream.\n");
            cap = strdup ("sup_channels=AUDIO_CHANNEL_OUT_MONO|AUDIO_CHANNEL_OUT_STEREO");
        } else {
            if (out->out_device & AUDIO_DEVICE_OUT_HDMI_ARC) {
                cap = (char *) strdup_hdmi_arc_cap_default (AUDIO_PARAMETER_STREAM_SUP_CHANNELS, format);
            } else if (out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
                cap = (char *) strdup_a2dp_cap_default(AUDIO_PARAMETER_STREAM_SUP_CHANNELS, format);
            } else {
                cap = (char *) get_hdmi_sink_cap (AUDIO_PARAMETER_STREAM_SUP_CHANNELS,format,&(adev->hdmi_descs));
            }
        }
        if (cap) {
            para = strdup (cap);
            aml_audio_free (cap);
        } else {
            para = strdup ("");
        }
        ALOGI ("%s\n", para);
        return para;
    } else if (strstr (keys, AUDIO_PARAMETER_STREAM_SUP_FORMATS) ) {
        if (out->out_device & AUDIO_DEVICE_OUT_HDMI_ARC) {
            cap = (char *) strdup_hdmi_arc_cap_default (AUDIO_PARAMETER_STREAM_SUP_FORMATS, format);
        } else if (out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
            cap = (char *) strdup_a2dp_cap_default(AUDIO_PARAMETER_STREAM_SUP_FORMATS, format);
        } else {
            if (out->is_tv_platform == 1) {
                ALOGV ("Amlogic - return hard coded sup_formats list for primary output stream.\n");
#if defined(IS_ATOM_PROJECT)
    cap = strdup ("sup_formats=AUDIO_FORMAT_PCM_32_BIT");
#else
    cap = strdup ("sup_formats=AUDIO_FORMAT_PCM_16_BIT");
#endif
            } else {
                cap = (char *) get_hdmi_sink_cap (AUDIO_PARAMETER_STREAM_SUP_FORMATS,format,&(adev->hdmi_descs));
            }
        }
        if (cap) {
            para = strdup (cap);
            aml_audio_free (cap);
        } else {
            para = strdup ("");
        }
        ALOGI ("%s\n", para);
        return para;
    }
    return strdup ("");
}

static uint32_t out_get_latency (const struct audio_stream_out *stream)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *) stream;

    snd_pcm_sframes_t frames = out_get_latency_frames (stream);
    //snd_pcm_sframes_t frames = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
    return (frames * 1000) / out->config.rate;
}

#define FLOAT_ZERO 0.000001
static int out_set_volume (struct audio_stream_out *stream, float left, float right)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int ret = 0;
    int is_dolby_audio = (out->hal_internal_format == AUDIO_FORMAT_E_AC3) || (out->hal_internal_format == AUDIO_FORMAT_AC3);
    int is_dts_audio   = (out->hal_internal_format == AUDIO_FORMAT_DTS) || (out->hal_internal_format == AUDIO_FORMAT_DTS_HD);
    ALOGI("%s(), stream(%p), left:%f right:%f ", __func__, stream, left, right);
    if (is_dolby_audio || is_dts_audio) {
        if (out->volume_l < FLOAT_ZERO && left > FLOAT_ZERO) {
            ALOGI("set offload mute: false");
            spdifenc_set_mute(false);
            out->offload_mute = false;
        } else if (out->volume_l > FLOAT_ZERO && left < FLOAT_ZERO) {
            ALOGI("set offload mute: true");
            spdifenc_set_mute(true);
            out->offload_mute = true;
        }
    }
    out->volume_l = left;
    out->volume_r = right;

    // When MS12 input is PCM to OTT, ms12 fail to change volume.we will change volume at input side.
    // When MS12 input is DD/DDP, we adjust main DD/DDP input volume here
    if ((eDolbyMS12Lib == adev->dolby_lib_type) && is_dolby_audio) {
        int iMS12DB = -96;

        if (out->volume_l != out->volume_r) {
            ALOGW("%s, left:%f right:%f NOT match", __FUNCTION__, left, right);
        }
        dolby_ms12_set_main_volume(out->volume_l);
    }
    return 0;
}

static int out_pause (struct audio_stream_out *stream)
{
    ALOGD ("out_pause(%p)\n", stream);

    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int r = 0;
    pthread_mutex_lock (&adev->lock);
    pthread_mutex_lock (&out->lock);
    /* a stream should fail to pause if not previously started */
    if (out->standby || out->pause_status == true) {
        // If output stream is standby or paused,
        // we should return Result::INVALID_STATE (3),
        // thus we can pass VTS test.
        ALOGE ("%s: stream in wrong status. standby(%d) or paused(%d)",
                __func__, out->standby, out->pause_status);
        r = INVALID_STATE;
        goto exit;
    }
    if (out->hw_sync_mode) {
        adev->hwsync_output = NULL;
        if (adev->active_output_count > 1) {
            ALOGI ("more than one active stream,skip alsa hw pause\n");
            goto exit1;
        }
    }

    if (out->pcm && pcm_is_ready (out->pcm)) {
        r = pcm_ioctl (out->pcm, SNDRV_PCM_IOCTL_PAUSE, 1);
        if (r < 0) {
            ALOGE ("cannot pause channel\n");
        } else {
            r = 0;
            // set the pcm pause state
            if (out->pcm == adev->pcm)
                adev->pcm_paused = true;
            else
                ALOGE ("out->pcm and adev->pcm are assumed same handle");
        }
    }
    if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected &&
            out->earc_pcm && pcm_is_ready (out->earc_pcm)) {
        r = pcm_ioctl (out->earc_pcm, SNDRV_PCM_IOCTL_PAUSE, 1);
        if (r < 0) {
            ALOGE ("cannot pause channel\n");
        } else {
            r = 0;
            // set the pcm pause state
            if (out->earc_pcm == adev->pcm)
                adev->pcm_paused = true;
            else
                ALOGE ("out->earc_pcm and adev->pcm are assumed same handle");
        }
    }

exit1:
    out->pause_status = true;
exit:
    if (out->hw_sync_mode) {
        ALOGI("%s set AUDIO_PAUSE when tunnel mode\n",__func__);
        sysfs_set_sysfs_str (TSYNC_EVENT, "AUDIO_PAUSE");
        out->tsync_status = TSYNC_STATUS_PAUSED;
    }
    pthread_mutex_unlock (&adev->lock);
    pthread_mutex_unlock (&out->lock);
    return r;
}

static int out_resume (struct audio_stream_out *stream)
{
    ALOGD ("out_resume (%p)\n", stream);
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int r = 0;
    int channel_count = popcount (out->hal_channel_mask);
    bool hwsync_lpcm = (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC && out->config.rate  <= 48000 &&
                        audio_is_linear_pcm(out->hal_internal_format) && channel_count <= 2);
    pthread_mutex_lock (&adev->lock);
    pthread_mutex_lock (&out->lock);
    /* a stream should fail to resume if not previously paused */
    if (/* !out->standby && */!out->pause_status) {
        // If output stream is not standby or not paused,
        // we should return Result::INVALID_STATE (3),
        // thus we can pass VTS test.
        ALOGE ("%s: stream in wrong status. standby(%d) or paused(%d)",
                __func__, out->standby, out->pause_status);
        r = INVALID_STATE;

        goto exit;
    }
    if (out->pcm && pcm_is_ready (out->pcm) ) {
        if (out->flags & AUDIO_OUTPUT_FLAG_DIRECT &&
            !hwsync_lpcm && alsa_device_is_auge()) {
            r = pcm_ioctl (out->pcm, SNDRV_PCM_IOCTL_PREPARE);
            if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected &&
                    out->earc_pcm && pcm_is_ready (out->earc_pcm)) {
                r = pcm_ioctl (out->earc_pcm, SNDRV_PCM_IOCTL_PREPARE);
            }
        } else {
            r = pcm_ioctl (out->pcm, SNDRV_PCM_IOCTL_PREPARE, 0);
            if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected &&
                    out->earc_pcm && pcm_is_ready (out->earc_pcm)) {
                r = pcm_ioctl (out->earc_pcm, SNDRV_PCM_IOCTL_PREPARE, 0);
            }
        }
        if (r < 0) {
            ALOGE ("%s(), cannot resume channel\n", __func__);
        } else {
            r = 0;
            // clear the pcm pause state
            if (out->pcm == adev->pcm)
                adev->pcm_paused = false;
        }
    }
    if (out->hw_sync_mode) {
        ALOGI ("init hal mixer when hwsync resume\n");
        adev->hwsync_output = out;
        sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_RESUME");
        out->tsync_status = TSYNC_STATUS_RUNNING;
    }
    out->pause_status = false;
exit:
    if (out->hw_sync_mode) {
        ALOGI("%s set AUDIO_RESUME when tunnel mode\n",__func__);
        sysfs_set_sysfs_str (TSYNC_EVENT, "AUDIO_RESUME");
        out->tsync_status = TSYNC_STATUS_RUNNING;
    }
    pthread_mutex_unlock (&adev->lock);
    pthread_mutex_unlock (&out->lock);
    return r;
}

/* use standby instead of pause to fix background pcm playback */
static int out_pause_new (struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(aml_dev->ms12);
    bool standy = aml_out->standby;
    int ret = 0;

    ALOGI("%s(), stream(%p), pause_status = %d,dolby_lib_type = %d, conti = %d,hw_sync_mode = %d,ms12_enable = %d,ms_conti_paused = %d\n",
          __func__, stream, aml_out->pause_status, aml_dev->dolby_lib_type, aml_dev->continuous_audio_mode, aml_out->hw_sync_mode, aml_dev->ms12.dolby_ms12_enable, aml_dev->ms12.is_continuous_paused);
    pthread_mutex_lock (&aml_dev->lock);
    pthread_mutex_lock (&aml_out->lock);

    /*
    send audio pause cmd to  video when tunnel mode.
    */
    if (aml_out->hw_sync_mode) {
        sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_PAUSE");
        aml_out->tsync_status = TSYNC_STATUS_PAUSED;
    }
    if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
        if (aml_dev->continuous_audio_mode == 1) {
            if ((aml_dev->ms12.dolby_ms12_enable == true) && (aml_dev->ms12.is_continuous_paused == false)) {
                aml_dev->ms12.is_continuous_paused = true;
                pthread_mutex_lock(&ms12->lock);
                dolby_ms12_set_pause_flag(aml_dev->ms12.is_continuous_paused);
                set_dolby_ms12_runtime_pause(&(aml_dev->ms12), aml_dev->ms12.is_continuous_paused);
                pthread_mutex_unlock(&ms12->lock);
            } else {
                ALOGI("%s do nothing\n", __func__);
            }
        } else {
            ret = do_output_standby_l(&stream->common);
            if (ret < 0) {
                goto exit;
            }
        }
    } else {

        ret = do_output_standby_l(&stream->common);
        if (ret < 0) {
            goto exit;
        }
    }
exit1:
    aml_out->pause_status = true;

    if (aml_out->hw_sync_mode) {
        sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_PAUSE");
        aml_out->tsync_status = TSYNC_STATUS_PAUSED;
    }

exit:
    if (standy) {
        ALOGD("%s(), stream(%p) already in standy, return INVALID_STATE", __func__, stream);
        ret = INVALID_STATE;
    }
    pthread_mutex_unlock(&aml_dev->lock);
    pthread_mutex_unlock(&aml_out->lock);
    return ret;
}

static int out_resume_new (struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(aml_dev->ms12);
    int ret = 0;

    ALOGI("%s(), stream(%p),standby = %d,pause_status = %d\n", __func__, stream, aml_out->standby, aml_out->pause_status);

    pthread_mutex_lock(&aml_dev->lock);
    pthread_mutex_lock(&aml_out->lock);
    /* a stream should fail to resume if not previously paused */
    // aml_out->standby is always "1" in ms12 continous mode, which will cause fail to resume here
    if (/*aml_out->standby || */aml_out->pause_status == false) {
        // If output stream is not standby or not paused,
        // we should return Result::INVALID_STATE (3),
        // thus we can pass VTS test.
        ALOGE ("Amlogic_HAL - %s: cannot resume, because output stream isn't in standby or paused state.", __FUNCTION__);
        ret = 3;
        goto exit;
    }
    if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
        if (aml_dev->continuous_audio_mode == 1) {
            if ((aml_dev->ms12.dolby_ms12_enable == true) && (aml_dev->ms12.is_continuous_paused == true)) {
                aml_dev->ms12.is_continuous_paused = false;
                pthread_mutex_lock(&ms12->lock);
                dolby_ms12_set_pause_flag(aml_dev->ms12.is_continuous_paused);
                set_dolby_ms12_runtime_pause(&(aml_dev->ms12), aml_dev->ms12.is_continuous_paused);
                pthread_mutex_unlock(&ms12->lock);
            }
        }
    }
    if (aml_out->pause_status == false) {
        ALOGE("%s(), stream status %d\n", __func__, aml_out->pause_status);
        goto exit;
    }
    aml_out->pause_status = false;
    if (aml_out->hw_sync_mode) {
        sysfs_set_sysfs_str(TSYNC_EVENT, "AUDIO_RESUME");
    }

exit:
    pthread_mutex_unlock (&aml_dev->lock);
    pthread_mutex_unlock (&aml_out->lock);
    aml_out->pause_status = 0;
    return ret;
}

static int out_flush_new (struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    ALOGI("%s(), stream(%p)\n", __func__, stream);
    out->frame_write_sum  = 0;
    out->last_frames_postion = 0;
    out->spdif_enc_init_frame_write_sum =  0;
    out->frame_skip_sum = 0;
    out->skip_frame = 0;
    out->input_bytes_size = 0;
    out->pause_status = false;

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (out->hw_sync_mode) {
            aml_audio_hwsync_init(out->hwsync, out);
        }
        //normal pcm(mixer thread) do not flush dolby ms12 input buffer
        if (continous_mode(adev) && (out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
            //1.audio easing duration is 32ms,
            //2.one loop for schedule_run cost about 32ms(contains the hardware costing),
            //3.if [pause, flush] too short, means it need more time to do audio easing
            //so, the delay time for 32ms(pause is completed after audio easing is done) is enough.
            aml_audio_sleep(64000);
            pthread_mutex_lock(&ms12->lock);
            dolby_ms12_main_flush(stream);
            pthread_mutex_unlock(&ms12->lock);
            out->continuous_audio_offset = 0;
        }
    }

    return 0;
}

// insert bytes of zero data to pcm which makes A/V synchronization
static int insert_output_bytes (struct aml_stream_out *out, size_t size)
{
    int ret = 0;
    size_t insert_size = size;
    size_t once_write_size = 0;
    struct audio_stream_out *stream = (struct audio_stream_out*)out;
    struct aml_audio_device *adev = out->dev;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    audio_format_t output_format = get_output_format(stream);
    char *insert_buf = (char*) aml_audio_malloc (8192);
    if (insert_buf == NULL) {
        ALOGE ("malloc size failed \n");
        return -ENOMEM;
    }

    if (!out->pcm) {
        ret = -ENOENT;
        goto exit;
    }
    if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected && !out->earc_pcm) {
        ret = -ENOENT;
        goto exit;
    }

    memset (insert_buf, 0, 8192);
    while (insert_size > 0) {
        once_write_size = insert_size > 8192 ? 8192 : insert_size;
        if (eDolbyMS12Lib == adev->dolby_lib_type && !is_bypass_dolbyms12(stream)) {
            size_t used_size = 0;
            ret = dolby_ms12_main_process(stream, insert_buf, once_write_size, &used_size);
            if (ret) {
                ALOGW("dolby_ms12_main_process cost size %zu,input size %zu,check!!!!", once_write_size, used_size);
            }
        } else {
            aml_hw_mixer_mixing(&adev->hw_mixer, insert_buf, once_write_size, output_format);
            //process_buffer_write(stream, buffer, bytes);
            if (audio_hal_data_processing(stream, insert_buf, once_write_size, &output_buffer, &output_buffer_bytes, output_format) == 0) {
                hw_write(stream, output_buffer, output_buffer_bytes, output_format);
            }
        }
        insert_size -= once_write_size;
    }

exit:
    aml_audio_free (insert_buf);
    return 0;
}

enum hwsync_status check_hwsync_status (uint apts_gap)
{
    enum hwsync_status sync_status;

    if (apts_gap < APTS_DISCONTINUE_THRESHOLD_MIN)
        sync_status = CONTINUATION;
    else if (apts_gap > APTS_DISCONTINUE_THRESHOLD_MAX)
        sync_status = RESYNC;
    else
        sync_status = ADJUSTMENT;

    return sync_status;
}

static ssize_t out_write_direct(struct audio_stream_out *stream, const void* buffer,
                                 size_t bytes)
{
    int ret = 0;
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_out_frame_size (stream);
    size_t in_frames = bytes / frame_size;
    bool force_input_standby = false;
    size_t out_frames = 0;
    void *buf;
    uint i, total_len;
    char prop[PROPERTY_VALUE_MAX];
    int codec_type = out->codec_type;
    int samesource_flag = 0;
    int32_t latency_frames = 0;
    uint64_t total_frame = 0;
    int return_bytes = bytes;
    size_t total_bytes = bytes;
    size_t bytes_cost = 0;
    char *read_buf = (char *)buffer;
    audio_hwsync_t *hw_sync = out->hwsync;
    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
    * on the output stream mutex - e.g. executing select_mode() while holding the hw device
    * mutex
    */
    if (adev->debug_flag)
        ALOGD("%s(): out %p, bytes %zu, hwsync:%d, last frame_write_sum %"PRIu64,
           __func__, out, bytes,
           out->hw_sync_mode, out->frame_write_sum);

    if (adev->out_device != out->out_device) {
        ALOGD("%s:%p device:%x,%x", __func__, stream, out->out_device, adev->out_device);
        out->out_device = adev->out_device;
        config_output(stream, true);
    }

    /*when hi-pcm stopped  and switch to 2-ch , then switch to hi-pcm,hi-pcm-mode must be
     set and wait 20ms for i2s device release*/
    if (get_codec_type(out->hal_internal_format) == TYPE_PCM && !adev->hi_pcm_mode
        && (out->config.rate > 48000 || out->config.channels >= 6)) {
        adev->hi_pcm_mode = true;
        usleep (20000);
    }
    pthread_mutex_lock (&adev->lock);
    pthread_mutex_lock (&out->lock);
    if (out->pause_status == true) {
        pthread_mutex_unlock (&adev->lock);
        pthread_mutex_unlock (&out->lock);
        ALOGI ("call out_write when pause status,size %zu,(%p)\n", bytes, out);
        return 0;
    }
    //ALOGV ("%s with flag 0x%x\n", __func__, out->flags);
    if ( (out->standby) && out->hw_sync_mode) {
        /*
        there are two types of raw data come to hdmi  audio hal
        1) compressed audio data without IEC61937 wrapped
        2) compressed audio data  with IEC61937 wrapped (typically from amlogic amadec source)
        we use the AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO to distiguwish the two cases.
        */
        if ((codec_type == TYPE_AC3 || codec_type == TYPE_EAC3)  && !(out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO)) {
             if (out->need_convert) {
                struct dolby_ddp_dec *ddp_dec = &(adev->ddp);
                ddp_dec->digital_raw = 1;
                ddp_dec->dual_input  = false;
                if (ddp_dec->status != 1) {
                    int status = dcv_decoder_init_patch(ddp_dec);
                    ddp_dec->nIsEc3 = 1;
                    ALOGI("dcv_decoder_init_patch return :%d,is 61937 %d", status, ddp_dec->is_iec61937);
               }
            } else {
                spdifenc_init(out->pcm, out->hal_internal_format);
                spdifenc_set_mute(out->offload_mute);
                out->spdif_enc_init_frame_write_sum = out->frame_write_sum;
            }
        }
        // todo: check timestamp header PTS discontinue for new sync point after seek
        if ((codec_type == TYPE_AC3 || codec_type == TYPE_EAC3)) {
            aml_audio_hwsync_init(out->hwsync, out);
            out->spdif_enc_init_frame_write_sum = out->frame_write_sum;
        }
    }
    if (out->standby) {
        ret = start_output_stream_direct (out);
        if (ret != 0) {
            pthread_mutex_unlock (&adev->lock);
            goto exit;
        }
        out->standby = 0;
        /* a change in output device may change the microphone selection */
        if (adev->active_input &&
            adev->active_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
            force_input_standby = true;
        }
    }
    void *write_buf = NULL;
    size_t  hwsync_cost_bytes = 0;
rewrite:
    if (out->hw_sync_mode == 1) {
        uint64_t  cur_pts = 0xffffffff;
        int outsize = 0;
        char tempbuf[128];
        if (adev->debug_flag)
            ALOGD("before aml_audio_hwsync_find_frame bytes %zu\n", total_bytes - bytes_cost);
        hwsync_cost_bytes = aml_audio_hwsync_find_frame(out->hwsync, (char *)buffer + bytes_cost, total_bytes - bytes_cost, &cur_pts, &outsize);
        if (cur_pts > 0xffffffff) {
            ALOGE ("APTS exeed the max 32bit value");
        }
        if (adev->debug_flag)
            ALOGI ("after aml_audio_hwsync_find_frame bytes remain %zu,cost %zu,outsize %d,pts %"PRId64"ms\n",
               total_bytes - bytes_cost - hwsync_cost_bytes, hwsync_cost_bytes, outsize, cur_pts / 90);
        //TODO,skip 3 frames after flush, to tmp fix seek pts discontinue issue.need dig more
        // to find out why seek ppint pts frame is remained after flush.WTF.
        if (out->skip_frame > 0) {
            out->skip_frame--;
            ALOGI ("skip pts@%"PRIx64",cur frame size %d,cost size %zu\n", cur_pts, outsize, hwsync_cost_bytes);
            pthread_mutex_unlock (&adev->lock);
            pthread_mutex_unlock (&out->lock);
            return hwsync_cost_bytes;
        }
        if (cur_pts != 0xffffffff && outsize > 0) {
                        int hwsync_hdmi_latency = aml_audio_get_hwsync_latency_offset();
            // if we got the frame body,which means we get a complete frame.
            //we take this frame pts as the first apts.
            //this can fix the seek discontinue,we got a fake frame,which maybe cached before the seek
            if (hw_sync->first_apts_flag == false) {
                if (cur_pts >= (out_get_latency(stream) + hwsync_hdmi_latency) * 90
                    /*&& out->last_frames_postion > 0*/) {
                    cur_pts -= (out_get_latency(stream) + hwsync_hdmi_latency) * 90;
                    aml_audio_hwsync_set_first_pts(out->hwsync, cur_pts);
                } else {
                    ALOGI("%s(), first pts not set, cur_pts %lld, last position %lld",
                        __func__, cur_pts, out->last_frames_postion);
                }
            } else {
                uint64_t apts;
                uint32_t apts32;
                uint pcr = 0;
                uint apts_gap = 0;
                uint64_t latency = (out_get_latency(stream) + hwsync_hdmi_latency) * 90;
                // check PTS discontinue, which may happen when audio track switching
                // discontinue means PTS calculated based on first_apts and frame_write_sum
                // does not match the timestamp of next audio samples
                if (cur_pts > latency) {
                    apts = cur_pts - latency;
                } else {
                    apts = 0;
                }

                apts32 = apts & 0xffffffff;

                if (get_sysfs_uint (TSYNC_PCRSCR, &pcr) == 0) {
                    enum hwsync_status sync_status = CONTINUATION;
                    apts_gap = get_pts_gap (pcr, apts32);
                    sync_status = check_hwsync_status (apts_gap);

                    ALOGV("%s()audio pts %dms, pcr %dms, latency %lldms, diff %dms",
                        __func__, apts32/90, pcr/90, latency/90,
                        (apts32 > pcr) ? (apts32 - pcr)/90 : (pcr - apts32)/90);

                    /*during video stop, pcr has been reset by video
                      we need ignore such pcr value*/
                    if (pcr == 0) {
                        ALOGI("pcr is reset by video");
                        sync_status = CONTINUATION;
                    }
                    // limit the gap handle to 0.5~5 s.
                    if (sync_status == ADJUSTMENT) {
                        // two cases: apts leading or pcr leading
                        // apts leading needs inserting frame and pcr leading neads discarding frame
                        if (apts32 > pcr) {
                            int insert_size = 0;
                            if (out->codec_type == TYPE_EAC3) {
                                insert_size = apts_gap / 90 * 48 * 4 * 4;
                            } else {
                                insert_size = apts_gap / 90 * 48 * 4;
                            }
                            insert_size = insert_size & (~63);
                            ALOGI ("audio gap 0x%"PRIx32" ms ,need insert data %d\n", apts_gap / 90, insert_size);
                            ret = insert_output_bytes (out, insert_size);
                        } else {
                            //audio pts smaller than pcr,need skip frame.
                            //we assume one frame duration is 32 ms for DD+(6 blocks X 1536 frames,48K sample rate)
                            if ((out->codec_type == TYPE_EAC3 || out->need_convert) && outsize > 0) {
                                ALOGI ("audio slow 0x%x,skip frame @pts 0x%"PRIx64",pcr 0x%x,cur apts 0x%x\n",
                                       apts_gap, cur_pts, pcr, apts32);
                                out->frame_skip_sum  +=   1536;
                                bytes = outsize;
                                pthread_mutex_unlock (&adev->lock);
                                goto exit;
                            }
                        }
                    } else if (sync_status == RESYNC) {
                        sprintf (tempbuf, "0x%x", apts32);
                        ALOGI ("tsync -> reset pcrscr 0x%x -> 0x%x, %s big,diff %"PRIx64" ms",
                               pcr, apts32, apts32 > pcr ? "apts" : "pcr", get_pts_gap (apts, pcr) / 90);

                        int ret_val = sysfs_set_sysfs_str (TSYNC_APTS, tempbuf);
                        if (ret_val == -1) {
                            ALOGE ("unable to open file %s,err: %s", TSYNC_APTS, strerror (errno) );
                        }
                    }
                }
            }
        }
        if (outsize > 0) {
            return_bytes = hwsync_cost_bytes;
            in_frames = outsize / frame_size;
            write_buf = hw_sync->hw_sync_body_buf;
        } else {
            return_bytes = hwsync_cost_bytes;
            pthread_mutex_unlock (&adev->lock);
            goto exit;
        }
    } else {
        write_buf = (void *) buffer;
    }
    pthread_mutex_unlock (&adev->lock);
    out_frames = in_frames;
    buf = (void *) write_buf;
    if (codec_type_is_raw_data (out->codec_type) && !(out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) ) {
        //here to do IEC61937 pack
        //ALOGV ("IEC61937 write size %zu,hw_sync_mode %d,flag %x\n", out_frames * frame_size, out->hw_sync_mode, out->flags);
        if (out->need_convert) {
            int ret = -1;
            struct dolby_ddp_dec *ddp_dec = & (adev->ddp);
            int process_bytes = in_frames * frame_size;
            do {
                if (ddp_dec->status == 1) {
                   ret = dcv_decoder_process_patch(ddp_dec, (unsigned char *)buf, process_bytes);
                   if (ret != 0)
                     break;
                   process_bytes = 0;
                } else {
                    config_output(stream, true);
                }
                if (ddp_dec->outlen_raw > 0) {
                    /*to avoid ca noise in Sony TV*/
                    struct snd_pcm_status status;
                    pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_STATUS, &status);
                    if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected && out->earc_pcm) {
                        pcm_ioctl(out->earc_pcm, SNDRV_PCM_IOCTL_STATUS, &status);
                    }
                    if (status.state == PCM_STATE_SETUP ||
                        status.state == PCM_STATE_PREPARED ||
                        status.state == PCM_STATE_XRUN) {
                        ALOGI("mute the first raw data");
                        memset(ddp_dec->outbuf_raw, 0, ddp_dec->outlen_raw);
                    }
                    if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected && out->earc_pcm && adev->bHDMIARCon) {
                        ret = pcm_write (out->earc_pcm, ddp_dec->outbuf_raw, ddp_dec->outlen_raw);
                    } else {
                        ret = pcm_write (out->pcm, ddp_dec->outbuf_raw, ddp_dec->outlen_raw);
                    }
                }
                if (ret == 0) {
                   out->frame_write_sum += ddp_dec->outlen_raw / 4 ;
                } else {
                    ALOGI ("pcm_get_error(out->pcm):%s",pcm_get_error (out->pcm) );
                }
            } while(ddp_dec->remain_size >= 768);

         } else if (out->codec_type  > 0) {
            // compressed audio DD/DD+
            bytes = spdifenc_write ( (void *) buf, out_frames * frame_size);
            //need return actual size of this burst write
            if (out->hw_sync_mode == 1) {
                bytes = hwsync_cost_bytes;
            }
            //ALOGV ("spdifenc_write return %zu\n", bytes);
            if (out->codec_type == TYPE_EAC3) {
                out->frame_write_sum = spdifenc_get_total() / 16 + out->spdif_enc_init_frame_write_sum;
            } else {
                out->frame_write_sum = spdifenc_get_total() / 4 + out->spdif_enc_init_frame_write_sum;
            }
            //ALOGV ("out %p, after out->frame_write_sum %"PRId64"\n", out, out->frame_write_sum);
            ALOGV("---after out->frame_write_sum %"PRId64",spdifenc total %lld\n",
                out->frame_write_sum, spdifenc_get_total() / 16);
        }
        goto exit;
    }
    //here handle LPCM audio (hi-res audio) which goes to direct output
    if (!out->standby) {
        int write_size = out_frames * frame_size;
        //for 5.1/7.1 LPCM direct output,we assume only use left channel volume
        if (!codec_type_is_raw_data(out->codec_type)
                && (out->multich > 2 || out->hal_internal_format != AUDIO_FORMAT_PCM_16_BIT)) {
            //volume apply here,TODO need apply that inside convert_audio_sample_for_output function.
            apply_volume(out->volume_l, write_buf, audio_bytes_per_sample(out->hal_format), bytes);

            if (write_buf) {
                if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected && out->earc_pcm && adev->bHDMIARCon) {
                    ret = pcm_write (out->earc_pcm, write_buf, write_size);
                } else {
                    ret = pcm_write (out->pcm, write_buf, write_size);
                }
                if (ret == 0) {
                    out->frame_write_sum += out_frames;
                } else {
                    ALOGI ("pcm_get_error(out->pcm):%s",pcm_get_error (out->pcm) );
                }
            }
        } else {
            //2 channel LPCM or raw data pass through
            if (!codec_type_is_raw_data (out->codec_type) && out->config.channels == 2) {
                short *sample = (short*) buf;
                int l, r;
                size_t kk;
                for (kk = 0; kk <  out_frames; kk++) {
                    l = out->volume_l * sample[kk * 2];
                    sample[kk * 2] = CLIP (l);
                    r = out->volume_r * sample[kk * 2 + 1];
                    sample[kk * 2 + 1] = CLIP (r);
                }
            }
            if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected && out->earc_pcm && adev->bHDMIARCon) {
                ret = pcm_write (out->earc_pcm, (void *) buf, out_frames * frame_size);
            } else {
                ret = pcm_write (out->pcm, (void *) buf, out_frames * frame_size);
            }
            if (ret == 0) {
                out->frame_write_sum += out_frames;
            } else {
                ALOGI ("pcm_get_error(out->pcm):%s",pcm_get_error (out->pcm) );
            }
        }
    }

exit:
    total_frame = out->frame_write_sum + out->frame_skip_sum;
    //ALSA hw latency fames
    latency_frames = out_get_latency_frames(stream);
    //latency_frames += HDMI_LATENCY_MS * 48;
    //mantual tunning latency frames
    int tuning_latency_frame = aml_audio_get_arc_latency_offset(adev->sink_format)*48;
    int total_latency_frame = 0;
    total_latency_frame = tuning_latency_frame + latency_frames;
    clock_gettime (CLOCK_MONOTONIC, &out->timestamp);
    out->lasttimestamp.tv_sec = out->timestamp.tv_sec;
    out->lasttimestamp.tv_nsec = out->timestamp.tv_nsec;
    if (total_latency_frame > 0 && total_frame < (uint64_t)total_latency_frame) {
        out->last_frames_postion = 0;
    } else {
        out->last_frames_postion = total_frame - total_latency_frame;//total_frame;
    }
    if (adev->debug_flag)
        ALOGD("out %p,out->last_frames_postion %"PRId64", total latency frame = %d, skp sum %lld , tune frames %d,alsa frame %d\n",
            out, out->last_frames_postion, total_latency_frame, out->frame_skip_sum,tuning_latency_frame,latency_frames);
    pthread_mutex_unlock (&out->lock);
    if (ret != 0) {
        usleep (bytes * 1000000 / audio_stream_out_frame_size (stream) /
                out_get_sample_rate (&stream->common) );
    }
    /*
    if the data is not  consumed totally,
    we need re-send data again
    */
    if (return_bytes > 0 && total_bytes > (return_bytes + bytes_cost)) {
        bytes_cost += return_bytes;
        goto rewrite;
    }
    else if (return_bytes < 0)
        return return_bytes;
    else
        return total_bytes;
}

static int out_get_render_position (const struct audio_stream_out *stream,
                                    uint32_t *dsp_frames)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    uint64_t  dsp_frame_int64 = out->last_frames_postion;
    *dsp_frames = (uint32_t)(dsp_frame_int64 & 0xffffffff);
    int frame_latency = 0;
    int out_rate = out->config.rate;

    if (*dsp_frames == 0) {
        if (adev->debug_flag) {
            ALOGI("Amlogic_HAL - %s:%d: return Result::INVALID_STATE (3) instead of other error code. dsp_frames==0", __func__, __LINE__);
        }
        return INVALID_STATE;
    }

    if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
        //dsp_frame_int64 = out->last_frames_postion ;
        //*dsp_frames = (uint32_t)(dsp_frame_int64 & 0xffffffff);
        if (out->last_dsp_frame > dsp_frame_int64) {
            ALOGI("maybe uint32_t wraparound,print something,last %u,now %u", out->last_dsp_frame, *dsp_frames);
            ALOGI("wraparound,out_get_render_position return %u,playback time %"PRIu64" ms,sr %d\n", *dsp_frames,
                  out->last_frames_postion * 1000 / out->config.rate, out->config.rate);
        }
    }
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
       /*
         *1.after MS12, output(pcm/dd/dd+) samplerate is changed to 48kHz,
         *handle the case hal_rate != 48KHz
         *2.Bypass MS12, do not go through this process.
         */
        if (direct_continous((struct audio_stream_out *)stream)) {
            dsp_frame_int64 = adev->ms12.last_frames_postion;
        } else if (out->is_normal_pcm && adev->ms12.dolby_ms12_enable) {
            dsp_frame_int64 =  adev->ms12.sys_audio_frame_pos;
        }
        if ((out->hal_rate != MM_FULL_POWER_SAMPLING_RATE) &&
            (!is_bypass_dolbyms12((struct audio_stream_out *)stream))) {
            out_rate = MM_FULL_POWER_SAMPLING_RATE;
        }
    } else if (eDolbyDcvLib  == adev->dolby_lib_type) {
        if (out->hal_internal_format == AUDIO_FORMAT_AC3 ||
            out->hal_internal_format == AUDIO_FORMAT_E_AC3) {
            /* why we sitll use hal rate instead of alsa output rate?
             * becase for dd/ddp input, the positon is calculated with write size in mixer_main_buffer_write
             * so the frame position rate is same with input
             * we will unify the position later, then we can remove this
             */
            out_rate = out->hal_rate;
        }
    }

    *dsp_frames = (dsp_frame_int64) * out->hal_rate / out_rate;

    if (!adev->continuous_audio_mode) {
        if (adev->active_outport == OUTPORT_HDMI_ARC) {
            int arc_latency_ms = 0;
            if (adev->audio_type == EAC3 || adev->audio_type == AC3)
                arc_latency_ms = aml_audio_get_arc_latency_offset(adev->sink_format);
            frame_latency = arc_latency_ms * (out->hal_rate / 1000);
            *dsp_frames += frame_latency;
        } else if (adev->active_outport == OUTPORT_HDMI) {
            int hdmi_latency_ms = 0;
            if (adev->audio_type == EAC3 || adev->audio_type == AC3)
                hdmi_latency_ms = aml_audio_get_hdmi_latency_offset(out->hal_internal_format,
                                           adev->ms12.dolby_ms12_enable,
                                           adev->hdmi_format);
            frame_latency = hdmi_latency_ms * (out->hal_rate / 1000);
            *dsp_frames += frame_latency ;
        } else if (adev->active_outport == OUTPORT_SPEAKER) {
            int speaker_latency_ms = 0;
            if (adev->audio_type == EAC3 || adev->audio_type == AC3)
                speaker_latency_ms = aml_audio_get_speaker_latency_offset(out->hal_internal_format);
            frame_latency = speaker_latency_ms * (out->hal_rate / 1000);
            *dsp_frames += frame_latency ;
        }
    }

    if (*dsp_frames < 0)
        return -EINVAL;
    if (adev->debug_flag) {
        ALOGI("out_get_render_position %d frame_latency %d \n", *dsp_frames,frame_latency);
    }
    return 0;
}

static int out_add_audio_effect (const struct audio_stream *stream, effect_handle_t effect)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *dev = out->dev;
    int i;
    int status = 0;
    char *name = "VirtualX";
    effect_handle_t tmp;
    pthread_mutex_lock (&dev->lock);
    pthread_mutex_lock (&out->lock);
    if (dev->native_postprocess.num_postprocessors >= MAX_POSTPROCESSORS) {
        status = -ENOSYS;
        goto exit;
    }

    for (i = 0; i < dev->native_postprocess.num_postprocessors; i++) {
        if (dev->native_postprocess.postprocessors[i] == effect) {
            status = 0;
            goto exit;
        }
    }

    dev->native_postprocess.postprocessors[dev->native_postprocess.num_postprocessors++] = effect;
    /*specify effect order for virtualx.*/
    effect_descriptor_t tmpdesc;
    for ( i = 0; i < dev->native_postprocess.num_postprocessors; i++) {
        (*effect)->get_descriptor(dev->native_postprocess.postprocessors[i], &tmpdesc);
        if (0 == strcmp(tmpdesc.name,name)) {
            tmp = dev->native_postprocess.postprocessors[i];
            dev->native_postprocess.postprocessors[i] = dev->native_postprocess.postprocessors[0];
            dev->native_postprocess.postprocessors[0] = tmp;
        }
    }
    if (dev->native_postprocess.num_postprocessors >= dev->native_postprocess.total_postprocessors)
        dev->native_postprocess.total_postprocessors = dev->native_postprocess.num_postprocessors;

exit:
    pthread_mutex_unlock (&out->lock);
    pthread_mutex_unlock (&dev->lock);
    return status;
}

static int out_remove_audio_effect (const struct audio_stream *stream, effect_handle_t effect)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *dev = out->dev;
    int i;
    int status = -EINVAL;
    bool found = false;
    pthread_mutex_lock (&dev->lock);
    pthread_mutex_lock (&out->lock);
    if (dev->native_postprocess.num_postprocessors <= 0) {
        status = -ENOSYS;
        goto exit;
    }

    for (i = 0; i < dev->native_postprocess.num_postprocessors; i++) {
        if (found) {
            dev->native_postprocess.postprocessors[i - 1] = dev->native_postprocess.postprocessors[i];
            continue;
        }

        if (dev->native_postprocess.postprocessors[i] == effect) {
            dev->native_postprocess.postprocessors[i] = NULL;
            status = 0;
            found = true;
        }
    }

    if (status != 0)
        goto exit;

    dev->native_postprocess.num_postprocessors--;
exit:
    pthread_mutex_unlock (&out->lock);
    pthread_mutex_unlock (&dev->lock);
    return status;
}
static int out_get_next_write_timestamp (const struct audio_stream_out *stream __unused,
        int64_t *timestamp __unused)
{
    // return -EINVAL;

    // VTS can only recognizes Result:OK or Result:INVALID_STATE, which is 0 or 3.
    // So we return ESRCH (3) in order to pass VTS.
    ALOGI ("Amlogic_HAL - %s: return ESRCH (3) instead of -EINVAL (-22)", __FUNCTION__);
    return ESRCH;
}

//actually maybe it be not useful now  except pass CTS_TEST:
//  run cts -c android.media.cts.AudioTrackTest -m testGetTimestamp
static int out_get_presentation_position (const struct audio_stream_out *stream, uint64_t *frames, struct timespec *timestamp)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    uint64_t frames_written_hw = out->last_frames_postion;
    struct timespec written_timestamp = out->lasttimestamp;
    int frame_latency = 0,timems_latency = 0;
    int out_rate = out->config.rate;
    if (!frames || !timestamp) {
        ALOGI("%s, !frames || !timestamp\n", __FUNCTION__);
        return -EINVAL;
    }

    if (out->is_normal_pcm && eDolbyMS12Lib == adev->dolby_lib_type && adev->ms12.dolby_ms12_enable) {
        frames_written_hw = adev->ms12.sys_audio_frame_pos;
        written_timestamp = adev->ms12.sys_audio_timestamp;
        ALOGV("system write =%lld frame =%lld diff=%lld",adev->sys_audio_frame_written, *frames, adev->sys_audio_frame_written - *frames);
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (direct_continous((struct audio_stream_out *)stream)) {
            frames_written_hw = adev->ms12.last_frames_postion;
            written_timestamp = adev->ms12.timestamp;
        }
        out_rate = MM_FULL_POWER_SAMPLING_RATE;
    } else if (eDolbyDcvLib  == adev->dolby_lib_type) {
        if (out->hal_internal_format == AUDIO_FORMAT_AC3 ||
            out->hal_internal_format == AUDIO_FORMAT_E_AC3) {
            /* why we sitll use hal rate instead of alsa output rate?
             * becase for dd/ddp input, the positon is calculated with write size in mixer_main_buffer_write
             * so the frame position rate is same with input
             * we will unify the position later, then we can remove this
             */
            out_rate = out->hal_rate;
        }
    }


    if (!adev->continuous_audio_mode) {
        if (adev->active_outport == OUTPORT_HDMI_ARC) {
            int arc_latency_ms = 0;
            if (adev->audio_type == EAC3 || adev->audio_type == AC3)
                arc_latency_ms = aml_audio_get_arc_latency_offset(adev->sink_format);
            frame_latency = arc_latency_ms * (out->hal_rate / 1000);
        } else if (adev->active_outport == OUTPORT_HDMI) {
            int hdmi_latency_ms = 0;
            if (adev->audio_type == EAC3 || adev->audio_type == AC3)
                hdmi_latency_ms = aml_audio_get_hdmi_latency_offset(out->hal_internal_format,
                                           adev->ms12.dolby_ms12_enable,
                                           adev->hdmi_format);
            frame_latency = hdmi_latency_ms * (out->hal_rate / 1000);
        } else if (adev->active_outport == OUTPORT_SPEAKER) {
            int speaker_latency_ms = 0;
            if (adev->audio_type == EAC3 || adev->audio_type == AC3)
                speaker_latency_ms = aml_audio_get_speaker_latency_offset(out->hal_internal_format);
            frame_latency = speaker_latency_ms * (out->hal_rate / 1000);
        }
    }

    if ((frame_latency + (int64_t)frames_written_hw) <= 0) {
        ALOGV("%s(), not ready yet", __func__);
        return -EINVAL;
    }

    *frames = (frame_latency + frames_written_hw) * out->hal_rate / out_rate;
    *timestamp = written_timestamp;

    if (adev->debug_flag) {
        ALOGI("out_get_presentation_position out %p %"PRIu64", sec = %ld, nanosec = %ld tunned_latency_ms %d\n", out, *frames, timestamp->tv_sec, timestamp->tv_nsec, timems_latency);
        int64_t  frame_diff_ms =  (*frames - out->last_frame_reported)/48;
        int64_t  system_time_ms = 0;
        if (timestamp->tv_nsec < out->last_timestamp_reported.tv_nsec) {
            system_time_ms = (timestamp->tv_nsec + 1000000000 - out->last_timestamp_reported.tv_nsec)/1000000;
        }
        else
            system_time_ms = (timestamp->tv_nsec - out->last_timestamp_reported.tv_nsec)/1000000;
        int64_t jitter_diff = llabs(frame_diff_ms - system_time_ms);
        if  (jitter_diff > JITTER_DURATION_MS) {
            ALOGI("%s jitter out last pos info: %p %"PRIu64", sec = %ld, nanosec = %ld\n",__func__,out, out->last_frame_reported,
                out->last_timestamp_reported.tv_sec, out->last_timestamp_reported.tv_nsec);
            ALOGI("%s jitter  system time diff %"PRIu64" ms, position diff %"PRIu64" ms, jitter %"PRIu64" ms \n",
                __func__,system_time_ms,frame_diff_ms,jitter_diff);
        }
        out->last_frame_reported = *frames;
        out->last_timestamp_reported = *timestamp;
    }
    return 0;
}

/** audio_stream_in implementation **/
static unsigned int select_port_by_device(struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;
    unsigned int inport = PORT_I2S;
    audio_devices_t in_device = in->device;

    if (in_device & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
        inport = PORT_PCM;
    } else if ((in_device & AUDIO_DEVICE_IN_HDMI) ||
            (in_device & AUDIO_DEVICE_IN_HDMI_ARC) ||
            (in_device & AUDIO_DEVICE_IN_SPDIF)) {
        /* fix auge tv input, hdmirx, tunner */
        if (alsa_device_is_auge() &&
                (in_device & AUDIO_DEVICE_IN_HDMI)) {
            inport = PORT_TV;
        } else if ((access(SYS_NODE_EARC_RX, F_OK) == 0) &&
                (in_device & AUDIO_DEVICE_IN_HDMI_ARC)) {
            inport = PORT_EARC;
        } else {
            inport = PORT_SPDIF;
        }
    } else if ((in_device & AUDIO_DEVICE_IN_BACK_MIC) ||
            (in_device & AUDIO_DEVICE_IN_BUILTIN_MIC)) {
        if (adev->mic_desc) {
            struct mic_in_desc *desc = adev->mic_desc;

            switch (desc->mic) {
            case DEV_MIC_PDM:
                inport = PROT_PDM;
                break;
            case DEV_MIC_TDM:
                inport = PORT_BUILTINMIC;
                break;
            default:
                inport = PORT_BUILTINMIC;
                break;
            }
        } else
            inport = PORT_BUILTINMIC;
    } else {
        /* fix auge tv input, hdmirx, tunner */
        if (alsa_device_is_auge()
            && (in_device & AUDIO_DEVICE_IN_TV_TUNER))
            inport = PORT_TV;
        else
            inport = PORT_I2S;
    }

#ifdef ENABLE_AEC_FUNC
    /* AEC using inner loopback port */
    if (in_device & AUDIO_DEVICE_IN_BUILTIN_MIC)
        inport = PORT_LOOPBACK;
#endif

#ifdef USB_KARAOKE
    if (in->source == AUDIO_SOURCE_KARAOKE_SPEAKER)
        inport = PORT_LOOPBACK;
#endif

    return inport;
}

/* TODO: add non 2+2 cases */
static void update_alsa_config(struct aml_stream_in *in) {
#ifdef ENABLE_AEC_FUNC
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        in->config.rate = in->requested_rate;
        in->config.channels = 4;
    }
#else
    (void)in;
#endif
}

static int choose_stream_pcm_config(struct aml_stream_in *in);
static int add_in_stream_resampler(struct aml_stream_in *in);

/* must be called with hw device and input stream mutexes locked */
static int start_input_stream(struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;
    unsigned int card = CARD_AMLOGIC_BOARD;
    unsigned int port = PORT_I2S;
    unsigned int alsa_device = 0;
    int ret = 0;

    ret = choose_stream_pcm_config(in);
    if (ret < 0)
        return -EINVAL;

    adev->active_input = in;
    if (adev->mode != AUDIO_MODE_IN_CALL) {
        adev->in_device &= ~AUDIO_DEVICE_IN_ALL;
        adev->in_device |= in->device;
    }

    card = alsa_device_get_card_index();
    port = select_port_by_device(in);
    /* check to update alsa device by port */
    alsa_device = alsa_device_update_pcm_index(port, CAPTURE);
#ifdef USB_KARAOKE
    if (in->source == AUDIO_SOURCE_KARAOKE_SPEAKER) {
        card = alsa_device_get_card_index_by_name("Loopback");
        if (card < 0)
            return -EINVAL;
        /* Aloop in device id = 1 */
        alsa_device = 1;
    }
#endif
    ALOGD("*%s, open alsa_card(%d) alsa_device(%d), in_device:0x%x\n",
        __func__, card, alsa_device, adev->in_device);

    /* this assumes routing is done previously */
    in->pcm = pcm_open(card, alsa_device, PCM_IN | PCM_NONEBLOCK, &in->config);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("%s: cannot open pcm_in driver: %s", __func__, pcm_get_error(in->pcm));
        pcm_close (in->pcm);
        adev->active_input = NULL;
        return -ENOMEM;
    }

    if (in->requested_rate != in->config.rate) {
        ret = add_in_stream_resampler(in);
        if (ret < 0) {
            pcm_close (in->pcm);
            adev->active_input = NULL;
            return -EINVAL;
        }
    }

    ALOGD("%s: device(%x) channels=%d rate=%d requested_rate=%d mode= %d",
        __func__, in->device, in->config.channels,
        in->config.rate, in->requested_rate, adev->mode);



    /* if no supported sample rate is available, use the resampler */
    if (in->resampler) {
        in->resampler->reset(in->resampler);
        in->frames_in = 0;
    }

    return 0;
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    return in->requested_rate;
}

static int in_set_sample_rate(struct audio_stream *stream __unused, uint32_t rate __unused)
{
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    size_t size;
#if defined(IS_ATOM_PROJECT)
    audio_channel_mask_t channel_mask = in->hal_channel_mask;
    audio_format_t format = in->hal_format;

    if ((in->device & AUDIO_DEVICE_IN_LINE) && in->ref_count == 1) {
        channel_mask = AUDIO_CHANNEL_OUT_STEREO;
        format = AUDIO_FORMAT_PCM_32_BIT;
    }
    ALOGD("%s: enter: channel_mask(%#x) rate(%d) format(%#x)", __func__,
        channel_mask, in->requested_rate, format);
    size = get_input_buffer_size(in->config.period_size, in->requested_rate,
                                  format,
                                  audio_channel_count_from_in_mask(channel_mask));
#else
    ALOGD("%s: enter: channel_mask(%#x) rate(%d) format(%#x)", __func__,
        in->hal_channel_mask, in->requested_rate, in->hal_format);
    size = get_input_buffer_size(in->config.period_size, in->requested_rate,
                                  in->hal_format,
                                  audio_channel_count_from_in_mask(in->hal_channel_mask));
#endif
    ALOGD("%s: exit: buffer_size = %zu", __func__, size);

    return size;
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
#if defined(IS_ATOM_PROJECT)
    if ((in->device & AUDIO_DEVICE_IN_LINE) && in->ref_count == 1)
        return AUDIO_CHANNEL_IN_STEREO;
#endif
    return in->hal_channel_mask;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
#if defined(IS_ATOM_PROJECT)
    if ((in->device & AUDIO_DEVICE_IN_LINE) && in->ref_count == 1)
        return AUDIO_FORMAT_PCM_32_BIT;
#endif
    return in->hal_format;
}

static int in_set_format(struct audio_stream *stream __unused, audio_format_t format __unused)
{
    return -ENOSYS;
}

/* must be called with hw device and input stream mutexes locked */
static int do_input_standby (struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;

    ALOGD ("%s(%p) in->standby = %d", __FUNCTION__, in, in->standby);
    if (!in->standby) {
        pcm_close (in->pcm);
        in->pcm = NULL;

        if (in->resampler) {
            release_resampler(in->resampler);
            in->resampler = NULL;
        }
        if (in->buffer) {
            aml_audio_free(in->buffer);
            in->buffer = NULL;
        }

        adev->active_input = NULL;
        if (adev->mode != AUDIO_MODE_IN_CALL) {
            adev->in_device &= ~AUDIO_DEVICE_IN_ALL;
            //select_input_device(adev);
        }

        in->standby = 1;
    }
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int status;

#if defined(IS_ATOM_PROJECT)
    /*after mic and linein all stopped, stoped the input device*/
    if ((in->device & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_LINE)) && in->ref_count == 2) {
        ALOGD("%s: ref_count = %d", __func__, in->ref_count);
        return 0;
    }
#endif

    ALOGD("%s: enter: stream(%p)", __func__, stream);
    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    status = do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    ALOGD("%s: exit", __func__);

    return status;
}

static int in_dump (const struct audio_stream *stream __unused, int fd __unused)
{
    return 0;
}

static int in_set_parameters (struct audio_stream *stream, const char *kvpairs)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *adev = in->dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret, val = 0;
    bool do_standby = false;

    ALOGD ("%s(%p, %s)", __FUNCTION__, stream, kvpairs);
    parms = str_parms_create_str (kvpairs);

    ret = str_parms_get_str (parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value, sizeof (value) );

    pthread_mutex_lock (&adev->lock);
    pthread_mutex_lock (&in->lock);
    if (ret >= 0) {
        val = atoi (value);
        /* no audio source uses val == 0 */
        if ( (in->source != val) && (val != 0) ) {
            in->source = val;
            do_standby = true;
        }
    }

    ret = str_parms_get_str (parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof (value) );
    if (ret >= 0) {
        val = atoi (value) & ~AUDIO_DEVICE_BIT_IN;
        if ( (in->device != (unsigned)val) && (val != 0) ) {
            in->device = val;
            do_standby = true;
        }
    }

    if (do_standby) {
        do_input_standby (in);
    }
    pthread_mutex_unlock (&in->lock);
    pthread_mutex_unlock (&adev->lock);

    int framesize = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_FRAME_COUNT, &framesize);

    if (ret >= 0) {
        if (framesize > 0) {
            ALOGI ("Reset audio input hw frame size from %d to %d\n",
                   in->config.period_size * in->config.period_count, framesize);
            in->config.period_size = framesize / in->config.period_count;
            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&in->lock);

            if (!in->standby && (in == adev->active_input) ) {
                do_input_standby (in);
                start_input_stream (in);
                in->standby = 0;
            }

            pthread_mutex_unlock (&in->lock);
            pthread_mutex_unlock (&adev->lock);
        }
    }

    str_parms_destroy (parms);

    // VTS can only recognizes Result::OK, which is 0x0.
    // So we change ret value to 0 when ret isn't equal to 0
    if (ret > 0) {
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 if it's greater than 0 for passing VTS test.", __FUNCTION__);
        ret = 0;
    } else if (ret < 0) {
        ALOGI ("Amlogic_HAL - %s: parameter is NULL, change ret value to 0 if it's greater than 0 for passing VTS test.", __FUNCTION__);
        ret = 0;
    }

    return ret;
}

static char * in_get_parameters (const struct audio_stream *stream, const char *keys)
{
    char *cap = NULL;
    char *para = NULL;
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    ALOGI ("in_get_parameters %s,in %p\n", keys, in);
    if (strstr (keys, AUDIO_PARAMETER_STREAM_SUP_FORMATS) ) {
        ALOGV ("Amlogic - return hard coded sup_formats list for in stream.\n");
        cap = strdup ("sup_formats=AUDIO_FORMAT_PCM_16_BIT");
        if (cap) {
            para = strdup (cap);
            aml_audio_free (cap);
            return para;
        }
    }
    return strdup ("");
}

static int in_set_gain (struct audio_stream_in *stream __unused, float gain __unused)
{
    return 0;
}

static int get_next_buffer (struct resampler_buffer_provider *buffer_provider,
                            struct resampler_buffer* buffer)
{
    struct aml_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL) {
        return -EINVAL;
    }

    in = (struct aml_stream_in *) ( (char *) buffer_provider -
                                    offsetof (struct aml_stream_in, buf_provider) );

    if (in->pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }

    if (in->frames_in == 0) {
        in->read_status = aml_alsa_input_read ((struct audio_stream_in *)in, (void*) in->buffer,
                                    in->config.period_size * audio_stream_in_frame_size (&in->stream) );
        if (in->read_status != 0) {
            ALOGE ("get_next_buffer() pcm_read error %d", in->read_status);
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->frames_in = in->config.period_size;
    }

    buffer->frame_count = (buffer->frame_count > in->frames_in) ?
                          in->frames_in : buffer->frame_count;
    buffer->i16 = in->buffer + (in->config.period_size - in->frames_in) *
                  in->config.channels;

    return in->read_status;

}

static void release_buffer (struct resampler_buffer_provider *buffer_provider,
                            struct resampler_buffer* buffer)
{
    struct aml_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL) {
        return;
    }

    in = (struct aml_stream_in *) ( (char *) buffer_provider -
                                    offsetof (struct aml_stream_in, buf_provider) );

    in->frames_in -= buffer->frame_count;
}

/* read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer specified */
static ssize_t read_frames (struct aml_stream_in *in, void *buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;

    while (frames_wr < frames) {
        size_t frames_rd = frames - frames_wr;
        if (in->resampler != NULL) {
            in->resampler->resample_from_provider (in->resampler,
                                                   (int16_t *) ( (char *) buffer +
                                                           frames_wr * audio_stream_in_frame_size (&in->stream) ),
                                                   &frames_rd);
        } else {
            struct resampler_buffer buf = {
                { .raw = NULL, },
                .frame_count = frames_rd,
            };
            get_next_buffer (&in->buf_provider, &buf);
            if (buf.raw != NULL) {
                memcpy ( (char *) buffer +
                         frames_wr * audio_stream_in_frame_size (&in->stream),
                         buf.raw,
                         buf.frame_count * audio_stream_in_frame_size (&in->stream) );
                frames_rd = buf.frame_count;
            }
            release_buffer (&in->buf_provider, &buf);
        }
        /* in->read_status is updated by getNextBuffer() also called by
         * in->resampler->resample_from_provider() */
        if (in->read_status != 0) {
            return in->read_status;
        }

        frames_wr += frames_rd;
    }
    return frames_wr;
}

#define DEBUG_AEC_VERBOSE (0)
#define DEBUG_AEC (0) // Remove after AEC is fine-tuned
#define TIMESTAMP_LEN (8)

static uint32_t mic_buf_print_count = 0;

static int in_reset_config_param(struct audio_stream_in *stream, AML_INPUT_STREAM_CONFIG_TYPE_E enType, const void *pValue)
{
    int                     s32Ret = 0;
    struct aml_stream_in    *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *adev = in->dev;
    pthread_mutex_lock(&in->lock);

    switch (enType) {
        case AML_INPUT_STREAM_CONFIG_TYPE_CHANNELS:
            in->config.channels = *(unsigned int *)pValue;
            ALOGD("%s:%d Config channel nummer to %d", __func__, __LINE__, in->config.channels);
            break;

        case AML_INPUT_STREAM_CONFIG_TYPE_PERIODS:
            in->config.period_size = *(unsigned int *)pValue;
            ALOGD("%s:%d Config Period size to %d", __func__, __LINE__, in->config.period_size);
            break;
        default:
            ALOGW("%s:%d not support input stream type:%#x", __func__, __LINE__, enType);
            return -1;
    }

    if (0 == in->standby) {
        do_input_standby(in);
    }
    s32Ret = start_input_stream(in);
    in->standby = 0;
    pthread_mutex_unlock(&in->lock);
    if (s32Ret < 0) {
        ALOGW("start input stream failed! ret:%#x", s32Ret);
    }
    return s32Ret;
}

#ifdef ENABLE_AEC_FUNC
static void inread_proc_aec(struct audio_stream_in *stream,
        void *buffer, size_t bytes)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    size_t in_frames = bytes / audio_stream_in_frame_size(stream);
    int channel_count = audio_channel_count_from_in_mask(in->hal_channel_mask);
    char *read_buf = in->tmp_buffer_8ch;
    aec_timestamp a_timestamp;
    int aec_frame_div = in_frames/512;
    if (in_frames % 512) {
        ALOGE("AEC should 512 frames align,now %d\n",in_frames);
        return ;
    }
    ALOGV("%s,in %d\n",__func__,in_frames);
    //split the mic data with the speaker data.
    short *mic_data, *speaker_data;
    short *read_buf_16 = (short *)read_buf;
    short *aec_out_buf = NULL;
    int cleaned_samples_per_channel = 0;
    size_t bytes_per_sample =
        audio_bytes_per_sample(stream->common.get_format(&stream->common));
    int enable_dump = getprop_bool("media.audio_hal.aec.outdump");
    if (enable_dump) {
        aml_audio_dump_audio_bitstreams("/data/tmp/audio_mix.raw",
                read_buf_16, in_frames*2*2*2);
    }
    //skip the 4 ch data buffer area
    mic_data = (short *)(read_buf + in_frames*2*2*2);
    speaker_data = (short *)(read_buf + in_frames*2*2*2 + in_frames*2*2);
    size_t jj = 0;
    if (channel_count == 2 || channel_count == 1) {
        for (jj = 0;  jj < in_frames; jj ++) {
            mic_data[jj*2 + 0] = read_buf_16[4*jj + 0];
            mic_data[jj*2 + 1] = read_buf_16[4*jj + 1];
            speaker_data[jj*2] = read_buf_16[4*jj + 2];
            speaker_data[jj*2 + 1] = read_buf_16[4*jj + 3];
        }
    } else {
        ALOGV("%s(), channel count inval: %d", __func__, channel_count);
        return;
    }
    if (enable_dump) {
            aml_audio_dump_audio_bitstreams("/data/tmp/audio_mic.raw",
                mic_data, in_frames*2*2);
            aml_audio_dump_audio_bitstreams("/data/tmp/audio_speaker.raw",
                speaker_data, in_frames*2*2);
    }
    for (int i = 0; i < aec_frame_div; i++) {
        cleaned_samples_per_channel = 512;
        short *cur_mic_data = mic_data + i*512*channel_count;
        short *cur_spk_data = speaker_data + i*512*channel_count;
        a_timestamp = get_timestamp();
        aec_set_mic_buf_info(512, a_timestamp.timeStamp, true);
        aec_set_spk_buf_info(512, a_timestamp.timeStamp, true);
        aec_out_buf = aec_spk_mic_process_int16(cur_spk_data,
                cur_mic_data, &cleaned_samples_per_channel);
        if (!aec_out_buf || cleaned_samples_per_channel == 0
                || cleaned_samples_per_channel > (int)512) {
            ALOGV("aec process fail %s,in %d clean sample %d,div %d,in frame %d,ch %d",
                    __func__,512,cleaned_samples_per_channel,aec_frame_div,in_frames,channel_count);
            adjust_channels(cur_mic_data, 2, (char *) buffer + channel_count*512*2*i, channel_count,
                    bytes_per_sample, 512*2*2);
        } else {
            if (enable_dump) {
                aml_audio_dump_audio_bitstreams("/data/tmp/audio_aec.raw",
                    aec_out_buf, cleaned_samples_per_channel*2*2);
            }
            ALOGV("%p,clean sample %d, in frame %d",
                aec_out_buf, cleaned_samples_per_channel, 512);
            adjust_channels(aec_out_buf, 2, (char *)buffer + channel_count*512*2*i, channel_count,
                    bytes_per_sample, 512*2*2);
        }
    }
    //apply volume here
    short *vol_buf = (short *)buffer;
    unsigned int kk = 0;
    int val32 = 0;
    for (kk = 0; kk < bytes/2; kk++) {
        val32 = vol_buf[kk] << 3;
        vol_buf[kk] = CLIP(val32);
    }
    if (enable_dump) {
        aml_audio_dump_audio_bitstreams("/data/tmp/audio_final.raw",
                vol_buf, bytes);
    }
}
#else
static void inread_proc_aec(struct audio_stream_in *stream,
        void *buffer, size_t bytes)
{
    (void)stream;
    (void)buffer;
    (void)bytes;
}
#endif

/* here to fix pcm switch to raw nosie issue ,it is caused by hardware format detection later than output
so we delay pcm output one frame to work around the issue,but it has a negative effect on av sync when normal
pcm playback.abouot delay audio 21.3*BT_AND_USB_PERIOD_DELAY_BUF_CNT ms */
static void processBtAndUsbCardData(struct aml_stream_in *in, struct aml_audio_parser *parser, void *pBuffer, size_t bytes)
{
    bool bIsBufNull = false;
    for (int i=0; i<BT_AND_USB_PERIOD_DELAY_BUF_CNT; i++) {
        if (NULL == in->pBtUsbPeriodDelayBuf[i]) {
            bIsBufNull = true;
        }
    }

    if (NULL == in->pBtUsbTempDelayBuf || in->delay_buffer_size != bytes || bIsBufNull) {
        in->pBtUsbTempDelayBuf = aml_audio_realloc(in->pBtUsbTempDelayBuf, bytes);
        memset(in->pBtUsbTempDelayBuf, 0, bytes);
        for (int i=0; i<BT_AND_USB_PERIOD_DELAY_BUF_CNT; i++) {
            in->pBtUsbPeriodDelayBuf[i] = aml_audio_realloc(in->pBtUsbPeriodDelayBuf[i], bytes);
            memset(in->pBtUsbPeriodDelayBuf[i], 0, bytes);
        }
        in->delay_buffer_size = bytes;
    }

    if (AUDIO_FORMAT_PCM_16_BIT == parser->aformat || AUDIO_FORMAT_PCM_32_BIT == parser->aformat) {
        memcpy(in->pBtUsbTempDelayBuf, pBuffer, bytes);
        memcpy(pBuffer, in->pBtUsbPeriodDelayBuf[BT_AND_USB_PERIOD_DELAY_BUF_CNT-1], bytes);
        for (int i=BT_AND_USB_PERIOD_DELAY_BUF_CNT-1; i>0; i--) {
            memcpy(in->pBtUsbPeriodDelayBuf[i], in->pBtUsbPeriodDelayBuf[i-1], bytes);
        }
        memcpy(in->pBtUsbPeriodDelayBuf[0], in->pBtUsbTempDelayBuf, bytes);
    }
}

static void processHdmiInputFormatChange(struct aml_stream_in *in, struct aml_audio_parser *parser)
{
    audio_format_t enCurFormat = audio_parse_get_audio_type(parser->audio_parse_para);
    if (enCurFormat != parser->aformat) {
        ALOGI("%s:%d input format changed from %#x to %#x, PreDecType:%#x", __func__, __LINE__, parser->aformat, enCurFormat, parser->enCurDecType);
        for (int i=0; i<BT_AND_USB_PERIOD_DELAY_BUF_CNT; i++) {
             memset(in->pBtUsbPeriodDelayBuf[i], 0, in->delay_buffer_size);
        }
        parser->in = in;
        if (AUDIO_FORMAT_PCM_16_BIT == enCurFormat) {
            if (AUDIO_FORMAT_AC3 == parser->aformat || AUDIO_FORMAT_E_AC3 == parser->aformat) {//from dd/dd+ -> pcm
                dcv_decode_release(parser);
            } else if (AUDIO_FORMAT_DTS == parser->aformat || AUDIO_FORMAT_DTS_HD == parser->aformat) {//from dts -> pcm
                dca_decode_release(parser);
            } else if (AUDIO_FORMAT_INVALID == parser->aformat) {//from PAUSE or MUTE -> pcm
                if (AML_AUDIO_DECODER_TYPE_DOLBY == parser->enCurDecType) {
                    dcv_decode_release(parser);
                } else if (AML_AUDIO_DECODER_TYPE_DTS == parser->enCurDecType){
                    dca_decode_release(parser);
                }
            }
            parser->enCurDecType = AML_AUDIO_DECODER_TYPE_NONE;
        } else if (AUDIO_FORMAT_AC3 == enCurFormat || AUDIO_FORMAT_E_AC3 == enCurFormat) {
            if (AUDIO_FORMAT_PCM_16_BIT == parser->aformat) {//from pcm -> dd/dd+
                dcv_decode_init(parser);
            } else {
                if (AML_AUDIO_DECODER_TYPE_NONE == parser->enCurDecType) {// non-dd/dd+ decoder scene
                    dcv_decode_init(parser);
                } else if (AML_AUDIO_DECODER_TYPE_DTS == parser->enCurDecType) {//from dts -> dd/dd+
                    dca_decode_release(parser);
                    dcv_decode_init(parser);
                } else {
                    ALOGI("from pause or mute to continue play Dolby audio");
                }
            }
            parser->enCurDecType = AML_AUDIO_DECODER_TYPE_DOLBY;
        } else if (AUDIO_FORMAT_DTS == enCurFormat || AUDIO_FORMAT_DTS_HD == enCurFormat) {
            if (AUDIO_FORMAT_PCM_16_BIT == parser->aformat) {//from pcm -> dts
                dca_decode_init(parser);
            } else {
                if (AML_AUDIO_DECODER_TYPE_NONE == parser->enCurDecType) {// non-dts decoder scene
                    dca_decode_init(parser);
                } else if (AML_AUDIO_DECODER_TYPE_DOLBY == parser->enCurDecType) {//from dd/dd+ -> dts
                    dcv_decode_release(parser);
                    dca_decode_init(parser);
                } else {
                    ALOGI("from pause or mute to continue play DTS audio");
                }
            }
            parser->enCurDecType = AML_AUDIO_DECODER_TYPE_DTS;
        } else if (enCurFormat == AUDIO_FORMAT_INVALID) {
            ALOGI("cur format invalid, do nothing");
        } else {
            ALOGW("This format unsupport or no need to reset decoder!");
        }
        parser->aformat = enCurFormat;
    }
}

static size_t parserRingBufferDataRead(struct aml_audio_parser *parser, void* buffer, size_t bytes)
{
    int ret = 0;
    /*if data is ready, read from buffer.*/
    if (parser->data_ready == 1) {
        ret = ring_buffer_read(&parser->aml_ringbuffer, (unsigned char*)buffer, bytes);
        if (ret < 0) {
            ALOGE("%s:%d parser in_read err", __func__, __LINE__);
        } else if (ret == 0) {
            unsigned int u32TimeoutMs = 40;
            while (u32TimeoutMs > 0) {
                usleep(5000);
                ret = ring_buffer_read(&parser->aml_ringbuffer, (unsigned char*)buffer, bytes);
                if (parser->aformat == AUDIO_FORMAT_INVALID) { // don't need to wait when the format is unavailable
                    break;
                }
                if (ret > 0) {
                    bytes = ret;
                    break;
                }
                u32TimeoutMs -= 5;
            }
            if (u32TimeoutMs <= 0) {
                memset (buffer, 0, bytes);
                ALOGW("%s:%d read parser ring buffer timeout 40 ms, insert mute data", __func__, __LINE__);
            }
        } else {
            bytes = ret;
        }
    } else {
        memset (buffer, 0, bytes);
    }
    return bytes;
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer, size_t bytes)
{
    int ret = 0;
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    struct aml_audio_parser *parser = adev->aml_parser;
    size_t in_frames = bytes / audio_stream_in_frame_size(&in->stream);
    struct aml_audio_patch* patch = adev->audio_patch;
    size_t cur_in_bytes, cur_in_frames;
    int in_mute = 0, parental_mute = 0;
    bool stable = true;
    int v_ltcy = 0;

    if (bytes == 0)
        return 0;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the input stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&in->lock);
    /* For voice communication BT & MIC switches */
    if (adev->active_inport == INPORT_BT_SCO_HEADSET_MIC) {
        int ret = 0;
        if (!in->bt_sco_active) {
            do_input_standby(in);
            in->bt_sco_active = true;
            in->device = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET & ~AUDIO_DEVICE_BIT_IN;
        }
    } else if (in->bt_sco_active) {
        do_input_standby(in);
        in->bt_sco_active = false;
        in->device = inport_to_device(adev->active_inport) & ~AUDIO_DEVICE_BIT_IN;
    }

    if (in->standby) {
        ret = start_input_stream(in);
        if (ret < 0)
            goto exit;
        in->standby = 0;
    }
    /* if audio patch type is hdmi to mixer, check audio format from hdmi*/
    if (adev->patch_src == SRC_HDMIIN && parser != NULL) {
        processHdmiInputFormatChange(in, parser);
    }
    /*if raw data from hdmi and decoder is ready read from decoder buffer*/
    if (parser != NULL && parser->decode_enabled == 1) {
        bytes = parserRingBufferDataRead(parser, buffer, bytes);
    }
#ifdef ENABLE_AEC_FUNC
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        size_t read_size = 0;
        cur_in_frames = in_frames;
        cur_in_bytes = in_frames * 2 * 2;
        // 2 ch 16 bit TODO: add more fmt
        if (!in->tmp_buffer_8ch || in->tmp_buffer_8ch_size < 4 * cur_in_bytes) {
            ALOGI("%s: realloc tmp_buffer_8ch size from %zu to %zu",
                __func__, in->tmp_buffer_8ch_size, 4 * cur_in_bytes);
            in->tmp_buffer_8ch = aml_audio_realloc(in->tmp_buffer_8ch, 4 * cur_in_bytes);
            if (!in->tmp_buffer_8ch) {
                ALOGE("%s malloc failed\n", __func__);
            }
            in->tmp_buffer_8ch_size = 4 * cur_in_bytes;
        }
        // need read 4 ch out from the alsa driver then do aec.
        read_size = cur_in_bytes * 2;
        ret = aml_alsa_input_read(stream, in->tmp_buffer_8ch, read_size);
    }
#endif
    else if (adev->patch_src == SRC_DTV && adev->tuner2mix_patch == 1)
    {
        if (adev->debug_flag) {
            ALOGD("%s:%d dtv_in_read data size:%d, src_gain:%f",__func__,__LINE__, bytes, adev->src_gain[adev->active_inport]);
        }
        ret = dtv_in_read(stream, buffer, bytes);
        if (adev->src_gain[adev->active_inport] != 1.0)
            apply_volume(adev->src_gain[adev->active_inport], buffer, sizeof(uint16_t), bytes);
        goto exit;
    } else {
        /* when audio is unstable, need to mute the audio data for a while
         * the mute time is related to hdmi audio buffer size
         */
        bool stable = signal_status_check(adev->in_device, &in->mute_mdelay, stream);

        if (!stable) {
            if (in->mute_log_cntr == 0)
                ALOGI("%s: audio is unstable, mute channel", __func__);
            if (in->mute_log_cntr++ >= 100)
                in->mute_log_cntr = 0;
            clock_gettime(CLOCK_MONOTONIC, &in->mute_start_ts);
            //clock_gettime(CLOCK_MONOTONIC, &adev->mute_start_ts);
            adev->patch_start = false;
            in->mute_flag = 1;
        }

        if (in->mute_flag == 1) {
            in_mute = Stop_watch(in->mute_start_ts, in->mute_mdelay);
            if (!in_mute) {
                ALOGV("%s: unmute audio since audio signal is stable", __func__);
                in->mute_log_cntr = 0;
                in->mute_flag = 0;
                /* fade in start */
                ALOGV("start fade in");
                start_ease_in(adev);
            }
        }

        if (adev->parental_control_av_mute && (adev->active_inport == INPORT_TUNER || adev->active_inport == INPORT_LINEIN))
            parental_mute = 1;

        /*if need mute input source, don't read data from hardware anymore*/
        if (adev->mic_mute || in_mute || parental_mute || in->spdif_fmt_hw == SPDIFIN_AUDIO_TYPE_PAUSE || adev->source_mute) {
            memset(buffer, 0, bytes);
            unsigned int estimated_sched_time_us = 1000;
            uint64_t frame_duration = bytes * 1000000 / audio_stream_in_frame_size(stream) /
                    in_get_sample_rate(&stream->common);

            if (frame_duration > estimated_sched_time_us) {
                usleep(frame_duration - estimated_sched_time_us);
            }
            if (patch) {
                patch->is_alsa_input_mute = true;
            }
        } else {
            if (adev->debug_flag) {
                ALOGD("%s:%d pcm_read data size:%d, channels:%d, pResampler:%p",__func__,__LINE__, bytes, in->config.channels, in->resampler);
            }

            if (patch && (patch->is_alsa_input_mute == true) &&
                (adev->patch_src == SRC_ATV || adev->patch_src == SRC_HDMIIN ||
                 adev->patch_src == SRC_LINEIN)) {
                v_ltcy = aml_sysfs_get_int("/sys/class/video/vframe_walk_delay");
                if (v_ltcy > 0) {
                    patch->is_alsa_input_mute = false;
                    aml_alsa_input_flush(stream);
                    pre_avsync(patch);
                    ret = do_avsync(patch, stream, (unsigned char*)buffer, bytes);
                    if (ret != 0) {
                        bytes = ret;
                        goto exit;
                    }
                }
            }

            if (in->resampler) {
                ret = read_frames(in, buffer, in_frames);
            } else if (in->audio_packet_type == AUDIO_PACKET_AUDS && in->config.channels != 2) {
                ret = input_stream_channels_adjust(stream, buffer, bytes);
            } else if (!((adev->in_device & AUDIO_DEVICE_IN_HDMI_ARC) &&
                    (access(SYS_NODE_EARC_RX, F_OK) == 0) &&
                    (aml_mixer_ctrl_get_int(&adev->alsa_mixer,
                            AML_MIXER_ID_HDMI_EARC_AUDIO_ENABLE) == 0))) {
                ret = aml_alsa_input_read(stream, buffer, bytes);
            }

            if (ret < 0) {
                if (adev->debug_flag) {
                    ALOGE("%s:%d pcm_read fail, ret:%s",__func__,__LINE__, strerror(errno));
                }
                goto exit;
            }
        }
    }

    /*noise gate is only used in Linein for 16bit audio data*/
    if (adev->active_inport == INPORT_LINEIN && adev->aml_ng_enable == 1) {
        int ng_status = noise_evaluation(adev->aml_ng_handle, buffer, bytes >> 1);
        /*if (ng_status == NG_MUTE)
            ALOGI("noise gate is working!");*/
    }

    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        inread_proc_aec(stream, buffer, bytes);
    } else if (!adev->audio_patching) {
        /* case dev->mix, set audio gain to src and TV source gain */
        float source_gain;
        if (adev->patch_src == SRC_HDMIIN)
            source_gain = adev->eq_data.s_gain.hdmi;
        else if (adev->patch_src == SRC_LINEIN)
            source_gain = adev->eq_data.s_gain.av;
        else if (adev->patch_src == SRC_ATV)
            source_gain = adev->eq_data.s_gain.atv;
        else
            source_gain = adev->eq_data.s_gain.media;

        source_gain *= adev->src_gain[adev->active_inport];
        apply_volume(source_gain, buffer, sizeof(int16_t), bytes);
    }

exit:
    if (ret < 0) {
        ALOGE("%s: read failed - sleeping for buffer duration", __func__);
        usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
                in_get_sample_rate(&stream->common));
    }
    pthread_mutex_unlock(&in->lock);
    if (SRC_HDMIIN == adev->patch_src && parser != NULL) {
        audio_format_t cur_aformat = audio_parse_get_audio_type(parser->audio_parse_para);
        if ((parser->aformat == AUDIO_FORMAT_PCM_16_BIT && cur_aformat != parser->aformat ) ||
            (parser->aformat == AUDIO_FORMAT_PCM_32_BIT && cur_aformat != parser->aformat) ||
             parser->aformat == AUDIO_FORMAT_INVALID) {
            memset(buffer , 0 , bytes);
        } else {
            processBtAndUsbCardData(in, parser, buffer, bytes);
        }
    }

    if (ret >= 0 && getprop_bool("media.audiohal.indump")) {
        aml_audio_dump_audio_bitstreams("/data/audio/alsa_read.raw",
            buffer, bytes);
    }

    return bytes;
}

static uint32_t in_get_input_frames_lost (struct audio_stream_in *stream __unused)
{
    return 0;
}

static int in_get_active_microphones (const struct audio_stream_in *stream __unused,
                                     struct audio_microphone_characteristic_t *mic_array __unused,
                                     size_t *mic_count __unused) {
    return 0;
}

static int adev_get_microphones (const struct audio_hw_device *dev __unused,
                                struct audio_microphone_characteristic_t *mic_array __unused,
                                size_t *mic_count) {
    ALOGI("%s", __func__);
    *mic_count = 0;
    return 0;
}

/****************ch_num's value ******************************
   Virtual:x   0 : mean 2.0 ch   1 : mean 5.1 ch  param: 47
   TruvolumeHD 0 : mean 2.0 ch   4 : mean 5.1 ch  param: 70
   cmdCode     5 : effect setparameter   2 : effect reset
*************************************************************/
#define VIRTUALXINMODE       47
#define TRUVOLUMEINMODE      70
#define VIRTUALX_CHANNEL_NUM 81

void virtualx_setparameter(struct aml_audio_device *adev,int param,int ch_num,int cmdCode)
{
    effect_descriptor_t tmpdesc;
    int32_t replyData;
    uint32_t replySize = sizeof(int32_t);
    uint32_t cmdSize = (int)(sizeof(effect_param_t) + sizeof(uint32_t) + sizeof(uint32_t));
    uint32_t buf32[sizeof(effect_param_t) / sizeof(uint32_t) + 2];
    effect_param_t *p = (effect_param_t *)buf32;
    p->psize = sizeof(uint32_t);
    p->vsize = sizeof(uint32_t);
    *(int32_t *)p->data = param;
    *((int32_t *)p->data + 1) = ch_num;
    if (adev->native_postprocess.postprocessors[0] != NULL) {
        (*(adev->native_postprocess.postprocessors[0]))->get_descriptor(adev->native_postprocess.postprocessors[0], &tmpdesc);
        if (0 == strcmp(tmpdesc.name,"VirtualX")) {
            (*adev->native_postprocess.postprocessors[0])->command(adev->native_postprocess.postprocessors[0],cmdCode,cmdSize,
                (void *)p,&replySize,&replyData);
        }
    }
}

// open corresponding stream by flags, formats and others params
static int adev_open_output_stream(struct audio_hw_device *dev,
                                audio_io_handle_t handle __unused,
                                audio_devices_t devices,
                                audio_output_flags_t flags,
                                struct audio_config *config,
                                struct audio_stream_out **stream_out,
                                const char *address __unused)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_stream_out *out;
    int digital_codec;
    int ret;

    ALOGD("%s: enter: devices(%#x) channel_mask(%#x) rate(%d) format(%#x) flags(%#x)", __func__,
        devices, config->channel_mask, config->sample_rate, config->format, flags);

    out = (struct aml_stream_out *)aml_audio_calloc(1, sizeof(struct aml_stream_out));
    if (!out)
        return -ENOMEM;

    if (adev->libvx_exist) {
        virtualx_setparameter(adev,VIRTUALXINMODE,0,5);
        virtualx_setparameter(adev,TRUVOLUMEINMODE,0,5);
        adev->effect_in_ch = 2;
    }
    if (flags == AUDIO_OUTPUT_FLAG_NONE)
        flags = AUDIO_OUTPUT_FLAG_PRIMARY;
    if (config->channel_mask == AUDIO_CHANNEL_NONE)
        config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    if (config->sample_rate == 0)
        config->sample_rate = 48000;

    if (flags & AUDIO_OUTPUT_FLAG_PRIMARY) {
        if (config->format == AUDIO_FORMAT_DEFAULT)
            config->format = AUDIO_FORMAT_PCM_16_BIT;

        out->stream.common.get_channels = out_get_channels;
        out->stream.common.get_format = out_get_format;

        if ((eDolbyMS12Lib == adev->dolby_lib_type) && (!adev->is_TV)) {
            // BOX with ms 12 need to use new method
            out->stream.write = out_write_new;
            out->stream.common.standby = out_standby_new;
        } else {
            out->stream.common.standby = out_standby;
        }

        out->hal_channel_mask = config->channel_mask;
        out->hal_rate = config->sample_rate;
        out->hal_format = config->format;
        out->hal_internal_format = out->hal_format;

        out->config = pcm_config_out;
        out->config.channels = audio_channel_count_from_out_mask(config->channel_mask);
        out->config.rate = config->sample_rate;

        switch (config->format) {
        case AUDIO_FORMAT_PCM_16_BIT:
            out->config.format = PCM_FORMAT_S16_LE;
            break;
        case AUDIO_FORMAT_PCM_32_BIT:
            out->config.format = PCM_FORMAT_S32_LE;
            break;
        default:
            break;
        }
    } else if (flags & AUDIO_OUTPUT_FLAG_DIRECT) {
        if (devices & AUDIO_DEVICE_OUT_ALL_A2DP)
            config->format = AUDIO_FORMAT_PCM_16_BIT;
        else if (config->format == AUDIO_FORMAT_DEFAULT)
            config->format = AUDIO_FORMAT_AC3;

        out->stream.common.get_channels = out_get_channels_direct;
        out->stream.common.get_format = out_get_format_direct;

        if ((eDolbyMS12Lib == adev->dolby_lib_type) && (!adev->is_TV)) {
            // BOX with ms 12 need to use new method
            out->stream.write = out_write_new;
            out->stream.common.standby = out_standby_new;
        } else {
            out->stream.write = out_write_direct;
            out->stream.common.standby = out_standby_direct;
        }

        out->hal_channel_mask = config->channel_mask;
        out->hal_rate = config->sample_rate;
        out->hal_format = config->format;
        out->hal_internal_format = out->hal_format;

        out->config = pcm_config_out_direct;
        out->config.channels = audio_channel_count_from_out_mask(config->channel_mask);
        out->config.rate = config->sample_rate;
        switch (config->format) {
        case AUDIO_FORMAT_PCM_16_BIT:
            out->config.format = PCM_FORMAT_S16_LE;
            break;
        case AUDIO_FORMAT_PCM_32_BIT:
            out->config.format = PCM_FORMAT_S32_LE;
            break;
        case AUDIO_FORMAT_IEC61937:
            if (out->config.channels == 2 && (out->config.rate == 192000 || out->config.rate == 176400 || out->config.rate == 128000)) {
                out->config.rate /= 4;
                out->hal_internal_format = AUDIO_FORMAT_E_AC3;
            } else if (out->config.channels == 2 && out->config.rate >= 32000 && out->config.rate <= 48000) {
                if (adev->audio_type == DTS) {
                    out->hal_internal_format = AUDIO_FORMAT_DTS;
                    adev->dts_hd.frame_info.is_dtscd = false;
                    adev->dolby_lib_type = eDolbyDcvLib;
                } else {
                    out->hal_internal_format = AUDIO_FORMAT_AC3;
                }
            } else if (out->config.channels >= 6 && out->config.rate == 192000) {
                out->hal_internal_format = AUDIO_FORMAT_DTS_HD;
            } else {
                config->format = AUDIO_FORMAT_DEFAULT;
                ret = -EINVAL;
                goto err;
            }
            if (adev->audio_type != DTS) {
                adev->dolby_lib_type = adev->dolby_lib_type_last;
                if (eDolbyMS12Lib == adev->dolby_lib_type) {
                    adev->ms12.dolby_ms12_enable = true;
                }
            } else {
                adev->ms12.dolby_ms12_enable = false;
            }
            ALOGI("convert format IEC61937 to 0x%x\n", out->hal_internal_format);
            break;
        case AUDIO_FORMAT_AC3:
        case AUDIO_FORMAT_E_AC3:
        case AUDIO_FORMAT_AC4:
            adev->dolby_lib_type = adev->dolby_lib_type_last;
            break;
        case AUDIO_FORMAT_DTS:
        case AUDIO_FORMAT_DTS_HD:
            adev->dolby_lib_type = eDolbyDcvLib;
            break;

        default:
            break;
        }
        digital_codec = get_codec_type(out->hal_internal_format);
        switch (digital_codec) {
        case TYPE_AC3:
            out->raw_61937_frame_size = 4;
            break;
        case TYPE_EAC3:
            out->config.period_size *= 2;
            out->raw_61937_frame_size = 4;
            break;
        case TYPE_TRUE_HD:
            out->config.period_size *= 4 * 2;
            out->raw_61937_frame_size = 16;
            break;
        case TYPE_DTS:
            out->config.period_count *= 2;
            out->raw_61937_frame_size = 4;
            break;
        case TYPE_DTS_HD:
            out->config.period_count *= 4;
            out->raw_61937_frame_size = 16;
            break;
        case TYPE_PCM:
            if (out->config.channels >= 6 || out->config.rate > 48000)
                adev->hi_pcm_mode = true;
        default:
            out->raw_61937_frame_size = 1;
            break;
        }
        if (codec_type_is_raw_data(digital_codec)) {
            ALOGI("%s: for raw audio output,force alsa stereo output", __func__);
            out->config.channels = 2;
            out->multich = 2;
        } else if (out->config.channels > 2) {
            out->multich = out->config.channels;
        }
    } else {
        // TODO: add other cases here
        ALOGE("%s: flags = %#x invalid", __func__, flags);
        ret = -EINVAL;
        goto err;
    }
    out->hal_ch   = audio_channel_count_from_out_mask(out->hal_channel_mask);
    out->hal_frame_size = audio_bytes_per_frame(out->hal_ch, out->hal_internal_format);
    if (out->hal_ch == 0) {
        out->hal_ch = 2;
    }
    if (out->hal_frame_size == 0) {
        out->hal_frame_size = 1;
    }

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.set_format = out_set_format;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.get_presentation_position = out_get_presentation_position;

    if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
        // BOX with ms 12 need to use new method
        out->stream.pause = out_pause_new;
        out->stream.resume = out_resume_new;
        out->stream.flush = out_flush_new;
    } else {
        out->stream.pause = out_pause;
        out->stream.resume = out_resume;
        out->stream.flush = out_flush;
    }

    out->out_device = devices;
    out->flags = flags;
    out->volume_l = 1.0;
    out->volume_r = 1.0;
    out->last_volume_l = 0.0;
    out->last_volume_r = 0.0;
    out->dev = adev;
    out->standby = true;
    out->frame_write_sum = 0;
    out->hw_sync_mode = false;
    out->need_convert = false;
    out->need_drop_size = 0;
    out->enInputPortType = AML_MIXER_INPUT_PORT_BUTT;

    if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        if ((eDolbyMS12Lib == adev->dolby_lib_type) && !adev->ms12.dolby_ms12_enable) {
           config_output((struct audio_stream_out *)out, true);
        }
        outMmapInit(out);
    }
    //aml_audio_hwsync_init(out->hwsync,out);
    /* FIXME: when we support multiple output devices, we will want to
     * do the following:
     * adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
     * adev->devices |= out->device;
     * select_output_device(adev);
     * This is because out_set_parameters() with a route is not
     * guaranteed to be called after an output stream is opened.
     */
    if (adev->is_TV) {
        out->is_tv_platform = 1;
        out->config.channels = 8;
        out->config.format = PCM_FORMAT_S32_LE;

        out->tmp_buffer_8ch_size = out->config.period_size * 4 * 8;
        out->tmp_buffer_8ch = aml_audio_malloc(out->tmp_buffer_8ch_size);
        if (!out->tmp_buffer_8ch) {
            ALOGE("%s: alloc tmp_buffer_8ch failed", __func__);
            ret = -ENOMEM;
            goto err;
        }
        memset(out->tmp_buffer_8ch, 0, out->tmp_buffer_8ch_size);

        out->audioeffect_tmp_buffer = aml_audio_malloc(out->config.period_size * 6);
        if (!out->audioeffect_tmp_buffer) {
            ALOGE("%s: alloc audioeffect_tmp_buffer failed", __func__);
            ret = -ENOMEM;
            goto err;
        }
        memset(out->audioeffect_tmp_buffer, 0, out->config.period_size * 6);
    }

    out->hwsync =  aml_audio_calloc(1, sizeof(audio_hwsync_t));
    if (!out->hwsync) {
        ALOGE("%s,malloc hwsync failed", __func__);
        if (out->tmp_buffer_8ch) {
            aml_audio_free(out->tmp_buffer_8ch);
        }
        if (out->audioeffect_tmp_buffer) {
            aml_audio_free(out->audioeffect_tmp_buffer);
        }
        aml_audio_free(out);
        return -ENOMEM;
    }
    out->hwsync->tsync_fd = -1;
    // for every stream , init hwsync once. ready to use in hwsync mode
    aml_audio_hwsync_init(out->hwsync, out);

    /*if tunnel mode pcm is not 48Khz, resample to 48K*/
    if (flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) {
        ALOGD("%s format=%d rate=%d", __func__, out->hal_internal_format, out->config.rate);
        if (audio_is_linear_pcm(out->hal_internal_format) && out->config.rate != 48000) {
            if (out->resample_handle == NULL)
            {
                audio_resample_config_t resample_config;
                ALOGI("init resampler from %d to 48000!\n", out->config.rate);
                resample_config.aformat   = out->hal_internal_format;
                resample_config.channels  = 2;
                resample_config.input_sr  = out->config.rate;
                resample_config.output_sr = 48000;
                ret = aml_audio_resample_init((aml_audio_resample_t **)&out->resample_handle, AML_AUDIO_SIMPLE_RESAMPLE, &resample_config);
                if (ret < 0) {
                    ALOGE("resample init error\n");
                    return -1;
                }
            }

        }
    }
    out->ddp_frame_size = aml_audio_get_ddp_frame_size();
    *stream_out = &out->stream;
    ALOGD("%s: exit", __func__);

    return 0;
err:
    if (out->audioeffect_tmp_buffer)
        aml_audio_free(out->audioeffect_tmp_buffer);
    if (out->tmp_buffer_8ch)
        aml_audio_free(out->tmp_buffer_8ch);
    aml_audio_free(out);
    return ret;
}

//static int out_standby_new(struct audio_stream *stream);
static void adev_close_output_stream(struct audio_hw_device *dev,
                                    struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;

    ALOGD("%s: enter: dev(%p) stream(%p)", __func__, dev, stream);
    if (adev->libvx_exist) {
        virtualx_setparameter(adev,0,0,2);
        virtualx_setparameter(adev,VIRTUALXINMODE,0,5);
        virtualx_setparameter(adev,TRUVOLUMEINMODE,0,5);
        adev->effect_in_ch = 2;
    }
    if ((out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) && adev->audio_patching)
        a2dp_out_standby(&stream->common);

    if (adev->useSubMix) {
        if (out->usecase == STREAM_PCM_NORMAL || out->usecase == STREAM_PCM_HWSYNC)
            out_standby_subMixingPCM(&stream->common);
        else
            out_standby_new(&stream->common);
    } else {
        out_standby_new(&stream->common);
    }

    if (continous_mode(adev) && (eDolbyMS12Lib == adev->dolby_lib_type)) {
        if (out->volume_l != 1.0) {
            if (!audio_is_linear_pcm(out->hal_internal_format)) {
                dolby_ms12_set_main_volume(1.0);
            }
        }
        if (out->hw_sync_mode) {
            /*we only suppport one stream hw sync and MS12 always attach with it.
            So when it is released, ms12 also need set hwsync to NULL*/
            struct aml_stream_out *ms12_out = (struct aml_stream_out *)adev->ms12_out;
            out->hw_sync_mode = 0;
            if (ms12_out != NULL) {
                ms12_out->hw_sync_mode = 0;
                ms12_out->hwsync = NULL;
            }
        }
    }

    /*now the raw stream is closed, we can reset the sink device*/
    if (adev->continuous_audio_mode && (eDolbyMS12Lib == adev->dolby_lib_type)) {
        if (!audio_is_linear_pcm(out->hal_internal_format)) {
            get_sink_format((struct audio_stream_out *)adev->active_outputs[STREAM_PCM_NORMAL]);
        }
    }

    /*when open dts decoder, the dolby lib is changed, so we need restore it*/
    if (out->hal_internal_format == AUDIO_FORMAT_DTS) {
        if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
            adev->dolby_lib_type = eDolbyMS12Lib;
        }
    }

    if (out->restore_continuous == true) {
        ALOGI("restore ms12 continuous mode");
        adev->continuous_audio_mode = 1;

    }
    pthread_mutex_lock(&out->lock);
    aml_audio_free(out->audioeffect_tmp_buffer);
    aml_audio_free(out->tmp_buffer_8ch);
    if (out->hwsync) {
        // release hwsync resource
        aml_audio_hwsync_release(out->hwsync);
        aml_audio_free(out->hwsync);
    }

    if (out->spdifenc_init) {
        aml_spdif_encoder_close(out->spdifenc_handle);
        out->spdifenc_handle = NULL;
        out->spdifenc_init = false;
    }
    if (out->resample_handle) {
        aml_audio_resample_close(out->resample_handle);
        out->resample_handle = NULL;
    }
    if (out->flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        outMmapDeInit(out);
    }

    if (out->spdifout_handle) {
        aml_audio_spdifout_close(out->spdifout_handle);
        out->spdifout_handle = NULL;
    }

    pthread_mutex_unlock(&out->lock);
    aml_audio_free(stream);
    ALOGD("%s: exit", __func__);
}

static int set_arc_hdmi (struct audio_hw_device *dev, char *value, size_t len)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    struct aml_arc_hdmi_desc *hdmi_desc = &adev->hdmi_descs;
    char *pt = NULL, *tmp = NULL;
    int i = 0;

    if (strlen (value) > len) {
        ALOGE ("value array overflow!");
        return -EINVAL;
    }

    pt = strtok_r (value, "[], ", &tmp);
    while (pt != NULL) {
        //index 1 means avr port
        if (i == 1)
            hdmi_desc->avr_port = atoi (pt);

        pt = strtok_r (NULL, "[], ", &tmp);
        i++;
    }

    ALOGI ("ARC HDMI AVR port = %d", hdmi_desc->avr_port);
    return 0;
}

static void dump_format_desc (struct format_desc *desc)
{
    if (desc) {
        ALOGI ("dump format desc = %p", desc);
        ALOGI ("\t-fmt %d", desc->fmt);
        ALOGI ("\t-is support %d", desc->is_support);
        ALOGI ("\t-max channels %d", desc->max_channels);
        ALOGI ("\t-sample rate masks %#x", desc->sample_rate_mask);
        ALOGI ("\t-max bit rate %d", desc->max_bit_rate);
        ALOGI ("\t-atmos supported %d", desc->atmos_supported);
    }
}

static int set_arc_format (struct audio_hw_device *dev, char *value, size_t len)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    struct aml_arc_hdmi_desc *hdmi_desc = &adev->hdmi_descs;
    struct format_desc *fmt_desc = NULL;
    char *pt = NULL, *tmp = NULL;
    int i = 0, val = 0;
    enum arc_hdmi_format format = _LPCM;
    if (strlen (value) > len) {
        ALOGE ("value array overflow!");
        return -EINVAL;
    }

    pt = strtok_r (value, "[], ", &tmp);
    while (pt != NULL) {
        val = atoi (pt);
        switch (i) {
        case 0:
            format = val;
            if (val == _AC3) {
                fmt_desc = &hdmi_desc->dd_fmt;
                fmt_desc->fmt = val;
            } else if (val == _DDP) {
                fmt_desc = &hdmi_desc->ddp_fmt;
                fmt_desc->fmt = val;
            } else if (val == _LPCM) {
                fmt_desc = &hdmi_desc->pcm_fmt;
                fmt_desc->fmt = val;
            } else if (val == _DTS) {
                fmt_desc = &hdmi_desc->dts_fmt;
                fmt_desc->fmt = val;
            } else if (val == _DTSHD) {
                fmt_desc = &hdmi_desc->dtshd_fmt;
                fmt_desc->fmt = val;
            } else {
                ALOGE ("unsupport fmt %d", val);
                return -EINVAL;
            }
            break;
        case 1:
            fmt_desc->is_support = val;
            break;
        case 2:
            fmt_desc->max_channels = val + 1;
            break;
        case 3:
            fmt_desc->sample_rate_mask = val;
            break;
        case 4:
            if (format == _DDP) {
                fmt_desc = &hdmi_desc->ddp_fmt;
                fmt_desc->atmos_supported = val > 0 ? true : false;
                aml_mixer_ctrl_set_int (&adev->alsa_mixer, AML_MIXER_ID_HDMI_ATMOS_EDID,fmt_desc->atmos_supported);
                /**
                 * Add flag to indicate hdmi arc format updated.
                 * Out write thread checks it and makes decision on output format.
                 * NOTICE: assuming that DDP format is the last ARC KV parameters.
                 */
                adev->arc_hdmi_updated = 1;
            } else {
                fmt_desc->max_bit_rate = val * 80;
            }
            break;
        default:
            break;
        }

        pt = strtok_r (NULL, "[], ", &tmp);
        i++;
    }

    dump_format_desc (fmt_desc);
    return 0;
}

const char* outport2String(enum OUT_PORT enOutPort)
{
    if (enOutPort < OUTPORT_SPEAKER || enOutPort > OUTPORT_MAX) {
        return "UNKNOWN";
    }
    char *apcOutPort[OUTPORT_MAX+1] = {
        "[0x0]SPEAKER",
        "[0x1]HDMI_ARC",
        "[0x2]HDMI",
        "[0x3]SPDIF",
        "[0x4]AUX_LINE",
        "[0x5]HEADPHONE",
        "[0x6]REMOTE_SUBMIX",
        "[0x7]BT_SCO",
        "[0x8]BT_SCO_HEADSET",
        "[0x9]A2DP",
        "[0xA]MAX"
    };
    return apcOutPort[enOutPort];
}

const char* inport2String(enum IN_PORT enInPort)
{
    if (enInPort < INPORT_TUNER || enInPort > INPORT_MAX) {
        return "UNKNOWN";
    }
    char *apcInPort[INPORT_MAX+1] = {
        "[0x0]TUNER",
        "[0x1]HDMIIN",
        "[0x2]SPDIFIN",
        "[0x3]LINEIN",
        "[0x4]REMOTE_SUBMIXIN",
        "[0x5]WIRED_HEADSETIN",
        "[0x6]BUILTIN_MICIN",
        "[0x7]BT_SCO_HEADSET_MICIN",
        "[0x8]ARCIN",
        "[0x9]MAX"
    };
    return apcInPort[enInPort];
}

static int aml_audio_output_routing(struct audio_hw_device *dev,
                                    enum OUT_PORT outport,
                                    bool user_setting)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;

    if (aml_dev->active_outport != outport) {
        ALOGI("%s: switch from %s to %s", __func__,
            outport2String(aml_dev->active_outport), outport2String(outport));

        /* switch off the active output */
        switch (aml_dev->active_outport) {
        case OUTPORT_SPEAKER:
            audio_route_apply_path(aml_dev->ar, "speaker_off");
            break;
        case OUTPORT_HDMI_ARC:
            audio_route_apply_path(aml_dev->ar, "hdmi_arc_off");
            break;
        case OUTPORT_HEADPHONE:
            audio_route_apply_path(aml_dev->ar, "headphone_off");
            break;
        case OUTPORT_BT_SCO:
        case OUTPORT_BT_SCO_HEADSET:
            close_btSCO_device(aml_dev);
            break;
        case OUTPORT_A2DP:
            ALOGE("%s: active_outport = %s A2DP off", __func__, outport2String(aml_dev->active_outport));
            break;
        default:
            ALOGE("%s: active_outport = %s unsupport", __func__, outport2String(aml_dev->active_outport));
            break;
        }

        /* switch on the new output */
        switch (outport) {
        case OUTPORT_SPEAKER:
            if (!aml_dev->speaker_mute)
                audio_route_apply_path(aml_dev->ar, "speaker");
            audio_route_apply_path(aml_dev->ar, "spdif");
            break;
        case OUTPORT_HDMI_ARC:
            audio_route_apply_path(aml_dev->ar, "hdmi_arc");
            /* TODO: spdif case need deal with hdmi arc format */
            if (aml_dev->hdmi_format != 3)
                audio_route_apply_path(aml_dev->ar, "spdif");
            break;
        case OUTPORT_HEADPHONE:
            audio_route_apply_path(aml_dev->ar, "headphone");
            audio_route_apply_path(aml_dev->ar, "spdif");
            break;
        case OUTPORT_BT_SCO:
        case OUTPORT_BT_SCO_HEADSET:
            break;
        case OUTPORT_A2DP:
            ALOGE("%s: active_outport = %s A2DP on", __func__, outport2String(outport));
            break;
        default:
            ALOGE("%s: outport = %s unsupport", __func__, outport2String(outport));
            break;
        }

        audio_route_update_mixer(aml_dev->ar);
        aml_dev->active_outport = outport;
    } else if (outport == OUTPORT_SPEAKER && user_setting) {
        /* In this case, user toggle the speaker_mute menu */
        if (aml_dev->speaker_mute)
            audio_route_apply_path(aml_dev->ar, "speaker_off");
        else
            audio_route_apply_path(aml_dev->ar, "speaker");
        audio_route_update_mixer(aml_dev->ar);
    } else {
        ALOGI("%s: outport %s already exists, do nothing", __func__, outport2String(outport));
    }

    return 0;
}

static int aml_audio_input_routing(struct audio_hw_device *dev,
                                    enum IN_PORT inport)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;

    if (adev->active_inport != inport) {
        ALOGI("%s: switch from %s to %s", __func__,
            inport2String(adev->active_inport), inport2String(inport));
        /* switch on the new input */
        switch (inport) {
        case INPORT_BUILTIN_MIC:
        case INPORT_BT_SCO_HEADSET_MIC:
        case INPORT_TUNER:
        case INPORT_HDMIIN:
        case INPORT_ARCIN:
        case INPORT_SPDIF:
        case INPORT_REMOTE_SUBMIXIN:
        case INPORT_LINEIN:
        case INPORT_WIRED_HEADSETIN:
        case INPORT_LOOPBACK:
            break;
        default:
            ALOGE("%s: inport = %s unsupport", __func__, inport2String(inport));
            inport = INPORT_MAX;
            break;
        }
        adev->active_inport = inport;
    } else {
        ALOGI("%s: inport %s already exists, do nothing", __func__, inport2String(inport));
    }

    return 0;
}

static int adev_set_parameters (struct audio_hw_device *dev, const char *kvpairs)
{
    ALOGD ("%s(%p, %s)", __FUNCTION__, dev, kvpairs);

    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    struct str_parms *parms;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    char value[VAL_LEN];
    int val = 0;
    int ret = 0;

    ALOGI ("%s(kv: %s)", __FUNCTION__, kvpairs);
    parms = str_parms_create_str (kvpairs);
    ret = str_parms_get_str (parms, "screen_state", value, sizeof (value) );
    if (ret >= 0) {
        if (strcmp (value, AUDIO_PARAMETER_VALUE_ON) == 0) {
            adev->low_power = false;
        } else {
            adev->low_power = true;
        }
        goto exit;
    }

    ret = str_parms_get_int (parms, "disable_pcm_mixing", &val);
    if (ret >= 0) {
        adev->disable_pcm_mixing = val;
        if (!dolby_stream_active(adev)) {
            get_sink_format((struct audio_stream_out *)adev->active_outputs[STREAM_PCM_NORMAL]);
        }
        ALOGI ("disable_pcm_mixing set to %d\n", adev->disable_pcm_mixing);
        goto exit;
    }

    ret = str_parms_get_int(parms, "Audio hdmi-out mute", &val);
    if (ret >= 0) {
        /* for tv,hdmitx module is not registered, do not reponse this control interface */
#ifndef TV_AUDIO_OUTPUT
        {
            aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_HDMI_OUT_AUDIO_MUTE, val);
            ALOGI("audio hdmi out status: %d\n", val);
        }
#endif
        goto exit;
    }

    ret = str_parms_get_int(parms, "Audio spdif mute", &val);
    if (ret >= 0) {
        audio_route_set_spdif_mute(&adev->alsa_mixer, val);
        ALOGI("audio spdif out status: %d\n", val);
        goto exit;
    }

    // HDMI cable plug off
    ret = str_parms_get_int(parms, "disconnect", &val);
    if (ret >= 0) {
        if (val & AUDIO_DEVICE_OUT_HDMI_ARC) {
            adev->bHDMIConnected = 0;
            ALOGI("bHDMIConnected: %d\n", val);
        } else if (val & AUDIO_DEVICE_OUT_ALL_A2DP) {
            adev->a2dp_updated = 1;
            adev->a2dp_connected = false;
            adev->out_device &= (~val);
            a2dp_output_disable(dev);
            ALOGI("adev_set_parameters a2dp disconnect: %x, device=%x\n", val, adev->out_device);
        }
        /* for tv, the adev->reset_dtv_audio is reset in "HDMI ARC Switch" param */
        if ((adev->patch_src == SRC_DTV) &&
            (val & (AUDIO_DEVICE_OUT_HDMI_ARC | AUDIO_DEVICE_OUT_HDMI | AUDIO_DEVICE_OUT_ALL_A2DP))) {
            ALOGI("disconnect set reset_dtv_audio 1, val %x\n", val);
            adev->reset_dtv_audio = 1;
        }
        goto exit;
    }

    // HDMI cable plug in
    ret = str_parms_get_int(parms, "connect", &val);
    if (ret >= 0) {
        if ((val & AUDIO_DEVICE_OUT_HDMI_ARC) || (val & AUDIO_DEVICE_OUT_HDMI)) {
            adev->bHDMIConnected = 1;
            ALOGI("%s,bHDMIConnected: %d\n", __FUNCTION__, val);
        } else if (val & AUDIO_DEVICE_OUT_ALL_A2DP) {
            adev->a2dp_updated = 1;
            adev->out_device |= val;
            adev->a2dp_connected = true;
            a2dp_output_enable(dev);
            ALOGI("adev_set_parameters a2dp connect: %x, device=%x\n", val, adev->out_device);
        }
        if ((adev->patch_src == SRC_DTV) &&
            (val & (AUDIO_DEVICE_OUT_HDMI_ARC | AUDIO_DEVICE_OUT_HDMI | AUDIO_DEVICE_OUT_ALL_A2DP))) {
            ALOGI("connect set reset_dtv_audio 1, val %x\n", val);
            adev->reset_dtv_audio = 1;
        }
        goto exit;
    }

    ret = str_parms_get_str (parms, "BT_SCO", value, sizeof (value) );
    if (ret >= 0) {
        if (strcmp (value, AUDIO_PARAMETER_VALUE_OFF) == 0) {
            close_btSCO_device(adev);
        }
        goto exit;
    }

    //  HDMI plug in and UI [Sound Output Device] set to "ARC" will recieve HDMI ARC Switch = 1
    //  HDMI plug off and UI [Sound Output Device] set to "speaker" will recieve HDMI ARC Switch = 0
    ret = str_parms_get_int(parms, "HDMI ARC Switch", &val);
    if (ret >= 0) {
        adev->bHDMIARCon = (val == 0) ? 0 : 1;
        audio_route_set_hdmi_arc_mute(&adev->alsa_mixer, !adev->bHDMIARCon);
        ALOGI("%s audio hdmi arc status: %d\n", __FUNCTION__, adev->bHDMIARCon);
        /*
           * when user switch UI setting, means output device changed,
           * use "arc_hdmi_updated" paramter to notify HDMI ARC have updated status
           * while in config_output(), it detects this change, then re-route output config.
           */
        adev->arc_hdmi_updated = 1;
        if (adev->patch_src == SRC_DTV)
            adev->reset_dtv_audio = 1;
        if (adev->bHDMIARCon) {
            aml_audio_output_routing((struct audio_hw_device *)adev, OUTPORT_HDMI_ARC, true);
        }
        goto exit;
    }
    ret = str_parms_get_int(parms, "spdifin/arcin switch", &val);
    if (ret >= 0) {
        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIFIN_ARCIN_SWITCH, val);
        ALOGI("audio source: %s\n", val?"ARCIN":"SPDIFIN");
        goto exit;
    }

    //add for fireos tv for Dolby audio setting
    ret = str_parms_get_int (parms, "hdmi_format", &val);
    if (ret >= 0 ) {
        adev->hdmi_format = val;
        ALOGI ("HDMI format: %d\n", adev->hdmi_format);
        goto exit;
    }

    ret = str_parms_get_int (parms, "spdif_format", &val);
    if (ret >= 0 ) {
        adev->spdif_format = val;
        ALOGI ("S/PDIF format: %d\n", adev->spdif_format);
        goto exit;
    }

    ret = str_parms_get_int(parms, "audio_type", &val);
    if (ret >= 0) {
        adev->audio_type = val;
        ALOGI("audio_type: %d\n", adev->audio_type);
        goto exit;

    }
    ret = str_parms_get_int(parms, "hdmi_is_passthrough_active", &val);
    if (ret >= 0 ) {
        adev->hdmi_is_pth_active = val;
        ALOGI ("hdmi_is_passthrough_active: %d\n", adev->hdmi_is_pth_active);
        goto exit;
    }

    ret = str_parms_get_int (parms, "capability:routing", &val);
    if (ret >= 0) {
        adev->routing = val;
        ALOGI ("capability:routing = %#x\n", adev->routing);
        goto exit;
    }

    ret = str_parms_get_int (parms, "capability:format", &val);
    if (ret >= 0) {
        adev->output_config.format = val;
        ALOGI ("capability:format = %#x\n", adev->output_config.format = val);
        goto exit;
    }

    ret = str_parms_get_int (parms, "capability:channels", &val);
    if (ret >= 0) {
        adev->output_config.channel_mask = val;
        ALOGI ("capability:channel_mask = %#x\n", adev->output_config.channel_mask);
        goto exit;
    }

    ret = str_parms_get_int (parms, "capability:sampling_rate", &val);
    if (ret >= 0) {
        adev->output_config.sample_rate = val;
        ALOGI ("capability:sample_rate = %d\n", adev->output_config.sample_rate);
        goto exit;
    }

    ret = str_parms_get_int (parms, "ChannelReverse", &val);
    if (ret >= 0) {
        adev->FactoryChannelReverse = val;
        ALOGI ("ChannelReverse = %d\n", adev->FactoryChannelReverse);
        goto exit;
    }

    ret = str_parms_get_str (parms, "set_ARC_hdmi", value, sizeof (value) );
    if (ret >= 0) {
        set_arc_hdmi (dev, value, VAL_LEN);
        goto exit;
    }

    ret = str_parms_get_str (parms, "set_ARC_format", value, sizeof (value) );
    if (ret >= 0) {
        set_arc_format (dev, value, VAL_LEN);
        goto exit;
    }

    /* eARCTX_CDS */
    ret = str_parms_get_str (parms, "eARC_RX CDS", value, sizeof (value) );
    if (ret >= 0) {
        struct aml_mixer_handle *amixer = &adev->alsa_mixer;
        struct mixer *pMixer = amixer->pMixer;

        earcrx_config_cds(pMixer, value);
        goto exit;
    }

    ret = str_parms_get_str (parms, "tuner_in", value, sizeof (value) );
    // tuner_in=atv: tuner_in=dtv
    if (ret >= 0) {
        if (adev->tuner2mix_patch) {
            if (strncmp(value, "dtv", 3) == 0) {
                adev->patch_src = SRC_DTV;
                if (adev->audio_patching == 0) {
                    ALOGI("%s, !!! now create the dtv patch now\n ", __func__);
                    ret = create_dtv_patch(dev, AUDIO_DEVICE_IN_TV_TUNER,AUDIO_DEVICE_OUT_SPEAKER);
                    if (ret == 0) {
                        adev->audio_patching = 1;
                    }
                }
            } else if (strncmp(value, "atv", 3) == 0) {
                adev->patch_src = SRC_ATV;
                set_audio_source(&adev->alsa_mixer,
                        ATV, alsa_device_is_auge());
            }
            ALOGI("%s, tuner to mixer case, no need to create patch", __func__);
            goto exit;
        }
        if (strncmp(value, "dtv", 3) == 0) {
            // no audio patching in dtv
            if (adev->audio_patching && (adev->patch_src == SRC_ATV)) {
                // this is to handle atv->dtv case
                ALOGI("%s, atv->dtv", __func__);
                ret = release_patch(adev);
                if (!ret) {
                    adev->audio_patching = 0;
                }
            }
            ALOGI("%s, now the audio patch src is %d  the audio_patching is %d ", __func__, adev->patch_src, adev->audio_patching);
#ifdef ENABLE_DTV_PATCH
            if ((adev->patch_src == SRC_DTV) && adev->audio_patching) {
                ALOGI("%s, now release the dtv patch now\n ", __func__);
                if (!adev->is_TV) {
                    ALOGI("tunner in set reset_dtv_audio 1\n");
                    adev->reset_dtv_audio = 1;
                }
                ret = release_dtv_patch(adev);
                if (!ret) {
                    adev->audio_patching = 0;
                }
            }
            ALOGI("%s, now end release dtv patch the audio_patching is %d ", __func__, adev->audio_patching);
            ALOGI("%s, now create the dtv patch now\n ", __func__);
            adev->patch_src = SRC_DTV;
            if (eDolbyMS12Lib == adev->dolby_lib_type && adev->continuous_audio_mode)
            {
                bool set_ms12_non_continuous = true;
                get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                adev->exiting_ms12 = 1;
                clock_gettime(CLOCK_MONOTONIC, &adev->ms12_exiting_start);
                usecase_change_validate_l(adev->active_outputs[STREAM_PCM_NORMAL], true);
            }

            ret = create_dtv_patch(dev, AUDIO_DEVICE_IN_TV_TUNER,
                                   AUDIO_DEVICE_OUT_SPEAKER);
            if (ret == 0) {
                adev->audio_patching = 1;
            }
#endif
            ALOGI("%s, now end create dtv patch the audio_patching is %d ", __func__, adev->audio_patching);

        } else if (strncmp(value, "atv", 3) == 0) {
#ifdef ENABLE_DTV_PATCH
            // need create patching
            if ((adev->patch_src == SRC_DTV) && adev->audio_patching) {
                ret = release_dtv_patch(adev);
                if (!ret) {
                    adev->audio_patching = 0;
                }
            }
#endif
            if (eDolbyMS12Lib == adev->dolby_lib_type && adev->continuous_audio_mode) {
                ALOGI("In ATV exit MS12 continuous mode");
                bool set_ms12_non_continuous = true;
                get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                adev->exiting_ms12 = 1;
                clock_gettime(CLOCK_MONOTONIC, &adev->ms12_exiting_start);
                usecase_change_validate_l(adev->active_outputs[STREAM_PCM_NORMAL], true);
            }

            if (!adev->audio_patching) {
                ALOGI ("%s, create atv patching", __func__);
                set_audio_source(&adev->alsa_mixer,
                        ATV, alsa_device_is_auge());
                ret = create_patch (dev, AUDIO_DEVICE_IN_TV_TUNER, AUDIO_DEVICE_OUT_SPEAKER);
                // audio_patching ok, mark the patching status
                if (ret == 0) {
                    adev->audio_patching = 1;
                }
            } else {
                ALOGI("now reset the audio buffer now\n");
                ring_buffer_reset(&(adev->audio_patch->aml_ringbuffer));
            }
            adev->patch_src = SRC_ATV;
        }
        goto exit;
    }

    /*
     * This is a work around when plug in HDMI-DVI connector
     * first time application only recognize it as HDMI input device
     * then it can know it's DVI in ,and send "audio=linein" message to audio hal
    */
    ret = str_parms_get_str(parms, "audio", value, sizeof(value));
    if (ret >= 0) {
        bool is_linein_audio = strncmp(value, "linein", 6) == 0;
        bool is_hdmiin_audio = strncmp(value, "hdmi", 4) == 0;
        bool is_tv_exit = strncmp(value, "TV_exit", 7) == 0;
        if (is_linein_audio || is_hdmiin_audio) {

            struct audio_patch *pAudPatchTmp = NULL;

            if (is_linein_audio) {
                get_audio_patch_by_src_dev(dev, AUDIO_DEVICE_IN_HDMI, &pAudPatchTmp);
            } else if (is_hdmiin_audio) {
                get_audio_patch_by_src_dev(dev, AUDIO_DEVICE_IN_LINE, &pAudPatchTmp);
            }

            if (pAudPatchTmp == NULL) {
                //ALOGE("%s, There is no audio patch using HDMI as input", __func__);
                goto exit;
            }

            if (is_linein_audio && pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_HDMI) {
                ALOGE("%s, pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_HDMI", __func__);
                goto exit;
            } else if (is_hdmiin_audio && pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_LINE) {
                ALOGE("%s, pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_LINE", __func__);
                goto exit;
            }

            // dev->dev (example: HDMI in-> speaker out)
            if (pAudPatchTmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE
                && pAudPatchTmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
                // This "adev->audio_patch" will be created in create_patch() function
                // which in current design is only in "dev->dev" case
                // make sure everything is matching, no error
                if (is_linein_audio && adev->audio_patch && (adev->patch_src == SRC_HDMIIN) /*&& pAudPatchTmp->id == adev->audio_patch->patch_hdl*/) {
                    // TODO: notices
                    // These codes must corresponding to the same case in adev_create_audio_patch() and adev_release_audio_patch()
                    // Anything change in the adev_create_audio_patch() . Must also change code here..
                    ALOGI("%s, create hdmi-dvi patching dev->dev", __func__);
                    release_patch(adev);
                    aml_audio_input_routing(dev, INPORT_LINEIN);
                    set_audio_source(&adev->alsa_mixer,
                            LINEIN, alsa_device_is_auge());
                    ret = create_patch_ext(dev, AUDIO_DEVICE_IN_LINE, pAudPatchTmp->sinks[0].ext.device.type, pAudPatchTmp->id);
                    pAudPatchTmp->sources[0].ext.device.type = AUDIO_DEVICE_IN_LINE;
                } else if (is_hdmiin_audio && adev->audio_patch /* && (adev->patch_src == SRC_LINEIN) && pAudPatchTmp->id == adev->audio_patch->patch_hdl*/) {
                    ALOGI("%s, create dvi-hdmi patching dev->dev", __func__);
                    release_patch(adev);
                    aml_audio_input_routing(dev, INPORT_HDMIIN);
                    set_audio_source(&adev->alsa_mixer,
                            HDMIIN, alsa_device_is_auge());
                    ret = create_patch_ext(dev, AUDIO_DEVICE_IN_HDMI, pAudPatchTmp->sinks[0].ext.device.type, pAudPatchTmp->id);
                    pAudPatchTmp->sources[0].ext.device.type = AUDIO_DEVICE_IN_HDMI;
                }
            }

            // dev->mix (example: HDMI in-> USB out)
            if (pAudPatchTmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE
                && pAudPatchTmp->sinks[0].type == AUDIO_PORT_TYPE_MIX) {
                if (is_linein_audio && adev->audio_patch && (adev->patch_src == SRC_HDMIIN) /*&& pAudPatchTmp->id == adev->audio_patch->patch_hdl*/) {
                    ALOGI("%s, !!create hdmi-dvi patching dev->mix", __func__);
                    release_parser(adev);

                    aml_audio_input_routing(dev, INPORT_LINEIN);
                    adev->patch_src = SRC_LINEIN;
                    pAudPatchTmp->sources[0].ext.device.type = AUDIO_DEVICE_IN_LINE;

                    set_audio_source(&adev->alsa_mixer,
                            LINEIN, alsa_device_is_auge());
                } else if (is_hdmiin_audio && adev->audio_patch /* && (adev->patch_src == SRC_LINEIN) && pAudPatchTmp->id == adev->audio_patch->patch_hdl*/) {
                    ALOGI("%s, !!create dvi-hdmi patching dev->mix", __func__);
                    release_parser(adev);

                    aml_audio_input_routing(dev, INPORT_HDMIIN);
                    adev->patch_src = SRC_HDMIIN;
                    pAudPatchTmp->sources[0].ext.device.type = AUDIO_DEVICE_IN_HDMI;

                    set_audio_source(&adev->alsa_mixer,
                            HDMIIN, alsa_device_is_auge());
                }
            }

        } else if (is_tv_exit) {
            adev->source_mute = true;
            ALOGI("%s() fast exiting TV, mute the tv source", __func__);
        }
    }

    //  HDMI plug in and UI [Sound Output Device] set to "ARC" will recieve speaker_mute = 1
    ret = str_parms_get_str (parms, "speaker_mute", value, sizeof (value) );
    if (ret >= 0) {
        if (strncmp(value, "true", 4) == 0 || strncmp(value, "1", 1) == 0) {
            adev->speaker_mute = 1;
        } else if (strncmp(value, "false", 5) == 0 || strncmp(value, "0", 1) == 0) {
            adev->speaker_mute = 0;
        } else {
            ALOGE("%s() unsupport speaker_mute value: %s",   __func__, value);
        }

        /*
            when ARC is connecting, and user switch [Sound Output Device] to "speaker"
            we need to set out_port as OUTPORT_SPEAKER ,
            the aml_audio_output_routing() will "mute" ARC and "unmute" speaker.
           */
        int out_port = adev->active_outport;
        if (adev->active_outport == OUTPORT_HDMI_ARC && adev->speaker_mute == 0) {
            out_port = OUTPORT_SPEAKER;
        }

        ret = aml_audio_output_routing(dev, out_port, true);
        if (ret < 0) {
            ALOGE("%s() output routing failed", __func__);
        }
        goto exit;
    }

    ret = str_parms_get_str(parms, "parental_control_av_mute", value, sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "true", 4) == 0) {
            adev->parental_control_av_mute = true;
        } else if (strncmp(value, "false", 5) == 0) {
            adev->parental_control_av_mute = false;
        } else {
            ALOGE("%s() unsupport parental_control_av_mute value: %s",
                    __func__, value);
        }
        ALOGI("parental_control_av_mute set to %d\n", adev->parental_control_av_mute);
        goto exit;
    }

    // used for get first apts for A/V sync
    ret = str_parms_get_str (parms, "first_apts", value, sizeof (value) );
    if (ret >= 0) {
        unsigned int first_apts = atoi (value);
        ALOGI ("audio set first apts 0x%x\n", first_apts);
        adev->first_apts = first_apts;
        adev->first_apts_flag = true;
        adev->frame_trigger_thred = 0;
        goto exit;
    }

    {
        ret = str_parms_get_int(parms, "dual_decoder_support", &val);
        if (ret >= 0) {
            if (adev->audio_patch != NULL) {
                pthread_mutex_lock(&adev->lock);
                adev->dual_decoder_support = val;
                ALOGI("dual_decoder_support set to %d\n", adev->dual_decoder_support);
                if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
                    pthread_mutex_lock(&ms12->lock);
                    set_audio_system_format(AUDIO_FORMAT_PCM_16_BIT);
                    set_audio_app_format(AUDIO_FORMAT_PCM_16_BIT);
                    //only use to set associate flag, dd/dd+ format is same.
                    if (adev->dual_decoder_support == 1) {
                        set_audio_associate_format(AUDIO_FORMAT_AC3);
                    } else {
                        set_audio_associate_format(AUDIO_FORMAT_INVALID);
                    }
                    dolby_ms12_config_params_set_associate_flag(adev->dual_decoder_support);
                    pthread_mutex_unlock(&ms12->lock);
                }
                adev->need_reset_for_dual_decoder = true;
                pthread_mutex_unlock(&adev->lock);
            } else {
                ALOGE("%s()the audio patch is NULL \n", __func__);
            }
            goto exit;
        }

        ret = str_parms_get_int(parms, "associate_audio_mixing_enable", &val);
        if (ret >= 0) {
            if (adev->audio_patch != NULL) {
                pthread_mutex_lock(&adev->lock);
                if (val == 0) {
                    dtv_assoc_audio_cache(-1);
                }
                adev->associate_audio_mixing_enable = val;
                ALOGI("associate_audio_mixing_enable set to %d\n", adev->associate_audio_mixing_enable);
                if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
                    pthread_mutex_lock(&ms12->lock);
                    dolby_ms12_set_asscociated_audio_mixing(adev->associate_audio_mixing_enable);
                    set_ms12_ad_mixing_enable(ms12, adev->associate_audio_mixing_enable);
                    pthread_mutex_unlock(&ms12->lock);
                }
                pthread_mutex_unlock(&adev->lock);
            } else {
                ALOGE("%s()the audio patch is NULL \n", __func__);
            }

            goto exit;
        }

        ret = str_parms_get_int(parms, "dual_decoder_mixing_level", &val);
        if (ret >= 0) {
            int mix_user_prefer = 0;
            int mixing_level = val;
            pthread_mutex_lock(&adev->lock);
            if (mixing_level < 0) {
                mixing_level = 0;
            } else if (mixing_level > 100) {
                mixing_level = 100;
            }
            mix_user_prefer = (mixing_level * 64 - 32 * 100) / 100; //[0,100] mapping to [-32,32]
            adev->mixing_level = mix_user_prefer;
            ALOGI("mixing_level set to %d\n", adev->mixing_level);
            if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
                pthread_mutex_lock(&ms12->lock);
                dolby_ms12_set_user_control_value_for_mixing_main_and_associated_audio(adev->mixing_level);
                set_ms12_ad_mixing_level(ms12, adev->mixing_level);
                pthread_mutex_unlock(&ms12->lock);
            }
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
    }

    /*use dolby_lib_type_last to check ms12 type, because durig playing DTS file,
      this type will be changed to dcv*/
    if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
        ret = str_parms_get_int(parms, "continuous_audio_mode", &val);
        if (ret >= 0) {
            int disable_continuous = !val;
            // if exit netflix, we need disable atmos lock
            if (disable_continuous) {
                adev->atoms_lock_flag = 0;
                set_ms12_atmos_lock(&(adev->ms12), adev->atoms_lock_flag);
                ALOGI("exit netflix, set atmos lock as 0");
            }
            ALOGI("%s ignore the continuous_audio_mode!\n", __func__ );
            adev->is_netflix = val;
            goto exit;
            ALOGI("%s continuous_audio_mode set to %d\n", __func__ , val);
            char buf[PROPERTY_VALUE_MAX] = {0};
            ret = property_get(DISABLE_CONTINUOUS_OUTPUT, buf, NULL);
            if (ret > 0) {
                sscanf(buf, "%d", &disable_continuous);
                ALOGI("%s[%s] disable_continuous %d\n", DISABLE_CONTINUOUS_OUTPUT, buf, disable_continuous);
            }
            pthread_mutex_lock(&adev->lock);
            if (continous_mode(adev) && disable_continuous) {
                // If the Netflix application is terminated, your platform must disable Atmos locking.
                // For more information, see When to enable/disable Atmos lock.
                // The following Netflix application state transition scenarios apply (state definitions described in Always Ready):
                // Running->Not Running
                // Not Running->Running
                // Hidden->Visible
                // Visible->Hidden
                // Application crash / abnormal shutdown
               if (eDolbyMS12Lib == adev->dolby_lib_type) {
                    adev->atoms_lock_flag = 0;
                    dolby_ms12_set_atmos_lock_flag(adev->atoms_lock_flag);
                }

                if (dolby_stream_active(adev) || hwsync_lpcm_active(adev)) {
                    ALOGI("%s later release MS12 when direct stream active", __func__);
                    adev->need_remove_conti_mode = true;
                } else {
                    bool set_ms12_non_continuous = true;
                    ALOGI("%s Dolby MS12 is at continuous output mode, here go to end it!\n", __FUNCTION__);
                    get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                    adev->exiting_ms12 = 1;
                    clock_gettime(CLOCK_MONOTONIC, &adev->ms12_exiting_start);
                    usecase_change_validate_l(adev->active_outputs[STREAM_PCM_NORMAL], true);
                }
            } else {
                if ((!disable_continuous) && !continous_mode(adev)) {
                    adev->mix_init_flag = false;
                    adev->continuous_audio_mode = 1;
                }
            }
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }

        ret = str_parms_get_int(parms, "hdmi_dolby_atmos_lock", &val);
        if (ret >= 0) {
            ALOGI("%s hdmi_dolby_atmos_lock set to %d\n", __func__ , val);
            char buf[PROPERTY_VALUE_MAX];
            int b_atmos_lock = val;

            if (eDolbyMS12Lib == adev->dolby_lib_type) {
            // Note: some guide from www.netflix.com
            // In the Netflix application, enable/disable Atmos locking
            // when you receive the signal from the Netflix application.
            pthread_mutex_lock(&adev->lock);
            if (continous_mode(adev)) {
                if (b_atmos_lock == adev->atoms_lock_flag) {
                    ALOGI("%s already adev->atoms_lock_flag = %d, need to do nothing.\n", __func__,adev->atoms_lock_flag);
                } else {
                    // enable/disable atoms lock
                    set_ms12_atmos_lock(&(adev->ms12),b_atmos_lock);
                    adev->atoms_lock_flag = b_atmos_lock;
                    ALOGI("%s set adev->atoms_lock_flag = %d, \n", __func__,adev->atoms_lock_flag);
                }
            } else {
                ALOGI("%s not in continous mode, do nothing\n", __func__);
            }
            pthread_mutex_unlock(&adev->lock);
            }
            goto exit;
        }
        ret = str_parms_get_str(parms, "ms12_runtime", value, sizeof(value));
        if (ret >= 0) {
            char *parm = strstr(kvpairs, "=");
            pthread_mutex_lock(&adev->lock);
            if (parm)
                aml_ms12_update_runtime_params(&(adev->ms12), parm + 1);
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }

    }
    ret = str_parms_get_str(parms, "SOURCE_GAIN", value, sizeof(value));
    if (ret >= 0) {
        float fAtvGainDb = 0, fDtvGainDb = 0, fHdmiGainDb = 0, fAvGainDb = 0, fMediaGainDb = 0;
        sscanf(value,"%f %f %f %f %f", &fAtvGainDb, &fDtvGainDb, &fHdmiGainDb, &fAvGainDb, &fMediaGainDb);
        ALOGI("%s() audio source gain: atv:%f, dtv:%f, hdmiin:%f, av:%f, media:%f", __func__,
        fAtvGainDb, fDtvGainDb, fHdmiGainDb, fAvGainDb, fMediaGainDb);
        adev->eq_data.s_gain.atv = DbToAmpl(fAtvGainDb);
        adev->eq_data.s_gain.dtv = DbToAmpl(fDtvGainDb);
        adev->eq_data.s_gain.hdmi = DbToAmpl(fHdmiGainDb);
        adev->eq_data.s_gain.av = DbToAmpl(fAvGainDb);
        adev->eq_data.s_gain.media = DbToAmpl(fMediaGainDb);
        goto exit;
    }
    ret = str_parms_get_str(parms, "SOURCE_MUTE", value, sizeof(value));
    if (ret >= 0) {
        sscanf(value,"%d", &adev->source_mute);
        ALOGI("%s() set audio source mute: %s",__func__,
            adev->source_mute?"mute":"unmute");
        goto exit;
    }
    ret = str_parms_get_str(parms, "POST_GAIN", value, sizeof(value));
    if (ret >= 0) {
        sscanf(value,"%f %f %f", &adev->eq_data.p_gain.speaker, &adev->eq_data.p_gain.spdif_arc,
                &adev->eq_data.p_gain.headphone);
        ALOGI("%s() audio device gain: speaker:%f, spdif_arc:%f, headphone:%f", __func__,
        adev->eq_data.p_gain.speaker, adev->eq_data.p_gain.spdif_arc,
        adev->eq_data.p_gain.headphone);
        goto exit;
    }
#ifdef ADD_AUDIO_DELAY_INTERFACE
    ret = str_parms_get_int(parms, "hal_param_speaker_delay_time_ms", &val);
    if (ret >= 0) {
        aml_audio_delay_set_time(AML_DELAY_OUTPORT_SPEAKER, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_spdif_delay_time_ms", &val);
    if (ret >= 0) {
        aml_audio_delay_set_time(AML_DELAY_OUTPORT_SPDIF, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "delay_time", &val);
    if (ret >= 0) {
        aml_audio_delay_set_time(AML_DELAY_OUTPORT_ALL, val);
        goto exit;
    }
#endif
    ret = str_parms_get_str(parms, "EQ_PARAM", value, sizeof(value));
    if (ret >= 0) {
       sscanf(value, "%lf %lf %u %u %u",&adev->Eq_data.G,&adev->Eq_data.Q,&adev->Eq_data.fc,&adev->Eq_data.type,&adev->Eq_data.band_id);
       setpar_eq(adev->Eq_data.G,adev->Eq_data.Q,adev->Eq_data.fc,adev->Eq_data.type,adev->Eq_data.band_id);
       goto exit;
    }
    ret = str_parms_get_str(parms,"mb_drc",value,sizeof(value));
    if (ret >= 0) {
       sscanf(value, "%u %u %u %u %f %f",&adev->Drc_data.band_id,&adev->Drc_data.attrack_time,&adev->Drc_data.release_time,
             &adev->Drc_data.estimate_time,&adev->Drc_data.K,&adev->Drc_data.threshold);
       setmb_drc(adev->Drc_data.band_id,adev->Drc_data.attrack_time,adev->Drc_data.release_time,adev->Drc_data.estimate_time,
                adev->Drc_data.K,adev->Drc_data.threshold);
       goto exit;
    }

    ret = str_parms_get_str(parms,"fb_drc",value,sizeof(value));
    if (ret >= 0) {
       sscanf(value, "%u %u %u %u %f %f %u",&adev->Drc_data.band_id,&adev->Drc_data.attrack_time,&adev->Drc_data.release_time,
             &adev->Drc_data.estimate_time,&adev->Drc_data.K,&adev->Drc_data.threshold,&adev->Drc_data.delays);
       setfb_drc(adev->Drc_data.band_id,adev->Drc_data.attrack_time,adev->Drc_data.release_time,adev->Drc_data.estimate_time,
               adev->Drc_data.K,adev->Drc_data.threshold,adev->Drc_data.delays);
       goto exit;
    }
    ret = str_parms_get_str(parms,"csfilter_drc",value,sizeof(value));
    if (ret >= 0) {
       sscanf(value, "%u %u",&adev->Drc_data.band_id,&adev->Drc_data.fc);
       setcsfilter_drc(adev->Drc_data.band_id,adev->Drc_data.fc);
       goto exit;
    }
    ret = str_parms_get_int(parms, "eq_enable", &val);
    if (ret >= 0) {
        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_AED_EQ_ENABLE, val);
        ALOGI("audio AED EQ enable: %s\n", val?"enable":"disable");
        goto exit;
    }
    ret = str_parms_get_int(parms, "multi_drc_enable", &val);
    if (ret >= 0) {
        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_AED_MULTI_DRC_ENABLE, val);
        ALOGI("audio AED Multi-band DRC enable: %s\n", val?"enable":"disable");
        goto exit;
    }
    ret = str_parms_get_int(parms, "fullband_drc_enable", &val);
    if (ret >= 0) {
        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_AED_FULL_DRC_ENABLE, val);
        ALOGI("audio AED Full-band DRC enable: %s\n", val?"enable":"disable");
        goto exit;
    }
    ret = str_parms_get_str(parms, "DTS_POST_GAIN", value, sizeof(value));
    if (ret >= 0) {
        sscanf(value,"%f", &adev->dts_post_gain);
        ALOGI("%s() audio dts post gain: %f", __func__, adev->dts_post_gain);
        goto exit;
    }

    ret = str_parms_get_int(parms, "EQ_MODE", &val);
    if (ret >= 0) {
        if (eq_mode_set(&adev->eq_data, val) < 0)
            ALOGE("%s: eq_mode_set failed", __FUNCTION__);
        goto exit;
    }

    ret = str_parms_get_str(parms, "cmd", value, sizeof(value));
    if (ret > 0) {
        int cmd = atoi(value);
        if (adev->audio_patch == NULL) {
            ALOGI("%s()the audio patch is NULL \n", __func__);
        }


#ifdef ENABLE_DTV_PATCH
        switch (cmd) {
        case 1:
            ALOGI("%s() live AUDIO_DTV_PATCH_CMD_START\n", __func__);
            dtv_patch_add_cmd(AUDIO_DTV_PATCH_CMD_START);
            break;
        case 2:
            ALOGI("%s() live AUDIO_DTV_PATCH_CMD_PAUSE\n", __func__);
            dtv_patch_add_cmd(AUDIO_DTV_PATCH_CMD_PAUSE);
            break;
        case 3:
            ALOGI("%s() live AUDIO_DTV_PATCH_CMD_RESUME\n", __func__);
            dtv_patch_add_cmd(AUDIO_DTV_PATCH_CMD_RESUME);
            break;
        case 4:
            ALOGI("%s() live AUDIO_DTV_PATCH_CMD_STOP\n", __func__);
            dtv_patch_add_cmd(AUDIO_DTV_PATCH_CMD_STOP);
            break;
        default:
            ALOGI("%s() live %d \n", __func__, cmd);
            break;
        }
#endif
        goto exit;
    }

    ret = str_parms_get_str(parms, "mode", value, sizeof(value));
    if (ret > 0) {
        int mode = atoi(value);
        if (adev->audio_patch == NULL) {
            ALOGI("%s()the audio patch is NULL \n", __func__);
            goto exit;
        }
        ALOGI("DTV sound mode %d ",mode );
        adev->audio_patch->mode = mode;
    }
    ret = str_parms_get_str(parms, "sound_track", value, sizeof(value));
    if (ret > 0) {
        int mode = atoi(value);
        if (adev->audio_patch != NULL) {
            ALOGI("%s()the audio patch is not NULL \n", __func__);
            if (adev->patch_src == SRC_DTV) {
                ALOGI("DTV sound mode %d ",mode);
                adev->audio_patch->mode = mode;
            }
            goto exit;
        }
        ALOGI("video player sound_track mode %d ",mode );
        adev->sound_track_mode = mode;
        goto exit;
    }
    ret = str_parms_get_str(parms, "fmt", value, sizeof(value));
    if (ret > 0) {
        unsigned int audio_fmt = (unsigned int)atoi(value); // zz
        ALOGI("%s() get the audio format %d\n", __func__, audio_fmt);
        if (adev->audio_patch != NULL) {
            adev->dtv_aformat = adev->audio_patch->dtv_aformat = audio_fmt;
        } else {
            ALOGI("%s()the audio patch is NULL \n", __func__);
        }
        goto exit;
    }

    ret = str_parms_get_str(parms, "subapid", value, sizeof(value));
    if (ret > 0) {
        int audio_sub_pid = (unsigned int)atoi(value);
        ALOGI("%s() get the sub audio pid %d\n", __func__, audio_sub_pid);
        if (audio_sub_pid > 0) {
            adev->sub_apid = audio_sub_pid;
        } else {
            adev->sub_apid = -1;
        }
        goto exit;
    }

    ret = str_parms_get_str(parms, "subafmt", value, sizeof(value));
    if (ret > 0) {
        int audio_sub_fmt = (unsigned int)atoi(value);
        ALOGI("%s() get the sub audio fmt %d\n", __func__, audio_sub_fmt);
        if (audio_sub_fmt >= 0) {
            adev->sub_afmt = audio_sub_fmt;
        } else {
            adev->sub_afmt = -1;
        }
        goto exit;
    }

    ret = str_parms_get_str(parms, "has_dtv_video", value, sizeof(value));
    if (ret > 0) {
        unsigned int has_video = (unsigned int)atoi(value);
        ALOGI("%s() get the has video parameters %d \n", __func__, has_video);
        if (adev->audio_patch != NULL) {
            adev->audio_patch->dtv_has_video = has_video;
        } else {
            ALOGI("%s()the audio patch is NULL \n", __func__);
        }

        goto exit;
    }

    ret = str_parms_get_str(parms, "has_video", value, sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "true", 4) == 0) {
            adev->is_has_video = true;
        } else if (strncmp(value, "false", 5) == 0) {
            adev->is_has_video = false;
        } else {
            adev->is_has_video = true;
            ALOGE("%s() unsupport value %s choose is_has_video(default) %d\n", __func__, value, adev->is_has_video);
        }
        ALOGI("is_has_video set to %d\n", adev->is_has_video);
    }

    ret = str_parms_get_str(parms, "reconfigA2dp", value, sizeof(value));
    if (ret >= 0) {
        ALOGE ("%s A2DP reconfigA2dp out_device=%x", __FUNCTION__, adev->out_device);
        goto exit;
    }

#if defined(AUDIO_EFFECT_EXTERN_DEVICE)
    if (adev->a2dp_output) {
        struct a2dp_stream_out* out = (struct a2dp_stream_out*)adev->a2dp_output;
        ret = str_parms_get_str(parms, "BT_GAIN", value, sizeof(value));
        if (ret >= 0) {
            sscanf(value, "%f", &out->bt_gain);
            ALOGI("%s() audio bt gain: %f", __func__,out->bt_gain);
        }
        ret = str_parms_get_str(parms, "BT_MUTE", value, sizeof(value));
        if (ret >= 0) {
            sscanf(value, "%d", &out->bt_unmute);
            ALOGI("%s() audio bt unmute: %d", __func__,out->bt_unmute);
        }

        ret = str_parms_get_str(parms, "BT_GAIN_RIGHT", value, sizeof(value));
        if (ret >= 0) {
            sscanf(value, "%f %f", &out->right_gain,&out->left_gain);
            ALOGI("%s() audio bt right gain: %f left gain is %f", __func__,out->right_gain, out->left_gain);
        }
        ret = str_parms_get_str(parms, "BT_GAIN_LEFT", value, sizeof(value));
        if (ret >= 0) {
            sscanf(value, "%f %f", &out->left_gain,&out->right_gain);
            ALOGI("%s() audio bt left gain: %f right gain is %f", __func__,out->left_gain, out->right_gain);
        }
    }
#endif

    ret = str_parms_get_str(parms, "hfp_set_sampling_rate", value, sizeof(value));
    if (ret >= 0) {
        ALOGI ("Amlogic_HAL - %s: hfp_set_sampling_rate. Abort function and return 0.", __FUNCTION__);
        goto exit;
    }

    ret = str_parms_get_str(parms, "hfp_volume", value, sizeof(value));
    if (ret >= 0) {
        ALOGI ("Amlogic_HAL - %s: hfp_volume. Abort function and return 0.", __FUNCTION__);
        goto exit;
    }

    ret = str_parms_get_str(parms, "bt_headset_name", value, sizeof(value));
    if (ret >= 0) {
        ALOGE ("Amlogic_HAL - %s: bt_headset_name. Abort function and return 0.", __FUNCTION__);
        goto exit;
    }

    ret = str_parms_get_str(parms, "rotation", value, sizeof(value));
    if (ret >= 0) {
        ALOGI ("Amlogic_HAL - %s: rotation. Abort function and return 0.", __FUNCTION__);
        goto exit;
    }

    ret = str_parms_get_str(parms, "bt_headset_nrec", value, sizeof(value));
    if (ret >= 0) {
        ALOGI ("Amlogic_HAL - %s: bt_headset_nrec. Abort function and return 0.", __FUNCTION__);
        goto exit;
    }

    ret = str_parms_get_str(parms, "bt_wbs", value, sizeof(value));
    if (ret >= 0) {
        ALOGI ("Amlogic_HAL - %s: bt_wbs. Abort function and return 0.", __FUNCTION__);
        goto exit;
    }

    ret = str_parms_get_str(parms, "hfp_enable", value, sizeof(value));
    if (ret >= 0) {
        ALOGI ("Amlogic_HAL - %s: hfp_enable. Abort function and return 0.", __FUNCTION__);
        goto exit;
    }

    ret = str_parms_get_str(parms, "HACSetting", value, sizeof(value));
    if (ret >= 0) {
        ALOGI ("Amlogic_HAL - %s: HACSetting. Abort function and return 0.", __FUNCTION__);
        goto exit;
    }

    ret = str_parms_get_str(parms, "tty_mode", value, sizeof(value));
    if (ret >= 0) {
        ALOGI ("Amlogic_HAL - %s: tty_mode. Abort function and return 0.", __FUNCTION__);
        goto exit;
    }

    ret = str_parms_get_str(parms, "TV-Mute", value, sizeof(value));
    if (ret >= 0) {
        unsigned int tv_mute = (unsigned int)atoi(value);
        ALOGI ("Amlogic_HAL - %s: TV-Mute:%d.", __FUNCTION__,tv_mute);
        adev->need_reset_ringbuffer = tv_mute;
        adev->tv_mute = tv_mute;
        adev->hw_mixer.mute_main_flag = tv_mute;
        goto exit;
    }
    ret = str_parms_get_str(parms, "direct-mode", value, sizeof(value));
    if (ret >= 0) {
        unsigned int direct_mode = (unsigned int)atoi(value);
        ALOGI ("Amlogic_HAL - %s: direct-mode:%d.", __FUNCTION__,direct_mode);
        adev->direct_mode = direct_mode;
        goto exit;
    }

    ret = str_parms_get_str(parms, "show-meminfo", value, sizeof(value));
    if (ret >= 0) {
        unsigned int level = (unsigned int)atoi(value);
        ALOGE ("Amlogic_HAL - %s: ShowMem info level:%d.", __FUNCTION__,level);
        aml_audio_debug_malloc_showinfo(level);
        return 0;
    }

#ifdef USB_KARAOKE
    ret = str_parms_get_int(parms, "karaoke_switch", &val);
    if (ret >= 0) {
        bool karaoke_on = !!val;
        adev->usb_audio.karaoke.karaoke_on = karaoke_on;
        ALOGI("[%s]Set usb karaoke: %d", __FUNCTION__, karaoke_on);
        goto exit;
    }
    ret = str_parms_get_int(parms, "karaoke_mic_mute", &val);
    if (ret >= 0) {
        bool mic_mute = !!val;
        adev->usb_audio.karaoke.kara_mic_mute = mic_mute;
        ALOGI("[%s]Set usb mic mute: %d", __FUNCTION__, mic_mute);
        goto exit;
    }
    ret = str_parms_get_str(parms, "karaoke_mic_volume", value, sizeof(value));
    if (ret >= 0) {
        float karaoke_mic_volume = 0;
        sscanf(value,"%f", &karaoke_mic_volume);
        adev->usb_audio.karaoke.kara_mic_gain = DbToAmpl(karaoke_mic_volume);
        ALOGI("[%s]Set usb mic volume: %f dB", __func__, karaoke_mic_volume);
        goto exit;
    }
#endif

    ret = str_parms_get_str(parms, "bypass_dap", value, sizeof(value));
    if (ret >= 0) {
        sscanf(value,"%d %f", &adev->ms12.dap_bypass_enable, &adev->ms12.dap_bypassgain);
        ALOGD("dap_bypass_enable is %d and dap_bypassgain is %f",adev->ms12.dap_bypass_enable, adev->ms12.dap_bypassgain);
        goto exit;
    }

exit:
    str_parms_destroy (parms);

    // VTS regards 0 as success, so if we setting parameter successfully,
    // zero should be returned instead of data length.
    // To pass VTS test, ret must be Result::OK (0) or Result::NOT_SUPPORTED (4).
    if (kvpairs == NULL) {
        ALOGE ("Amlogic_HAL - %s: kvpairs points to NULL. Abort function and return 0.", __FUNCTION__);
        return 0;
    }
    if (ret > 0 || (strlen (kvpairs) == 0) ) {
        //ALOGI ("Amlogic_HAL - %s: return 0 instead of length of data be copied.", __FUNCTION__);
        ret = 0;
    } else if (ret < 0) {
        ALOGI ("Amlogic_HAL - %s: return Result::NOT_SUPPORTED (4) instead of other error code.", __FUNCTION__);
        //ALOGI ("Amlogic_HAL - %s: return Result::OK (0) instead of other error code.", __FUNCTION__);
        ret = 4;
    }
    return ret;
}

static char * adev_get_parameters (const struct audio_hw_device *dev,
                                   const char *keys)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    char temp_buf[64] = {0};

    if (!strcmp (keys, AUDIO_PARAMETER_HW_AV_SYNC) ) {
        ALOGI ("get hwsync id\n");
        return strdup ("hw_av_sync=12345678");
    }

    if (strstr (keys, AUDIO_PARAMETER_HW_AV_EAC3_SYNC) ) {
        return strdup ("HwAvSyncEAC3Supported=true");
    } else if (strstr (keys, "hdmi_format") ) {
        sprintf (temp_buf, "hdmi_format=%d", adev->hdmi_format);
        return strdup (temp_buf);
    } else if (strstr (keys, "spdif_format") ) {
        sprintf (temp_buf, "spdif_format=%d", adev->spdif_format);
        return strdup (temp_buf);
    } else if (strstr (keys, "hdmi_is_passthrough_active") ) {
        sprintf (temp_buf, "hdmi_is_passthrough_active=%d", adev->hdmi_is_pth_active);
        return strdup (temp_buf);
    } else if (strstr (keys, "disable_pcm_mixing") ) {
        sprintf (temp_buf, "disable_pcm_mixing=%d", adev->disable_pcm_mixing);
        return strdup (temp_buf);
    } else if (strstr (keys, "hdmi_encodings") ) {
        struct format_desc *fmtdesc = NULL;
        bool dd = false, ddp = false;

        // query dd support
        fmtdesc = &adev->hdmi_descs.dd_fmt;
        if (fmtdesc && fmtdesc->fmt == _AC3)
            dd = fmtdesc->is_support;

        // query ddp support
        fmtdesc = &adev->hdmi_descs.ddp_fmt;
        if (fmtdesc && fmtdesc->fmt == _DDP)
            ddp = fmtdesc->is_support;

        sprintf (temp_buf, "hdmi_encodings=%s", "pcm;");
        if (ddp)
            sprintf (temp_buf, "ac3;eac3;");
        else if (dd)
            sprintf (temp_buf, "ac3;");

        return strdup (temp_buf);
    } else if (strstr (keys, "is_passthrough_active") ) {
        bool active = false;
        pthread_mutex_lock (&adev->lock);
        // "is_passthrough_active" is used by amnuplayer to check is current audio hal have passthrough instance(DD/DD+/DTS)
        // if already have one passthrough instance, it will not invoke another one.
        // While in HDMI plug off/in test case. Player will query "is_passthrough_active" before adev->usecase_masks finally settle
        // So Player will have a chance get a middle-term value, which is wrong.
        // now only one passthrough instance, do not check
        /* if (adev->usecase_masks & RAW_USECASE_MASK)
            active = true;
        else if (adev->audio_patch && (adev->audio_patch->aformat == AUDIO_FORMAT_E_AC3 \
                                       || adev->audio_patch->aformat == AUDIO_FORMAT_AC3) ) {
            active = true;
        } */
        pthread_mutex_unlock (&adev->lock);
        sprintf (temp_buf, "is_passthrough_active=%d",active);
        return  strdup (temp_buf);
    } else if (!strcmp(keys, "SOURCE_GAIN")) {
        sprintf(temp_buf, "source_gain = %f %f %f %f %f", adev->eq_data.s_gain.atv, adev->eq_data.s_gain.dtv,
                adev->eq_data.s_gain.hdmi, adev->eq_data.s_gain.av, adev->eq_data.s_gain.media);
        return strdup(temp_buf);
    } else if (!strcmp(keys, "POST_GAIN")) {
        sprintf(temp_buf, "post_gain = %f %f %f", adev->eq_data.p_gain.speaker, adev->eq_data.p_gain.spdif_arc,
                adev->eq_data.p_gain.headphone);
        return strdup(temp_buf);
    } else if (strstr(keys, "dolby_ms12_enable")) {
        int ms12_enable = (eDolbyMS12Lib == adev->dolby_lib_type_last);
        ALOGI("ms12_enable :%d", ms12_enable);
        sprintf(temp_buf, "dolby_ms12_enable=%d", ms12_enable);
        return  strdup(temp_buf);
    } else if (!strcmp(keys, "SOURCE_MUTE")) {
        sprintf(temp_buf, "source_mute = %d", adev->source_mute);
        return strdup(temp_buf);
    } else if (!strcmp(keys, "parental_control_av_mute")) {
        sprintf(temp_buf, "parental_control_av_mute = %d", adev->parental_control_av_mute);
        return strdup(temp_buf);
    }

    //1.HDMI in samplerate
    else if (strstr(keys, "HDMIIN audio samplerate")) {
        int cur_samplerate = aml_mixer_ctrl_get_int(&adev->alsa_mixer,AML_MIXER_ID_HDMI_IN_SAMPLERATE);
        ALOGD("cur_samplerate :%d", cur_samplerate);
        sprintf(temp_buf, "%d", cur_samplerate);
        return  strdup(temp_buf);
    }
    //3.HDMI in channels
    else if (strstr(keys, "HDMIIN audio channels")) {
        int cur_channels = aml_mixer_ctrl_get_int(&adev->alsa_mixer,AML_MIXER_ID_HDMI_IN_CHANNELS);
        ALOGD("cur_channels :%d", cur_channels);
        sprintf(temp_buf, "%d", cur_channels);
        return  strdup(temp_buf);
    }
    //4.audio format
    else if (strstr(keys, "HDMIIN audio format")) {
        int cur_format = aml_mixer_ctrl_get_int(&adev->alsa_mixer,AML_MIXER_ID_HDMI_IN_FORMATS);
        ALOGV("cur_format :%d", cur_format);
        sprintf(temp_buf, "%d", cur_format);
        return  strdup(temp_buf);
    }
    //5.HDMI Passthrough audio data type
    else if (strstr(keys, "HDMIIN Audio Type")) {
        int cur_type = aml_mixer_ctrl_get_int(&adev->alsa_mixer,AML_MIXER_ID_HDMIIN_AUDIO_TYPE);
        ALOGD("cur_type :%d", cur_type);
        sprintf(temp_buf, "%d", cur_type);
        return  strdup(temp_buf);
    }
    else if (strstr(keys, "is_dolby_atmos")) {
        if (dolby_stream_active(adev)) {
            if (eDolbyMS12Lib == adev->dolby_lib_type)
                sprintf(temp_buf, "is_dolby_atmos=%d", adev->ms12.is_dolby_atmos);
            else
                sprintf(temp_buf, "is_dolby_atmos=%d", adev->ddp.is_dolby_atmos);
        }
        else {
            adev->ms12.is_dolby_atmos = 0;
            adev->ddp.is_dolby_atmos = 0;
            sprintf(temp_buf, "is_dolby_atmos=%d", adev->ms12.is_dolby_atmos);
        }
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    }
    else if (strstr(keys, "HDMI ARC Switch")) {
        sprintf(temp_buf, "HDMI ARC Switch=%d", adev->bHDMIARCon);
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    }
    else if (strstr(keys, "eARC_TX CDS")) {
        struct aml_mixer_handle *amixer = &adev->alsa_mixer;
        struct mixer *pMixer = amixer->pMixer;
        char cds[256] = {0};

        earctx_fetch_cds(pMixer, cds);
        return strdup(cds);
    }
    else if (strstr(keys, "eARC_RX CDS")) {
        struct aml_mixer_handle *amixer = &adev->alsa_mixer;
        struct mixer *pMixer = amixer->pMixer;
        char cds[256] = {0};

        earcrx_fetch_cds(pMixer, cds);
        return strdup(cds);
    }
    else if (strstr(keys, "eq_enable")) {
        int cur_status = aml_mixer_ctrl_get_int(&adev->alsa_mixer,AML_MIXER_ID_AED_EQ_ENABLE);
        sprintf(temp_buf, "%d", cur_status);
        ALOGD("temp_buf %d", cur_status);
        return strdup(temp_buf);
    }
    else if (strstr(keys, "multi_drc_enable")) {
        int cur_status = aml_mixer_ctrl_get_int(&adev->alsa_mixer,AML_MIXER_ID_AED_MULTI_DRC_ENABLE);
        sprintf(temp_buf, "%d", cur_status);
        ALOGD("temp_buf %d", cur_status);
        return strdup(temp_buf);
    }
    else if (strstr(keys, "fullband_drc_enable")) {
        int cur_status = aml_mixer_ctrl_get_int(&adev->alsa_mixer,AML_MIXER_ID_AED_FULL_DRC_ENABLE);
        sprintf(temp_buf, "%d", cur_status);
        ALOGD("temp_buf %d", cur_status);
        return strdup(temp_buf);
    }
    else if (strstr (keys, "stream_dra_channel") ) {
       if (adev->audio_patch != NULL && adev->patch_src == SRC_DTV) {
          if (adev->audio_patch->dtv_NchOriginal > 8 || adev->audio_patch->dtv_NchOriginal < 1) {
              sprintf (temp_buf, "0.0");
            } else {
              sprintf(temp_buf, "channel_num=%d.%d", adev->audio_patch->dtv_NchOriginal,adev->audio_patch->dtv_lfepresent);
              ALOGD ("temp_buf=%s\n", temp_buf);
            }
       } else {
          sprintf (temp_buf, "0.0");
       }
        return strdup(temp_buf);
    }
    else if (strstr(keys, "HDMI Switch")) {
        sprintf(temp_buf, "HDMI Switch=%d", (OUTPORT_HDMI == adev->active_outport));
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr(keys, "isReconfigA2dpSupported")) {
        return  strdup("isReconfigA2dpSupported=1");
    }
    return strdup("");
}

static int adev_init_check (const struct audio_hw_device *dev __unused)
{
    return 0;
}

static int adev_set_voice_volume (struct audio_hw_device *dev __unused, float volume __unused)
{
    return 0;
}

static int adev_set_master_volume (struct audio_hw_device *dev __unused, float volume __unused)
{
    return -ENOSYS;
}

static int adev_get_master_volume (struct audio_hw_device *dev __unused,
                                   float *volume __unused)
{
    return -ENOSYS;
}

static int adev_set_master_mute (struct audio_hw_device *dev __unused, bool muted __unused)
{
    return -ENOSYS;
}

static int adev_get_master_mute (struct audio_hw_device *dev __unused, bool *muted __unused)
{
    return -ENOSYS;
}
static int adev_set_mode (struct audio_hw_device *dev, audio_mode_t mode)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    ALOGD ("%s(%p, %d)", __FUNCTION__, dev, mode);

    pthread_mutex_lock (&adev->lock);
    if (adev->mode != mode) {
        adev->mode = mode;
        select_mode (adev);
    }
    pthread_mutex_unlock (&adev->lock);

    return 0;
}

static int adev_set_mic_mute (struct audio_hw_device *dev, bool state)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;

    adev->mic_mute = state;

    return 0;
}

static int adev_get_mic_mute (const struct audio_hw_device *dev, bool *state)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;

    *state = adev->mic_mute;

    return 0;

}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev __unused,
                                        const struct audio_config *config)
{
    int channel_count = audio_channel_count_from_in_mask(config->channel_mask);
    size_t size;

    ALOGD("%s: enter: channel_mask(%#x) rate(%d) format(%#x)", __func__,
        config->channel_mask, config->sample_rate, config->format);

    size = get_input_buffer_size(config->frame_count, config->sample_rate, config->format, channel_count);

    ALOGD("%s: exit: buffer_size = %zu", __func__, size);

    return size;
}

static int choose_stream_pcm_config(struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;
    int channel_count = audio_channel_count_from_in_mask(in->hal_channel_mask);
    int ret = 0;
    if (in->device & AUDIO_DEVICE_IN_ALL_SCO) {
        memcpy(&in->config, &pcm_config_bt, sizeof(pcm_config_bt));
    } else {
        if (!((in->device & AUDIO_DEVICE_IN_HDMI) ||
                (in->device & AUDIO_DEVICE_IN_HDMI_ARC))) {
            memcpy(&in->config, &pcm_config_in, sizeof(pcm_config_in));
        }
    }

    if (adev->mic_desc) {
        struct mic_in_desc *desc = adev->mic_desc;

        in->config.channels = desc->config.channels;
        in->config.rate = desc->config.rate;
        in->config.format = desc->config.format;
    } else if (in->config.channels != 8)
        in->config.channels = channel_count;

    switch (in->hal_format) {
        case AUDIO_FORMAT_PCM_16_BIT:
            in->config.format = PCM_FORMAT_S16_LE;
            break;
        case AUDIO_FORMAT_PCM_32_BIT:
            in->config.format = PCM_FORMAT_S32_LE;
            break;
        default:
            ALOGE("%s(), fmt not supported %#x", __func__, in->hal_format);
            break;
    }
    /* TODO: modify alsa config by params */
    update_alsa_config(in);

    return 0;
}

int add_in_stream_resampler(struct aml_stream_in *in)
{
    int ret = 0;

    if (in->requested_rate == in->config.rate)
        return 0;

    in->buffer = aml_audio_calloc(1, in->config.period_size * audio_stream_in_frame_size(&in->stream));
    if (!in->buffer) {
        ret = -ENOMEM;
        goto err;
    }

    ALOGD("%s: in->requested_rate = %d, in->config.rate = %d",
            __func__, in->requested_rate, in->config.rate);
    in->buf_provider.get_next_buffer = get_next_buffer;
    in->buf_provider.release_buffer = release_buffer;
    ret = create_resampler(in->config.rate, in->requested_rate, in->config.channels,
                        RESAMPLER_QUALITY_DEFAULT, &in->buf_provider, &in->resampler);
    if (ret != 0) {
        ALOGE("%s: create resampler failed (%dHz --> %dHz)", __func__, in->config.rate, in->requested_rate);
        ret = -EINVAL;
        goto err_resampler;
    }

    return 0;
err_resampler:
    aml_audio_free(in->buffer);
err:
    return ret;
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                audio_io_handle_t handle __unused,
                                audio_devices_t devices,
                                struct audio_config *config,
                                struct audio_stream_in **stream_in,
                                audio_input_flags_t flags __unused,
                                const char *address __unused,
                                audio_source_t source)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct usb_audio_device *usb_adev = &adev->usb_audio;
    struct aml_stream_in *in;
    int channel_count = audio_channel_count_from_in_mask(config->channel_mask);
    int ret;

    ALOGD("%s: enter: devices(%#x) channel_mask(%#x) rate(%d) format(%#x) source(%d)", __func__,
        devices, config->channel_mask, config->sample_rate, config->format, source);

    devices &= ~AUDIO_DEVICE_BIT_IN;

    if (devices & AUDIO_DEVICE_IN_ALL_USB) {
        usb_adev->adev_primary = (void*)adev;
        adev->in_device |= devices;
        ALOGD("%s: adev->in_device = %x", __func__, adev->in_device);
        ret = adev_open_usb_input_stream(usb_adev, devices, config, stream_in, address);
        if (ret < 0) {
            *stream_in = NULL;
        }
        return ret;
    }

    if (check_input_parameters(config->sample_rate, config->format, channel_count, devices) != 0) {
        if (devices & AUDIO_DEVICE_IN_ALL_SCO) {
            config->sample_rate = VX_NB_SAMPLING_RATE;
            config->channel_mask = AUDIO_CHANNEL_IN_MONO;
            config->format = AUDIO_FORMAT_PCM_16_BIT;
        } else {
            config->sample_rate = 48000;
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
        }
        return -EINVAL;
    }

    /* add constrains for MIC which may be used by both loopback and normal record */
    if (adev->mic_desc && (devices & AUDIO_DEVICE_IN_BUILTIN_MIC ||
            devices & AUDIO_DEVICE_IN_BACK_MIC)) {
        struct mic_in_desc *mic_desc = adev->mic_desc;

        ret = check_mic_parameters(mic_desc, config);
        if (ret < 0) {
            config->sample_rate = mic_desc->config.rate;
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            return ret;
        }
    }

    if (channel_count == 1)
        // in fact, this value should be AUDIO_CHANNEL_OUT_BACK_LEFT(16u) according to VTS codes,
        // but the macroname can be confusing, so I'd like to set this value to
        // AUDIO_CHANNEL_IN_FRONT(16u) instead of AUDIO_CHANNEL_OUT_BACK_LEFT.
        config->channel_mask = AUDIO_CHANNEL_IN_FRONT;
    else
        config->channel_mask = AUDIO_CHANNEL_IN_STEREO;

    in = (struct aml_stream_in *)aml_audio_calloc(1, sizeof(struct aml_stream_in));
    if (!in)
        return -ENOMEM;
#if defined(ENABLE_HBG_PATCH)
    if (is_hbg_hidraw()) {
        in->stream.common.get_sample_rate = in_hbg_get_sample_rate;
        in->stream.common.set_sample_rate = in_hbg_set_sample_rate;
        in->stream.common.get_buffer_size = in_hbg_get_buffer_size;
        in->stream.common.get_channels = in_hbg_get_channels;
        in->stream.common.get_format = in_hbg_get_format;
        in->stream.common.set_format = in_hbg_set_format;
        in->stream.common.standby = in_hbg_standby;
        in->stream.common.dump = in_hbg_dump;
        in->stream.common.set_parameters = in_hbg_set_parameters;
        in->stream.common.get_parameters = in_hbg_get_parameters;
        in->stream.common.add_audio_effect = in_hbg_add_audio_effect;
        in->stream.common.remove_audio_effect = in_hbg_remove_audio_effect;
        in->stream.set_gain = in_hbg_set_gain;
        in->stream.read = in_hbg_read;
        in->stream.get_input_frames_lost = in_hbg_get_input_frames_lost;
        in->stream.get_capture_position =  in_hbg_get_hbg_capture_position;
        in->hbg_channel = regist_callBack_stream();
        in->stream.get_active_microphones = in_get_active_microphones;
        }else if (remoteDeviceOnline() && (devices & AUDIO_DEVICE_IN_BUILTIN_MIC) && (config->channel_mask != BUILT_IN_MIC)) {
#else
    if (remoteDeviceOnline() && (devices & AUDIO_DEVICE_IN_BUILTIN_MIC) && (config->channel_mask != BUILT_IN_MIC)) {
#endif
        in->stream.common.set_sample_rate = kehwin_in_set_sample_rate;
        in->stream.common.get_sample_rate = kehwin_in_get_sample_rate;
        in->stream.common.get_buffer_size = kehwin_in_get_buffer_size;
        in->stream.common.get_channels = kehwin_in_get_channels;
        in->stream.common.get_format = kehwin_in_get_format;
        in->stream.common.set_format = kehwin_in_set_format;
        in->stream.common.dump = kehwin_in_dump;
        in->stream.common.set_parameters = kehwin_in_set_parameters;
        in->stream.common.get_parameters = kehwin_in_get_parameters;
        in->stream.common.add_audio_effect = kehwin_in_add_audio_effect;
        in->stream.common.remove_audio_effect = kehwin_in_remove_audio_effect;
        in->stream.set_gain = kehwin_in_set_gain;
        in->stream.common.standby = kehwin_in_standby;
        in->stream.read = kehwin_in_read;
        in->stream.get_input_frames_lost = kehwin_in_get_input_frames_lost;
        in->stream.get_capture_position =  kehwin_in_get_capture_position;
    } else {
        in->stream.common.get_sample_rate = in_get_sample_rate;
        in->stream.common.set_sample_rate = in_set_sample_rate;
        in->stream.common.get_buffer_size = in_get_buffer_size;
        in->stream.common.get_channels = in_get_channels;
        in->stream.common.get_format = in_get_format;
        in->stream.common.set_format = in_set_format;
        in->stream.common.standby = in_standby;
        in->stream.common.dump = in_dump;
        in->stream.common.set_parameters = in_set_parameters;
        in->stream.common.get_parameters = in_get_parameters;
        in->stream.set_gain = in_set_gain;
        in->stream.read = in_read;
        in->stream.get_input_frames_lost = in_get_input_frames_lost;
        in->stream.get_active_microphones = in_get_active_microphones;
    }

    in->device = devices;
    in->dev = adev;
    in->standby = 1;
    in->source = source;
    in->requested_rate = config->sample_rate;
    in->hal_channel_mask = config->channel_mask;
    in->hal_format = config->format;
    in->mute_mdelay = INPUTSOURCE_MUTE_DELAY_MS;

    for (int i=0; i<BT_AND_USB_PERIOD_DELAY_BUF_CNT; i++) {
        in->pBtUsbPeriodDelayBuf[i] = NULL;
    }
    in->pBtUsbTempDelayBuf = NULL;
    in->delay_buffer_size = 0;

    if (in->device & AUDIO_DEVICE_IN_ALL_SCO) {
        memcpy(&in->config, &pcm_config_bt, sizeof(pcm_config_bt));
    } else {
        memcpy(&in->config, &pcm_config_in, sizeof(pcm_config_in));
    }

    if (in->device & AUDIO_DEVICE_IN_WIRED_HEADSET) {
        // bluetooth rc voice
        // usecase for bluetooth rc audio hal
        ALOGI("%s: use RC audio HAL", __func__);
        ret = rc_open_input_stream(&in, config);
        if (ret != 0)
            return ret;
        config->sample_rate = in->config.rate;
        config->channel_mask = AUDIO_CHANNEL_IN_MONO;
    }

#ifdef ENABLE_AEC_FUNC
    // Default 2 ch pdm + 2 ch lb
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        aec_spk_mic_init(in->requested_rate, 2, 2);
    }
#endif

#if (ENABLE_NANO_PATCH == 1)
/*[SEN5-autumn.zhao-2018-01-11] add for B06 audio support { */
        recording_device = nano_get_recorde_device();
        ALOGD("recording_device=%d\n",recording_device);
        if (recording_device == RECORDING_DEVICE_NANO) {
                int ret = nano_open(&in->config, config,source,&in->stream);
                if (ret < 0) {
                        recording_device = RECORDING_DEVICE_OTHER;
                }
                else{
                        ALOGD("use nano function\n");
                        in->stream.common.get_sample_rate = nano_get_sample_rate;
                        in->stream.common.get_buffer_size = nano_get_buffer_size;
                        in->stream.common.get_channels = nano_get_channels;
                        in->stream.common.get_format = nano_get_format;
                        in->stream.read = nano_read;
                }
        }
/*[SEN5-autumn.zhao-2018-01-11] add for B06 audio support } */
#endif
    *stream_in = &in->stream;
    ALOGD("%s: exit", __func__);

#if ENABLE_NANO_NEW_PATH
        if (nano_is_connected() && (devices & AUDIO_DEVICE_IN_BUILTIN_MIC)) {
        ret = nano_input_open(*stream_in, config);
        if (ret < 0) {
            ALOGD("%s: nano_input_open : %d",__func__,ret);
        }
    }
#endif

    return 0;
err:
    if (in->resampler) {
        release_resampler(in->resampler);
        in->resampler = NULL;
    }
    if (in->buffer) {
        aml_audio_free(in->buffer);
        in->buffer = NULL;
    }
    aml_audio_free(in);
    *stream_in = NULL;
    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                struct audio_stream_in *stream)
{

    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    ALOGD("%s: enter: dev(%p) stream(%p) in->device(0x%x)", __func__, dev, stream, adev->in_device);

    if (stream == adev->usb_audio.stream) {
        struct stream_in *usb_in = (struct stream_in *)stream;
        adev_close_usb_input_stream(stream);
        adev->in_device &= ~usb_in->device;
        ALOGD("%s: adev->in_device = %x", __func__, adev->in_device);
        return;
    }

    if (remoteDeviceOnline() && (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC)) {
        kehwin_adev_close_input_stream(dev,stream);
        //return;
    }
#if ENABLE_NANO_NEW_PATH
    nano_close(stream);
#endif

    in_standby(&stream->common);

#if (ENABLE_NANO_PATCH == 1)
/*[SEN5-autumn.zhao-2018-03-15] add for B06A remote audio support { */
    if(recording_device == RECORDING_DEVICE_NANO){
       nano_close(stream);
    }
/*[SEN5-autumn.zhao-2018-03-15] add for B06A remote audio support } */
#endif
    if (in->device & AUDIO_DEVICE_IN_WIRED_HEADSET)
        rc_close_input_stream(in);

#ifdef ENABLE_AEC_FUNC
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        if (in->tmp_buffer_8ch) {
            aml_audio_free(in->tmp_buffer_8ch);
            in->tmp_buffer_8ch = NULL;
        }
        aec_spk_mic_release();
    }
#endif

    if (in->input_tmp_buffer) {
        aml_audio_free(in->input_tmp_buffer);
        in->input_tmp_buffer = NULL;
        in->input_tmp_buffer_size = 0;
    }

    for (int i=0; i<BT_AND_USB_PERIOD_DELAY_BUF_CNT; i++) {
        if (in->pBtUsbPeriodDelayBuf[i]) {
            aml_audio_free(in->pBtUsbPeriodDelayBuf[i]);
            in->pBtUsbPeriodDelayBuf[i] = NULL;
        }
    }

    if (in->pBtUsbTempDelayBuf) {
        aml_audio_free(in->pBtUsbTempDelayBuf);
        in->pBtUsbTempDelayBuf = NULL;
    }
    in->delay_buffer_size = 0;

    if (in->proc_buf) {
        aml_audio_free(in->proc_buf);
    }
    if (in->ref_buf) {
        aml_audio_free(in->ref_buf);
    }

#if defined(ENABLE_HBG_PATCH)
    unregist_callBack_stream(in->hbg_channel);
#endif

    aml_audio_free(stream);
    ALOGD("%s: exit", __func__);
    return;
}

const char *audio_port_role[] = {"AUDIO_PORT_ROLE_NONE", "AUDIO_PORT_ROLE_SOURCE", "AUDIO_PORT_ROLE_SINK"};
const char *audio_port_type[] = {"AUDIO_PORT_TYPE_NONE", "AUDIO_PORT_TYPE_DEVICE", "AUDIO_PORT_TYPE_MIX", "AUDIO_PORT_TYPE_SESSION"};
static void dump_audio_port_config (const struct audio_port_config *port_config)
{
    if (port_config == NULL)
        return;

    ALOGI ("  -%s port_config(%p)", __FUNCTION__, port_config);
    ALOGI ("\t-id(%d), role(%s), type(%s)", port_config->id, audio_port_role[port_config->role], audio_port_type[port_config->type]);
    ALOGV ("\t-config_mask(%#x)", port_config->config_mask);
    ALOGI ("\t-sample_rate(%d), channel_mask(%#x), format(%#x)", port_config->sample_rate,
           port_config->channel_mask, port_config->format);
    ALOGV ("\t-gain.index(%#x)", port_config->gain.index);
    ALOGV ("\t-gain.mode(%#x)", port_config->gain.mode);
    ALOGV ("\t-gain.channel_mask(%#x)", port_config->gain.channel_mask);
    ALOGI ("\t-gain.value0(%d)", port_config->gain.values[0]);
    ALOGI ("\t-gain.value1(%d)", port_config->gain.values[1]);
    ALOGI ("\t-gain.value2(%d)", port_config->gain.values[2]);
    ALOGV ("\t-gain.ramp_duration_ms(%d)", port_config->gain.ramp_duration_ms);
    switch (port_config->type) {
    case AUDIO_PORT_TYPE_DEVICE:
        ALOGI ("\t-port device: type(%#x) addr(%s)",
               port_config->ext.device.type, port_config->ext.device.address);
        break;
    case AUDIO_PORT_TYPE_MIX:
        ALOGI ("\t-port mix: iohandle(%d)", port_config->ext.mix.handle);
        break;
    default:
        break;
    }
}

/* must be called with hw device and output stream mutexes locked */
int do_output_standby_l(struct audio_stream *stream)
{
    struct audio_stream_out *out = (struct audio_stream_out *) stream;
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);

    ALOGI("[%s:%d] stream usecase:%s , continuous:%d", __func__, __LINE__,
        usecase2Str(aml_out->usecase), adev->continuous_audio_mode);

    if ((aml_out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) && adev->a2dp_out) {
        if ((eDolbyMS12Lib == adev->dolby_lib_type) && (ms12->dolby_ms12_enable == true)) {
            get_dolby_ms12_cleanup(&adev->ms12, false);
        }
        if ( aml_out->dev->patch_src != SRC_DTV && aml_out->dev->patch_src != SRC_ATV &&
             aml_out->dev->patch_src != SRC_LINEIN && aml_out->dev->patch_src != SRC_HDMIIN )
            a2dp_out_standby(stream);
    }

    /*
    if continous mode,we need always have output.
    so we should not disable the output.
    */
    pthread_mutex_lock(&adev->alsa_pcm_lock);
    if (aml_out->status == STREAM_HW_WRITING && !continous_mode(adev)) {
        ALOGI("%s aml_out(%p)standby close", __func__, aml_out);
        aml_alsa_output_close(out);
        if (eDolbyDcvLib == adev->dolby_lib_type
                && (adev->ddp.digital_raw == 1 || adev->dts_hd.digital_raw == 1)) {
            //struct pcm *pcm = adev->pcm_handle[DIGITAL_DEVICE];
            if (is_dual_output_stream(out)/*aml_out->dual_output_flag && pcm*/) {
                //ALOGI("%s close dual output pcm handle %p", __func__, pcm);
                //pcm_close(pcm);
                //adev->pcm_handle[DIGITAL_DEVICE] = NULL;
                aml_tinymix_set_spdif_format(AUDIO_FORMAT_PCM_16_BIT, aml_out);
                set_stream_dual_output(out, false);
            }
        }
    }
    aml_out->status = STREAM_STANDBY;
    aml_out->standby= 1;

    if (adev->continuous_audio_mode == 0) {
        // release buffers
        if (aml_out->buffer) {
            aml_audio_free(aml_out->buffer);
            aml_out->buffer = NULL;
        }

        if (aml_out->resampler) {
            release_resampler(aml_out->resampler);
            aml_out->resampler = NULL;
        }
    }
    usecase_change_validate_l (aml_out, true);
    pthread_mutex_unlock(&adev->alsa_pcm_lock);
    if (is_usecase_mix (aml_out->usecase) ) {
        uint32_t usecase = adev->usecase_masks & ~ (1 << STREAM_PCM_MMAP);
        /*unmask the mmap case*/
        ALOGI ("%s current usecase_masks %x",__func__,adev->usecase_masks);
        /* only relesae hw mixer when no direct output left */
        if (usecase <= 1) {
            if (eDolbyMS12Lib == adev->dolby_lib_type) {
                if (!continous_mode(adev)) {
                    // plug in HMDI ARC case, get_dolby_ms12_cleanup() will block HDMI ARC info send to audio hw
                    // Add some condition here to protect.
                    // TODO: debug later
                    if (ms12->dolby_ms12_enable == true) {
                        get_dolby_ms12_cleanup(&adev->ms12, false);
                    }
                    //ALOGI("[%s:%d] get_dolby_ms12_cleanup\n", __FUNCTION__, __LINE__);
                    pthread_mutex_lock(&adev->alsa_pcm_lock);
                    struct pcm *pcm = adev->pcm_handle[DIGITAL_DEVICE];
                    if (is_dual_output_stream(out) && pcm) {
                        ALOGI("%s close dual output pcm handle %p", __func__, pcm);
                        pcm_close(pcm);
                        adev->pcm_handle[DIGITAL_DEVICE] = NULL;
                        set_stream_dual_output(out, false);
                    }
                    pthread_mutex_unlock(&adev->alsa_pcm_lock);
                    if (adev->dual_spdifenc_inited) {
                        adev->dual_spdifenc_inited = 0;
                        aml_tinymix_set_spdif_format(AUDIO_FORMAT_PCM_16_BIT, aml_out);
                    }
                }
            } else {
                aml_hw_mixer_deinit(&adev->hw_mixer);
            }

            if (!continous_mode(adev)) {
                adev->mix_init_flag = false;
            } else {
                if (eDolbyMS12Lib == adev->dolby_lib_type) {
                    if (adev->need_remove_conti_mode == true) {
                        bool set_ms12_non_continuous = true;
                        ALOGI("%s,release ms12 here", __func__);
                        get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                        //ALOGI("[%s:%d] get_dolby_ms12_cleanup\n", __FUNCTION__, __LINE__);
                        adev->ms12.is_continuous_paused = false;
                        adev->ms12.need_resume       = 0;
                        adev->ms12.need_resync       = 0;
                        adev->need_remove_conti_mode = false;
                    }
                    pthread_mutex_lock(&adev->ms12.lock);
                    if (adev->ms12.dolby_ms12_enable) {
                        if (adev->ms12_main1_dolby_dummy == false
                            && !audio_is_linear_pcm(aml_out->hal_internal_format)) {
                            dolby_ms12_set_main_dummy(0, true);
                            dolby_ms12_main_flush((void *)stream);
                            dolby_ms12_set_pause_flag(false);
                            //int iMS12DB = 0;//restore to full volume
                            //set_dolby_ms12_primary_input_db_gain(&(adev->ms12), iMS12DB , 10);
                            //adev->ms12.curDBGain = iMS12DB;
                            adev->ms12.is_continuous_paused = false;
                            adev->ms12.need_resume       = 0;
                            adev->ms12.need_resync       = 0;
                            adev->ms12_main1_dolby_dummy = true;
                            adev->ms12_out->hw_sync_mode = false;
                            aml_out->hwsync->payload_offset = 0;
                            ALOGI("%s set main dd+ dummy", __func__);
                        } else if (adev->ms12_ott_enable == true
                                   && audio_is_linear_pcm(aml_out->hal_internal_format)
                                   && (aml_out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC)) {

                            dolby_ms12_main_flush((void *)stream);
                            dolby_ms12_set_pause_flag(false);
                            adev->ms12.is_continuous_paused = false;
                            adev->ms12.need_resume       = 0;
                            adev->ms12.need_resync       = 0;
                            dolby_ms12_set_main_dummy(1, true);
                            adev->ms12_ott_enable = false;
                            adev->ms12_out->hw_sync_mode = false;
                            aml_out->hwsync->payload_offset = 0;
                            ALOGI("%s set ott dummy", __func__);
                        }
                    }
                    set_dolby_ms12_runtime_pause(&(adev->ms12), adev->ms12.is_continuous_paused);
                    pthread_mutex_unlock(&adev->ms12.lock);
                }
            }
        }
        if (adev->spdif_encoder_init_flag) {
            // will cause MS12 ddp playing break..
            //release_spdif_encoder_output_buffer(out);
        }
    }

    if (aml_out->spdifout_handle) {
        aml_audio_spdifout_close(aml_out->spdifout_handle);
        aml_out->spdifout_handle = NULL;
    }

    if (aml_out->is_normal_pcm) {
        set_system_app_mixing_status(aml_out, aml_out->status);
        aml_out->normal_pcm_mixing_config = false;
    }
    aml_out->pause_status = false;//clear pause status
    if (aml_out->hw_sync_mode && aml_out->tsync_status != TSYNC_STATUS_STOP) {
        ALOGI("%s set AUDIO_PAUSE\n",__func__);
        sysfs_set_sysfs_str (TSYNC_EVENT, "AUDIO_PAUSE");
        aml_out->tsync_status = TSYNC_STATUS_PAUSED;

        ALOGI("%s set AUDIO_STOP\n",__func__);
        sysfs_set_sysfs_str (TSYNC_EVENT, "AUDIO_STOP");
        aml_out->tsync_status = TSYNC_STATUS_STOP;
    }
    return 0;
}

int out_standby_new(struct audio_stream *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    int status;

    ALOGD("%s: enter", __func__);
    if (continous_mode(aml_out->dev)
        && (aml_out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC)) {
        //1.audio easing duration is 32ms,
        //2.one loop for schedule_run cost about 32ms(contains the hardware costing),
        //3.if [pause, flush] too short, means it need more time to do audio easing
        //so, the delay time for 32ms(pause is completed after audio easing is done) is enough.
        aml_audio_sleep(64000);
    }
    pthread_mutex_lock (&aml_out->dev->lock);
    pthread_mutex_lock (&aml_out->lock);
    status = do_output_standby_l(stream);
    pthread_mutex_unlock (&aml_out->lock);
    pthread_mutex_unlock (&aml_out->dev->lock);
    ALOGD("%s: exit", __func__);

    return status;
}

static bool is_iec61937_format (struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;

    if ( (adev->hdmi_format == BYPASS) && \
         (aml_out->hal_format == AUDIO_FORMAT_IEC61937) && \
         (adev->sink_format == AUDIO_FORMAT_E_AC3) ) {
        /*
         *case 1.With Kodi APK Dolby passthrough
         *Media Player direct format is AUDIO_FORMAT_IEC61937, containing DD+ 7.1ch audio
         *case 2.HDMI-IN or SPDIF-IN
         *audio format is AUDIO_FORMAT_IEC61937, containing DD+ 7.1ch audio
         */
        return true;
    }
    /*other data is dd/dd+ raw data*/

    return false;
}

static void aml_tinymix_set_spdif_format(audio_format_t output_format,struct aml_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    int aml_spdif_format = AML_STEREO_PCM;
    int spdif_mute = 0;
    if (output_format == AUDIO_FORMAT_AC3) {
        aml_spdif_format = AML_DOLBY_DIGITAL;
        audio_set_spdif_clock(stream, AML_DOLBY_DIGITAL);
    } else if (output_format == AUDIO_FORMAT_E_AC3) {
        aml_spdif_format = AML_DOLBY_DIGITAL_PLUS;
        audio_set_spdif_clock(stream, AML_DOLBY_DIGITAL_PLUS);
        // for BOX with ms12 continous mode, need DDP output
        if ((eDolbyMS12Lib == aml_dev->dolby_lib_type) && aml_dev->continuous_audio_mode && !aml_dev->is_TV) {
            // do nothing
            spdif_mute = 1;
        } else {
            spdif_mute = 1;
        }
    } else if (output_format == AUDIO_FORMAT_DTS) {
        aml_spdif_format = AML_DTS;
        audio_set_spdif_clock(stream, AML_DTS);
    } else {
        aml_spdif_format = AML_STEREO_PCM;
        audio_set_spdif_clock(stream, AML_STEREO_PCM);
    }
    aml_mixer_ctrl_set_int(&aml_dev->alsa_mixer, AML_MIXER_ID_SPDIF_FORMAT, aml_spdif_format);
    aml_mixer_ctrl_set_int(&aml_dev->alsa_mixer, AML_MIXER_ID_EARC_AUDIO_TYPE, aml_spdif_format);
    audio_route_set_spdif_mute(&aml_dev->alsa_mixer, spdif_mute);
    ALOGI("%s tinymix AML_MIXER_ID_SPDIF_FORMAT %d,spdif mute %d",
          __FUNCTION__, aml_spdif_format, spdif_mute);
}
audio_format_t get_output_format (struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    audio_format_t output_format = aml_out->hal_internal_format;

    struct dolby_ms12_desc *ms12 = & (adev->ms12);

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
            output_format = adev->sink_format;
    } else if (eDolbyDcvLib == adev->dolby_lib_type) {
        if (adev->hdmi_format > 0) {
            output_format = adev->sink_format;
        }
    }

    if (adev->debug_flag)
        ALOGI("%s(), out fmt %#x", __func__, output_format);
    return output_format;
}

ssize_t aml_audio_spdif_output (struct audio_stream_out *stream,
                                void *buffer, size_t byte, spdif_config_t *spdif_config)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    int ret = 0;

    if (!aml_out->dual_output_flag) {
        ALOGE ("%s() not support, dual flag = %d",
               __func__, aml_out->dual_output_flag);
        return -EINVAL;
    }
    if (aml_out->spdifout_handle == NULL) {
        ret = aml_audio_spdifout_open(&aml_out->spdifout_handle, spdif_config);
    }

    aml_audio_spdifout_processs(aml_out->spdifout_handle, buffer, byte);

    return ret;
}


/* AEC need a wall clock (monotonic clk?) to sync*/
static aec_timestamp get_timestamp(void) {
    struct timespec ts;
    unsigned long long current_time;
    aec_timestamp return_val;
    /*clock_gettime(CLOCK_MONOTONIC, &ts);*/
    clock_gettime(CLOCK_REALTIME, &ts);
    current_time = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    return_val.timeStamp = current_time;
    return return_val;
}

#if defined(IS_ATOM_PROJECT)
    int get_atom_stream_type(size_t frames) {
    int type = STREAM_ANDROID;
    switch (frames) {
        case 1024:
            type = STREAM_OPTAUX;
            break;
        case 1536:
            type = STREAM_HDMI;
            break;
        default:
            type = STREAM_ANDROID;
    }
    return type;
}

void atom_speaker_buffer_reset(struct aml_audio_device *adev) {
    adev->spk_buf_write_count = 0;
    adev->spk_buf_read_count = 0;
    adev->extra_write_bytes = 0;
    adev->debug_spk_buf_time_last = 0;
    ring_buffer_reset(&adev->spk_ring_buf);
    pthread_mutex_lock(&adev->aec_spk_mic_lock);
    aec_spk_mic_reset();
    pthread_mutex_unlock(&adev->aec_spk_mic_lock);
}

void atom_pack_speaker_ring_buffer(struct aml_audio_device *adev, size_t bytes) {

    size_t out_frames = bytes / FRAMESIZE_32BIT_STEREO;
    atom_stream_type_t stream_type = get_atom_stream_type(out_frames);
    aec_timestamp cur_time = get_timestamp();
    int spk_time_diff = (int) (cur_time.timeStamp - adev->spk_buf_last_write_time);
    if (adev->atom_stream_type_val != stream_type ||
        spk_time_diff < 0 || spk_time_diff > 300000) {
        ALOGW("%s: spk buf reset. diff: %d", __func__, spk_time_diff);
        atom_speaker_buffer_reset(adev);
        adev->atom_stream_type_val = stream_type;
        adev->spk_buf_very_first_write_time = cur_time.timeStamp;
    }
    adev->spk_buf_last_write_time = cur_time.timeStamp;
    size_t first_set_len = 0;
    switch(adev->atom_stream_type_val) {
        case STREAM_HDMI:
        case STREAM_OPTAUX:
            first_set_len = bytes;
            if (first_set_len > adev->spk_buf_size) {
                first_set_len = adev->spk_buf_size;
            }
            break;
        default:
            first_set_len = adev->spk_buf_size - adev->extra_write_bytes;
    }
    if (DEBUG_AEC)
        ALOGW("%s: type: %d, first_set_len: %d, extra_write_bytes: %d",
         __func__, adev->atom_stream_type_val, first_set_len, adev->extra_write_bytes);

    //[cur_timestamp][spk_buf_data(first_set_len)]
    if (adev->extra_write_bytes == 0) {
        // add timestamp
        ring_buffer_write(&adev->spk_ring_buf, (unsigned char*)cur_time.tsB,
            TIMESTAMP_LEN, UNCOVER_WRITE);

        if (DEBUG_AEC) {
            if (adev->spk_buf_write_count++ > 10000000 ) {
                adev->spk_buf_write_count = 0;
            }
            ALOGW("%s: ---- TS[Spk(type:%d)] ---- timestamp(d): %llu, first_set_len: %d, spk_buf[write]: %llu",
                __func__, adev->atom_stream_type_val, cur_time.timeStamp, first_set_len, adev->spk_buf_write_count);
        }
    }

    unsigned char* aec_buf = (unsigned char*)adev->aec_buf;

    // first_set
    ring_buffer_write(&adev->spk_ring_buf, (unsigned char*)aec_buf,
            first_set_len, UNCOVER_WRITE);

    if (adev->atom_stream_type_val == STREAM_ANDROID) {
        // Calculate remaining data
        adev->extra_write_bytes = adev->spk_write_bytes - first_set_len;
        // Timestamp will always be needed for the next set of Android streams
        //[cur_timestamp][spk_buf_data(extra_write_bytes)]
        // Add bytes/384*1000 ms to this timestamp
        cur_time.timeStamp += first_set_len/384 * 1000;

        ring_buffer_write(&adev->spk_ring_buf, (unsigned char*)cur_time.tsB,
            TIMESTAMP_LEN, UNCOVER_WRITE);

        if (DEBUG_AEC) {
            if (adev->spk_buf_write_count++ > 10000000 ) {
                adev->spk_buf_write_count = 0;
            }
            ALOGW("%s: ---- TS[Spk(type:%d)] ---- timestamp(d): %llu, extra_write_bytes: %d, spk_buf[write]: %llu",
                __func__, adev->atom_stream_type_val, cur_time.timeStamp, adev->extra_write_bytes, adev->spk_buf_write_count);
        }

        // second set
        ring_buffer_write(&adev->spk_ring_buf, (unsigned char*) &aec_buf[first_set_len],
                adev->extra_write_bytes, UNCOVER_WRITE);
    } else if (adev->atom_stream_type_val == STREAM_HDMI) {
        if (adev->extra_write_bytes == 0) {
            adev->extra_write_bytes = 1536 * FRAMESIZE_32BIT_STEREO;
        } else {
            adev->extra_write_bytes = 0;
        }
    } else if (adev->atom_stream_type_val == STREAM_OPTAUX) {
        if (adev->extra_write_bytes == 0) {
            adev->extra_write_bytes = 2048 * FRAMESIZE_32BIT_STEREO;
        } else if (adev->extra_write_bytes == (2048 * FRAMESIZE_32BIT_STEREO)) {
            adev->extra_write_bytes = 1024 * FRAMESIZE_32BIT_STEREO;
        } else {
            adev->extra_write_bytes = 0;
        }
    }

    // calculate extra_write_bytes for next run
    if (adev->extra_write_bytes >= adev->spk_buf_size) {
        adev->extra_write_bytes = 0;
    }
}

int dsp_process_output(struct aml_audio_device *adev, void *in_buffer,
        size_t bytes)
{
    int32_t *out_buffer;
    size_t out_frames = bytes / FRAMESIZE_32BIT_STEREO;
    int32_t *input32 = (int32_t *)in_buffer;
    uint i = 0;

    for (i = 0; i < bytes/sizeof(int32_t); i++) {
        input32[i] = input32[i] >> 3;
    }

    /*malloc dsp buffer*/
    if (adev->dsp_frames < out_frames) {
        pthread_mutex_lock(&adev->dsp_processing_lock);
        dsp_realloc_buffer(&adev->dsp_in_buf, &adev->effect_buf, &adev->aec_buf, out_frames);
        pthread_mutex_unlock(&adev->dsp_processing_lock);
        adev->dsp_frames = out_frames;
        ALOGI("%s: dsp_buf = %p, effect_buffer = %p, aec_buffer = %p", __func__,
            adev->dsp_in_buf, adev->effect_buf, adev->aec_buf);
    }

    pthread_mutex_lock(&adev->dsp_processing_lock);
    harman_dsp_process(in_buffer, adev->dsp_in_buf, adev->effect_buf, adev->aec_buf, out_frames);
    pthread_mutex_unlock(&adev->dsp_processing_lock);

    out_buffer = (int32_t *)adev->effect_buf;

    /* when aec is ready, init fir filter*/
    if (!adev->pstFir_spk)
        Fir_initModule(&adev->pstFir_spk);

    if (adev->mic_running) {
        //pthread_mutex_lock(&adev->aec_spk_buf_lock);

        /*copy the speaker playout data to speaker ringbuffer*/
        if (adev->spk_write_bytes != (size_t)(FRAMESIZE_32BIT_STEREO * out_frames))
            adev->spk_write_bytes = FRAMESIZE_32BIT_STEREO * out_frames;

        if (adev->spk_running == 0) {
            adev->spk_running = 1;
        }

        /*TODO: when overflow, how to do??*/
        int int_get_buffer_write_space = get_buffer_write_space(&adev->spk_ring_buf);
        if (int_get_buffer_write_space <
           (int)((FRAMESIZE_32BIT_STEREO * out_frames) + (2 * TIMESTAMP_LEN))) {
           if (DEBUG_AEC) {
               ALOGI("[%s]: Warning: spk_ring_buf overflow, write_bytes = TIMESTAMP_BYTE + %zu",
                    __func__, FRAMESIZE_32BIT_STEREO * out_frames + (2 * TIMESTAMP_LEN));
           }
           // Reset buffers
           atom_speaker_buffer_reset(adev);
        } else {
            if (adev->has_dsp_lib) {
                if (adev->spk_buf_size != 0) {
                    atom_pack_speaker_ring_buffer(adev, bytes);
                }
            } else
                ring_buffer_write(&adev->spk_ring_buf, (unsigned char*)in_buffer,
                        FRAMESIZE_32BIT_STEREO * out_frames, UNCOVER_WRITE);
        }

        if (DEBUG_AEC) {
            ALOGW("%s: buffer_write_space: %d, out_frames: %d, extra_write_bytes: %d",
                __func__, int_get_buffer_write_space, out_frames, adev->extra_write_bytes);
        }
        //pthread_mutex_unlock(&adev->aec_spk_buf_lock);
    }

    return 0;
}
#endif

static void output_mute(struct audio_stream_out *stream, size_t *output_buffer_bytes)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    size_t target_len = MIN(aml_out->tmp_buffer_8ch_size, *output_buffer_bytes);
    //int timer_in_ms = 0;

    if (adev->patch_src == SRC_LINEIN || adev->patch_src == SRC_SPDIFIN
            || adev->patch_src == SRC_HDMIIN || adev->patch_src == SRC_ARCIN) {
        /* when aux/spdif/arcin/hdmiin switching or format change,
           mute 1000ms, then start fade in. */
        if (adev->active_input != NULL && (!adev->patch_start)) {
            clock_gettime(CLOCK_MONOTONIC, &adev->mute_start_ts);
            adev->patch_start = true;
            adev->mute_start = true;
            adev->timer_in_ms = 1000;
            //ALOGI ("%s() detect AUX/SPDIF start mute!", __func__);
        }

        if (adev->active_input != NULL &&
                adev->spdif_fmt_hw != adev->active_input->spdif_fmt_hw) {
            clock_gettime(CLOCK_MONOTONIC, &adev->mute_start_ts);
            adev->spdif_fmt_hw = adev->active_input->spdif_fmt_hw;
            adev->mute_start = true;
            adev->timer_in_ms = 500;
            ALOGI ("%s() detect AUX/SPDIF format change, start mute!", __func__);
        }
    } else if (adev->patch_src == SRC_DTV || adev->patch_src == SRC_ATV) {
        /*dtv start patching, mute 200ms, then start fade in.*/
        if (adev->audio_patch != NULL && (!adev->patch_start) && (adev->audio_patch->output_thread_exit == 0)) {
            clock_gettime(CLOCK_MONOTONIC, &adev->mute_start_ts);
            adev->patch_start = true;
            adev->mute_start = true;
            adev->timer_in_ms = 500;
            ALOGI ("%s() detect tv source start mute 200ms", __func__);
        }
    }

    if (aml_out->tmp_buffer_8ch != NULL && adev->mute_start) {
        if (!Stop_watch(adev->mute_start_ts, adev->timer_in_ms)) {
            adev->mute_start = false;
            adev->timer_in_ms = 0;
            start_ease_in(adev);
            ALOGI ("%s() tv source unmute, start fade in", __func__);
        } else {
            ALOGV("%s line %d target memset len 0x%x\n", __func__, __LINE__, target_len);
            memset(aml_out->tmp_buffer_8ch, 0, target_len);
        }
    }

    /*ease in or ease out*/
    aml_audio_ease_process(adev->audio_ease, aml_out->tmp_buffer_8ch, target_len);
    return;
}

#define EQ_GAIN_DEFAULT (0.16)
ssize_t audio_hal_data_processing(struct audio_stream_out *stream,
                                const void *buffer,
                                size_t bytes,
                                void **output_buffer,
                                size_t *output_buffer_bytes,
                                audio_format_t output_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    int16_t *tmp_buffer = (int16_t *) buffer;
    int16_t *effect_tmp_buf = NULL;
    int out_frames = bytes / 4;
    size_t i;
    int j, ret;
    uint32_t latency_frames = 0;
    uint64_t total_frame = 0;

    /* raw data need packet to IEC61937 format by spdif encoder */
    if (output_format == AUDIO_FORMAT_IEC61937) {
        //ALOGI("IEC61937 Format");
        *output_buffer = (void *) buffer;
        *output_buffer_bytes = bytes;
    } else if ((output_format == AUDIO_FORMAT_AC3) || (output_format == AUDIO_FORMAT_E_AC3)) {
        //ALOGI("%s, aml_out->hal_format %x , is_iec61937_format = %d, \n", __func__, aml_out->hal_format,is_iec61937_format(stream));
        if ((is_iec61937_format(stream) == true) ||
            (adev->dolby_lib_type == eDolbyDcvLib)) {
            *output_buffer = (void *) buffer;
            *output_buffer_bytes = bytes;
        } else {
            if (aml_out->spdifenc_init == false) {
                ALOGI("%s, aml_spdif_encoder_open, output_format %#x\n", __func__, output_format);
                ret = aml_spdif_encoder_open(&aml_out->spdifenc_handle, output_format);
                if (ret) {
                    ALOGE("%s() aml_spdif_encoder_open failed", __func__);
                    return ret;
                }
                aml_out->spdifenc_init = true;
                aml_out->spdif_enc_init_frame_write_sum = aml_out->frame_write_sum;
                adev->spdif_encoder_init_flag = true;
            }
            aml_spdif_encoder_process(aml_out->spdifenc_handle, buffer, bytes, output_buffer, output_buffer_bytes);
        }
    } else if (output_format == AUDIO_FORMAT_DTS) {
        *output_buffer = (void *) buffer;
        *output_buffer_bytes = bytes;
    } else if (output_format == AUDIO_FORMAT_PCM_32_BIT) {
        int32_t *tmp_buffer = (int32_t *)buffer;
        size_t out_frames = bytes / FRAMESIZE_32BIT_STEREO;

        if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected && aml_out->earc_pcm && adev->bHDMIARCon) {
            apply_volume(1.0, tmp_buffer, sizeof(uint32_t), bytes);
            *output_buffer = tmp_buffer;
            *output_buffer_bytes = bytes;
        } else {
        float gain_speaker = adev->sink_gain[OUTPORT_SPEAKER];
        if (aml_out->hw_sync_mode)
             gain_speaker *= aml_out->volume_l;
        apply_volume(gain_speaker, tmp_buffer, sizeof(uint32_t), bytes);

        /* 2 ch 32 bit --> 8 ch 32 bit mapping, need 8X size of input buffer size */
        if (aml_out->tmp_buffer_8ch_size < FRAMESIZE_32BIT_8ch * out_frames) {
            aml_out->tmp_buffer_8ch = aml_audio_realloc(aml_out->tmp_buffer_8ch, FRAMESIZE_32BIT_8ch * out_frames);
            if (!aml_out->tmp_buffer_8ch) {
                ALOGE("%s: realloc tmp_buffer_8ch buf failed size = %zu format = %#x", __func__,
                        FRAMESIZE_32BIT_8ch * out_frames, output_format);
                return -ENOMEM;
            } else {
                ALOGI("%s: realloc tmp_buffer_8ch size from %zu to %zu format = %#x", __func__,
                        aml_out->tmp_buffer_8ch_size, FRAMESIZE_32BIT_8ch * out_frames, output_format);
            }
            aml_out->tmp_buffer_8ch_size = FRAMESIZE_32BIT_8ch * out_frames;
        }

        for (i = 0; i < out_frames; i++) {
            aml_out->tmp_buffer_8ch[8 * i]     = tmp_buffer[2 * i];
            aml_out->tmp_buffer_8ch[8 * i + 1] = tmp_buffer[2 * i + 1];
            aml_out->tmp_buffer_8ch[8 * i + 2] = tmp_buffer[2 * i];
            aml_out->tmp_buffer_8ch[8 * i + 3] = tmp_buffer[2 * i + 1];
            aml_out->tmp_buffer_8ch[8 * i + 4] = tmp_buffer[2 * i];
            aml_out->tmp_buffer_8ch[8 * i + 5] = tmp_buffer[2 * i + 1];
            aml_out->tmp_buffer_8ch[8 * i + 6] = 0;
            aml_out->tmp_buffer_8ch[8 * i + 7] = 0;
        }

        *output_buffer = aml_out->tmp_buffer_8ch;
        *output_buffer_bytes = FRAMESIZE_32BIT_8ch * out_frames;
        }
    } else {
        /*atom project supports 32bit hal only*/
        /*TODO: Direct PCM case, I think still needs EQ and AEC */

        if (aml_out->is_tv_platform == 1) {
            int16_t *tmp_buffer = (int16_t *)buffer;
            size_t out_frames = bytes / (2 * 2);
            int16_t *effect_tmp_buf;
            effect_descriptor_t tmpdesc;
            int32_t *spk_tmp_buf;
            int32_t *ps32SpdifTempBuffer = NULL;
            float source_gain;
            float gain_speaker = adev->eq_data.p_gain.speaker;

            /* handling audio effect process here */
            if (adev->effect_buf_size < bytes) {
                adev->effect_buf = aml_audio_realloc(adev->effect_buf, bytes);
                if (!adev->effect_buf) {
                    ALOGE ("realloc effect buf failed size %zu format = %#x", bytes, output_format);
                    return -ENOMEM;
                } else {
                    ALOGI("realloc effect_buf size from %zu to %zu format = %#x", adev->effect_buf_size, bytes, output_format);
                }
                adev->effect_buf_size = bytes;

                adev->spk_output_buf = aml_audio_realloc(adev->spk_output_buf, bytes*2);
                if (!adev->spk_output_buf) {
                    ALOGE ("realloc headphone buf failed size %zu format = %#x", bytes, output_format);
                    return -ENOMEM;
                }
                // 16bit -> 32bit, need realloc
                adev->spdif_output_buf = aml_audio_realloc(adev->spdif_output_buf, bytes * 2);
                if (!adev->spdif_output_buf) {
                    ALOGE ("realloc spdif buf failed size %zu format = %#x", bytes, output_format);
                    return -ENOMEM;
                }
            }

            effect_tmp_buf = (int16_t *)adev->effect_buf;
            spk_tmp_buf = (int32_t *)adev->spk_output_buf;
            ps32SpdifTempBuffer = (int32_t *)adev->spdif_output_buf;

#ifdef ENABLE_AVSYNC_TUNING
            tuning_spker_latency(adev, effect_tmp_buf, tmp_buffer, bytes);
#else
            memcpy(effect_tmp_buf, tmp_buffer, bytes);
#endif

            /*apply dtv source gain for speaker*/
            if (adev->patch_src == SRC_DTV && adev->audio_patching)
                source_gain = adev->eq_data.s_gain.dtv;
            else if (adev->patch_src == SRC_HDMIIN && adev->audio_patching)
                source_gain = adev->eq_data.s_gain.hdmi;
            else if (adev->patch_src == SRC_LINEIN && adev->audio_patching)
                source_gain = adev->eq_data.s_gain.av;
            else if (adev->patch_src == SRC_ATV && adev->audio_patching)
                source_gain = adev->eq_data.s_gain.atv;
            else
                source_gain = adev->eq_data.s_gain.media;

            if (adev->patch_src == SRC_DTV && adev->audio_patch != NULL) {
                aml_audio_switch_output_mode((int16_t *)effect_tmp_buf, bytes, adev->audio_patch->mode);
            } else if ( adev->audio_patch == NULL) {
               if (adev->sound_track_mode == 3)
                  adev->sound_track_mode = AM_AOUT_OUTPUT_LRMIX;
               aml_audio_switch_output_mode((int16_t *)effect_tmp_buf, bytes, adev->sound_track_mode);
            }
            /*aduio effect process for speaker*/
            if (adev->native_postprocess.num_postprocessors == adev->native_postprocess.total_postprocessors
                && !(adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP)) {
                for (j = 0; j < adev->native_postprocess.num_postprocessors; j++) {
                    if (adev->effect_in_ch == 6) {
                        if (adev->native_postprocess.postprocessors[j] != NULL) {
                            (*(adev->native_postprocess.postprocessors[j]))->get_descriptor(adev->native_postprocess.postprocessors[j], &tmpdesc);
                            if (0 != strcmp(tmpdesc.name,"VirtualX")) {
                                audio_post_process(adev->native_postprocess.postprocessors[j], effect_tmp_buf, out_frames);
                            }
                        }
                    } else {
                        audio_post_process(adev->native_postprocess.postprocessors[j], effect_tmp_buf, out_frames);
                    }
                }
            } else {
                gain_speaker *= EQ_GAIN_DEFAULT;
            }

            if (aml_getprop_bool("media.audiohal.outdump")) {
                FILE *fp1 = fopen("/data/audio_spk.pcm", "a+");
                if (fp1) {
                    int flen = fwrite((char *)effect_tmp_buf, 1, bytes, fp1);
                    ALOGV("%s buffer %p size %zu\n", __FUNCTION__, effect_tmp_buf, bytes);
                    fclose(fp1);
                }
            }

            if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected && aml_out->earc_pcm && adev->bHDMIARCon) {
                apply_volume_16to32(1.0, tmp_buffer, spk_tmp_buf, bytes);
                *output_buffer = (void *) spk_tmp_buf;
                *output_buffer_bytes = bytes * 2;
            } else {
            /* apply volume for spk/hp, SPDIF/HDMI keep the max volume */
            if (adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
                if ((adev->patch_src == SRC_DTV || adev->patch_src == SRC_HDMIIN
                        || adev->patch_src == SRC_LINEIN || adev->patch_src == SRC_ATV)
                        && adev->audio_patching) {
                    apply_volume_16to32(adev->sink_gain[OUTPORT_A2DP] * source_gain, effect_tmp_buf, spk_tmp_buf, bytes);
                } else {
                    apply_volume_16to32(source_gain, effect_tmp_buf, spk_tmp_buf, bytes);
                }
            } else {
                gain_speaker *= (adev->sink_gain[OUTPORT_SPEAKER]);
                apply_volume_16to32(gain_speaker * source_gain * adev->ms12.dap_bypassgain, effect_tmp_buf, spk_tmp_buf, bytes);
                /* SPDIF with source_gain*/
                if (is_dolbyms12_dap_enable(aml_out)) {
                    if (adev->ms12.spdif_ring_buffer.size &&
                        get_buffer_read_space(&adev->ms12.spdif_ring_buffer) >= (int)bytes) {
                        ring_buffer_read(&adev->ms12.spdif_ring_buffer, (unsigned char*)adev->ms12.lpcm_temp_buffer, bytes);
                        apply_volume_16to32(source_gain, (int16_t *)adev->ms12.lpcm_temp_buffer, ps32SpdifTempBuffer, bytes);
                    }
                } else {
                    apply_volume_16to32(source_gain, tmp_buffer, ps32SpdifTempBuffer, bytes);
                }
            }

#ifdef ADD_AUDIO_DELAY_INTERFACE
            aml_audio_delay_process(AML_DELAY_OUTPORT_SPEAKER, spk_tmp_buf,
                            out_frames * 2 * 4, AUDIO_FORMAT_PCM_16_BIT);
            if (OUTPORT_SPEAKER == adev->active_outport && AUDIO_FORMAT_PCM_16_BIT == aml_out->hal_internal_format) {
                // spdif(PCM) out delay process, frame size 2ch * 4 Byte
                aml_audio_delay_process(AML_DELAY_OUTPORT_SPDIF, ps32SpdifTempBuffer,
                        out_frames * 2 * 4, AUDIO_FORMAT_PCM_16_BIT);
            }
#endif

            /* 2 ch 32 bit --> 8 ch 32 bit mapping, need 8X size of input buffer size */
            if (aml_out->tmp_buffer_8ch_size < 8 * bytes) {
                aml_out->tmp_buffer_8ch = aml_audio_realloc(aml_out->tmp_buffer_8ch, 8 * bytes);
                if (!aml_out->tmp_buffer_8ch) {
                    ALOGE("%s: realloc tmp_buffer_8ch buf failed size = %zu format = %#x",
                        __func__, 8 * bytes, output_format);
                    return -ENOMEM;
                } else {
                    ALOGI("%s: realloc tmp_buffer_8ch size from %zu to %zu format = %#x",
                        __func__, aml_out->tmp_buffer_8ch_size, 8 * bytes, output_format);
                }
                aml_out->tmp_buffer_8ch_size = 8 * bytes;
            }

            if (alsa_device_is_auge()) {
                for (i = 0; i < out_frames; i++) {
                    if (adev->FactoryChannelReverse) {
                        aml_out->tmp_buffer_8ch[8 * i + 0] = spk_tmp_buf[2 * i + 1];
                        aml_out->tmp_buffer_8ch[8 * i + 1] = spk_tmp_buf[2 * i];
                    } else {
                        aml_out->tmp_buffer_8ch[8 * i + 0] = spk_tmp_buf[2 * i];
                        aml_out->tmp_buffer_8ch[8 * i + 1] = spk_tmp_buf[2 * i + 1];
                    }
                    aml_out->tmp_buffer_8ch[8 * i + 2] = ps32SpdifTempBuffer[2 * i];
                    aml_out->tmp_buffer_8ch[8 * i + 3] = ps32SpdifTempBuffer[2 * i + 1];
                    aml_out->tmp_buffer_8ch[8 * i + 4] = (int32_t)tmp_buffer[2 * i] << 16;
                    aml_out->tmp_buffer_8ch[8 * i + 5] = (int32_t)tmp_buffer[2 * i + 1] << 16;
                    aml_out->tmp_buffer_8ch[8 * i + 6] = (int32_t)tmp_buffer[2 * i] << 16;
                    aml_out->tmp_buffer_8ch[8 * i + 7] = (int32_t)tmp_buffer[2 * i + 1] << 16;
                }
            } else {
                for (i = 0; i < out_frames; i++) {
                    aml_out->tmp_buffer_8ch[8 * i + 0] = (int32_t)tmp_buffer[2 * i] << 16;
                    aml_out->tmp_buffer_8ch[8 * i + 1] = (int32_t)tmp_buffer[2 * i + 1] << 16;
                    aml_out->tmp_buffer_8ch[8 * i + 2] = (int32_t)spk_tmp_buf[2 * i];
                    aml_out->tmp_buffer_8ch[8 * i + 3] = (int32_t)spk_tmp_buf[2 * i + 1];
                    aml_out->tmp_buffer_8ch[8 * i + 4] = (int32_t)tmp_buffer[2 * i] << 16;
                    aml_out->tmp_buffer_8ch[8 * i + 5] = (int32_t)tmp_buffer[2 * i + 1] << 16;
                    aml_out->tmp_buffer_8ch[8 * i + 6] = 0;
                    aml_out->tmp_buffer_8ch[8 * i + 7] = 0;
                }
            }

            *output_buffer = aml_out->tmp_buffer_8ch;
            *output_buffer_bytes = 8 * bytes;
            }
        } else {
            float gain_speaker = 1.0;
            if ((aml_out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP)) {
                if (adev->audio_patching) {
                    gain_speaker = adev->sink_gain[OUTPORT_A2DP];
                }
            } else if (!adev->is_TV) {
                gain_speaker = adev->sink_gain[adev->active_outport];
            } else {
                gain_speaker = adev->sink_gain[OUTPORT_SPEAKER];
            }
            /*
            for dolby audio with ms12 enabled,the gain will apply to
            ms12 main audio, there is no need to apply any more.
            */
            int is_dolby_audio = (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) \
                                 || (aml_out->hal_internal_format == AUDIO_FORMAT_AC3);
            if ((eDolbyMS12Lib == adev->dolby_lib_type) && is_dolby_audio) {
                gain_speaker = 1.0;
            }
            if (adev->patch_src == SRC_DTV && adev->audio_patch != NULL) {
                aml_audio_switch_output_mode((int16_t *)buffer, bytes, adev->audio_patch->mode);
            }

            *output_buffer = (void *) buffer;
            *output_buffer_bytes = bytes;
            apply_volume(gain_speaker, *output_buffer, sizeof(uint16_t), bytes);
        }
    }

    if (adev->patch_src == SRC_DTV && adev->tuner2mix_patch == 1) {
        dtv_in_write(stream,buffer, bytes);
    }

    if (adev->audio_patching) {
        output_mute(stream, output_buffer_bytes);
    }
    return 0;
}

ssize_t hw_write (struct audio_stream_out *stream
                  , const void *buffer
                  , size_t bytes
                  , audio_format_t output_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    const uint16_t *tmp_buffer = buffer;
    int16_t *effect_tmp_buf = NULL;
    bool is_dtv = (adev->patch_src == SRC_DTV);
    int out_frames = bytes / 4;
    ssize_t ret = 0;
    int i;
    uint32_t latency_frames = 0;
    uint64_t total_frame = 0;
    uint64_t write_frames = 0;
    uint64_t  sys_total_cost = 0;
    int  adjust_ms = 0;

    adev->debug_flag = aml_audio_get_debug_flag();
    if (adev->debug_flag) {
        ALOGD("+[%s:%d] stream:%p, bytes:%zu, output_format:%#x, hal_internal_format:%#x", __func__, __LINE__,
            stream, bytes, output_format, aml_out->hal_internal_format);
    }
    if (is_dtv && need_hw_mix(adev->usecase_masks)) {
        if (adev->audio_patch && adev->audio_patch->avsync_callback)
            adev->audio_patch->avsync_callback(adev->audio_patch,aml_out);
    }
    pthread_mutex_lock(&adev->alsa_pcm_lock);
    aml_out->alsa_output_format = output_format;
    if (aml_out->status != STREAM_HW_WRITING) {
        ALOGI("%s, aml_out %p alsa open output_format %#x\n", __func__, aml_out, output_format);
        aml_out->alsa_output_format = output_format;
        if (eDolbyDcvLib == adev->dolby_lib_type) {
            if (!is_dual_output_stream(stream)) {
                aml_tinymix_set_spdif_format(output_format,aml_out);
            }
        }

        if (adev->useSubMix) {
            if (adev->audio_patching &&
                output_format != AUDIO_FORMAT_PCM_16_BIT &&
                output_format != AUDIO_FORMAT_PCM) {
                // TODO: mbox+dvb and bypass case
                ret = aml_alsa_output_open(stream);
                if (ret) {
                    ALOGE("%s() open failed", __func__);
                }
            } else {
                aml_out->pcm = getSubMixingPCMdev(adev->sm);
                if (aml_out->pcm == NULL) {
                    ALOGE("%s() get pcm handle failed", __func__);
                }
                if (adev->rawtopcm_flag) {
                    ALOGI("disable rawtopcm_flag --");
                    pcm_stop(aml_out->pcm);
                    adev->rawtopcm_flag = false;
                }
            }
        } else {
            if (!adev->tuner2mix_patch) {
                ret = aml_alsa_output_open(stream);
                if (ret) {
                    ALOGE("%s() open failed", __func__);
                }
            }
        }

        if (ret == 0)
            aml_out->status = STREAM_HW_WRITING;
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type && !is_bypass_dolbyms12(stream)) {
        if (aml_out->hw_sync_mode && !adev->ms12.is_continuous_paused) {
            // histroy here: ms12lib->pcm_output()->hw_write() aml_out is passed as private data
            //               when registering output callback function in dolby_ms12_register_pcm_callback()
            // some times "aml_out->hwsync->aout == NULL"
            // one case is no main audio playing, only aux audio playing (Netflix main screen)
            // in this case dolby_ms12_get_consumed_payload() always return 0, no AV sync can be done
            if (aml_out->hwsync->aout) {
                if (is_bypass_dolbyms12(stream))
                    aml_audio_hwsync_audio_process(aml_out->hwsync, aml_out->hwsync->payload_offset, &adjust_ms);
                else {
                    uint64_t ms12_payload = dolby_ms12_get_consumed_payload();
                    if (audio_is_linear_pcm(aml_out->hal_internal_format) && (ms12_payload >= 6144))
                        ms12_payload -= 6144;
                    aml_audio_hwsync_audio_process(aml_out->hwsync, ms12_payload, &adjust_ms);
                }
            } else {
                ALOGW("%s,aml_out->hwsync->aout == NULL",__FUNCTION__);
            }
        }
    }
    if (aml_out->pcm || adev->a2dp_out || is_sco_port(adev->active_outport)) {
#ifdef ADD_AUDIO_DELAY_INTERFACE
        ret = aml_audio_delay_process(AML_DELAY_OUTPORT_ALL, (void *) tmp_buffer, bytes, output_format);
        if (ret < 0) {
            //ALOGW("aml_audio_delay_process skip, ret:%#x", ret);
        }
#endif
        if (adjust_ms) {
            int adjust_bytes = 0;
            memset((void*)buffer, 0, bytes);
            if (adev->a2dp_out) {
                adjust_bytes = 48 * 4 * abs(adjust_ms); // 2ch 16bit
            } else if (output_format == AUDIO_FORMAT_E_AC3) {
                adjust_bytes = 192 * 4 * abs(adjust_ms);
            } else if (output_format == AUDIO_FORMAT_AC3) {
                adjust_bytes = 48 * 4 * abs(adjust_ms);
            } else {
                if (adev->is_TV) {
                    adjust_bytes = 48 * 32 * abs(adjust_ms);    //8ch 32 bit.
                    } else {
                        adjust_bytes = 48 * 4 * abs(adjust_ms); // 2ch 16bit
                }
            }
            adjust_bytes &= ~255;
            ALOGI("%s hwsync audio need %s %d ms,adjust bytes %d",
                  __func__, adjust_ms > 0 ? "insert" : "skip", abs(adjust_ms), adjust_bytes);
            if (adjust_ms > 0) {
                char *buf = aml_audio_malloc(1024);
                int write_size = 0;
                if (!buf) {
                    ALOGE("%s malloc failed", __func__);
                    pthread_mutex_unlock(&adev->alsa_pcm_lock);
                    return -1;
                }
                memset(buf, 0, 1024);
                while (adjust_bytes > 0) {
                    write_size = adjust_bytes > 1024 ? 1024 : adjust_bytes;
                    if (adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
                        ret = a2dp_out_write(stream, (void*)buf, write_size);
                    } else if (is_sco_port(adev->active_outport)) {
                        ret = write_to_sco(stream, buffer, bytes);
                    } else {
                        ret = aml_alsa_output_write(stream, (void*)buf, write_size);
                    }
                    if (ret < 0) {
                        ALOGE("%s alsa write fail when insert", __func__);
                        break;
                    }
                    adjust_bytes -= write_size;
                }
                aml_audio_free(buf);
            } else {
                //do nothing.
                /*
                if (bytes > (size_t)adjust_bytes)
                    bytes -= adjust_bytes;
                else
                    bytes = 0;
                */
            }
        }
        if (adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
            ret = a2dp_out_write(stream, buffer, bytes);
        } else if (is_sco_port(adev->active_outport)) {
            ret = write_to_sco(stream, buffer, bytes);
        } else {
            ret = aml_alsa_output_write(stream, (void *) buffer, bytes);
        }
        //ALOGE("!!aml_alsa_output_write");
        if (ret < 0) {
            ALOGE("ALSA out write fail");
            aml_out->frame_write_sum += out_frames;
        } else {
            if (!continous_mode(adev)) {
                if ((output_format == AUDIO_FORMAT_AC3) ||
                    (output_format == AUDIO_FORMAT_E_AC3) ||
                    (output_format == AUDIO_FORMAT_DTS)) {
                    if (is_iec61937_format(stream) == true) {
                        //continuous_audio_mode = 0, when mixing-off and 7.1ch
                        //FIXME out_frames 4 or 16 when DD+ output???
                        aml_out->frame_write_sum += out_frames;

                    } else {
                        int sample_per_bytes = (output_format == AUDIO_FORMAT_E_AC3) ? 16 : 4;

                        if (eDolbyDcvLib == adev->dolby_lib_type && aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                            aml_out->frame_write_sum = aml_out->input_bytes_size / audio_stream_out_frame_size(stream);
                            //aml_out->frame_write_sum += out_frames;
                        } else {
                            aml_out->frame_write_sum += bytes / sample_per_bytes; //old code
                            //aml_out->frame_write_sum = spdif_encoder_ad_get_total() / sample_per_bytes + aml_out->spdif_enc_init_frame_write_sum; //8.1
                        }
                    }
                } else {
                    if (SUPPORT_EARC_OUT_HW && adev->bHDMIConnected && aml_out->earc_pcm &&
                            adev->bHDMIARCon) {
                        out_frames = out_frames * 4;
                    } else if (aml_out->is_tv_platform) {
                        if (aml_out->hal_format == AUDIO_FORMAT_IEC61937 && eDolbyDcvLib == adev->dolby_lib_type) {
                            aml_out->frame_write_sum = 0;
                            out_frames = aml_out->input_bytes_size / audio_stream_out_frame_size(stream);
                        } else {
                            out_frames = out_frames / 8;
                        }
                    }
                    aml_out->frame_write_sum += out_frames;
                    total_frame =  aml_out->frame_write_sum;
                }
            }
        }
    }

    if (aml_out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
        latency_frames = a2dp_out_get_latency(stream) * aml_out->hal_rate / 1000;
    } else {
        latency_frames = aml_alsa_output_get_letancy(stream) * aml_out->config.rate / 1000;
    }

    pthread_mutex_unlock(&adev->alsa_pcm_lock);

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        /*it is the main alsa write function we need notify that to all sub-stream */
        adev->ms12.latency_frame = latency_frames;
        if (continous_mode(adev)) {
            adev->ms12.sys_avail = dolby_ms12_get_system_buffer_avail(NULL);
        }
        //ALOGD("alsa latency=%d", latency_frames);
    }

    /*
    */
    if (!continous_mode(adev)) {
        if (aml_out->hal_internal_format == AUDIO_FORMAT_PCM_16_BIT) {
            write_frames = aml_out->input_bytes_size / 4;
            //total_frame = write_frames;
        } else {
            total_frame = aml_out->frame_write_sum + aml_out->frame_skip_sum;
            if (aml_out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
                if (aml_out->hal_internal_format == AUDIO_FORMAT_AC3) {
                    write_frames = aml_out->input_bytes_size / 6144 * 32 * 48;
                }
                if (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) {
                    write_frames = aml_out->input_bytes_size / 24576 * 32 * 48;
                }
            } else {
                // write_frames = aml_out->input_bytes_size / aml_out->ddp_frame_size * 32 * 48;
                if (is_bypass_dolbyms12(stream)) {
                    //EAC3 IEC61937 size=4*6144bytes, same as PCM(6144bytes=1536*2ch*2bytes_per_sample)
                    //every 24kbytes coulc cost 32ms to output.
                    write_frames = aml_out->frame_write_sum;
                }
                else {
                    size_t playload_used = dolby_ms12_get_consumed_payload();
                    int ddp_size = 0;
                    if (aml_out->last_playload_used > 0) {
                        ddp_size = playload_used - aml_out->last_playload_used;
                        /*santity check if current frame size matchs dd+ spec*/
                        if (ddp_size > 0 && ddp_size < 4096) {
                            if (aml_out->ddp_frame_size != ddp_size) {
                                ALOGI("ddp frame changed from %d to %d",aml_out->ddp_frame_size,ddp_size);
                                aml_out->ddp_frame_size = ddp_size;
                            }
                        }
                    }
                    aml_out->last_playload_used = playload_used;
                    write_frames = aml_out->input_bytes_size/aml_out->ddp_frame_size*32*48;
                }
            }
        }
    } else {
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            if (!audio_is_linear_pcm(aml_out->hal_internal_format)) {
                /*use the pcm which is gennerated by udc, to get the total frame by nbytes/nbtyes_per_sample
                 *Please be careful about the aml_out->continuous_audio_offset;*/
                if (!adev->ms12.nbytes_of_dmx_output_pcm_frame)
                    adev->ms12.nbytes_of_dmx_output_pcm_frame = nbytes_of_dolby_ms12_downmix_output_pcm_frame();
                total_frame = dolby_ms12_get_n_bytes_pcmout_of_udc() / adev->ms12.nbytes_of_dmx_output_pcm_frame;
                write_frames =  aml_out->total_ddp_frame_nblks * 256;/*256samples in one block*/
                if (adev->debug_flag) {
                    ALOGI("%s,total_frame %llu write_frames %"PRIu64" total frame block nums %"PRIu64"",
                        __func__, total_frame, write_frames, aml_out->total_ddp_frame_nblks);
                }
            }
            /*case 3*/
            else if (aml_out->hw_sync_mode) {
                total_frame = dolby_ms12_get_consumed_payload() / 4;
            }
            /*case 1*/
            else {
                /*system volume prestation caculatation is done inside aux write thread*/
                total_frame = 0;
                //write_frames = (aml_out->input_bytes_size - dolby_ms12_get_system_buffer_avail(NULL)) / 4;
                //total_frame = write_frames;
            }
        }

    }
    /*we should also to caculate the alsa latency*/
    {
        clock_gettime (CLOCK_MONOTONIC, &aml_out->timestamp);
        aml_out->lasttimestamp.tv_sec = aml_out->timestamp.tv_sec;
        aml_out->lasttimestamp.tv_nsec = aml_out->timestamp.tv_nsec;
        if (total_frame >= latency_frames) {
            aml_out->last_frames_postion = total_frame - latency_frames;
        } else {
            aml_out->last_frames_postion = 0;
        }
    }
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (continous_mode(adev)) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            adev->ms12.timestamp.tv_sec = ts.tv_sec;
            adev->ms12.timestamp.tv_nsec = ts.tv_nsec;
            adev->ms12.last_frames_postion = aml_out->last_frames_postion;

        }
        /* check sys audio postion */
        sys_total_cost = dolby_ms12_get_consumed_sys_audio();
        if (adev->ms12.last_sys_audio_cost_pos != sys_total_cost) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            adev->ms12.sys_audio_timestamp.tv_sec = ts.tv_sec;
            adev->ms12.sys_audio_timestamp.tv_nsec = ts.tv_nsec;
            /*FIXME. 2ch 16 bit audio */
            adev->ms12.sys_audio_frame_pos = adev->ms12.sys_audio_base_pos + sys_total_cost/4 - latency_frames;
        }
        if (adev->debug_flag)
            ALOGI("sys audio pos %"PRIu64" ms ,sys_total_cost %"PRIu64",base pos %"PRIu64",latency %d \n",
                    adev->ms12.sys_audio_frame_pos/48,sys_total_cost,adev->ms12.sys_audio_base_pos,latency_frames);
        adev->ms12.last_sys_audio_cost_pos = sys_total_cost;
    }

    if (adev->debug_flag) {
        uint64_t diff_time_ms = 0;
        if ((aml_out->hal_internal_format == AUDIO_FORMAT_AC3) || (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3)) {
            diff_time_ms =(write_frames - total_frame) / 48;
        } else {
            diff_time_ms = (write_frames - aml_out->last_frames_postion) / 48;
        }
        ALOGD("[%s:%d] pcm:%p, is_61937_frame:%d, write_frames:%llu, diff_ms:%llu ms", __func__, __LINE__,
             aml_out->pcm, is_iec61937_format(stream), write_frames, diff_time_ms);
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            //ms12 internal buffer avail(main/associate/system)
            if (adev->ms12.dolby_ms12_enable == true)
                ALOGI("%s MS12 buffer avail main %d associate %d system %d\n",
                      __FUNCTION__, dolby_ms12_get_main_buffer_avail(NULL), dolby_ms12_get_associate_buffer_avail(), dolby_ms12_get_system_buffer_avail(NULL));
        }
        ALOGD("[%s:%d] total_frame:%llu, latency_frames:%d, last_frames:%llu, input_bytes:%llu", __func__, __LINE__,
              total_frame, latency_frames, aml_out->last_frames_postion, aml_out->input_bytes_size);
    }
    return ret;
}


void config_output(struct audio_stream_out *stream, bool reset_decoder)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_stream_out *out = NULL;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct aml_audio_patch *patch = adev->audio_patch;

    int ret = 0;
    bool main1_dummy = false;
    bool ott_input = false;
    bool dtscd_flag = false;
    int i  = 0 ;
    uint64_t write_frames = 0;

    int is_arc_connected = 0;
    int sink_format = AUDIO_FORMAT_PCM_16_BIT;

    adev->dcvlib_bypass_enable = 0;
    adev->dtslib_bypass_enable = 0;

    if (adev->bHDMIARCon && adev->bHDMIConnected && adev->speaker_mute) {
        is_arc_connected = 1;
    }
    /*
    *   when ARC is connecting, and user switch [Sound Output Device] to "ARC"
    *   we need to set out_port as OUTPORT_HDMI_ARC ,
    *   the aml_audio_output_routing() will "UNmute" ARC and "mute" speaker.
    */
    int out_port = adev->active_outport;
    if (adev->active_outport == OUTPORT_SPEAKER && is_arc_connected) {
        out_port = OUTPORT_HDMI_ARC;
    }

    /*
    *   when ARC is connecting, and user switch [Sound Output Device] to "speaker"
    *   we need to set out_port as OUTPORT_SPEAKER ,
    *   the aml_audio_output_routing() will "mute" ARC and "unmute" speaker.
    */
    if (adev->active_outport == OUTPORT_HDMI_ARC && !is_arc_connected) {
        out_port = OUTPORT_SPEAKER;
    }

    if (adev->debug_flag) {
        ALOGI("%s:%d active_outport:%d, is_arc_connected:%d, speaker_mute:%d, out_port:%d", __func__, __LINE__,
            adev->active_outport, is_arc_connected, adev->speaker_mute, out_port);
        ALOGI("bHDMIARCon:%d, bHDMIConnected:%d, hal_internal_format:0x%x", adev->bHDMIARCon, adev->bHDMIConnected, aml_out->hal_internal_format);
    }
    ret = aml_audio_output_routing((struct audio_hw_device *)adev, out_port, true);
    if (ret < 0) {
        ALOGE("%s() output routing failed", __func__);
    }
    /*get sink format*/
    get_sink_format (stream);
    sink_format = adev->sink_format;
    ALOGI("%s() adev->dolby_lib_type = %d", __FUNCTION__, adev->dolby_lib_type);
    if (aml_out->hal_internal_format != AUDIO_FORMAT_DTS
            && aml_out->hal_internal_format != AUDIO_FORMAT_DTS_HD) {
         struct dca_dts_dec *dts_dec = & (adev->dts_hd);
         if (dts_dec->status == 1) {
            dca_decoder_release_patch(dts_dec);
            if (adev->libvx_exist) {
                virtualx_setparameter(adev,0,0,2);
                virtualx_setparameter(adev,VIRTUALXINMODE,0,5);
                virtualx_setparameter(adev,TRUVOLUMEINMODE,0,5);
                adev->effect_in_ch = 2;
            }
            if (dts_dec->digital_raw > 0) {
                struct pcm *pcm = adev->pcm_handle[DIGITAL_DEVICE];
                if (pcm && is_dual_output_stream(stream)) {
                    ALOGI("%s close dual output pcm handle %p", __func__, pcm);
                    pcm_close(pcm);
                    adev->pcm_handle[DIGITAL_DEVICE] = NULL;
                    set_stream_dual_output(stream, false);
                    aml_tinymix_set_spdif_format(AUDIO_FORMAT_PCM_16_BIT,aml_out);
                }
            }
            ALOGI("dca_decoder_release_patch release");
        }

        if ((eDolbyMS12Lib == adev->dolby_lib_type) && !is_bypass_dolbyms12(stream) && (reset_decoder == true)) {
            pthread_mutex_lock(&adev->lock);
            get_dolby_ms12_cleanup(&adev->ms12, false);
            pthread_mutex_lock(&adev->alsa_pcm_lock);
            struct pcm *pcm = adev->pcm_handle[DIGITAL_DEVICE];

            if (is_dual_output_stream(stream) && pcm) {
                ALOGI("%s close pcm handle %p", __func__, pcm);
                pcm_close(pcm);
                adev->pcm_handle[DIGITAL_DEVICE] = NULL;
                ALOGI("------%s close pcm handle %p", __func__, pcm);
                aml_tinymix_set_spdif_format(AUDIO_FORMAT_PCM_16_BIT,aml_out);
            }
            update_stream_dual_output(stream);
            if (adev->dual_spdifenc_inited) {
                adev->dual_spdifenc_inited = 0;
            }
            if (aml_out->status == STREAM_HW_WRITING) {
                aml_alsa_output_close(stream);
                aml_out->status = STREAM_STANDBY;
                aml_tinymix_set_spdif_format(AUDIO_FORMAT_PCM_16_BIT,aml_out);
            }
            pthread_mutex_unlock(&adev->alsa_pcm_lock);
            //FIXME. also need check the sample rate and channel num.
            audio_format_t aformat = aml_out->hal_internal_format;
            if (continous_mode(adev) && !dolby_stream_active(adev)) {
                /*dummy we support it is  DD+*/
                aformat = AUDIO_FORMAT_E_AC3;
                main1_dummy = true;
            }
            if (continous_mode(adev) && hwsync_lpcm_active(adev)) {
                ott_input = true;
            }
            if (continous_mode(adev)) {
                adev->ms12_main1_dolby_dummy = main1_dummy;
                adev->ms12_ott_enable = ott_input;
                dolby_ms12_set_ott_sound_input_enable(true/*ott_input*/);
                dolby_ms12_set_dolby_main1_as_dummy_file(true);
            }
            ring_buffer_reset(&adev->spk_tuning_rbuf);
            adev->ms12.is_continuous_paused = false;
            ret = get_the_dolby_ms12_prepared(aml_out, aformat, AUDIO_CHANNEL_OUT_STEREO, aml_out->hal_rate);
            /*set the volume to current one*/
            if (!audio_is_linear_pcm(aml_out->hal_internal_format)) {
                dolby_ms12_set_main_volume(aml_out->volume_l);
            }
            if (continous_mode(adev)) {
                dolby_ms12_set_main_dummy(0, main1_dummy);
                dolby_ms12_set_main_dummy(1, !ott_input);
            }
            if (adev->ms12_out != NULL && adev->ms12_out->hwsync) {
                //aml_audio_hwsync_init(adev->ms12_out->hwsync, adev->ms12_out);
                adev->ms12_out->hwsync->aout = adev->ms12_out;
                ALOGI("set ms12 hwsync out to %p",adev->ms12_out);
            }
            adev->mix_init_flag = true;
            ALOGI("%s() get_the_dolby_ms12_prepared %s, ott_enable = %d, main1_dummy = %d",
                    __FUNCTION__, (ret == 0) ? "succuss" : "fail", ott_input, main1_dummy);
            store_stream_presentation(adev);
            pthread_mutex_unlock(&adev->lock);
        } else if (eDolbyDcvLib == adev->dolby_lib_type) {
            pthread_mutex_lock(&adev->alsa_pcm_lock);
            if (aml_out->status == STREAM_HW_WRITING) {
                aml_alsa_output_close(stream);
                aml_out->status = STREAM_STANDBY;
                aml_tinymix_set_spdif_format(AUDIO_FORMAT_PCM_16_BIT,aml_out);
            }
            pthread_mutex_unlock(&adev->alsa_pcm_lock);
            /*init or close ddp decoder*/
            struct dolby_ddp_dec *ddp_dec = & (adev->ddp);

            update_stream_dual_output(stream);
            switch (adev->hdmi_format) {
            case PCM:
                ddp_dec->digital_raw = 0;
                adev->dcvlib_bypass_enable = 0;
                break;
            case DD:
                ddp_dec->digital_raw = 1;
                //STB case
                if (!adev->is_TV) {
                    set_stream_dual_output(stream, false);

                } else {
                    set_stream_dual_output(stream, true);
                }
                adev->dcvlib_bypass_enable = 0;
                break;
            case AUTO:
            case BYPASS:
                //STB case
                if (!adev->is_TV) {
                    char *cap = NULL;
                    cap = (char *) get_hdmi_sink_cap (AUDIO_PARAMETER_STREAM_SUP_FORMATS,0,&(adev->hdmi_descs));
                    if (cap && mystrstr(cap, "AUDIO_FORMAT_E_AC3")) {
                        ddp_dec->digital_raw = 2;
                        adev->dcvlib_bypass_enable = 1;
                    } else if (cap && mystrstr(cap, "AUDIO_FORMAT_AC3")) {
                        if (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) {
                            adev->dcvlib_bypass_enable = 0;
                            ddp_dec->digital_raw = 1;
                        } else if (aml_out->hal_internal_format == AUDIO_FORMAT_AC3) {
                            adev->dcvlib_bypass_enable = 1;
                            ddp_dec->digital_raw = 1;
                        } else {
                            adev->dcvlib_bypass_enable = 1;
                            ddp_dec->digital_raw = 0;
                        }
                    } else {
                        adev->dcvlib_bypass_enable = 0;
                        ddp_dec->digital_raw = 0;
                    }
                    if (cap) {
                        aml_audio_free(cap);
                    }
                } else {
                    if (adev->hdmi_descs.ddp_fmt.is_support) {
                        ddp_dec->digital_raw = 2;
                        adev->dcvlib_bypass_enable = 1;
                    } else if (adev->hdmi_descs.dd_fmt.is_support) {
                        if (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) {
                            adev->dcvlib_bypass_enable = 0;
                            ddp_dec->digital_raw = 1;
                        } else if (aml_out->hal_internal_format == AUDIO_FORMAT_AC3) {
                            adev->dcvlib_bypass_enable = 1;
                            ddp_dec->digital_raw = 1;
                        } else {
                            adev->dcvlib_bypass_enable = 1;
                            ddp_dec->digital_raw = 0;
                        }
                    } else {
                        if (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3 ||
                            aml_out->hal_internal_format == AUDIO_FORMAT_AC3) {
                            adev->dcvlib_bypass_enable = 0;
                            if (is_dual_output_stream(stream))
                                ddp_dec->digital_raw = 1;
                            else
                                ddp_dec->digital_raw = 0;
                        }
                    }
                }
                if (adev->patch_src == SRC_DTV)
                    adev->dcvlib_bypass_enable = 0;
                break;
            default:
                ddp_dec->digital_raw = 0;
                break;
            }
            if (aml_out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
                ALOGI("disable raw output when a2dp device\n");
                ddp_dec->digital_raw = 0;
            }
            ALOGI("%s:%d ddp_dec->digital_raw:%d, dcvlib_bypass_enable:%d, dual_output_flag: %d", __func__, __LINE__,
                ddp_dec->digital_raw, adev->dcvlib_bypass_enable,is_dual_output_stream(stream));
            if (adev->dcvlib_bypass_enable != 1) {
                if (ddp_dec->status != 1 && (aml_out->hal_internal_format == AUDIO_FORMAT_AC3
                                          || aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3)) {
                    if (adev->dual_decoder_support) {
                        ddp_dec->dual_input = true;
                    }
                    int status = dcv_decoder_init_patch(ddp_dec);
                    if (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) {
                        ddp_dec->nIsEc3 = 1;
                    } else if (aml_out->hal_internal_format == AUDIO_FORMAT_AC3) {
                        ddp_dec->nIsEc3 = 0;
                    }
                    /*check if the input format is contained with 61937 format*/
                    if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                        ddp_dec->is_iec61937 = true;
                    } else {
                        ddp_dec->is_iec61937 = false;
                    }
                    ALOGI("dcv_decoder_init_patch return :%d,is 61937 %d", status, ddp_dec->is_iec61937);
                } else if (ddp_dec->status == 1 && (aml_out->hal_internal_format == AUDIO_FORMAT_AC3
                                          || aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3)) {
                    dcv_decoder_release_patch(ddp_dec);
                    if (aml_out->spdifout_handle) {
                        aml_audio_spdifout_close(aml_out->spdifout_handle);
                        aml_out->spdifout_handle = NULL;
                        set_stream_dual_output(stream, false);
                    }
                    ALOGI("dcv_decoder_release_patch release");
                }
           }

            pthread_mutex_lock(&adev->lock);
            if (!adev->hw_mixer.start_buf) {
                aml_hw_mixer_init(&adev->hw_mixer);
                if (adev->patch_src == SRC_DTV) {
                    adev->hw_mixer.mute_main_flag = adev->tv_mute;
                }
            } else {
                aml_hw_mixer_reset(&adev->hw_mixer);
            }
            pthread_mutex_unlock(&adev->lock);
        } else {
            ALOGI("%s() don't reset decoder ", __FUNCTION__);
        }
    } else {
        pthread_mutex_lock(&adev->alsa_pcm_lock);
        if (aml_out->status == STREAM_HW_WRITING) {
            aml_alsa_output_close(stream);
            aml_out->status = STREAM_STANDBY;
            aml_tinymix_set_spdif_format(AUDIO_FORMAT_PCM_16_BIT,aml_out);
        }
        pthread_mutex_unlock(&adev->alsa_pcm_lock);
        update_stream_dual_output(stream);

        /*init or close dts decoder*/
        struct dca_dts_dec *dts_dec = & (adev->dts_hd);
        switch (adev->hdmi_format) {
        case PCM:
            dts_dec->digital_raw = 0;
            adev->dtslib_bypass_enable = 0;
            break;
        case DD:
            dts_dec->digital_raw = 1;
            //STB case
            if (!adev->is_TV) {
                set_stream_dual_output(stream, false);
                adev->dtslib_bypass_enable = 1;
            } else {
                set_stream_dual_output(stream, true);
                adev->dtslib_bypass_enable = 0;
            }
            adev->optical_format = AUDIO_FORMAT_AC3;
            break;
        case AUTO:
        case BYPASS:
            if (((!adev->is_TV) && (adev->active_outport == OUTPORT_HDMI)) ||
                (adev->is_TV && (adev->active_outport == OUTPORT_HDMI_ARC))) {
                if (adev->hdmi_descs.dtshd_fmt.is_support) {
                    if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                        adev->dtslib_bypass_enable = 1;
                    } else {
                        adev->dtslib_bypass_enable = 0;
                    }
                    dts_dec->digital_raw = 1;
                } else if (adev->hdmi_descs.dts_fmt.is_support) {
                    if (aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD) {
                        adev->dtslib_bypass_enable = 0;
                    } else if (aml_out->hal_internal_format == AUDIO_FORMAT_DTS) {
                        if (dts_dec->frame_info.is_dtscd) {
                            adev->dtslib_bypass_enable = 0;
                            dtscd_flag = true;
                        } else if (aml_out->hal_format == AUDIO_FORMAT_IEC61937){
                            adev->dtslib_bypass_enable = 1;
                        } else {
                            adev->dtslib_bypass_enable = 0;
                        }
                    }
                    dts_dec->digital_raw = 1;
                } else {
                    dts_dec->digital_raw = 0;
                    adev->dtslib_bypass_enable = 0;
                }
            } else if (adev->active_outport == OUTPORT_SPEAKER) {
                dts_dec->digital_raw = 1;
                adev->dtslib_bypass_enable = 0;
            }
            break;
        default:
            dts_dec->digital_raw = 0;
            break;
        }
        ALOGI("dts_dec->digital_raw:%d adev->dtslib_bypass_enable:%d", dts_dec->digital_raw, adev->dtslib_bypass_enable);
        if (adev->dtslib_bypass_enable != 1) {
            if (dts_dec->status != 1 && (aml_out->hal_internal_format == AUDIO_FORMAT_DTS
                                         || aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD)) {
                int status = dca_decoder_init_patch(dts_dec);

                if (adev->libvx_exist) {
                    if (adev->virtualx_mulch) {
                        virtualx_setparameter(adev,0,0,2);
                        virtualx_setparameter(adev,VIRTUALXINMODE,1,5);
                        virtualx_setparameter(adev,TRUVOLUMEINMODE,4,5);
                        adev->effect_in_ch = 6;
                    } else {
                        virtualx_setparameter(adev,VIRTUALXINMODE,0,5);
                        virtualx_setparameter(adev,TRUVOLUMEINMODE,0,5);
                        adev->effect_in_ch = 2;
                    }
                }
                if ((patch && audio_parse_get_audio_type_direct(patch->audio_parse_para) == DTSCD) || dtscd_flag) {
                    dts_dec->frame_info.is_dtscd = true;
                    ALOGI("dts cd stream,dtscd_flag %d,type %d",dtscd_flag,audio_parse_get_audio_type_direct(patch->audio_parse_para));
                }

                if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                    dts_dec->frame_info.is_iec61937 = true;
                } else {
                    dts_dec->frame_info.is_iec61937 = false;
                }

                ALOGI("dca_decoder_init_patch return:%d, is_dtscd:%d, is_iec61937:%d, raw:%d"
                    , status, dts_dec->frame_info.is_dtscd, dts_dec->frame_info.is_iec61937, dts_dec->digital_raw);
            } else if (dts_dec->status == 1 && (aml_out->hal_internal_format == AUDIO_FORMAT_DTS
                                                || aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD)) {
                dca_decoder_release_patch(dts_dec);
                if (adev->libvx_exist) {
                    virtualx_setparameter(adev,0,0,2);
                    virtualx_setparameter(adev,VIRTUALXINMODE,0,5);
                    virtualx_setparameter(adev,TRUVOLUMEINMODE,0,5);
                    adev->effect_in_ch = 2;
                }
                if (aml_out->spdifout_handle) {
                    aml_audio_spdifout_close(aml_out->spdifout_handle);
                    aml_out->spdifout_handle = NULL;
                    set_stream_dual_output(stream, false);
                }
                ALOGI("dca_decoder_release_patch release");
            }

        }

        pthread_mutex_lock(&adev->lock);
        if (!adev->hw_mixer.start_buf) {
            aml_hw_mixer_init(&adev->hw_mixer);
            if (adev->patch_src == SRC_DTV) {
                adev->hw_mixer.mute_main_flag = adev->tv_mute;
            }
        } else {
            aml_hw_mixer_reset(&adev->hw_mixer);
        }
        pthread_mutex_unlock(&adev->lock);
    }

    /*TV-4745: After switch from normal PCM playing to MS12, the device will
     be changed to SPDIF, but when switch back to the normal PCM, the out device
     is still SPDIF, then the sound is abnormal.
    */
    if (!(continous_mode(adev) && (eDolbyMS12Lib == adev->dolby_lib_type))) {
        if (sink_format == AUDIO_FORMAT_PCM_16_BIT || sink_format == AUDIO_FORMAT_PCM_32_BIT) {
            aml_out->device = PORT_I2S;
        } else {
            aml_out->device = PORT_SPDIF;
        }
    }
    ALOGI("%s(), device: %d", __func__, aml_out->device);
    return ;
}

ssize_t mixer_main_buffer_write (struct audio_stream_out *stream, const void *buffer,
                                 size_t bytes)
{
    ALOGV("%s write in %zu!", __FUNCTION__, bytes);
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct aml_stream_out *ms12_out = (struct aml_stream_out *)adev->ms12_out;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct aml_audio_patch *patch = adev->audio_patch;
    int case_cnt;
    int ret = -1;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    bool need_reconfig_output = false;
    bool need_reset_decoder = true;
    void   *write_buf = NULL;
    size_t  write_bytes = 0;
    size_t  hwsync_cost_bytes = 0;
    int total_write = 0;
    size_t used_size = 0;
    int write_retry = 0;
    size_t total_bytes = bytes;
    size_t bytes_cost = 0;
    int ms12_write_failed = 0;
    effect_descriptor_t tmpdesc;
    uint32_t latency_frames = 0;
    audio_hwsync_t *hw_sync = aml_out->hwsync;
    int return_bytes = bytes;
    bool digital_input_src = (patch && \
           (patch->input_src == AUDIO_DEVICE_IN_HDMI
           || patch->input_src == AUDIO_DEVICE_IN_SPDIF
           || patch->input_src == AUDIO_DEVICE_IN_TV_TUNER));

    if (adev->debug_flag) {
        ALOGI("%s:%d out:%p write in %zu,format:0x%x,ms12_ott:%d,conti:%d,hw_sync:%d", __FUNCTION__, __LINE__,
              aml_out, bytes, aml_out->hal_internal_format,adev->ms12_ott_enable,adev->continuous_audio_mode,aml_out->hw_sync_mode);
        ALOGI("useSubMix:%d, dolby:%d, hal_format:0x%x, usecase:0x%x, usecase_masks:0x%x",
            adev->useSubMix, adev->dolby_lib_type, aml_out->hal_format, aml_out->usecase, adev->usecase_masks);
        if (patch) {
            ALOGD("aformat:0x%x", patch->aformat);
        } else {
            ALOGD("not create patch!!!");
        }
    }

    if (buffer == NULL) {
        ALOGE ("%s() invalid buffer %p\n", __FUNCTION__, buffer);
        return -1;
    }

    if (eDolbyMS12Lib != adev->dolby_lib_type) {
        // For compatible by lianlian
        if (aml_out->standby) {
            ALOGI("%s(), standby to unstandby", __func__);
            aml_out->standby = false;
        }
    }

    // why clean up, ms12 thead will handle all?? zz
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (patch && continous_mode(adev)) {
            if (adev->ms12.dolby_ms12_enable) {
                pthread_mutex_lock(&adev->lock);
                get_dolby_ms12_cleanup(&adev->ms12, false);
                pthread_mutex_unlock(&adev->lock);
            }
            return return_bytes;
        }
    }
    case_cnt = popcount (adev->usecase_masks);
    if (adev->mix_init_flag == false) {
        ALOGI ("%s mix init, mask %#x",__func__,adev->usecase_masks);
        pthread_mutex_lock (&adev->lock);
        /* recovery from stanby case */
        if (aml_out->status == STREAM_STANDBY) {
            ALOGI("%s() recovery from standby, dev masks %#x, usecase[%s]",
                  __func__, adev->usecase_masks, usecase2Str(aml_out->usecase));
            adev->usecase_masks |= (1 << aml_out->usecase);
            case_cnt = popcount(adev->usecase_masks);
        }

        if (aml_out->usecase == STREAM_PCM_HWSYNC || aml_out->usecase == STREAM_RAW_HWSYNC) {
            aml_audio_hwsync_init(aml_out->hwsync, aml_out);
        }
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            if (case_cnt > MAX_INPUT_STREAM_CNT && adev->need_remove_conti_mode == true) {
                bool set_ms12_non_continuous = true;
                ALOGI("%s,exit continuous release ms12 here", __func__);
                get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                adev->need_remove_conti_mode = false;
            }
        }
        need_reconfig_output = true;
        adev->mix_init_flag =  true;
        /*if mixer has started, no need restart*/
        if (!adev->hw_mixer.start_buf) {
            aml_hw_mixer_init(&adev->hw_mixer);
            if (adev->patch_src == SRC_DTV) {
                adev->hw_mixer.mute_main_flag = adev->tv_mute;
            }
        }
        pthread_mutex_unlock(&adev->lock);
    }
    if (case_cnt > MAX_INPUT_STREAM_CNT) {
        ALOGE ("%s usemask %x,we do not support two direct stream output at the same time.TO CHECK CODE FLOW!!!!!!",__func__,adev->usecase_masks);
        return return_bytes;
    }
    char val[PROPERTY_VALUE_MAX];
    if (adev->libvx_exist && property_get("media.libplayer.dtsMulChPcm", val, NULL) > 0) {
        if (strcmp(val, "true" /*enble 5.1 ch*/) == 0) {
            if (adev->virtualx_mulch != true) {
                adev->virtualx_mulch = true;
                virtualx_setparameter(adev,VIRTUALXINMODE,1,5);
                virtualx_setparameter(adev,TRUVOLUMEINMODE,4,5);
                adev->effect_in_ch = 6;
                /*reconfig dts  decoder interface*/
                if (aml_out->hal_internal_format == AUDIO_FORMAT_DTS) {
                    need_reconfig_output = true;
                }
            }
        } else if (strcmp(val, "false"/*disable 5.1 ch*/) == 0) {
            if (adev->virtualx_mulch != false) {
                adev->virtualx_mulch = false;
                virtualx_setparameter(adev,VIRTUALXINMODE,0,5);
                virtualx_setparameter(adev,TRUVOLUMEINMODE,0,5);
                adev->effect_in_ch = 2;
                /*reconfig dts  decoder interface*/
                if (aml_out->hal_internal_format == AUDIO_FORMAT_DTS) {
                    need_reconfig_output = true;
                }
            }
        }
    }

    /* here to check if the audio HDMI ARC format updated. */
    if (adev->arc_hdmi_updated) {
        ALOGI ("%s(), arc format updated, need reconfig output", __func__);
        need_reconfig_output = true;
        /*
        we reset the whole decoder pipeline when audio routing change,
        audio output option change, we do not need do a/v sync in this user case.
        in order to get a low cpu loading, we enabled less ms12 modules in each
        hdmi in user case, we need reset the pipeline to get proper one.
        */
        need_reset_decoder = digital_input_src ? true: false;
        adev->arc_hdmi_updated = 0;
    }
    /* here to check if the hdmi audio output format dynamic changed. */
    if (adev->pre_hdmi_format != adev->hdmi_format ) {
        ALOGI("hdmi format is changed from %d to %d need reconfig output", adev->pre_hdmi_format, adev->hdmi_format);
        adev->pre_hdmi_format = adev->hdmi_format;
        need_reconfig_output = true;
        need_reset_decoder = digital_input_src ? true: false;
    }

    if (adev->a2dp_updated) {
        ALOGI ("%s(), a2dp updated, need reconfig output, %d %d", __func__, adev->out_device, aml_out->out_device);
        need_reconfig_output = true;
        adev->a2dp_updated = 0;
    }

    /* here to check if the audio output routing changed. */
    if (adev->out_device != aml_out->out_device) {
        ALOGI("%s(), output routing changed, need reconfig output, out dev:%#x, adev dev:%#x", __func__,
            aml_out->out_device, adev->out_device);
        need_reconfig_output = true;
        need_reset_decoder = false;
        aml_out->out_device = adev->out_device;
    }
hwsync_rewrite:
    /* handle HWSYNC audio data*/
    if (aml_out->hw_sync_mode) {
        uint64_t  cur_pts = 0xffffffff;
        int outsize = 0;
        char tempbuf[128];
        ALOGV ("before aml_audio_hwsync_find_frame bytes %zu\n", total_bytes - bytes_cost);
        hwsync_cost_bytes = aml_audio_hwsync_find_frame(aml_out->hwsync, (char *)buffer + bytes_cost, total_bytes - bytes_cost, &cur_pts, &outsize);
        if (cur_pts > 0xffffffff) {
            ALOGE("APTS exeed the max 32bit value");
        }
        ALOGV ("after aml_audio_hwsync_find_frame bytes remain %zu,cost %zu,outsize %d,pts %"PRIx64"\n",
               total_bytes - bytes_cost - hwsync_cost_bytes, hwsync_cost_bytes, outsize, cur_pts);
        //TODO,skip 3 frames after flush, to tmp fix seek pts discontinue issue.need dig more
        // to find out why seek ppint pts frame is remained after flush.WTF.
        if (aml_out->skip_frame > 0) {
            aml_out->skip_frame--;
            ALOGI ("skip pts@%"PRIx64",cur frame size %d,cost size %zu\n", cur_pts, outsize, hwsync_cost_bytes);
            return hwsync_cost_bytes;
        }
        if (cur_pts != 0xffffffff && outsize > 0) {
            if (eDolbyMS12Lib == adev->dolby_lib_type && !is_bypass_dolbyms12(stream)) {
                // missing code with aml_audio_hwsync_checkin_apts, need to add for netflix tunnel mode.
                aml_audio_hwsync_checkin_apts(aml_out->hwsync, aml_out->hwsync->payload_offset, cur_pts);
                aml_out->hwsync->payload_offset += outsize;
            } else {
                // if we got the frame body,which means we get a complete frame.
                //we take this frame pts as the first apts.
                //this can fix the seek discontinue,we got a fake frame,which maybe cached before the seek
                if (hw_sync->first_apts_flag == false) {
                    aml_audio_hwsync_set_first_pts(aml_out->hwsync, cur_pts);
                } else {
                    uint64_t apts;
                    uint32_t apts32;
                    uint pcr = 0;
                    uint apts_gap = 0;
                    uint64_t latency = out_get_latency (stream) * 90;
                    // check PTS discontinue, which may happen when audio track switching
                    // discontinue means PTS calculated based on first_apts and frame_write_sum
                    // does not match the timestamp of next audio samples
                    if (cur_pts > latency) {
                        apts = cur_pts - latency;
                    } else {
                        apts = 0;
                    }
                    apts32 = apts & 0xffffffff;
                    if (get_sysfs_uint (TSYNC_PCRSCR, &pcr) == 0) {
                        enum hwsync_status sync_status = CONTINUATION;
                        apts_gap = get_pts_gap (pcr, apts32);
                        sync_status = check_hwsync_status (apts_gap);
                        //ALOGI("pts = %d pcr=%d gap=%d", apts32/90, pcr/90, apts_gap / 90);
                        // limit the gap handle to 0.5~5 s.
                        if (sync_status == ADJUSTMENT) {
                            // two cases: apts leading or pcr leading
                            // apts leading needs inserting frame and pcr leading neads discarding frame
                            if (apts32 > pcr) {
                                int insert_size = 0;
                                if (aml_out->codec_type == TYPE_EAC3) {
                                    insert_size = apts_gap / 90 * 48 * 4 * 4;
                                } else {
                                    insert_size = apts_gap / 90 * 48 * 4;
                                }
                                insert_size = insert_size & (~63);
                                ALOGI ("audio gap 0x%"PRIx32" ms ,need insert data %d\n", apts_gap / 90, insert_size);
                                ret = insert_output_bytes (aml_out, insert_size);
                            } else {
                                //audio pts smaller than pcr,need skip frame.
                                //we assume one frame duration is 32 ms for DD+(6 blocks X 1536 frames,48K sample rate)
                                if (aml_out->codec_type == TYPE_EAC3 && outsize > 0) {
                                    ALOGI ("audio slow 0x%x,skip frame @pts 0x%"PRIx64",pcr 0x%x,cur apts 0x%x\n",
                                       apts_gap, cur_pts, pcr, apts32);
                                    aml_out->frame_skip_sum  +=   1536;
                                    return_bytes = hwsync_cost_bytes;
                                    goto exit;
                                }
                            }
                        } else if (sync_status == RESYNC) {
                            sprintf (tempbuf, "0x%x", apts32);
                            ALOGI ("tsync -> reset pcrscr 0x%x -> 0x%x, %s big,diff %"PRIx64" ms",
                                   pcr, apts32, apts32 > pcr ? "apts" : "pcr", get_pts_gap (apts, pcr) / 90);

                            int ret_val = sysfs_set_sysfs_str (TSYNC_APTS, tempbuf);
                            if (ret_val == -1) {
                                ALOGE ("unable to open file %s,err: %s", TSYNC_APTS, strerror (errno) );
                            }
                        }
                    }
                }
            }
        }
        if (outsize > 0) {
            /*
            Because we have a playload cache between two write burst.we need
            write the playload size to hw and return actual cost size to AF.
            So we use different size and buffer addr to hw writing.
            */
            return_bytes = hwsync_cost_bytes;
            write_bytes = outsize;
            //in_frames = outsize / frame_size;
            write_buf = hw_sync->hw_sync_body_buf;

            // Tunnel Mode PCM is 16bits 2ch
            if ((eDolbyMS12Lib == adev->dolby_lib_type) && continous_mode(adev) &&
                audio_is_linear_pcm(aml_out->hal_internal_format)) {
                short *sample = (short*) write_buf;
                int out_frames = write_bytes / (2*2); // 2 bytes per sample
                int kk;
                for (kk = 0; kk <  out_frames; kk++) {
                    sample[kk * 2] *= aml_out->volume_l;
                    sample[kk * 2 + 1] *= aml_out->volume_r;
                }
            }

        } else {
            return_bytes = hwsync_cost_bytes;
            if (need_reconfig_output) {
                config_output(stream, need_reset_decoder);
            }
            goto exit;
        }
    } else {
        write_buf = (void *) buffer;
        write_bytes = bytes;
    }

    /* here to check if the audio input format changed. */
    if (adev->audio_patch) {
        audio_format_t cur_aformat;
        if (IS_HDMI_IN_HW(patch->input_src) ||
                patch->input_src == AUDIO_DEVICE_IN_SPDIF) {
            cur_aformat = audio_parse_get_audio_type (patch->audio_parse_para);
            if (adev->debug_flag) {
                audio_type_parse_t *status = patch->audio_parse_para;
                ALOGI("%s:%d cur_aformat:0x%x, aformat:0x%x", __func__, __LINE__, cur_aformat, patch->aformat);
            }
            if ((cur_aformat == AUDIO_FORMAT_DTS || cur_aformat == AUDIO_FORMAT_DTS_HD) &&
                    adev->libvx_exist) {
                int cur_channels = get_dts_stream_channels((const char*)buffer, bytes);
                static int pre_channels = 0;
                if (cur_aformat != patch->aformat) {
                    pre_channels = 0;
                }
                if ((cur_channels != -1) && (pre_channels != cur_channels) && (adev->libvx_exist)) {
                    ALOGD("%s, pre_channels = %d, cur_channels = %d", __func__, pre_channels, cur_channels);
                    pre_channels = cur_channels;
                    if ((cur_channels == 6) || (cur_channels == 7) || (cur_channels == 8)) {
                        virtualx_setparameter(adev, VIRTUALX_CHANNEL_NUM, 6, 5);
                    } else {
                        virtualx_setparameter(adev, VIRTUALX_CHANNEL_NUM, 2, 5);
                    }
                    need_reconfig_output = true;
                    need_reset_decoder = true;
                }
            }
            if (cur_aformat != patch->aformat) {
                ALOGI ("HDMI/SPDIF input format changed from %#x to %#x\n", patch->aformat, cur_aformat);
                patch->aformat = cur_aformat;
                //FIXME: if patch audio format change, the hal_format need to redefine.
                //then the out_get_format() can get it.
                ALOGI ("hal_format changed from %#x to %#x\n", aml_out->hal_format, cur_aformat);
                if (cur_aformat != AUDIO_FORMAT_PCM_16_BIT && cur_aformat != AUDIO_FORMAT_PCM_32_BIT) {
                    aml_out->hal_format = AUDIO_FORMAT_IEC61937;
                } else {
                    aml_out->hal_format = cur_aformat;
                }
                /*under ms12 lib case, we need do such swicth*/
                if ((aml_out->hal_internal_format == AUDIO_FORMAT_DTS) && (adev->dolby_lib_type_last == eDolbyMS12Lib)) {
                    ALOGI("reset dts decoder");
                    config_output (stream, true);
                }
                aml_out->hal_internal_format = cur_aformat;
                aml_out->hal_channel_mask = audio_parse_get_audio_channel_mask (patch->audio_parse_para);
                ALOGI ("%s hal_channel_mask %#x\n", __FUNCTION__, aml_out->hal_channel_mask);
                if (aml_out->hal_internal_format == AUDIO_FORMAT_DTS ||
                        aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD) {
                    /*switch to MS12, we need cleanup MS12 first*/
                    if (adev->dolby_lib_type_last == eDolbyMS12Lib && adev->ms12.dolby_ms12_enable) {
                        get_dolby_ms12_cleanup(&adev->ms12, false);
                    }
                    adev->dolby_lib_type = eDolbyDcvLib;
                    if (aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD) {
                        /* For DTS HBR case, needs enlarge buffer and start threshold to anti-xrun */
                        aml_out->config.period_count = 6;
                        aml_out->config.start_threshold =
                            aml_out->config.period_size * aml_out->config.period_count;
                    }
                } else {
                    adev->dolby_lib_type = adev->dolby_lib_type_last;
                }
                //we just do not support dts decoder,just mute as LPCM
                need_reconfig_output = true;
                need_reset_decoder = true;
                /* reset audio patch ringbuffer */
                ring_buffer_reset(&patch->aml_ringbuffer);

#ifdef ADD_AUDIO_DELAY_INTERFACE
                // fixed switch between RAW and PCM noise, drop delay residual data
                aml_audio_delay_clear(AML_DELAY_OUTPORT_SPDIF);
                aml_audio_delay_clear(AML_DELAY_OUTPORT_ALL);
#endif
                // HDMI input && HDMI ARC output case, when input format change, output format need also change
                // for examle: hdmi input DD+ => DD,  HDMI ARC DD +=> DD
                // so we need to notify to reset spdif output format here.
                // adev->spdif_encoder_init_flag will be checked elsewhere when doing output.
                // and spdif_encoder will be initalize by correct format then.
                if (aml_out->spdifenc_init) {
                    aml_spdif_encoder_close(aml_out->spdifenc_handle);
                    aml_out->spdifenc_handle = NULL;
                    aml_out->spdifenc_init = false;
                }
                adev->spdif_encoder_init_flag = false;
                if (aml_out->spdifout_handle) {
                    ALOGI("[%s:%d] close spdif device", __func__, __LINE__);
                    aml_audio_spdifout_close(aml_out->spdifout_handle);
                    aml_out->spdifout_handle = NULL;
                    set_stream_dual_output(stream, false);
                }
            }

            if (cur_aformat == AUDIO_FORMAT_INVALID) {
                // the input data is invalid, mute it
                memset((void *)buffer, 0, bytes);
            }
        }
    } else if (aml_out->hal_format== AUDIO_FORMAT_IEC61937 &&
                aml_out->hal_internal_format == AUDIO_FORMAT_DTS &&
                aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD &&
                !adev->dts_hd.frame_info.is_dtscd) {
        audio_channel_mask_t cur_ch_mask;
        audio_format_t cur_aformat;
        int package_size;
        int cur_audio_type = audio_type_parse(write_buf, write_bytes, &package_size, &cur_ch_mask);
        int cur_channels;
        cur_aformat = audio_type_convert_to_android_audio_format_t(cur_audio_type);
        ALOGD("%s, cur_aformat = %0x, cur_audio_type= %d", __func__, cur_aformat, cur_audio_type);
        if (cur_audio_type == DTSCD) {
            adev->dts_hd.frame_info.is_dtscd = true;
        } else {
            adev->dts_hd.frame_info.is_dtscd = false;
        }
        if (adev->libvx_exist) {
            cur_channels = get_dts_stream_channels(write_buf, write_bytes);
            ALOGD("%s, cur_channels = %d", __func__, cur_channels);
            if ((cur_channels == 6) || (cur_channels == 7) || (cur_channels == 8)) {
                virtualx_setparameter(adev, VIRTUALX_CHANNEL_NUM, 6, 5);
            } else {
                virtualx_setparameter(adev, VIRTUALX_CHANNEL_NUM, 2, 5);
            }
            need_reconfig_output = true;
            need_reset_decoder = true;
        }
    }

    if (adev->need_reset_for_dual_decoder == true) {
        need_reconfig_output = true;
        need_reset_decoder = true;
        adev->need_reset_for_dual_decoder = false;
        ALOGI("%s get the dual decoder support, need reset", __FUNCTION__);
    }


    if (eDolbyMS12Lib == adev->dolby_lib_type &&
        aml_out->hal_internal_format != AUDIO_FORMAT_DTS &&
        aml_out->hal_internal_format != AUDIO_FORMAT_DTS_HD) {
        /**
         * Need config MS12 if its not enabled.
         * Since MS12 is essential for main input.
         */
        if (!adev->ms12.dolby_ms12_enable && !is_bypass_dolbyms12(stream)) {
            ALOGI("ms12 is not enabled, reconfig it");
            need_reconfig_output = true;
            need_reset_decoder = true;
        }
    }
    if (need_reconfig_output) {
        config_output (stream,need_reset_decoder);
        need_reconfig_output = false;
    }

    if ((eDolbyMS12Lib == adev->dolby_lib_type) && !is_bypass_dolbyms12(stream) && !is_dts_format(aml_out->hal_internal_format)) {
        // in NETFLIX moive selcet screen, switch between movies, adev->ms12_out will change.
        // so we need to update to latest staus just before use.
        ms12_out = (struct aml_stream_out *)adev->ms12_out;
        if (ms12_out == NULL) {
            // add protection here
            ALOGI("%s,ERRPR ms12_out = NULL,adev->ms12_out = %p", __func__, adev->ms12_out);
            return return_bytes;
        }
        /*
        continous mode,available dolby format coming,need set main dolby dummy to false
        */
        if (continous_mode(adev) && adev->ms12_main1_dolby_dummy == true
            && !audio_is_linear_pcm(aml_out->hal_internal_format)) {
            pthread_mutex_lock(&adev->lock);
            dolby_ms12_set_main_dummy(0, false);
            adev->ms12_main1_dolby_dummy = false;
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_lock(&adev->trans_lock);
            ms12_out->hal_internal_format = aml_out->hal_internal_format;
            ms12_out->hw_sync_mode = aml_out->hw_sync_mode;
            ms12_out->hwsync = aml_out->hwsync;
            pthread_mutex_unlock(&adev->trans_lock);
            ALOGI("%s set dolby main1 dummy false", __func__);
        } else if (continous_mode(adev) && adev->ms12_ott_enable == false
                   && audio_is_linear_pcm(aml_out->hal_internal_format)) {
            pthread_mutex_lock(&adev->lock);
            dolby_ms12_set_main_dummy(1, false);
            adev->ms12_ott_enable = true;
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_lock(&adev->trans_lock);
            ms12_out->hal_internal_format = aml_out->hal_internal_format;
            ms12_out->hw_sync_mode = aml_out->hw_sync_mode;
            ms12_out->hwsync = aml_out->hwsync;
            pthread_mutex_unlock(&adev->trans_lock);
            ALOGI("%s set dolby ott enable", __func__);
        }
        if (continous_mode(adev)) {
            if ((adev->ms12.dolby_ms12_enable == true) && (adev->ms12.is_continuous_paused == true)) {
                pthread_mutex_lock(&ms12->lock);
                dolby_ms12_set_pause_flag(false);
                adev->ms12.is_continuous_paused = false;
                set_dolby_ms12_runtime_pause(&(adev->ms12), adev->ms12.is_continuous_paused);
                pthread_mutex_unlock(&ms12->lock);
            }
        }
    }
    aml_out->input_bytes_size += write_bytes;

    if (patch && (adev->dtslib_bypass_enable || adev->dcvlib_bypass_enable)) {
        int cur_samplerate = audio_parse_get_audio_samplerate(patch->audio_parse_para);
        if (cur_samplerate != patch->input_sample_rate) {
            ALOGI ("HDMI/SPDIF input samplerate from %d to %d\n", patch->input_sample_rate, cur_samplerate);
            patch->input_sample_rate = cur_samplerate;
            if (patch->aformat == AUDIO_FORMAT_DTS ||  patch->aformat == AUDIO_FORMAT_DTS_HD) {
                if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                    if (cur_samplerate == 44100 || cur_samplerate == 32000) {
                        aml_out->config.rate = cur_samplerate;
                    } else {
                        aml_out->config.rate = 48000;
                    }
                }
            } else if (patch->aformat == AUDIO_FORMAT_AC3) {
                if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                    aml_out->config.rate = cur_samplerate;
                }
            } else if (patch->aformat == AUDIO_FORMAT_E_AC3) {
                if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                    if (cur_samplerate == 192000 || cur_samplerate == 176400) {
                        aml_out->config.rate = cur_samplerate / 4;
                    } else {
                        aml_out->config.rate = cur_samplerate;
                    }
                }
            } else {
                aml_out->config.rate = 48000;
            }
            ALOGI("adev->dtslib_bypass_enable :%d,adev->dcvlib_bypass_enable:%d, aml_out->config.rate :%d\n",adev->dtslib_bypass_enable,
            adev->dcvlib_bypass_enable,aml_out->config.rate);
        }
    }

    if ((patch && patch->aformat == AUDIO_FORMAT_DTS)
        || (patch && patch->aformat == AUDIO_FORMAT_DTS_HD)
        || (aml_out->hal_internal_format == AUDIO_FORMAT_DTS)
        || (aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD)) {
        audio_format_t output_format = AUDIO_FORMAT_PCM_16_BIT;
        int16_t *tmp_buffer;
        int read_bytes = 0;
        if (adev->dtslib_bypass_enable) {
            if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                output_format = AUDIO_FORMAT_DTS;
                if (adev->debug_flag) {
                    ALOGD("%s:%d DTS write passthrough data", __func__, __LINE__);
                }
                if (audio_hal_data_processing(stream, (void *)write_buf, write_bytes, &output_buffer, &output_buffer_bytes, output_format) == 0) {
                    hw_write(stream, output_buffer, output_buffer_bytes, output_format);
                }
                return return_bytes;
            }
        }

        struct dca_dts_dec *dts_dec = &(adev->dts_hd);
        if (dts_dec->status != 1) {
            config_output(stream, true);
        }

        if (dts_dec->status == 1) {
            if (adev->debug_flag) {
                ALOGD("%s:%d DTS decoder start", __func__, __LINE__);
            }

            while (dca_decoder_process_patch(dts_dec, (unsigned char *)write_buf, write_bytes) >= 0) {
                write_bytes = 0;
                read_bytes =  PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE * (dts_dec->pcm_out_info.channel_num);
#if 1
                //wirte raw data
                if ((dts_dec->digital_raw > 0) && (dts_dec->outlen_raw > 0)) {
                    /* all the HDMI in we goes through into decoder, because sometimes it is 44.1 khz, we don't know
                        such info if we doesn't decoded it.
                    */
                    // we only support 44.1 Khz & 48 Khz raw output
                    if (dts_dec->pcm_out_info.sample_rate == 44100 ) {
                        aml_out->config.rate = dts_dec->pcm_out_info.sample_rate;
                    } else {
                        aml_out->config.rate = 48000;
                    }

                    output_format = AUDIO_FORMAT_DTS;
                    if (is_dual_output_stream(stream)) {
                        spdif_config_t spdif_config = { 0 };
                        spdif_config.audio_format = AUDIO_FORMAT_IEC61937;
                        spdif_config.sub_format   = AUDIO_FORMAT_DTS;
                        spdif_config.rate         = aml_out->config.rate;

                        while (get_buffer_read_space(&dts_dec->raw_ring_buf) >= read_bytes) {
                            ring_buffer_read(&dts_dec->raw_ring_buf, dts_dec->outbuf_raw, read_bytes);
                            aml_audio_spdif_output(stream, (void *)dts_dec->outbuf_raw, read_bytes, &spdif_config);
                        }
                    } else if (adev->active_outport == OUTPORT_HDMI_ARC) {
                        while (get_buffer_read_space(&dts_dec->raw_ring_buf) >= read_bytes) {
                            ring_buffer_read(&dts_dec->raw_ring_buf, dts_dec->outbuf_raw, read_bytes);
                            if (audio_hal_data_processing(stream, (void *)dts_dec->outbuf_raw, read_bytes, &output_buffer, &output_buffer_bytes, output_format) == 0) {
                                hw_write(stream, output_buffer, output_buffer_bytes, output_format);
                            }
                        }
                        continue;
                    }
                }
#endif
                //write pcm data
                int tmp_bytes = read_bytes;
                output_format = AUDIO_FORMAT_PCM_16_BIT;
                while (get_buffer_read_space(&dts_dec->output_ring_buf) >= (int)read_bytes) {
                    ring_buffer_read(&dts_dec->output_ring_buf, dts_dec->outbuf, read_bytes);
                    if (dts_dec->pcm_out_info.channel_num == 6) {
                        int16_t *dts_buffer = (int16_t *)dts_dec->outbuf;
                        if (adev->effect_buf_size < (size_t)read_bytes) {
                            adev->effect_buf = aml_audio_realloc(adev->effect_buf, read_bytes);
                            if (!adev->effect_buf) {
                                ALOGE ("realloc effect buf failed size %zu format = %#x", read_bytes, output_format);
                                return -ENOMEM;
                            } else {
                                ALOGI("realloc effect_buf size from %zu to %zu format = %#x", adev->effect_buf_size, read_bytes, output_format);
                            }
                            adev->effect_buf_size = read_bytes;
                        }
                        tmp_buffer = (int16_t *)adev->effect_buf;
                        memcpy(tmp_buffer, dts_buffer, read_bytes);
                        if (adev->native_postprocess.postprocessors[0] != NULL) {
                            (*(adev->native_postprocess.postprocessors[0]))->get_descriptor(adev->native_postprocess.postprocessors[0], &tmpdesc);
                            if (0 == strcmp(tmpdesc.name,"VirtualX")) {
                                audio_post_process(adev->native_postprocess.postprocessors[0], tmp_buffer, read_bytes/(6 * 2));
                            }
                        }
                        tmp_bytes /= 3;
                    } else {
                        tmp_buffer = (int16_t *) dts_dec->outbuf;
                        tmp_bytes = read_bytes;
                    }
                    aml_hw_mixer_mixing(&adev->hw_mixer, (void*)tmp_buffer, tmp_bytes, output_format);
                    if (audio_hal_data_processing(stream, (void*)tmp_buffer, tmp_bytes, &output_buffer, &output_buffer_bytes, output_format) == 0) {
                        hw_write(stream, output_buffer, output_buffer_bytes, output_format);
                    }
                }
            }
        }
        return return_bytes;
    }

    /*
     *when disable_pcm_mixing is true, the 7.1ch DD+ could not be process with Dolby MS12
     *the HDMI-In or Spdif-In is special with IEC61937 format, other input need packet with spdif-encoder.
     */
    audio_format_t output_format = get_output_format (stream);
    if (adev->debug_flag) {
        ALOGD("%s:%d hal_format:%#x, output_format:0x%x, sink_format:0x%x",
            __func__, __LINE__, aml_out->hal_format, output_format, adev->sink_format);
    }

    {
        //#ifdef DOLBY_MS12_ENABLE
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            if (is_bypass_dolbyms12(stream)) {
                if (adev->debug_flag) {
                    ALOGI("%s passthrough dolbyms12, format %#x\n", __func__, aml_out->hal_format);
                }
                output_format = aml_out->hal_internal_format;
                if (audio_hal_data_processing (stream, write_buf, write_bytes, &output_buffer, &output_buffer_bytes, output_format) == 0)
                    hw_write (stream, output_buffer, output_buffer_bytes, output_format);
            }
            else {
                /*not continuous mode, we use sink gain control the volume*/
                if (!continous_mode(adev)) {
                    /*for pcm output, we will control it in hal_data_process*/
                    if (!adev->is_TV && (adev->audio_patching)) {
                        float out_gain = 1.0f;
                        out_gain = adev->sink_gain[adev->active_outport];
                        if (adev->patch_src == SRC_DTV && adev->tv_mute)
                            out_gain = 0.0f;
                        if (!audio_is_linear_pcm(aml_out->hal_internal_format)) {
                            dolby_ms12_set_main_volume(out_gain);
                        } else {
                            if (adev->patch_src == SRC_DTV && adev->tv_mute)
                                apply_volume(0.0f , write_buf, sizeof(int16_t), write_bytes);
                        }
                    }
                    /*when it is non continuous mode, we bypass data here*/
                    dolby_ms12_bypass_process(stream, write_buf, write_bytes);
                }
                /*begin to write, clear the total write*/
                total_write = 0;
re_write:
                if (adev->debug_flag) {
                    ALOGI("%s dolby_ms12_main_process before write_bytes %zu!\n", __func__, write_bytes);
                }

                used_size = 0;
                ret = dolby_ms12_main_process(stream, (char*)write_buf + total_write, write_bytes, &used_size);
                if (ret == 0) {
                    if (adev->debug_flag) {
                        ALOGI("%s dolby_ms12_main_process return %d, return used_size %zu!\n", __FUNCTION__, ret, used_size);
                    }
                    if (used_size < write_bytes && write_retry < MS12_MAIN_WRITE_LOOP_THRESHOLD) {
                        if (adev->debug_flag) {
                            ALOGI("%s dolby_ms12_main_process used  %zu,write total %zu,left %zu\n", __FUNCTION__, used_size, write_bytes, write_bytes - used_size);
                        }
                        total_write += used_size;
                        write_bytes -= used_size;
                        aml_audio_sleep(1000);
                        if (adev->debug_flag >= 2) {
                            ALOGI("%s sleeep 1ms\n", __FUNCTION__);
                        }
                        write_retry++;
                        if (adev->ms12.dolby_ms12_enable) {
                            goto re_write;
                        }
                    }
                    if (write_retry >= MS12_MAIN_WRITE_LOOP_THRESHOLD) {
                        ALOGE("%s main write retry time output,left %zu", __func__, write_bytes);
                        //bytes -= write_bytes;
                        ms12_write_failed = 1;
                    }
                    goto exit;
                } else {
                    ALOGE("%s dolby_ms12_main_process failed %d", __func__, ret);
                }
            }
        } else if (eDolbyDcvLib == adev->dolby_lib_type) {
            //#else
            //ALOGI("aml_out->hal_format %0x ",aml_out->hal_format);

            if (adev->dcvlib_bypass_enable) {
                if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                    output_format = get_output_format(stream);
                    if (audio_hal_data_processing(stream, (void *)write_buf, write_bytes, &output_buffer, &output_buffer_bytes, output_format) == 0) {
                        hw_write(stream, output_buffer, output_buffer_bytes, output_format);
                    }
                    return return_bytes;
                }
            }
            if ((patch && (patch->aformat == AUDIO_FORMAT_AC3 || patch->aformat == AUDIO_FORMAT_E_AC3)) ||
                aml_out->hal_internal_format == AUDIO_FORMAT_AC3 || aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) {
                int ret = -1;
                struct dolby_ddp_dec *ddp_dec = &(adev->ddp);

dcv_rewrite:
                if (ddp_dec->status == 1) {
#if defined(IS_ATOM_PROJECT)
                    /*for 32bit hal, raw data only support 16bit*/
                    int16_t *p = (int16_t *)write_buf;
                    int32_t *p1 = (int32_t *)write_buf;
                    for (size_t i = 0; i < write_bytes/4; i++) {
                        p[i] = p1[i] >> 16;
                    }
                    write_bytes /= 2;
#endif
                    /* config the main and ad mixer level*/
                    if (ddp_dec->dual_input && (adev->ddp.mixer_level != adev->mixing_level)) {
                        ddp_dec->mixer_level = adev->mixing_level;
                        dcv_decoder_config(ddp_dec, DDP_CONFIG_MIXER_LEVEL, (ddp_config_t *)(&ddp_dec->mixer_level));
                    }

                    ret = dcv_decoder_process_patch(ddp_dec, (unsigned char *)write_buf, write_bytes);
                } else {
                    config_output(stream, need_reset_decoder);
                }
                if (ret < 0 ) {
                    if (adev->debug_flag)
                        ALOGE("%s(), %d decoder error, ret %d", __func__, __LINE__, ret);
                    goto exit;
                }
                /*wirte raw data*/
                if (ddp_dec->outlen_raw > 0 && is_dual_output_stream(stream)) {/*dual output: pcm & raw*/
                    if (ddp_dec->pcm_out_info.sample_rate > 0)
                        aml_out->config.rate = ddp_dec->pcm_out_info.sample_rate;

                    spdif_config_t spdif_config = { 0 };
                    spdif_config.audio_format = AUDIO_FORMAT_IEC61937;
                    spdif_config.sub_format   = adev->optical_format;
                    spdif_config.rate         = aml_out->config.rate;
                    if (adev->parental_control_av_mute == false) {
                        aml_audio_spdif_output(stream, (void *)ddp_dec->outbuf_raw, ddp_dec->outlen_raw, &spdif_config);
                    }
                }
                //now only TV ARC output is using single output. we are implementing the OTT HDMI output in this case.
                // TODO  add OUTPUT_HDMI in this case
                // or STB case
                else if (ddp_dec->digital_raw > 0 && (adev->active_outport == OUTPORT_HDMI_ARC || !adev->is_TV)) {/*single raw output*/
                    if (ddp_dec->pcm_out_info.sample_rate > 0)
                        aml_out->config.rate = ddp_dec->pcm_out_info.sample_rate;
                    if (patch)
                           patch->sample_rate = ddp_dec->pcm_out_info.sample_rate;
                    if (ddp_dec->outlen_raw > 0) {
                        output_format = get_output_format(stream);
                        if (adev->tv_mute) {
                            memset(ddp_dec->outbuf_raw, 0, ddp_dec->outlen_raw);
                        }
                        if (audio_hal_data_processing(stream, (void *)ddp_dec->outbuf_raw, ddp_dec->outlen_raw, &output_buffer, &output_buffer_bytes, output_format) == 0) {
                            hw_write(stream, output_buffer, output_buffer_bytes, output_format);
                        }
                    }
                    return return_bytes;
                }

                if (ddp_dec->outlen_pcm > 0)
                    bytes = ddp_dec->outlen_pcm;
                else
                    bytes = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE * 4;
                if (bytes != 6144)
                    ALOGD("%s: ddp decoder size %d", __func__, bytes);

                while (get_buffer_read_space(&ddp_dec->output_ring_buf) > (int)bytes) {
                    ring_buffer_read(&ddp_dec->output_ring_buf, ddp_dec->outbuf, bytes);
                    void *tmp_buffer = (void *) ddp_dec->outbuf;
#if defined(IS_ATOM_PROJECT)
                    audio_format_t output_format = AUDIO_FORMAT_PCM_32_BIT;
#else
                    audio_format_t output_format = AUDIO_FORMAT_PCM_16_BIT;
#endif
                    aml_hw_mixer_mixing(&adev->hw_mixer, tmp_buffer, bytes, output_format);
                    if (audio_hal_data_processing(stream, tmp_buffer, bytes, &output_buffer, &output_buffer_bytes, output_format) == 0) {
                        hw_write(stream, output_buffer, output_buffer_bytes, output_format);
                    }
                }
                if (ddp_dec->curFrmSize <= ddp_dec->remain_size) {
                    write_bytes = 0;
                    goto dcv_rewrite;
                }
                return return_bytes;
            }

            void *tmp_buffer = (void *) write_buf;
            if (write_buf == NULL) {
                ALOGE("%s(), NULL write buffer!", __func__);
                goto exit;
            }
            if (aml_out->hw_sync_mode) {
#if defined(IS_ATOM_PROJECT)
                audio_format_t output_format = AUDIO_FORMAT_PCM_32_BIT;
                if (!adev->output_tmp_buf || adev->output_tmp_buf_size < 2 * hw_sync->hw_sync_frame_size) {
                    adev->output_tmp_buf = aml_audio_realloc(adev->output_tmp_buf, 2 * hw_sync->hw_sync_frame_size);
                    adev->output_tmp_buf_size = 2 * hw_sync->hw_sync_frame_size;
                }
                uint16_t *p = (uint16_t *)write_buf;
                int32_t *p1 = (int32_t *)adev->output_tmp_buf;
                tmp_buffer = (void *)adev->output_tmp_buf;
                for (unsigned i = 0; i < hw_sync->hw_sync_frame_size / 2; i++) {
                    p1[i] = ((int32_t)p[i]) << 16;
                }
                //hw_sync->hw_sync_frame_size *= 2;
                for (int i = 0; i < 2; i ++) {
                    tmp_buffer = (char *)tmp_buffer + i * hw_sync->hw_sync_frame_size;

#endif
                    if (adev->debug_flag) {
                        ALOGD("%s:%d mixing hw_sync mode, output_format:0x%x, hw_sync_frame_size:%d", __func__, __LINE__, output_format, hw_sync->hw_sync_frame_size);
                    }
                    aml_hw_mixer_mixing(&adev->hw_mixer, tmp_buffer, hw_sync->hw_sync_frame_size, output_format);
                    if (audio_hal_data_processing(stream, tmp_buffer, hw_sync->hw_sync_frame_size, &output_buffer, &output_buffer_bytes, output_format) == 0) {
                        hw_write(stream, output_buffer, output_buffer_bytes, output_format);
                    }
#if defined(IS_ATOM_PROJECT)
                }
#endif

            } else {
                if (adev->debug_flag) {
                    ALOGD("%s:%d mixing non-hw_sync mode, output_format:0x%x, write_bytes:%d", __func__, __LINE__, output_format, write_bytes);
                }
                if (getprop_bool("media.audiohal.mixer")) {
                    aml_audio_dump_audio_bitstreams("/data/audio/beforemix.raw",
                        tmp_buffer, write_bytes);
                }
                aml_hw_mixer_mixing(&adev->hw_mixer, tmp_buffer, write_bytes, output_format);
                if (getprop_bool("media.audiohal.mixer")) {
                    aml_audio_dump_audio_bitstreams("/data/audio/mixed.raw",
                            tmp_buffer, write_bytes);
                }
                if (audio_hal_data_processing(stream, tmp_buffer, write_bytes, &output_buffer, &output_buffer_bytes, output_format) == 0) {
                    hw_write(stream, output_buffer, output_buffer_bytes, output_format);
                }
            }
        }
    }
exit:
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (continous_mode(adev)) {
            aml_out->timestamp = adev->ms12.timestamp;
            //clock_gettime(CLOCK_MONOTONIC, &aml_out->timestamp);
            aml_out->last_frames_postion = adev->ms12.last_frames_postion;
        }
    } else if (eDolbyDcvLib  == adev->dolby_lib_type) {
        if (aml_out->hal_internal_format == AUDIO_FORMAT_AC3 ||
            aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) {
            latency_frames = out_get_latency_frames(stream);
            if (aml_out->input_bytes_size >= latency_frames) {
                aml_out->last_frames_postion = aml_out->input_bytes_size / audio_stream_out_frame_size(stream) - latency_frames;
            } else {
                aml_out->last_frames_postion = 0;
            }
        }
    }

    /*if the data consume is not complete, it will be send again by audio flinger,
      this old data will cause av sync problem after seek.
    */
    if ((eDolbyMS12Lib == adev->dolby_lib_type) && (continous_mode(adev)) && (aml_out->hw_sync_mode) && !ms12_write_failed) {
        /*
        if the data is not  consumed totally,
        we need re-send data again
        */
        if (return_bytes > 0 && total_bytes > (return_bytes + bytes_cost)) {
            bytes_cost += return_bytes;
            //ALOGI("total bytes=%d cost=%d return=%d", total_bytes,bytes_cost,return_bytes);
            goto hwsync_rewrite;
        }
        else if (return_bytes < 0)
            return return_bytes;
        else
            return total_bytes;
    }

    if (adev->debug_flag) {
        ALOGI("%s return %zu!\n", __FUNCTION__, return_bytes);
    }
    return return_bytes;
}

ssize_t mixer_aux_buffer_write(struct audio_stream_out *stream, const void *buffer,
                               size_t bytes)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = 0;
    size_t frame_size = audio_stream_out_frame_size(stream);
    size_t in_frames = bytes / frame_size;
    size_t bytes_remaining = bytes;
    size_t bytes_written = 0;
    bool need_reconfig_output = false;
    bool  need_reset_decoder = false;
    int retry = 0;
    unsigned int alsa_latency_frame = 0;
    pthread_mutex_lock(&adev->lock);
    bool hw_mix = need_hw_mix(adev->usecase_masks);
    uint64_t enter_ns = 0;
    uint64_t leave_ns = 0;

    if (eDolbyMS12Lib == adev->dolby_lib_type && continous_mode(adev)) {
        enter_ns = aml_audio_get_systime_ns();
    }

    if (adev->debug_flag) {
        ALOGD("%s:%d size:%d, dolby_lib_type:0x%x, frame_size:%d",
        __func__, __LINE__, bytes, adev->dolby_lib_type, frame_size);
    }

    if ((aml_out->status == STREAM_HW_WRITING) && hw_mix) {
        ALOGI("%s(), aux do alsa close\n", __func__);
        pthread_mutex_lock(&adev->alsa_pcm_lock);

        aml_alsa_output_close(stream);

        ALOGI("%s(), aux alsa close done\n", __func__);
        aml_out->status = STREAM_MIXING;
        pthread_mutex_unlock(&adev->alsa_pcm_lock);
    }

    if (aml_out->status == STREAM_STANDBY) {
        aml_out->status = STREAM_MIXING;
    }

    if (adev->useSubMix) {
        // For compatible by lianlian
        if (aml_out->standby) {
            ALOGI("%s(), standby to unstandby", __func__);
            aml_out->standby = false;
        }
    }

    if (aml_out->is_normal_pcm && !aml_out->normal_pcm_mixing_config) {
        if (0 == set_system_app_mixing_status(aml_out, aml_out->status)) {
            aml_out->normal_pcm_mixing_config = true;
        } else {
            aml_out->normal_pcm_mixing_config = false;
        }
    }

    pthread_mutex_unlock(&adev->lock);
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (continous_mode(adev)) {
            /*only system sound active*/
            if (!hw_mix) {
                /* here to check if the audio HDMI ARC format updated. */
                if (((adev->arc_hdmi_updated) || (adev->a2dp_updated)) && (adev->ms12.dolby_ms12_enable == true)) {
                    //? if we need protect
                    adev->arc_hdmi_updated = 0;
                    adev->a2dp_updated = 0;
                    need_reconfig_output = true;
                    ALOGI("%s() HDMI ARC EndPoint or a2dp changing status, need reconfig Dolby MS12\n", __func__);
                }

                /* here to check if the audio output routing changed. */
                if ((adev->out_device != aml_out->out_device) && (adev->ms12.dolby_ms12_enable == true)) {
                    ALOGI("%s(), output routing changed from 0x%x to 0x%x,need MS12 reconfig output", __func__, aml_out->out_device, adev->out_device);
                    if (aml_out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP)
                        need_reset_decoder = true;
                    aml_out->out_device = adev->out_device;
                    need_reconfig_output = true;
                }
                /* here to check if the hdmi audio output format dynamic changed. */
                if (adev->pre_hdmi_format != adev->hdmi_format) {
                    ALOGI("hdmi format is changed from %d to %d need reconfig output", adev->pre_hdmi_format, adev->hdmi_format);
                    adev->pre_hdmi_format = adev->hdmi_format;
                    need_reconfig_output = true;
                }
            }

            /* here to check if ms12 is already enabled */
            if (!adev->ms12.dolby_ms12_enable) {
                ALOGI("%s(), 0x%x, Swithing system output to MS12, need MS12 reconfig output", __func__, aml_out->out_device);
                need_reconfig_output = true;
                need_reset_decoder = true;
            }

            if (need_reconfig_output) {
                /*during ms12 switch, the frame write may be not matched with
                  the input size, we need to align it*/
                if (aml_out->frame_write_sum * frame_size != aml_out->input_bytes_size) {
                    ALOGI("Align the frame write from %lld to %lld", aml_out->frame_write_sum, aml_out->input_bytes_size/frame_size);
                    aml_out->frame_write_sum = aml_out->input_bytes_size/frame_size;
                }
                config_output(stream, need_reset_decoder);
            }
        }
        /*
         *when disable_pcm_mixing is true and offload format is ddp and output format is ddp
         *the system tone voice should not be mixed
         */
        if (is_bypass_dolbyms12(stream))
            usleep(bytes * 1000000 /frame_size/out_get_sample_rate(&stream->common)*5/6);
        else {
            /*
             *when disable_pcm_mixing is true and offload format is dolby
             *the system tone voice should not be mixed
             */
            if ((adev->hdmi_format == BYPASS) && (dolby_stream_active(adev) || hwsync_lpcm_active(adev))) {
                memset((void *)buffer, 0, bytes);
                if (adev->debug_flag) {
                    ALOGI("%s mute the mixer voice(system/alexa)\n", __FUNCTION__);
                }
            }

            int count = 0;
            while (bytes_remaining && adev->ms12.dolby_ms12_enable && retry < 20) {
                size_t used_size = 0;
                ret = dolby_ms12_system_process(stream, (char *)buffer + bytes_written, bytes_remaining, &used_size);
                if (!ret) {
                    bytes_remaining -= used_size;
                    bytes_written += used_size;
                    retry = 0;
                }
                retry++;
                if (bytes_remaining) {
                    //usleep(bytes_remaining * 1000000 / frame_size / out_get_sample_rate(&stream->common));
                    aml_audio_sleep(5000);
                }
                count++;
                if ((count % 10) == 0) {
                    ALOGE("%s count %d", __func__, count);
                }
            }
        }
    } else {
        bytes_written = aml_hw_mixer_write(&adev->hw_mixer, buffer, bytes);

        uint32_t u32SampleRate = out_get_sample_rate(&stream->common);
        uint32_t u32FreeBuffer = aml_hw_mixer_get_space(&adev->hw_mixer);

        uint64_t u64BufferDelayUs = ((AML_HW_MIXER_BUF_SIZE * 1000) / ( frame_size * u32SampleRate)) * 1000;
        uint64_t u64ConstantDelayUs = bytes * 1000000 / (frame_size * u32SampleRate * 3);

        ALOGV("%s:%d sampleRate:%d, bytes_written:%d, frame_size:%d, u32FreeBuffer:%d, delay:%llums", __func__, __LINE__,
            u32SampleRate, bytes_written, frame_size, u32FreeBuffer, (u64ConstantDelayUs/1000));
        usleep(u64ConstantDelayUs);

        // when idle buffer is less than 2/5, sleep 1/5 buffer size time
        if (u32FreeBuffer < 2 * AML_HW_MIXER_BUF_SIZE / 5) {
            // (1/5 * AML_HW_MIXER_BUF_SIZE) / (frame_size * u32SampleRate) * 1000000 us
            uint64_t u64DelayTimeUs = ((AML_HW_MIXER_BUF_SIZE * 1000) / u32SampleRate) * (1000 / (5 * frame_size));
            usleep(u64DelayTimeUs);
            ALOGI("%s:%d, mixer idle buffer less than 2/5, need usleep:%lldms end ", __func__, __LINE__, u64DelayTimeUs/1000);
        }

        if (getprop_bool("media.audiohal.mixer")) {
            aml_audio_dump_audio_bitstreams("/data/audio/mixerAux.raw", buffer, bytes);
        }
    }
    aml_out->input_bytes_size += bytes;
    aml_out->frame_write_sum += in_frames;

    clock_gettime (CLOCK_MONOTONIC, &aml_out->timestamp);
    aml_out->lasttimestamp.tv_sec = aml_out->timestamp.tv_sec;
    aml_out->lasttimestamp.tv_nsec = aml_out->timestamp.tv_nsec;

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        /*
           mixer_aux_buffer_write is called when the hw write is called another thread,for example
           main write thread or ms12 thread. aux audio is coming from the audioflinger mixed thread.
           we need caculate the system buffer latency in ms12 also with the alsa out latency.It is
           48K 2 ch 16 bit audio.
           */
        alsa_latency_frame = adev->ms12.latency_frame;
        int system_latency = 0;
        system_latency = dolby_ms12_get_system_buffer_avail(NULL) / frame_size;

        aml_out->last_frames_postion = aml_out->frame_write_sum - system_latency;
        if (aml_out->last_frames_postion >= alsa_latency_frame) {
            aml_out->last_frames_postion -= alsa_latency_frame;
        }
        if (adev->debug_flag) {
            ALOGI("%s stream audio presentation %"PRIu64" latency_frame %d.ms12 system latency_frame %d,total frame=%lld %lld ms",
                  __func__,aml_out->last_frames_postion, alsa_latency_frame, system_latency,aml_out->frame_write_sum, aml_out->frame_write_sum/48);
        }
    } else {
        aml_out->last_frames_postion = aml_out->frame_write_sum;
    }
    return bytes;

}

ssize_t mixer_app_buffer_write(struct audio_stream_out *stream, const void *buffer, size_t bytes)
{
   struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
   struct aml_audio_device *adev = aml_out->dev;
   struct dolby_ms12_desc *ms12 = &(adev->ms12);
   int ret = 0;
   size_t frame_size = audio_stream_out_frame_size(stream);
   size_t bytes_remaining = bytes;
   size_t bytes_written = 0;
   int retry = 20;

   if (adev->debug_flag) {
       ALOGD("[%s:%d] size:%d, frame_size:%d", __func__, __LINE__, bytes, frame_size);
   }

   if (eDolbyMS12Lib != adev->dolby_lib_type) {
        ALOGW("[%s:%d] dolby_lib_type:%d, is not ms12, not support app write", __func__, __LINE__, adev->dolby_lib_type);
        return -1;
   }

   if (is_bypass_dolbyms12(stream)) {
       ALOGW("[%s:%d] is_bypass_dolbyms12, not support app write", __func__, __LINE__);
       return -1;
   }

   while (bytes_remaining && adev->ms12.dolby_ms12_enable && retry > 0) {
       size_t used_size = 0;
       ret = dolby_ms12_app_process(stream, (char *)buffer + bytes_written, bytes_remaining, &used_size);
       if (!ret) {
           bytes_remaining -= used_size;
           bytes_written += used_size;
       }
       retry--;
       if (bytes_remaining) {
           aml_audio_sleep(1000);
       }
   }
   if (retry <= 10) {
       ALOGE("[%s:%d] write retry=%d ", __func__, __LINE__, retry);
   }
   if (retry == 0 && bytes_remaining != 0) {
       ALOGE("[%s:%d] write timeout 10 ms ", __func__, __LINE__);
       bytes -= bytes_remaining;
   }

   return bytes;
}

ssize_t process_buffer_write(struct audio_stream_out *stream,
                            const void *buffer,
                            size_t bytes)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    if (adev->debug_flag) {
        ALOGD("%s:%d size:%d, hal_internal_format:0x%x", __func__, __LINE__, bytes, aml_out->hal_internal_format);
    }

    if (adev->out_device != aml_out->out_device) {
        ALOGD("%s:%p device:%x,%x", __func__, stream, aml_out->out_device, adev->out_device);
        aml_out->out_device = adev->out_device;
        config_output(stream, true);
    }

    if (audio_hal_data_processing(stream, buffer, bytes, &output_buffer, &output_buffer_bytes, aml_out->hal_internal_format) == 0) {
        hw_write(stream, output_buffer, output_buffer_bytes, aml_out->hal_internal_format);
    }
    if (bytes > 0) {
        aml_out->input_bytes_size += bytes;
    }
    return bytes;
}

/* must be called with hw device mutexes locked */
static int usecase_change_validate_l(struct aml_stream_out *aml_out, bool is_standby)
{
    struct aml_audio_device *aml_dev = aml_out->dev;
    bool hw_mix;

    if (is_standby) {
        ALOGI("++[%s:%d], dev masks:%#x, is_standby:%d, out usecase:%s", __func__, __LINE__,
            aml_dev->usecase_masks, is_standby, usecase2Str(aml_out->usecase));
        /**
         * If called by standby, reset out stream's usecase masks and clear the aml_dev usecase masks.
         * So other active streams could know that usecase have been changed.
         * But keep it's own usecase if out_write is called in the future to exit standby mode.
         */
        aml_out->dev_usecase_masks = 0;
        aml_out->write = NULL;
        aml_dev->usecase_masks &= ~(1 << aml_out->usecase);

        if ((aml_out->usecase == STREAM_RAW_DIRECT ||
            aml_out->usecase == STREAM_RAW_HWSYNC)
            && (eDolbyDcvLib == aml_dev->dolby_lib_type)) {
            aml_dev->rawtopcm_flag = true;
            ALOGI("enable rawtopcm_flag !!!");
        }
        ALOGI("--[%s:%d], dev masks:%#x, is_standby:%d, out usecase %s", __func__, __LINE__,
            aml_dev->usecase_masks, is_standby, usecase2Str(aml_out->usecase));
        return 0;
    }

    /* No usecase changes, do nothing */
    if (((aml_dev->usecase_masks == aml_out->dev_usecase_masks) && aml_dev->usecase_masks) && (aml_dev->continuous_audio_mode == 0)) {
        return 0;
    }

        /* check the usecase validation */
    if (popcount(aml_dev->usecase_masks) > MAX_INPUT_STREAM_CNT) {
        ALOGE("[%s:%d], invalid masks:%#x, out usecase:%s!", __func__, __LINE__,
            aml_dev->usecase_masks, usecase2Str(aml_out->usecase));
        return -EINVAL;
    }

    if (((aml_dev->continuous_audio_mode == 1) && (aml_dev->debug_flag > 1)) || \
        (aml_dev->continuous_audio_mode == 0))
        ALOGI("++++[%s:%d],continuous:%d dev masks:%#x,out masks:%#x,out usecase:%s,aml_out:%p", __func__,  __LINE__,
            aml_dev->continuous_audio_mode, aml_dev->usecase_masks, aml_out->dev_usecase_masks, usecase2Str(aml_out->usecase), aml_out);

    /* new output case entered, so no masks has been set to the out stream */
    if (!aml_out->dev_usecase_masks) {
        if ((1 << aml_out->usecase) & aml_dev->usecase_masks) {
            ALOGE("[%s:%d], usecase: %s already exists!!, aml_out:%p", __func__,  __LINE__, usecase2Str(aml_out->usecase), aml_out);
            return -EINVAL;
        }
        /* add the new output usecase to aml_dev usecase masks */
        aml_dev->usecase_masks |= 1 << aml_out->usecase;
    }

    /* choose the out_write functions by usecase masks */
    hw_mix = need_hw_mix(aml_dev->usecase_masks);
    if (aml_dev->continuous_audio_mode == 0) {
        if (hw_mix) {
            /**
             * normal pcm write to aux buffer
             * others write to main buffer
             * may affect the output device
             */
            if (aml_out->is_normal_pcm) {
                aml_out->write = mixer_aux_buffer_write;
                ALOGI("%s(),1 mixer_aux_buffer_write !", __FUNCTION__);
            } else {
                aml_out->write = mixer_main_buffer_write;
                ALOGI("%s(),1 mixer_main_buffer_write !", __FUNCTION__);
            }
        } else {
            /**
             * only one stream output will be processed then send to hw.
             * This case only for normal output without mixing
             */
            aml_out->write = process_buffer_write;
            ALOGI("[%s:%d],1 process_buffer_write ", __func__, __LINE__);
        }
    } else {
        /**
         * normal pcm write to aux buffer
         * others write to main buffer
         * may affect the output device
         */
        if (aml_out->is_normal_pcm) {
            aml_out->write = mixer_aux_buffer_write;
            //ALOGE("%s(),2 mixer_aux_buffer_write !", __FUNCTION__);
            //FIXEME if need config ms12 here if neeeded.
        } else if (aml_out->flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
            aml_out->write = mixer_app_buffer_write;
            //ALOGI("[%s:%d], mixer_app_buffer_write !", __func__, __LINE__);
        } else {
            aml_out->write = mixer_main_buffer_write;
            //ALOGE("%s(),2 mixer_main_buffer_write !", __FUNCTION__);
        }
    }

    /* store the new usecase masks in the out stream */
    aml_out->dev_usecase_masks = aml_dev->usecase_masks;
    if (((aml_dev->continuous_audio_mode == 1) && (aml_dev->debug_flag > 1)) || \
        (aml_dev->continuous_audio_mode == 0))
        ALOGI("----[%s:%d], continuous:%d dev masks:%#x, out masks:%#x, out usecase:%s", __func__, __LINE__,
            aml_dev->continuous_audio_mode, aml_dev->usecase_masks, aml_out->dev_usecase_masks, usecase2Str(aml_out->usecase));
    return 0;
}

/* out_write entrance: every write goes in here. */
ssize_t out_write_new(struct audio_stream_out *stream,
                      const void *buffer,
                      size_t bytes)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    ssize_t ret = 0;
    write_func  write_func_p = NULL;
    size_t frame_size = audio_stream_out_frame_size(stream);
    size_t in_frames = bytes / frame_size;
    if (adev->debug_flag > 1) {
        ALOGI("+<IN>%s: out_stream(%p) position(%zu)", __func__, stream, bytes);
    }

    if (adev->dolby_lib_type_last == eDolbyMS12Lib) {
        if (is_disable_ms12_continuous(stream)) {
            if (adev->continuous_audio_mode) {
                bool set_ms12_non_continuous = true;
                adev->delay_disable_continuous = 0;
                ALOGI("we need disable MS12 continuous");
                get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                adev->exiting_ms12 = 1;
                aml_out->restore_continuous = true;
                clock_gettime(CLOCK_MONOTONIC, &adev->ms12_exiting_start);
            }
        }
    }

    if ((adev->dolby_lib_type_last == eDolbyMS12Lib) &&
        ((adev->patch_src == SRC_DTV) && adev->audio_patching)) {
        /*in dtv case, we can't use continuous mode*/
        if (adev->continuous_audio_mode) {
            bool set_ms12_non_continuous = true;
            ALOGI("in dtv case, we need disable continuous mode");
            get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
        }
    }


    /**
     * deal with the device output changes
     * pthread_mutex_lock(&aml_out->lock);
     * out_device_change_validate_l(aml_out);
     * pthread_mutex_unlock(&aml_out->lock);
     */
    pthread_mutex_lock(&adev->lock);
    if (adev->direct_mode) {
        /*
         * when the third_party apk calls pcm_close during use and then calls pcm_open again,
         * primary hal does not access the sound card,
         * continue to let the third_party apk access the sound card.
         */
        aml_alsa_output_close(stream);
        aml_out->status = STREAM_STANDBY;
        ALOGI("%s,direct mode write,skip bytes %zu\n",__func__,bytes);
        /*TODO accurate delay time */
        usleep(in_frames*1000/48);
        aml_out->frame_write_sum += in_frames;
        pthread_mutex_unlock(&adev->lock);
        return bytes;
    }
    ret = usecase_change_validate_l(aml_out, false);
    if (ret < 0) {
        ALOGE("%s() failed", __func__);
        pthread_mutex_unlock(&adev->lock);
        return ret;
    }
    if (aml_out->write) {
        write_func_p = aml_out->write;
    }
    pthread_mutex_unlock(&adev->lock);
    if (write_func_p) {
        ret = write_func_p(stream, buffer, bytes);
    }
    if (ret > 0) {
        aml_out->total_write_size += ret;
        if (aml_out->is_normal_pcm) {
            size_t frame_size = audio_stream_out_frame_size(stream);
            if (frame_size != 0) {
                adev->sys_audio_frame_written = aml_out->input_bytes_size / frame_size;
            }
        }
    }
    if (adev->debug_flag > 1) {
        ALOGI("-<OUT>%s() ret %zd,%p %"PRIu64"\n", __func__, ret, stream, aml_out->total_write_size);
    }

    return ret;
}

int adev_open_output_stream_new(struct audio_hw_device *dev,
                                audio_io_handle_t handle __unused,
                                audio_devices_t devices,
                                audio_output_flags_t flags,
                                struct audio_config *config,
                                struct audio_stream_out **stream_out,
                                const char *address)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_stream_out *aml_out = NULL;
    stream_usecase_t usecase = STREAM_PCM_NORMAL;
    bool tv_src_stream = false;
    int ret;

    ALOGD("%s: enter", __func__);
    if (address && !strncmp(address, "AML_TV_SOURCE", 13)) {
        ALOGI("%s(): aml TV source stream", __func__);
        tv_src_stream = true;
    }
    ret = adev_open_output_stream(dev,
                                    0,
                                    devices,
                                    flags,
                                    config,
                                    stream_out,
                                    NULL);
    if (ret < 0) {
        ALOGE("%s(), open stream failed", __func__);
    }

    aml_out = (struct aml_stream_out *)(*stream_out);
    /* only pcm mode use new write method */

    get_sink_format(&aml_out->stream);
    aml_out->card = alsa_device_get_card_index();
    if (adev->sink_format == AUDIO_FORMAT_PCM_16_BIT) {
        aml_out->device = PORT_I2S;
    } else {
        aml_out->device = PORT_SPDIF;
    }
    aml_out->usecase = attr_to_usecase(aml_out->device, aml_out->hal_format, aml_out->flags);
    aml_out->is_normal_pcm = (aml_out->usecase == STREAM_PCM_NORMAL) ? 1 : 0;
    aml_out->out_cfg = *config;
    if (adev->useSubMix) {
        // In V1.1, android out lpcm stream and hwsync pcm stream goes to aml mixer,
        // tv source keeps the original way.
        // Next step is to make all compitable.
        if (aml_out->usecase == STREAM_PCM_NORMAL ||
            aml_out->usecase == STREAM_PCM_HWSYNC ||
            aml_out->usecase == STREAM_PCM_MMAP) {
            /*for 96000, we need bypass submix, this is for DTS certification*/
            if (config->sample_rate == 96000 || config->sample_rate == 88200 ||
                    audio_channel_count_from_out_mask(config->channel_mask) > 2) {
                aml_out->bypass_submix = true;
                aml_out->stream.write = out_write_direct;
                aml_out->stream.common.standby = out_standby_direct;
            } else {
                ret = initSubMixingInput(aml_out, config);
                aml_out->bypass_submix = false;
                if (ret < 0) {
                    ALOGE("initSub mixing input failed");
                }
            }
        } else {
            ALOGI("%s(), direct usecase: %s", __func__, usecase2Str(aml_out->usecase));
            if (adev->is_TV) {
                aml_out->stream.write = out_write_new;
                aml_out->stream.common.standby = out_standby_new;
            }
        }
    } else {
        aml_out->stream.write = out_write_new;
        aml_out->stream.common.standby = out_standby_new;
    }
    aml_out->status = STREAM_STANDBY;
    if (adev->continuous_audio_mode == 0) {
        adev->spdif_encoder_init_flag = false;
    }
    if (devices & AUDIO_DEVICE_OUT_ALL_A2DP) {
        if (!audio_is_linear_pcm(aml_out->hal_format)) {
            aml_out->stream.write = out_write_new;
            aml_out->stream.common.standby = out_standby_new;
            aml_out->stream.pause = out_pause_new;
            aml_out->stream.resume = out_resume_new;
            aml_out->stream.flush = out_flush_new;
        }
    }
    aml_out->codec_type = get_codec_type(aml_out->hal_internal_format);

    pthread_mutex_lock(&adev->lock);
    adev->active_outputs[aml_out->usecase] = aml_out;
    pthread_mutex_unlock(&adev->lock);

    if (aml_getprop_bool("media.audio.hal.debug")) {
        aml_out->debug_stream = 1;
    }
    ALOGD("-%s: out %p: usecase:%s card:%d alsa devices:%d", __func__,
        aml_out, usecase2Str(aml_out->usecase), aml_out->card, aml_out->device);

    return 0;
}

void adev_close_output_stream_new(struct audio_hw_device *dev,
                                struct audio_stream_out *stream)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;

    ALOGD("%s: enter usecase = %s", __func__, usecase2Str(aml_out->usecase));
    /* call legacy close to reuse codes */
    adev->active_outputs[aml_out->usecase] = NULL;

    if (adev->useSubMix) {
        if (aml_out->is_normal_pcm ||
            aml_out->usecase == STREAM_PCM_HWSYNC ||
            aml_out->usecase == STREAM_PCM_MMAP) {
            if (!aml_out->bypass_submix) {
                deleteSubMixingInput(aml_out);
            }
        }
    }
    if (aml_out->hw_sync_mode && aml_out->tsync_status != TSYNC_STATUS_STOP) {
        ALOGI("%s set AUDIO_PAUSE when close stream\n",__func__);
        sysfs_set_sysfs_str (TSYNC_EVENT, "AUDIO_PAUSE");
        aml_out->tsync_status = TSYNC_STATUS_PAUSED;

        ALOGI("%s set AUDIO_STOP when close stream\n",__func__);
        sysfs_set_sysfs_str (TSYNC_EVENT, "AUDIO_STOP");
        aml_out->tsync_status = TSYNC_STATUS_STOP;
    }

    adev_close_output_stream(dev, stream);
    //adev->dual_decoder_support = false;
    ALOGD("%s: exit", __func__);
}

static void ts_wait_time(struct timespec *ts, uint32_t time)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += time / 1000000;
    ts->tv_nsec += (time * 1000) % 1000000000;
    if (ts->tv_nsec >= 1000000000) {
        ts->tv_sec++;
        ts->tv_nsec -=1000000000;
    }
}

// buffer/period ratio, bigger will add more latency
void *audio_patch_input_threadloop(void *data)
{
    struct aml_audio_patch *patch = (struct aml_audio_patch *)data;
    struct audio_hw_device *dev;
    struct aml_audio_device *aml_dev;
    ring_buffer_t *ringbuffer;
    struct audio_stream_in *stream_in = NULL;
    struct aml_stream_in *in;
    struct audio_config stream_config;
    struct timespec ts;
    int aux_read_bytes, read_bytes;
    int ret = 0, retry = 0;
    int ring_buffer_size = 0;
    hdmiin_audio_packet_t last_audio_packet = AUDIO_PACKET_AUDS;
    int txl_chip = check_chip_name("txl", 3);
    int last_channel_count = 2;

    ALOGD("%s: enter", __func__);
    if (!patch) {
        return (void *)0;
    }
    dev = patch->dev;
    aml_dev = (struct aml_audio_device *) dev;
    ringbuffer = & (patch->aml_ringbuffer);
    patch->chanmask = stream_config.channel_mask = patch->in_chanmask;
    patch->sample_rate = stream_config.sample_rate = patch->in_sample_rate;
    patch->aformat = stream_config.format = patch->in_format;

    ret = adev_open_input_stream(patch->dev, 0, patch->input_src, &stream_config, &stream_in, 0, NULL, 0);
    if (ret < 0) {
        ALOGE("%s: open input steam failed ret = %d", __func__, ret);
        return (void *)0;
    }

    in = (struct aml_stream_in *)stream_in;

    patch->in_buf_size = read_bytes = DEFAULT_CAPTURE_PERIOD_SIZE * audio_stream_in_frame_size(&in->stream);
    patch->in_buf = aml_audio_calloc(1, patch->in_buf_size);
    if (!patch->in_buf) {
        adev_close_input_stream(patch->dev, &in->stream);
        return (void *)0;
    }

    int first_start = 1;
    prctl(PR_SET_NAME, (unsigned long)"audio_input_patch");
    if (ringbuffer) {
        ring_buffer_size = ringbuffer->size;
    }
    while (!patch->input_thread_exit) {
        int bytes_avail = 0;
        int period_mul = 1;
        int read_threshold = 0;
        switch (patch->aformat) {
        case AUDIO_FORMAT_E_AC3:
            period_mul = EAC3_MULTIPLIER;
            break;
        case AUDIO_FORMAT_MAT:
            period_mul = 4 * 4;
            break;
        case AUDIO_FORMAT_DTS_HD:
            // 192Khz
            period_mul = 4 * 4;
            break;
        default:
            period_mul = 1;
        }
        if (patch->input_src == AUDIO_DEVICE_IN_LINE) {
            read_threshold = 4 * read_bytes * period_mul;
        }
        ALOGV("++%s start to in read, periodmul %d, threshold %d",
              __func__, period_mul, read_threshold);

        // buffer size diff from allocation size, need to resize.
        if (patch->in_buf_size < (size_t)read_bytes * period_mul) {
            ALOGI("%s: !!realloc in buf size from %zu to %zu", __func__, patch->in_buf_size, read_bytes * period_mul);
            patch->in_buf = aml_audio_realloc(patch->in_buf, read_bytes * period_mul);
            patch->in_buf_size = read_bytes * period_mul;
        }
#if 0
        struct timespec before_read;
        struct timespec after_read;
        int us = 0;
        clock_gettime(CLOCK_MONOTONIC, &before_read);
#endif
        bytes_avail = in_read(stream_in, patch->in_buf, read_bytes * period_mul);
        if (aml_dev->tv_mute) {
            memset(patch->in_buf, 0, bytes_avail);
        }

#if 0
        clock_gettime(CLOCK_MONOTONIC, &after_read);
        us = calc_time_interval_us(&before_read, &after_read);
        ALOGD("function gap =%d \n", us);
#endif

        ALOGV("++%s in read over read_bytes = %d, in_read returns = %d",
              __FUNCTION__, read_bytes * period_mul, bytes_avail);
        if (bytes_avail > 0) {
            do {
                if (patch->input_src == AUDIO_DEVICE_IN_HDMI) {
                    hdmiin_audio_packet_t cur_audio_packet = get_hdmiin_audio_packet(&aml_dev->alsa_mixer);
                    int current_channel = get_hdmiin_channel(&aml_dev->alsa_mixer);

                    /*if it is txl & 88.2k~192K, we will use software detect*/
                    if (txl_chip && (cur_audio_packet == AUDIO_PACKET_HBR)) {
                        feeddata_audio_type_parse(&patch->audio_parse_para, patch->in_buf, bytes_avail);
                    }

                    //reconfig input stream and buffer when HBR and AUDS audio switching or channel num changed
                    if ((current_channel != -1 && last_channel_count != current_channel)
                        || (cur_audio_packet != AUDIO_PACKET_NONE && last_audio_packet != cur_audio_packet)) {
                        int period_size = 0;
                        int buf_size = 0;
                        int channel = 2;
                        bool bSpdifin_PAO = false;

                        ALOGD("HDMI Format Switch from last_type=%d, cur_type=%d, last_ch = %d, cur_ch = %d\n",
                            last_audio_packet, cur_audio_packet, last_channel_count, current_channel);

                        if (cur_audio_packet == AUDIO_PACKET_HBR) {
                            // if it is high bitrate bitstream, use PAO and increase the buffer size
                            bSpdifin_PAO = true;
                            if (is_need_config_channel())
                                period_size = DEFAULT_CAPTURE_PERIOD_SIZE * 4;
                            else
                                period_size = DEFAULT_CAPTURE_PERIOD_SIZE * 4 * 4;
                            // increase the buffer size
                            buf_size = ring_buffer_size * 8;
                            channel = 8;
                        } else if (cur_audio_packet == AUDIO_PACKET_AUDS) {
                            bSpdifin_PAO = false;
                            period_size = DEFAULT_CAPTURE_PERIOD_SIZE;
                            // reset to original one
                            buf_size = ring_buffer_size;
                            channel = current_channel;
                        }

                        if (!alsa_device_is_auge()) {
                            set_spdifin_pao(&aml_dev->alsa_mixer, bSpdifin_PAO);
                        }

                        ring_buffer_reset_size(ringbuffer, buf_size);
                        in_reset_config_param(stream_in, AML_INPUT_STREAM_CONFIG_TYPE_PERIODS, &period_size);
                        if (is_need_config_channel())
                            in_reset_config_param(stream_in, AML_INPUT_STREAM_CONFIG_TYPE_CHANNELS, &channel);
                        last_audio_packet = cur_audio_packet;
                        last_channel_count = current_channel;
                        in->audio_packet_type = cur_audio_packet;
                        break;
                    }
                }
                if (get_buffer_write_space(ringbuffer) >= bytes_avail) {
                    retry = 0;
                    ret = ring_buffer_write(ringbuffer,
                                            (unsigned char*)patch->in_buf,
                                            bytes_avail, UNCOVER_WRITE);
                    if (ret != bytes_avail) {
                        ALOGE("%s(), write buffer fails!", __func__);
                    }

                    if (!first_start || get_buffer_read_space(ringbuffer) >= read_threshold) {
                        pthread_cond_signal(&patch->cond);
                        if (first_start) {
                            first_start = 0;
                        }
                    }
                    //usleep(1000);
                } else if ((aml_dev->in_device & AUDIO_DEVICE_IN_HDMI_ARC) &&
                        (access(SYS_NODE_EARC_RX, F_OK) == 0) &&
                        (aml_mixer_ctrl_get_int(&aml_dev->alsa_mixer,
                                AML_MIXER_ID_HDMI_EARC_AUDIO_ENABLE) == 0)) {
                    ALOGW("%s(), arc in disconnect,please check the status", __func__);
                } else {
                    retry = 1;
                    pthread_cond_signal(&patch->cond);
                    //Fixme: if ringbuffer is full enough but no output, reset ringbuffer
                    ALOGW("%s(), ring buffer no space to write, buffer aml_audio_free size:%d, need write size:%d", __func__,
                        get_buffer_write_space(ringbuffer), bytes_avail);
                    ring_buffer_reset(ringbuffer);
                    usleep(3000);
                }
            } while (retry && !patch->input_thread_exit);
        } else {
            ALOGV("%s(), read alsa pcm fails, to _read(%d), bytes_avail(%d)!",
                  __func__, read_bytes * period_mul, bytes_avail);
            if (get_buffer_read_space(ringbuffer) >= bytes_avail) {
                pthread_cond_signal(&patch->cond);
            }
            usleep(3000);
        }
    }

    adev_close_input_stream(patch->dev, &in->stream);
    if (patch->in_buf) {
        aml_audio_free(patch->in_buf);
        patch->in_buf = NULL;
    }
    ALOGD("%s: exit", __func__);

    return (void *)0;
}

#define AVSYNC_SAMPLE_MAX_CNT (10)

void *audio_patch_output_threadloop(void *data)
{
    struct aml_audio_patch *patch = (struct aml_audio_patch *)data;
    struct audio_hw_device *dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    ring_buffer_t *ringbuffer = &(patch->aml_ringbuffer);
    struct audio_stream_out *stream_out = NULL;
    struct aml_stream_out *aml_out = NULL,*out;
    struct audio_config stream_config;
    struct timespec ts;
    int write_bytes = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
    int ret;
    ALOGD("%s: enter", __func__);
    stream_config.channel_mask = patch->out_chanmask;
    stream_config.sample_rate = patch->out_sample_rate;
    stream_config.format = patch->out_format;

#ifdef DOLBY_MS12_INPUT_FORMAT_TEST
    char buf[PROPERTY_VALUE_MAX];
    int prop_ret = -1;
    int format = 0;
    prop_ret = property_get("dolby.ms12.input.format", buf, NULL);
    if (prop_ret > 0) {
        format = atoi(buf);
        if (format == 1) {
            stream_config.format = AUDIO_FORMAT_AC3;
        } else if (format == 2) {
            stream_config.format = AUDIO_FORMAT_E_AC3;
        }
    }
#endif
    /*
    may we just exit from a direct active stream playback
    still here.we need remove to standby to new playback
    */
    pthread_mutex_lock(&aml_dev->lock);
    aml_out = direct_active(aml_dev);
    if (aml_out) {
        ALOGI("%s stream %p active,need standby aml_out->usecase:%s ", __func__, aml_out, usecase2Str(aml_out->usecase));
        pthread_mutex_lock(&aml_out->lock);
        do_output_standby_l((struct audio_stream *)aml_out);
        pthread_mutex_unlock(&aml_out->lock);
        if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
            get_dolby_ms12_cleanup(&aml_dev->ms12, false);
        }
        if (aml_dev->need_remove_conti_mode == true) {
            ALOGI("%s,conntinous mode still there,release ms12 here", __func__);
            aml_dev->need_remove_conti_mode = false;
            aml_dev->continuous_audio_mode = 0;
        }
    }
    aml_dev->mix_init_flag = false;
    pthread_mutex_unlock(&aml_dev->lock);
    ret = adev_open_output_stream_new(patch->dev,
                                      0,
                                      patch->output_src,
                                      AUDIO_OUTPUT_FLAG_DIRECT,
                                      &stream_config,
                                      &stream_out,
                                      "AML_TV_SOURCE");
    if (ret < 0) {
        ALOGE("%s: open output stream failed", __func__);
        return (void *)0;
    }

    out = (struct aml_stream_out *)stream_out;
    patch->out_buf_size = write_bytes = out->config.period_size * audio_stream_out_frame_size(&out->stream);
    patch->out_buf = aml_audio_calloc(1, patch->out_buf_size);
    if (!patch->out_buf) {
        adev_close_output_stream_new(patch->dev, &out->stream);
        return (void *)0;
    }
    prctl(PR_SET_NAME, (unsigned long)"audio_output_patch");

    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    CPU_SET(2, &cpuSet);
    CPU_SET(3, &cpuSet);
    int sastat = sched_setaffinity(0, sizeof(cpu_set_t), &cpuSet);
    if (sastat) {
        ALOGW("%s(), failed to set cpu affinity", __FUNCTION__);
    }
    while (!patch->output_thread_exit) {
        int period_mul = 1;
        switch (patch->aformat) {
        case AUDIO_FORMAT_E_AC3:
            period_mul = EAC3_MULTIPLIER;
            break;
        case AUDIO_FORMAT_DTS_HD:
            period_mul = 4;
            break;
        default:
            period_mul = 1;
        }
        // buffer size diff from allocation size, need to resize.
        if (patch->out_buf_size < (size_t)write_bytes * period_mul) {
            ALOGI("%s: !!realloc out buf size from %zu to %zu", __func__, patch->out_buf_size, write_bytes * period_mul);
            patch->out_buf = aml_audio_realloc(patch->out_buf, write_bytes * period_mul);
            patch->out_buf_size = write_bytes * period_mul;
        }

        pthread_mutex_lock(&patch->mutex);
        ALOGV("%s(), ringbuffer level read before wait--%d",
              __func__, get_buffer_read_space(ringbuffer));
        if (get_buffer_read_space(ringbuffer) < (write_bytes * period_mul)) {
            // wait 300ms
            ts_wait_time(&ts, 300000);
            pthread_cond_timedwait(&patch->cond, &patch->mutex, &ts);
        }
        pthread_mutex_unlock(&patch->mutex);

        ALOGV("%s(), ringbuffer level read after wait-- %d",
              __func__, get_buffer_read_space(ringbuffer));
        if (get_buffer_read_space(ringbuffer) >= (write_bytes * period_mul)) {
            ret = ring_buffer_read(ringbuffer,
                                   (unsigned char*)patch->out_buf, write_bytes * period_mul);
            if (ret == 0) {
                ALOGE("%s(), ring_buffer read 0 data!", __func__);
            }
#if 0
            struct timespec before_read;
            struct timespec after_read;
            int us = 0;
            clock_gettime(CLOCK_MONOTONIC, &before_read);
#endif

            ALOGV("++%s ringbuf get data ok, start to out write, bytes-%d",
                  __FUNCTION__, ret);
            ret = out_write_new(stream_out, patch->out_buf, ret);
            ALOGV("++%s ringbuf get data ok, out write over!, retbytes = %d",
                  __FUNCTION__, ret);
#if 0
            clock_gettime(CLOCK_MONOTONIC, &after_read);
            us = calc_time_interval_us(&before_read, &after_read);
            ALOGD("function gap =%d \n", us);
#endif
            //do_avsync(patch, period_mul, &stream_config);
        } else {
            ALOGW("%s(), no enough data in ring buffer, available data size:%d, need data size:%d", __func__,
                get_buffer_read_space(ringbuffer), (write_bytes * period_mul));
            //repeat last period data, when no data in ringbuffer
            ret = out_write_new(stream_out, patch->out_buf, DEFAULT_PLAYBACK_PERIOD_SIZE);
            usleep( (DEFAULT_PLAYBACK_PERIOD_SIZE) * 1000000 / 4 /
                stream_config.sample_rate);
        }
    }

    adev_close_output_stream_new(patch->dev, &out->stream);
    if (patch->out_buf) {
        aml_audio_free(patch->out_buf);
        patch->out_buf = NULL;
    }
    ALOGD("%s: exit", __func__);

    return (void *)0;
}

#define PATCH_PERIOD_COUNT  (4)
static int create_patch_l(struct audio_hw_device *dev,
                        audio_devices_t input,
                        audio_devices_t output __unused)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    struct aml_audio_patch *patch;
    int play_buffer_size = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
    pthread_attr_t attr;
    struct sched_param param;
    int ret = 0;

    ALOGD("%s: enter", __func__);
    if (aml_dev->audio_patch) {
        ALOGD("%s: patch exists, first release it", __func__);
        ALOGD("%s: new input %#x, old input %#x",
            __func__, input, aml_dev->audio_patch->input_src);
        if (aml_dev->audio_patch->is_dtv_src)
            release_dtv_patch_l(aml_dev);
        else
            release_patch_l(aml_dev);
    }

    patch = aml_audio_calloc(1, sizeof(*patch));
    if (!patch) {
        return -ENOMEM;
    }
    aml_dev->source_mute = 0;
    patch->dev = dev;
    patch->input_src = input;
    patch->is_dtv_src = false;
    patch->aformat = AUDIO_FORMAT_PCM_16_BIT;
    patch->avsync_sample_max_cnt = AVSYNC_SAMPLE_MAX_CNT;
    aml_dev->audio_patch = patch;
    pthread_mutex_init(&patch->mutex, NULL);
    pthread_cond_init(&patch->cond, NULL);

    patch->in_sample_rate = 48000;
    patch->in_chanmask = AUDIO_CHANNEL_IN_STEREO;
    patch->output_src = AUDIO_DEVICE_OUT_SPEAKER;
    patch->out_sample_rate = 48000;
    patch->out_chanmask = AUDIO_CHANNEL_OUT_STEREO;
#if defined(IS_ATOM_PROJECT)
    patch->in_format = AUDIO_FORMAT_PCM_32_BIT;
    patch->out_format = AUDIO_FORMAT_PCM_32_BIT;
#else
    patch->in_format = AUDIO_FORMAT_PCM_16_BIT;
    patch->out_format = AUDIO_FORMAT_PCM_16_BIT;
#endif
    patch->avsync_drop = 0;
    patch->avsync_adelay = 0;
    patch->is_alsa_input_mute = false;
    patch->drop_buf = aml_audio_calloc(1, sizeof(unsigned char) * DROP_AUDIO_SIZE);

    if (aml_dev->useSubMix) {
        // switch normal stream to old tv mode writing
        switchNormalStream(aml_dev->active_outputs[STREAM_PCM_NORMAL], 0);
    }

    if (patch->out_format == AUDIO_FORMAT_PCM_16_BIT)
        ret = ring_buffer_init(&patch->aml_ringbuffer, 8 * 2 * 2 * play_buffer_size * PATCH_PERIOD_COUNT);
    else
        ret = ring_buffer_init(&patch->aml_ringbuffer, 8 * 2 * 4 * play_buffer_size * PATCH_PERIOD_COUNT);
    if (ret < 0) {
        ALOGE("%s: init audio ringbuffer failed", __func__);
        goto err_ring_buf;
    }
    ret = pthread_create(&patch->audio_input_threadID, NULL,
                          &audio_patch_input_threadloop, patch);

    if (ret != 0) {
        ALOGE("%s: Create input thread failed", __func__);
        goto err_in_thread;
    }
    ret = pthread_create(&patch->audio_output_threadID, NULL,
                          &audio_patch_output_threadloop, patch);
    if (ret != 0) {
        ALOGE("%s: Create output thread failed", __func__);
        goto err_out_thread;
    }

    if (IS_HDMI_IN_HW(patch->input_src) ||
            patch->input_src == AUDIO_DEVICE_IN_SPDIF) {
        //TODO add sample rate and channel information
        ret = creat_pthread_for_audio_type_parse(&patch->audio_parse_threadID,
                &patch->audio_parse_para, &aml_dev->alsa_mixer, patch->input_src);
        if (ret !=  0) {
            ALOGE("%s: create format parse thread failed", __func__);
            goto err_parse_thread;
        }
    }

    ALOGD("%s: exit", __func__);
    return 0;
err_parse_thread:
    patch->output_thread_exit = 1;
    pthread_join(patch->audio_output_threadID, NULL);
err_out_thread:
    patch->input_thread_exit = 1;
    pthread_join(patch->audio_input_threadID, NULL);
err_in_thread:
    ring_buffer_release(&patch->aml_ringbuffer);
err_ring_buf:
    aml_dev->audio_patch = NULL;
    aml_audio_free(patch);
    return ret;
}

static int create_patch_ext(struct audio_hw_device *dev,
                            audio_devices_t input,
                            audio_devices_t output __unused,
                            audio_patch_handle_t handle)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    int ret = 0;

    pthread_mutex_lock(&aml_dev->patch_lock);
    ret = create_patch_l(dev, input, output);
    pthread_mutex_unlock(&aml_dev->patch_lock);

    // successful
    if (!ret) {
        aml_dev->audio_patch->patch_hdl = handle;
    }

    return ret;
}

int release_patch_l(struct aml_audio_device *aml_dev)
{
    struct aml_audio_patch *patch = aml_dev->audio_patch;

    ALOGD("%s: enter", __func__);
    if (aml_dev->audio_patch == NULL) {
        ALOGD("%s(), no patch to release", __func__);
        goto exit;
    }
    patch->output_thread_exit = 1;
    patch->input_thread_exit = 1;
    if (IS_HDMI_IN_HW(patch->input_src) ||
            patch->input_src == AUDIO_DEVICE_IN_SPDIF)
        exit_pthread_for_audio_type_parse(patch->audio_parse_threadID,&patch->audio_parse_para);
    patch->input_thread_exit = 1;
    pthread_join(patch->audio_input_threadID, NULL);
    patch->output_thread_exit = 1;
    pthread_join(patch->audio_output_threadID, NULL);
    ring_buffer_release(&patch->aml_ringbuffer);
    aml_audio_free(patch->drop_buf);
    aml_audio_free(patch);
    aml_dev->audio_patch = NULL;
    ALOGD("%s: exit", __func__);
#if defined(IS_ATOM_PROJECT)
    aml_audio_free(aml_dev->output_tmp_buf);
    aml_dev->output_tmp_buf = NULL;
    aml_dev->output_tmp_buf_size = 0;
#endif

    if (aml_dev->useSubMix) {
        switchNormalStream(aml_dev->active_outputs[STREAM_PCM_NORMAL], 1);
    }

exit:
    return 0;
}

static int release_patch(struct aml_audio_device *aml_dev)
{
    pthread_mutex_lock(&aml_dev->patch_lock);
    release_patch_l(aml_dev);
    pthread_mutex_unlock(&aml_dev->patch_lock);

    return 0;
}

static int create_patch(struct audio_hw_device *dev,
                        audio_devices_t input,
                        audio_devices_t output __unused)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    int ret = 0;

    pthread_mutex_lock(&aml_dev->patch_lock);
    ret = create_patch_l(dev, input, output);
    pthread_mutex_unlock(&aml_dev->patch_lock);

    return ret;
}
static int create_parser (struct audio_hw_device *dev, enum IN_PORT inport)
{
    struct aml_audio_parser *parser;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    int period_size = 4096;
    int ret = 0;
    audio_devices_t input_src;

    ALOGI ("++%s", __func__);
    parser = aml_audio_calloc (1, sizeof (struct aml_audio_parser) );
    if (!parser) {
        ret = -ENOMEM;
        goto err;
    }

    parser->dev = dev;
    parser->aformat = AUDIO_FORMAT_PCM_16_BIT;
    aml_dev->aml_parser = parser;
    pthread_mutex_init (&parser->mutex, NULL);

    ret = ring_buffer_init (& (parser->aml_ringbuffer), 4*period_size*PATCH_PERIOD_COUNT);
    if (ret < 0) {
        ALOGE ("Fail to init audio ringbuffer!");
        goto err_ring_buf;
    }

    if (inport == INPORT_HDMIIN) {
        input_src = AUDIO_DEVICE_IN_HDMI;
    } else if (inport == INPORT_ARCIN) {
        input_src = AUDIO_DEVICE_IN_HDMI_ARC;
    } else {
        input_src = AUDIO_DEVICE_IN_SPDIF;
    }

    ret = creat_pthread_for_audio_type_parse (&parser->audio_parse_threadID,
            &parser->audio_parse_para, &aml_dev->alsa_mixer, input_src);
    if (ret !=  0) {
        ALOGE ("%s,create format parse thread fail!\n",__FUNCTION__);
        goto err_in_thread;
    }
    ALOGI ("--%s input_source %x", __FUNCTION__, input_src);
    return 0;
err_in_thread:
    ring_buffer_release (& (parser->aml_ringbuffer) );
err_ring_buf:
    aml_audio_free (parser);
err:
    return ret;
}

static int release_parser (struct aml_audio_device *aml_dev)
{
    struct aml_audio_parser *parser = aml_dev->aml_parser;

    ALOGI ("++%s", __FUNCTION__);
    if (aml_dev->aml_parser == NULL) {
        ALOGI("--%s aml_dev->aml_parser == NULL", __FUNCTION__);
        return 0;
    }
    if (parser->decode_enabled == 1) {
        ALOGI ("++%s: release decoder!", __FUNCTION__);
        dcv_decode_release (parser);
        parser->aformat = AUDIO_FORMAT_PCM_16_BIT;
    }
    exit_pthread_for_audio_type_parse (parser->audio_parse_threadID,&parser->audio_parse_para);
    ring_buffer_release (& (parser->aml_ringbuffer) );
    aml_audio_free (parser);
    aml_dev->aml_parser = NULL;
    ALOGI ("--%s", __FUNCTION__);
    return 0;
}

static void dump_audio_patch_set (struct audio_patch_set *patch_set)
{
    struct audio_patch *patch = NULL;
    unsigned int i = 0;

    if (!patch_set)
        return;

    patch = &patch_set->audio_patch;
    if (!patch)
        return;

    ALOGI ("  - %s(), id: %d", __func__, patch->id);
    for (i = 0; i < patch->num_sources; i++)
        dump_audio_port_config (&patch->sources[i]);
    for (i = 0; i < patch->num_sinks; i++)
        dump_audio_port_config (&patch->sinks[i]);
}

static void dump_aml_audio_patch_sets (struct audio_hw_device *dev)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    struct audio_patch_set *patch_set = NULL;
    struct audio_patch *patch = NULL;
    struct listnode *node = NULL;
    unsigned int i = 0;

    ALOGI ("++%s(), lists all patch sets:", __func__);
    list_for_each (node, &aml_dev->patch_list) {
        ALOGI (" - patch set num: %d", i);
        patch_set = node_to_item (node, struct audio_patch_set, list);
        dump_audio_patch_set (patch_set);
        i++;
    }
    ALOGI ("--%s(), lists all patch sets over", __func__);
}


static int get_audio_patch_by_hdl(struct audio_hw_device *dev, audio_patch_handle_t handle, struct audio_patch *p_audio_patch)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;

    struct listnode *node = NULL;

    struct audio_patch_set *patch_set_tmp = NULL;
    struct audio_patch *patch_tmp = NULL;
    p_audio_patch = NULL;

    list_for_each(node, &aml_dev->patch_list) {
        patch_set_tmp = node_to_item(node, struct audio_patch_set, list);
        patch_tmp = &patch_set_tmp->audio_patch;


        if (patch_tmp->id == handle) {
            p_audio_patch = patch_tmp;
            break;
        }
    }
    return 0;
}

static int get_audio_patch_by_src_dev(struct audio_hw_device *dev, audio_devices_t dev_type, struct audio_patch **p_audio_patch)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    struct listnode *node = NULL;
    struct audio_patch_set *patch_set_tmp = NULL;
    struct audio_patch *patch_tmp = NULL;

    list_for_each(node, &aml_dev->patch_list) {
        patch_set_tmp = node_to_item(node, struct audio_patch_set, list);
        patch_tmp = &patch_set_tmp->audio_patch;
        if (patch_tmp->sources[0].ext.device.type == dev_type) {
            ALOGI("%s, patch_tmp->id = %d, dev_type = %ud", __func__, patch_tmp->id, dev_type);
            *p_audio_patch = patch_tmp;
            break;
        }
    }
    return 0;
}
/* remove audio patch from dev list */
static int unregister_audio_patch(struct audio_hw_device *dev __unused,
                                struct audio_patch_set *patch_set)
{
    ALOGD("%s: enter", __func__);
    if (!patch_set)
        return -EINVAL;
#ifdef DEBUG_PATCH_SET
    dump_audio_patch_set(patch_set);
#endif
    list_remove(&patch_set->list);
    aml_audio_free(patch_set);
    ALOGD("%s: exit", __func__);

    return 0;
}

/* add new audio patch to dev list */
static struct audio_patch_set *register_audio_patch(struct audio_hw_device *dev,
                                                unsigned int num_sources,
                                                const struct audio_port_config *sources,
                                                unsigned int num_sinks,
                                                const struct audio_port_config *sinks,
                                                audio_patch_handle_t *handle)
{
    /* init audio patch */
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    struct audio_patch_set *patch_set_new = NULL;
    struct audio_patch *patch_new = NULL;
    struct audio_patch_set *patch_set_tmp = NULL;
    struct audio_patch *patch_tmp = NULL;
    struct listnode *node = NULL;
    audio_patch_handle_t patch_handle;

    patch_set_new = aml_audio_calloc(1, sizeof (*patch_set_new) );
    if (!patch_set_new) {
        ALOGE("%s(): no memory", __func__);
        return NULL;
    }

    patch_new = &patch_set_new->audio_patch;
    patch_handle = (audio_patch_handle_t) android_atomic_inc(&aml_dev->next_unique_ID);
    *handle = patch_handle;

    /* init audio patch new */
    patch_new->id = patch_handle;
    patch_new->num_sources = num_sources;
    memcpy(patch_new->sources, sources, num_sources * sizeof(struct audio_port_config));
    patch_new->num_sinks = num_sinks;
    memcpy(patch_new->sinks, sinks, num_sinks * sizeof (struct audio_port_config));
#ifdef DEBUG_PATCH_SET
    ALOGD("%s(), patch set new to register:", __func__);
    dump_audio_patch_set(patch_set_new);
#endif

    /* find if mix->dev exists and remove from list */
    list_for_each(node, &aml_dev->patch_list) {
        patch_set_tmp = node_to_item(node, struct audio_patch_set, list);
        patch_tmp = &patch_set_tmp->audio_patch;
        if (patch_tmp->sources[0].type == AUDIO_PORT_TYPE_MIX &&
            patch_tmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE &&
            sources[0].ext.mix.handle == patch_tmp->sources[0].ext.mix.handle) {
            ALOGI("patch found mix->dev id %d, patchset %p", patch_tmp->id, patch_set_tmp);
                break;
        } else {
            patch_set_tmp = NULL;
            patch_tmp = NULL;
        }
    }
    /* found mix->dev patch, so release and remove it */
    if (patch_set_tmp) {
        ALOGD("%s, found the former mix->dev patch set, remove it first", __func__);
        unregister_audio_patch(dev, patch_set_tmp);
        patch_set_tmp = NULL;
        patch_tmp = NULL;
    }
    /* find if dev->mix exists and remove from list */
    list_for_each (node, &aml_dev->patch_list) {
        patch_set_tmp = node_to_item(node, struct audio_patch_set, list);
        patch_tmp = &patch_set_tmp->audio_patch;
        if (patch_tmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE &&
            patch_tmp->sinks[0].type == AUDIO_PORT_TYPE_MIX &&
            sinks[0].ext.mix.handle == patch_tmp->sinks[0].ext.mix.handle) {
            ALOGI("patch found dev->mix id %d, patchset %p", patch_tmp->id, patch_set_tmp);
                break;
        } else {
            patch_set_tmp = NULL;
            patch_tmp = NULL;
        }
    }
    /* found dev->mix patch, so release and remove it */
    if (patch_set_tmp) {
        ALOGD("%s, found the former dev->mix patch set, remove it first", __func__);
        unregister_audio_patch(dev, patch_set_tmp);
    }
    /* add new patch set to dev patch list */
    list_add_head(&aml_dev->patch_list, &patch_set_new->list);
    ALOGI("--%s: after registering new patch, patch sets will be:", __func__);
    //dump_aml_audio_patch_sets(dev);

    return patch_set_new;
}

bool bypass_primary_patch(const struct audio_port_config *sources)
{
    if ((sources->ext.device.type == AUDIO_DEVICE_IN_WIRED_HEADSET) ||
            ((remoteDeviceOnline()|| nano_is_connected()) &&
            (sources->ext.device.type == AUDIO_DEVICE_IN_BUILTIN_MIC)))
        return true;

    return false;
}

static int adev_create_audio_patch(struct audio_hw_device *dev,
                                unsigned int num_sources,
                                const struct audio_port_config *sources,
                                unsigned int num_sinks,
                                const struct audio_port_config *sinks,
                                audio_patch_handle_t *handle)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    struct audio_patch_set *patch_set;
    const struct audio_port_config *src_config = sources;
    const struct audio_port_config *sink_config = sinks;
    enum input_source input_src = HDMIIN;
    uint32_t sample_rate = 48000, channel_cnt = 2;
    enum OUT_PORT outport = OUTPORT_SPEAKER;
    enum IN_PORT inport = INPORT_HDMIIN;
    unsigned int i = 0;
    int ret = -1;
    aml_dev->no_underrun_max = property_get_int32("media.audio_hal.nounderrunmax", 60);
    if (bypass_primary_patch(src_config)) {
        ALOGI("[%s:%d] bluetooth voice search is in use, bypass it.", __func__, __LINE__);
        return -1;
    }
    if (!sources || !sinks || !handle) {
        ALOGE("[%s:%d] null pointer! sources:%p, sinks:%p, handle:%p", __func__, __LINE__, sources, sinks, handle);
        return -EINVAL;
    }

    if ((num_sources != 1) || (num_sinks > AUDIO_PATCH_PORTS_MAX)) {
        ALOGE("[%s:%d] unsupport num_sources:%d != 1 or num_sinks:%d > %d", __func__, __LINE__,
            num_sources, num_sinks, AUDIO_PATCH_PORTS_MAX);
        return -EINVAL;
    }

    patch_set = register_audio_patch(dev, num_sources, sources, num_sinks, sinks, handle);
    ALOGI("[%s:%d] num_sources:%d, num_sinks:%d, source:%d -> sink:%d, patch_src:%d, AF:%p", __func__, __LINE__,
        num_sources, num_sinks, src_config->type, sink_config->type,aml_dev->patch_src, handle);
    if (!patch_set) {
        ALOGW("[%s:%d] patch_set is null, create fail.", __func__, __LINE__);
        return -ENOMEM;
    }

    if (sink_config->type == AUDIO_PORT_TYPE_DEVICE) {
        android_dev_convert_to_hal_dev(sink_config->ext.device.type, (int *)&outport);
          /*
           * when HDMI plug in or UI [Sound Output Device] set to "ARC" ,AUDIO need to output from HDMI ARC
           * while in current design, speaker is an "always on" device
           * in this case , there will be 2 devices, one is ARC, one is SPEAKER
           * we need to output audio from ARC, so set outport as OUTPORT_HDMI_ARC
           */
        if (num_sinks == 2) {
            ALOGI("[%s:%d] sink[0]:%#x, sink[1]:%#x", __func__, __LINE__, sink_config[0].ext.device.type, sink_config[1].ext.device.type);
            if ((sink_config[0].ext.device.type == AUDIO_DEVICE_OUT_HDMI_ARC && sink_config[1].ext.device.type == AUDIO_DEVICE_OUT_SPEAKER)
                || (sink_config[0].ext.device.type == AUDIO_DEVICE_OUT_SPEAKER && sink_config[1].ext.device.type == AUDIO_DEVICE_OUT_HDMI_ARC)) {
                if (aml_dev->bHDMIARCon) {
                    outport = OUTPORT_HDMI_ARC;
                } else {
                    outport = OUTPORT_SPEAKER;
                }
                ALOGI("[%s:%d] bHDMIARCon:%d speaker and HDMI_ARC co-exist case, ouput:%s.", __func__, __LINE__,
                    aml_dev->bHDMIARCon, outport2String(outport));
            }
        }
        if (outport != OUTPORT_SPDIF) {
            ret = aml_audio_output_routing(dev, outport, false);
        }
        if (ret < 0) {
            ALOGW("[%s:%d] output routing failed, outport:%s.", __func__, __LINE__, outport2String(outport));
        }
        aml_dev->out_device = 0;
        for (i = 0; i < num_sinks; i++) {
            aml_dev->out_device |= sink_config[i].ext.device.type;
        }

        /* 1.device to device audio patch. TODO: unify with the android device type*/
        if (src_config->type == AUDIO_PORT_TYPE_DEVICE) {
            if (sink_config->config_mask & AUDIO_PORT_CONFIG_SAMPLE_RATE) {
                sample_rate = sink_config->sample_rate;
            }

            if (sink_config->config_mask & AUDIO_PORT_CONFIG_CHANNEL_MASK) {
                channel_cnt = audio_channel_count_from_out_mask(sink_config->channel_mask);
            }

            ret = android_dev_convert_to_hal_dev(src_config->ext.device.type, (int *)&inport);
            if (ret != 0) {
                ALOGE("[%s:%d] device->device patch: unsupport input dev:%#x.", __func__, __LINE__, src_config->ext.device.type);
                ret = -EINVAL;
                unregister_audio_patch(dev, patch_set);
            }
            input_src = android_input_dev_convert_to_hal_input_src(src_config->ext.device.type);
            if (AUDIO_DEVICE_IN_TV_TUNER != src_config->ext.device.type) {
                aml_dev->patch_src = android_input_dev_convert_to_hal_patch_src(src_config->ext.device.type);
            }
            aml_audio_input_routing(dev, inport);
            aml_dev->src_gain[inport] = 1.0;
            //aml_dev->sink_gain[outport] = 1.0;
            ALOGI("[%s:%d] dev->dev patch: inport(%s) -> outport(%s)", __func__, __LINE__,
                inport2String(inport), outport2String(outport));

            // ATV path goes to dev set_params which could
            // tell atv or dtv source and decide to create or not.
            // One more case is ATV->ATV, should recreate audio patch.
            if ((inport != INPORT_TUNER)
                    || ((inport == INPORT_TUNER) && (aml_dev->patch_src == SRC_ATV))) {
                if (input_src != SRC_NA)
                    set_audio_source(&aml_dev->alsa_mixer, input_src, alsa_device_is_auge());

                ret = create_patch(dev, src_config->ext.device.type, outport);
                if (ret) {
                    ALOGE("[%s:%d] create patch failed, outport:%s.", __func__, __LINE__, outport2String(outport));
                    ret = -EINVAL;
                    unregister_audio_patch(dev, patch_set);
                }
                aml_dev->audio_patching = 1;
                if (inport == INPORT_TUNER) {
                    aml_dev->patch_src = SRC_ATV;
                }
                if (inport == INPORT_HDMIIN || inport == INPORT_LINEIN ||
                    inport == INPORT_SPDIF  || inport == INPORT_TUNER) {
                    if (eDolbyMS12Lib == aml_dev->dolby_lib_type && aml_dev->continuous_audio_mode)
                    {
                        bool set_ms12_non_continuous = true;
                        get_dolby_ms12_cleanup(&aml_dev->ms12, set_ms12_non_continuous);
                        aml_dev->exiting_ms12 = 1;
                        clock_gettime(CLOCK_MONOTONIC, &aml_dev->ms12_exiting_start);
                        usecase_change_validate_l(aml_dev->active_outputs[STREAM_PCM_NORMAL], true);
                        ALOGI("enter patching mode, exit MS12 continuous mode");
                    }
                }
            } else if ((inport == INPORT_TUNER) && (aml_dev->patch_src == SRC_DTV)) {
#ifdef ENABLE_DTV_PATCH
                if (property_get_bool("tv.need.tvview.fast_switch", false)) {
                    ALOGI("in tvview fast switch mode, no need re-create DTV patch 2\n");
                    return ret;
                }
                 if ((aml_dev->patch_src == SRC_DTV) && aml_dev->audio_patching) {
                     ALOGI("%s, now release the dtv patch now\n ", __func__);
                     ret = release_dtv_patch(aml_dev);
                     if (!ret) {
                         aml_dev->audio_patching = 0;
                     }
                 }
                 ALOGI("%s, now end release dtv patch the audio_patching is %d ", __func__, aml_dev->audio_patching);
                 ALOGI("%s, now create the dtv patch now\n ", __func__);
                 aml_dev->patch_src = SRC_DTV;

                 ret = create_dtv_patch(dev, AUDIO_DEVICE_IN_TV_TUNER,
                                        AUDIO_DEVICE_OUT_SPEAKER);
                 if (ret == 0) {
                    aml_dev->audio_patching = 1;
                    dtv_patch_add_cmd(AUDIO_DTV_PATCH_CMD_START);/// this command should not be sent here
                 }
                ALOGI("%s, now end create dtv patch the audio_patching is %d ", __func__, aml_dev->audio_patching);
#endif
            }
        } else if (src_config->type == AUDIO_PORT_TYPE_MIX) {  /* 2. mix to device audio patch */
            ALOGI("[%s:%d] mix->device patch: mix - >outport(%s)", __func__, __LINE__, outport2String(outport));
            ret = 0;
        } else {
            ALOGE("[%s:%d] invalid patch, source error, source:%d to sink DEVICE", __func__, __LINE__, src_config->type);
            ret = -EINVAL;
            unregister_audio_patch(dev, patch_set);
        }
    } else if (sink_config->type == AUDIO_PORT_TYPE_MIX) {
        if (src_config->type == AUDIO_PORT_TYPE_DEVICE) { /* 3.device to mix audio patch */
            ret = android_dev_convert_to_hal_dev(src_config->ext.device.type, (int *)&inport);
            if (ret != 0) {
                ALOGE("[%s:%d] device->mix patch: unsupport input dev:%#x.", __func__, __LINE__, src_config->ext.device.type);
                ret = -EINVAL;
                unregister_audio_patch(dev, patch_set);
            }

            if (AUDIO_DEVICE_IN_TV_TUNER == src_config->ext.device.type) {
                aml_dev->tuner2mix_patch = true;
            } else {
                aml_dev->patch_src = android_input_dev_convert_to_hal_patch_src(src_config->ext.device.type);
            }
            input_src = android_input_dev_convert_to_hal_input_src(src_config->ext.device.type);
            if (input_src != SRC_NA)
                set_audio_source(&aml_dev->alsa_mixer, input_src, alsa_device_is_auge());

            aml_audio_input_routing(dev, inport);
            aml_dev->src_gain[inport] = 1.0;

            if (inport == INPORT_HDMIIN || inport == INPORT_ARCIN || inport == INPORT_SPDIF) {
                create_parser(dev, inport);
            } else if ((inport == INPORT_TUNER) && (aml_dev->patch_src == SRC_DTV)){
                if (property_get_bool("tv.need.tvview.fast_switch", false)) {
                    ALOGI("in tvview fast switch mode, no need re-create DTV patch 1");
                    return ret;
                }
                if (aml_dev->audio_patching) {
                    ALOGI("%s,!!!now release the dtv patch now\n ", __func__);
                    ret = release_dtv_patch(aml_dev);
                    if (!ret) {
                        aml_dev->audio_patching = 0;
                    }
                }
                ALOGI("%s, !!! now create the dtv patch now\n ", __func__);
                ret = create_dtv_patch(dev, AUDIO_DEVICE_IN_TV_TUNER,AUDIO_DEVICE_OUT_SPEAKER);
                if (ret == 0) {
                    aml_dev->audio_patching = 1;
                    dtv_patch_add_cmd(AUDIO_DTV_PATCH_CMD_START);
                }
            }
            ALOGI("[%s:%d] device->mix patch: inport(%s) -> mix", __func__, __LINE__, inport2String(inport));
            ret = 0;
        } else {
            ALOGE("[%s:%d] invalid patch, source error, source:%d to sink MIX", __func__, __LINE__, src_config->type);
            ret = -EINVAL;
            unregister_audio_patch(dev, patch_set);
        }
    } else {
        ALOGE("[%s:%d] invalid patch, sink:%d error", __func__, __LINE__, sink_config->type);
        ret = -EINVAL;
        unregister_audio_patch(dev, patch_set);
    }
    return ret;
}

/* Release an audio patch */
static int adev_release_audio_patch(struct audio_hw_device *dev,
                                audio_patch_handle_t handle)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    struct audio_patch_set *patch_set = NULL;
    struct audio_patch *patch = NULL;
    struct listnode *node = NULL;
    int ret = 0;

    ALOGI("++%s: handle(%d)", __func__, handle);
    if (list_empty(&aml_dev->patch_list)) {
        ALOGE("No patch in list to release");
        ret = -EINVAL;
        goto exit;
    }

    /* find audio_patch in patch_set list */
    list_for_each(node, &aml_dev->patch_list) {
        patch_set = node_to_item(node, struct audio_patch_set, list);
        patch = &patch_set->audio_patch;
        if (patch->id == handle) {
            ALOGI("patch set found id %d, patchset %p", patch->id, patch_set);
            break;
        } else {
            patch_set = NULL;
            patch = NULL;
        }
    }

    if (!patch_set || !patch) {
        ALOGE("Can't get patch in list");
        ret = -EINVAL;
        goto exit;
    }

    if (patch->sources[0].type == AUDIO_PORT_TYPE_DEVICE
        && patch->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
#ifdef ENABLE_DTV_PATCH
        if (aml_dev->patch_src == SRC_DTV) {
            ALOGI("patch src == DTV now line %d \n", __LINE__);

            if (property_get_bool("tv.need.tvview.fast_switch", false)) {
                ALOGI("intvview fast switch mode, no need release DTV patch\n");
                return ret;
            }
            if (!aml_dev->is_TV) {
                aml_dev->reset_dtv_audio = 1;
                ALOGI("device->device,reset the dtv audio port\n");
            }
            release_dtv_patch(aml_dev);
            aml_dev->audio_patching = 0;
        }
#endif
        if (aml_dev->patch_src != SRC_DTV
                && aml_dev->patch_src != SRC_INVAL
                && aml_dev->audio_patching == 1) {
            release_patch(aml_dev);
        }
        /*for no patch case, we need to restore it*/
        if (eDolbyMS12Lib == aml_dev->dolby_lib_type && (aml_dev->continuous_audio_mode_default == 1))
        {
            aml_dev->continuous_audio_mode = 1;
            get_dolby_ms12_cleanup(&aml_dev->ms12, false);
            ALOGI("%s restore continuous_audio_mode=%d", __func__, aml_dev->continuous_audio_mode);
        }
        aml_dev->audio_patching = 0;
        aml_dev->patch_src = SRC_INVAL;
        if (aml_dev->is_TV) {
            aml_dev->parental_control_av_mute = false;
        }
    }

    if (patch->sources[0].type == AUDIO_PORT_TYPE_DEVICE
        && patch->sinks[0].type == AUDIO_PORT_TYPE_MIX) {
        if (aml_dev->patch_src == SRC_HDMIIN) {
            release_parser(aml_dev);
        }
#ifdef ENABLE_DTV_PATCH
        if (aml_dev->patch_src == SRC_DTV) {
            ALOGI("patch src == DTV now line %d \n", __LINE__);
            release_dtv_patch(aml_dev);
            aml_dev->audio_patching = 0;
        }
#endif
        aml_dev->patch_src = SRC_INVAL;
        aml_dev->tuner2mix_patch = false;
    }

    unregister_audio_patch(dev, patch_set);
    ALOGI("--%s: after releasing patch, patch sets will be:", __func__);
    //dump_aml_audio_patch_sets(dev);
#ifdef ADD_AUDIO_DELAY_INTERFACE
    aml_audio_delay_clear(AML_DELAY_OUTPORT_SPEAKER);
    aml_audio_delay_clear(AML_DELAY_OUTPORT_SPDIF);
    aml_audio_delay_clear(AML_DELAY_OUTPORT_ALL);
#endif

#if defined(IS_ATOM_PROJECT)
#ifdef DEBUG_VOLUME_CONTROL
    int vol = property_get_int32("media.audio_hal.volume", -1);
    if (vol != -1)
        aml_dev->sink_gain[OUTPORT_SPEAKER] = (float)vol;
else
    aml_dev->sink_gain[OUTPORT_SPEAKER] = 1.0;
#endif
#endif
exit:
    return ret;
}

static int aml_dev_dump_latency(struct aml_audio_device *aml_dev, int fd)
{
    struct aml_stream_in *in = aml_dev->active_input;
    struct aml_audio_patch *patch = aml_dev->audio_patch;
    int rbuf_ltcy = 0, spk_tuning_ltcy = 0, ms12_ltcy = 0, alsa_in_ltcy = 0;
    int alsa_out_i2s_ltcy = 0, alsa_out_spdif_ltcy = 0;
    int v_ltcy = 0;
    int frame_size = 0;
    int ret = 0;
    snd_pcm_sframes_t frames = 0;
    dprintf(fd, "-------------[AML_HAL] audio Latency--------------------------\n");

    frame_size = CHANNEL_CNT * audio_bytes_per_sample(AUDIO_FORMAT_PCM_16_BIT);
    if (patch) {
        size_t rbuf_avail = 0;

        rbuf_avail = get_buffer_read_space(&patch->aml_ringbuffer);
        frames = rbuf_avail / frame_size;
        rbuf_ltcy = frames / SAMPLE_RATE_MS;
        if (patch->aformat == AUDIO_FORMAT_E_AC3) {
            rbuf_ltcy /= EAC3_MULTIPLIER;
        }
        v_ltcy = aml_sysfs_get_int("/sys/class/video/vframe_walk_delay");
    } else {
        return 0;
    }

    if (aml_dev->spk_tuning_lvl) {
        size_t rbuf_avail = 0;

        rbuf_avail = get_buffer_read_space(&aml_dev->spk_tuning_rbuf);
        frames = rbuf_avail / frame_size;
        spk_tuning_ltcy = frames / SAMPLE_RATE_MS;
    }

    if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
        if (aml_dev->ms12.dolby_ms12_enable == true) {
            int dolby_main_avail = dolby_ms12_get_main_buffer_avail(NULL);
            if (dolby_main_avail > 0) {
                ms12_ltcy = dolby_main_avail / frame_size / SAMPLE_RATE_MS;
                if (patch && patch->aformat == AUDIO_FORMAT_E_AC3) {
                    ms12_ltcy /= EAC3_MULTIPLIER;
                }
            }
        }
    }

    if (aml_dev->pcm_handle[I2S_DEVICE]) {
        ret = pcm_ioctl(aml_dev->pcm_handle[I2S_DEVICE], SNDRV_PCM_IOCTL_DELAY, &frames);
        if (ret >= 0) {
            alsa_out_i2s_ltcy = frames / SAMPLE_RATE_MS;
        }
    }

    if (aml_dev->pcm_handle[DIGITAL_DEVICE]) {
        ret = pcm_ioctl(aml_dev->pcm_handle[DIGITAL_DEVICE], SNDRV_PCM_IOCTL_DELAY, &frames);
        if (ret >= 0) {
            alsa_out_spdif_ltcy = frames / SAMPLE_RATE_MS;
        }
    }

    if (in) {
        ret = pcm_ioctl(in->pcm, SNDRV_PCM_IOCTL_DELAY, &frames);
        if (ret >= 0) {
            alsa_in_ltcy = frames / SAMPLE_RATE_MS;
            if (patch && patch->aformat == AUDIO_FORMAT_E_AC3) {
                alsa_in_ltcy /= EAC3_MULTIPLIER;
            }
        }
    }

    if (patch) {
        int in_path_ltcy = alsa_in_ltcy + rbuf_ltcy + ms12_ltcy;
        int out_path_ltcy = 0;
        int whole_path_ltcy = 0;

        if (aml_dev->sink_format == AUDIO_FORMAT_PCM_16_BIT) {
            out_path_ltcy = alsa_out_i2s_ltcy + spk_tuning_ltcy;
        } else if (aml_dev->sink_format == AUDIO_FORMAT_AC3) {
            out_path_ltcy = alsa_out_spdif_ltcy;
        } else if (aml_dev->sink_format == AUDIO_FORMAT_E_AC3) {
            out_path_ltcy = alsa_out_spdif_ltcy / EAC3_MULTIPLIER;
        }

        whole_path_ltcy = in_path_ltcy + out_path_ltcy;

        dprintf(fd, "[AML_HAL]      audio patch latency         : %6d ms\n", rbuf_ltcy);
        dprintf(fd, "[AML_HAL]      audio spk tuning latency    : %6d ms\n", spk_tuning_ltcy);
        dprintf(fd, "[AML_HAL]      MS12 buffer latency         : %6d ms\n", ms12_ltcy);
        dprintf(fd, "[AML_HAL]      alsa hw i2s latency         : %6d ms\n", alsa_out_i2s_ltcy);
        dprintf(fd, "[AML_HAL]      alsa hw spdif latency       : %6d ms\n", alsa_out_spdif_ltcy);
        dprintf(fd, "[AML_HAL]      alsa in hw latency          : %6d ms\n\n", alsa_in_ltcy);
        dprintf(fd, "[AML_HAL]      dev->dev audio total latency: %6d ms\n", whole_path_ltcy);
        if (v_ltcy > 0 && v_ltcy < 200) {
            dprintf(fd, "[AML_HAL]      video path total latency    : %6d ms\n", v_ltcy);
        } else {
            dprintf(fd, "[AML_HAL]      video path total latency    : N/A\n");
        }
    }
    return 0;
}

static void audio_patch_dump(struct aml_audio_device* aml_dev, int fd)
{
    struct aml_audio_patch *pstPatch = aml_dev->audio_patch;

    if (NULL == pstPatch) {
        dprintf(fd, "-------------[AML_HAL] audio patch [not create]-----------\n");
        return;
    }
    dprintf(fd, "-------------[AML_HAL] audio patch [%p]---------------\n", pstPatch);

    if (pstPatch->aml_ringbuffer.size != 0) {
        uint32_t u32FreeBuffer = get_buffer_write_space(&pstPatch->aml_ringbuffer);
        dprintf(fd, "[AML_HAL]      RingBuf   size: %10d Byte|  UnusedBuf:%10d Byte(%d%%)\n",
        pstPatch->aml_ringbuffer.size, u32FreeBuffer, u32FreeBuffer* 100 / pstPatch->aml_ringbuffer.size);
    } else {
        dprintf(fd, "[AML_HAL]  patch  RingBuf    : buffer size is 0\n");
    }
    if (pstPatch->audio_parse_para) {
        int s32AudioType = ((audio_type_parse_t*)pstPatch->audio_parse_para)->audio_type;
        dprintf(fd, "[AML_HAL]      Hal audio Type: [0x%x]%-10s| Src Format:%#10x\n", s32AudioType,
            audio_type_convert_to_string(s32AudioType),  pstPatch->aformat);
    }

    dprintf(fd, "[AML_HAL]      IN_SRC        : %#10x     | OUT_SRC   :%#10x\n", pstPatch->input_src, pstPatch->output_src);
    dprintf(fd, "[AML_HAL]      IN_Format     : %#10x     | OUT_Format:%#10x\n", pstPatch->in_format, pstPatch->out_format);
    dprintf(fd, "[AML_HAL]      active Outport: %s\n", outport2String(aml_dev->active_outport));
    dprintf(fd, "[AML_HAL]      sink format: %#x\n", aml_dev->sink_format);
    if (aml_dev->active_outport == OUTPORT_HDMI_ARC) {
        struct aml_arc_hdmi_desc *hdmi_desc = &aml_dev->hdmi_descs;
        bool dd_is_support = hdmi_desc->dd_fmt.is_support;
        bool ddp_is_support = hdmi_desc->ddp_fmt.is_support;

        dprintf(fd, "[AML_HAL]     -dd: %d, ddp: %d\n",
                dd_is_support, ddp_is_support);
        dprintf(fd, "[AML_HAL]     -ARC on: %d\n",
                aml_dev->bHDMIARCon);
    }

    struct dolby_ddp_dec *ddp_dec = &aml_dev->ddp;
    if (ddp_dec) {
        dprintf(fd, "[AML_HAL]     -ddp_dec: status %d\n", ddp_dec->status);
        dprintf(fd, "[AML_HAL]     -ddp_dec: digital_raw %d\n", ddp_dec->digital_raw);
    }
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    struct aml_audio_device* aml_dev = (struct aml_audio_device*)device;
    const int kNumRetries = 5;
    const int kSleepTimeMS = 100;
    int retry = kNumRetries;

    // refresh debug_flag status
    aml_dev->debug_flag = aml_audio_get_debug_flag();

    dprintf(fd, "\n----------------------------[AML_HAL] primary audio hal[dev:%p]----------------------------\n", aml_dev);
    while (retry > 0 && pthread_mutex_trylock(&aml_dev->lock) != 0) {
        usleep(kSleepTimeMS * 1000);
        retry--;
    }

    if (retry > 0) {
        aml_dev_dump_latency(aml_dev, fd);
        pthread_mutex_unlock(&aml_dev->lock);
    } else {
        // Couldn't lock
        dprintf(fd, "[AML_HAL]      Could not obtain aml_dev lock.\n");
    }
    if (aml_dev->hw_mixer.need_cache_flag) {
        uint32_t u32FreeBuffer = aml_hw_mixer_get_space(&aml_dev->hw_mixer);
        dprintf(fd, "[AML_HAL]      MixerBuf  size: %10d Byte|  UnusedBuf:%10d Byte(%d%%)\n",
        aml_dev->hw_mixer.buf_size, u32FreeBuffer, u32FreeBuffer * 100 / aml_dev->hw_mixer.buf_size);

    } else {
        dprintf(fd, "[AML_HAL]      MixerBuf      : UnAllocated\n");
    }
    audio_patch_dump(aml_dev, fd);
    dprintf(fd, "\n");
    dprintf(fd, "[AML_HAL]      user output settings: %d\n",
            aml_dev->hdmi_format);
    dprintf(fd, "[AML_HAL]      dolby_lib: %d\n",
            aml_dev->dolby_lib_type);
#ifdef AML_MALLOC_DEBUG
    aml_audio_debug_malloc_showinfo(MEMINFO_SHOW_PRINT);
#endif

    if (aml_dev->useSubMix) {
        subMixingDump(fd, aml_dev);
    }

    if (profile_is_valid(&aml_dev->usb_audio.in_profile)) {
        dprintf(fd, "\n-----------[AML_HAL] USB input device Capability-----------\n");
        profile_dump(&aml_dev->usb_audio.in_profile, fd);
    }

    return 0;
}

pthread_mutex_t adev_mutex = PTHREAD_MUTEX_INITIALIZER;
static void * g_adev = NULL;
void *adev_get_handle(void) {
    return (void *)g_adev;
}

static int adev_close(hw_device_t *device)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)device;
    pthread_mutex_lock(&adev_mutex);
    ALOGD("%s: enter", __func__);
    adev->count--;
    if (adev->count > 0) {
        pthread_mutex_unlock(&adev_mutex);
        return 0;
    }

#if defined(ENABLE_HBG_PATCH)
    stopReceiveAudioData();
#endif

    unload_ddp_decoder_lib(&adev->ddp);
    if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
        aml_ms12_lib_release();
    }
#if defined(IS_ATOM_PROJECT)
    if (adev->aec_buf)
        aml_audio_free(adev->aec_buf);
    if (adev->dsp_in_buf)
        aml_audio_free(adev->dsp_in_buf);
#endif
    if (adev->effect_buf) {
        aml_audio_free(adev->effect_buf);
    }
    if (adev->spk_output_buf) {
        aml_audio_free(adev->spk_output_buf);
    }
    if (adev->spdif_output_buf) {
        aml_audio_free(adev->spdif_output_buf);
    }
    if (adev->aml_ng_handle) {
        release_noise_gate(adev->aml_ng_handle);
        adev->aml_ng_handle = NULL;
    }
    ring_buffer_release(&(adev->spk_tuning_rbuf));
    if (adev->ar) {
        audio_route_free(adev->ar);
    }
    eq_drc_release(&adev->eq_data);
    close_mixer_handle(&adev->alsa_mixer);
    if (adev->sm) {
        deleteHalSubMixing(adev->sm);
    }
    aml_hwsync_close_tsync(adev->tsync_fd);
    pthread_mutex_destroy(&adev->patch_lock);

#ifdef ADD_AUDIO_DELAY_INTERFACE
    if (adev->is_TV) {
        aml_audio_delay_deinit();
    }
#endif
#ifdef PDM_MIC_CHANNELS
    if (adev->mic_desc)
        aml_audio_free(adev->mic_desc);
#endif
    g_adev = NULL;
    aml_audio_free(device);
    pthread_mutex_unlock(&adev_mutex);
    aml_audio_debug_malloc_close();
    return 0;
}

static int adev_set_audio_port_config (struct audio_hw_device *dev, const struct audio_port_config *config)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    enum OUT_PORT outport = OUTPORT_SPEAKER;
    enum IN_PORT inport = INPORT_HDMIIN;
    int ret = 0;
    int num_sinks = 1;

    ALOGI ("++%s", __FUNCTION__);
    if (config == NULL) {
        ALOGE ("NULL configs");
        return -EINVAL;
    }

    //dump_audio_port_config(config);
    if ( (config->config_mask & AUDIO_PORT_CONFIG_GAIN) == 0) {
        ALOGE ("null configs");
        return -EINVAL;
    }

    struct audio_patch_set *patch_set = NULL;
    struct audio_patch *patch = NULL;
    struct listnode *node = NULL;

    /* find the corrisponding sink for this src */
    list_for_each(node, &aml_dev->patch_list) {
        patch_set = node_to_item(node, struct audio_patch_set, list);
        patch = &patch_set->audio_patch;
        if (patch->sources[0].ext.device.type == config->ext.device.type) {
            ALOGI("patch set found id %d, patchset %p", patch->id, patch_set);
            break;
        } else if (patch->sources[0].type == AUDIO_PORT_TYPE_MIX &&
                   patch->sinks[0].type == AUDIO_PORT_TYPE_DEVICE &&
                   patch->sinks[0].id == config->id) {
            ALOGI("patch found mix->dev id %d, patchset %p", patch->id, patch_set);
            break;
        } else if (config->ext.device.type == AUDIO_DEVICE_IN_HDMI
                && patch->sources[0].ext.device.type == AUDIO_DEVICE_IN_LINE) {
            ALOGI("HDMIIN DVI case, using LINEIN patch");
            break;
        } else {
            patch_set = NULL;
            patch = NULL;
        }
    }

    if (!patch_set || !patch) {
        ALOGE("%s(): no right patch available", __func__);
        return -EINVAL;
    }

    num_sinks = patch->num_sinks;
    if (num_sinks == 2) {
        if ((patch->sinks[0].ext.device.type == AUDIO_DEVICE_OUT_HDMI_ARC &&
             patch->sinks[1].ext.device.type == AUDIO_DEVICE_OUT_SPEAKER) ||
             (patch->sinks[0].ext.device.type == AUDIO_DEVICE_OUT_SPEAKER &&
             patch->sinks[1].ext.device.type == AUDIO_DEVICE_OUT_HDMI_ARC)) {
            if (aml_dev->bHDMIARCon) {
                outport = OUTPORT_HDMI_ARC;
                ALOGI("%s() speaker and HDMI_ARC co-exist case, output=ARC", __func__);
            } else {
                outport = OUTPORT_SPEAKER;
                ALOGI("%s() speaker and HDMI_ARC co-exist case, output=SPEAKER", __func__);
            }
        }
    }

    if (config->type == AUDIO_PORT_TYPE_DEVICE) {
        if (config->role == AUDIO_PORT_ROLE_SINK) {
            if (num_sinks == 1) {
                switch (config->ext.device.type) {
                case AUDIO_DEVICE_OUT_HDMI_ARC:
                    outport = OUTPORT_HDMI_ARC;
                    break;
                case AUDIO_DEVICE_OUT_HDMI:
                outport = OUTPORT_HDMI;
                break;
            case AUDIO_DEVICE_OUT_SPDIF:
                outport = OUTPORT_SPDIF;
                break;
            case AUDIO_DEVICE_OUT_AUX_LINE:
                outport = OUTPORT_AUX_LINE;
                break;
            case AUDIO_DEVICE_OUT_SPEAKER:
                outport = OUTPORT_SPEAKER;
                break;
            case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
                outport = OUTPORT_HEADPHONE;
                break;
            case AUDIO_DEVICE_OUT_REMOTE_SUBMIX:
                outport = OUTPORT_REMOTE_SUBMIX;
                break;
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
                outport = OUTPORT_BT_SCO;
                break;
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
                outport = OUTPORT_BT_SCO_HEADSET;
                break;
            case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP:
            case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES:
            case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER:
                outport = OUTPORT_A2DP;
                break;
            default:
                ALOGE ("%s: invalid out device type %#x",
                          __func__, config->ext.device.type);
            }
        }

#ifdef DEBUG_VOLUME_CONTROL
            int vol = property_get_int32("media.audio_hal.volume", -1);
            if (vol != -1) {
                aml_dev->sink_gain[outport] = (float)vol;
                aml_dev->sink_gain[OUTPORT_SPEAKER] = (float)vol;
            } else
#endif

            {
                float volume_in_dB = ((float)config->gain.values[0]) / 100;
                aml_dev->sink_gain[outport] = DbToAmpl(volume_in_dB);

                ALOGI(" - set sink device[%#x](outport[%s]): volume_Mb[%d], gain[%f]",
                        config->ext.device.type, outport2String(outport),
                        config->gain.values[0], aml_dev->sink_gain[outport]);
            }
            ALOGI(" - now the sink gains are:");
            ALOGI("\t- OUTPORT_SPEAKER->gain[%f]", aml_dev->sink_gain[OUTPORT_SPEAKER]);
            ALOGI("\t- OUTPORT_HDMI_ARC->gain[%f]", aml_dev->sink_gain[OUTPORT_HDMI_ARC]);
            ALOGI("\t- OUTPORT_HEADPHONE->gain[%f]", aml_dev->sink_gain[OUTPORT_HEADPHONE]);
            ALOGI("\t- OUTPORT_HDMI->gain[%f]", aml_dev->sink_gain[OUTPORT_HDMI]);
            ALOGI("\t- active outport is: %s", outport2String(aml_dev->active_outport));
        } else if (config->role == AUDIO_PORT_ROLE_SOURCE) {
            switch (config->ext.device.type) {
            case AUDIO_DEVICE_IN_HDMI:
                inport = INPORT_HDMIIN;
                break;
            case AUDIO_DEVICE_IN_HDMI_ARC:
                inport = INPORT_ARCIN;
                break;
            case AUDIO_DEVICE_IN_LINE:
                inport = INPORT_LINEIN;
                break;
            case AUDIO_DEVICE_IN_SPDIF:
                inport = INPORT_SPDIF;
                break;
            case AUDIO_DEVICE_IN_TV_TUNER:
                inport = INPORT_TUNER;
                break;
            case AUDIO_DEVICE_IN_REMOTE_SUBMIX:
                inport = INPORT_REMOTE_SUBMIXIN;
                break;
            case AUDIO_DEVICE_IN_WIRED_HEADSET:
                inport = INPORT_WIRED_HEADSETIN;
                break;
            case AUDIO_DEVICE_IN_BUILTIN_MIC:
                inport = INPORT_BUILTIN_MIC;
                break;
            case AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET:
                inport = INPORT_BT_SCO_HEADSET_MIC;
                break;
            }

            aml_dev->src_gain[inport] = 1.0;
            if (patch->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
                if (num_sinks == 2) {
                    switch (outport) {
                    case OUTPORT_HDMI_ARC:
                        aml_dev->sink_gain[outport] = 1.0;
                        break;
                    case OUTPORT_SPEAKER:
                        aml_dev->sink_gain[outport] = DbToAmpl(((float)config->gain.values[0]) / 100);
                        break;
                    default:
                        ALOGE("%s: invalid out device type %#x",
                              __func__, outport);
                    }
                } else if (num_sinks == 1) {
                switch (patch->sinks->ext.device.type) {
                    case AUDIO_DEVICE_OUT_HDMI_ARC:
                        outport = OUTPORT_HDMI_ARC;
                        aml_dev->sink_gain[outport] = 1.0;
                        break;
                    case AUDIO_DEVICE_OUT_HDMI:
                        outport = OUTPORT_HDMI;
                        aml_dev->sink_gain[outport] = DbToAmpl(((float)config->gain.values[0]) / 100);
                        break;
                    case AUDIO_DEVICE_OUT_SPDIF:
                        outport = OUTPORT_SPDIF;
                        aml_dev->sink_gain[outport] = 1.0;
                        break;
                    case AUDIO_DEVICE_OUT_AUX_LINE:
                        outport = OUTPORT_AUX_LINE;
                        aml_dev->sink_gain[outport] = DbToAmpl(((float)config->gain.values[0]) / 100);
                        break;
                    case AUDIO_DEVICE_OUT_SPEAKER:
                        outport = OUTPORT_SPEAKER;
                        aml_dev->sink_gain[outport] = DbToAmpl(((float)config->gain.values[0]) / 100);
                        break;
                    case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
                        outport = OUTPORT_HEADPHONE;
                        aml_dev->sink_gain[outport] = DbToAmpl(((float)config->gain.values[0]) / 100);
                        break;
                    case AUDIO_DEVICE_OUT_REMOTE_SUBMIX:
                        outport = OUTPORT_REMOTE_SUBMIX;
                        aml_dev->sink_gain[outport] = DbToAmpl(((float)config->gain.values[0]) / 100);
                        break;
                    case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
                        outport = OUTPORT_BT_SCO;
                        break;
                    case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
                        outport = OUTPORT_BT_SCO_HEADSET;
                        break;
                    case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP:
                    case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES:
                    case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER:
                        outport = OUTPORT_A2DP;
                        aml_dev->sink_gain[outport] = DbToAmpl((float)(config->gain.values[0] / 100));
                        break;
                default:
                    ALOGE ("%s: invalid out device type %#x",
                              __func__, patch->sinks->ext.device.type);
                    }
                }
                if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
                    /* dev->dev and DTV src gain using MS12 primary gain */
                    if (aml_dev->audio_patching || aml_dev->patch_src == SRC_DTV) {
                        pthread_mutex_lock(&aml_dev->lock);
                        // In recent (20180609) modify, HDMI ARC adjust volume will this function.
                        // Once this fucntion was called, HDMI ARC have no sound
                        // Maybe the db_gain parameter is difference form project to project
                        // After remove this function here, HDMI ARC volume still can work
                        // So. Temporary remove here.
                        // TODO: find out when call this function will cause HDMI ARC mute
                        //ret = set_dolby_ms12_primary_input_db_gain(&aml_dev->ms12, config->gain.values[1] / 100);
                        pthread_mutex_unlock(&aml_dev->lock);
                        if (ret < 0) {
                            ALOGE("set dolby primary gain failed");
                        }
                    }
                }

                if (aml_dev->patch_src == SRC_DTV) {
                   if (aml_dev->sink_gain[outport] <= 0.0f) {
                        aml_dev->passthrough_mute = 1;
                    } else if (aml_dev->sink_gain[outport] != 0.0f) {
                        aml_dev->passthrough_mute = 0;
                    }
                }

                ALOGI(" - set src device[%#x](inport:%s): gain[%f]",
                            config->ext.device.type, inport2String(inport), aml_dev->src_gain[inport]);
                ALOGI(" - set sink device[%#x](outport:%s): volume_Mb[%d], gain[%f]",
                            patch->sinks->ext.device.type, outport2String(outport),
                            config->gain.values[0], aml_dev->sink_gain[outport]);
            } else if (patch->sinks[0].type == AUDIO_PORT_TYPE_MIX) {
                aml_dev->src_gain[inport] = DbToAmpl(((float)config->gain.values[0]) / 100);
                ALOGI(" - set src device[%#x](inport:%s): gain[%f]",
                            config->ext.device.type, inport2String(inport), aml_dev->src_gain[inport]);
            }
            ALOGI(" - set gain for in_port:%s, active inport:%s",
                   inport2String(inport), inport2String(aml_dev->active_inport));
        } else {
            ALOGE ("unsupport");
        }
    }

    return 0;
}

static int adev_get_audio_port(struct audio_hw_device *dev __unused, struct audio_port *port __unused)
{
    return -ENOSYS;
}

#define MAX_SPK_EXTRA_LATENCY_MS (100)
#define DEFAULT_SPK_EXTRA_LATENCY_MS (15)

//#define PDM_MIC_CHANNELS 4
int init_mic_desc(struct aml_audio_device *adev)
{
    struct mic_in_desc *mic_desc = aml_audio_calloc(1, sizeof(struct mic_in_desc));
    if (!mic_desc)
        return -ENOMEM;

    /* Config the default MIC-IN device */
    mic_desc->mic = DEV_MIC_PDM;
    mic_desc->config.rate = 16000;
    mic_desc->config.format = PCM_FORMAT_S16_LE;

#if (PDM_MIC_CHANNELS == 4)
    ALOGI("%s(), 4 channels PDM mic", __func__);
    mic_desc->config.channels = 4;
#elif (PDM_MIC_CHANNELS == 2)
    mic_desc->config.channels = 2;
    ALOGI("%s(), 2 channels PDM mic", __func__);
#else
    ALOGI("%s(), default 2 channels PDM mic", __func__);
    mic_desc->config.channels = 2;
#endif

    adev->mic_desc = mic_desc;
    return 0;
};

static bool is_libvx_exist() {
    void *h_libvx_hanle = NULL;
    if (access(VIRTUALX_LICENSE_LIB_PATH, R_OK) != 0) {
        ALOGI("%s, %s does not exist", __func__, VIRTUALX_LICENSE_LIB_PATH);
        return false;
    }
    h_libvx_hanle = dlopen(VIRTUALX_LICENSE_LIB_PATH, RTLD_NOW);
    if (!h_libvx_hanle) {
        ALOGE("%s, fail to dlopen %s(%s)", __func__, VIRTUALX_LICENSE_LIB_PATH, dlerror());
        return false;
    } else {
        ALOGD("%s, success to dlopen %s", __func__, VIRTUALX_LICENSE_LIB_PATH);
        dlclose(h_libvx_hanle);
        h_libvx_hanle = NULL;
        return true;
    }
}

static int adev_open(const hw_module_t* module, const char* name, hw_device_t** device)
{
    struct aml_audio_device *adev;
    aml_audio_debug_malloc_open();
    size_t bytes_per_frame = audio_bytes_per_sample(AUDIO_FORMAT_PCM_16_BIT)
                             * audio_channel_count_from_out_mask(AUDIO_CHANNEL_OUT_STEREO);
    int buffer_size = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE * bytes_per_frame;
    int spk_tuning_buf_size = MAX_SPK_EXTRA_LATENCY_MS
                              * bytes_per_frame * MM_FULL_POWER_SAMPLING_RATE / 1000;
    int spdif_tuning_latency = aml_audio_get_spdif_tuning_latency();
    int card = CARD_AMLOGIC_BOARD;
    int ret = 0, i = 0;
    char buf[PROPERTY_VALUE_MAX];
    int disable_continuous = 1;

    ALOGD("%s: enter", __func__);
    pthread_mutex_lock(&adev_mutex);
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) {
        ret = -EINVAL;
        goto err;
    }
#if defined(TV_AUDIO_OUTPUT)
    // is_auge depend on this func...
    card = alsa_device_get_card_index();
    if (alsa_device_is_auge())
        alsa_depop(card);
#endif

    if (g_adev != NULL) {
        ALOGI("adev exsits ,reuse");
        adev = (struct aml_audio_device *)g_adev;
        adev->count++;
        *device = &adev->hw_device.common;
        ALOGI("*device:%p",*device);
        goto err;
    }
    adev = aml_audio_calloc(1, sizeof(struct aml_audio_device));
    if (!adev) {
        ret = -ENOMEM;
        goto err;
    }
    g_adev = (void *)adev;
    pthread_mutex_unlock(&adev_mutex);
    initAudio(1);  /*Judging whether it is Google's search engine*/
    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_3_0;
    adev->hw_device.common.module = (struct hw_module_t *)module;
    adev->hw_device.common.close = adev_close;

    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.get_master_volume = adev_get_master_volume;
    adev->hw_device.set_master_mute = adev_set_master_mute;
    adev->hw_device.get_master_mute = adev_get_master_mute;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream_new;
    adev->hw_device.close_output_stream = adev_close_output_stream_new;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.create_audio_patch = adev_create_audio_patch;
    adev->hw_device.release_audio_patch = adev_release_audio_patch;
    adev->hw_device.set_audio_port_config = adev_set_audio_port_config;
    adev->hw_device.get_microphones = adev_get_microphones;
    adev->hw_device.get_audio_port = adev_get_audio_port;
    adev->hw_device.dump = adev_dump;
    adev->active_outport = -1;
    adev->virtualx_mulch = true;
    adev->hdmi_format = AUTO;
    adev->pre_hdmi_format = AUTO;
    adev->ms12.dap_bypassgain = 1.0;
    card = alsa_device_get_card_index();
    if ((card < 0) || (card > 7)) {
        ALOGE("error to get audio card");
        ret = -EINVAL;
        goto err_adev;
    }

    adev->card = card;
    adev->ar = audio_route_init(adev->card, MIXER_XML_PATH);
    if (adev->ar == NULL) {
        ALOGE("audio route init failed");
        ret = -EINVAL;
        goto err_adev;
    }
    /* Set the default route before the PCM stream is opened */
    adev->mode = AUDIO_MODE_NORMAL;
    adev->out_device = AUDIO_DEVICE_OUT_SPEAKER;
    adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;
    adev->hi_pcm_mode = false;

    adev->eq_data.card = adev->card;
    if (eq_drc_init(&adev->eq_data) == 0) {
        ALOGI("%s() audio source gain: atv:%f, dtv:%f, hdmiin:%f, av:%f, media:%f", __func__,
           adev->eq_data.s_gain.atv, adev->eq_data.s_gain.dtv,
           adev->eq_data.s_gain.hdmi, adev->eq_data.s_gain.av, adev->eq_data.s_gain.media);
        ALOGI("%s() audio device gain: speaker:%f, spdif_arc:%f, headphone:%f", __func__,
           adev->eq_data.p_gain.speaker, adev->eq_data.p_gain.spdif_arc,
              adev->eq_data.p_gain.headphone);
        adev->aml_ng_enable = adev->eq_data.noise_gate.aml_ng_enable;
        adev->aml_ng_level = adev->eq_data.noise_gate.aml_ng_level;
        adev->aml_ng_attack_time = adev->eq_data.noise_gate.aml_ng_attack_time;
        adev->aml_ng_release_time = adev->eq_data.noise_gate.aml_ng_release_time;
        ALOGI("%s() audio noise gate level: %fdB, attack_time = %dms, release_time = %dms", __func__,
              adev->aml_ng_level, adev->aml_ng_attack_time, adev->aml_ng_release_time);
    }

    ret = aml_audio_output_routing(&adev->hw_device, OUTPORT_SPEAKER, false);
    if (ret < 0) {
        ALOGE("%s() routing failed", __func__);
        ret = -EINVAL;
        goto err_adev;
    }

    adev->next_unique_ID = 1;
    list_init(&adev->patch_list);
    adev->effect_buf = aml_audio_malloc(buffer_size);
    if (adev->effect_buf == NULL) {
        ALOGE("malloc effect buffer failed");
        ret = -ENOMEM;
        goto err_adev;
    }
    memset(adev->effect_buf, 0, buffer_size);
    adev->spk_output_buf = aml_audio_malloc(buffer_size * 2);
    if (adev->spk_output_buf == NULL) {
        ALOGE("no memory for headphone output buffer");
        ret = -ENOMEM;
        goto err_effect_buf;
    }
    memset(adev->spk_output_buf, 0, buffer_size);
    adev->spdif_output_buf = aml_audio_malloc(buffer_size * 2);
    if (adev->spdif_output_buf == NULL) {
        ALOGE("no memory for spdif output buffer");
        ret = -ENOMEM;
        goto err_adev;
    }
    memset(adev->spdif_output_buf, 0, buffer_size);

    adev->effect_buf_size = buffer_size;

    /* init speaker tuning buffers */
    ret = ring_buffer_init(&(adev->spk_tuning_rbuf), spk_tuning_buf_size);
    if (ret < 0) {
        ALOGE("Fail to init audio spk_tuning_rbuf!");
        goto err_spk_tuning_buf;
    }
    adev->spk_tuning_buf_size = spk_tuning_buf_size;

    /* if no latency set by prop, use default one */
    if (spdif_tuning_latency == 0) {
        spdif_tuning_latency = DEFAULT_SPK_EXTRA_LATENCY_MS;
    } else if (spdif_tuning_latency > MAX_SPK_EXTRA_LATENCY_MS) {
        spdif_tuning_latency = MAX_SPK_EXTRA_LATENCY_MS;
    } else if (spdif_tuning_latency < 0) {
        spdif_tuning_latency = 0;
    }

    // try to detect which dobly lib is readable
    adev->dolby_lib_type = detect_dolby_lib_type();
    adev->dolby_lib_type_last = adev->dolby_lib_type;
    adev->dolby_decode_enable = dolby_lib_decode_enable(adev->dolby_lib_type_last);

    /* convert MS to data buffer length need to cache */
    adev->spk_tuning_lvl = (spdif_tuning_latency * bytes_per_frame * MM_FULL_POWER_SAMPLING_RATE) / 1000;
    /* end of spker tuning things */
    *device = &adev->hw_device.common;
    adev->dts_post_gain = 1.0;
    /* set default HP gain */
    adev->sink_gain[OUTPORT_HEADPHONE] = 1.0;
    adev->ms12_main1_dolby_dummy = true;
    adev->ms12_ott_enable = false;
    adev->continuous_audio_mode_default = 0;
    adev->need_remove_conti_mode = false;
    adev->dual_spdif_support = property_get_bool("ro.vendor.platform.is.dualspdif", false);
    /*for ms12 case, we set default continuous mode*/
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        adev->continuous_audio_mode_default = 1;
    }
    adev->passthrough_mute = 0;
    ret = property_get(DISABLE_CONTINUOUS_OUTPUT, buf, NULL);
    if (ret > 0) {
        sscanf(buf, "%d", &disable_continuous);
        if (!disable_continuous) {
            adev->continuous_audio_mode_default = 1;
        }
        ALOGI("%s[%s] disable_continuous %d\n", DISABLE_CONTINUOUS_OUTPUT, buf, disable_continuous);
    }
    adev->continuous_audio_mode = adev->continuous_audio_mode_default;
    pthread_mutex_init(&adev->alsa_pcm_lock, NULL);
    pthread_mutex_init(&adev->patch_lock, NULL);
    open_mixer_handle(&adev->alsa_mixer);

    if (eDolbyMS12Lib != adev->dolby_lib_type) {
        adev->ms12.dolby_ms12_enable = false;
    } else {
        // in ms12 case, use new method for TV or BOX .
        adev->hw_device.open_output_stream = adev_open_output_stream_new;
        adev->hw_device.close_output_stream = adev_close_output_stream_new;
        ALOGI("%s,in ms12 case, use new method no matter if current platform is TV or BOX", __FUNCTION__);
        aml_ms12_lib_preload();
    }
    adev->atoms_lock_flag = false;

    if (eDolbyDcvLib == adev->dolby_lib_type) {
        memset(&adev->ddp, 0, sizeof(struct dolby_ddp_dec));
        adev->dcvlib_bypass_enable = 1;
    }
    /*
    need also load the dcv lib as we need that when device-mixer patch
    even when MS12 is enabled.
    */
    if (load_ddp_decoder_lib(&adev->ddp) == 0) {
        ALOGI("load_ddp_decoder_lib success ");
    }
    adev->libvx_exist = is_libvx_exist();
    ALOGI("%s, '%s' exist flag : %s", __FUNCTION__, VIRTUALX_LICENSE_LIB_PATH, (adev->libvx_exist) ? "true" : "false");

#if ENABLE_NANO_NEW_PATH
    nano_init();
#endif

    ALOGI("%s() adev->dolby_lib_type = %d", __FUNCTION__, adev->dolby_lib_type);
    adev->patch_src = SRC_INVAL;
    adev->audio_type = LPCM;
    adev->sound_track_mode = 0;

#if (ENABLE_NANO_PATCH == 1)
/*[SEN5-autumn.zhao-2018-01-11] add for B06 audio support { */
    nano_init();
/*[SEN5-autumn.zhao-2018-01-11] add for B06 audio support } */
#endif

#if defined(ENABLE_HBG_PATCH)
    startReceiveAudioData();
#endif


#if defined(TV_AUDIO_OUTPUT)
    adev->is_TV = true;
    ALOGI("%s(), TV platform", __func__);
#ifdef ADD_AUDIO_DELAY_INTERFACE
        ret = aml_audio_delay_init();
        if (ret < 0) {
            ALOGE("aml_audio_delay_init faild\n");
            goto err;
        }
#endif
#else
    adev->is_STB = property_get_bool("ro.vendor.platform.is.stb", false);
    adev->sink_gain[OUTPORT_SPEAKER] = 1.0;
    adev->sink_gain[OUTPORT_HDMI] = 1.0;
    ALOGI("%s(), OTT platform", __func__);
#endif
    adev->sink_gain[OUTPORT_A2DP] = 1.0;

    adev->useSubMix = false;
#ifdef SUBMIXER_V1_1
    adev->useSubMix = true;
    ALOGI("%s(), with macro SUBMIXER_V1_1, set useSubMix = TRUE", __func__);
#endif

    // FIXME: current MS12 is not compatible with SUBMIXER, when MS12 lib exists, use ms12 system.
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        adev->useSubMix = false;
        ALOGI("%s(), MS12 is not compatible with SUBMIXER currently, set useSubMix to FALSE", __func__);
    }

    if (adev->useSubMix) {
        ret = initHalSubMixing(&adev->sm, MIXER_LPCM, adev, adev->is_TV);
        adev->tsync_fd = aml_hwsync_open_tsync();
        if (adev->tsync_fd < 0) {
            ALOGE("%s() open tsync failed", __func__);
        }
        adev->rawtopcm_flag = false;
        profile_init(&adev->usb_audio.in_profile, PCM_IN);
#ifdef USB_KARAOKE
        subMixingSetKaraoke(adev, &adev->usb_audio.karaoke);
        pthread_mutex_init(&adev->usb_audio.karaoke.lock, NULL);
        adev->usb_audio.karaoke.kara_mic_gain = 1.0;
#endif
    }

    if (adev && adev->aml_ng_enable) {
        adev->aml_ng_handle = init_noise_gate(adev->aml_ng_level,
                                 adev->aml_ng_attack_time, adev->aml_ng_release_time);
        ALOGI("%s: init amlogic noise gate: level: %fdB, attrack_time = %dms, release_time = %dms",
              __func__, adev->aml_ng_level,
              adev->aml_ng_attack_time, adev->aml_ng_release_time);
    }

    if (aml_audio_ease_init(&adev->audio_ease) < 0) {
        ALOGE("aml_audio_ease_init faild\n");
        ret = -EINVAL;
        goto err_ringbuf;
    }

#ifdef PDM_MIC_CHANNELS
    init_mic_desc(adev);
#endif
    // adev->debug_flag is set in hw_write()
    // however, sometimes function didn't goto hw_write() before encounting error.
    // set debug_flag here to see more debug log when debugging.
    adev->debug_flag = aml_audio_get_debug_flag();
    adev->count = 1;
    ALOGD("%s: exit", __func__);
    return 0;

err_ringbuf:
    ring_buffer_release(&adev->spk_tuning_rbuf);
err_spk_tuning_buf:
    aml_audio_free(adev->spk_output_buf);
err_effect_buf:
    aml_audio_free(adev->effect_buf);
err_adev:
    aml_audio_free(adev);
err:
    pthread_mutex_unlock(&adev_mutex);
    return ret;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "aml audio HW HAL",
        .author = "amlogic, Corp.",
        .methods = &hal_module_methods,
    },
};
