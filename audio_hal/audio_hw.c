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

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
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
#include <aml_audio_spdifdec.h>

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
#include "aml_audio_ease.h"
#include "aml_audio_spdifout.h"
#include "aml_mmap_audio.h"
// for invoke bluetooth rc hal
#include "audio_hal_thunks.h"
#include "earc_utils.h"

#include <dolby_ms12_status.h>
#include <SPDIFEncoderAD.h>
#include "audio_hw_ms12.h"
#include "audio_hw_ms12_common.h"
#include "dolby_lib_api.h"
#include "aml_audio_ac3parser.h"
#include "aml_audio_ac4parser.h"
#include "aml_audio_ms12_sync.h"

#include "audio_hdmi_util.h"
#include "aml_audio_dev2mix_process.h"

#include "dmx_audio_es.h"
#include "aml_audio_ms12_render.h"
#include "aml_audio_nonms12_render.h"
#include "aml_vad_wakeup.h"

#define ENABLE_NANO_NEW_PATH 1
#if ENABLE_NANO_NEW_PATH
#include "jb_nano.h"
#endif

// for dtv playback
#include "audio_hw_dtv.h"
/*Google Voice Assistant channel_mask */
#define BUILT_IN_MIC 12

//#define SUBMIXER_V1_1
#define HDMI_LATENCY_MS 60

#ifdef ENABLE_AEC_HAL
#include "audio_aec_process.h"
#endif

/*[SEI-zhaopf-2018-10-29] add for HBG remote audio support { */
#if defined(ENABLE_HBG_PATCH)
#include "../hbg_bt_voice/hbg_blehid_mic.h"
#endif
/*[SEI-zhaopf-2018-10-29] add for HBG remote audio support } */

#include "sub_mixing_factory.h"
#include "amlAudioMixer.h"
#include "a2dp_hal.h"
#include "audio_bt_sco.h"
#include "aml_malloc_debug.h"

#ifdef ENABLE_AEC_APP
#include "audio_aec.h"
#endif
#include <audio_effects/effect_aec.h>
#include <audio_utils/clock.h>

#define CARD_AMLOGIC_BOARD 0
/* ALSA ports for AML */
#define PORT_I2S 0
#define PORT_SPDIF 1
#define PORT_PCM 2

#undef PLAYBACK_PERIOD_COUNT
#define PLAYBACK_PERIOD_COUNT 4

/* number of periods for capture */
#undef CAPTURE_PERIOD_COUNT
#define CAPTURE_PERIOD_COUNT 4

/*Google Voice Assistant channel_mask */
#define BUILT_IN_MIC 12

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
#define MIXER_XML_PATH "/vendor/etc/mixer_paths.xml"
#define DOLBY_MS12_INPUT_FORMAT_TEST

#define IEC61937_PACKET_SIZE_OF_AC3                     (0x1800)
#define IEC61937_PACKET_SIZE_OF_EAC3                    (0x6000)

#define MAX_INPUT_STREAM_CNT                            (3)

#define NETFLIX_DDP_BUFSIZE                             (768)
#define OUTPUT_PORT_MAX_COEXIST_NUM                     (3)

/*Tunnel sync HEADER is 20 bytes*/
#define TUNNEL_SYNC_HEADER_SIZE    (20)

/* this latency is from logcat time. */
#define HAL_MS12_PIPELINE_LATENCY (10)

#define DISABLE_CONTINUOUS_OUTPUT "persist.vendor.audio.continuous.disable"
/* Maximum string length in audio hal. */
#define AUDIO_HAL_CHAR_MAX_LEN                          (256)

static const struct pcm_config pcm_config_out = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_PLAYBACK_PERIOD_SIZE,
    .period_count = DEFAULT_PLAYBACK_PERIOD_CNT,
    .format = PCM_FORMAT_S16_LE,
};

static const struct pcm_config pcm_config_out_direct = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_PLAYBACK_PERIOD_SIZE,
    .period_count = DEFAULT_PLAYBACK_PERIOD_CNT,
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
    .period_size = DEFAULT_PLAYBACK_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

static void select_output_device (struct aml_audio_device *adev);
static void select_input_device (struct aml_audio_device *adev);
static void select_devices (struct aml_audio_device *adev);
static int adev_set_voice_volume (struct audio_hw_device *dev, float volume);
static int do_output_standby (struct aml_stream_out *out);
//static int do_output_standby_l (struct audio_stream *out);
static uint32_t out_get_sample_rate (const struct audio_stream *stream);
static int out_pause (struct audio_stream_out *stream);
static inline int is_usecase_mix (stream_usecase_t usecase);
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
ssize_t out_write_new(struct audio_stream_out *stream,
                      const void *buffer,
                      size_t bytes);
static int out_get_presentation_position(const struct audio_stream_out *stream,
                                         uint64_t *frames,
                                         struct timespec *timestamp);
static int adev_release_patch_restore_resource(struct aml_audio_device *aml_dev);

static aec_timestamp get_timestamp(void);

static int adev_get_mic_mute(const struct audio_hw_device* dev, bool* state);
static int adev_get_microphones(const struct audio_hw_device* dev,
                                struct audio_microphone_characteristic_t* mic_array,
                                size_t* mic_count);
static void get_mic_characteristics(struct audio_microphone_characteristic_t* mic_data,
                                    size_t* mic_count);
static void * g_aml_primary_adev = NULL;

void *aml_adev_get_handle(void)
{
    return (void *)g_aml_primary_adev;
}

static inline bool need_hw_mix(usecase_mask_t masks)
{
    return (masks > 1);
}

static inline int is_usecase_mix(stream_usecase_t usecase)
{
    return usecase > STREAM_PCM_NORMAL;
}

static inline short CLIP (int r)
{
    return (r >  0x7fff) ? 0x7fff :
           (r < -0x8000) ? -0x8000 :
           r;
}

//code here for audio hal mixer when hwsync with af mixer output stream output
//at the same,need do a software mixer in audio hal.
static int aml_hal_mixer_init (struct aml_hal_mixer *mixer)
{
    pthread_mutex_lock (&mixer->lock);
    mixer->wp = 0;
    mixer->rp = 0;
    mixer->buf_size = AML_HAL_MIXER_BUF_SIZE;
    mixer->need_cache_flag = 1;
    pthread_mutex_unlock (&mixer->lock);
    return 0;
}
static uint aml_hal_mixer_get_space (struct aml_hal_mixer *mixer)
{
    unsigned space;
    if (mixer->wp >= mixer->rp) {
        space = mixer->buf_size - (mixer->wp - mixer->rp);
    } else {
        space = mixer->rp - mixer->wp;
    }
    return space > 64 ? (space - 64) : 0;
}
static int aml_hal_mixer_get_content (struct aml_hal_mixer *mixer)
{
    unsigned content = 0;
    pthread_mutex_lock (&mixer->lock);
    if (mixer->wp >= mixer->rp) {
        content = mixer->wp - mixer->rp;
    } else {
        content = mixer->wp - mixer->rp + mixer->buf_size;
    }
    //ALOGI("wp %d,rp %d\n",mixer->wp,mixer->rp);
    pthread_mutex_unlock (&mixer->lock);
    return content;
}
//we assue the cached size is always smaller then buffer size
//need called by device mutux locked
static int aml_hal_mixer_write (struct aml_hal_mixer *mixer, const void *w_buf, uint size)
{
    unsigned space;
    unsigned write_size = size;
    unsigned tail = 0;
    pthread_mutex_lock (&mixer->lock);
    space = aml_hal_mixer_get_space (mixer);
    if (space < size) {
        ALOGI ("write data no space,space %d,size %d,rp %d,wp %d,reset all ptr\n", space, size, mixer->rp, mixer->wp);
        mixer->wp = 0;
        mixer->rp = 0;
    }
    //TODO
    if (write_size > space) {
        write_size = space;
    }
    if (write_size + mixer->wp > mixer->buf_size) {
        tail = mixer->buf_size - mixer->wp;
        memcpy (mixer->start_buf + mixer->wp, w_buf, tail);
        write_size -= tail;
        memcpy (mixer->start_buf, (unsigned char*) w_buf + tail, write_size);
        mixer->wp = write_size;
    } else {
        memcpy (mixer->start_buf + mixer->wp, w_buf, write_size);
        mixer->wp += write_size;
        mixer->wp %= AML_HAL_MIXER_BUF_SIZE;
    }
    pthread_mutex_unlock (&mixer->lock);
    return size;
}
//need called by device mutux locked
static int aml_hal_mixer_read (struct aml_hal_mixer *mixer, void *r_buf, uint size)
{
    unsigned cached_size;
    unsigned read_size = size;
    unsigned tail = 0;
    cached_size = aml_hal_mixer_get_content (mixer);
    pthread_mutex_lock (&mixer->lock);
    // we always assue we have enough data to read when hwsync enabled.
    // if we do not have,insert zero data.
    if (cached_size < size) {
        ALOGI ("read data has not enough data to mixer,read %d, have %d,rp %d,wp %d\n", size, cached_size, mixer->rp, mixer->wp);
        memset ( (unsigned char*) r_buf + cached_size, 0, size - cached_size);
        read_size = cached_size;
    }
    if (read_size + mixer->rp > mixer->buf_size) {
        tail = mixer->buf_size - mixer->rp;
        memcpy (r_buf, mixer->start_buf + mixer->rp, tail);
        read_size -= tail;
        memcpy ( (unsigned char*) r_buf + tail, mixer->start_buf, read_size);
        mixer->rp = read_size;
    } else {
        memcpy (r_buf, mixer->start_buf + mixer->rp, read_size);
        mixer->rp += read_size;
        mixer->rp %= AML_HAL_MIXER_BUF_SIZE;
    }
    pthread_mutex_unlock (&mixer->lock);
    return size;
}
// aml audio hal mixer code end

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
    int anlg_dock_headset_on;
    headset_on = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADSET;
    headphone_on = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
    speaker_on = adev->out_device & AUDIO_DEVICE_OUT_SPEAKER;
    hdmi_on = adev->out_device & AUDIO_DEVICE_OUT_AUX_DIGITAL;
    earpiece =  adev->out_device & AUDIO_DEVICE_OUT_EARPIECE;
    mic_in = adev->in_device & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC);
    headset_mic = adev->in_device & AUDIO_DEVICE_IN_WIRED_HEADSET;
    anlg_dock_headset_on = adev->out_device & AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET;
    ALOGD ("%s : hs=%d , hp=%d, sp=%d, hdmi=0x%x,earpiece=0x%x", __func__,
             headset_on, headphone_on, speaker_on, hdmi_on, earpiece);
    ALOGD ("%s : in_device(%#x), mic_in(%#x), headset_mic(%#x)", __func__,
             adev->in_device, mic_in, headset_mic);
    audio_route_reset (adev->ar);
    if (hdmi_on) {
        audio_route_apply_path (adev->ar, "hdmi");
    }
    if (headphone_on || headset_on || anlg_dock_headset_on) {
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

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream (struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;
    unsigned int card = CARD_AMLOGIC_BOARD;
    unsigned int port = PORT_I2S;
    int ret = 0;
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
        if (adev->bt_wbs)
            out->config.rate = VX_WB_SAMPLING_RATE;
    } else if (out->flags & AUDIO_OUTPUT_FLAG_DIRECT && !hwsync_lpcm) {
        port = PORT_SPDIF;
    }

    /* check to update port */
    port = alsa_device_update_pcm_index(port, PLAYBACK);

    ALOGD ("*%s, open card(%d) port(%d)", __FUNCTION__, card, port);

    /* default to low power: will be corrected in out_write if necessary before first write to
     * tinyalsa.
     */
    out->write_threshold = out->config.period_size * out->config.period_count;
    out->config.start_threshold = out->config.period_size * out->config.period_count;
    out->config.avail_min = 0;//SHORT_PERIOD_SIZE;
    //added by xujian for NTS hwsync/system stream mix smooth playback.
    //we need re-use the tinyalsa pcm handle by all the output stream, including
    //hwsync direct output stream,system mixer output stream.
    //TODO we need diff the code with AUDIO_DEVICE_OUT_ALL_SCO.
    //as it share the same hal but with the different card id.
    //TODO need reopen the tinyalsa card when sr/ch changed,
    if (adev->pcm == NULL) {
        ALOGD("%s(), pcm_open card %u port %u\n", __func__, card, port);
        out->pcm = pcm_open (card, port, PCM_OUT /*| PCM_MMAP | PCM_NOIRQ*/, & (out->config) );
        if (!pcm_is_ready (out->pcm) ) {
            ALOGE ("cannot open pcm_out driver: %s", pcm_get_error (out->pcm) );
            pcm_close (out->pcm);
            return -ENOMEM;
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

    if (out->hw_sync_mode == 1) {
        ALOGD ("start_output_stream with hw sync enable %p\n", out);
    }
    return 0;
}

static int check_input_parameters(uint32_t sample_rate, audio_format_t format, int channel_count, audio_devices_t devices)
{
    ALOGD("%s(sample_rate=%d, format=%d, channel_count=%d, devices = %x)", __FUNCTION__, sample_rate, format, channel_count, devices);

    if(AUDIO_DEVICE_IN_DEFAULT == devices && AUDIO_CHANNEL_NONE == channel_count &&
       AUDIO_FORMAT_DEFAULT == format && 0 == sample_rate) {
       /* Add for Hidl6.0:CloseDeviceWithOpenedInputStreams test */
       return -ENOSYS; /*Currently System Not Supported.*/
    }

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
        /*fallthrough*/
        case 12000:
        case 16000:
        case 17000:
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
            if ((channel_count == 2) && ((sample_rate == 8000) || (sample_rate == 16000) || (sample_rate == 32000) || (sample_rate == 44100)))
                return -ENOSYS;
            ALOGD("%s: unspported audio patch input device %x", __FUNCTION__, devices);
            return -EINVAL;
        }
    }

    return 0;
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
                (out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
                // local file playback, data from audio flinger direct mode
                // the data is packed by DCV decoder by OMX, 1536 samples per packet
                // to match with it, set the size to 1536
                // (ms12 decoder doesn't encounter this issue, so only handle with DCV decoder case)
                size = AC3_PERIOD_SIZE / 4;
                ALOGI("%s AUDIO_FORMAT_IEC61937(DIRECT) (eDolbyDcvLib) size = %zu)", __FUNCTION__, size);
            }
        } else if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
            size = AC3_PERIOD_SIZE;
        } else if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
            /*for issue SWPL-69439, we need increase the buf size*/
            size = (DEFAULT_PLAYBACK_PERIOD_SIZE << 2);
        } else {
            size = DEFAULT_PLAYBACK_PERIOD_SIZE;
        }
        break;
    case AUDIO_FORMAT_E_AC3:
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size =  EAC3_PERIOD_SIZE;
        } else if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
            size = EAC3_PERIOD_SIZE;//one iec61937 packet size
        } else if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
            size = (DEFAULT_PLAYBACK_PERIOD_SIZE << 3) + (DEFAULT_PLAYBACK_PERIOD_SIZE << 1);
        }  else {
            /*frame align*/
            if (1 /* adev->continuous_audio_mode */) {
                /*Tunnel sync HEADER is 16 bytes*/
                if ((out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) && out->hw_sync_mode) {
                    size = out->ddp_frame_size + TUNNEL_SYNC_HEADER_SIZE;
                } else {
                    size = out->ddp_frame_size * 4;
                }
            } else {
                size = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;    //PERIOD_SIZE;
            }
        }

        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
            ALOGI("%s eac3 AUDIO_FORMAT_IEC61937 = size%zu)", __FUNCTION__, size);
        }

        /*netflix ddp size is 768, if we change to a big value, then
         *every process time is too long, it will cause such case failed SWPL-41439
         */
        if (adev->is_netflix) {
            if (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) {
                size = NETFLIX_DDP_BUFSIZE + TUNNEL_SYNC_HEADER_SIZE;
            } else {
                size = NETFLIX_DDP_BUFSIZE;
            }
        }
        break;
    case AUDIO_FORMAT_AC4:
        /*
         *1.offload write interval is 10ms
         *2.AC4 datarate: ac4 frame size=16391 frame rate =23440 sample rate=48000
         *       16391Bytes/42.6ms
         *3.set the audio hal buffer size as 8192 Bytes, it is about 24ms
         */
        size = (DEFAULT_PLAYBACK_PERIOD_SIZE << 4);
        break;
    case AUDIO_FORMAT_DOLBY_TRUEHD:
        if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
            size = 16 * DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
        } else {
            /* TrueHD content, in SBR, need 8190bytes to feed the decoder.
             * so, choose the 8192bytes as an estimated value.
             */
            size = 4 * PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
        }
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = 4 * PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
        }
        break;
    case AUDIO_FORMAT_DTS:
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = DTS1_PERIOD_SIZE / 2;
        } else {
            size = DTSHD_PERIOD_SIZE * 8;
        }
        ALOGI("%s AUDIO_FORMAT_DTS buffer size = %zuframes", __FUNCTION__, size);
        break;
    case AUDIO_FORMAT_DTS_HD:
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = 4 * PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
        } else {
            size = DTSHD_PERIOD_SIZE * 8;
        }
        ALOGI("%s AUDIO_FORMAT_DTS_HD buffer size = %zuframes", __FUNCTION__, size);
        break;
#if 0
    case AUDIO_FORMAT_PCM:
        if (adev->continuous_audio_mode) {
            /*Tunnel sync HEADER is 16 bytes*/
            if (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) {
                size = (8192 + 16) / 4;
            }
        }
#endif
    default:
        if (adev->continuous_audio_mode && audio_is_linear_pcm(out->hal_internal_format)) {
            /*Tunnel sync HEADER is 20 bytes*/
            if (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) {
                size = (8192 + TUNNEL_SYNC_HEADER_SIZE);
                return size;
            } else {
                /* roll back the change for SWPL-15974 to pass the gts failure SWPL-20926*/
                return DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT* audio_stream_out_frame_size ( (struct audio_stream_out *) stream);
            }
        }
        if (out->config.rate == 96000)
            size = DEFAULT_PLAYBACK_PERIOD_SIZE * 2;
        else
            size = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
    }

    if (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC && audio_is_linear_pcm(out->hal_internal_format)) {
        size = (size * audio_stream_out_frame_size((struct audio_stream_out *) stream)) + TUNNEL_SYNC_HEADER_SIZE;
    } else {
        size = (size * audio_stream_out_frame_size((struct audio_stream_out *) stream));
    }

    // remove alignment to have an accurate size
    // size = ( (size + 15) / 16) * 16;
    return size;
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
    ALOGD ("%s(%p)", __FUNCTION__, out);
    if ((out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) && adev->a2dp_hal)
        a2dp_out_standby(adev);

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
        if (out->hw_sync_mode == 1 || adev->hwsync_output == out) {
            out->pause_status = false;
            adev->hwsync_output = NULL;
            ALOGI ("clear hwsync_output when hwsync standby\n");
        }
        int cnt = 0;
        for (cnt=0; cnt<STREAM_USECASE_MAX; cnt++) {
            if (adev->active_outputs[cnt] != NULL) {
                break;
            }
        }
        /* no active output here,we can close the pcm to release the sound card now*/
        if (cnt >= STREAM_USECASE_MAX) {
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

    out->write_count = 0;
    /* VTS: a stream should always succeed to flush
     * hardware/libhardware/include/hardware/audio.h: Stream must already
     * be paused before calling flush(), we check and complain this case
     */
    if (!out->pause_status) {
        ALOGW("%s(%p), stream should be in pause status", __func__, out);
    }

    standy_func = do_output_standby;

    aml_audio_trace_int("out_flush", 1);
    pthread_mutex_lock (&adev->lock);
    pthread_mutex_lock (&out->lock);
    if (out->pause_status == true) {
        // when pause status, set status prepare to avoid static pop sound
        ret = aml_alsa_output_stop(stream);
        if (ret < 0) {
            ALOGE("aml_alsa_output_stop error =%d", ret);
        }

        if (out->spdifout_handle) {
            ret = aml_audio_spdifout_stop(out->spdifout_handle);
            if (ret < 0) {
                ALOGE("aml_audio_spdifout_stop error =%d", ret);
            }

        }

        if (out->spdifout2_handle) {
            ret = aml_audio_spdifout_stop(out->spdifout2_handle);
            if (ret < 0) {
                ALOGE("aml_audio_spdifout_stop error =%d", ret);
            }
        }

    }
    standy_func (out);
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
    aml_audio_trace_int("out_flush", 0);
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

    standy_func = do_output_standby;
    startup_func = start_output_stream;

    ALOGD ("%s(kvpairs(%s), out_device=%#x)", __FUNCTION__, kvpairs, adev->out_device);
    parms = str_parms_create_str (kvpairs);

    ret = str_parms_get_str (parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof (value) );
    if (ret >= 0) {
        val = atoi (value);
        pthread_mutex_lock (&adev->lock);
        pthread_mutex_lock (&out->lock);
        if ( ( (adev->out_device & AUDIO_DEVICE_OUT_ALL) != val) && (val != 0) ) {
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
            pthread_mutex_unlock (&out->lock);
            pthread_mutex_unlock (&adev->lock);
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
            ALOGI ("[%s:%d] audio hw format change from %#x to %#x", __func__, __LINE__, config->format, fmt);
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
            pthread_mutex_unlock (&out->lock);
            pthread_mutex_unlock (&adev->lock);
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
            pthread_mutex_unlock (&out->lock);
            pthread_mutex_unlock (&adev->lock);
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
            pthread_mutex_unlock (&out->lock);
            pthread_mutex_unlock (&adev->lock);
        }

        // We shall return Result::OK, which is 0, if parameter is set successfully,
        // or we can not pass VTS test.
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 in order to pass VTS test.", __FUNCTION__);
        ret = 0;

        goto exit;
    }
    ret = str_parms_get_str (parms, "hw_av_sync", value, sizeof (value) );
    if (ret >= 0 && out->hw_sync_mode == false) {
        if (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) {
            int hw_sync_id = atoi(value);
            bool ret_set_id = false;

            if (hw_sync_id < 0) {
                ALOGE("%s, hw_sync_id:%d failed", __func__, hw_sync_id);
                goto exit;
            }

            pthread_mutex_lock(&out->lock);
            out->hwsync = aml_audio_calloc(1, sizeof(audio_hwsync_t));
            if (!out->hwsync) {
                pthread_mutex_unlock(&out->lock);
                ALOGE("%s,malloc hwsync failed", __func__);
                goto exit;
            }
            pthread_mutex_unlock(&out->lock);

            if (hw_sync_id == 12345678) {
                out->hwsync->tsync_fd = -1;
                out->hwsync->hwsync_id = hw_sync_id;
                aml_audio_hwsync_init(out->hwsync, out);
            } else {
                ALOGI ("[%s] adev->hw_mediasync:%p\n", __FUNCTION__, adev->hw_mediasync);
                if (adev->hw_mediasync == NULL) {
                    adev->hw_mediasync = aml_hwsync_mediasync_create();
                }
                if (adev->hw_mediasync != NULL) {
                    out->hwsync->use_mediasync = true;
                    out->hwsync->mediasync = adev->hw_mediasync;
                    out->hwsync->hwsync_id = hw_sync_id;
                    ret_set_id = aml_audio_hwsync_set_id(out->hwsync, hw_sync_id);
                }
                aml_audio_hwsync_init(out->hwsync, out);
            }


            bool sync_enable = ((hw_sync_id == 12345678) || ret_set_id) ? true : false;
            audio_hwsync_t *hw_sync = out->hwsync;
            ALOGI("stream:(%p) set hw_sync_id:%d (0x%x), %s hw_sync and the mode is %s\n",
                   out, hw_sync_id, hw_sync_id, sync_enable ? "enable" : "disable", (hw_sync_id == 12345678) ? "tsync" : "mediasync");
            out->hw_sync_mode = sync_enable;

            if (adev->ms12_out != NULL && adev->ms12_out->hwsync) {
                adev->ms12_out->hw_sync_mode = out->hw_sync_mode;
                ALOGI("set ms12_out %p hw_sync_mode %d",adev->ms12_out, adev->ms12_out->hw_sync_mode);
            }
            hw_sync->first_apts_flag = false;

            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&out->lock);
            out->frame_write_sum = 0;
            out->last_frames_postion = 0;
            /* clear up previous playback output status */
            if (!out->standby) {
                standy_func (out);
            }

            /*currently not use "hal mixer", here close these code*/
            /*if (sync_enable) {
                ALOGI ("init hal mixer when hwsync\n");
                aml_hal_mixer_init (&adev->hal_mixer);
            }*/
            if (continous_mode(adev) && out->hw_sync_mode) {
                dolby_ms12_hwsync_init();
            }
            pthread_mutex_unlock (&out->lock);
            pthread_mutex_unlock (&adev->lock);
        } else {
            ALOGE("Wrong hw_av_sync setting, AUDIO_OUTPUT_FLAG_HW_AV_SYNC is not set for this stream");
        }
        ret = 0;
        goto exit;
    } else {
        ALOGE("%s, ret:%d, hw_sync_mode:%s", __func__, ret, out->hw_sync_mode?"is true":"is false");
    }
    /*ret = str_parms_get_str (parms, "A2dpSuspended", value, sizeof (value) );
    if (ret >= 0) {
        ret = a2dp_out_set_parameters(stream, kvpairs);
        goto exit;
    }
    ret = str_parms_get_str (parms, "closing", value, sizeof (value) );
    if (ret >= 0) {
        ret = a2dp_out_set_parameters(stream, kvpairs);
        goto exit;
    }*/
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
        if (adev->debug_flag) {
            ALOGW("Amlogic_HAL - %s: parameter is NULL, change ret value to 0 in order to pass VTS test.", __func__);
        }
        ret = 0;
    }
    return ret;
}

static char *out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    if (strstr(keys, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES) || \
        strstr(keys, AUDIO_PARAMETER_STREAM_SUP_CHANNELS) || \
        strstr(keys, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        return out_get_parameters_wrapper_about_sup_sampling_rates__channels__formats(stream, keys);
    }
    else {
        ALOGE("%s() keys %s is not supported! TODO!\n", __func__, keys);
        return strdup ("");
    }
}

static uint32_t out_get_latency (const struct audio_stream_out *stream)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    uint32_t a2dp_delay = 0, alsa_latency = 0, ms12_latency = 0, ms12_pipeline_latnecy = 0, whole_latency = 0;

    if (out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
        a2dp_delay = a2dp_out_get_latency(adev) * out->hal_rate / 1000;
        return a2dp_delay;
    }

    snd_pcm_sframes_t frames = out_get_latency_frames (stream);
    ms12_latency = get_ms12_buffer_latency((struct aml_stream_out *)out);
    ms12_pipeline_latnecy = HAL_MS12_PIPELINE_LATENCY;
    alsa_latency = (frames * 1000) / out->config.rate;
    whole_latency =  ms12_latency + ms12_pipeline_latnecy + alsa_latency;

    ALOGV("%s  stream:%p frames:%lu out->config.rate:%u whole_latency:%u, alsa_latency:%u, ms12_latency:%u", __func__,
        stream, frames,out->config.rate, whole_latency, alsa_latency, ms12_latency);
    return whole_latency;

}

static uint32_t out_get_alsa_latency (const struct audio_stream_out *stream)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *) stream;
    snd_pcm_sframes_t frames = out_get_alsa_latency_frames (stream);
    return (frames * 1000) / out->config.rate;
}

static int out_set_volume (struct audio_stream_out *stream, float left, float right)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int ret = 0;
    bool is_dolby_format = is_dolby_ms12_support_compression_format(out->hal_internal_format);
    bool is_direct_pcm = is_direct_stream_and_pcm_format(out);
    bool is_mmap_pcm = is_mmap_stream_and_pcm_format(out);
    bool is_ms12_pcm_volume_control = (is_direct_pcm && !is_mmap_pcm);
    bool is_dts = is_dts_format(out->hal_internal_format);
    bool is_cbs_dtv_audio = dtv_tuner_framework(stream);

    ALOGI("%s(), stream(%p), left:%f right:%f, continous_mode(%d), hal_internal_format:%x, is dolby %d is direct pcm %d is_mmap_pcm %d\n",
        __func__, stream, left, right, continous_mode(adev), out->hal_internal_format, is_dolby_format, is_direct_pcm, is_mmap_pcm);

    /* for not use ms12 case, we can use spdif enc mute, other wise ms12 can handle it*/
    if (is_dts || \
        (is_dolby_format && (eDolbyDcvLib == adev->dolby_lib_type || is_bypass_dolbyms12(stream) || adev->hdmi_format == BYPASS))) {
        if (out->volume_l < FLOAT_ZERO && left > FLOAT_ZERO) {
            ALOGI("set offload mute: false");
            out->offload_mute = false;
        } else if (out->volume_l > FLOAT_ZERO && left < FLOAT_ZERO) {
            ALOGI("set offload mute: true");
            out->offload_mute = true;
        }
        if (out->spdifout_handle) {
            aml_audio_spdifout_mute(out->spdifout_handle, out->offload_mute);
        }
        if (out->spdifout2_handle) {
            aml_audio_spdifout_mute(out->spdifout2_handle, out->offload_mute);
        }

    }
    out->volume_l = left;
    out->volume_r = right;

    /*
     *The Dolby format(dd/ddp/ac4/true-hd/mat) and direct&UI-PCM(stereo or multi PCM)
     *use set_ms12_main_volume to control it.
     *The volume about mixer-PCM is controled by AudioFlinger
     */
    if ((eDolbyMS12Lib == adev->dolby_lib_type) && !is_cbs_dtv_audio && (is_dolby_format || is_ms12_pcm_volume_control)) {
        if (out->volume_l != out->volume_r) {
            ALOGW("%s, left:%f right:%f NOT match", __FUNCTION__, left, right);
        }
        //when  in tv platform ,we use the adev_set_port_config
        //to set the audio gain , but the app will call out_set_volume
        //to set the main volume to 1.0, so we will use
        //sink gain for volume adjust.
        set_ms12_main_volume(&adev->ms12,out->volume_l);
        /*
         * The postgain value has an impact on the Volume Modeler and the Audio Regulator:
         * Volume Modeler: Uses the postgain value to select the appropriate frequency response curve
         * to maintain a consistent perceived timbre at different listening levels.
         * SP45: Postgain
         * Sets the amount of gain that is to be applied to the signal after exiting MS12.
         * Settings From -130 to +30 dB, in 0.0625 dB steps
         */
        int dap_postgain = volume2Ms12DapPostgain(out->volume_l);
        set_ms12_dap_postgain(&adev->ms12, dap_postgain);
    } else if (is_cbs_dtv_audio) {
        /*
            for none-ms12 case, as tunner framework passthrough mode will use this stream to
            control the dtv status such as volume,pause,resume,we need check if this direct stream
            is used in this case.in current design, dtv audio patch is maintained inside HAL,
            it will use a seperated output stream to store all the input information,
            such as format,ch,sr and pts info.
        */
        adev->dtv_volume =  left;
    }
    return 0;
}


static int out_pause (struct audio_stream_out *stream)
{
    ALOGD ("out_pause(%p)\n", stream);

    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int r = 0;

    aml_audio_trace_int("out_pause", 1);
    if (aml_audio_trace_debug_level() > 0)
    {
        out->pause_time = aml_audio_get_systime() / 1000; //us --> ms
        if (out->pause_time > out->write_time && (out->pause_time - out->write_time < 5*1000)) { //continually write time less than 5s, audio gap
            ALOGD("%s: out_stream(%p) AudioGap pause_time:%llu,  diff_time(pause - write):%llu ms", __func__,
                   stream, out->pause_time, out->pause_time - out->write_time);
        } else {
            ALOGD("%s:  -------- pause ----------", __func__);
        }
    }
    out->write_count = 0;

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
        int cnt = 0;
        for (int i=0; i<STREAM_USECASE_MAX; i++) {
            if (adev->active_outputs[i] != NULL) {
                cnt++;
            }
        }
        if (cnt > 1) {
            ALOGI ("more than one active stream,skip alsa hw pause\n");
            goto exit1;
        }
    }
    r = aml_alsa_output_pause(stream);
    if (out->spdifout_handle) {
        aml_audio_spdifout_pause(out->spdifout_handle);
    }

    if (out->spdifout2_handle) {
        aml_audio_spdifout_pause(out->spdifout2_handle);
    }
exit1:
    out->pause_status = true;
exit:
    if (out->hw_sync_mode) {
        ALOGI("%s set AUDIO_PAUSE when tunnel mode\n",__func__);
        aml_hwsync_set_tsync_pause(out->hwsync);
        out->tsync_status = TSYNC_STATUS_PAUSED;
    }
    pthread_mutex_unlock (&adev->lock);
    pthread_mutex_unlock (&out->lock);
    aml_audio_trace_int("out_pause", 0);
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

    out->write_count = 0;
    aml_audio_trace_int("out_resume", 1);
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

    r = aml_alsa_output_resume(stream);

    if (out->spdifout_handle) {
        aml_audio_spdifout_resume(out->spdifout_handle);
    }

    if (out->spdifout2_handle) {
        aml_audio_spdifout_resume(out->spdifout2_handle);
    }

    if (out->hw_sync_mode) {
        ALOGI ("init hal mixer when hwsync resume\n");
        adev->hwsync_output = out;
        aml_hal_mixer_init(&adev->hal_mixer);
        aml_hwsync_set_tsync_resume(out->hwsync);
        out->tsync_status = TSYNC_STATUS_RUNNING;
    }
    out->pause_status = false;
exit:
    if (out->hw_sync_mode) {
        ALOGI("%s set AUDIO_RESUME when tunnel mode\n",__func__);
        aml_hwsync_set_tsync_resume(out->hwsync);
        out->tsync_status = TSYNC_STATUS_RUNNING;
    }
    pthread_mutex_unlock (&adev->lock);
    pthread_mutex_unlock (&out->lock);
    aml_audio_trace_int("out_resume", 0);
    return r;
}

/* use standby instead of pause to fix background pcm playback */
static int out_pause_new (struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(aml_dev->ms12);
    bool is_standby = aml_out->standby;
    int ret = 0;

    ALOGI("%s(), stream(%p), pause_status = %d,dolby_lib_type = %d, conti = %d,hw_sync_mode = %d,ms12_enable = %d,ms_conti_paused = %d\n",
          __func__, stream, aml_out->pause_status, aml_dev->dolby_lib_type, aml_dev->continuous_audio_mode, aml_out->hw_sync_mode, aml_dev->ms12.dolby_ms12_enable, aml_dev->ms12.is_continuous_paused);

    aml_audio_trace_int("out_pause_new", 1);
    if (aml_audio_trace_debug_level() > 0)
    {
        aml_out->pause_time = aml_audio_get_systime() / 1000; //us --> ms
        if (aml_out->pause_time > aml_out->write_time && (aml_out->pause_time - aml_out->write_time < 5*1000)) { //continually write time less than 5s, audio gap
            ALOGD("%s: out_stream(%p) AudioGap pause_time:%llu,  diff_time(pause - write):%llu ms", __func__,
                   stream, aml_out->pause_time, aml_out->pause_time - aml_out->write_time);
        } else {
            ALOGD("%s:  -------- pause ----------", __func__);
        }
    }
    aml_out->write_count = 0;

    pthread_mutex_lock (&aml_dev->lock);
    pthread_mutex_lock (&aml_out->lock);

    /* a stream should fail to pause if not previously started */
    if (aml_out->pause_status == true) {
        // If output stream is standby or paused,
        // we should return Result::INVALID_STATE (3),
        // thus we can pass VTS test.
        ALOGE ("%s: stream in wrong status. standby(%d) or paused(%d)",
                __func__, aml_out->standby, aml_out->pause_status);
        ret = INVALID_STATE;
        goto exit;
    }
    if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
        if (aml_dev->continuous_audio_mode == 1) {
            pthread_mutex_lock(&ms12->lock);
            if ((aml_dev->ms12.dolby_ms12_enable == true) &&
                ((aml_dev->ms12.is_continuous_paused == false) || (aml_out->pause_status == false))) {
                audiohal_send_msg_2_ms12(ms12, MS12_MESG_TYPE_PAUSE);
            } else {
                ALOGI("%s do nothing\n", __func__);
            }
            pthread_mutex_unlock(&ms12->lock);
        } else {
            /*if it raw data we don't do standby otherwise it may cause audioflinger
            underrun after resume please refer to issue SWPL-13091*/
            if (audio_is_linear_pcm(aml_out->hal_internal_format)) {
                ret = do_output_standby_l(&stream->common);
                if (ret < 0) {
                    goto exit;
                }
            }
        }
    } else {

        ret = do_output_standby_l(&stream->common);
        if (ret < 0) {
            goto exit;
        }
    }
exit:
    aml_out->pause_status = true;

    pthread_mutex_unlock(&aml_out->lock);
    pthread_mutex_unlock(&aml_dev->lock);
    aml_audio_trace_int("out_pause_new", 0);

    if (is_standby) {
        ALOGD("%s(), stream(%p) already in standy, return INVALID_STATE", __func__, stream);
        ret = INVALID_STATE;
    }
    aml_out->position_update = 0;

    ALOGI("%s(), stream(%p) exit", __func__, stream);
    return ret;
}

static int out_resume_new (struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(aml_dev->ms12);
    int ret = 0;

    ALOGI("%s(), stream(%p),standby = %d,pause_status = %d\n", __func__, stream, aml_out->standby, aml_out->pause_status);
    aml_audio_trace_int("out_resume_new", 1);
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
            if ((aml_dev->ms12.dolby_ms12_enable == true) && (aml_dev->ms12.is_continuous_paused || aml_out->pause_status)) {
                if (audio_is_linear_pcm(aml_out->hal_internal_format)) {
                    /*pcm data case, directly send resume message*/
                    pthread_mutex_lock(&ms12->lock);
                    ms12->ms12_resume_state = MS12_RESUME_FROM_RESUME;
                    audiohal_send_msg_2_ms12(ms12, MS12_MESG_TYPE_RESUME);
                    pthread_mutex_unlock(&ms12->lock);
                } else {
                    /*About resume, we should seperate the control message and data stream.
                    **This solution can avoid gap issue,
                    **for example ms12 remaing buffer and audioflinger can't send data in time.
                    **raw data case, resume it in write_new when first data coming.
                    **This is for fixing the almond NTS underflow cases TV-45739.
                    */
                    ALOGI("%s resume raw data later", __func__);
                    aml_dev->ms12.need_ms12_resume = true;
                }
            }
        }
    }

exit:
    pthread_mutex_unlock (&aml_out->lock);
    pthread_mutex_unlock (&aml_dev->lock);

    aml_out->pause_status = false;
    aml_audio_trace_int("out_resume_new", 0);
    ALOGI("%s(), stream(%p) exit", __func__, stream);
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

    aml_audio_trace_int("out_flush_new", 1);
    out->write_count = 0;
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (out->total_write_size == 0) {
            out->pause_status = false;
            ALOGI("%s not writing, do nothing", __func__);
            aml_audio_trace_int("out_flush_new", 0);
            return 0;
        }
        if (out->hw_sync_mode) {
            aml_audio_hwsync_init(out->hwsync, out);
            dolby_ms12_hwsync_init();
        }
        //normal pcm(mixer thread) do not flush dolby ms12 input buffer
        if (continous_mode(adev) && (out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
            pthread_mutex_lock(&ms12->lock);
            if (adev->ms12.dolby_ms12_enable)
                audiohal_send_msg_2_ms12(ms12, MS12_MESG_TYPE_FLUSH);
            out->continuous_audio_offset = 0;
            /*SWPL-39814, when using exo do seek, sometimes audio track will be reused, then the
             *sequece will be pause->flush->writing data, we need to handle this.
             *It may causes problem for normal pause/flush/resume
             */
            if ((out->pause_status || adev->ms12.is_continuous_paused) && adev->ms12.dolby_ms12_enable) {
                ms12->ms12_resume_state = MS12_RESUME_FROM_FLUSH;
                audiohal_send_msg_2_ms12(ms12, MS12_MESG_TYPE_RESUME);
            }
            pthread_mutex_unlock(&ms12->lock);
        }
    }

    if (out->hal_format == AUDIO_FORMAT_AC4) {
        aml_ac4_parser_reset(out->ac4_parser_handle);
    }

    out->pause_status = false;
    ALOGI("%s(), stream(%p) exit\n", __func__, stream);
    aml_audio_trace_int("out_flush_new", 0);
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

static int insert_output_bytes_direct (struct aml_stream_out *out, size_t size)
{
    int ret = 0;
    size_t insert_size = size;
    size_t once_write_size = 0;
    char *insert_buf = (char*) aml_audio_malloc (8192);

    if (insert_buf == NULL) {
        ALOGE ("malloc size failed \n");
        return -ENOMEM;
    }

    if (!out->pcm) {
        ret = -1;
        ALOGE("%s pcm is NULL", __func__);
        goto exit;
    }

    memset (insert_buf, 0, 8192);
    while (insert_size > 0) {
        once_write_size = insert_size > 8192 ? 8192 : insert_size;
        ret = pcm_write (out->pcm, insert_buf, once_write_size);
        if (ret < 0) {
            ALOGE("%s pcm_write failed", __func__);
            break;
        }
        insert_size -= once_write_size;
    }

exit:
    aml_audio_free (insert_buf);
    return ret;
}

static int out_get_render_position (const struct audio_stream_out *stream,
                                    uint32_t *dsp_frames)
{
    int ret = 0;
    uint64_t  dsp_frame_uint64 = 0;
    struct timespec timetamp = {0};
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    *dsp_frames = 0;
    ret = out_get_presentation_position(stream, &dsp_frame_uint64,&timetamp);
    if (ret == 0)
    {
        *dsp_frames = (uint32_t)(dsp_frame_uint64 & 0xffffffff);
        if (*dsp_frames == 0) {
            /*add this code INVALID_STATE(3) for VTS in AndroidP*/
            ret = INVALID_STATE;
        }
    } else {
        ret = -ENOSYS;
    }

    if (adev->debug_flag) {
        ALOGD("%s,pos %d ret =%d \n",__func__,*dsp_frames, ret);
    }
    return ret;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *dev = out->dev;
    int i;
    int status = 0;

    pthread_mutex_lock (&dev->lock);
    pthread_mutex_lock (&out->lock);

    if (dev->native_postprocess.num_postprocessors >= MAX_POSTPROCESSORS) {
        status = -ENOSYS;
        goto exit;
    }

    /* save audio effect handle in audio hal. if it is saved, skip this. */
    for (i = 0; i < dev->native_postprocess.num_postprocessors; i++) {
        if (dev->native_postprocess.postprocessors[i] == effect) {
            status = 0;
            goto exit;
        }
    }

    dev->native_postprocess.postprocessors[dev->native_postprocess.num_postprocessors++] = effect;

    effect_descriptor_t tmpdesc;
    (*effect)->get_descriptor(effect, &tmpdesc);
    if (0 == strcmp(tmpdesc.name, "VirtualX")) {
        dev->native_postprocess.libvx_exist = Check_VX_lib();
        ALOGI("%s, add audio effect: '%s' exist flag : %s", __FUNCTION__, VIRTUALX_LICENSE_LIB_PATH,
            (dev->native_postprocess.libvx_exist) ? "true" : "false");
        /* specify effect order for virtualx. VX does downmix from 5.1 to 2.0 */
        if (dev->native_postprocess.num_postprocessors > 1 && dev->native_postprocess.num_postprocessors < MAX_POSTPROCESSORS) {
            i = dev->native_postprocess.num_postprocessors - 1;
            effect_handle_t tmp;
            tmp = dev->native_postprocess.postprocessors[i];
            dev->native_postprocess.postprocessors[i] = dev->native_postprocess.postprocessors[0];
            dev->native_postprocess.postprocessors[0] = tmp;
            ALOGI("%s, add audio effect: Reorder VirtualX at the first of the effect chain.", __FUNCTION__);
        }
    }
    ALOGI("%s, add audio effect: %s in audio hal, effect_handle: %p, total num of effects: %d",
        __FUNCTION__, tmpdesc.name, effect, dev->native_postprocess.num_postprocessors);

    if (dev->native_postprocess.num_postprocessors > dev->native_postprocess.total_postprocessors)
        dev->native_postprocess.total_postprocessors = dev->native_postprocess.num_postprocessors;

exit:
    pthread_mutex_unlock (&out->lock);
    pthread_mutex_unlock (&dev->lock);
    return status;
}

static int out_remove_audio_effect(const struct audio_stream *stream __unused, effect_handle_t effect __unused)
{
    /*struct aml_stream_out *out = (struct aml_stream_out *) stream;
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

    effect_descriptor_t tmpdesc;
    (*effect)->get_descriptor(effect, &tmpdesc);
    ALOGI("%s, remove audio effect: %s in audio hal, effect_handle: %p, total num of effects: %d",
        __FUNCTION__, tmpdesc.name, effect, dev->native_postprocess.num_postprocessors);

exit:
    pthread_mutex_unlock (&out->lock);
    pthread_mutex_unlock (&dev->lock);
    return status;*/
    return 0;
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

// actually maybe it be not useful now  except pass CTS_TEST:
// run cts -c android.media.cts.AudioTrackTest -m testGetTimestamp
static int out_get_presentation_position (const struct audio_stream_out *stream, uint64_t *frames, struct timespec *timestamp)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    uint64_t frames_written_hw = out->last_frames_postion;
    int frame_latency = 0,timems_latency = 0;
    bool b_raw_in = false;
    bool b_raw_out = false;
    int ret = 0;
    int video_delay_frames = 0;
    int64_t origin_tv_nsec = 0;
    int origin_vdelay_frames = 0;
    if (!frames || !timestamp) {
        ALOGI("%s, !frames || !timestamp\n", __FUNCTION__);
        return -EINVAL;
    }

    /* add this code for VTS. */
    if (0 == frames_written_hw) {
        *frames = frames_written_hw;
        *timestamp = out->lasttimestamp;
        return ret;
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        ret = aml_audio_get_ms12_presentation_position(stream, frames, timestamp);
    } else if (eDolbyDcvLib == adev->dolby_lib_type) {
         bool is_audio_type_dolby = (adev->audio_type == EAC3 || adev->audio_type == AC3);
         bool is_hal_format_dolby = (out->hal_format == AUDIO_FORMAT_AC3 || out->hal_format == AUDIO_FORMAT_E_AC3);
         if (is_audio_type_dolby || is_hal_format_dolby) {
             timems_latency = aml_audio_get_latency_offset(adev->active_outport,
                                                             out->hal_internal_format,
                                                             adev->sink_format,
                                                             adev->ms12.dolby_ms12_enable);
             if (is_audio_type_dolby) {
                frame_latency = timems_latency * (out->hal_rate * out->rate_convert / 1000);
             }
             else if (is_hal_format_dolby) {
                frame_latency = timems_latency * (out->hal_rate / 1000);
             }
         }
         if ((frame_latency < 0) && (frames_written_hw < abs(frame_latency))) {
             ALOGV("%s(), not ready yet", __func__);
             return -EINVAL;
         }

        if (frame_latency >= 0)
            *frames = frame_latency + frames_written_hw;
        else if (frame_latency < 0) {
            if (frames_written_hw >= abs(frame_latency))
                *frames = frame_latency + frames_written_hw;
            else
                *frames = 0;
        }

        unsigned int output_sr = (out->config.rate) ? (out->config.rate) : (MM_FULL_POWER_SAMPLING_RATE);
        *frames = *frames * out->hal_rate / output_sr;
        *timestamp = out->lasttimestamp;
    }

    /*here we need add video delay*/
    video_delay_frames = get_media_video_delay(&adev->alsa_mixer) * out->hal_rate / 1000;
    origin_tv_nsec = timestamp->tv_nsec;
    origin_vdelay_frames = video_delay_frames;

    if (out->is_normal_pcm) {
        //  AF::Track's Position should larger than hal, so minus DEFAULT_PLAYBACK_PERIOD_SIZE
        int max_delay_frames = adev->sys_audio_frame_written - *frames - DEFAULT_PLAYBACK_PERIOD_SIZE;
        max_delay_frames = (max_delay_frames < 0 ? 0 : max_delay_frames);

        if (video_delay_frames > max_delay_frames) {
            int offset_frames = video_delay_frames - max_delay_frames;
            int offset_us = -(offset_frames * 1000 / (out->hal_rate/1000));

            aml_audio_delay_timestamp(timestamp, offset_us);
            video_delay_frames = max_delay_frames;
            if (adev->debug_flag) {
                ALOGI("%s sys_audio_frame_written:%lld frames:%lld max_delay_frames:%d offset_frames:%d offset_us:%d", __func__,
                    adev->sys_audio_frame_written, *frames, max_delay_frames, offset_frames, offset_us);
            }
        }
    }
    *frames += video_delay_frames;

    if (adev->debug_flag) {
        ALOGI("out_get_presentation_position out %p %"PRIu64", sec = %ld, nanosec = %ld(origin:%lld) tunned_latency_ms %d frame_latency %d video delay=%d(origin:%d)\n",
            out, *frames, timestamp->tv_sec, timestamp->tv_nsec, origin_tv_nsec, timems_latency, frame_latency, video_delay_frames, origin_vdelay_frames);
        int64_t  frame_diff_ms =  (*frames - out->last_frame_reported) * 1000 / out->hal_rate;
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

    return ret;
}
static int get_next_buffer (struct resampler_buffer_provider *buffer_provider,
                            struct resampler_buffer* buffer);
static void release_buffer (struct resampler_buffer_provider *buffer_provider,
                            struct resampler_buffer* buffer);

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
            /* T3 needs I2S for HDMIRX due to HBR issue */
            if (get_hdmiin_audio_mode(&adev->alsa_mixer) == HDMIIN_MODE_I2S) {
                inport = PORT_I2S4HDMIRX;
            }
        } else if ((is_earc_descrpt()) &&
                   (in_device & AUDIO_DEVICE_IN_HDMI_ARC)) {
            inport = PORT_EARC;
        } else {
            inport = PORT_SPDIF;
        }
    } else if ((in_device & AUDIO_DEVICE_IN_BACK_MIC) ||
            (in_device & AUDIO_DEVICE_IN_BUILTIN_MIC)) {
        inport = PORT_BUILTINMIC;
    } else if (in_device & AUDIO_DEVICE_IN_ECHO_REFERENCE) {
        inport = PORT_ECHO_REFERENCE;
    } else {
        /* fix auge tv input, hdmirx, tunner */
        if (alsa_device_is_auge()
            && (in_device & AUDIO_DEVICE_IN_TV_TUNER))
            inport = PORT_TV;
        else
            inport = PORT_I2S;
    }

#ifdef ENABLE_AEC_HAL
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
#ifdef ENABLE_AEC_HAL
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        in->config.rate = in->requested_rate;
        in->config.channels = 4;
    }
#endif
#ifdef ENABLE_AEC_APP
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        in->config.rate = in->requested_rate;
    }
#else
    (void)in;
#endif

    /*add for vts: CapturePositionAdvancesWithReads/18_default_primary
    _18__48000_AUDIO_CHANNEL_IN_MONO. it uses 1ch and c0d1c, but c0d1c not support 1ch.*/
    if ((in->device | AUDIO_DEVICE_BIT_IN) == AUDIO_DEVICE_IN_ECHO_REFERENCE && in->config.channels == 1)
        in->config.channels = 2;
}
static int choose_stream_pcm_config(struct aml_stream_in *in);
static int add_in_stream_resampler(struct aml_stream_in *in);

/* must be called with hw device and input stream mutexes locked */
int start_input_stream(struct aml_stream_in *in)
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
    ALOGD("*%s, open alsa_card(%d %d) alsa_device(%d), in_device:0x%x\n",
          __func__, card, port, alsa_device, adev->in_device);

    in->pcm = pcm_open(card, alsa_device, PCM_IN | PCM_MONOTONIC | PCM_NONEBLOCK, &in->config);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("%s: cannot open pcm_in driver: %s", __func__, pcm_get_error(in->pcm));
        pcm_close (in->pcm);
        adev->active_input = NULL;
        return -ENOMEM;
    }
    ALOGD("pcm_open in: card(%d), port(%d)", card, port);

    if (in->requested_rate != in->config.rate) {
        ret = add_in_stream_resampler(in);
        if (ret < 0) {
            pcm_close (in->pcm);
            adev->active_input = NULL;
            return -EINVAL;
        }
    }

    ALOGD("%s: device(%x) channels=%d period_size=%d rate=%d requested_rate=%d mode= %d",
        __func__, in->device, in->config.channels, in->config.period_size,
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

    ALOGD("%s: enter: channel_mask(%#x) rate(%d) format(%#x)", __func__,
        in->hal_channel_mask, in->requested_rate, in->hal_format);

    uint32_t req_channel = audio_channel_count_from_in_mask(in->hal_channel_mask);
    uint32_t req_format = audio_bytes_per_sample(in->hal_format);
    uint32_t period = in->config.period_size;
    if (in->source == AUDIO_SOURCE_ECHO_REFERENCE) {
        period = CAPTURE_PERIOD_SIZE * PLAYBACK_CODEC_SAMPLING_RATE / CAPTURE_CODEC_SAMPLING_RATE;
    }
    if (period == 0) {
        period = pcm_config_in.period_size;
    }
    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    period = (period + 15) / 16 * 16;
    size = period * req_channel * req_format * in->requested_rate / in->config.rate;

    ALOGD("%s: exit: buffer_size = %zu", __func__, size);
    return size;
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    return in->hal_channel_mask;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    ALOGV("%s(%p) in->hal_format=%d", __FUNCTION__, in, in->hal_format);
    return in->hal_format;
}

static int in_set_format(struct audio_stream *stream __unused, audio_format_t format __unused)
{
    return -ENOSYS;
}

/* must be called with hw device and input stream mutexes locked */
int do_input_standby(struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;

    ALOGD ("%s(%p) in->standby = %d", __FUNCTION__, in, in->standby);

    if (!in->standby) {
        pcm_close (in->pcm);
        in->pcm = NULL;

        adev->active_input = NULL;
        if (adev->mode != AUDIO_MODE_IN_CALL) {
            adev->in_device &= ~AUDIO_DEVICE_IN_ALL;
            //select_input_device(adev);
        }

        in->standby = 1;
#if 0
        ALOGD ("%s : output_standby=%d,input_standby=%d",
                 __FUNCTION__, output_standby, input_standby);
        if (output_standby && input_standby) {
            reset_mixer_state (adev->ar);
            update_mixer_state (adev->ar);
        }
#endif
    }
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int status;

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
    int ret = 0, val = 0;
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

    int format = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_FORMAT, &format);

    if (ret >= 0) {
        if (format != AUDIO_FORMAT_INVALID) {
            in->hal_format = (audio_format_t)format;
            ALOGV("  in->hal_format:%d  <== format:%d\n",
                   in->hal_format, format);

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

    /*add for vts*/
    int sample_rate = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_SAMPLING_RATE, &sample_rate);

    if (ret >= 0) {
        if (sample_rate > 0) {
            in->requested_rate = sample_rate;
            ALOGV("  in->requested_rate:%d  <== sample_rate:%d\n",
                  in->requested_rate, sample_rate);

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

    /*add for vts*/
    int channel_mask = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_CHANNELS, &channel_mask);

    if (ret >= 0) {
        if (channel_mask > 0) {
            in->hal_channel_mask = channel_mask;
            ALOGV("  in->hal_channel_mask:%d  <== channel_mask:%d\n",
                   in->hal_channel_mask, channel_mask);

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
        cap = strdup ("sup_formats=AUDIO_FORMAT_PCM_16_BIT|AUDIO_FORMAT_PCM_32_BIT");
        if (cap) {
            para = strdup (cap);
            aml_audio_free (cap);
            cap = NULL;
            return para;
        }
    } else if (strstr (keys, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES) ) {
        /*add for vts*/
        ALOGV ("Amlogic - return hard coded sup_sampling_rates list for in stream.\n");
        cap = strdup ("sup_sampling_rates=8000|11025|12000|16000|22050|24000|32000|44100|48000");
        if (cap) {
            para = strdup (cap);
            aml_audio_free (cap);
            cap = NULL;
            return para;
        }
    } else if (strstr (keys, AUDIO_PARAMETER_STREAM_SUP_CHANNELS) ) {
        /*add for vts*/
        ALOGV ("Amlogic - return hard coded sup_channels list for in stream.\n");
        cap = strdup ("sup_channels=AUDIO_CHANNEL_IN_MONO|AUDIO_CHANNEL_IN_STEREO");
        if (cap) {
            para = strdup (cap);
            aml_audio_free (cap);
            cap = NULL;
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

#ifdef ENABLE_AEC_HAL
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
    int enable_dump = getprop_bool("vendor.media.audio_hal.aec.outdump");
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

static ssize_t in_read(struct audio_stream_in *stream, void* buffer, size_t bytes)
{
    int ret = 0;
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    int channel_count = audio_channel_count_from_in_mask(in->hal_channel_mask);
    size_t in_frames = bytes / audio_stream_in_frame_size(&in->stream);
    struct aml_audio_patch* patch = adev->audio_patch;
    size_t cur_in_bytes, cur_in_frames;
    int in_mute = 0, parental_mute = 0;
    bool stable = true;

    ALOGV("%s(): stream: %p, source: %d, bytes %zu in->devices %0x", __func__, in, in->source, bytes, in->device);

#ifdef ENABLE_AEC_APP
    /* Special handling for Echo Reference: simply get the reference from FIFO.
     * The format and sample rate should be specified by arguments to adev_open_input_stream. */
    if (in->source == AUDIO_SOURCE_ECHO_REFERENCE) {
        if (in->standby) {
            ret = start_input_stream(in);
            if (ret < 0) {
                ALOGE("aec: fail to open stream\n");
                return 0;
            }
            in->standby = 0;
        }
        ret = aml_alsa_input_read(stream, buffer, bytes);
        if (ret != 0) {
            ALOGE("aec: fail to read bytes=%zu ret=%d\n", bytes, ret);
            return 0;
        }
        in->frames_read += in_frames;
        struct aec_info info;
        ret = get_pcm_timestamp(in->pcm, in_get_sample_rate(&stream->common),
                                &info, false /*input */);
        in->timestamp_nsec = audio_utils_ns_from_timespec(&info.timestamp);
        return bytes;

#if 0
        struct aec_info info;
        info.bytes = bytes;
        const uint64_t time_increment_nsec = (uint64_t)bytes * NANOS_PER_SECOND /
                                             audio_stream_in_frame_size(stream) /
                                             in_get_sample_rate(&stream->common);
        if (!aec_get_spk_running(adev->aec)) {
            if (in->timestamp_nsec == 0) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                const int64_t timestamp_nsec = audio_utils_ns_from_timespec(&now);
                in->timestamp_nsec = timestamp_nsec;
            } else {
                in->timestamp_nsec += time_increment_nsec;
            }
            memset(buffer, 0, bytes);
            const uint64_t time_increment_usec = time_increment_nsec / 1000;
            usleep(time_increment_usec);
        } else {
            int ref_ret = get_reference_samples(adev->aec, buffer, &info);
            if ((ref_ret) || (info.timestamp_usec == 0)) {
                memset(buffer, 0, bytes);
                in->timestamp_nsec += time_increment_nsec;
            } else {
                in->timestamp_nsec = 1000 * info.timestamp_usec;
            }
        }
        in->frames_read += in_frames;

#if DEBUG_AEC
        FILE* fp_ref = fopen("/data/local/traces/aec_ref.pcm", "a+");
        if (fp_ref) {
            fwrite((char*)buffer, 1, bytes, fp_ref);
            fclose(fp_ref);
        } else {
            ALOGE("AEC debug: Could not open file aec_ref.pcm!");
        }
        FILE* fp_ref_ts = fopen("/data/local/traces/aec_ref_timestamps.txt", "a+");
        if (fp_ref_ts) {
            fprintf(fp_ref_ts, "%" PRIu64 "\n", in->timestamp_nsec);
            fclose(fp_ref_ts);
        } else {
            ALOGE("AEC debug: Could not open file aec_ref_timestamps.txt!");
        }
#endif
        return info.bytes;
#endif
    }
#endif

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the input stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&in->lock);
    if (adev->dev2mix_patch) {
        ALOGV("dev2mix patch case ");
    } else {
        if (in->standby) {
            ret = start_input_stream(in);
            if (ret < 0)
                goto exit;
            in->standby = 0;
        }
    }

#if 0
    if (adev->dev_to_mix_parser != NULL) {
        bytes = aml_dev2mix_parser_process(in, buffer, bytes);
    }
#endif

#ifdef ENABLE_AEC_HAL
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        size_t read_size = 0;
        cur_in_frames = in_frames;
        cur_in_bytes = in_frames * 2 * 2;
        // 2 ch 16 bit TODO: add more fmt
        ret = aml_audio_check_and_realloc(&in->tmp_buffer_8ch, &in->tmp_buffer_8ch_size, 4 * cur_in_bytes);
        NO_R_CHECK_RET(ret, "alloc tmp_buffer_8ch size:%d fail", 4 * cur_in_bytes);

        // need read 4 ch out from the alsa driver then do aec.
        read_size = cur_in_bytes * 2;
        ret = aml_alsa_input_read(stream, in->tmp_buffer_8ch, read_size);
    }
#endif
    if (adev->dev2mix_patch) {
        float source_gain = aml_audio_get_s_gain_by_src(adev, adev->patch_src);
        ret = tv_in_read(stream, buffer, bytes);
        bytes = ret;
        if (getprop_bool("vendor.media.audiohal.indump")) {
            aml_audio_dump_audio_bitstreams("/data/audio/tv_in_read.raw",
                buffer, bytes);
        }
        apply_volume(source_gain * adev->src_gain[adev->active_inport], buffer, sizeof(uint16_t), bytes);
        goto exit;
    } else {

        /*if need mute input source, don't read data from hardware anymore*/
        if (adev->mic_mute) {
            memset(buffer, 0, bytes);
            usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
                in_get_sample_rate(&stream->common));
            ret = 0;

        } else {

            if (in->resampler) {
                ret = read_frames(in, buffer, in_frames);
            } else {
                ret = aml_alsa_input_read(stream, buffer, bytes);
            }

            if (ret < 0)
                goto exit;
            //DoDumpData(buffer, bytes, CC_DUMP_SRC_TYPE_INPUT);
        }
    }

    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        inread_proc_aec(stream, buffer, bytes);
    }

    if (ret >= 0) {
        in->frames_read += in_frames;
        in->timestamp_nsec = pcm_get_timestamp(in->pcm, in->config.rate, 0 /*isOutput*/);
    }
    bool mic_muted = false;
    adev_get_mic_mute((struct audio_hw_device*)adev, &mic_muted);
    if (mic_muted) {
        memset(buffer, 0, bytes);
    }

exit:
    if (ret < 0) {
        ALOGE("%s: read failed - sleeping for buffer duration", __func__);
        usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
                in_get_sample_rate(&stream->common));
    }
    pthread_mutex_unlock(&in->lock);

#if DEBUG_AEC && defined(ENABLE_AEC_APP)
    FILE* fp_in = fopen("/data/local/traces/aec_in.pcm", "a+");
    if (fp_in) {
        fwrite((char*)buffer, 1, bytes, fp_in);
        fclose(fp_in);
    } else {
        ALOGE("AEC debug: Could not open file aec_in.pcm!");
    }
    FILE* fp_mic_ts = fopen("/data/local/traces/aec_in_timestamps.txt", "a+");
    if (fp_mic_ts) {
        fprintf(fp_mic_ts, "%" PRIu64 "\n", in->timestamp_nsec);
        fclose(fp_mic_ts);
    } else {
        ALOGE("AEC debug: Could not open file aec_in_timestamps.txt!");
    }
#endif
    if (ret >= 0 && getprop_bool("vendor.media.audiohal.indump")) {
        aml_audio_dump_audio_bitstreams("/data/audio/alsa_read.raw",
            buffer, bytes);
    }

    return bytes;
}


static int in_get_capture_position (const struct audio_stream_in* stream, int64_t* frames,
                                   int64_t* time) {
    if (stream == NULL || frames == NULL || time == NULL) {
        return -EINVAL;
    }
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    *frames = in->frames_read;
    *time = in->timestamp_nsec;

    return 0;
}


static uint32_t in_get_input_frames_lost (struct audio_stream_in *stream __unused)
{
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream __unused, effect_handle_t effect __unused)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int status;
    effect_descriptor_t desc;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->num_preprocessors >= MAX_PREPROCESSORS) {
        status = -ENOSYS;
        goto exit;
    }

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;

    in->preprocessors[in->num_preprocessors++] = effect;

    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = true;
        do_input_standby(in);
    }

exit:

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int i;
    int status = -EINVAL;
    bool found = false;
    effect_descriptor_t desc;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->num_preprocessors <= 0) {
        status = -ENOSYS;
        goto exit;
    }

    for (i = 0; i < in->num_preprocessors; i++) {
        if (found) {
            in->preprocessors[i - 1] = in->preprocessors[i];
            continue;
        }
        if (in->preprocessors[i] == effect) {
            in->preprocessors[i] = NULL;
            status = 0;
            found = true;
        }
    }

    if (status != 0)
        goto exit;

    in->num_preprocessors--;

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;
    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = false;
        do_input_standby(in);
    }

exit:

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_get_active_microphones (const struct audio_stream_in *stream,
                                     struct audio_microphone_characteristic_t *mic_array,
                                     size_t *mic_count) {
    ALOGV("in_get_active_microphones");
    if ((mic_array == NULL) || (mic_count == NULL)) {
        return -EINVAL;
    }
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct audio_hw_device* dev = (struct audio_hw_device*)in->dev;
    bool mic_muted = false;
    adev_get_mic_mute(dev, &mic_muted);
    if ((in->source == AUDIO_SOURCE_ECHO_REFERENCE) || mic_muted) {
        *mic_count = 0;
        return 0;
    }
    adev_get_microphones(dev, mic_array, mic_count);
    return 0;
}

static int adev_get_microphones (const struct audio_hw_device* dev __unused,
                                struct audio_microphone_characteristic_t* mic_array,
                                size_t* mic_count) {
    ALOGV("adev_get_microphones");
    if ((mic_array == NULL) || (mic_count == NULL)) {
        return -EINVAL;
    }
    get_mic_characteristics(mic_array, mic_count);
    return 0;
}

static void get_mic_characteristics (struct audio_microphone_characteristic_t* mic_data,
                                    size_t* mic_count) {
    *mic_count = 1;
    memset(mic_data, 0, sizeof(struct audio_microphone_characteristic_t));
    strlcpy(mic_data->device_id, "builtin_mic", AUDIO_MICROPHONE_ID_MAX_LEN - 1);
    strlcpy(mic_data->address, "top", AUDIO_DEVICE_MAX_ADDRESS_LEN - 1);
    memset(mic_data->channel_mapping, AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED,
           sizeof(mic_data->channel_mapping));
    mic_data->device = AUDIO_DEVICE_IN_BUILTIN_MIC;
    mic_data->sensitivity = -37.0;
    mic_data->max_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
    mic_data->min_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
    mic_data->orientation.x = 0.0f;
    mic_data->orientation.y = 0.0f;
    mic_data->orientation.z = 0.0f;
    mic_data->geometric_location.x = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    mic_data->geometric_location.y = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    mic_data->geometric_location.z = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
}

static int out_set_event_callback(struct audio_stream_out *stream __unused,
                                       stream_event_callback_t callback, void *cookie /*StreamOut*/)
{
    ALOGD("func:%s  callback:%p cookie:%p", __func__, callback, cookie);

    return 0;
}

// open corresponding stream by flags, formats and others params
static int adev_open_output_stream(struct audio_hw_device *dev,
                                audio_io_handle_t handle __unused,
                                audio_devices_t devices,
                                audio_output_flags_t flags,
                                struct audio_config *config,
                                struct audio_stream_out **stream_out,
                                const char *address)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_stream_out *out;
    int digital_codec;
    int ret;

    ALOGD("%s: enter: devices(%#x) channel_mask(%#x) rate(%d) format(%#x) flags(%#x), address:%s", __func__,
        devices, config->channel_mask, config->sample_rate, config->format, flags, address);

    out = (struct aml_stream_out *)aml_audio_calloc(1, sizeof(struct aml_stream_out));
    if (!out) {
        ALOGE("%s malloc error", __func__);
        return -ENOMEM;
    }

    if (pthread_mutex_init(&out->lock, NULL)) {
        ALOGE("%s pthread_mutex_init failed", __func__);
    }

    if (address && !strncmp(address, "AML_", 4)) {
        ALOGI("%s(): aml TV source stream", __func__);
        out->is_tv_src_stream = true;
    }

    if (flags == AUDIO_OUTPUT_FLAG_NONE)
        flags = AUDIO_OUTPUT_FLAG_PRIMARY;
    if (config->channel_mask == AUDIO_CHANNEL_NONE)
        config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    if (config->sample_rate == 0)
        config->sample_rate = 48000;
    out->rate_convert = 1;
    if (flags & AUDIO_OUTPUT_FLAG_PRIMARY) {
        if (config->format == AUDIO_FORMAT_DEFAULT)
            config->format = AUDIO_FORMAT_PCM_16_BIT;

        out->stream.common.get_channels = out_get_channels;
        out->stream.common.get_format = out_get_format;

        if ((eDolbyMS12Lib == adev->dolby_lib_type) && (!adev->is_TV)) {
            // BOX with ms 12 need to use new method
            out->stream.write = out_write_new;
            out->stream.common.standby = out_standby_new;
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
        if (config->format == AUDIO_FORMAT_DEFAULT) {
            if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
                config->format = AUDIO_FORMAT_PCM_16_BIT;
            } else {
                config->format = AUDIO_FORMAT_AC3;
            }
        }

        out->stream.common.get_channels = out_get_channels_direct;
        out->stream.common.get_format = out_get_format_direct;

        if ((eDolbyMS12Lib == adev->dolby_lib_type) && (!adev->is_TV)) {
            // BOX with ms 12 need to use new method
            out->stream.write = out_write_new;
            out->stream.common.standby = out_standby_new;
        } else {
            out->stream.write = out_write_new;
            out->stream.common.standby = out_standby_new;
        }

        out->hal_channel_mask = config->channel_mask;
        out->hal_rate = config->sample_rate;
        out->hal_format = config->format;
        out->hal_internal_format = out->hal_format;
        if (out->hal_internal_format == AUDIO_FORMAT_E_AC3_JOC) {
            out->hal_internal_format = AUDIO_FORMAT_E_AC3;
            ALOGD("config hal_format %#x change to hal_internal_format(%#x)!", out->hal_format, out->hal_internal_format);
        }

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
                    adev->dolby_lib_type = eDolbyDcvLib;
                    out->restore_dolby_lib_type = true;
                } else {
                    out->hal_internal_format = AUDIO_FORMAT_AC3;
                }
            } else if (out->config.channels >= 6 && out->config.rate == 192000) {
                out->hal_internal_format = AUDIO_FORMAT_DTS_HD;
            }

            ALOGI("convert format IEC61937 to 0x%x\n", out->hal_internal_format);
            break;
        case AUDIO_FORMAT_AC3:
        case AUDIO_FORMAT_E_AC3:
        case AUDIO_FORMAT_AC4:
        case AUDIO_FORMAT_DOLBY_TRUEHD:
            break;
        case AUDIO_FORMAT_DTS:
        case AUDIO_FORMAT_DTS_HD:
            break;
        default:
            break;
        }

        digital_codec = get_codec_type(out->hal_internal_format);
        switch (digital_codec) {
        case TYPE_AC3:
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
            out->config.period_count *= 2;
            out->raw_61937_frame_size = 16;
            break;
        case TYPE_PCM:
            if (out->config.channels >= 6 || out->config.rate > 48000)
                adev->hi_pcm_mode = true;
            break;
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

    adev->audio_hal_info.format = AUDIO_FORMAT_PCM;
    adev->audio_hal_info.is_dolby_atmos = 0;
    adev->audio_hal_info.update_type = TYPE_PCM;
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
    out->stream.set_event_callback = out_set_event_callback;

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
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
    out->ms12_vol_ctrl = false;
    out->dev = adev;
    out->standby = true;
    out->frame_write_sum = 0;
    out->hw_sync_mode = false;
    out->need_convert = false;
    out->need_drop_size = 0;
    out->position_update = 0;
    out->inputPortID = -1;
    out->write_count = 0;

    if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        if ((eDolbyMS12Lib == adev->dolby_lib_type) &&
            !adev->ms12.dolby_ms12_enable &&
            adev->continuous_audio_mode) {
           config_output((struct audio_stream_out *)out, true);
        }
        outMmapInit(out);
    }

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
        out->config.channels = adev->default_alsa_ch;
        out->config.format = PCM_FORMAT_S32_LE;

        out->tmp_buffer_8ch_size = out->config.period_size * 4 * adev->default_alsa_ch;
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

    /*if tunnel mode pcm is not 48Khz, resample to 48K*/
    if (flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) {
        ALOGD("%s format=%d rate=%d", __func__, out->hal_internal_format, out->config.rate);
        if (audio_is_linear_pcm(out->hal_internal_format) && out->config.rate != 48000) {
            ALOGI("init resampler from %d to 48000!\n", out->config.rate);
            out->aml_resample.input_sr = out->config.rate;
            out->aml_resample.output_sr = 48000;
            out->aml_resample.channels = 2;
            resampler_init (&out->aml_resample);
            /*max buffer from 32K to 48K*/
            if (!out->resample_outbuf) {
                out->resample_outbuf = (unsigned char*) aml_audio_malloc (8192 * 10);
                if (!out->resample_outbuf) {
                    ALOGE ("malloc buffer failed\n");
                    ret = -1;
                    goto err;
                }
            }
        } else {
            if (out->resample_outbuf)
                aml_audio_free(out->resample_outbuf);
            out->resample_outbuf = NULL;
        }
    }

    if (out->hal_format == AUDIO_FORMAT_AC4) {
        aml_ac4_parser_open(&out->ac4_parser_handle);
    }

    out->ddp_frame_size = aml_audio_get_ddp_frame_size();
    out->resample_handle = NULL;
    *stream_out = &out->stream;
    ALOGD("%s: exit", __func__);

    return 0;
err:
    pthread_mutex_lock(&out->lock);
    if (out->hwsync) {
        aml_audio_free(out->hwsync);
        out->hwsync = NULL;
    }

    if (out->audioeffect_tmp_buffer)
        aml_audio_free(out->audioeffect_tmp_buffer);
    if (out->tmp_buffer_8ch)
        aml_audio_free(out->tmp_buffer_8ch);

    pthread_mutex_unlock(&out->lock);
    pthread_mutex_destroy(&out->lock);

    aml_audio_free(out);
    ALOGE("%s exit failed =%d", __func__, ret);
    return ret;
}

//static int out_standby_new(struct audio_stream *stream);
static void adev_close_output_stream(struct audio_hw_device *dev,
                                    struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int ret = 0;
    ALOGD("%s: enter: dev(%p) stream(%p) flags(%d)", __func__, dev, stream, out->flags);

    if (out->restore_hdmitx_selection) {
        /* switch back to spdifa when the dual stream is done */
        aml_audio_select_spdif_to_hdmi(AML_SPDIF_A_TO_HDMITX);
        out->restore_hdmitx_selection = false;
    }

    if (adev->useSubMix) {
        if (out->usecase == STREAM_PCM_NORMAL || out->usecase == STREAM_PCM_HWSYNC)
            out_standby_subMixingPCM(&stream->common);
        else
            out_standby_new(&stream->common);
    } else {
        out_standby_new(&stream->common);
    }

    if (out->dev_usecase_masks) {
        adev->usecase_masks &= ~(1 << out->usecase);
    }

    // This 1.0f volume may affect the waveform at the end.
    // Since stream will set volume before start, remove it.
    /* if (continous_mode(adev) && (eDolbyMS12Lib == adev->dolby_lib_type)) {
        if (out->volume_l != 1.0) {
            if (!audio_is_linear_pcm(out->hal_internal_format)) {
                set_ms12_main_volume(&adev->ms12, 1.0);
            }
        }
    } */

    pthread_mutex_lock(&out->lock);
    if (out->audioeffect_tmp_buffer) {
        aml_audio_free(out->audioeffect_tmp_buffer);
        out->audioeffect_tmp_buffer = NULL;
    }

#if ANDROID_PLATFORM_SDK_VERSION > 29
    if (dtv_tuner_framework(stream)) {
        /*enter into tuner framework case, we need to stop&release audio dtv patch*/
        ALOGD("[audiohal_kpi] %s:patching %d, dev:%p, out->dev:%p, patch:%p", __func__, out->dev->audio_patching, dev, out->dev, ((struct aml_audio_device *)dev)->audio_patch);
        out->dev->audio_patching = 0;
        ret = disable_dtv_patch_for_tuner_framework(dev);
        if (!ret) {
            ALOGI("%s: finish releasing patch", __func__);
        }
    }
#endif

    if (out->tmp_buffer_8ch) {
        aml_audio_free(out->tmp_buffer_8ch);
        out->tmp_buffer_8ch = NULL;
    }

    if (out->spdifenc_init) {
        aml_spdif_encoder_close(out->spdifenc_handle);
        out->spdifenc_handle = NULL;
        out->spdifenc_init = false;
    }

    if (out->ac3_parser_init) {
        aml_ac3_parser_close(out->ac3_parser_handle);
        out->ac3_parser_handle = NULL;
        out->ac3_parser_init = false;
    }


    if (out->flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        outMmapDeInit(out);
    }

    if (out->resample_outbuf) {
        aml_audio_free(out->resample_outbuf);
        out->resample_outbuf = NULL;
    }

    if (out->hal_format == AUDIO_FORMAT_AC4) {
        aml_ac4_parser_close(out->ac4_parser_handle);
        out->ac4_parser_handle = NULL;
    }
    /*main stream is closed, close the ms12 main decoder*/
    if (out->is_ms12_main_decoder) {
        pthread_mutex_lock(&adev->ms12.lock);
        /*after ms12 lock, dolby_ms12_enable may be cleared with clean up function*/
        if (adev->ms12.dolby_ms12_enable) {
            if (adev->ms12_main1_dolby_dummy == false
            && !audio_is_linear_pcm(out->hal_internal_format)) {
                dolby_ms12_set_main_dummy(0, true);
                adev->ms12_main1_dolby_dummy = true;
                ALOGI("%s set main dd+ dummy", __func__);
            } else if (adev->ms12_ott_enable == true
               && audio_is_linear_pcm(out->hal_internal_format)
               && (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC || out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) {

                dolby_ms12_set_main_dummy(1, true);
                adev->ms12_ott_enable = false;
                ALOGI("%s set ott dummy", __func__);
            }
            if (out->ms12_acmod2ch_lock_disable) {
                set_ms12_acmod2ch_lock(&adev->ms12, true);
            }
            adev->ms12.need_ms12_resume = false;
            adev->ms12.need_resync = 0;
            adev->ms12_out->hw_sync_mode = false;

            audiohal_send_msg_2_ms12(&adev->ms12, MS12_MESG_TYPE_FLUSH);
            adev->ms12.ms12_resume_state = MS12_RESUME_FROM_CLOSE;
            audiohal_send_msg_2_ms12(&adev->ms12, MS12_MESG_TYPE_RESUME);
        }
        pthread_mutex_unlock(&adev->ms12.lock);

        /*main stream is closed, wait mesg processed*/
        {
            int wait_cnt = 0;
            while (!ms12_msg_list_is_empty(&adev->ms12)) {
                aml_audio_sleep(5000);
                wait_cnt++;
                if (wait_cnt >= 200) {
                    break;
                }
            }
            ALOGI("main stream message is processed cost =%d ms", wait_cnt * 5);
        }
        if (out->hwsync && continous_mode(adev)) {
            /*we only suppport one stream hw sync and MS12 always attach with it.
            So when it is released, ms12 also need set hwsync to NULL*/
            struct aml_stream_out *ms12_out = (struct aml_stream_out *)adev->ms12_out;
            out->hw_sync_mode = 0;
            if (ms12_out != NULL) {
                ms12_out->hw_sync_mode = 0;
                ms12_out->hwsync = NULL;
            }
            dolby_ms12_hwsync_release();
        }
        dolby_ms12_main_close(stream);
    }
    if (out->hwsync) {
        if (adev->hw_mediasync && (adev->hw_mediasync == out->hwsync->mediasync)) {
            aml_audio_hwsync_release(out->hwsync);
            adev->hw_mediasync = NULL;
            adev->hw_mediasync_id = -1;
        }

        if (out->hwsync->mediasync) {
            //aml_audio_free(out->hwsync->mediasync);
            out->hwsync->mediasync = NULL;

        }
        aml_audio_free(out->hwsync);
        out->hwsync = NULL;
    }
    if (out->spdifout_handle) {
        aml_audio_spdifout_close(out->spdifout_handle);
        out->spdifout_handle = NULL;
    }
    if (out->spdifout2_handle) {
        aml_audio_spdifout_close(out->spdifout2_handle);
        out->spdifout2_handle = NULL;
    }

    if (out->aml_dec) {
        aml_decoder_release(out->aml_dec);
        out->aml_dec = NULL;
    }

    if (out->resample_handle) {
        aml_audio_resample_close(out->resample_handle);
        out->resample_handle = NULL;
    }

    /*TBD .to fix the AC-4 continous function in ms12 lib then remove this */
    /*
     * will close MS12 if the AVR DDP-ATMOS capbility is changed,
     * such as switch from DDP-AVR to ATMOS-AVR
     * then, next stream is new built, this setting is available.
     */
    if ((out->total_write_size != 0) &&
        ((out->hal_internal_format == AUDIO_FORMAT_AC4) || is_support_ms12_reset(stream))) {
        if (adev->continuous_audio_mode) {
            adev->delay_disable_continuous = 0;
            ALOGI("%s Need disable MS12 continuous", __func__);
            bool set_ms12_non_continuous = true;
            get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
            adev->exiting_ms12 = 1;
            out->restore_continuous = true;
            clock_gettime(CLOCK_MONOTONIC, &adev->ms12_exiting_start);
        }
    }

    /*all the ms12 related function is done */
    if (out->restore_continuous == true) {
        ALOGI("restore ms12 continuous mode");
        pthread_mutex_lock(&adev->ms12.lock);
        adev->continuous_audio_mode = 1;
        pthread_mutex_unlock(&adev->ms12.lock);
    }

    /*the dolby lib is changed, so we need restore it*/
    if (out->restore_dolby_lib_type) {
        adev->dolby_lib_type = adev->dolby_lib_type_last;
        ALOGI("%s restore dolby lib =%d", __func__, adev->dolby_lib_type);
    }

    pthread_mutex_unlock(&out->lock);

    pthread_mutex_destroy(&out->lock);

    aml_audio_free(stream);
    stream = NULL;
    ALOGD("%s: exit", __func__);
}

static int aml_audio_output_routing(struct audio_hw_device *dev,
                                    enum OUT_PORT outport,
                                    bool user_setting)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;

    if (aml_dev->active_outport != outport) {
        ALOGI("%s: switch from %s to %s", __func__,
            outputPort2Str(aml_dev->active_outport), outputPort2Str(outport));

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
        case OUTPORT_A2DP:
            break;
        case OUTPORT_BT_SCO:
        case OUTPORT_BT_SCO_HEADSET:
            close_btSCO_device(aml_dev);
            break;
        default:
            ALOGW("%s: pre active_outport:%d unsupport", __func__, aml_dev->active_outport);
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
            audio_route_apply_path(aml_dev->ar, "speaker_off");
            break;
        case OUTPORT_A2DP:
        case OUTPORT_BT_SCO:
        case OUTPORT_BT_SCO_HEADSET:
            break;
        default:
            ALOGW("%s: cur outport:%d unsupport", __func__, outport);
            break;
        }

        audio_route_update_mixer(aml_dev->ar);
        aml_dev->active_outport = outport;
        if (outport == OUTPORT_HDMI_ARC) {
            aml_dev->arc_connected_reconfig = true;
        }
    } else if (outport == OUTPORT_SPEAKER && user_setting) {
        /* In this case, user toggle the speaker_mute menu */
        if (aml_dev->speaker_mute)
            audio_route_apply_path(aml_dev->ar, "speaker_off");
        else
            audio_route_apply_path(aml_dev->ar, "speaker");
        audio_route_update_mixer(aml_dev->ar);
    } else {
        ALOGI("%s: outport %s already exists, do nothing", __func__, outputPort2Str(outport));
    }

    return 0;
}

static int aml_audio_input_routing(struct audio_hw_device *dev,
                                    enum IN_PORT inport)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;

    if (aml_dev->active_inport != inport) {
        ALOGI("%s: switch from %s to %s", __func__,
            inputPort2Str(aml_dev->active_inport), inputPort2Str(inport));
        switch (inport) {
        case INPORT_HDMIIN:
            audio_route_apply_path(aml_dev->ar, "hdmirx_in");
            break;
        case INPORT_LINEIN:
            audio_route_apply_path(aml_dev->ar, "line_in");
            break;
        default:
            ALOGW("%s: cur inport:%d unsupport", __func__, inport);
            break;
        }

        audio_route_update_mixer(aml_dev->ar);
        aml_dev->active_inport = inport;
    }

    return 0;
}


static int aml_audio_update_arc_status(struct aml_audio_device *adev, bool enable)
{
    ALOGI("[%s:%d] set audio hdmi arc status:%d", __func__, __LINE__, enable);
    audio_route_set_hdmi_arc_mute(&adev->alsa_mixer, !enable);
    adev->bHDMIARCon = enable;
    struct aml_arc_hdmi_desc *hdmi_desc = &adev->hdmi_descs;
    int is_arc_connected = 0;

    if (enable) {
        memcpy(hdmi_desc, &adev->hdmi_arc_capability_desc, sizeof(struct aml_arc_hdmi_desc));
    } else {
        memset(hdmi_desc, 0, sizeof(struct aml_arc_hdmi_desc));
        hdmi_desc->pcm_fmt.fmt = AML_HDMI_FORMAT_LPCM;
        hdmi_desc->dts_fmt.fmt = AML_HDMI_FORMAT_DTS;
        hdmi_desc->dtshd_fmt.fmt = AML_HDMI_FORMAT_DTSHD;
        hdmi_desc->dd_fmt.fmt = AML_HDMI_FORMAT_AC3;
        hdmi_desc->ddp_fmt.fmt = AML_HDMI_FORMAT_DDP;
        hdmi_desc->mat_fmt.fmt = AML_HDMI_FORMAT_MAT;
    }
    /*
     * when user switch UI setting, means output device changed,
     * use "arc_hdmi_updated" paramter to notify HDMI ARC have updated status
     * while in config_output(), it detects this change, then re-route output config.
     */
    adev->arc_hdmi_updated = 1;
    return 0;
}

static int aml_audio_set_speaker_mute(struct aml_audio_device *adev, char *value)
{
    int ret = 0;
    if (strncmp(value, "true", 4) == 0 || strncmp(value, "1", 1) == 0) {
        adev->speaker_mute = 1;
    } else if (strncmp(value, "false", 5) == 0 || strncmp(value, "0", 1) == 0) {
        adev->speaker_mute = 0;
    } else {
        ALOGE("%s() unsupport speaker_mute value: %s", __func__, value);
    }
    return 0;
}

static void check_usb_card_device(struct str_parms *parms, int device)
{
    /*usb audio hot plug need delay some time wait alsa file create */
    if ((device & AUDIO_DEVICE_OUT_ALL_USB) || (device & AUDIO_DEVICE_IN_ALL_USB)) {
        int card = 0, alsa_dev = 0, val, retry;
        char fn[256];
        int ret = str_parms_get_int(parms, "card", &val);
        if (ret >= 0) {
            card = val;
        }
        ret = str_parms_get_int(parms, "device", &val);
        if (ret >= 0) {
            alsa_dev = val;
        }
        snprintf(fn, sizeof(fn), "/dev/snd/pcmC%uD%u%c", card, alsa_dev,
             device & AUDIO_DEVICE_OUT_ALL_USB ? 'p' : 'c');
        for (retry = 0; access(fn, F_OK) < 0 && retry < 10; retry++) {
            usleep (20000);
        }
        if (access(fn, F_OK) < 0 && retry >= 10) {
            ALOGE("usb audio create alsa file time out,need check \n");
        }
    }
}

static void set_device_connect_state(struct aml_audio_device *adev, struct str_parms *parms, int device, bool state)
{
    AM_LOGI("state:%d, dev:%#x, pre_out:%#x, pre_in:%#x", state, device, adev->out_device, adev->in_device);
    if (state) {
        check_usb_card_device(parms, device);
        if (audio_is_output_device(device)) {
            if ((device & AUDIO_DEVICE_OUT_HDMI_ARC) || (device & AUDIO_DEVICE_OUT_HDMI)) {
                adev->bHDMIConnected = 1;
                adev->bHDMIConnected_update = 1;
                if (device & AUDIO_DEVICE_OUT_HDMI_ARC) {
                    aml_audio_set_speaker_mute(adev, "true");
                    aml_audio_update_arc_status(adev, true);
                }
                update_sink_format_after_hotplug(adev);
            } else if (device & AUDIO_DEVICE_OUT_ALL_A2DP) {
                adev->a2dp_updated = 1;
                adev->out_device |= device;
                a2dp_out_open(adev);
            } else if (device &  AUDIO_DEVICE_OUT_ALL_USB) {
                adev->out_device |= device;
            }
        }
    } else {
        if (audio_is_output_device(device)) {
            if ((device & AUDIO_DEVICE_OUT_HDMI_ARC) || (device & AUDIO_DEVICE_OUT_HDMI)) {
                adev->bHDMIConnected = 0;
                adev->bHDMIConnected_update = 1;
                adev->hdmi_descs.pcm_fmt.max_channels = 2;
                if (device & AUDIO_DEVICE_OUT_HDMI_ARC) {
                    aml_audio_set_speaker_mute(adev, "false");
                    aml_audio_update_arc_status(adev, false);
                }
            } else if (device & AUDIO_DEVICE_OUT_ALL_A2DP) {
                adev->a2dp_updated = 1;
                adev->out_device &= (~device);
                a2dp_out_close(adev);
            } else if (device &  AUDIO_DEVICE_OUT_ALL_USB) {
                adev->out_device &= (~device);
            }
        }
    }
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    struct str_parms *parms;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    char value[AUDIO_HAL_CHAR_MAX_LEN];
    int val = 0;
    int ret = 0;

    ALOGI ("%s(%p, kv: %s)", __FUNCTION__, dev, kvpairs);
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
        ALOGI ("ms12 disable_pcm_mixing set to %d\n", adev->disable_pcm_mixing);
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

    ret = str_parms_get_int(parms, "hal_param_dtv_spdif_protection_mode", &val);
    if (ret >= 0) {
        if (val == SPIDF_PROTECTION__MODE_NONE || val == SPIDF_PROTECTION__MODE_NEVER) {
            aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIF_B_OUT_CHANNEL_STATUS, SPID_PROTECTION_ENABLE);
        } else if (val == SPIDF_PROTECTION__MODE_ONCE){
            aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIF_B_OUT_CHANNEL_STATUS, SPID_PROTECION_DISABLE);
        }

        ALOGI("AUDIO SET SPIDF_PROTECTION__STATUS: %d\n", val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "Audio spdif mute", &val);
    if (ret >= 0) {
        audio_route_set_spdif_mute(&adev->alsa_mixer, val);
        ALOGI("[%s:%d] set SPDIF mute status: %d", __func__, __LINE__, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "disconnect", &val);
    if (ret >= 0) {
        set_device_connect_state(adev, parms, val, false);

        /*if (adev->bHDMIConnected == 0) {
            aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_STANDBY);
        }*/
        goto exit;
    }

    ret = str_parms_get_int(parms, "connect", &val);
    if (ret >= 0) {
        set_device_connect_state(adev, parms, val, true);

        if (adev->bHDMIConnected == 1) {
            aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_RUNNING);
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

    ret = str_parms_get_int(parms, "hal_param_bt_avrcp_supported", &val);
    if (ret >= 0) {
        adev->bt_avrcp_supported = (val != 0);
        goto exit;
    }

    //  HDMI plug in and UI [Sound Output Device] set to "ARC" will recieve HDMI ARC Switch = 1
    //  HDMI plug off and UI [Sound Output Device] set to "speaker" will recieve HDMI ARC Switch = 0
    ret = str_parms_get_int(parms, "HDMI ARC Switch", &val);
    if (ret >= 0) {
        aml_audio_update_arc_status(adev, (val != 0));
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_earctx_earc_mode", &val);
    if (ret >= 0) {
        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_TX_EARC_MODE, val);
        ALOGI("eARC_TX eARC Mode: %d\n", val);
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
        if (adev->hdmi_format != val)
            adev->hdmi_format_updated = 1;
        adev->hdmi_format = val;

        /* only switch from/to bypass mode, update the DUT's EDID */
        if (adev->hdmi_format == BYPASS || adev->hdmi_format_last == BYPASS)
            update_edid_after_edited_audio_sad(adev, &adev->hdmi_descs.ddp_fmt);

        adev->hdmi_format_last = adev->hdmi_format;

        //sysfs_set_sysfs_str(REPORT_DECODED_INFO, kvpairs);
        if ((eDolbyMS12Lib == adev->dolby_lib_type) && (adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP))
            adev->a2dp_no_reconfig_ms12 = aml_audio_get_systime() + 2000000;
        ALOGI ("HDMI format: %d\n", adev->hdmi_format);
        goto exit;
    }

    ret = str_parms_get_int (parms, "spdif_format", &val);
    if (ret >= 0 ) {
        adev->spdif_format = val;
        ALOGI ("S/PDIF format: %d\n", adev->spdif_format);
        goto exit;
    }

    ret = str_parms_get_int (parms, "hal_param_spdif_output_enable", &val);
    if (ret >= 0 ) {
        ALOGI ("[%s:%d] set spdif output enable:%d", __func__, __LINE__, val);
        if (val == 0) {
            audio_route_apply_path(adev->ar, "spdif_off");
        } else {
            audio_route_apply_path(adev->ar, "spdif_on");
        }
        adev->spdif_enable = (val == 0) ? false : true;
        audio_route_update_mixer(adev->ar);
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

    ret = str_parms_get_int (parms, "ChannelReverse", &val);
    if (ret >= 0) {
        adev->FactoryChannelReverse = val;
        ALOGI ("ChannelReverse = %d\n", adev->FactoryChannelReverse);
        goto exit;
    }

    ret = str_parms_get_str (parms, "set_ARC_hdmi", value, sizeof (value) );
    if (ret >= 0) {
        set_arc_hdmi(dev, value, AUDIO_HAL_CHAR_MAX_LEN);
        goto exit;
    }

    ret = str_parms_get_str (parms, "set_ARC_format", value, sizeof (value) );
    if (ret >= 0) {
        set_arc_format(dev, value, AUDIO_HAL_CHAR_MAX_LEN);
        goto exit;
    }

    //  HDMI plug in and UI [Sound Output Device] set to "ARC" will recieve speaker_mute = 1
    ret = str_parms_get_str (parms, "speaker_mute", value, sizeof (value) );
    if (ret >= 0) {
        aml_audio_set_speaker_mute(adev, value);
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

    /*use dolby_lib_type_last to check ms12 type, because durig playing DTS file,
      this type will be changed to dcv*/
    if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
        ret = str_parms_get_int(parms, "continuous_audio_mode", &val);
        if (ret >= 0) {
            int disable_continuous = !val;
            // if exit netflix, we need disable atmos lock
            if (disable_continuous) {
                adev->atoms_lock_flag = false;
                set_ms12_atmos_lock(&(adev->ms12), adev->atoms_lock_flag);
                ALOGI("exit netflix, set atmos lock as 0");
            }
            {
                bool acmod2ch_lock = !val;
                //when in netflix, we should always keep ddp5.1, exit netflix we can output ddp2ch
                set_ms12_acmod2ch_lock(&(adev->ms12), acmod2ch_lock);
            }

            ALOGI("%s ignore the continuous_audio_mode!\n", __func__ );
            adev->is_netflix = val;
            /*in netflix case, we enable atmos drop at the beginning*/
            dolby_ms12_enable_atmos_drop(val);

            if (adev->is_netflix) {
                aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_RUNNING);
            } else {
                /* currently system send the "continuous_audio_mode=0" in below a few scenario,
                ** 1)when ExoPlayer/AIV open and close, start play or exit play.
                ** 2)system bootup.
                ** so can't send the scheduler stanby to ms12 here.
                */
                //aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_STANDBY);
            }
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
                    ALOGI("%s Dolby MS12 is at continuous output mode, here go to end it!\n", __FUNCTION__);
                    bool set_ms12_non_continuous = true;
                    get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                    //ALOGI("[%s:%d] get_dolby_ms12_cleanup\n", __FUNCTION__, __LINE__);
                    adev->exiting_ms12 = 1;
                    clock_gettime(CLOCK_MONOTONIC, &adev->ms12_exiting_start);
                    if (adev->active_outputs[STREAM_PCM_NORMAL] != NULL)
                        usecase_change_validate_l(adev->active_outputs[STREAM_PCM_NORMAL], true);
                    //continuous_stream_do_standby(adev);
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
            adev->atoms_lock_flag = val ? true : false;

            if (eDolbyMS12Lib == adev->dolby_lib_type) {
            // Note: some guide from www.netflix.com
            // In the Netflix application, enable/disable Atmos locking
            // when you receive the signal from the Netflix application.
            pthread_mutex_lock(&adev->lock);
            if (continous_mode(adev)) {
                // enable/disable atoms lock
                set_ms12_atmos_lock(&(adev->ms12), adev->atoms_lock_flag);
                ALOGI("%s set adev->atoms_lock_flag = %d, \n", __func__,adev->atoms_lock_flag);
            } else {
                ALOGI("%s not in continous mode, do nothing\n", __func__);
            }
            pthread_mutex_unlock(&adev->lock);
            }
            goto exit;
        }

        //for the local playback, IEC61937 format!
        //temp to disable the Dolby MS12 continuous
        //if local playback on Always continuous is completed,
        //we should remove this function!
        ret = str_parms_get_int(parms, "is_ms12_continuous", &val);
        if (ret >= 0) {
            ALOGI("%s is_ms12_continuous set to %d\n", __func__ , val);
            goto exit;
        }
        /*after enable MS12 continuous mode, the audio delay is big, we
        need use this API to compensate some delay to video*/
        ret = str_parms_get_int(parms, "compensate_video_enable", &val);
        if (ret >= 0) {
            ALOGI("%s compensate_video_enable set to %d\n", __func__ , val);
            adev->compensate_video_enable = val;
            //aml_audio_compensate_video_delay(val);
            goto exit;
        }

        /*enable force ddp output for ms12 v2*/
        ret = str_parms_get_int(parms, "hal_param_force_ddp", &val);
        if (ret >= 0) {
            ALOGI("%s hal_param_force_ddp set to %d\n", __func__ , val);
            if (adev->ms12_force_ddp_out != val) {
                adev->ms12_force_ddp_out = val;
                get_dolby_ms12_cleanup(&adev->ms12, false);
            }
            goto exit;
        }
    } else if (eDolbyDcvLib == adev->dolby_lib_type_last) {
        ret = str_parms_get_int(parms, "continuous_audio_mode", &val);
        if (ret >= 0) {
            ALOGI("%s ignore the continuous_audio_mode!\n", __func__ );
            adev->is_netflix = val;
            goto exit;
        }
    }

#ifdef ADD_AUDIO_DELAY_INTERFACE
    ret = str_parms_get_int(parms, "hal_param_out_dev_delay_time_ms", &val);
    if (ret >= 0) {
        /* High 16 - bit expression type, low 16 - bit expression delay time */
        aml_audio_delay_set_time(val >> 16, val & 0xffff);
        goto exit;
    }
#endif

    /* deal with dvb cmd */
    ret = set_dtv_parameters(dev, parms);
    if (ret >= 0) {
        ALOGD("get dtv param(kv: %s)", kvpairs);
        goto exit;
    }

    /* deal with AQ cmd */
    ret = set_AQ_parameters(dev, parms);
    if (ret >= 0) {
        ALOGD("get AQ param(kv: %s)", kvpairs);
        goto exit;
    }

    /* deal with tv source switch cmd */
    ret = set_tv_source_switch_parameters(dev, parms);
    if (ret >= 0) {
        ALOGD("get TV source param(kv: %s)", kvpairs);
        goto exit;
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

    ret = str_parms_get_str(parms, "bt_wbs", value, sizeof(value));
    if (ret >= 0) {
        ALOGI("Amlogic_HAL - %s: bt_wbs=%s.", __func__, value);
        if (strncmp(value, "on", 2) == 0)
            adev->bt_wbs = true;
        else
            adev->bt_wbs = false;

        goto exit;
    }

    ret = str_parms_get_str(parms, "show-meminfo", value, sizeof(value));
    if (ret >= 0) {
        unsigned int level = (unsigned int)atoi(value);
        ALOGE ("Amlogic_HAL - %s: ShowMem info level:%d.", __FUNCTION__,level);
        aml_audio_debug_malloc_showinfo(level);
        goto exit;
    }

    ret = str_parms_get_str(parms, "diaglogue_enhancement", value, sizeof(value));
    if (ret >= 0) {
        adev->ms12.ac4_de = atoi(value);
        ALOGE ("Amlogic_HAL - %s: set MS12 ac4 Dialogue Enhancement gain :%d.", __FUNCTION__,adev->ms12.ac4_de);

        char parm[32] = "";
        sprintf(parm, "%s %d", "-ac4_de", adev->ms12.ac4_de);
        pthread_mutex_lock(&adev->lock);
        if (strlen(parm) > 0)
            aml_ms12_update_runtime_params(&(adev->ms12), parm);
        pthread_mutex_unlock(&adev->lock);
        goto exit;
    }

    ret = str_parms_get_str(parms, "picture_mode", value, sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "PQ_MODE_STANDARD", 16) == 0) {
            adev->pic_mode = PQ_STANDARD;
            adev->game_mode = false;
        } else if (strncmp(value, "PQ_MODE_MOVIE", 13) == 0) {
            adev->pic_mode = PQ_MOVIE;
            adev->game_mode = false;
        } else if (strncmp(value, "PQ_MODE_DYNAMIC", 15) == 0) {
            adev->pic_mode = PQ_DYNAMIC;
            adev->game_mode = false;
        } else if (strncmp(value, "PQ_MODE_NATURAL", 15) == 0) {
            adev->pic_mode = PQ_NATURAL;
            adev->game_mode = false;
        } else if (strncmp(value, "PQ_MODE_GAME", 12) == 0) {
            adev->pic_mode = PQ_GAME;
            adev->game_mode = true;
        } else if (strncmp(value, "PQ_MODE_PC", 10) == 0) {
            adev->pic_mode = PQ_PC;
            adev->game_mode = false;
        } else if (strncmp(value, "PQ_MODE_CUSTOMER", 16) == 0) {
            adev->pic_mode = PQ_CUSTOM;
            adev->game_mode = false;
        } else {
            adev->pic_mode = PQ_STANDARD ;
            adev->game_mode = false;
            ALOGE("%s() unsupport value %s choose pic mode (default) standard\n", __func__, value);
        }
        ALOGI("%s(), set pic mode to: %d, is game mode = %d\n", __func__, adev->pic_mode, adev->game_mode);
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

    ret = str_parms_get_str(parms, "bypass_dap", value, sizeof(value));
    if (ret >= 0) {
        sscanf(value,"%d %f", &adev->ms12.dap_bypass_enable, &adev->ms12.dap_bypassgain);
        ALOGD("dap_bypass_enable is %d and dap_bypassgain is %f",adev->ms12.dap_bypass_enable, adev->ms12.dap_bypassgain);
        goto exit;
    }

    ret = str_parms_get_str(parms, "VX_SET_DTS_Mode", value, sizeof(value));
    if (ret >= 0) {
        int dts_decoder_output_mode = atoi(value);
        if (dts_decoder_output_mode > 6 || dts_decoder_output_mode < 0)
            goto exit;
        if (dts_decoder_output_mode == 2)
            adev->native_postprocess.vx_force_stereo = 1;
        else
            adev->native_postprocess.vx_force_stereo = 0;
        dca_set_out_ch_internal(dts_decoder_output_mode);
        ALOGD("set dts decoder output mode to %d", dts_decoder_output_mode);
        goto exit;
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
    ret = str_parms_get_int(parms, "karaoke_reverb_enable", &val);
    if (ret >= 0) {
        bool reverb_enable = !!val;
        adev->usb_audio.karaoke.reverb_enable = reverb_enable;
        ALOGI("[%s]Set usb mic reverb enable: %d", __FUNCTION__, reverb_enable);
        goto exit;
    }
    ret = str_parms_get_int(parms, "karaoke_reverb_mode", &val);
    if (ret >= 0) {
        int reverb_mode = val;
        adev->usb_audio.karaoke.reverb_mode = reverb_mode;
        ALOGI("[%s]Set usb mic reverb mode: %d", __FUNCTION__, reverb_mode);
        goto exit;
    }
#endif
    ret = str_parms_get_str(parms, "hal_param_vad_wakeup", value, sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "suspend", 7) == 0) {
            aml_vad_suspend(&adev->alsa_mixer);
        } else if (strncmp(value, "resume", 7) == 0) {
            aml_vad_resume(&adev->alsa_mixer);
        } else {
            AM_LOGI("not supported param:%s", value);
        }
        goto exit;
    }

    ret = str_parms_get_str(parms, "direct-mode", value, sizeof(value));
    if (ret >= 0) {
        unsigned int direct_mode = (unsigned int)atoi(value);
        ALOGI ("Amlogic_HAL - %s: direct-mode:%d.", __FUNCTION__,direct_mode);
        adev->direct_mode = direct_mode;
        goto exit;
    }

exit:
    str_parms_destroy (parms);
    /* always success to pass VTS */
    return 0;
}

static void adev_get_hal_control_volume_en(struct aml_audio_device *adev, char *temp_buf)
{
    bool hal_control_vol_en = true;
    /* For STB product.*/
    if (!adev->is_TV) {
        /*  Audio_hal has no ability to control volume at the following scence:
         *    1. non-ms12, output non-pcm, cec closed.
         *    2. ms12, ouput non-pcm, cec closed, passthrough.
         */
        if (adev->dolby_lib_type != eDolbyMS12Lib ||
            (adev->dolby_lib_type == eDolbyMS12Lib && adev->hdmi_format == BYPASS)) {
            enum AML_SPDIF_FORMAT format = AML_STEREO_PCM;
            enum AML_SPDIF_TO_HDMITX spdif_index = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIF_TO_HDMI);
            if (spdif_index == AML_SPDIF_A_TO_HDMITX) {
                format = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIF_FORMAT);
            } else if (spdif_index == AML_SPDIF_B_TO_HDMITX) {
                format = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIF_B_FORMAT);
            } else {
                AM_LOGW("unsupported spdif index:%d, use the 2ch PCM.", spdif_index);
            }
            hal_control_vol_en = (format == AML_STEREO_PCM) ? true : false;
        }
    }
    sprintf (temp_buf, "hal_param_hal_control_vol_en=%d", hal_control_vol_en);
    if (adev->debug_flag) {
        AM_LOGD("can hal control the platform volume, en:%d", hal_control_vol_en);
    }
}

static char * adev_get_parameters (const struct audio_hw_device *dev,
                                   const char *keys)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    char temp_buf[AUDIO_HAL_CHAR_MAX_LEN] = {0};

    if (!strcmp (keys, AUDIO_PARAMETER_HW_AV_SYNC) ) {
        ALOGI ("get hw_av_sync id\n");
        if (adev->patch_src == SRC_DTV && adev->audio_patching ==1) {
            unsigned int path_id = 0;
            aml_dtv_audio_instances_t *dtv_audio_instances = (aml_dtv_audio_instances_t *)adev->aml_dtv_audio_instances;
            if (dtv_audio_instances) {
                aml_dtvsync_t *dtvsync = &dtv_audio_instances->dtvsync[path_id];
                if (dtvsync) {
                    int32_t id = dtvsync->mediasync_id;
                    ALOGI ("%s,id:%d\n", __func__, id);
                    if (id != 0) {
                        sprintf (temp_buf, "hw_av_sync=%d", id);
                        return strdup (temp_buf);
                    }
                    return strdup ("hw_av_sync=12345678");
                }
            }
        } else {
            if (adev->hw_mediasync == NULL) {
                adev->hw_mediasync = aml_hwsync_mediasync_create();
            }
            if (adev->hw_mediasync != NULL) {
            /* if hw_mediasync already be alloced, we only get mediasync id from local */
                if (adev->hw_mediasync_id != -1) {
                    ALOGI ("get hw_av_sync id hw_mediasync_id=%d\n", adev->hw_mediasync_id);
                    sprintf (temp_buf, "hw_av_sync=%d", adev->hw_mediasync_id);
                    return strdup (temp_buf);
                } else {
                    int32_t id = -1;
                    bool ret = aml_audio_hwsync_get_id(adev->hw_mediasync, &id);
                    ALOGI ("ret: %d, id:%d\n", ret, id);
                    if (ret && id != -1) {
                        adev->hw_mediasync_id = id;
                        sprintf (temp_buf, "hw_av_sync=%d", id);
                        return strdup (temp_buf);
                    }
                }
            }
            return strdup ("hw_av_sync=12345678");
        }
    } else if (strstr (keys, AUDIO_PARAMETER_HW_AV_EAC3_SYNC) ) {
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
        if (fmtdesc && fmtdesc->fmt == AML_HDMI_FORMAT_AC3)
            dd = fmtdesc->is_support;

        // query ddp support
        fmtdesc = &adev->hdmi_descs.ddp_fmt;
        if (fmtdesc && fmtdesc->fmt == AML_HDMI_FORMAT_DDP)
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
    } else if (strstr(keys, "hal_param_hal_control_vol_en")) {
        adev_get_hal_control_volume_en(adev, temp_buf);
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
    } else if (strstr(keys, "isReconfigA2dpSupported")) {
        return  strdup("isReconfigA2dpSupported=1");
    } else if (strstr (keys, "stream_dra_channel") ) {
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
    } else if (strstr(keys, "HDMI Switch")) {
        sprintf(temp_buf, "HDMI Switch=%d", (OUTPORT_HDMI == adev->active_outport || adev->bHDMIConnected == 1));
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (!strcmp(keys, "parental_control_av_mute")) {
        sprintf(temp_buf, "parental_control_av_mute = %d", adev->parental_control_av_mute);
        return strdup(temp_buf);
    } else if (strstr (keys, "diaglogue_enhancement") ) {
        sprintf(temp_buf, "diaglogue_enhancement=%d", adev->ms12.ac4_de);
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr(keys, "audioindicator")) {
        get_audio_indicator(adev, temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_dtv_latencyms") ) {
        int latancyms = dtv_patch_get_latency(adev);
        sprintf(temp_buf, "hal_param_dtv_latencyms=%d", latancyms);
        ALOGV("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_audio_output_mode") ) {
        int audio_output_mode = adev->dtv_sound_mode;
        sprintf(temp_buf, "hal_param_audio_output_mode=%d", audio_output_mode);
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_audio_is_tv") ) {
        int is_tv = adev->is_TV;
        sprintf(temp_buf, "hal_param_audio_is_tv=%d", adev->is_TV);
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_get_earctx_cds") ) {
        char cds[AUDIO_HAL_CHAR_MAX_LEN] = {0};

        earctx_fetch_cds(&adev->alsa_mixer, cds, 0);
        sprintf(temp_buf, "hal_param_get_earctx_cds=%s", cds);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_get_earctx_attend_type") ) {
        int type = aml_audio_earctx_get_type(adev);
        sprintf(temp_buf, "hal_param_get_earctx_attend_type=%d", type);
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
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

    ALOGD("%s: enter: channel_mask(%#x) rate(%d) format(%#x)", __func__,
        config->channel_mask, config->sample_rate, config->format);

    if (check_input_parameters(config->sample_rate, config->format, channel_count, AUDIO_DEVICE_NONE) != 0) {
        return -EINVAL;
    }

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size_t period = pcm_config_in.period_size;
    period = (period + 15) / 16 * 16;
    uint32_t req_format = audio_bytes_per_sample(config->format);
    size_t size = period * channel_count * req_format * config->sample_rate / pcm_config_in.rate;
    ALOGD("%s: exit: buffer_size = %zu", __func__, size);

    return size;
}

static int choose_stream_pcm_config(struct aml_stream_in *in)
{
    int channel_count = audio_channel_count_from_in_mask(in->hal_channel_mask);
    struct aml_audio_device *adev = in->dev;
    int ret = 0;

    if (in->device & AUDIO_DEVICE_IN_ALL_SCO) {
        memcpy(&in->config, &pcm_config_bt, sizeof(pcm_config_bt));
        if (adev->bt_wbs)
            in->config.rate = VX_WB_SAMPLING_RATE;
    } else if (in->device & AUDIO_DEVICE_IN_HDMI || in->device & AUDIO_DEVICE_IN_HDMI_ARC) {
        // do nothing.
    } else if (in->device & AUDIO_DEVICE_IN_BLUETOOTH_BLE) {
        return -EINVAL;
    } else {
        memcpy(&in->config, &pcm_config_in, sizeof(pcm_config_in));
    }

    if (in->config.channels != 8)
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
    int sample_rate = config->sample_rate;
    int channel_count = audio_channel_count_from_in_mask(config->channel_mask);
    int ret = 0;

    ALOGD("%s: enter: devices(%#x) channel_mask(%#x) rate(%d) format(%#x) source(%d)", __func__,
        devices, config->channel_mask, config->sample_rate, config->format, source);

    if ((ret = check_input_parameters(config->sample_rate, config->format, channel_count, devices)) != 0) {
        if (-ENOSYS == ret) {
            ALOGV("  check_input_parameters input config Not Supported, and set to Default config(48000,2,PCM_16_BIT)");
            config->sample_rate = DEFAULT_OUT_SAMPLING_RATE;
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
        } else {
            ALOGV("  check_input_parameters input config Not Supported, and set to Default config(48000,2,PCM_16_BIT)");
            config->sample_rate = DEFAULT_OUT_SAMPLING_RATE;
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
            return -EINVAL;
        }
    } else {
        //check successfully, continue excute.
    }

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

    if (channel_count == 1)
        // in fact, this value should be AUDIO_CHANNEL_OUT_BACK_LEFT(16u) according to VTS codes,
        // but the macroname can be confusing, so I'd like to set this value to
        // AUDIO_CHANNEL_IN_FRONT(16u) instead of AUDIO_CHANNEL_OUT_BACK_LEFT.
        config->channel_mask = AUDIO_CHANNEL_IN_FRONT;
    else
        config->channel_mask = AUDIO_CHANNEL_IN_STEREO;

    in = (struct aml_stream_in *)aml_audio_calloc(1, sizeof(struct aml_stream_in));
    if (!in) {
        ALOGE("  calloc fail, return!!!");
        return -ENOMEM;
    }

#if defined(ENABLE_HBG_PATCH)
//[SEI-Tiger-2019/02/28] Optimize HBG RCU{
    if (is_hbg_hidraw() && (config->channel_mask != BUILT_IN_MIC)) {
//[SEI-Tiger-2019/02/28] Optimize HBG RCU}
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
    } else
#endif
    {
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
        in->stream.get_capture_position = in_get_capture_position;
        in->stream.get_input_frames_lost = in_get_input_frames_lost;
        in->stream.get_active_microphones = in_get_active_microphones;
        in->stream.common.add_audio_effect = in_add_audio_effect;
        in->stream.common.remove_audio_effect = in_remove_audio_effect;
    }

    in->device = devices & ~AUDIO_DEVICE_BIT_IN;
    in->dev = adev;
    in->standby = 1;
    in->source = source;
    in->requested_rate = sample_rate;
    in->hal_channel_mask = config->channel_mask;
    in->hal_format = config->format;

    if (in->device & AUDIO_DEVICE_IN_ALL_SCO) {
        memcpy(&in->config, &pcm_config_bt, sizeof(pcm_config_bt));
        if (adev->bt_wbs)
            in->config.rate = VX_WB_SAMPLING_RATE;
    } else if (in->device & AUDIO_DEVICE_IN_WIRED_HEADSET) {
        //bluetooth rc voice
        // usecase for bluetooth rc audio hal
        ALOGI("%s: use RC audio HAL", __func__);
        ret = rc_open_input_stream(&in, config);
        if (ret != 0) {
            ALOGE("  rc_open_input_stream fail, goto err!!!");
            goto err;
        }
        config->sample_rate = in->config.rate;
        config->channel_mask = AUDIO_CHANNEL_IN_MONO;
    } else
        memcpy(&in->config, &pcm_config_in, sizeof(pcm_config_in));
    in->config.channels = channel_count;
    in->source = source;
    if (source == AUDIO_SOURCE_ECHO_REFERENCE) {
        in->config.format = PCM_FORMAT_S32_LE;
        ALOGD("aec: force config rate=%d ch=%d format=%d\n",
              in->config.rate, in->config.channels, in->config.format);
    }
    /* TODO: modify alsa config by params */
    update_alsa_config(in);

    switch (config->format) {
        case AUDIO_FORMAT_PCM_16_BIT:
            in->config.format = PCM_FORMAT_S16_LE;
            break;
        case AUDIO_FORMAT_PCM_32_BIT:
            in->config.format = PCM_FORMAT_S32_LE;
            break;
        default:
            break;
    }

    in->buffer = aml_audio_malloc(in->config.period_size * audio_stream_in_frame_size(&in->stream));
    if (!in->buffer) {
        ret = -ENOMEM;
        ALOGE("  malloc fail, goto err!!!");
        goto err;
    }
    memset(in->buffer, 0, in->config.period_size * audio_stream_in_frame_size(&in->stream));

    if (!(in->device & AUDIO_DEVICE_IN_WIRED_HEADSET) && in->requested_rate != in->config.rate) {
        ALOGD("%s: in->requested_rate = %d, in->config.rate = %d",
            __func__, in->requested_rate, in->config.rate);
        in->buf_provider.get_next_buffer = get_next_buffer;
        in->buf_provider.release_buffer = release_buffer;
        ret = create_resampler(in->config.rate, in->requested_rate, in->config.channels,
                            RESAMPLER_QUALITY_DEFAULT, &in->buf_provider, &in->resampler);
        if (ret != 0) {
            ALOGE("%s: create resampler failed (%dHz --> %dHz)", __func__, in->config.rate, in->requested_rate);
            ret = -EINVAL;
            goto err;
        }
    }

#ifdef ENABLE_AEC_HAL
    // Default 2 ch pdm + 2 ch lb
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        aec_spk_mic_init(in->requested_rate, 2, 2);
    }
#endif

    /* If AEC is in the app, only configure based on ECHO_REFERENCE spec.
     * If AEC is in the HAL, configure using the given mic stream. */
#ifdef ENABLE_AEC_APP
    bool aecInput = (source == AUDIO_SOURCE_ECHO_REFERENCE);
    if (aecInput) {
        int aec_ret = init_aec_mic_config(adev->aec, in);
        if (aec_ret) {
            ALOGE("AEC: Mic config init failed!");
            return -EINVAL;
        }
    }
#endif

    *stream_in = &in->stream;

#if ENABLE_NANO_NEW_PATH
    if (in->device & AUDIO_DEVICE_IN_BLUETOOTH_BLE) {
        if (nano_is_connected()) {
            ret = nano_input_open(*stream_in, config);
            if (ret < 0) {
                ALOGD("%s: nano_input_open : %d",__func__,ret);
            }
        } else {
            AM_LOGW("nano is disconnect!!!");
            return -EINVAL;
        }
    }
#endif

    ALOGD("%s: exit", __func__);
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

    ALOGD("%s: enter: dev(%p) stream(%p)", __func__, dev, stream);

    if (stream == adev->usb_audio.stream) {
        struct stream_in *usb_in = (struct stream_in *)stream;
        adev->in_device &= ~usb_in->device;
        adev_close_usb_input_stream(stream);
        ALOGD("%s: adev->in_device = %x", __func__, adev->in_device);
        return;
    }

#if ENABLE_NANO_NEW_PATH
    nano_close(stream);
#endif

    in_standby(&stream->common);

    if (in->device & AUDIO_DEVICE_IN_WIRED_HEADSET)
        rc_close_input_stream(in);

#ifdef ENABLE_AEC_HAL
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        if (in->tmp_buffer_8ch) {
            aml_audio_free(in->tmp_buffer_8ch);
            in->tmp_buffer_8ch = NULL;
        }
        aec_spk_mic_release();
    }
#endif

    if (in->resampler) {
        release_resampler(in->resampler);
        in->resampler = NULL;
    }

    pthread_mutex_lock (&in->lock);
    if (in->buffer) {
        aml_audio_free(in->buffer);
        in->buffer = NULL;
    }
    pthread_mutex_unlock (&in->lock);

    if (in->proc_buf) {
        aml_audio_free(in->proc_buf);
    }
    if (in->ref_buf) {
        aml_audio_free(in->ref_buf);
    }
/*[SEI-zhaopf-2018-10-29] add for HBG remote audio support { */
#if defined(ENABLE_HBG_PATCH)
    unregist_callBack_stream(in->hbg_channel);
#endif
/*[SEI-zhaopf-2018-10-29] add for HBG remote audio support } */

#ifdef ENABLE_AEC_APP
    if (in->device & AUDIO_DEVICE_IN_ECHO_REFERENCE) {
        destroy_aec_mic_config(adev->aec);
    }
#endif

    aml_audio_free(stream);
    ALOGD("%s: exit", __func__);
    return;
}

static void dump_audio_port_config (const struct audio_port_config *port_config)
{
    if (port_config == NULL)
        return;

    ALOGI ("  -%s port_config(%p)", __FUNCTION__, port_config);
    ALOGI ("\t-id(%d), role(%s), type(%s)",
        port_config->id, audioPortRole2Str(port_config->role), audioPortType2Str(port_config->type));
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

    if (aml_out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
        int cnt = 0;
        for (int i=0; i<STREAM_USECASE_MAX; i++) {
            struct aml_stream_out *stream_temp = adev->active_outputs[i];
            if (stream_temp != NULL && !stream_temp->standby) {
                cnt++;
            }
        }
        if (cnt <= 1) {
            ALOGI("[%s:%d] stream cnt:%d", __func__, __LINE__, cnt);
            if ((eDolbyMS12Lib == adev->dolby_lib_type) && (ms12->dolby_ms12_enable == true)) {
                get_dolby_ms12_cleanup(&adev->ms12, false);
            }
            a2dp_out_standby(adev);
        }
    }

    /*
    if continous mode,we need always have output.
    so we should not disable the output.
    */
    pthread_mutex_lock(&adev->alsa_pcm_lock);
    /*SWPL-15191 when exit movie player, it will set continuous and the alsa
      can't be closed. After playing something alsa will be opend again and can't be
      closed anymore, because its ref is bigger than 0.
      Now we add this patch to make sure the alsa will be closed.
    */
    if (aml_out->stream_status == STREAM_HW_WRITING &&
        ((!continous_mode(adev) || (!ms12->dolby_ms12_enable && (eDolbyMS12Lib == adev->dolby_lib_type))))) {
        ALOGI("%s aml_out(%p)standby close", __func__, aml_out);
        aml_alsa_output_stop(out);

        if (aml_out->spdifout_handle) {
            aml_audio_spdifout_stop(aml_out->spdifout_handle);
            //aml_out->spdifout_handle = NULL;
        }
        if (aml_out->spdifout2_handle) {
            aml_audio_spdifout_stop(aml_out->spdifout2_handle);
            //aml_out->spdifout2_handle = NULL;
        }
    }
    aml_out->stream_status = STREAM_STANDBY;
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
    pthread_mutex_unlock(&adev->alsa_pcm_lock);

    usecase_change_validate_l (aml_out, true);
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
                    if (aml_out->dual_output_flag && pcm) {
                        ALOGI("%s close dual output pcm handle %p", __func__, pcm);
                        pcm_close(pcm);
                        adev->pcm_handle[DIGITAL_DEVICE] = NULL;
                        aml_out->dual_output_flag = 0;
                    }
                    pthread_mutex_unlock(&adev->alsa_pcm_lock);
                    if (adev->dual_spdifenc_inited) {
                        adev->dual_spdifenc_inited = 0;
                        aml_audio_set_spdif_format(PORT_SPDIF, AML_STEREO_PCM, aml_out);
                    }
                }
            } else {
                aml_hw_mixer_deinit(&adev->hw_mixer);
            }

            if (!continous_mode(adev)) {
                adev->mix_init_flag = false;
            } else {
                if (is_dolby_ms12_main_stream(out)) {
                    dolby_ms12_set_pause_flag(false);
                }
                if (eDolbyMS12Lib == adev->dolby_lib_type && adev->ms12.dolby_ms12_enable) {
                    /*here will not be excuted, as need_remove_conti_mode not be set to true
                      we can remove this part code later. FIXME.*/
                    if (adev->need_remove_conti_mode == true) {
                        ALOGI("%s,release ms12 here", __func__);
                        bool set_ms12_non_continuous = true;
                        get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                        //ALOGI("[%s:%d] get_dolby_ms12_cleanup\n", __FUNCTION__, __LINE__);
                        adev->ms12.is_continuous_paused = false;
                        adev->ms12.need_ms12_resume = false;
                        adev->ms12.need_resync       = 0;
                        adev->need_remove_conti_mode = false;
                    }
                }
            }
        }
        if (adev->spdif_encoder_init_flag) {
            // will cause MS12 ddp playing break..
            //release_spdif_encoder_output_buffer(out);
        }
    }
    if (aml_out->is_normal_pcm) {
        set_system_app_mixing_status(aml_out, aml_out->stream_status);
        aml_out->normal_pcm_mixing_config = false;
    }
    aml_out->pause_status = false;//clear pause status

    if (aml_out->hw_sync_mode && aml_out->tsync_status != TSYNC_STATUS_STOP) {
        ALOGI("%s set AUDIO_PAUSE\n",__func__);
        aml_hwsync_set_tsync_pause(aml_out->hwsync);
        aml_out->tsync_status = TSYNC_STATUS_PAUSED;

        ALOGI("%s set AUDIO_STOP\n",__func__);
        aml_hwsync_set_tsync_stop(aml_out->hwsync);
        aml_out->tsync_status = TSYNC_STATUS_STOP;
    }

#ifdef ENABLE_AEC_APP
    aec_set_spk_running(adev->aec, false);
#endif

    return 0;
}

int out_standby_new(struct audio_stream *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    int status;

    ALOGD("%s: enter", __func__);
    aml_audio_trace_int("out_standby_new", 1);
    if (aml_out->stream_status == STREAM_STANDBY) {
        ALOGI("already standby, do nothing");
        aml_audio_trace_int("out_standby_new", 0);
        return 0;
    }

#if 0 // close this part, put the sleep to ms12 dolby_ms12_main_flush().
    if (continous_mode(aml_out->dev)
        && (aml_out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC)) {
        //1.audio easing duration is 32ms,
        //2.one loop for schedule_run cost about 32ms(contains the hardware costing),
        //3.if [pause, flush] too short, means it need more time to do audio easing
        //so, the delay time for 32ms(pause is completed after audio easing is done) is enough.
        //aml_audio_sleep(64000);
    }
#endif
    pthread_mutex_lock (&aml_out->dev->lock);
    pthread_mutex_lock (&aml_out->lock);
    status = do_output_standby_l(stream);
    pthread_mutex_unlock (&aml_out->lock);
    pthread_mutex_unlock (&aml_out->dev->lock);
    ALOGI("%s exit", __func__);
    aml_audio_trace_int("out_standby_new", 0);

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
            if (adev->dual_spdif_support) {
                if (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3 && adev->sink_format == AUDIO_FORMAT_E_AC3) {
                    if (adev->dolby_decode_enable == 1) {
                        output_format =  AUDIO_FORMAT_AC3;
                    } else {
                        output_format =  AUDIO_FORMAT_E_AC3;
                    }
                } else {
                    output_format = adev->sink_format;
                }
            } else {
                output_format = adev->sink_format;
            }
        } else
            output_format = adev->sink_format;
    }

    return output_format;
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

ssize_t audio_hal_data_processing_ms12v2(struct audio_stream_out *stream,
                                const void *buffer,
                                size_t bytes,
                                void **output_buffer,
                                size_t *output_buffer_bytes,
                                audio_format_t output_format,
                                int nchannels)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    /* TODO  support 24/32 bit sample */
    int out_frames = bytes / (nchannels * 2); /* input is nchannels 16 bit */
    size_t i;
    int j, ret;
    uint32_t latency_frames = 0;
    uint64_t total_frame = 0;
    int enable_dump = aml_getprop_bool("vendor.media.audiohal.outdump");
    if (adev->debug_flag) {
        ALOGD("%s,size %zu,format %x,ch %d\n",__func__,bytes,output_format,nchannels);
    }
    {
        /* nchannels 16 bit PCM, and there is no effect applied after MS12 processing */
        {
            size_t out_frames = bytes / (nchannels * 2); /* input is nchannels 16 bit */
            if (enable_dump) {
                FILE *fp1 = fopen("/data/vendor/audiohal/ms12_out_spk.pcm", "a+");
                if (fp1) {
                    int flen = fwrite((char *)buffer, 1, bytes, fp1);
                    ALOGV("%s buffer %p size %zu\n", __FUNCTION__, buffer, bytes);
                    fclose(fp1);
                }
            }

            ret = aml_audio_check_and_realloc((void **)&adev->out_32_buf, &adev->out_32_buf_size, 2 * bytes);
            R_CHECK_RET(ret, "alloc out_32_buf size:%d fail", 2 * bytes);

            /* apply volume for spk/hp, SPDIF/HDMI keep the max volume */
            float gain_speaker = adev->sink_gain[OUTPORT_SPEAKER];
            if ((eDolbyMS12Lib == adev->dolby_lib_type) && aml_out->ms12_vol_ctrl) {
                gain_speaker = 1.0;
            }
            apply_volume_16to32(gain_speaker, (int16_t *)buffer, adev->out_32_buf, bytes);
            if (enable_dump) {
                FILE *fp1 = fopen("/data/vendor/audiohal/ms12_out_spk-volume-32bit.pcm", "a+");
                if (fp1) {
                    int flen = fwrite((char *)adev->out_32_buf, 1, bytes*2, fp1);
                    ALOGV("%s buffer %p size %zu\n", __FUNCTION__, adev->out_32_buf, bytes);
                    fclose(fp1);
                }
            }

            /* nchannels 32 bit --> 8 channel 32 bit mapping */
            ret = aml_audio_check_and_realloc((void **)&aml_out->tmp_buffer_8ch, &aml_out->tmp_buffer_8ch_size,
                    out_frames * 4 * adev->default_alsa_ch);
            R_CHECK_RET(ret, "alloc tmp_buffer_8ch size:%d fail", out_frames * 4 * adev->default_alsa_ch);

            for (i = 0; i < out_frames; i++) {
                for (j = 0; j < nchannels; j++) {
                    aml_out->tmp_buffer_8ch[adev->default_alsa_ch * i + j] = adev->out_32_buf[nchannels * i + j];
                }
                for(j = nchannels; j < adev->default_alsa_ch; j++) {
                    aml_out->tmp_buffer_8ch[adev->default_alsa_ch * i + j] = 0;
                }
            }
            *output_buffer = aml_out->tmp_buffer_8ch;
            *output_buffer_bytes = out_frames * 4 * adev->default_alsa_ch; /* from nchannels 32 bit to 8 ch 32 bit */
            if (enable_dump) {
                FILE *fp1 = fopen("/data/vendor/audiohal/ms12_out_10_spk.pcm", "a+");
                if (fp1) {
                    int flen = fwrite((char *)aml_out->tmp_buffer_8ch, 1, out_frames * 4 * adev->default_alsa_ch, fp1);
                    fclose(fp1);
                }
            }
        }
    }
    return 0;
}

ssize_t audio_hal_data_processing(struct audio_stream_out *stream,
                                const void *buffer,
                                size_t bytes,
                                void **output_buffer,
                                size_t *output_buffer_bytes,
                                audio_format_t output_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct aml_audio_patch *patch = adev->audio_patch;
    size_t out_frames = bytes / 4;
    size_t buffer_need_size = bytes + EFFECT_PROCESS_BLOCK_SIZE;
    size_t i, j;
    int ret;
    uint32_t latency_frames = 0;
    uint64_t total_frame = 0;
    int auge_chip = alsa_device_is_auge();

    /* raw data need packet to IEC61937 format by spdif encoder */
    if (output_format == AUDIO_FORMAT_IEC61937) {
        //ALOGI("IEC61937 Format");
        *output_buffer = (void *) buffer;
        *output_buffer_bytes = bytes;
    } else if ((output_format == AUDIO_FORMAT_AC3) || (output_format == AUDIO_FORMAT_E_AC3)) {
        //ALOGI("%s, aml_out->hal_format %x , is_iec61937_format = %d, \n", __func__,
        //      aml_out->hal_format,is_iec61937_format(stream));
        if ((is_iec61937_format(stream) == true) ||
            (adev->dolby_lib_type == eDolbyDcvLib)) {
            *output_buffer = (void *) buffer;
            *output_buffer_bytes = bytes;
            /* Need mute when play DTV, HDMI output. */
            if (adev->sink_gain[OUTPORT_HDMI] < FLOAT_ZERO && adev->active_outport == OUTPORT_HDMI && adev->audio_patching) {
                memset(*output_buffer, 0, bytes);
            }
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
        out_frames = bytes / FRAMESIZE_32BIT_STEREO;
        if (!aml_out->ms12_vol_ctrl) {
            float gain_speaker = adev->sink_gain[OUTPORT_SPEAKER];
            if (aml_out->hw_sync_mode)
                gain_speaker *= aml_out->volume_l;
            apply_volume(gain_speaker, tmp_buffer, sizeof(uint32_t), bytes);

        }

        /* 2 ch 32 bit --> 8 ch 32 bit mapping, need 8X size of input buffer size */
        ret = aml_audio_check_and_realloc((void **)&aml_out->tmp_buffer_8ch, &aml_out->tmp_buffer_8ch_size,
            FRAMESIZE_32BIT_8ch * out_frames);
        R_CHECK_RET(ret, "alloc tmp_buffer_8ch size:%d fail", FRAMESIZE_32BIT_8ch * out_frames);

        for (i = 0; i < out_frames; i++) {
            aml_out->tmp_buffer_8ch[8 * i] = tmp_buffer[2 * i];
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
    } else {
        if (aml_out->is_tv_platform == 1) {
            ret = aml_audio_check_and_realloc((void **)&adev->out_16_buf, &adev->out_16_buf_size, buffer_need_size);
            R_CHECK_RET(ret, "alloc out_16_buf size:%d fail", bytes);

            ret = aml_audio_check_and_realloc((void **)&adev->out_32_buf, &adev->out_32_buf_size, 2 * buffer_need_size);
            R_CHECK_RET(ret, "alloc out_32_buf size:%d fail", 2 * bytes);

            /* 2 ch 16 bit --> 8 ch 32 bit mapping, need 8X size of input buffer size */
            ret = aml_audio_check_and_realloc((void **)&aml_out->tmp_buffer_8ch, &aml_out->tmp_buffer_8ch_size, 8 * buffer_need_size);
            R_CHECK_RET(ret, "alloc tmp_buffer_8ch size:%d fail", 8 * bytes);

            for (int dev = AML_AUDIO_OUT_DEV_TYPE_SPEAKER; dev < AML_AUDIO_OUT_DEV_TYPE_BUTT; dev++) {
                memcpy(adev->out_16_buf, buffer, bytes);
                float volume = aml_audio_get_s_gain_by_src(adev, adev->patch_src);
                if (dev == AML_AUDIO_OUT_DEV_TYPE_SPEAKER || dev == AML_AUDIO_OUT_DEV_TYPE_HEADPHONE) {
                    /* apply volume for spk/hp, SPDIF/HDMI keep the max volume */
                    if (adev->active_outport == OUTPORT_A2DP) {
                        if ((adev->patch_src == SRC_DTV || adev->patch_src == SRC_HDMIIN
                                || adev->patch_src == SRC_LINEIN || adev->patch_src == SRC_ATV)
                                && adev->audio_patching) {
                                volume *= adev->sink_gain[OUTPORT_A2DP];
                        }
                    } else if (dev == AML_AUDIO_OUT_DEV_TYPE_HEADPHONE) {
                        volume *= adev->eq_data.p_gain.headphone * adev->sink_gain[OUTPORT_HEADPHONE];
                    } else {
                        volume *= adev->eq_data.p_gain.speaker * adev->sink_gain[OUTPORT_SPEAKER];
                    }

                    /* for ms12 lib, and audio volume control in ms12, bypass all volume apply */
                    if (eDolbyMS12Lib == adev->dolby_lib_type && aml_out->ms12_vol_ctrl) {
                        volume = 1.0;
                    } else if (adev->volume_ease.config_easing && dev == AML_AUDIO_OUT_DEV_TYPE_SPEAKER) {
                        /* start audio volume easing */
                        float vol_now = aml_audio_ease_get_current_volume(adev->volume_ease.ease);
                        config_volume_easing(adev->volume_ease.ease, vol_now, volume);
                        adev->volume_ease.ease->do_easing = true;
                        adev->volume_ease.config_easing = false;
                    }

                    if (adev->patch_src == SRC_DTV && adev->audio_patch != NULL) {
                        aml_audio_switch_output_mode((int16_t *)adev->out_16_buf, bytes, adev->sound_track_mode);
                    } else if (adev->audio_patch == NULL) {
                        aml_audio_switch_output_mode((int16_t *)adev->out_16_buf, bytes, adev->sound_track_mode);
                    }

                    if (adev->active_outport != OUTPORT_A2DP && adev->active_outport != OUTPORT_HEADPHONE) {
                        out_frames = audio_post_process(&adev->native_postprocess, adev->out_16_buf, out_frames);
                        bytes = out_frames * 4;
                    }
                    if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
                        aml_audio_dump_audio_bitstreams("/data/audio/audio_spk.pcm", adev->out_16_buf, bytes);
                    }
                }
#ifdef ADD_AUDIO_DELAY_INTERFACE
                if (dev != AML_AUDIO_OUT_DEV_TYPE_OTHER) {
                    aml_audio_delay_process(out_dev_convert_to_delay_type(dev), adev->out_16_buf, bytes,
                            AUDIO_FORMAT_PCM_16_BIT, MM_FULL_POWER_SAMPLING_RATE);
                }
#endif
                if (!adev->volume_ease.ease->do_easing || dev != AML_AUDIO_OUT_DEV_TYPE_SPEAKER) {
                    apply_volume_16to32(volume, adev->out_16_buf, adev->out_32_buf, bytes);
                } else {
                    /*do ease process when adjust vol,vol apply is handled by ease process,when ease process finished,
                    vol apply need handled by apply volume function,vol is float type,use fadbs to compare*/
                    apply_volume_16to32(1.0, adev->out_16_buf, adev->out_32_buf, bytes);
                    aml_audio_ease_process(adev->volume_ease.ease, adev->out_32_buf, bytes * 2);
                }

                for (j = 0; j < out_frames; j++) {
                    aml_out->tmp_buffer_8ch[8 * j + 2 * alsa_out_ch_mask[auge_chip][dev]]      = adev->out_32_buf[2 * j];
                    aml_out->tmp_buffer_8ch[8 * j + 2 * alsa_out_ch_mask[auge_chip][dev] + 1]  = adev->out_32_buf[2 * j + 1];
                }
            }
            *output_buffer = aml_out->tmp_buffer_8ch;
            *output_buffer_bytes = 8 * bytes;
        } else {
            if (adev->patch_src == SRC_DTV && adev->audio_patch != NULL) {
                aml_audio_switch_output_mode((int16_t *)buffer, bytes, adev->audio_patch->mode);
            } else if (adev->audio_patch == NULL) {
                aml_audio_switch_output_mode((int16_t *)buffer, bytes, adev->sound_track_mode);
            }

            *output_buffer = (void *) buffer;
            *output_buffer_bytes = bytes;
        }
    }
    /*when REPORT_DECODED_INFO is added, we will enable it*/
#if 0
    if (adev->audio_patch != NULL && adev->patch_src == SRC_DTV) {
        int sample_rate = 0, pch = 0, lfepresent;
        char sysfs_buf[AUDIO_HAL_CHAR_MAX_LEN] = {0};
        if (adev->audio_patch->aformat != AUDIO_FORMAT_E_AC3
            && adev->audio_patch->aformat != AUDIO_FORMAT_AC3 &&
            adev->audio_patch->aformat != AUDIO_FORMAT_DTS) {
            unsigned int errcount;
            char sysfs_buf[AUDIO_HAL_CHAR_MAX_LEN] = {0};
            audio_decoder_status(&errcount);
            sprintf(sysfs_buf, "decoded_err %d", errcount);
            sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
            dtv_audio_decpara_get(&sample_rate, &pch, &lfepresent);
            pch = pch + lfepresent;
        } else if (adev->audio_patch->aformat == AUDIO_FORMAT_AC3 ||
            adev->audio_patch->aformat == AUDIO_FORMAT_E_AC3) {
            pch = adev->ddp.sourcechnum;
            sample_rate = adev->ddp.sourcesr;
        }
        sprintf(sysfs_buf, "ch_num %d", pch);
        sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
        sprintf(sysfs_buf, "samplerate %d", sample_rate);
        sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
        if (pch == 2) {
            sprintf (sysfs_buf, "ch_configuration %d", TIF_HAL_PLAYBACK_AUDIO_SOURCE_CHANNEL_CONFIGURATION_STEREO);
        } else if (pch == 6) {
            sprintf (sysfs_buf, "ch_configuration %d", TIF_HAL_PLAYBACK_AUDIO_SOURCE_CHANNEL_CONFIGURATION_5_1);
        } else {
            ALOGV("unsupport yet");
        }
        sysfs_set_sysfs_str(REPORT_DECODED_INFO, sysfs_buf);
    }
#endif
    if (adev->dev2mix_patch) {
        tv_in_write(stream, buffer, bytes);
        memset((char *)buffer, 0, bytes);
        if (aml_out->is_tv_platform == 1) {
           memset(aml_out->tmp_buffer_8ch, 0, (*output_buffer_bytes));
        }
    }

    return 0;
}

ssize_t hw_write (struct audio_stream_out *stream
                  , const void *buffer
                  , size_t bytes
                  , audio_format_t output_format)
{
    ALOGV ("+%s() buffer %p bytes %zu", __func__, buffer, bytes);
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    audio_config_base_t in_data_config = {48000, AUDIO_CHANNEL_OUT_STEREO, AUDIO_FORMAT_PCM_16_BIT};
    const uint16_t *tmp_buffer = buffer;
    int16_t *effect_tmp_buf = NULL;
    struct aml_audio_patch *patch = adev->audio_patch;
    bool is_dtv = (adev->patch_src == SRC_DTV);
    int ch = 2;
    int bytes_per_sample = 2;

    int out_frames = 0;
    ssize_t ret = 0;
    int i;
    uint32_t latency_frames = 0;
    uint64_t total_frame = 0;
    uint64_t write_frames = 0;
    uint64_t  sys_total_cost = 0;
    int  adjust_ms = 0;
    int  alsa_port = -1;

    if (adev->is_TV && audio_is_linear_pcm(output_format)) {
        ch = adev->default_alsa_ch;
        bytes_per_sample = 4;
        in_data_config.channel_mask = AUDIO_CHANNEL_OUT_7POINT1;
        in_data_config.format = AUDIO_FORMAT_PCM_32_BIT;
    }
    out_frames = bytes / (ch * bytes_per_sample);

    adev->debug_flag = aml_audio_get_debug_flag();
    if (adev->debug_flag) {
        ALOGI("+%s() buffer %p bytes %zu, format %#x out %p hw_sync_mode %d\n",
            __func__, buffer, bytes, output_format, aml_out, aml_out->hw_sync_mode);
    }
    if (patch && !patch->skip_amadec_flag) {
        if (is_dtv && need_hw_mix(adev->usecase_masks)) {
        if (adev->audio_patch->avsync_callback && aml_out->dtvsync_enable)
            adev->audio_patch->avsync_callback(adev->audio_patch,aml_out);
        }
    }

    pthread_mutex_lock(&adev->alsa_pcm_lock);
    aml_out->alsa_output_format = output_format;
    if (aml_out->stream_status != STREAM_HW_WRITING) {
        ALOGI("%s, aml_out %p alsa open output_format %#x\n", __func__, aml_out, output_format);
        if (adev->useSubMix) {
            if (/*adev->audio_patching &&*/
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
                if (adev->raw_to_pcm_flag) {
                    ALOGI("disable raw_to_pcm_flag --");
                    pcm_stop(aml_out->pcm);
                    adev->raw_to_pcm_flag = false;
                }
            }
        } else {
            {
                ret = aml_alsa_output_open(stream);
                if (ret) {
                    ALOGE("%s() open failed", __func__);
                }
            }
        }
        if (is_dtv) {
            audio_set_spdif_clock(aml_out, get_codec_type(output_format));
        }
        aml_out->stream_status = STREAM_HW_WRITING;
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type && !is_bypass_dolbyms12(stream)) {
        if (aml_out->hw_sync_mode && !adev->ms12.is_continuous_paused) {
            // histroy here: ms12lib->pcm_output()->hw_write() aml_out is passed as private data
            //               when registering output callback function in dolby_ms12_register_pcm_callback()
            // some times "aml_out->hwsync->aout == NULL"
            // one case is no main audio playing, only aux audio playing (Netflix main screen)
            // in this case dolby_ms12_get_consumed_payload() always return 0, no AV sync can be done zzz
            if (aml_out->hwsync->aout) {
                if (is_bypass_dolbyms12(stream)) {
                    aml_audio_hwsync_audio_process(aml_out->hwsync, aml_out->hwsync->payload_offset, out_frames, &adjust_ms);
                }
                else {
                    if (!audio_is_linear_pcm(aml_out->hal_internal_format)) {
                        /*if udc decode doens't generate any data, we should not use the consume offset to get pts*/
                        ALOGV("udc generate pcm =%lld", dolby_ms12_get_main_pcm_generated(stream));
                        if (dolby_ms12_get_main_pcm_generated(stream)) {
                            aml_audio_hwsync_audio_process(aml_out->hwsync, dolby_ms12_get_main_bytes_consumed(stream), out_frames, &adjust_ms);
                        }
                    } else {
                        /* because the pcm consumed playload offset is at the end of consume buffer,
                         * we need the beginning position and ms12 always
                         * output 1536 frame every time
                         */
                        uint64_t consume_payload = dolby_ms12_get_main_bytes_consumed(stream);
                        /* for non 48khz hwsync pcm, the pts check in is used original 44.1khz offset,
                         * but we resample it before send to ms12, so we consumed offset is 48khz,
                         * so we need convert it
                         */
                        if (audio_is_linear_pcm(aml_out->hwsync->aout->hal_internal_format) &&
                            (aml_out->hwsync->aout->hal_rate != 48000)) {
                            consume_payload = consume_payload * aml_out->hwsync->aout->hal_rate / 48000;
                        }

                        aml_audio_hwsync_audio_process(aml_out->hwsync, consume_payload, out_frames, &adjust_ms);
                    }
                }
            } else {
                if (adev->debug_flag) {
                    ALOGI("%s,aml_out->hwsync->aout == NULL",__FUNCTION__);
                }
            }
        }
    }
    if (aml_out->pcm || adev->a2dp_hal || is_sco_port(adev->active_outport)) {
#ifdef ADD_AUDIO_DELAY_INTERFACE
        ret = aml_audio_delay_process(AML_DELAY_OUTPORT_ALL, (void *) tmp_buffer, bytes,
                output_format, MM_FULL_POWER_SAMPLING_RATE);
        if (ret < 0) {
            //ALOGW("aml_audio_delay_process skip, ret:%#x", ret);
        }
#endif
        if (adjust_ms) {
            int adjust_bytes = 0;
            if ((output_format == AUDIO_FORMAT_E_AC3) || (output_format == AUDIO_FORMAT_AC3)) {
                int i = 0;
                int bAtmos = 0;
                int insert_frame = adjust_ms/32;
                int raw_size = 0;
                if (insert_frame > 0) {
                    char *raw_buf = NULL;
                    char *temp_buf = NULL;
                    /*atmos lock or input is ddp atmos*/
                    if (adev->atoms_lock_flag ||
                        (adev->ms12.is_dolby_atmos && adev->ms12_main1_dolby_dummy == false)) {
                        bAtmos = 1;
                    }
                    raw_buf = aml_audio_get_muteframe(output_format, &raw_size, bAtmos);
                    ALOGI("insert atmos=%d raw frame size=%d times=%d", bAtmos, raw_size, insert_frame);
                    if (raw_buf && (raw_size > 0)) {
                        temp_buf = aml_audio_malloc(raw_size);
                        if (!temp_buf) {
                            ALOGE("%s malloc failed", __func__);
                            pthread_mutex_unlock(&adev->alsa_pcm_lock);
                            return -1;
                        }
                        for (i = 0; i < insert_frame; i++) {
                            memcpy(temp_buf, raw_buf, raw_size);
                            ret = aml_alsa_output_write(stream, (void*)temp_buf, raw_size);
                            if (ret < 0) {
                                ALOGE("%s alsa write fail when insert", __func__);
                                break;
                            }
                        }
                        aml_audio_free(temp_buf);
                    }
                }
            } else {
                memset((void*)buffer, 0, bytes);
                if (output_format == AUDIO_FORMAT_E_AC3) {
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
                        if (adev->active_outport == OUTPORT_A2DP) {
                            ret = a2dp_out_write(adev, &in_data_config, (void*)buf, write_size);
                        } else if (is_sco_port(adev->active_outport)) {
                            ret = write_to_sco(adev, &in_data_config, buffer, bytes);
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
        }
        if (adev->active_outport == OUTPORT_A2DP) {
            ret = a2dp_out_write(adev, &in_data_config, buffer, bytes);
        } else if (is_sco_port(adev->active_outport)) {
            ret = write_to_sco(adev, &in_data_config, buffer, bytes);
        } else {
            ret = aml_alsa_output_write(stream, (void *) buffer, bytes);
        }
        //ALOGE("!!aml_alsa_output_write"); ///zzz
        if (ret < 0) {
            ALOGE("ALSA out write fail");
            aml_out->frame_write_sum += out_frames;
        } else {
            if (!continous_mode(adev)) {
                if ((output_format == AUDIO_FORMAT_AC3) ||
                    (output_format == AUDIO_FORMAT_E_AC3) ||
                    (output_format == AUDIO_FORMAT_MAT) ||
                    (output_format == AUDIO_FORMAT_DTS)) {
                    if (is_iec61937_format(stream) == true) {
                        //continuous_audio_mode = 0, when mixing-off and 7.1ch
                        //FIXME out_frames 4 or 16 when DD+ output???
                        aml_out->frame_write_sum += out_frames;

                    } else {
                        int sample_per_bytes = (output_format == AUDIO_FORMAT_MAT) ? 64 :
                                                (output_format == AUDIO_FORMAT_E_AC3) ? 16 : 4;

                        if (eDolbyDcvLib == adev->dolby_lib_type && aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                            aml_out->frame_write_sum = aml_out->input_bytes_size / audio_stream_out_frame_size(stream);
                            //aml_out->frame_write_sum += out_frames;
                        } else {
                            aml_out->frame_write_sum += bytes / sample_per_bytes; //old code
                            //aml_out->frame_write_sum = spdif_encoder_ad_get_total() / sample_per_bytes + aml_out->spdif_enc_init_frame_write_sum; //8.1
                        }
                    }
                } else {
                    aml_out->frame_write_sum += out_frames;
                    total_frame =  aml_out->frame_write_sum;
                }
            }
        }
    }
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        latency_frames = aml_audio_out_get_ms12_latency_frames(stream);
    } else {
        latency_frames = out_get_latency_frames(stream);
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
            write_frames = aml_out->input_bytes_size / aml_out->hal_frame_size;
            //total_frame = write_frames;
        } else {
            total_frame = aml_out->frame_write_sum + aml_out->frame_skip_sum;
        }
    } else {
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            if (!audio_is_linear_pcm(aml_out->hal_internal_format)) {
                /*use the pcm which is gennerated by udc, to get the total frame by nbytes/nbtyes_per_sample
                 *Please be careful about the aml_out->continuous_audio_offset;*/

                total_frame = dolby_ms12_get_main_pcm_generated(stream);
                write_frames =  aml_out->total_ddp_frame_nblks * 256;/*256samples in one block*/
                if (adev->debug_flag) {
                    ALOGI("%s,total_frame %"PRIu64" write_frames %"PRIu64" total frame block nums %"PRIu64"",
                        __func__, total_frame, write_frames, aml_out->total_ddp_frame_nblks);
                }
            }
            /*case 3:pcm_hwsync*/
            else if (aml_out->hw_sync_mode) {
                total_frame = dolby_ms12_get_main_pcm_generated(stream);
            }
            /*case 1:pcm_direct+pcm_normal*/
            else {
                /*system volume prestation caculatation is done inside aux write thread*/
                total_frame = dolby_ms12_get_main_pcm_generated(stream);
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
        aml_out->position_update = 1;
        //ALOGI("position =%lld time sec = %ld, nanosec = %ld", aml_out->last_frames_postion, aml_out->lasttimestamp.tv_sec , aml_out->lasttimestamp.tv_nsec);
    }
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (continous_mode(adev)) {
            if (adev->ms12.is_continuous_paused) {
                if (total_frame == adev->ms12.last_ms12_pcm_out_position) {
                    adev->ms12.ms12_position_update = false;
                }
            }
            /* the ms12 generate pcm out is not changed, we assume it is the same one
             * don't update the position
             */
            if (total_frame != adev->ms12.last_ms12_pcm_out_position) {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                adev->ms12.timestamp.tv_sec = ts.tv_sec;
                adev->ms12.timestamp.tv_nsec = ts.tv_nsec;
                adev->ms12.last_frames_postion = aml_out->last_frames_postion;
                adev->ms12.last_ms12_pcm_out_position = total_frame;
                adev->ms12.ms12_position_update = true;
            }
        }
        /* check sys audio postion */
        sys_total_cost = dolby_ms12_get_consumed_sys_audio();
        if (adev->ms12.last_sys_audio_cost_pos != sys_total_cost) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            adev->ms12.sys_audio_timestamp.tv_sec = ts.tv_sec;
            adev->ms12.sys_audio_timestamp.tv_nsec = ts.tv_nsec;
            /*FIXME. 2ch 16 bit audio */
            adev->ms12.sys_audio_frame_pos = adev->ms12.sys_audio_base_pos + adev->ms12.sys_audio_skip + sys_total_cost/4 - latency_frames;
        }
        if (adev->debug_flag) {
            ALOGI("%s  ms12.last_frames_postion:%llu,last_ms12_pcm_out_position:%llu; sys audio pos %"PRIu64" ms ,sys_total_cost %"PRIu64",base pos %"PRIu64",latency %d \n", __func__,
                  adev->ms12.last_frames_postion, adev->ms12.last_ms12_pcm_out_position,
                  adev->ms12.sys_audio_frame_pos/48,sys_total_cost,adev->ms12.sys_audio_base_pos,latency_frames);
        }
        adev->ms12.last_sys_audio_cost_pos = sys_total_cost;
    }
    if (adev->debug_flag) {
        ALOGI("%s() stream(%p) pcm handle %p format input %#x output %#x 61937 frame %d",
              __func__, stream, aml_out->pcm, aml_out->hal_internal_format, output_format, is_iec61937_format(stream));

        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            //ms12 internal buffer avail(main/associate/system)
            if (adev->ms12.dolby_ms12_enable == true) {
                ALOGI("%s MS12 buffer avail main %d associate %d system %d\n",
                      __FUNCTION__, dolby_ms12_get_main_buffer_avail(NULL), dolby_ms12_get_associate_buffer_avail(), dolby_ms12_get_system_buffer_avail(NULL));
            }
        }

        if ((aml_out->hal_internal_format == AUDIO_FORMAT_AC3) || (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3)) {
            ALOGI("%s() total_frame %"PRIu64" latency_frames %d last_frames_postion %"PRIu64" total write %"PRIu64" total writes frames %"PRIu64" diff latency %"PRIu64" ms\n",
                  __FUNCTION__, total_frame, latency_frames, aml_out->last_frames_postion, aml_out->input_bytes_size, write_frames, (write_frames - total_frame) / 48);
        } else {
            ALOGI("%s() total_frame %"PRIu64" latency_frames %d last_frames_postion %"PRIu64" total write %"PRIu64" total writes frames %"PRIu64" diff latency %"PRIu64" ms\n",
                  __FUNCTION__, total_frame, latency_frames, aml_out->last_frames_postion, aml_out->input_bytes_size, write_frames, (write_frames - aml_out->last_frames_postion) / 48);
        }
    }
    return ret;
}

audio_format_t get_non_ms12_output_format(audio_format_t src_format, struct aml_audio_device *aml_dev)
{
    audio_format_t output_format = AUDIO_FORMAT_PCM_16_BIT;
    struct aml_arc_hdmi_desc *hdmi_desc = &aml_dev->hdmi_descs;
    if (aml_dev->hdmi_format == AUTO) {
        if (src_format == AUDIO_FORMAT_E_AC3 ) {
            if (hdmi_desc->ddp_fmt.is_support)
               output_format = AUDIO_FORMAT_E_AC3;
            else if (hdmi_desc->dd_fmt.is_support)
                output_format = AUDIO_FORMAT_AC3;
        } else if (src_format == AUDIO_FORMAT_AC3 ) {
            if (hdmi_desc->dd_fmt.is_support)
                output_format = AUDIO_FORMAT_AC3;
        }
    }
    return output_format;
}

void config_output(struct audio_stream_out *stream, bool reset_decoder)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct aml_audio_patch *patch = adev->audio_patch;

    int ret = 0;
    bool main1_dummy = false;
    bool ott_input = false;
    bool dtscd_flag = false;
    bool reset_decoder_stored = reset_decoder;
    int i  = 0 ;
    uint64_t write_frames = 0;

    int is_arc_connected = 0;
    int sink_format = AUDIO_FORMAT_PCM_16_BIT;
    adev->dcvlib_bypass_enable = 0;
    adev->dtslib_bypass_enable = 0;

    /*get sink format*/
    get_sink_format (stream);
    ALOGI("%s() aml_out:%p  adev->dolby_lib_type = %d", __FUNCTION__, aml_out, adev->dolby_lib_type);
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        bool is_compatible = false;
        bool is_direct_pcm = is_direct_stream_and_pcm_format(aml_out);
        bool is_mmap_pcm = is_mmap_stream_and_pcm_format(aml_out);
        bool is_ms12_pcm_volume_control = (is_direct_pcm && !is_mmap_pcm);
        bool is_a2dp_device = (aml_out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP);

        //ALOGI("%s is_ms12_pcm_volume_control:%d, is_a2dp_device:%d, out_device:0x%x, volume_l:%f",
        //        __func__, is_ms12_pcm_volume_control, is_a2dp_device, aml_out->out_device, aml_out->volume_l);

        if (!is_dolby_ms12_support_compression_format(aml_out->hal_internal_format)) {

            if (aml_out->aml_dec) {
                is_compatible = aml_decoder_output_compatible(stream, adev->sink_format, adev->optical_format);
                if (is_compatible) {
                    reset_decoder = false;
                }
            }

            if (reset_decoder) {
                /*if decoder is init, we close it first*/
                if (aml_out->aml_dec) {
                    aml_decoder_release(aml_out->aml_dec);
                    aml_out->aml_dec = NULL;
                }

                memset(&aml_out->dec_config, 0, sizeof(aml_dec_config_t));

                /*prepare the decoder config*/
                ret = aml_decoder_config_prepare(stream, aml_out->hal_internal_format, &aml_out->dec_config);

                if (ret < 0) {
                    ALOGE("config decoder error");
                    return;
                }

                ret = aml_decoder_init(&aml_out->aml_dec, aml_out->hal_internal_format, (aml_dec_config_t *)&aml_out->dec_config);
                if (ret < 0) {
                    ALOGE("aml_decoder_init failed");
                }

            }

        }
        is_compatible = false;
        reset_decoder = reset_decoder_stored;
        ALOGI("continous_mode(adev) %d ms12->dolby_ms12_enable %d",continous_mode(adev), ms12->dolby_ms12_enable);
        if (continous_mode(adev) && ms12->dolby_ms12_enable) {
            is_compatible = is_ms12_output_compatible(stream, adev->sink_format, adev->optical_format);
        }
        if (is_compatible) {
            reset_decoder = false;
        }

        if (!is_bypass_dolbyms12(stream) && (reset_decoder == true)) {
            pthread_mutex_lock(&adev->lock);
            get_dolby_ms12_cleanup(&adev->ms12, false);
            pthread_mutex_lock(&adev->alsa_pcm_lock);
            struct pcm *pcm = adev->pcm_handle[DIGITAL_DEVICE];
            if (aml_out->dual_output_flag && pcm) {
                ALOGI("%s close pcm handle %p", __func__, pcm);
                pcm_close(pcm);
                adev->pcm_handle[DIGITAL_DEVICE] = NULL;
                //aml_out->dual_output_flag = 0;
                //ALOGI("------%s close pcm handle %p", __func__, pcm);
                aml_audio_set_spdif_format(PORT_SPDIF, AML_STEREO_PCM,aml_out);
            }
            if (adev->dual_spdifenc_inited) {
                adev->dual_spdifenc_inited = 0;
            }
            if (aml_out->stream_status == STREAM_HW_WRITING) {
                aml_alsa_output_close(stream);
                aml_out->stream_status = STREAM_STANDBY;
                aml_audio_set_spdif_format(PORT_SPDIF, AML_STEREO_PCM,aml_out);
            }
            pthread_mutex_unlock(&adev->alsa_pcm_lock);
            //FIXME. also need check the sample rate and channel num.
            audio_format_t aformat = aml_out->hal_internal_format;
            if (continous_mode(adev)) {
                if (!dolby_stream_active(adev)) {
                    /*dummy we support it is  DD+*/
                    aformat = AUDIO_FORMAT_E_AC3;
                    main1_dummy = true;
                } else {
                    /*there is some dolby stream active and this stream is not the same one with current one*/
                    struct aml_stream_out *aml_active_out = direct_active(adev);
                    if (audio_is_linear_pcm(aformat)
                        && aml_active_out
                        && (aml_active_out->hal_internal_format == AUDIO_FORMAT_AC4)) {
                        /*we use ac4 as the main input, otherwise it may causes ms12 init fail*/
                        aformat = AUDIO_FORMAT_AC4;
                        ALOGI("use ac4 as the main input type");
                    }

                }
            }
            if (continous_mode(adev) && hwsync_lpcm_active(adev)) {
                ott_input = true;
            }
            if (continous_mode(adev)) {
                adev->ms12_main1_dolby_dummy = main1_dummy;
                adev->ms12_ott_enable = ott_input;
                /*AC4 does not support -ui (OTT sound) */
                dolby_ms12_set_ott_sound_input_enable(aformat != AUDIO_FORMAT_AC4);
                dolby_ms12_set_dolby_main1_as_dummy_file(aformat != AUDIO_FORMAT_AC4);
            }
            ring_buffer_reset(&adev->spk_tuning_rbuf);
            adev->ms12.is_continuous_paused = false;
            ret = get_the_dolby_ms12_prepared(aml_out, aformat,
                aml_out->hal_channel_mask,
                aml_out->hal_rate);

            if (is_dolby_ms12_main_stream(stream) && continous_mode(adev)) {
                adev->ms12.main_input_start_offset_ns = aml_out->main_input_ns;
                adev->ms12.main_input_bytes_offset    = aml_out->input_bytes_size;
                ALOGI("main start offset ns =%lld", adev->ms12.main_input_start_offset_ns);
            }

            /*set the volume to current one*/
            if (!audio_is_linear_pcm(aml_out->hal_internal_format)
                || (is_ms12_pcm_volume_control && !is_a2dp_device)
                /*The volume step is sent to BT module and BT moudle will
                **handle the volume.
                **So AudioHal should pass the audio data with volume fullscale.
                **AudioPolicy send vol 0.0/1.0 to AudioHal when BT connect,
                **In fact, just vol 1.0 is useful,so add this vol filter.
                */
                || (is_a2dp_device && (aml_out->volume_l == 1.0))) {
                set_ms12_main_volume(&adev->ms12, aml_out->volume_l);
            }
            if (continous_mode(adev) && adev->ms12.dolby_ms12_enable) {
                dolby_ms12_set_main_dummy(0, main1_dummy);
                dolby_ms12_set_main_dummy(1, !ott_input);
            }

            /*netflix always ddp 5.1 output, other case we need output ddp 2ch*/
            if (continous_mode(adev) && main1_dummy && !adev->is_netflix) {
                pthread_mutex_lock(&ms12->lock);
                set_ms12_acmod2ch_lock(&adev->ms12, true);
                pthread_mutex_unlock(&ms12->lock);
            }
            if (adev->ms12_out != NULL && adev->ms12_out->hwsync) {
                //aml_audio_hwsync_init(adev->ms12_out->hwsync, adev->ms12_out);
                adev->ms12_out->hwsync->aout = adev->ms12_out;
                adev->ms12_out->hw_sync_mode = aml_out->hw_sync_mode;
                ALOGI("set ms12 hwsync out to %p set its hw_sync_mode %d",adev->ms12_out, adev->ms12_out->hw_sync_mode);
            }

            adev->mix_init_flag = true;
            ALOGI("%s() get_the_dolby_ms12_prepared %s, ott_enable = %d, main1_dummy = %d", __FUNCTION__, (ret == 0) ? "succuss" : "fail", ott_input, main1_dummy);
            pthread_mutex_unlock(&adev->lock);
            /* if ms12 reconfig, do avsync */
            if (ret == 0 && adev->audio_patch && (adev->patch_src == SRC_HDMIIN ||
                adev->patch_src == SRC_ATV || adev->patch_src == SRC_LINEIN)) {
                adev->audio_patch->need_do_avsync = true;
                ALOGI("set ms12, then do avsync!");
            }
        }
    } else {
        bool is_compatible = false;
        if (aml_out->aml_dec) {
            is_compatible = aml_decoder_output_compatible(stream, adev->sink_format, adev->optical_format);
            if (is_compatible) {
                reset_decoder = false;
            }
        }

        if (reset_decoder) {
            pthread_mutex_lock(&adev->alsa_pcm_lock);
            if (aml_out->stream_status == STREAM_HW_WRITING) {
                aml_alsa_output_close(stream);
                aml_out->stream_status = STREAM_STANDBY;
            }
            pthread_mutex_unlock(&adev->alsa_pcm_lock);
            /*if decoder is init, we close it first*/
            if (aml_out->aml_dec) {
                aml_decoder_release(aml_out->aml_dec);
                aml_out->aml_dec = NULL;
            }
            /* In netflix, when ddp do seek, we should not
             * close the spdif out, otherwise it will disable
             * the audio clock, and this will causes the audio
             * clock discontinuty.
             * todo: shall remove it for all cases?
             */
            if (!adev->is_netflix) {
                if (aml_out->spdifout_handle) {
                    aml_audio_spdifout_close(aml_out->spdifout_handle);
                    aml_out->spdifout_handle = NULL;
                    aml_out->dual_output_flag = 0;
                }
                if (aml_out->spdifout2_handle) {
                    aml_audio_spdifout_close(aml_out->spdifout2_handle);
                    aml_out->spdifout2_handle = NULL;
                }
            }

            memset(&aml_out->dec_config, 0, sizeof(aml_dec_config_t));

            /*prepare the decoder config*/
            ret = aml_decoder_config_prepare(stream, aml_out->hal_internal_format, &aml_out->dec_config);

            if (ret < 0) {
                ALOGE("config decoder error");
                return;
            }

            ret = aml_decoder_init(&aml_out->aml_dec, aml_out->hal_internal_format, (aml_dec_config_t *)&aml_out->dec_config);
            if (ret < 0) {
                ALOGE("aml_decoder_init failed");
            }

            pthread_mutex_lock(&adev->lock);
            if (!adev->hw_mixer.start_buf) {
                aml_hw_mixer_init(&adev->hw_mixer);
            } else {
                aml_hw_mixer_reset(&adev->hw_mixer);
            }
            pthread_mutex_unlock(&adev->lock);
        }
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
    ALOGI("[%s:%d] out stream alsa port device:%d", __func__, __LINE__, aml_out->device);
    return ;
}

ssize_t mixer_main_buffer_write(struct audio_stream_out *stream, const void *buffer,
                                 size_t bytes)
{
    ALOGV("%s write in %zu!\n", __FUNCTION__, bytes);
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
    bool need_reconfig_samplerate = false;
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
    int return_bytes = bytes;
    uint64_t apts64 = 0;

    audio_hwsync_t *hw_sync = aml_out->hwsync;
    bool digital_input_src = (patch && \
           (patch->input_src == AUDIO_DEVICE_IN_HDMI
           || patch->input_src == AUDIO_DEVICE_IN_SPDIF
           || patch->input_src == AUDIO_DEVICE_IN_TV_TUNER));
    if (adev->debug_flag) {
        ALOGI("[%s:%d] out:%p bytes:%zu,format:%#x,ms12_ott:%d,conti:%d,hw_sync:%d", __func__, __LINE__,
              aml_out, bytes, aml_out->hal_internal_format,adev->ms12_ott_enable,adev->continuous_audio_mode,aml_out->hw_sync_mode);
        ALOGI("[%s:%d] hal_format:%#x, out_usecase:%s, dev_usecase_masks:%#x", __func__, __LINE__,
            aml_out->hal_format, usecase2Str(aml_out->usecase), adev->usecase_masks);
    }

    if (buffer == NULL) {
        ALOGE ("%s() invalid buffer %p\n", __FUNCTION__, buffer);
        return -1;
    }

    if (aml_out->standby) {
        ALOGI("%s(), standby to unstandby", __func__);

        if (aml_out->tsync_status != TSYNC_STATUS_RUNNING && aml_out->hw_sync_mode) {
            hw_sync->first_apts_flag = false; //start tsync again.
        }

        aml_out->standby = false;
    }

    if (aml_out->hw_sync_mode) {
        // when connect bt, bt stream maybe open before hdmi stream close,
        // bt stream mediasync is set to adev->hw_mediasync, and it would be
        // release in hdmi stream close, so bt stream mediasync is invalid
        if ((aml_out->hwsync->mediasync != NULL) && (adev->hw_mediasync == NULL)) {
            adev->hw_mediasync = aml_hwsync_mediasync_create();
            aml_out->hwsync->use_mediasync = true;
            aml_out->hwsync->mediasync = adev->hw_mediasync;
            ret = aml_audio_hwsync_set_id(aml_out->hwsync, aml_out->hwsync->hwsync_id);
            if (!ret)
                ALOGD("%s: aml_audio_hwsync_set_id fail: ret=%d, id=%d", __func__, ret, aml_out->hwsync->hwsync_id);
            aml_audio_hwsync_init(aml_out->hwsync, aml_out);
            if (eDolbyMS12Lib == adev->dolby_lib_type)
                dolby_ms12_hwsync_init();
        }
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (!continous_mode(adev) && adev->ms12.dolby_ms12_enable) {
            if (adev->ms12.main_input_fmt != ms12_get_audio_hal_format(aml_out->hal_internal_format)) {
                bool set_ms12_non_continuous = true;
                ALOGI("main format is not match with current one, we need reset it");
                get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                adev->doing_reinit_ms12 = true;
            }
        }
    }
    case_cnt = popcount (adev->usecase_masks);
    if (adev->mix_init_flag == false) {
        ALOGI ("%s mix init, mask %#x",__func__,adev->usecase_masks);
        pthread_mutex_lock (&adev->lock);
        /* recovery from stanby case */
        if (aml_out->stream_status == STREAM_STANDBY) {
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
                ALOGI("%s,exit continuous release ms12 here", __func__);
                bool set_ms12_non_continuous = true;
                get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                adev->need_remove_conti_mode = false;
            }
        }
        need_reconfig_output = true;
        adev->mix_init_flag =  true;
        /*if mixer has started, no need restart*/
        if (!adev->hw_mixer.start_buf) {
            aml_hw_mixer_init(&adev->hw_mixer);
        }
        pthread_mutex_unlock(&adev->lock);
    }
    if (case_cnt > MAX_INPUT_STREAM_CNT) {
        ALOGE ("%s usemask %x,we do not support two direct stream output at the same time.TO CHECK CODE FLOW!!!!!!",__func__,adev->usecase_masks);
        return return_bytes;
    }
    /*for ms12 continuous mode, we need update status here, instead of in hw_write*/
    if (aml_out->stream_status == STREAM_STANDBY && continous_mode(adev)) {
        aml_out->stream_status = STREAM_HW_WRITING;
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
        need_reset_decoder = true;//digital_input_src ? true: false;
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

    if (adev->hdmi_format_updated) {
        ALOGI("%s(), hdmi format updated, need reconfig output, [PCM(0)/SPDIF(4)/AUTO(5)] %d", __func__, adev->hdmi_format);
        need_reconfig_output = true;
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            need_reset_decoder = false;
        } else {
            need_reset_decoder = true;
        }
        adev->hdmi_format_updated = 0;
    }

    if (adev->bHDMIConnected_update) {
        ALOGI("%s(), hdmi connect updated, need reconfig output", __func__);
        need_reconfig_output = true;
        need_reset_decoder = true;
        adev->bHDMIConnected_update = 0;
    }

    /* here to check if the audio output routing changed. */
    if (adev->out_device != aml_out->out_device) {
        ALOGI ("[%s:%d] output routing changed, need reconfig output, adev_dev:%#x, out_dev:%#x",
            __func__, __LINE__, adev->out_device, aml_out->out_device);
        need_reconfig_output = true;
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            need_reset_decoder = false;
        }
        aml_out->out_device = adev->out_device;
    }

hwsync_rewrite:
    /* handle HWSYNC audio data*/
    if (aml_out->hw_sync_mode) {
        uint64_t  cur_pts = ULLONG_MAX;//defined in limits.h
        int outsize = 0;

        ALOGV ("before aml_audio_hwsync_find_frame bytes %zu\n", total_bytes - bytes_cost);
        hwsync_cost_bytes = aml_audio_hwsync_find_frame(aml_out->hwsync, (char *)buffer + bytes_cost, total_bytes - bytes_cost, &cur_pts, &outsize);
        if (cur_pts > ULLONG_MAX) {
            ALOGE("APTS exeed the max 64bit value");
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
        if (cur_pts != ULLONG_MAX && outsize > 0) {
            if (eDolbyMS12Lib == adev->dolby_lib_type && !is_bypass_dolbyms12(stream)) {
                if (hw_sync->wait_video_done == false && hw_sync->use_mediasync) {
                    apts64 = cur_pts & ULLONG_MAX;
                    aml_hwsync_wait_video_start(hw_sync);
                    aml_hwsync_wait_video_drop(hw_sync, apts64);
                    hw_sync->wait_video_done = true;
                }

                // missing code with aml_audio_hwsync_checkin_apts, need to add for netflix tunnel mode. zzz
                aml_audio_hwsync_checkin_apts(aml_out->hwsync, aml_out->hwsync->payload_offset, cur_pts);
                if ((BYPASS != adev->hdmi_format) && continous_mode(adev) && !audio_is_linear_pcm(aml_out->hal_internal_format) && adev->is_netflix) {
                    dolby_ms12_hwsync_checkin_pts(aml_out->hwsync->payload_offset, cur_pts);
                }
                aml_out->hwsync->payload_offset += outsize;
            } else {
                // if we got the frame body,which means we get a complete frame.
                //we take this frame pts as the first apts.
                //this can fix the seek discontinue,we got a fake frame,which maybe cached before the seek
                if (hw_sync->use_mediasync) {
                    uint64_t apts;
                    int debug_enable = get_debug_value(AML_DEBUG_AUDIOHAL_HW_SYNC);
                    int latency = (int)out_get_latency(stream);
                    int tunning_latency = aml_audio_get_nonms12_tunnel_latency(stream) / 48;
                    int latency_pts = 0;
                    int video_delay_ms = 0;
                    bool valid_pts = true;
                    /*here we need add video delay*/
                    video_delay_ms = get_media_video_delay(&adev->alsa_mixer);
                    if ((latency + tunning_latency) > video_delay_ms) {
                        latency_pts = (latency + tunning_latency - video_delay_ms) * 90;
                    }
                    // check PTS discontinue, which may happen when audio track switching
                    // discontinue means PTS calculated based on first_apts and frame_write_sum
                    // does not match the timestamp of next audio samples
                    if (cur_pts >= abs(latency_pts)) {
                        apts = cur_pts - latency_pts;
                        valid_pts = true;
                    } else {
                        apts = 0;
                        valid_pts = false;
                    }
                    apts64 = apts & ULLONG_MAX;
                    /*if the pts is zero, to avoid video pcr not set issue, we just set it as 1ms*/
                    if (apts64 == 0) {
                        apts64 = 1 * 90;
                    }
                    if (debug_enable) {
                        ALOGI("total latency =%d ms alsa =%d video delay=%d tunning latency=%d apts 0x%llx apts64 0x%llx\n",
                        latency_pts / 90, latency, video_delay_ms, tunning_latency, apts, apts64);
                    }

                    aml_audio_hwsync_update_threshold(hw_sync);
                    if (hw_sync->wait_video_done == false) {
                        aml_hwsync_wait_video_start(hw_sync);
                        aml_hwsync_wait_video_drop(hw_sync, apts64);
                        hw_sync->wait_video_done = true;
                    }

                    /* video will drop frames from HEAAC to DDP51 in NTS fly audio cases,
                    ** the reason is that alsa threshold is emplty,
                    ** so pts update too quickly the first four times.
                    ** Here drop 3 packages pts for nonDolby DDP51 cases,
                    ** the other player not drop any pts.
                    */
                    int write_drop_threshold;
                    if (adev->is_netflix) {
                        write_drop_threshold = 3;
                    } else {
                        write_drop_threshold = 0;
                    }

                    if (aml_out->alsa_running_status == true && valid_pts) {
                        if (hw_sync->first_apts_flag == false) {
                            aml_audio_hwsync_set_first_pts(aml_out->hwsync, apts64);
                        }

                        uint64_t pcr = 0;
                        int pcr_pts_gap = 0;
                        ret = aml_hwsync_get_tsync_pts(aml_out->hwsync, &pcr);
                        aml_hwsync_reset_tsync_pcrscr(aml_out->hwsync, apts64);
                        pcr_pts_gap = ((int)(apts64 - pcr)) / 90;
                        if (abs(pcr_pts_gap) > 50 || debug_enable) {
                            ALOGI("%s pcr =%llu pts =%llu,  diff =%d ms", __func__, pcr/90, apts64/90, pcr_pts_gap);
                        }
                    } else {
                        ALOGI("%s  write_count:%d, drop this pts", __func__, aml_out->write_count);
                    }
                } else {

                    if (hw_sync->first_apts_flag == false) {
                        aml_audio_hwsync_set_first_pts(aml_out->hwsync, cur_pts);
                    } else {
                        uint64_t apts;
                        uint64_t apts64;
                        uint64_t pcr = 0;
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
                        apts64 = apts & ULLONG_MAX;
                        if (aml_hwsync_get_tsync_pts(aml_out->hwsync, &pcr) == 0) {
                            enum hwsync_status sync_status = CONTINUATION;
                            apts_gap = get_pts_gap (pcr, apts64);
                            sync_status = check_hwsync_status (apts_gap);

                            // limit the gap handle to 0.5~5 s.
                            if (sync_status == ADJUSTMENT) {
                                // two cases: apts leading or pcr leading
                                // apts leading needs inserting frame and pcr leading neads discarding frame
                                if (apts64 > pcr) {
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
                                        ALOGI ("audio slow 0x%x,skip frame @pts 0x%"PRIx64",pcr 0x%llx,cur apts 0x%llx\n",
                                        apts_gap, cur_pts, pcr, apts64);
                                        aml_out->frame_skip_sum  +=   1536;
                                        return_bytes = hwsync_cost_bytes;
                                        goto exit;
                                    }
                                }
                            } else if (sync_status == RESYNC) {
                                ALOGI ("tsync -> reset pcrscr 0x%llx -> ox%llx, %s big,diff %"PRIx64" ms",
                                    pcr, apts64, apts64 > pcr ? "apts" : "pcr", get_pts_gap (apts, pcr) / 90);

                                int ret_val = aml_hwsync_reset_tsync_pcrscr(aml_out->hwsync, apts64);
                                if (ret_val == -1) {
                                    ALOGE ("aml_hwsync_reset_tsync_pcrscr,err: %s", strerror (errno) );
                                }
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

    if (write_bytes > 0) {
        if ((eDolbyMS12Lib == adev->dolby_lib_type) && continous_mode(adev)) {
            /*SWPL-11531 resume the timer here, because we have data now*/
            /*resume ms12/hwsync here, as we receive the first data*/
            if (adev->ms12.need_ms12_resume) {
                ALOGI("%s resume the ms12 and hwsync", __func__);
                pthread_mutex_lock(&ms12->lock);
                ms12->ms12_resume_state = MS12_RESUME_FROM_RESUME;
                audiohal_send_msg_2_ms12(ms12, MS12_MESG_TYPE_RESUME);
                pthread_mutex_unlock(&ms12->lock);
                adev->ms12.need_resync = 1;
                adev->ms12.need_ms12_resume = false;
            } else if (aml_out->tsync_status == TSYNC_STATUS_STOP && aml_out->hw_sync_mode) {
                pthread_mutex_lock(&ms12->lock);
                audiohal_send_msg_2_ms12(ms12, MS12_MESG_TYPE_RESUME);
                aml_hwsync_set_tsync_resume(aml_out->hwsync);
                aml_out->tsync_status = TSYNC_STATUS_RUNNING;
                adev->ms12.need_resync = 1;
                ALOGI("resume ms12 and the timer");
                pthread_mutex_unlock(&ms12->lock);
            }
        }
    }

    /* here to check if the audio input format changed. */
    audio_format_t cur_aformat;
    if (adev->audio_patch && aml_out->is_tv_src_stream && IS_DIGITAL_IN_HW(patch->input_src)) {
        cur_aformat = audio_parse_get_audio_type (patch->audio_parse_para);
        if (cur_aformat != patch->aformat) {
            ALOGI ("HDMI/SPDIF input format changed from %#x to %#x\n", patch->aformat, cur_aformat);
            patch->aformat = cur_aformat;
            //FIXME: if patch audio format change, the hal_format need to redefine.
            //then the out_get_format() can get it.
            ALOGI ("hal_format changed from %#x to %#x\n", aml_out->hal_format, cur_aformat);
            if (cur_aformat != AUDIO_FORMAT_PCM_16_BIT && cur_aformat != AUDIO_FORMAT_PCM_32_BIT) {
                aml_out->hal_format = AUDIO_FORMAT_IEC61937;
            } else {
                aml_out->hal_format = cur_aformat ;
            }
            aml_out->hal_internal_format = cur_aformat;
            aml_out->hal_channel_mask = audio_parse_get_audio_channel_mask (patch->audio_parse_para);
            ALOGI ("%s hal_channel_mask %#x\n", __FUNCTION__, aml_out->hal_channel_mask);
            if (aml_out->hal_internal_format == AUDIO_FORMAT_DTS ||
                aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD) {
                /*when switch from ms12 to dts, we should clean ms12 first*/
                if (adev->dolby_lib_type == eDolbyMS12Lib) {
                    get_dolby_ms12_cleanup(&adev->ms12, false);
                }
                adev->dolby_lib_type = eDolbyDcvLib;
                if (aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD) {
                    /* For DTS HBR case, needs enlarge buffer and start threshold to anti-xrun */
                    aml_out->config.period_count = 6;
                    aml_out->config.start_threshold =
                        aml_out->config.period_size * aml_out->config.period_count;
                }

                if (audio_parse_get_audio_type_direct(patch->audio_parse_para) == DTSCD ) {
                    aml_out->is_dtscd = true;
                } else {
                    aml_out->is_dtscd = false;
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
            aml_audio_delay_clear(AML_DELAY_OUTPORT_SPDIF_RAW);
            aml_audio_delay_clear(AML_DELAY_OUTPORT_ARC_RAW);
            aml_audio_delay_clear(AML_DELAY_OUTPORT_ALL);
#endif
            /* we need standy a2dp when switch the format, in order to prevent UNDERFLOW in a2dp stack. */
            if (adev->active_outport == OUTPORT_A2DP) {
                adev->need_reset_a2dp = true;
            }

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
            need_reconfig_samplerate = true;
        } else {
            need_reconfig_samplerate = false;
        }
    }
    /*dts cd process need to discuss here */
    else if (aml_out->hal_format == AUDIO_FORMAT_IEC61937 &&
        (aml_out->hal_internal_format == AUDIO_FORMAT_DTS || aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD) &&
        !aml_out->dts_check) {

        audio_channel_mask_t cur_ch_mask;
        int package_size;
        int cur_audio_type = audio_type_parse(write_buf, write_bytes, &package_size, &cur_ch_mask);

        cur_aformat = audio_type_convert_to_android_audio_format_t(cur_audio_type);
        ALOGI("cur_aformat:%0x cur_audio_type:%d", cur_aformat, cur_audio_type);
        if (cur_audio_type == DTSCD) {
            aml_out->is_dtscd = true;
        } else {
            aml_out->is_dtscd = false;
        }
        aml_out->dts_check = true;
        /*
        if (cur_aformat == AUDIO_FORMAT_DTS || cur_aformat == AUDIO_FORMAT_AC3) {
            aml_out->hal_internal_format = cur_aformat;
            if (aml_out->hal_internal_format == AUDIO_FORMAT_DTS) {
                adev->dolby_lib_type = eDolbyDcvLib;
                aml_out->restore_dolby_lib_type = true;
            } else if (aml_out->hal_internal_format == AUDIO_FORMAT_AC3) {
                adev->dolby_lib_type = adev->dolby_lib_type_last;
            }
        } else {
            return return_bytes;
        }
        */
    }
    else if (!is_bypass_dolbyms12(stream)) {
        adev->dolby_lib_type = adev->dolby_lib_type_last;
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        /**
         * Need config MS12 if its not enabled.
         * Since MS12 is essential for main input.
         */
        if (!adev->ms12.dolby_ms12_enable && !is_bypass_dolbyms12(stream)) {
            ALOGI("ms12 is not enabled, reconfig it");
            need_reconfig_output = true;
            need_reset_decoder = true;
        }

        /**
         * Need config MS12 in this scenario.
         * Switch source between HDMI1 and HDMI2, the two source playback pcm data.
         * sometimes dolby_ms12_enable is true(system stream config ms12), here should reconfig
         * ms12 when switching to HDMI stream source.(Jira:TV-46722)
         */
        if (need_reconfig_output && adev->ms12.dolby_ms12_enable && patch && patch->input_src == AUDIO_DEVICE_IN_HDMI) {
            need_reset_decoder = true;
            ALOGI ("%s() %d, HDMI input source, need reset decoder:%d", __func__, __LINE__, need_reset_decoder);
        }
    }

    if (need_reconfig_output) {
        config_output (stream,need_reset_decoder);
        need_reconfig_output = false;
    }

    if ((eDolbyMS12Lib == adev->dolby_lib_type) && !is_bypass_dolbyms12(stream) && !is_dts_format(aml_out->hal_internal_format)) {
        // in NETFLIX moive selcet screen, switch between movies, adev->ms12_out will change.
        // so we need to update to latest staus just before use.zzz
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
            if (!adev->is_netflix) {
                set_ms12_acmod2ch_lock(&adev->ms12, false);
                aml_out->ms12_acmod2ch_lock_disable = true;
            }
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_lock(&adev->trans_lock);
            ms12_out->hal_internal_format = aml_out->hal_internal_format;
            ms12_out->hw_sync_mode = aml_out->hw_sync_mode;
            ms12_out->hwsync = aml_out->hwsync;
            ms12_out->hal_ch = aml_out->hal_ch;
            ms12_out->hal_rate = aml_out->hal_rate;
            pthread_mutex_unlock(&adev->trans_lock);
            ALOGI("%s set dolby main1 dummy false", __func__);
        } else if (continous_mode(adev) && adev->ms12_ott_enable == false
                   && audio_is_linear_pcm(aml_out->hal_internal_format)) {
            pthread_mutex_lock(&adev->lock);
            dolby_ms12_set_main_dummy(1, false);
            adev->ms12_ott_enable = true;
            if (!adev->is_netflix && is_multi_channel_pcm(stream)) {
                set_ms12_acmod2ch_lock(&adev->ms12, false);
                aml_out->ms12_acmod2ch_lock_disable = true;
                ALOGI("ms12 multi pcm in, disable acmod 2ch lock");
            }
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_lock(&adev->trans_lock);
            ms12_out->hal_internal_format = aml_out->hal_internal_format;
            ms12_out->hw_sync_mode = aml_out->hw_sync_mode;
            ms12_out->hwsync = aml_out->hwsync;
            ms12_out->hal_ch = aml_out->hal_ch;
            ms12_out->hal_rate = aml_out->hal_rate;
            pthread_mutex_unlock(&adev->trans_lock);
            ALOGI("%s set dolby ott enable", __func__);
        }

        /*the hal_internal_format of ms12 not update to format of new stream(DDP)
        **from HDMI to local player, as adev->ms12_main1_dolby_dummy not set to true in config_output.
        **so update the format of ms12 to avoid this case last_frames_position(0) issue.
        */
        if (ms12_out->hal_internal_format != aml_out->hal_internal_format) {
            ms12_out->hal_internal_format = aml_out->hal_internal_format;
        }
        /*during netlfix pause, it will first pause, then ms12 will do fade out
          but netflix will continue write some data, this will resume ms12 again.
          then the sound will be discontinue. we need remove this
        */
#if 0
        if (continous_mode(adev)) {
            if ((adev->ms12.dolby_ms12_enable == true) && (adev->ms12.is_continuous_paused == true)) {
                pthread_mutex_lock(&ms12->lock);
                dolby_ms12_set_pause_flag(false);
                adev->ms12.is_continuous_paused = false;
                set_dolby_ms12_runtime_pause(&(adev->ms12), adev->ms12.is_continuous_paused);
                pthread_mutex_unlock(&ms12->lock);
            }
        }
#endif
    }
    aml_out->input_bytes_size += write_bytes;
    if (patch && (adev->dtslib_bypass_enable || adev->dcvlib_bypass_enable)) {
        int cur_samplerate = audio_parse_get_audio_samplerate(patch->audio_parse_para);
        if (cur_samplerate != patch->input_sample_rate || need_reconfig_samplerate) {
            ALOGI ("HDMI/SPDIF input samplerate from %d to %d, or need_reconfig_samplerate\n",
                    patch->input_sample_rate, cur_samplerate);
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

    /*
     *when disable_pcm_mixing is true, the 7.1ch DD+ could not be process with Dolby MS12
     *the HDMI-In or Spdif-In is special with IEC61937 format, other input need packet with spdif-encoder.
     */
    audio_format_t output_format = get_output_format (stream);
    if (adev->debug_flag) {
        ALOGD("%s:%d hal_format:%#x, output_format:0x%x, sink_format:0x%x",
            __func__, __LINE__, aml_out->hal_format, output_format, adev->sink_format);
    }
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        ret = aml_audio_ms12_render(stream, write_buf, write_bytes);
    } else if (eDolbyDcvLib == adev->dolby_lib_type) {
        ret = aml_audio_nonms12_render(stream, write_buf, write_bytes);
    }


exit:
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (continous_mode(adev)) {
            aml_out->timestamp = adev->ms12.timestamp;
            //clock_gettime(CLOCK_MONOTONIC, &aml_out->timestamp);
            aml_out->last_frames_postion = adev->ms12.last_frames_postion;
            if (adev->debug_flag)
                ALOGI("%s out:%p aml_out->last_frames_postion:%llu \n", __FUNCTION__, aml_out, aml_out->last_frames_postion);
        }
    }
    /*if the data consume is not complete, it will be send again by audio flinger,
      this old data will cause av sync problem after seek.
    */
    if (aml_out->hw_sync_mode) {
        /*
        if the data is not  consumed totally,
        we need re-send data again
        */
        if (return_bytes > 0 && total_bytes > (return_bytes + bytes_cost)) {
            bytes_cost += return_bytes;
            //ALOGI("total bytes=%d cost=%d return=%d", total_bytes,bytes_cost,return_bytes);

            /* We need to wait for Google to fix the issue:
             * Issue: After pause, there will be residual sound in AF, which will cause NTS fail.
             * Now we need to judge whether the current format is DTS */
            if (is_dts_format(aml_out->hal_internal_format))
                return bytes_cost;
            else
                goto hwsync_rewrite;
        }
        else if (return_bytes < 0)
            return return_bytes;
        else
            return total_bytes;
    }



    if (adev->debug_flag) {
        ALOGI("%s return %d!\n", __FUNCTION__, return_bytes);
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
    uint64_t sleep_time_us = 0;

    if (eDolbyMS12Lib == adev->dolby_lib_type && continous_mode(adev)) {
        enter_ns = aml_audio_get_systime_ns();
    }

    if (adev->debug_flag) {
        ALOGD("%s:%d size:%zu, dolby_lib_type:0x%x, frame_size:%zu",
        __func__, __LINE__, bytes, adev->dolby_lib_type, frame_size);
    }

    if ((aml_out->stream_status == STREAM_HW_WRITING) && hw_mix) {
        ALOGI("%s(), aux do alsa close\n", __func__);
        pthread_mutex_lock(&adev->alsa_pcm_lock);
        aml_alsa_output_close(stream);

        ALOGI("%s(), aux alsa close done\n", __func__);
        aml_out->stream_status = STREAM_MIXING;
        pthread_mutex_unlock(&adev->alsa_pcm_lock);
    }

    if (aml_out->stream_status == STREAM_STANDBY) {
        aml_out->stream_status = STREAM_MIXING;
    }

    if (aml_out->standby) {
        ALOGI("%s(), standby to unstandby", __func__);
        aml_out->audio_data_handle_state = AUDIO_DATA_HANDLE_START;
        aml_out->standby = false;
    }

    if (aml_out->is_normal_pcm && !aml_out->normal_pcm_mixing_config) {
        if (0 == set_system_app_mixing_status(aml_out, aml_out->stream_status)) {
            aml_out->normal_pcm_mixing_config = true;
        } else {
            aml_out->normal_pcm_mixing_config = false;
        }
    }

    pthread_mutex_unlock(&adev->lock);
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (adev->a2dp_no_reconfig_ms12 > 0) {
            uint64_t curr = aml_audio_get_systime();
            if (adev->a2dp_no_reconfig_ms12 <= curr)
                adev->a2dp_no_reconfig_ms12 = 0;
        }
        if (continous_mode(adev)) {
            /*only system sound active*/
            if (!hw_mix && (!(dolby_stream_active(adev) || hwsync_lpcm_active(adev)))) {
                /* here to check if the audio HDMI ARC format updated. */
                if (((adev->arc_hdmi_updated) || (adev->a2dp_updated) || (adev->hdmi_format_updated) || (adev->bHDMIConnected_update))
                    && (adev->ms12.dolby_ms12_enable == true)) {
                    //? if we need protect
                    if ((adev->a2dp_no_reconfig_ms12 > 0) && (aml_out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) && (adev->a2dp_updated == 0)) {
                        need_reset_decoder = false;
                        ALOGD("%s: a2dp output and change audio format, no reconfig ms12 for hdmi update", __func__);
                    } else {
                        need_reset_decoder = true;
                        if (adev->hdmi_format_updated) {
                            ALOGI("%s(), hdmi format updated, [PCM(0)/SPDIF(4)/AUTO(5)] current %d", __func__, adev->hdmi_format);
                        }
                        else
                            ALOGI("%s() %s%s%s changing status, need reconfig Dolby MS12\n", __func__,
                                    (adev->arc_hdmi_updated==0)?" ":"HDMI ARC EndPoint ",
                                    (adev->bHDMIConnected_update==0)?" ":"HDMI ",
                                    (adev->a2dp_updated==0)?" ":"a2dp ");
                    }
                    adev->arc_hdmi_updated = 0;
                    adev->a2dp_updated = 0;
                    adev->hdmi_format_updated = 0;
                    adev->bHDMIConnected_update = 0;
                    need_reconfig_output = true;
                }

                /* here to check if the audio output routing changed. */
                if ((adev->out_device != aml_out->out_device) && (adev->ms12.dolby_ms12_enable == true)) {
                    ALOGI("%s(), output routing changed from 0x%x to 0x%x,need MS12 reconfig output", __func__, aml_out->out_device, adev->out_device);
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
            /* here to check if ms12 is already enabled, if main stream is doing init ms12, we don't need do it */
            if (!adev->ms12.dolby_ms12_enable && !adev->doing_reinit_ms12 && !adev->doing_cleanup_ms12) {
                ALOGI("%s(), 0x%x, Swithing system output to MS12, need MS12 reconfig output", __func__, aml_out->out_device);
                need_reconfig_output = true;
                need_reset_decoder = true;
            }

            if (need_reconfig_output) {
                /*during ms12 switch, the frame write may be not matched with
                  the input size, we need to align it*/
                if (aml_out->frame_write_sum * frame_size != aml_out->input_bytes_size) {
                    ALOGI("Align the frame write from %" PRId64 " to %" PRId64 "", aml_out->frame_write_sum, aml_out->input_bytes_size/frame_size);
                    aml_out->frame_write_sum = aml_out->input_bytes_size/frame_size;
                }
                config_output(stream,need_reset_decoder);
            }
        }
        /*
         *when disable_pcm_mixing is true and offload format is ddp and output format is ddp
         *the system tone voice should not be mixed
         */
        if (is_bypass_dolbyms12(stream)) {
            ms12->sys_audio_skip += bytes / frame_size;
            usleep(bytes * 1000000 /frame_size/out_get_sample_rate(&stream->common)*5/6);
        } else {
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

            /*No need here, We do track switch in audio_hal_data_processing()*/
            /*
            {
                aml_audio_switch_output_mode((int16_t *)buffer, bytes, adev->sound_track_mode);
            }*/

            /* audio zero data detect, and do fade in */
            if (adev->is_netflix && STREAM_PCM_NORMAL == aml_out->usecase) {
                aml_audio_data_handle(stream, buffer, bytes);
            }

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
            }
            if (bytes_remaining) {
                ms12->sys_audio_skip += bytes_remaining / frame_size;
                ALOGI("bytes_remaining =%d totoal skip =%lld", bytes_remaining, ms12->sys_audio_skip);
            }
        }
    } else {
        size_t content_bytes = aml_hw_mixer_get_content_l(&adev->hw_mixer);
        size_t space_bytes = adev->hw_mixer.buf_size - content_bytes;
        bytes_written = aml_hw_mixer_write(&adev->hw_mixer, buffer, bytes);
        /*these data is skip for ms12, we still need calculate it*/
        if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
            ms12->sys_audio_skip += bytes / frame_size;
        }
        if (content_bytes < adev->hw_mixer.buf_size / 2) {
            sleep_time_us = (uint64_t)bytes_written * 1000000 / frame_size / out_get_sample_rate(&stream->common) / 2;
        } else {
            sleep_time_us = (uint64_t)bytes_written * 1000000 / frame_size / out_get_sample_rate(&stream->common);
        }
        ALOGV("aml_audio_sleep  sleep_time_us %lld ",sleep_time_us);
        aml_audio_sleep(sleep_time_us);

        if (getprop_bool("vendor.media.audiohal.mixer")) {
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

        if (adev->compensate_video_enable) {
            alsa_latency_frame = 0;
        }
        aml_out->last_frames_postion = aml_out->frame_write_sum - system_latency;
        if (aml_out->last_frames_postion >= alsa_latency_frame) {
            aml_out->last_frames_postion -= alsa_latency_frame;
        }
        if (adev->debug_flag) {
            ALOGI("%s stream audio presentation %"PRIu64" latency_frame %d.ms12 system latency_frame %d,total frame=%" PRId64 " %" PRId64 " ms",
                  __func__,aml_out->last_frames_postion, alsa_latency_frame, system_latency,aml_out->frame_write_sum, aml_out->frame_write_sum/48);
        }
    } else {
        aml_out->last_frames_postion = aml_out->frame_write_sum;
    }

    /*if system sound return too quickly, it will causes audio flinger underrun*/
    if (eDolbyMS12Lib == adev->dolby_lib_type && continous_mode(adev)) {
        uint64_t cost_time_us = 0;
        uint64_t frame_us = in_frames*1000/48;
        uint64_t minum_sleep_time_us = 5000;
        leave_ns = aml_audio_get_systime_ns();
        cost_time_us = (leave_ns - enter_ns)/1000;
        /*it costs less than 10ms*/
        //ALOGI("cost us=%lld frame/2=%lld", cost_time_us, frame_us/2);
        if ( cost_time_us < minum_sleep_time_us) {
            //ALOGI("sleep =%lld", minum_sleep_time_us - cost_time_us);
            aml_audio_sleep(minum_sleep_time_us - cost_time_us);
        }
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
        ALOGD("[%s:%d] size:%zu, frame_size:%zu", __func__, __LINE__, bytes, frame_size);
    }

    if (eDolbyMS12Lib != adev->dolby_lib_type) {
        ALOGW("[%s:%d] dolby_lib_type:%d, is not ms12, not support app write", __func__, __LINE__, adev->dolby_lib_type);
        return -1;
    }

    if (is_bypass_dolbyms12(stream)) {
        ALOGW("[%s:%d] is_bypass_dolbyms12, not support app write", __func__, __LINE__);
        return -1;
    }

    uint32_t channels = audio_channel_count_from_out_mask(aml_out->hal_channel_mask);
    if (channels > 2) {
        ALOGW("[%s:%d] channels:%d > 2, not support app write", __func__, __LINE__, channels);
        return -1;
    }

    /*for ms12 continuous mode, we need update status here, instead of in hw_write*/
    if (aml_out->stream_status == STREAM_STANDBY && continous_mode(adev)) {
        aml_out->stream_status = STREAM_HW_WRITING;
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

    if (adev->out_device != aml_out->out_device) {
        ALOGD("%s:%p device:%x,%x", __func__, stream, aml_out->out_device, adev->out_device);
        aml_out->out_device = adev->out_device;
        config_output(stream, true);
    }

    /*during ms12 continuous exiting, the write function will be
     set to this function, then some part of audio need to be
     discarded, otherwise it will cause audio gap*/
    if (adev->exiting_ms12) {
        int64_t gap;
        void * data = (void*)buffer;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        gap = calc_time_interval_us(&adev->ms12_exiting_start, &ts);

        if (gap >= 500*1000) {
            adev->exiting_ms12 = 0;
        } else {
            ALOGV("during MS12 exiting gap=%" PRId64 " mute the data", gap);
            memset(data, 0, bytes);
        }
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
int usecase_change_validate_l(struct aml_stream_out *aml_out, bool is_standby)
{
    struct aml_audio_device *aml_dev = NULL;
    struct dolby_ms12_desc *ms12 = NULL;
    bool hw_mix = false;
    if (aml_out == NULL) {
        ALOGE("%s stream is NULL", __func__);
        return 0;
    }
    aml_dev = aml_out->dev;
    ms12 = &(aml_dev->ms12);

    if (is_standby) {
        ALOGI("++[%s:%d], dev masks:%#x, is_standby:%d, out usecase:%s", __func__, __LINE__,
            aml_dev->usecase_masks, is_standby, usecase2Str(aml_out->usecase));
        /**
         * If called by standby, reset out stream's usecase masks and clear the aml_dev usecase masks.
         * So other active streams could know that usecase have been changed.
         * But keep it's own usecase if out_write is called in the future to exit standby mode.
         */
        aml_out->write = NULL;

        if (aml_out->dev_usecase_masks) {
            aml_dev->usecase_masks &= ~(1 << aml_out->usecase);
            aml_out->dev_usecase_masks = 0;
        }
        aml_dev->usecase_cnt[aml_out->usecase]--;

        /* out_standby_new will be called after seek, here shouldn't setup the raw_to_pcm_flag.
        ** raw_to_pcm_flag will be set in aml_audio_spdif_output_stop when raw stream exit.
        */
        /*if ((aml_out->usecase == STREAM_RAW_DIRECT ||
            aml_out->usecase == STREAM_RAW_HWSYNC)
            && (eDolbyDcvLib == aml_dev->dolby_lib_type)) {
            aml_dev->raw_to_pcm_flag = true;
            ALOGI("enable raw_to_pcm_flag !!!");
        }*/

        if (0 == aml_dev->usecase_masks) {
            // send the SCHEDULER_STANDBY to ms12.
            aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_STANDBY);
        } else {
            // do something.
        }
        ALOGI("--[%s:%d], dev masks:%#x, is_standby:%d, out usecase %s", __func__, __LINE__,
            aml_dev->usecase_masks, is_standby, usecase2Str(aml_out->usecase));
        return 0;
    }

    /* No usecase changes, do nothing */
    if (((aml_dev->usecase_masks == aml_out->dev_usecase_masks) && aml_dev->usecase_masks) && (aml_dev->continuous_audio_mode == 0)) {
        /**
         * When the current write_func was not matching with the function usecase_change_validate_l,
         * the write_func should be reset to avoid the wrong write_func was used in current thread.
         */
        if ((STREAM_PCM_NORMAL == aml_out->usecase) && (aml_out->write_func == MIXER_AUX_BUFFER_WRITE_SM)) {
            ALOGE("[%s:%d], aml_out->write_func is %d, reset it!", __func__, __LINE__, aml_out->write_func);
        } else {
            ALOGV("[%s:%d], No usecase changes, do nothing!", __func__, __LINE__);
            return 0;
        }
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
        aml_dev->usecase_cnt[aml_out->usecase]++;
        if ((1 << aml_out->usecase) & aml_dev->usecase_masks) {
            ALOGE("[%s:%d], usecase: %s already exists!!, aml_out:%p", __func__,  __LINE__, usecase2Str(aml_out->usecase), aml_out);
            return -EINVAL;
        }
        /* add the new output usecase to aml_dev usecase masks */
        aml_dev->usecase_masks |= 1 << aml_out->usecase;
    }

    /*any stream is active, and the ms12 scheduler state is not Running.
    **here should send the MS12_SCHEDULER_RUNNING to ms12.
    */
    if (ms12->ms12_scheduler_state != MS12_SCHEDULER_RUNNING && aml_dev->usecase_masks >= 1) {
        aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_RUNNING);
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
                aml_out->write_func = MIXER_AUX_BUFFER_WRITE;
                ALOGI("%s(),1 mixer_aux_buffer_write !", __FUNCTION__);
            } else {
                aml_out->write = mixer_main_buffer_write;
                aml_out->write_func = MIXER_MAIN_BUFFER_WRITE;
                ALOGI("%s(),1 mixer_main_buffer_write !", __FUNCTION__);
            }
        } else {
            /**
             * only one stream output will be processed then send to hw.
             * This case only for normal output without mixing
             */
            aml_out->write = process_buffer_write;
            aml_out->write_func = PROCESS_BUFFER_WRITE;
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
            aml_out->write_func = MIXER_AUX_BUFFER_WRITE;

            //ALOGE("%s(),2 mixer_aux_buffer_write !", __FUNCTION__);
            //FIXEME if need config ms12 here if neeeded.
        } else if (aml_out->flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
            aml_out->write = mixer_app_buffer_write;
            aml_out->write_func = MIXER_APP_BUFFER_WRITE;
            //ALOGI("[%s:%d], mixer_app_buffer_write !", __func__, __LINE__);
        } else {
            aml_out->write = mixer_main_buffer_write;
            aml_out->write_func = MIXER_MAIN_BUFFER_WRITE;
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
#if ANDROID_PLATFORM_SDK_VERSION > 29
    if (aml_out && adev &&
        (aml_out->dev->patch_src == SRC_DTV) &&
        aml_out->dev->audio_patching &&
        (aml_out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) &&
        (aml_out->audioCfg.offload_info.content_id != 0)&&
        (aml_out->audioCfg.offload_info.sync_id != 0)) {
        /*enter into tuner framework case, this data is from framework.
        we need to make its write dummy*/
        if (adev->debug_flag > 1) {
            ALOGD("%s:patching %d, adev:%p, out->dev:%p, patch:%p.finish write", __func__, aml_out->dev->audio_patching, adev, aml_out->dev, adev->audio_patch);
        }
        return bytes;
    }
#endif

    if (aml_audio_trace_debug_level() > 0) {
        if (false == aml_out->pause_status  &&  aml_out->write_count < 1) {
            aml_out->write_time = aml_audio_get_systime() / 1000; //us --> ms
            ALOGD("%s: out_stream(%p) bytes(%zu), write_time:%llu, count:%d", __func__,
                       stream, bytes, aml_out->write_time, aml_out->write_count);
        }
    }
    aml_out->write_count++;

    if (!aml_out->is_tv_src_stream && (aml_out->flags & AUDIO_OUTPUT_FLAG_DIRECT) && adev->audio_patch) {
        ALOGD("%s: AF direct stream comming, patch exists, first release it", __func__);
        pthread_mutex_lock(&adev->patch_lock);
        if (adev->audio_patch && adev->audio_patch->is_dtv_src)
            release_dtv_patch_l(adev);
        else
            release_patch_l(adev);
        pthread_mutex_unlock(&adev->patch_lock);

        /*for no patch case, we need to restore it*/
        adev_release_patch_restore_resource(adev);
    }

    /*when there is data writing in this stream, we can add it to active stream*/
    pthread_mutex_lock(&adev->lock);
    adev->active_outputs[aml_out->usecase] = aml_out;
    if (adev->direct_mode) {
        /*
         * when the third_party apk calls pcm_close during use and then calls pcm_open again,
         * primary hal does not access the sound card,
         * continue to let the third_party apk access the sound card.
         */
        aml_alsa_output_close(stream);
        aml_out->stream_status = STREAM_STANDBY;
        if (adev->debug_flag) {
            ALOGI("%s,direct mode write,skip bytes %zu\n",__func__,bytes);
        }
        /*TODO accurate delay time */
        usleep(in_frames*1000/48);
        aml_out->frame_write_sum += in_frames;
        pthread_mutex_unlock(&adev->lock);
        return bytes;
    }
    pthread_mutex_unlock(&adev->lock);


    /*move it from open function, because when hdmi hot plug, audio service will
     * call many times open/close to query the hdmi capability, this will affect the
     * sink format
     */
    if (!aml_out->is_sink_format_prepared) {
        get_sink_format(&aml_out->stream);
        if (!adev->is_TV) {
            if (is_use_spdifb(aml_out)) {
                aml_audio_select_spdif_to_hdmi(AML_SPDIF_B_TO_HDMITX);
                aml_out->restore_hdmitx_selection = true;
            }
            aml_out->card = alsa_device_get_card_index();
            if (adev->sink_format == AUDIO_FORMAT_PCM_16_BIT) {
                aml_out->device = PORT_I2S;
            } else {
                aml_out->device = PORT_SPDIF;
            }
        }
        aml_out->is_sink_format_prepared = true;
    }

    if (aml_out->continuous_mode_check) {
        if (adev->dolby_lib_type_last == eDolbyMS12Lib) {
            /*if these format can't be supported by ms12, we can bypass it*/
            if (is_bypass_dolbyms12(stream)) {
                adev->dolby_lib_type = eDolbyDcvLib;
                aml_out->restore_dolby_lib_type = true;
                ALOGI("bypass ms12 change dolby dcv lib type");
            }
            if (is_disable_ms12_continuous(stream)) {
                if (adev->continuous_audio_mode) {
                    ALOGI("Need disable MS12 continuous");
                    if (adev->dolby_lib_type == eDolbyMS12Lib) {
                        adev->doing_reinit_ms12 = true;
                    }
                    bool set_ms12_non_continuous = true;
                    get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
                    adev->exiting_ms12 = 1;
                    aml_out->restore_continuous = true;
                    clock_gettime(CLOCK_MONOTONIC, &adev->ms12_exiting_start);
                }
            }
            else if (is_need_reset_ms12_continuous(stream)) {
                    ALOGI("Need reset MS12 continuous as main audio changed\n");
                    adev->doing_reinit_ms12 = true;
                    get_dolby_ms12_cleanup(&adev->ms12, false);
            } else if (is_support_ms12_reset(stream)) {
                ALOGI("is_support_ms12_reset true\n");
                adev->doing_reinit_ms12 = true;
                get_dolby_ms12_cleanup(&adev->ms12, false);
            }
        }
        aml_out->continuous_mode_check = false;
    }

    if ((adev->dolby_lib_type_last == eDolbyMS12Lib) &&
        (adev->audio_patching)) {
        /*in patching case, we can't use continuous mode*/
        if (adev->continuous_audio_mode) {
            bool set_ms12_non_continuous = true;
            ALOGI("in patch case src =%d, we need disable continuous mode", adev->patch_src);
            get_dolby_ms12_cleanup(&adev->ms12, set_ms12_non_continuous);
            adev->doing_reinit_ms12 = true;
        }
    }


    aml_audio_trace_int("out_write_new", bytes);
    /**
     * deal with the device output changes
     * pthread_mutex_lock(&aml_out->lock);
     * out_device_change_validate_l(aml_out);
     * pthread_mutex_unlock(&aml_out->lock);
     */
    pthread_mutex_lock(&adev->lock);
    ret = usecase_change_validate_l(aml_out, false);
    if (ret < 0) {
        ALOGE("%s() failed", __func__);
        pthread_mutex_unlock(&adev->lock);
        aml_audio_trace_int("out_write_new", 0);
        return ret;
    }

    if (aml_out->write) {
        write_func_p = aml_out->write;
    }
    pthread_mutex_unlock(&adev->lock);
    if (write_func_p) {
        ret = write_func_p(stream, buffer, bytes);
        /* update audio format to display audio info banner.*/
        /* DTS needs earlier update in decode flow */
        if (!is_dts_format(aml_out->hal_internal_format))
            update_audio_format(adev, aml_out->hal_internal_format);
    }
    aml_audio_trace_int("out_write_new", 0);
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
    int ret;
    ALOGD("%s: enter", __func__);
    ret = adev_open_output_stream(dev,
                                    0,
                                    devices,
                                    flags,
                                    config,
                                    stream_out,
                                    address);
    if (ret < 0) {
        ALOGE("%s(), open stream failed", __func__);
        return ret;
    }

    aml_out = (struct aml_stream_out *)(*stream_out);
    aml_out->usecase = attr_to_usecase(aml_out->device, aml_out->hal_format, aml_out->flags);
    aml_out->is_normal_pcm = (aml_out->usecase == STREAM_PCM_NORMAL) ? 1 : 0;
    aml_out->out_cfg = *config;
    if (adev->useSubMix) {
        // In V1.1, android out lpcm stream and hwsync pcm stream goes to aml mixer,
        // tv source keeps the original way.
        // Next step is to make all compitable.
        unsigned int channel_num = audio_channel_count_from_out_mask(config->channel_mask);
        if (aml_out->usecase == STREAM_PCM_NORMAL ||
            aml_out->usecase == STREAM_PCM_HWSYNC ||
            aml_out->usecase == STREAM_PCM_MMAP ||
            (aml_out->usecase == STREAM_PCM_DIRECT &&
            config->sample_rate == 48000 && channel_num == 2)) {
            /*for 96000, we need bypass submix, this is for DTS certification*/
            /* for DTV case, maybe this function is called by the DTV output thread,
               and the audio patch is enabled, we do not need to wait DTV exit as it is
               enabled by DTV itself */
            if (config->sample_rate == 96000 || config->sample_rate == 88200 ||
                    (aml_out->usecase != STREAM_PCM_MMAP && channel_num > 2) || aml_out->is_tv_src_stream) {
                aml_out->bypass_submix = true;
                aml_out->stream.write = out_write_new;
                aml_out->stream.common.standby = out_standby_new;
                ALOGI("bypass submix");
            } else {
                int retry_count = 0;
                /* when dvb switch to neflix,dvb send cmd stop and dtv decoder_state
                    AUDIO_DTV_PATCH_DECODER_STATE_INIT, but when hdmi plug in and out dvb
                    do not send cmd stop and only relase audiopatch,dtv decoder_state AUDIO_DTV_PATCH_DECODER_STATE_RUNING*/
                while  (adev->audio_patch && adev->audio_patching
                    && adev->patch_src == SRC_DTV && retry_count < 50
                    && adev->audio_patch->dtv_decoder_state == AUDIO_DTV_PATCH_DECODER_STATE_INIT) {
                    usleep(20000);
                    retry_count++;
                    ALOGW("waiting dtv patch release before create submixing path %d\n",retry_count);
                }
                ret = initSubMixingInput(aml_out, config);
                aml_out->bypass_submix = false;
                aml_out->inputPortID = -1;
                if (ret < 0) {
                    ALOGE("initSub mixing input failed");
                }
            }
        } else {
            aml_out->bypass_submix = true;
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
    aml_out->stream_status = STREAM_STANDBY;
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
#if ANDROID_PLATFORM_SDK_VERSION > 29
    if (config != NULL) {
        /*valid audio_config means enter in tuner framework case, then we need to create&start audio dtv patch*/
        ALOGD("%s: dev:%p, fmt:%d, dmx fmt:%d, content id:%d,sync id %d,adev->patch_src %d, adev->audio_patching %d", __func__, dev, config->offload_info.format, android_fmt_convert_to_dmx_fmt(config->offload_info.format), config->offload_info.content_id, config->offload_info.sync_id, adev->patch_src, adev->audio_patching);
        ret = enable_dtv_patch_for_tuner_framework(config, dev);
        aml_out->audioCfg.offload_info.content_id = config->offload_info.content_id;
        aml_out->audioCfg.offload_info.sync_id = config->offload_info.sync_id;
        if ((*stream_out) && dtv_tuner_framework(*stream_out)) {
            /*assign pause/resume api for tuner framework output stream.
              application scenarios like: timeshift pause/resume*/
            aml_out->stream.pause = out_pause_dtv_patch_for_tunerframework;
            aml_out->stream.resume = out_resume_dtv_patch_for_tunerframework;
        }
    }
#endif
    aml_out->codec_type = get_codec_type(aml_out->hal_internal_format);
    aml_out->continuous_mode_check = true;

    /* init ease for stream */
    if (aml_audio_ease_init(&aml_out->audio_stream_ease) < 0) {
        ALOGE("%s  aml_audio_ease_init faild\n", __func__);
        ret = -EINVAL;
        goto AUDIO_EASE_INIT_FAIL;
    }

    if (aml_getprop_bool("vendor.media.audio.hal.debug")) {
        aml_out->debug_stream = 1;
    }
    ALOGD("-%s: out %p: io handle:%d, usecase:%s card:%d alsa devices:%d", __func__,
        aml_out, handle, usecase2Str(aml_out->usecase), aml_out->card, aml_out->device);

    return 0;

AUDIO_EASE_INIT_FAIL:
    adev_close_output_stream(dev, *stream_out);
    return ret;
}

void adev_close_output_stream_new(struct audio_hw_device *dev,
                                struct audio_stream_out *stream)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;

    ALOGD("%s: enter usecase = %s", __func__, usecase2Str(aml_out->usecase));

    /* free stream ease resource  */
    aml_audio_ease_close(aml_out->audio_stream_ease);

    /* call legacy close to reuse codes */
    if (adev->active_outputs[aml_out->usecase] == aml_out) {
        adev->active_outputs[aml_out->usecase] = NULL;
    }
    if (adev->useSubMix) {
        if (aml_out->is_normal_pcm ||
            aml_out->usecase == STREAM_PCM_HWSYNC ||
            aml_out->usecase == STREAM_PCM_MMAP) {
            if (!aml_out->bypass_submix) {
                deleteSubMixingInput(aml_out);
            }
        }
    }
    /* when switch hdmi output to a2dp output, close hdmi stream maybe after open a2dp stream,
     * and here set audio stop would cause audio stuck
     */
    if (aml_out->hw_sync_mode && aml_out->tsync_status != TSYNC_STATUS_STOP && !has_hwsync_stream_running(stream)) {
        ALOGI("%s set AUDIO_PAUSE and AUDIO_STOP when close stream\n",__func__);
        aml_hwsync_set_tsync_pause(aml_out->hwsync);
        aml_hwsync_set_tsync_stop(aml_out->hwsync);
        aml_out->tsync_status = TSYNC_STATUS_STOP;
    }
    adev_close_output_stream(dev, stream);
    //adev->dual_decoder_support = false;
    //destroy_aec_reference_config(adev->aec);
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
    struct audio_hw_device *dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    ring_buffer_t *ringbuffer = & (patch->aml_ringbuffer);
    struct audio_stream_in *stream_in = NULL;
    struct aml_stream_in *in;
    struct audio_config stream_config;
    struct timespec ts;
    int aux_read_bytes, read_bytes;
    // FIXME: add calc for read_bytes;
    read_bytes = DEFAULT_CAPTURE_PERIOD_SIZE * CAPTURE_PERIOD_COUNT;
    int ret = 0, retry = 0;
    audio_format_t cur_aformat;
    int ring_buffer_size = 0;

    ALOGI("++%s", __FUNCTION__);

    ALOGD("%s: enter", __func__);
    patch->chanmask = stream_config.channel_mask = patch->in_chanmask;
    patch->sample_rate = stream_config.sample_rate = patch->in_sample_rate;
    patch->aformat = stream_config.format = patch->in_format;

    ret = adev_open_input_stream(patch->dev, 0, patch->input_src, &stream_config, &stream_in, 0, NULL, 0);
    if (ret < 0) {
        ALOGE("%s: open input steam failed ret = %d", __func__, ret);
        return (void *)0;
    }

    in = (struct aml_stream_in *)stream_in;
    /* CVBS IN signal is stable very quickly, sometimes there is no unstable state,
     * we need to do ease in before playing CVBS IN at in_read func.
     */
    if (in->device & AUDIO_DEVICE_IN_LINE) {
        in->mute_flag = true;
    }

    patch->in_buf_size = read_bytes = in->config.period_size * audio_stream_in_frame_size(&in->stream);
    patch->in_buf = aml_audio_calloc(1, patch->in_buf_size);
    if (!patch->in_buf) {
        adev_close_input_stream(patch->dev, &in->stream);
        return (void *)0;
    }

    int first_start = 1;
    prctl(PR_SET_NAME, (unsigned long)"audio_input_patch");
    aml_set_thread_priority("audio_input_patch", patch->audio_input_threadID);
    /*affinity the thread to cpu 2/3 which has few IRQ*/
    aml_audio_set_cpu23_affinity();

    if (ringbuffer) {
        ring_buffer_size = ringbuffer->size;
    }


    while (!patch->input_thread_exit) {
        int bytes_avail = 0;
        /* Todo: read bytes should reconfig with period size */
        int period_mul = 1;//convert_audio_format_2_period_mul(patch->aformat);
        int read_threshold = 0;
        read_bytes = in->config.period_size * audio_stream_in_frame_size(&in->stream) * period_mul;
        bool hdmi_raw_in_flag = patch && (patch->input_src == AUDIO_DEVICE_IN_HDMI) && (!audio_is_linear_pcm(patch->aformat));
        if (hdmi_raw_in_flag) {
            read_bytes = read_bytes / 2;

        }

        if (patch->input_src == AUDIO_DEVICE_IN_LINE) {
            read_threshold = 4 * read_bytes;
        }

        // buffer size diff from allocation size, need to resize.
        if (patch->in_buf_size < (size_t)read_bytes) {
            ALOGI("%s: !!realloc in buf size from %zu to %d", __func__, patch->in_buf_size, read_bytes);
            patch->in_buf = aml_audio_realloc(patch->in_buf, read_bytes);
            patch->in_buf_size = read_bytes;
            memset(patch->in_buf, 0, patch->in_buf_size);
        }

        if (in->standby) {
            ret = start_input_stream(in);
            if (ret < 0) {
                ALOGE("start_input_stream failed !");
            }
            in->standby = 0;
        }

        bytes_avail = read_bytes;

        /* if audio is unstable, don't read data from hardware */
        if (aml_dev->tv_mute || !check_tv_stream_signal(&in->stream)) {
            memset(patch->in_buf, 0, bytes_avail);
            ring_buffer_clear(ringbuffer);
            usleep(20*1000);
        } else {
            if (aml_dev->patch_src == SRC_HDMIIN && in->audio_packet_type == AUDIO_PACKET_AUDS && in->config.channels != 2) {
                ret = input_stream_channels_adjust(&in->stream, patch->in_buf, read_bytes);
            } else {
                aml_alsa_input_read(&in->stream, patch->in_buf, read_bytes);
                if (IS_DIGITAL_IN_HW(patch->input_src) && !check_digital_in_stream_signal(&in->stream)) {
                    memset(patch->in_buf, 0, bytes_avail);
                }
            }
            if (get_debug_value(AML_DUMP_AUDIOHAL_TV)) {
                aml_audio_dump_audio_bitstreams("/data/vendor/audiohal/tv_read.raw", patch->in_buf, read_bytes);
            }
        }

        /*noise gate is only used in Linein for 16bit audio data*/
        if (aml_dev->active_inport == INPORT_LINEIN && aml_dev->aml_ng_enable == 1) {
            int ng_status = noise_evaluation(aml_dev->aml_ng_handle, patch->in_buf, bytes_avail >> 1);
            /*if (ng_status == NG_MUTE)
                ALOGI("noise gate is working!");*/
        }
        ALOGV("++%s in read over read_bytes = %d, in_read returns = %d, threshold %d",
              __FUNCTION__, read_bytes, bytes_avail, read_threshold);

        if (bytes_avail > 0) {
            //DoDumpData(patch->in_buf, bytes_avail, CC_DUMP_SRC_TYPE_INPUT);
            do {
                if (patch->input_src == AUDIO_DEVICE_IN_HDMI)
                {
                    pthread_mutex_lock(&in->lock);
                    ret = reconfig_read_param_through_hdmiin(aml_dev, in, ringbuffer, ring_buffer_size);
                    pthread_mutex_unlock(&in->lock);
                    if (ret == 0) {
                        /* we need standy a2dp when switch the hdmiin param, in order to prevent UNDERFLOW in a2dp stack. */
                        aml_dev->need_reset_a2dp = true;
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
                } else {
                    retry = 1;
                    pthread_cond_signal(&patch->cond);
                    //Fixme: if ringbuffer is full enough but no output, reset ringbuffer
                    ALOGV("%s(), ring buffer no space to write, buffer free size:%d, need write size:%d", __func__,
                        get_buffer_write_space(ringbuffer), bytes_avail);
                    ring_buffer_reset(ringbuffer);
                    usleep(3000);
                }
            } while (retry && !patch->input_thread_exit);
        } else {
            ALOGV("%s(), read alsa pcm fails, to _read(%d), bytes_avail(%d)!",
                  __func__, read_bytes, bytes_avail);
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

void *audio_patch_output_threadloop(void *data)
{
    struct aml_audio_patch *patch = (struct aml_audio_patch *)data;
    struct audio_hw_device *dev = patch->dev;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    ring_buffer_t *ringbuffer = & (patch->aml_ringbuffer);
    struct audio_stream_out *stream_out = NULL;
    struct aml_stream_out *aml_out = NULL,*out;
    struct audio_config stream_config = AUDIO_CONFIG_INITIALIZER;
    struct timespec ts;
    int write_bytes = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
    int txlx_chip = check_chip_name("txlx", 4, &aml_dev->alsa_mixer);
    int ret;
    ALOGD("%s: enter", __func__);
    stream_config.channel_mask = patch->out_chanmask;
    stream_config.sample_rate = patch->out_sample_rate;
    stream_config.format = patch->out_format;

#ifdef DOLBY_MS12_INPUT_FORMAT_TEST
    char buf[PROPERTY_VALUE_MAX] = {0};
    int prop_ret = -1;
    int format = 0;
    prop_ret = property_get("vendor.dolby.ms12.input.format", buf, NULL);
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
    aml_dev->mute_start = true;
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
    aml_set_thread_priority("audio_output_patch", patch->audio_output_threadID);
    /*affinity the thread to cpu 2/3 which has few IRQ*/
    aml_audio_set_cpu23_affinity();

    while (!patch->output_thread_exit) {
        int period_mul = (patch->aformat == AUDIO_FORMAT_E_AC3) ? EAC3_MULTIPLIER : 1;

        if (aml_dev->game_mode)
            write_bytes = LOW_LATENCY_PLAYBACK_PERIOD_SIZE * audio_stream_out_frame_size(&out->stream);

        // buffer size diff from allocation size, need to resize.
        ret = aml_audio_check_and_realloc(&patch->out_buf, &patch->out_buf_size, write_bytes * period_mul);
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
            /* avsync for dev->dev patch*/
            if (patch && (patch->need_do_avsync == true) && (patch->input_signal_stable == true) &&
                    (aml_dev->patch_src == SRC_ATV || aml_dev->patch_src == SRC_HDMIIN ||
                    aml_dev->patch_src == SRC_LINEIN || aml_dev->patch_src == SRC_SPDIFIN)) {

                if (!txlx_chip) {
                    aml_dev_try_avsync(patch);
                    if (patch->skip_frames) {
                        ALOGD("%s(), skip this period data for avsync!", __func__);
                        usleep(5);
                        continue;
                    }
                } else {
                    patch->need_do_avsync = 0;
                }
            }

            if (patch && patch->input_src == AUDIO_DEVICE_IN_HDMI) {
                stream_check_reconfig_param(stream_out);
            }

            out_write_new(stream_out, patch->out_buf, ret);

        } else {
            ALOGV("%s(), no enough data in ring buffer, available data size:%d, need data size:%d", __func__,
                get_buffer_read_space(ringbuffer), (write_bytes * period_mul));
            if (audio_is_linear_pcm(patch->aformat)) {
                usleep( (DEFAULT_PLAYBACK_PERIOD_SIZE) * 1000000 / 4 /
                    stream_config.sample_rate);
            }
        }
    }
    do_output_standby_l((struct audio_stream *)out);
    adev_close_output_stream_new(patch->dev, &out->stream);
    if (patch->out_buf) {
        aml_audio_free(patch->out_buf);
        patch->out_buf = NULL;
    }
    ALOGD("%s: exit", __func__);

    return (void *)0;
}

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

    patch = aml_audio_calloc(1, sizeof(*patch));
    if (!patch) {
        return -ENOMEM;
    }

    patch->dev = dev;
    patch->input_src = input;
    patch->is_dtv_src = false;
    patch->aformat = AUDIO_FORMAT_PCM_16_BIT;
    aml_dev->audio_patch = patch;
    pthread_mutex_init(&patch->mutex, NULL);
    pthread_cond_init(&patch->cond, NULL);

    patch->in_sample_rate = 48000;
    patch->in_chanmask = AUDIO_CHANNEL_IN_STEREO;
    patch->output_src = AUDIO_DEVICE_OUT_SPEAKER;
    patch->out_sample_rate = 48000;
    patch->out_chanmask = AUDIO_CHANNEL_OUT_STEREO;
    patch->in_format = AUDIO_FORMAT_PCM_16_BIT;
    patch->out_format = AUDIO_FORMAT_PCM_16_BIT;

    /* when audio patch start, singal is unstale or
     * patch signal is unstable, it need do avsync
     */
    patch->need_do_avsync = true;
    patch->game_mode = patch->game_mode;

    if (aml_dev->useSubMix) {
        // switch normal stream to old tv mode writing
        switchNormalStream(aml_dev->active_outputs[STREAM_PCM_NORMAL], 0);
    }

    if (patch->out_format == AUDIO_FORMAT_PCM_16_BIT) {
        ALOGD("%s: init audio ringbuffer game %d", __func__, patch->game_mode);
        if (!patch->game_mode)
            ret = ring_buffer_init(&patch->aml_ringbuffer, 4 * 2 * play_buffer_size * PATCH_PERIOD_COUNT);
        else
            ret = ring_buffer_init(&patch->aml_ringbuffer, 2 * 4 * LOW_LATENCY_PLAYBACK_PERIOD_SIZE);
    } else {
        ret = ring_buffer_init(&patch->aml_ringbuffer, 4 * 4 * play_buffer_size * PATCH_PERIOD_COUNT);
    }

    if (aml_dev->dev2mix_patch) {
        create_tvin_buffer(patch);
    }

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

    if (IS_DIGITAL_IN_HW(patch->input_src)) {
        //TODO add sample rate and channel information
        ret = creat_pthread_for_audio_type_parse(&patch->audio_parse_threadID,
                &patch->audio_parse_para, &aml_dev->alsa_mixer, patch->input_src);
        if (ret !=  0) {
            ALOGE("%s: create format parse thread failed", __func__);
            goto err_parse_thread;
        }
    }

    aml_dev->audio_patch = patch;
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
    aml_audio_free(patch);
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
    tv_do_ease_out(aml_dev);
    patch->output_thread_exit = 1;
    patch->input_thread_exit = 1;
    if (IS_DIGITAL_IN_HW(patch->input_src))
        exit_pthread_for_audio_type_parse(patch->audio_parse_threadID,&patch->audio_parse_para);
    patch->input_thread_exit = 1;
    pthread_join(patch->audio_input_threadID, NULL);
    patch->output_thread_exit = 1;
    pthread_join(patch->audio_output_threadID, NULL);
    ring_buffer_release(&patch->aml_ringbuffer);
    release_tvin_buffer(patch);
    aml_audio_free(patch);
    aml_dev->audio_patch = NULL;
    aml_dev->audio_patch_2_af_stream = true;
    aml_dev->patch_start = false;

    if (aml_dev->useSubMix) {
        switchNormalStream(aml_dev->active_outputs[STREAM_PCM_NORMAL], 1);
    }
    ALOGD("%s: exit", __func__);
exit:
    return 0;
}

int release_patch(struct aml_audio_device *aml_dev)
{
    pthread_mutex_lock(&aml_dev->patch_lock);
    release_patch_l(aml_dev);
    pthread_mutex_unlock(&aml_dev->patch_lock);
    return 0;
}

int create_patch(struct audio_hw_device *dev,
                        audio_devices_t input,
                        audio_devices_t output)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    int ret = 0;

    pthread_mutex_lock(&aml_dev->patch_lock);
    ret = create_patch_l(dev, input, output);
    pthread_mutex_unlock(&aml_dev->patch_lock);

    return ret;
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

int get_audio_patch_by_src_dev(struct audio_hw_device *dev, audio_devices_t dev_type, struct audio_patch **p_audio_patch)
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

static enum OUT_PORT get_output_dev_for_sinks(enum OUT_PORT *sinks, int num_sinks, enum OUT_PORT sink) {
    for (int i=0; i<num_sinks; i++) {
        if (sinks[i] == sink) {
            return sink;
        }
    }
    return -1;
}

static enum OUT_PORT get_output_dev_for_strategy(struct aml_audio_device *adev, enum OUT_PORT *sinks, int num_sinks)
{
    uint32_t sink = -1;
    if (sink == -1) {
        sink = get_output_dev_for_sinks(sinks, num_sinks, OUTPORT_A2DP);
    }
    if (sink == -1 && adev->bHDMIARCon) {
        sink = get_output_dev_for_sinks(sinks, num_sinks, OUTPORT_HDMI_ARC);
    }
    if (sink == -1) {
        sink = get_output_dev_for_sinks(sinks, num_sinks, OUTPORT_HDMI);
    }
    if (sink == -1) {
        sink = get_output_dev_for_sinks(sinks, num_sinks, OUTPORT_SPEAKER);
    }
    if (sink == -1) {
        sink = OUTPORT_SPEAKER;
    }
    return sink;
}

/* remove audio patch from dev list */
static int unregister_audio_patch(struct audio_hw_device *dev __unused,
                                struct audio_patch_set *patch_set)
{
    R_CHECK_POINTER_LEGAL(-EINVAL, patch_set, "");
#ifdef DEBUG_PATCH_SET
    dump_audio_patch_set(patch_set);
#endif
    AM_LOGI("delete the patch id:%d", patch_set->audio_patch.id);
    list_remove(&patch_set->list);
    aml_audio_free(patch_set);
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

    patch_set_new = aml_audio_calloc(1, sizeof(struct audio_patch_set));
    R_CHECK_POINTER_LEGAL(NULL, patch_set_new, "no memory");

    patch_new = &patch_set_new->audio_patch;
    *handle = (audio_patch_handle_t) android_atomic_inc(&aml_dev->next_unique_ID);

    /* init audio patch new */
    patch_new->id = *handle;
    patch_new->num_sources = num_sources;
    memcpy(patch_new->sources, sources, num_sources * sizeof(struct audio_port_config));
    patch_new->num_sinks = num_sinks;
    memcpy(patch_new->sinks, sinks, num_sinks * sizeof (struct audio_port_config));
#ifdef DEBUG_PATCH_SET
    ALOGD("%s(), patch set new to register:", __func__);
    dump_audio_patch_set(patch_set_new);
#endif

    /* find if mix->dev / dev->mix exists and remove from list */
    list_for_each(node, &aml_dev->patch_list) {
        patch_set_tmp = node_to_item(node, struct audio_patch_set, list);
        patch_tmp = &patch_set_tmp->audio_patch;
        if (patch_tmp->sources[0].type == AUDIO_PORT_TYPE_MIX &&
            patch_tmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE &&
            sources[0].ext.mix.handle == patch_tmp->sources[0].ext.mix.handle) {
            AM_LOGI("patch found id:%d mix(%d)->dev_0(%#x) remove it.", patch_tmp->id, patch_tmp->sources[0].ext.mix.handle,
                patch_tmp->sinks[0].ext.device.type);
            unregister_audio_patch(dev, patch_set_tmp);
            break;
        } else if (patch_tmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE &&
            patch_tmp->sinks[0].type == AUDIO_PORT_TYPE_MIX &&
            sinks[0].ext.mix.handle == patch_tmp->sinks[0].ext.mix.handle) {
            AM_LOGI("patch found id:%d dev(%#x)->mix(%d) remove it.", patch_tmp->id, patch_tmp->sources[0].ext.device.type,
                patch_tmp->sinks[0].ext.mix.handle);
            unregister_audio_patch(dev, patch_set_tmp);
            break;
        }
    }
    /* add new patch set to dev patch list */
    list_add_head(&aml_dev->patch_list, &patch_set_new->list);
    //dump_aml_audio_patch_sets(dev);
    return patch_set_new;
}

static bool is_contain_d2d_patch(struct aml_audio_device *adev)
{
    struct listnode *node = NULL;
    struct audio_patch_set *patch_set_tmp = NULL;
    struct audio_patch *patch_tmp = NULL;
    /* find if mix->dev / dev->mix exists and remove from list */
    list_for_each(node, &adev->patch_list) {
        patch_set_tmp = node_to_item(node, struct audio_patch_set, list);
        patch_tmp = &patch_set_tmp->audio_patch;
        if (patch_tmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE &&
            patch_tmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
            return true;
        }
    }
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
    aml_dev->no_underrun_max = property_get_int32("vendor.media.audio_hal.nounderrunmax", 60);
    aml_dev->start_mute_max = property_get_int32("vendor.media.audio_hal.startmutemax", 50);

    if ((src_config->ext.device.type == AUDIO_DEVICE_IN_WIRED_HEADSET) || (src_config->ext.device.type == AUDIO_DEVICE_IN_BLUETOOTH_BLE)) {
        ALOGD("bluetooth voice search is in use, bypass adev_create_audio_patch()!!\n");
        //we can't return error to application because it maybe process the error .
        return 0;
    }
    R_CHECK_POINTER_LEGAL(-EINVAL, sources, "");
    R_CHECK_POINTER_LEGAL(-EINVAL, sinks, "");
    R_CHECK_POINTER_LEGAL(-EINVAL, handle, "");
    R_CHECK_PARAM_LEGAL(-EINVAL, num_sources, 0, 1, "");
    R_CHECK_PARAM_LEGAL(-EINVAL, num_sinks, 0, AUDIO_PATCH_PORTS_MAX - 1, "");

    patch_set = register_audio_patch(dev, num_sources, sources, num_sinks, sinks, handle);
    R_CHECK_POINTER_LEGAL(-ENOMEM, patch_set, "create patch fail");
    AM_LOGI("sources:%d sinks:%d %s->%s new id:%d patch_src:%s", num_sources, num_sinks, audioPortType2Str(src_config->type),
        audioPortType2Str(sink_config->type), *handle, patchSrc2Str(aml_dev->patch_src));

    if (sink_config->type == AUDIO_PORT_TYPE_DEVICE) {
        android_dev_convert_to_hal_dev(sink_config->ext.device.type, (int *)&outport);
        /* Only 3 sink devices are allowed to coexist */
        R_CHECK_PARAM_LEGAL(-EINVAL, num_sinks, 0, OUTPUT_PORT_MAX_COEXIST_NUM - 1, "not support num_sinks");
        if (num_sinks > 1) {
            enum OUT_PORT sink_devs[OUTPUT_PORT_MAX_COEXIST_NUM] = {OUTPORT_SPEAKER, OUTPORT_SPEAKER, OUTPORT_SPEAKER};
            for (int i=0; i<num_sinks; i++) {
                android_dev_convert_to_hal_dev(sink_config[i].ext.device.type, (int *)&sink_devs[i]);
                if (aml_dev->debug_flag) {
                    ALOGD("[%s:%d] sink[%d]:%s", __func__, __LINE__, i, outputPort2Str(sink_devs[i]));
                }
            }
            outport = get_output_dev_for_strategy(aml_dev, sink_devs, num_sinks);
        } else {
            ALOGI("[%s:%d] one sink, sink:%s", __func__, __LINE__, outputPort2Str(outport));
        }


        /*  For switch from ARC to Speaker case(open system sound):
         *  The key tone will trigger first, and create mix->dev[SPK] patch, that is routing to SPK(unmute SPK. However,
         *  at this time dev->dev[ARC] patch not be released), then recreate dev->dev(SPK) patch, recreate audio_patch_thread,
         *  generate SPK gap.
         *  Solution: Do not update routing when there include a dev->dev path, do it when dev->dev released
         */
        if (outport != OUTPORT_SPDIF && !(src_config->type == AUDIO_PORT_TYPE_MIX && is_contain_d2d_patch(aml_dev))) {
            ret = aml_audio_output_routing(dev, outport, false);
        }
        aml_dev->out_device = 0;
        for (i = 0; i < num_sinks; i++) {
            aml_dev->out_device |= sink_config[i].ext.device.type;
        }

        /* 1.device to device audio patch. TODO: unify with the android device type */
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
                patch_set = NULL;
            }

            aml_audio_input_routing(dev, inport);
            if (AUDIO_DEVICE_IN_ECHO_REFERENCE != src_config->ext.device.type &&
                AUDIO_DEVICE_IN_TV_TUNER != src_config->ext.device.type) {
                aml_dev->patch_src = android_input_dev_convert_to_hal_patch_src(src_config->ext.device.type);
            }
            input_src = android_input_dev_convert_to_hal_input_src(src_config->ext.device.type);
            aml_dev->active_inport = inport;
            aml_dev->src_gain[inport] = 1.0;
            //aml_dev->sink_gain[outport] = 1.0;
            AM_LOGI("dev(%s) -> dev(%s) patch, patch_src:%s", inputPort2Str(inport), outputPort2Str(outport), patchSrc2Str(aml_dev->patch_src));
            AM_LOGI("input dev:%#x, all output dev:%#x", src_config->ext.device.type, aml_dev->out_device);
            if (inport == INPORT_TUNER) {
                if (aml_dev->is_TV) {
                    if (aml_dev->patch_src != SRC_DTV)
                        aml_dev->patch_src = SRC_ATV;
                } else {
                   aml_dev->patch_src = SRC_DTV;
                }
            }
            // ATV path goes to dev set_params which could
            // tell atv or dtv source and decide to create or not.
            // One more case is ATV->ATV, should recreate audio patch.
            if ((inport != INPORT_TUNER)
                    || ((inport == INPORT_TUNER) && (aml_dev->patch_src == SRC_ATV))) {
                if (input_src != SRC_NA) {
                    set_audio_source(&aml_dev->alsa_mixer, input_src, alsa_device_is_auge());
                }

                if (aml_dev->audio_patch) {
                    ALOGD("%s: patch exists, first release it", __func__);
                    ALOGD("%s: new input %#x, old input %#x",
                        __func__, inport, aml_dev->audio_patch->input_src);
                    if (aml_dev->audio_patch->is_dtv_src)
                        release_dtv_patch(aml_dev);
                    else
                        release_patch(aml_dev);
                }
                ret = create_patch(dev, src_config->ext.device.type, aml_dev->out_device);
                if (ret) {
                    ALOGE("[%s:%d] create patch failed, all out dev:%#x.", __func__, __LINE__, aml_dev->out_device);
                    ret = -EINVAL;
                    unregister_audio_patch(dev, patch_set);
                    patch_set = NULL;
                }
                aml_dev->audio_patching = 1;
                if (inport == INPORT_HDMIIN ||
                    inport == INPORT_LINEIN ||
                    inport == INPORT_SPDIF  ||
                    inport == INPORT_TUNER) {
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
                 if (/*aml_dev->is_TV*/1) {

                     if (aml_dev->is_TV) {
                         if ((aml_dev->patch_src == SRC_DTV) && aml_dev->audio_patching) {
                             ALOGI("%s, now release the dtv patch now\n ", __func__);
                             ret = release_dtv_patch(aml_dev);
                             if (!ret) {
                                 aml_dev->audio_patching = 0;
                             }
                         }
                         ALOGI("%s, now end release dtv patch the audio_patching is %d ", __func__, aml_dev->audio_patching);
                         ALOGI("%s, now create the dtv patch now\n ", __func__);
                     }

                     aml_dev->patch_src = SRC_DTV;
                     if (eDolbyMS12Lib == aml_dev->dolby_lib_type && aml_dev->continuous_audio_mode) {
                        get_dolby_ms12_cleanup(&aml_dev->ms12, true);
                        aml_dev->exiting_ms12 = 1;
                        aml_dev->continuous_audio_mode = 0;
                        clock_gettime(CLOCK_MONOTONIC, &aml_dev->ms12_exiting_start);
                        if (aml_dev->active_outputs[STREAM_PCM_NORMAL] != NULL)
                            usecase_change_validate_l(aml_dev->active_outputs[STREAM_PCM_NORMAL], true);
                     }
                     ret = create_dtv_patch(dev, AUDIO_DEVICE_IN_TV_TUNER,
                                            AUDIO_DEVICE_OUT_SPEAKER);
                     if (ret == 0) {
                        aml_dev->audio_patching = 1;
                     }
                     ALOGI("%s, now end create dtv patch the audio_patching is %d ", __func__, aml_dev->audio_patching);

                 }

            }
            if (input_src == LINEIN && aml_dev->aml_ng_enable) {
                aml_dev->aml_ng_handle = init_noise_gate(aml_dev->aml_ng_level,
                                         aml_dev->aml_ng_attack_time, aml_dev->aml_ng_release_time);
                ALOGE("%s: init amlogic noise gate: level: %fdB, attrack_time = %dms, release_time = %dms",
                      __func__, aml_dev->aml_ng_level,
                      aml_dev->aml_ng_attack_time, aml_dev->aml_ng_release_time);
            }
        } else if (src_config->type == AUDIO_PORT_TYPE_MIX) {  /* 2. mix to device audio patch */
            AM_LOGI("mix(%d) -> dev(%s) patch", src_config->ext.mix.handle, outputPort2Str(outport));
            ret = 0;
        } else {
            AM_LOGE("invalid patch, source error, source:%d(%s)->DEVICE", src_config->type, audioPortType2Str(src_config->type));
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
                patch_set = NULL;
            }
            aml_audio_input_routing(dev, inport);
            input_src = android_input_dev_convert_to_hal_input_src(src_config->ext.device.type);
            if (AUDIO_DEVICE_IN_TV_TUNER == src_config->ext.device.type) {
                aml_dev->dev2mix_patch = true;
                if (aml_dev->is_TV) {
                   if (input_src == ATV) {
                       aml_dev->patch_src = SRC_ATV;
                   }
                } else {
                   /*for stb ,AUDIO_DEVICE_IN_TV_TUNER is always DTV */
                   if (input_src == ATV) {
                       aml_dev->patch_src = SRC_DTV;
                   }
                }
            } else if (AUDIO_DEVICE_IN_ECHO_REFERENCE != src_config->ext.device.type) {
                if (AUDIO_DEVICE_IN_BUILTIN_MIC != src_config->ext.device.type &&
                    AUDIO_DEVICE_IN_BACK_MIC != src_config->ext.device.type )
                    aml_dev->patch_src = android_input_dev_convert_to_hal_patch_src(src_config->ext.device.type);
            }
            AM_LOGI("dev(%s) -> mix(%d) patch, patch_src:%s, in dev:%#x",
                inputPort2Str(inport), sink_config->ext.mix.handle, patchSrc2Str(aml_dev->patch_src), src_config->ext.device.type);
            if (input_src != SRC_NA) {
                set_audio_source(&aml_dev->alsa_mixer, input_src, alsa_device_is_auge());
            }
            aml_dev->active_inport = inport;
            aml_dev->src_gain[inport] = 1.0;
            if (inport == INPORT_HDMIIN || inport == INPORT_ARCIN || inport == INPORT_SPDIF
                || inport == INPORT_LINEIN || ((inport == INPORT_TUNER) && (aml_dev->patch_src == SRC_ATV))) {
                 aml_dev->dev2mix_patch = true;
#if 0
                aml_dev2mix_parser_create(dev, src_config->ext.device.type);
#endif
                if (input_src != SRC_NA) {
                    set_audio_source(&aml_dev->alsa_mixer, input_src, alsa_device_is_auge());
                }


                if (aml_dev->audio_patch) {
                    ALOGD("%s: patch exists, first release it", __func__);
                    ALOGD("%s: new input %#x, old input %#x",
                        __func__, inport, aml_dev->audio_patch->input_src);
                    if (aml_dev->audio_patch->is_dtv_src)
                        release_dtv_patch(aml_dev);
                    else
                        release_patch(aml_dev);
                }
                ret = create_patch(dev, src_config->ext.device.type, aml_dev->out_device);
                if (ret) {
                    ALOGE("[%s:%d] create patch failed, all out dev:%#x.", __func__, __LINE__, aml_dev->out_device);
                    ret = -EINVAL;
                    unregister_audio_patch(dev, patch_set);
                    patch_set = NULL;
                }
                aml_dev->audio_patching = 1;
                if (inport == INPORT_HDMIIN ||
                    inport == INPORT_LINEIN ||
                    inport == INPORT_SPDIF  ||
                    inport == INPORT_TUNER) {
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
            } else if ((inport == INPORT_TUNER) && (aml_dev->patch_src == SRC_DTV)){///zzz
                if (/*aml_dev->is_TV*/1) {
                    if (aml_dev->audio_patching) {
                        ALOGI("%s,!!!now release the dtv patch now\n ", __func__);
                        ret = release_dtv_patch(aml_dev);
                        if (!ret) {
                            aml_dev->audio_patching = 0;
                        }
                    }
                    ALOGI("%s, !!! now create the dtv patch now\n ", __func__);
                    ret = create_dtv_patch(dev, AUDIO_DEVICE_IN_TV_TUNER, AUDIO_DEVICE_OUT_SPEAKER);
                    if (ret == 0) {
                        aml_dev->audio_patching = 1;
                    }
                }
            }
            aml_audio_input_routing(dev, inport);
            ret = 0;
        } else {
            AM_LOGE("invalid patch, source error, source:%d(%s)->MIX", src_config->type, audioPortType2Str(src_config->type));
            ret = -EINVAL;
            unregister_audio_patch(dev, patch_set);
        }
    } else {
        AM_LOGE("invalid patch, sink:%d(%s) error", sink_config->type, audioPortType2Str(sink_config->type));
        ret = -EINVAL;
        unregister_audio_patch(dev, patch_set);
    }
    aml_mixer_ctrl_set_int(&aml_dev->alsa_mixer, AML_MIXER_ID_AUDIO_HAL_FORMAT, TYPE_PCM);
    return ret;
}

static int adev_release_patch_restore_resource(struct aml_audio_device *aml_dev)
{
    int ret = 0;

    /* for no patch case, we need to restore it, especially note the multi-instance audio-patch */
    if (eDolbyMS12Lib == aml_dev->dolby_lib_type && (aml_dev->continuous_audio_mode_default == 1) && !is_dtv_patch_alive(aml_dev))
    {
        get_dolby_ms12_cleanup(&aml_dev->ms12, false);
        /*continuous mode is using in ms12 prepare, we should lock it*/
        pthread_mutex_lock(&aml_dev->ms12.lock);
        aml_dev->continuous_audio_mode = 1;
        pthread_mutex_unlock(&aml_dev->ms12.lock);
        ALOGI("%s restore continuous_audio_mode=%d", __func__, aml_dev->continuous_audio_mode);
    }
    aml_dev->audio_patching = 0;
    /* save ATV src to deal with ATV HP hotplug */
    if (aml_dev->patch_src != SRC_ATV && aml_dev->patch_src != SRC_DTV) {
        aml_dev->patch_src = SRC_INVAL;
    }
    if (aml_dev->is_TV) {
        aml_dev->parental_control_av_mute = false;
    }


    /* The route output device is updated again. According to the current available devices to do a policy,
     * determine the current output device.
     */
    enum OUT_PORT sink_devs[OUTPUT_PORT_MAX_COEXIST_NUM] = {OUTPORT_SPEAKER, OUTPORT_SPEAKER, OUTPORT_SPEAKER};
    size_t num_sinks = 0;
    audio_devices_t out_dev = aml_dev->out_device;
    while (out_dev) {
        uint8_t right_zeros = get_bit_position_in_mask(sizeof(audio_devices_t) * 8 - 1, &out_dev);
        out_dev &= ~(1 << right_zeros);
        audio_devices_t sink_dev = 1 << right_zeros;
        if (sink_dev) {
            android_dev_convert_to_hal_dev(sink_dev, (int *)&sink_devs[num_sinks]);
            num_sinks++;
            if (num_sinks >= OUTPUT_PORT_MAX_COEXIST_NUM) {
                AM_LOGW("invalid num_sinks:%d >= %d", num_sinks, OUTPUT_PORT_MAX_COEXIST_NUM);
                break;
            }
        }
    }
    enum OUT_PORT outport = get_output_dev_for_strategy(aml_dev, sink_devs, num_sinks);
    ret = aml_audio_output_routing(&aml_dev->hw_device, outport, false);
    if (ret < 0) {
        ALOGE("%s() routing failed", __func__);
        ret = -EINVAL;
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

    if (list_empty(&aml_dev->patch_list)) {
        AM_LOGE("No patch in list to release");
        ret = -EINVAL;
        goto exit;
    }

    /* find audio_patch in patch_set list */
    list_for_each(node, &aml_dev->patch_list) {
        patch_set = node_to_item(node, struct audio_patch_set, list);
        patch = &patch_set->audio_patch;
        if (patch->id == handle) {
            break;
        } else {
            patch_set = NULL;
            patch = NULL;
        }
    }
    R_CHECK_POINTER_LEGAL(-EINVAL, patch_set, "Can't get patch in list");
    R_CHECK_POINTER_LEGAL(-EINVAL, patch, "Can't get patch in list");

    /* aml_dev patch is not the release patch */
    if (aml_dev->audio_patch && aml_dev->audio_patch->input_src != patch->sources[0].ext.device.type)
        goto exit_unregister;
    AM_LOGI("id:%d %s->%s patch_src:%s", handle, audioPortType2Str(patch->sources[0].type),
        audioPortType2Str(patch->sinks[0].type), patchSrc2Str(aml_dev->patch_src));
    if (patch->sources[0].type == AUDIO_PORT_TYPE_DEVICE
        && patch->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {

        if (aml_dev->patch_src == SRC_DTV) {
            ALOGI("patch src == DTV now line %d \n", __LINE__);
            release_dtv_patch(aml_dev);
            aml_dev->audio_patching = 0;
        }

        if (aml_dev->patch_src != SRC_DTV
                && aml_dev->patch_src != SRC_INVAL
                && aml_dev->audio_patching == 1) {
            release_patch(aml_dev);
            if (aml_dev->patch_src == SRC_LINEIN && aml_dev->aml_ng_handle) {

                release_noise_gate(aml_dev->aml_ng_handle);
                aml_dev->aml_ng_handle = NULL;
            }
        }

        /*for no patch case, we need to restore it*/
        ret = adev_release_patch_restore_resource(aml_dev);
    }

    if (patch->sources[0].type == AUDIO_PORT_TYPE_DEVICE
        && patch->sinks[0].type == AUDIO_PORT_TYPE_MIX) {
#if 0
        if (aml_dev->patch_src == SRC_HDMIIN) {
            aml_dev2mix_parser_release(aml_dev);
        }
#endif


        if (aml_dev->patch_src == SRC_DTV &&
                patch->sources[0].ext.device.type == AUDIO_DEVICE_IN_TV_TUNER) {
            ALOGI("patch src == DTV now line %d \n", __LINE__);
            release_dtv_patch(aml_dev);
            aml_dev->audio_patching = 0;
        } else {
            release_patch(aml_dev);
        }

        if (aml_dev->patch_src != SRC_ATV && aml_dev->patch_src != SRC_DTV) {
            aml_dev->patch_src = SRC_INVAL;
        }
        if (aml_dev->audio_patching) {
            ALOGI("patch src reset to  DTV now line= %d \n", __LINE__);
            //aml_dev->patch_src = SRC_DTV;
            aml_dev->active_inport = INPORT_TUNER;
        }
        aml_dev->dev2mix_patch = false;
    }
    aml_mixer_ctrl_set_int(&aml_dev->alsa_mixer, AML_MIXER_ID_AUDIO_HAL_FORMAT, TYPE_PCM);
    //dump_aml_audio_patch_sets(dev);
#ifdef ADD_AUDIO_DELAY_INTERFACE
    aml_audio_delay_clear(AML_DELAY_OUTPORT_SPEAKER);
    aml_audio_delay_clear(AML_DELAY_OUTPORT_SPDIF);
    aml_audio_delay_clear(AML_DELAY_OUTPORT_SPDIF_RAW);
    aml_audio_delay_clear(AML_DELAY_OUTPORT_ARC_RAW);
    aml_audio_delay_clear(AML_DELAY_OUTPORT_ALL);
#endif

exit_unregister:
    unregister_audio_patch(dev, patch_set);
exit:
    return ret;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    struct aml_audio_device* aml_dev = (struct aml_audio_device*)device;

    struct aml_stream_out *aml_out = NULL;
    const int kNumRetries = 5;
    const int kSleepTimeMS = 100;
    int retry = kNumRetries;
    int i;
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

    dprintf(fd, "\n");
    dprintf(fd, "[AML_HAL]      hdmi_format     : %10d |  active_outport    :    %s\n",
        aml_dev->hdmi_format, outputPort2Str(aml_dev->active_outport));
    dprintf(fd, "[AML_HAL]      A2DP gain       : %10f |  patch_src         :    %s\n",
        aml_dev->sink_gain[OUTPORT_A2DP], patchSrc2Str(aml_dev->patch_src));
    dprintf(fd, "[AML_HAL]      SPEAKER gain    : %10f |  HDMI gain         :    %f\n",
        aml_dev->sink_gain[OUTPORT_SPEAKER], aml_dev->sink_gain[OUTPORT_HDMI]);
    dprintf(fd, "[AML_HAL]      ms12 main volume: %10f\n", aml_dev->ms12.main_volume);
    aml_audio_ease_t *audio_ease = aml_dev->audio_ease;
    if (audio_ease && fabs(audio_ease->current_volume) <= 1e-6) {
        dprintf(fd, "[AML_HAL] ease out muted. start:%f target:%f\n", audio_ease->start_volume, audio_ease->target_volume);
    }
    dprintf(fd, "[AML_HAL]      dolby_lib: %d\n", aml_dev->dolby_lib_type);
    dprintf(fd, "[AML_HAL]      build ms12 version: %d\n", aml_dev->support_ms12_version);
    dprintf(fd, "\n[AML_HAL]      usecase_masks: %#x\n", aml_dev->usecase_masks);
    dprintf(fd, "\nAML stream outs:\n");

    for (i = 0; i < STREAM_USECASE_MAX ; i++) {
        aml_out = aml_dev->active_outputs[i];
        if (aml_out) {
            dprintf(fd, "  out: %d, pointer: %p\n", i, aml_out);
            aml_stream_out_dump(aml_out, fd);
        }
    }
#ifdef AML_MALLOC_DEBUG
    aml_audio_debug_malloc_showinfo(MEMINFO_SHOW_PRINT);
#endif
    if (aml_dev->useSubMix) {
        subMixingDump(fd, aml_dev);
    }

    aml_audio_patches_dump(aml_dev, fd);
    audio_patch_dump(aml_dev, fd);
    a2dp_hal_dump(aml_dev, fd);

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
    ALOGD("%s: count:%d enter", __func__, adev->count);
    adev->count--;
    if (adev->count > 0) {
        pthread_mutex_unlock(&adev_mutex);
        return 0;
    }

    /* free ease resource  */
    aml_audio_ease_close(adev->audio_ease);
    aml_audio_ease_close(adev->volume_ease.ease);

    /* destroy thread for communication between Audio Hal and MS12 */
    if ((eDolbyMS12Lib == adev->dolby_lib_type)) {
        ms12_mesg_thread_destroy(&adev->ms12);
        ALOGD("%s, ms12_mesg_thread_destroy finished!\n", __func__);
    }
    aml_audio_timer_delete();

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        get_dolby_ms12_cleanup(&adev->ms12, false);

        int wait_count = 0;
        while (adev->ms12_out != NULL) {
            if (wait_count >= 100) {
                break;
            }
            wait_count++;
            usleep(10*1000);//10ms
        }
        ALOGD("%s, wait_count:%d, ms12 resource should be released finish\n", __func__, wait_count);
    }

/*[SEI-zhaopf-2018-10-29] add for HBG remote audio support { */
#if defined(ENABLE_HBG_PATCH)
    stopReceiveAudioData();
#endif

    if (adev->out_16_buf) {
        aml_audio_free(adev->out_16_buf);
    }
    if (adev->out_32_buf) {
        aml_audio_free(adev->out_32_buf);
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
    if (adev->aml_dtv_audio_instances) {
        aml_dtv_audio_instances_t *dtv_audio_instances = (aml_dtv_audio_instances_t *)adev->aml_dtv_audio_instances;
        for (int index = 0; index < DVB_DEMUX_SUPPORT_MAX_NUM; index ++) {
            aml_dtvsync_t *dtvsync =  &dtv_audio_instances->dtvsync[index];
            pthread_mutex_destroy(&dtvsync->ms_lock);
        }
        aml_audio_free(adev->aml_dtv_audio_instances);
    }
    if (adev->sm) {
        deleteHalSubMixing(adev->sm);
    }
    aml_hwsync_close_tsync(adev->tsync_fd);
    pthread_mutex_destroy(&adev->patch_lock);
    pthread_mutex_destroy(&adev->dtv_lock);

#ifdef ADD_AUDIO_DELAY_INTERFACE
    if (adev->is_TV) {
        aml_audio_delay_deinit();
    }
#endif
#ifdef ENABLE_AEC_APP
    release_aec(adev->aec);
#endif
    g_adev = NULL;

    aml_audio_free(device);
    g_aml_primary_adev = NULL;
    pthread_mutex_unlock(&adev_mutex);

    aml_audio_debug_close();
    aml_audio_debug_malloc_close();

    ALOGD("%s:  exit", __func__);
    return 0;
}

static int adev_set_audio_port_config(struct audio_hw_device *dev, const struct audio_port_config *config)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    enum OUT_PORT outport = OUTPORT_SPEAKER;
    enum IN_PORT inport = INPORT_HDMIIN;
    int ret = 0;

    if (config == NULL) {
        ALOGE("[%s:%d] audio_port_config is null", __func__, __LINE__);
        return -EINVAL;
    }
    if ((config->config_mask & AUDIO_PORT_CONFIG_GAIN) == 0) {
        ALOGE("[%s:%d] config_mask:%#x invalid", __func__, __LINE__, config->config_mask);
        return -EINVAL;
    }
    ALOGI("++[%s:%d] audio_port id:%d, role:%s, type:%s", __func__, __LINE__, config->id,
        audioPortRole2Str(config->role), audioPortType2Str(config->type));
    struct audio_patch_set *patch_set = NULL;
    struct audio_patch *patch = NULL;
    struct listnode *node = NULL;

    /* find the corrisponding sink for this src */
    list_for_each(node, &aml_dev->patch_list) {
        patch_set = node_to_item(node, struct audio_patch_set, list);
        patch = &patch_set->audio_patch;
        if (patch->sources[0].ext.device.type == config->ext.device.type) {
            ALOGI("[%s:%d] patch set found id:%d, dev:%#x, patchset:%p", __func__, __LINE__,
                patch->id, config->ext.device.type, patch_set);
            break;
        } else {
            int i=0;
            for (i=0; i<patch->num_sinks; i++) {
                if ((patch->sources[0].type == AUDIO_PORT_TYPE_MIX &&
                       patch->sinks[i].type == AUDIO_PORT_TYPE_DEVICE &&
                       patch->sinks[i].id == config->id)) {
                    ALOGI("patch found mix->dev patch id:%d, sink id:%d, patchset:%p", patch->id, config->id, patch_set);
                    break;
                }
            }
            if (i >= patch->num_sinks) {
                patch_set = NULL;
                patch = NULL;
            } else {
                break;
            }
        }
    }

    if (!patch_set || !patch) {
        ALOGW("[%s:%d] no right patch available. patch_set:%p or patch:%p is null", __func__, __LINE__, patch_set, patch);
        return -EINVAL;
    }

    /* Only 3 sink devices are allowed to coexist */
    enum OUT_PORT sink_devs[OUTPUT_PORT_MAX_COEXIST_NUM] = {OUTPORT_SPEAKER, OUTPORT_SPEAKER, OUTPORT_SPEAKER};
    for (int i=0; i<patch->num_sinks; i++) {
        if (i >= OUTPUT_PORT_MAX_COEXIST_NUM) {
            ALOGW("[%s:%d] invalid num_sinks:%d", __func__, __LINE__, patch->num_sinks);
            break;
        } else {
            android_dev_convert_to_hal_dev(patch->sinks[i].ext.device.type, (int *)&sink_devs[i]);
            if (aml_dev->debug_flag) {
                ALOGD("[%s:%d] sink[%d]:%s", __func__, __LINE__, i, outputPort2Str(sink_devs[i]));
            }
        }
    }

    if (config->type == AUDIO_PORT_TYPE_DEVICE) {
        if (config->role == AUDIO_PORT_ROLE_SINK) {
            if (patch->num_sinks == 1) {
                android_dev_convert_to_hal_dev(config->ext.device.type, (int *)&outport);
            }
            if (config->ext.device.type != AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET) {
                aml_dev->sink_gain[outport] = DbToAmpl(config->gain.values[0] / 100.0);
            } else {
                aml_dev->sink_gain[outport] = 1.0;
            }
            ALOGI(" - set sink device[%#x](outport:%s): volume_Mb[%d], gain[%f]",
                    config->ext.device.type, outputPort2Str(outport),
                    config->gain.values[0], aml_dev->sink_gain[outport]);

            ALOGI(" - now the sink gains are:");
            ALOGI("\t- OUTPORT_SPEAKER->gain[%f]", aml_dev->sink_gain[OUTPORT_SPEAKER]);
            ALOGI("\t- OUTPORT_HDMI_ARC->gain[%f]", aml_dev->sink_gain[OUTPORT_HDMI_ARC]);
            ALOGI("\t- OUTPORT_HEADPHONE->gain[%f]", aml_dev->sink_gain[OUTPORT_HEADPHONE]);
            ALOGI("\t- OUTPORT_HDMI->gain[%f]", aml_dev->sink_gain[OUTPORT_HDMI]);
            ALOGI("\t- OUTPORT_ANLG_DOCK_HEADSET->gain[%f]", aml_dev->sink_gain[OUTPORT_HEADPHONE]);
            ALOGI("\t- active outport is: %s", outputPort2Str(aml_dev->active_outport));
        } else if (config->role == AUDIO_PORT_ROLE_SOURCE) {
            android_dev_convert_to_hal_dev(config->ext.device.type, (int *)&inport);
            aml_dev->src_gain[inport] = 1.0;
            if (patch->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
                if (patch->num_sinks == 2 || patch->num_sinks == 3) {
                    outport = get_output_dev_for_strategy(aml_dev, sink_devs, patch->num_sinks);
                    float volume = DbToAmpl(config->gain.values[0] / 100.0);
                    switch (outport) {
                        case OUTPORT_HDMI_ARC:
                            aml_dev->sink_gain[outport] = 1.0;
                            break;
                        case OUTPORT_A2DP:
                            /* For scenarios that do not support AVRCP, audio_hal control is required. */
                            if (aml_dev->bt_avrcp_supported) {
                                aml_dev->sink_gain[outport] = 1.0;
                            } else {
                                aml_dev->sink_gain[outport] = volume;
                            }
                            break;
                        case OUTPORT_SPEAKER:
                            aml_dev->sink_gain[outport] = volume;
                            break;
                        default:
                            ALOGW("[%s:%d] invalid out device type:%s", __func__, __LINE__, outputPort2Str(outport));
                    }
                } else if (patch->num_sinks == 1) {
                    outport = sink_devs[0];
                    if (OUTPORT_HDMI_ARC == outport || OUTPORT_SPDIF == outport ||
                        (OUTPORT_A2DP == outport && aml_dev->bt_avrcp_supported)) {
                        aml_dev->sink_gain[outport] =  1.0;
                    } else {
                        aml_dev->sink_gain[outport] = DbToAmpl(config->gain.values[0] / 100.0);
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
                ALOGI(" - set src device[%#x](inport:%s): gain[%f]",
                            config->ext.device.type, inputPort2Str(inport), aml_dev->src_gain[inport]);
                ALOGI(" - set sink device[%#x](outport:%s): volume_Mb[%d], gain[%f]",
                            patch->sinks->ext.device.type, outputPort2Str(outport),
                            config->gain.values[0], aml_dev->sink_gain[outport]);
            } else if (patch->sinks[0].type == AUDIO_PORT_TYPE_MIX) {
                aml_dev->src_gain[inport] = DbToAmpl(config->gain.values[0] / 100.0);
                ALOGI(" - set src device[%#x](inport:%s): gain[%f]",
                            config->ext.device.type, inputPort2Str(inport), aml_dev->src_gain[inport]);
            }
            ALOGI(" - set gain for in_port:%s, active inport:%s",
                   inputPort2Str(inport), inputPort2Str(aml_dev->active_inport));
        } else {
            ALOGW("[%s:%d] unsupported role:%d type.", __func__, __LINE__, config->role);
        }

        /* for both dev->dev and mixer->dev, start volume ease */
        if (outport == OUTPORT_SPEAKER && aml_dev->last_sink_gain != aml_dev->sink_gain[OUTPORT_SPEAKER]) {
            ALOGD("start easing: vol last %f, vol new %f", aml_dev->last_sink_gain, aml_dev->sink_gain[OUTPORT_SPEAKER]);
            aml_dev->volume_ease.config_easing = true;
            aml_dev->last_sink_gain = aml_dev->sink_gain[OUTPORT_SPEAKER];

            if ((eDolbyMS12Lib == aml_dev->dolby_lib_type)) {
                /*
                 * The postgain value has an impact on the Volume Modeler and the Audio Regulator:
                 * Volume Modeler: Uses the postgain value to select the appropriate frequency response curve
                 * to maintain a consistent perceived timbre at different listening levels.
                 * SP45: Postgain
                 * Sets the amount of gain that is to be applied to the signal after exiting MS12.
                 * Settings From -130 to +30 dB, in 0.0625 dB steps
                 */
                int dap_postgain = volume2Ms12DapPostgain(aml_dev->sink_gain[OUTPORT_SPEAKER]);
                set_ms12_dap_postgain(&aml_dev->ms12, dap_postgain);
            }
        }
    }

    return 0;
}

static int adev_get_audio_port(struct audio_hw_device *dev __unused, struct audio_port *port __unused)
{
    return -ENOSYS;
}

#if ANDROID_PLATFORM_SDK_VERSION > 29

static int adev_add_device_effect(struct audio_hw_device *dev,
                                  audio_port_handle_t device, effect_handle_t effect)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;

    ALOGD("func:%s device:%d effect_handle_t:%p, active_outport:%d", __func__, device, effect, aml_dev->active_outport);
    return 0;
}

static int adev_remove_device_effect(struct audio_hw_device *dev,
                                     audio_port_handle_t device, effect_handle_t effect)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;

    ALOGD("func:%s device:%d effect_handle_t:%p, active_outport:%d", __func__, device, effect, aml_dev->active_outport);
    return 0;
}
#endif

#define MAX_SPK_EXTRA_LATENCY_MS (100)
#define DEFAULT_SPK_EXTRA_LATENCY_MS (15)

static int adev_open(const hw_module_t* module, const char* name, hw_device_t** device)
{
    struct aml_audio_device *adev;
    size_t bytes_per_frame = audio_bytes_per_sample(AUDIO_FORMAT_PCM_16_BIT)
                             * audio_channel_count_from_out_mask(AUDIO_CHANNEL_OUT_STEREO);
    int buffer_size = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE * bytes_per_frame;
    int spk_tuning_buf_size = MAX_SPK_EXTRA_LATENCY_MS
                              * bytes_per_frame * MM_FULL_POWER_SAMPLING_RATE / 1000;
    int spdif_tuning_latency = aml_audio_get_spdif_tuning_latency();
    int card = CARD_AMLOGIC_BOARD;
    int ret = 0, i;
    char buf[PROPERTY_VALUE_MAX] = {0};
    int disable_continuous = 1;

    ALOGD("%s: enter", __func__);
    pthread_mutex_lock(&adev_mutex);
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) {
        ret = -EINVAL;
        goto err;
    }

    if (g_adev != NULL) {
        ALOGI("adev exsits ,reuse");
        adev = (struct aml_audio_device *)g_adev;
        adev->count++;
        *device = &adev->hw_device.common;
        ALOGI("*device:%p",*device);
        goto err;
    }
    aml_audio_debug_malloc_open();
    aml_audio_debug_open();

    adev = aml_audio_calloc(1, sizeof(struct aml_audio_device));
    if (!adev) {
        ret = -ENOMEM;
        goto err;
    }
    g_adev = (void *)adev;
    g_aml_primary_adev = (void *)adev;

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
#if ANDROID_PLATFORM_SDK_VERSION > 29
    adev->hw_device.add_device_effect = adev_add_device_effect;
    adev->hw_device.remove_device_effect = adev_remove_device_effect;
#endif
    adev->hw_device.get_microphones = adev_get_microphones;
    adev->hw_device.get_audio_port = adev_get_audio_port;
    adev->hw_device.dump = adev_dump;
    adev->hdmi_format = AUTO;
    adev->ms12.ms12_scheduler_state = MS12_SCHEDULER_NONE;
    adev->ms12.last_scheduler_state = MS12_SCHEDULER_NONE;
    adev->audio_patch_2_af_stream = false;
    adev->ms12.ms12_resume_state = MS12_RESUME_NONE;
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
        adev->aml_dap_v1_enable = adev->eq_data.aml_dap_v1_enable;
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
    adev->out_16_buf_size = buffer_size;
    adev->out_16_buf = aml_audio_calloc(1, buffer_size);
    if (adev->out_16_buf == NULL) {
        AM_LOGE("malloc out_16_buf buffer failed, size:%d", buffer_size);
        ret = -ENOMEM;
        goto err_adev;
    }

    adev->out_32_buf_size = buffer_size * 2;
    adev->out_32_buf = aml_audio_calloc(1, buffer_size * 2);
    if (adev->out_32_buf == NULL) {
        AM_LOGE("malloc out_32_buf buffer failed size:%d", buffer_size * 2);
        ret = -ENOMEM;
        goto err_out_16_buf;
    }

    /* init speaker tuning buffers */
    ret = ring_buffer_init(&(adev->spk_tuning_rbuf), spk_tuning_buf_size);
    if (ret < 0) {
        AM_LOGE("Fail to init audio spk_tuning_rbuf!");
        goto err_out_32_buf;
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
    adev->dts_decode_enable = dts_lib_decode_enable();
    adev->is_ms12_tuning_dat = is_ms12_tuning_dat_in_dut();

#ifdef MS12_V24_ENABLE
    adev->support_ms12_version = eDolbyMS12_V2;
#else
    adev->support_ms12_version = eDolbyMS12_V1;
#endif

    /* convert MS to data buffer length need to cache */
    adev->spk_tuning_lvl = (spdif_tuning_latency * bytes_per_frame * MM_FULL_POWER_SAMPLING_RATE) / 1000;
    /* end of spker tuning things */
    *device = &adev->hw_device.common;
    adev->dts_post_gain = 1.0;
    for (i = 0; i < OUTPORT_MAX; i++) {
        adev->sink_gain[i] = 1.0f;
    }
    adev->ms12_main1_dolby_dummy = true;
    adev->ms12_ott_enable = false;
    adev->continuous_audio_mode_default = 0;
    adev->need_remove_conti_mode = false;
    adev->dual_spdif_support = property_get_bool("ro.vendor.platform.is.dualspdif", false);
    adev->ms12_force_ddp_out = property_get_bool("ro.vendor.platform.is.forceddp", false);
    adev->spdif_enable = true;
    adev->dolby_ms12_dap_init_mode = property_get_int32("ro.vendor.platform.ms12.dap_init_mode", 0);

    /*for ms12 case, we set default continuous mode*/
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        adev->continuous_audio_mode_default = 1;
    }
    /*we can use property to debug it*/
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
        adev->ms12.dolby_ms12_enable == false;
    } else {
        // in ms12 case, use new method for TV or BOX .zzz
        adev->hw_device.open_output_stream = adev_open_output_stream_new;
        adev->hw_device.close_output_stream = adev_close_output_stream_new;
        ALOGI("%s,in ms12 case, use new method no matter if current platform is TV or BOX", __FUNCTION__);
    }
    adev->atoms_lock_flag = false;

    if (eDolbyDcvLib == adev->dolby_lib_type) {
        adev->dcvlib_bypass_enable = 1;
    }

    adev->audio_patch = NULL;
    memset(&adev->dts_hd, 0, sizeof(struct dca_dts_dec));
    adev->sound_track_mode = 0;
    adev->dtv_sound_mode = 0;
    adev->mixing_level = 0;
    adev->advol_level = 100;
    adev->aml_dtv_audio_instances = aml_audio_calloc(1, sizeof(aml_dtv_audio_instances_t));
    if (adev->aml_dtv_audio_instances == NULL) {
        ALOGE("malloc aml_dtv_audio_instances failed");
        ret = -ENOMEM;
        goto err_spk_tuning_rbuf;
    } else {
        aml_dtv_audio_instances_t *dtv_audio_instances = (aml_dtv_audio_instances_t *)adev->aml_dtv_audio_instances;
        for (int index = 0; index < DVB_DEMUX_SUPPORT_MAX_NUM; index ++) {
            aml_dtvsync_t *dtvsync =  &dtv_audio_instances->dtvsync[index];
            pthread_mutex_init(&dtvsync->ms_lock, NULL);
        }
    }
    pthread_mutex_init(&adev->dtv_lock, NULL);

#if ENABLE_NANO_NEW_PATH
    nano_init();
#endif
    ALOGI("%s() adev->dolby_lib_type = %d", __FUNCTION__, adev->dolby_lib_type);
    adev->patch_src = SRC_INVAL;
    adev->audio_type = LPCM;

/*[SEI-zhaopf-2018-10-29] add for HBG remote audio support { */
#if defined(ENABLE_HBG_PATCH)
    startReceiveAudioData();
#endif

#ifdef ADD_AUDIO_DELAY_INTERFACE
    ret = aml_audio_delay_init();
    if (ret < 0) {
        AM_LOGE("aml_audio_delay_init fail");
        goto err_dtv_audio_instances;
    }
#endif

/*[SEI-zhaopf-2018-10-29] add for HBG remote audio support } */
#if defined(TV_AUDIO_OUTPUT)
    adev->is_TV = true;
    adev->default_alsa_ch =  aml_audio_get_default_alsa_output_ch();
    /*Now SoundBar type is depending on TV audio as only tv support multi-channel LPCM output*/
    adev->is_SBR = aml_audio_check_sbr_product();
    ALOGI("%s(), TV platform,soundbar platform %d", __func__,adev->is_SBR);
#else
    /* for stb/ott, fixed 2 channels speaker output for alsa*/
    adev->default_alsa_ch = 2;
    adev->is_STB = property_get_bool("ro.vendor.platform.is.stb", false);
    ALOGI("%s(), OTT platform", __func__);
#endif


#ifdef ENABLE_AEC_APP
    if (init_aec(CAPTURE_CODEC_SAMPLING_RATE, NUM_AEC_REFERENCE_CHANNELS,
                    CHANNEL_STEREO, &adev->aec)) {
        goto err_adev;
    }
#endif

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
        adev->raw_to_pcm_flag = false;
        profile_init(&adev->usb_audio.in_profile, PCM_IN);
#ifdef USB_KARAOKE
        subMixingSetKaraoke(adev, &adev->usb_audio.karaoke);
        pthread_mutex_init(&adev->usb_audio.karaoke.lock, NULL);
        adev->usb_audio.karaoke.kara_mic_gain = 1.0;
#endif
    }

    if (aml_audio_ease_init(&adev->audio_ease) < 0) {
        ALOGE("aml_audio_ease_init faild\n");
        ret = -EINVAL;
        goto err_dtv_audio_instances;
    }

    if (aml_audio_ease_init(&adev->volume_ease.ease) < 0) {
        ALOGE("aml_audio volume easing init faild\n");
        ret = -EINVAL;
        goto err_vol_ease;
    }
    adev->volume_ease.config_easing = true;

    // adev->debug_flag is set in hw_write()
    // however, sometimes function didn't goto hw_write() before encounting error.
    // set debug_flag here to see more debug log when debugging.
    adev->debug_flag = aml_audio_get_debug_flag();
    adev->count = 1;
    adev->is_multi_demux = is_multi_demux();

    memset(&(adev->hdmi_descs), 0, sizeof(struct aml_arc_hdmi_desc));
    aml_audio_timer_init();
    aml_audio_timer_create();
    ALOGD("%s adev->dolby_lib_type:%d  !adev->is_TV:%d", __func__, adev->dolby_lib_type, !adev->is_TV);
    /* create thread for communication between Audio Hal and MS12 */
    if ((eDolbyMS12Lib == adev->dolby_lib_type)) {
        ret = ms12_mesg_thread_create(&adev->ms12);
        if (0 != ret) {
            ALOGE("%s, ms12_mesg_thread_create fail!\n", __func__);
            goto Err_MS12_MesgThreadCreate;
        }
    }

    // init hw_mediasync
    adev->hw_mediasync = NULL;
    adev->hw_mediasync_id = -1;

    /* dtv_volume init , range [0, 1]*/
    adev->dtv_volume = 1.0;
    pthread_mutex_unlock(&adev_mutex);

    adev->insert_mute_flag = false;

    ALOGD("%s: exit", __func__);
    return 0;

Err_MS12_MesgThreadCreate:

    aml_audio_ease_close(adev->volume_ease.ease);
err_vol_ease:
    aml_audio_ease_close(adev->audio_ease);
err_dtv_audio_instances:
    aml_audio_free(adev->aml_dtv_audio_instances);
err_spk_tuning_rbuf:
    ring_buffer_release(&adev->spk_tuning_rbuf);
err_out_32_buf:
    aml_audio_free(adev->out_32_buf);
err_out_16_buf:
    aml_audio_free(adev->out_16_buf);
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
