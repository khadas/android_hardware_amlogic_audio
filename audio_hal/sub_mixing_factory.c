
#define LOG_TAG "audio_hw_hal_submixing"
//#define LOG_NDEBUG 0
#define __USE_GNU

#include <errno.h>
#include <cutils/log.h>
#include <system/audio.h>
#include <inttypes.h>
#include <aml_volume_utils.h>

#include "sub_mixing_factory.h"
#include "amlAudioMixer.h"
#include "audio_hw.h"
#include "audio_hw_utils.h"
#include "hw_avsync_callbacks.h"
#include "aml_audio_ms12.h"
#include "dolby_lib_api.h"
#include "alsa_device_parser.h"
#include "a2dp_hal.h"
#include "aml_malloc_debug.h"
#ifdef ENABLE_AEC_APP
#include "audio_aec.h"
#endif
#include "aml_audio_timer.h"
#include "karaoke_manager.h"

#include "audio_hwsync_wrap.h"
#include "audio_hw_resource_mgr.h"

//#define DEBUG_TIME

#define WRITE_COUNT_LATENCY_THRESHOLD  (6)
#define SUBMIX_USECASE_MASK            (0xffffff7e)  /* PCM_NORMAL(0) and PCM_MMAP(7) have been cleared*/
static int on_notify_cbk(void *data);
static int on_input_avail_cbk(void *data);
static ssize_t out_write_subMixingPCM(struct audio_stream_out *stream,
                      const void *buffer,
                      size_t bytes);
static int out_pause_subMixingPCM(struct audio_stream_out *stream);
static int out_resume_subMixingPCM(struct audio_stream_out *stream);
static int out_flush_subMixingPCM(struct audio_stream_out *stream);

struct pcm *getSubMixingPCMdev(struct subMixing *sm)
{
    return pcm_mixer_get_pcm_handle(sm->mixerData);
}

static int startMixingThread(struct subMixing *sm)
{
    return pcm_mixer_thread_run(sm->mixerData);
}

static int exitMixingThread(struct subMixing *sm)
{
    return pcm_mixer_thread_exit(sm->mixerData);
}

static int initSubMixingOutput(
        struct subMixing *sm,
        struct aml_audio_device *adev)
{
    R_CHECK_POINTER_LEGAL(-EINVAL, sm, "");
    if (sm->type == MIXER_LPCM) {
        struct audioCfg cfg;
        output_get_default_config(&cfg, is_TV(adev));
        struct amlAudioMixer *amixer = newAmlAudioMixer(adev, cfg);
        R_CHECK_POINTER_LEGAL(-ENOMEM, amixer, "newAmlAudioMixer failed");
        sm->mixerData = amixer;
        /* TV product has EQ DRC and sink gain */
        if (adev->eq_drc_inited) {
            ALOGI("%s(), eq data addr %p", __func__, &adev->eq_data);
            subMixingSetEQData(adev, &adev->eq_data);
        }
        if (is_TV(adev)) {
            ALOGI("%s(), sink gain addr %p", __func__, adev->sink_gain);
            subMixingSetSinkGain(adev, adev->sink_gain);
        }
        startMixingThread(sm);
    } else if (sm->type == MIXER_MS12) {
        //TODO
        AM_LOGW("not support yet, in TODO list");
    } else {
        AM_LOGE("not support");
        return -EINVAL;
    }
    return 0;
};

static int releaseSubMixingOutput(struct subMixing *sm)
{
    R_CHECK_POINTER_LEGAL(-EINVAL, sm, "");
    AM_LOGI("++");
    exitMixingThread(sm);
    freeAmlAudioMixer(sm->mixerData);
    sm->mixerData = NULL;

    return 0;
}

static ssize_t aml_out_write_to_mixer(struct audio_stream_out *stream, const void* buffer,
                                    size_t bytes)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    struct subMixing *sm = adev->sm;
    struct amlAudioMixer *audio_mixer = sm->mixerData;
    const char *data = (char *)buffer;
    size_t written_total = 0, frame_size = 4;
    uint32_t latency_frames = 0;
    struct timespec ts;

    if (adev->is_netflix && (STREAM_PCM_NORMAL == out->usecase || STREAM_PCM_HWSYNC == out->usecase)) {
        aml_audio_data_handle(stream, buffer, bytes);
    }

    do {
        ssize_t written = 0;
        AM_LOGV("stream usecase: %s, written_total %zu, bytes %zu",
            usecase2Str(out->usecase), written_total, bytes);

        written = mixer_write_inport(audio_mixer,
                out->inputPortID, data, bytes - written_total);
        if (written < 0) {
            AM_LOGE("write failed, errno = %zu", written);
            return written;
        }

        if (written > 0) {
            written_total += written;
            data += written;
            //latency_frames = mixer_get_inport_latency_frames(audio_mixer, out->port_index) +
             //       mixer_get_outport_latency_frames(audio_mixer);
            //pthread_mutex_lock(&out->lock);
            //clock_gettime(CLOCK_MONOTONIC, &out->timestamp);
            //out->last_frames_position += written / frame_size - latency_frames;
            //pthread_mutex_unlock(&out->lock);
        }
        AM_LOGV("port index(%d) written(%zu), written_total(%zu), bytes(%zu)",
            out->inputPortID, written, written_total, bytes);

        if (written_total >= bytes) {
            AM_LOGV("exit");
            break;
        }

        //usleep((bytes- written_total) * 1000 / 5 / 48);
        //if (out->port_index == 1) {
        ts_wait_time_us(&ts, 5000);
        AM_LOGV("-wait....");
        pthread_mutex_lock(&out->cond_lock);
        pthread_cond_timedwait(&out->cond, &out->cond_lock, &ts);
        AM_LOGV("--wait wakeup");
        pthread_mutex_unlock(&out->cond_lock);
        //}
    } while (1);

    return written_total;
}

static int consume_meta_data(void *cookie,
        uint32_t frame_size, int64_t pts, uint64_t offset)
{
    struct aml_stream_out *out = (struct aml_stream_out *)cookie;
    struct aml_audio_device *adev = out->dev;
    //struct aml_audio_mixer *audio_mixer = adev->audio_mixer;
    struct subMixing *sm = adev->sm;
    struct amlAudioMixer *audio_mixer = sm->mixerData;
    struct meta_data_list *mdata_list = aml_audio_calloc(1, sizeof(struct meta_data_list));

    R_CHECK_POINTER_LEGAL(-ENOMEM, mdata_list, "no memory");
    if (out->pause_status) {
        AM_LOGE("write in pause status");
    }

    mdata_list->mdata.frame_size = frame_size;
    mdata_list->mdata.pts = pts;
    mdata_list->mdata.payload_offset = offset;

    if (out->debug_stream) {
        AM_LOGD("frame_size %d, pts %" PRId64 "ms, payload offset %" PRId64 "",
                frame_size, pts/1000000, offset);
    }
    if (get_mixer_hwsync_frame_size(audio_mixer) != frame_size) {
        AM_LOGI("resize frame_size %d", frame_size);
        set_mixer_hwsync_frame_size(audio_mixer, frame_size);
    }
    pthread_mutex_lock(&out->mdata_lock);
    list_add_tail(&out->mdata_list, &mdata_list->list);
    pthread_mutex_unlock(&out->mdata_lock);
    return 0;
}

void sm_timer_callback_handler(union sigval sigv)
{
    struct aml_audio_device *adev = aml_adev_get_handle();
    struct aml_stream_out *out = NULL;
    bool is_hwsync_lpcm = false;

    AM_LOGD("func:%s sigv:%d ~~~~~~~~~~", __func__, sigv.sival_int);
    for (int i = 0 ; i < STREAM_USECASE_MAX; i++) {
        out = adev->active_outputs[i];
        if (out && audio_is_linear_pcm(out->hal_internal_format)
            && (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC)) {
            is_hwsync_lpcm = true;
            break;
        }
    }

    if (out && is_hwsync_lpcm) {
        out->frame_write_sum_updated = false;
    }
    return ;
}

static int consume_output_data(void *cookie, const void* buffer, size_t bytes)
{
    ssize_t written = 0;
    uint64_t latency_frames = 0;
    struct audio_stream_out *stream = (struct audio_stream_out *)cookie;
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    struct subMixing *sm = adev->sm;
    struct amlAudioMixer *audio_mixer = sm->mixerData;
    uint16_t *in_buf_16 = (uint16_t *)buffer;
    struct timespec tval, new_tval;
    uint64_t us_since_last_write = 0;
    int64_t throttle_timeus = 0;
    int frame_size = 4;
    void * out_buf = (void*)buffer;
    size_t out_size = bytes;
    int bResample = 0;
    int channels = audio_channel_count_from_out_mask(out->hal_channel_mask);
    float volume[8];
    float last_volume[8];
    volume[0]   = out->volume_l;
    volume[1]   = out->volume_r;
    last_volume[0]   = out->last_volume_l;
    last_volume[1]   = out->last_volume_r;

    AM_LOGV("++bytes = %zu", bytes);
    if (out->pause_status) {
        AM_LOGE("write in pause status");
    }
    if (audio_is_linear_pcm(out->hal_format)) {
        frame_size = out->hal_frame_size;
    }
    clock_gettime(CLOCK_MONOTONIC, &tval);
    /*
    android only support max stereo stream volume configuration,we have to reuse left volume as
    C/LFE/Ls/Rs/Lrs/Rrs volume
    */
    if (channels > 2) {
        for (int ch = 2; ch < channels; ch ++) {
            last_volume[ch] = last_volume[0];
            volume[ch] = volume[0];
        }
    }
    if (adev->debug_flag) {
        AM_LOGI("last_volume=%f volume=%f channels=%d bytes=%zu", last_volume[0], volume[0], channels, bytes);
    }
    apply_volume_fade(last_volume, volume, in_buf_16, sizeof(uint16_t), channels, bytes);
    out->last_volume_l = out->volume_l;
    out->last_volume_r = out->volume_r;
    if (out->hw_sync_mode && out->resample_outbuf != NULL) {
        int out_frame = bytes >> 2;
        out_frame = resample_process (&out->aml_resample, out_frame,
                (int16_t *) buffer, (int16_t *) out->resample_outbuf);
        out_size = out_frame << 2;
        out_buf = out->resample_outbuf;
        bResample = 1;
    }
    written = aml_out_write_to_mixer(stream, out_buf, out_size);

    if (written < 0) {
        AM_LOGE("written failed, %zd", written);
        goto exit;
    }

    /*here may be a problem, after resample, the size write to mixer is changed,
      to avoid some problem, we assume it is totally written.
    */
    if (bResample) {
        written = bytes;
    }

    clock_gettime(CLOCK_MONOTONIC, &new_tval);
    us_since_last_write = llabs((new_tval.tv_sec - out->timestamp.tv_sec) * 1000000 +
            (new_tval.tv_nsec - out->timestamp.tv_nsec) / 1000);
    //out->timestamp = new_tval;

    int used_this_write = (new_tval.tv_sec - tval.tv_sec) * 1000000 +
            (new_tval.tv_nsec - tval.tv_nsec) / 1000;
    int target_us = bytes * 1000 / out->hal_frame_size / 48;
    // calculate presentation frames and timestamps
    //clock_gettime(CLOCK_MONOTONIC, &out->timestamp);
    //latency_frames = mixer_get_inport_latency_frames(audio_mixer, out->port_index) +
    //    mixer_get_outport_latency_frames(audio_mixer);
    //latency_frames = mixer_get_inport_latency_frames(audio_mixer, out->port_index);
    //out->frame_write_sum += written;

    //if (out->last_frames_position > out->frame_write_sum)
    //    out->last_frames_position = out->frame_write_sum - latency_frames;
    //else
    //    out->last_frames_position = out->frame_write_sum;
    AM_LOGV("++written = %zd", written);
    if (getprop_bool("vendor.media.audiohal.hwsync")) {
        aml_audio_dump_audio_bitstreams("/data/audio/consumeout.raw", buffer, written);
    }
    if (0) {
        AM_LOGD("last_frames_position(%" PRId64 ") latency_frames(%" PRId64 ")",
            out->last_frames_position, latency_frames);
    }
    throttle_timeus = target_us - us_since_last_write;
    if (throttle_timeus > 0 && throttle_timeus < 200000) {
        AM_LOGV("throttle time %" PRId64 " us", throttle_timeus);
        if (throttle_timeus > 1000)
            usleep(throttle_timeus - 1000);
    }

    //throttle simply 4/5 duration
    //usleep(bytes * 1000 / 4 / 48 * 1 / 2);
exit:
    clock_gettime(CLOCK_MONOTONIC, &out->timestamp);
    out->lasttimestamp.tv_sec = out->timestamp.tv_sec;
    out->lasttimestamp.tv_nsec = out->timestamp.tv_nsec;
    if (written >= 0) {
        out->frame_write_sum += written / frame_size;

        //start the timer to monitor frame_write_sum_updated
        uint32_t remaining_time = audio_timer_remaining_time(out->timer_id);
        if (remaining_time > 0) {
            audio_timer_stop(out->timer_id);
        }
        audio_one_shot_timer_start(out->timer_id, AML_TIMER_CONSUME_DATA_DELAY);
        out->frame_write_sum_updated = true;
    }
    if (out->debug_stream) {
        AM_LOGD("(frames sum %" PRId64 " - latency_frames:%" PRIu64"), = last frames %" PRId64 "", out->frame_write_sum, latency_frames, out->last_frames_position);
    }
    return written;
}

static ssize_t out_write_hwsync_lpcm(struct audio_stream_out *stream, const void* buffer,
                                    size_t bytes)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    struct subMixing *sm = adev->sm;
    size_t channel_count = audio_channel_count_from_out_mask(out->hal_channel_mask);
    size_t frame_size = audio_bytes_per_frame(channel_count, out->hal_format);;
    int written_total = 0;
    bool ret = false;
    struct timespec ts;
    memset(&ts, 0, sizeof(struct timespec));

    // when connect bt, bt stream maybe open before hdmi stream close,
    // bt stream mediasync is set to adev->hw_mediasync, and it would be
    // release in hdmi stream close, so bt stream mediasync is invalid
    if (out->hwsync->mediasync != NULL && adev->hw_mediasync == NULL) {
        adev->hw_mediasync = aml_audio_hwsync_create();
        out->hwsync->use_mediasync = true;
        out->hwsync->mediasync = adev->hw_mediasync;
        ret = aml_hwsync_wrap_set_id(out->hwsync, out->hwsync->hwsync_id);
        if (!ret) {
            ALOGI("mediasync set hwsync id fail, try gMediaSync_bindStaticInstance");
            ret = aml_hwsync_wrap_set_static_id(out->hwsync, out->hwsync->hwsync_id);
        }
        if (!ret) {
            ALOGD("%s: aml_hwsync_wrap_set_id fail: ret=%d, id=%d", __func__, ret, out->hwsync->hwsync_id);
            ret = aml_hwsync_wrap_get_id(out->hwsync->mediasync, &out->hwsync->hwsync_id);
            if (ret) {
                adev->hw_sync_id = out->hwsync->hwsync_id;
                aml_hwsync_wrap_set_id(out->hwsync, out->hwsync->hwsync_id);
            }
        }
        aml_audio_hwsync_init(out->hwsync, out);
    }
    if (out->standby) {
        AM_LOGI("io %d: out:%p usecase:%s standby to unstandby", out->io_handle, out, usecase2Str(out->usecase));
        aml_audio_set_cpu23_affinity();
        out->hwsync_extractor = new_hw_avsync_header_extractor(consume_meta_data,
                consume_output_data, out);
        out->first_pts_set = false;
        out->need_first_sync = false;
        out->last_pts = 0;
        out->last_payload_offset = 0;
        pthread_mutex_init(&out->mdata_lock, NULL);
        list_init(&out->mdata_list);
        pthread_mutex_lock(&adev->lock);
        init_mixer_input_port(sm->mixerData, &out->audioCfg, out->flags,
            on_notify_cbk, out, on_input_avail_cbk, out,
            on_meta_data_cbk, out, out->volume_l);
        pthread_mutex_unlock(&adev->lock);
        AM_LOGI("hwsync port type = %d",
                get_input_port_type(&out->audioCfg, out->flags));
        out->standby = false;
        out->audio_data_handle_state = AUDIO_DATA_HANDLE_START;
        mixer_set_continuous_output(sm->mixerData, false);
        /*wait video ready*/
        if (out->hwsync->use_mediasync) {
            aml_hwsync_wait_video_start(out->hwsync);
        } else {
            int vframe_ready_cnt = 0;
            int delay_count = 0;
            while (delay_count < 10) {
                vframe_ready_cnt = get_sysfs_int("/sys/class/video/vframe_ready_cnt");
                if (vframe_ready_cnt < 2) {
                    usleep(10000);
                    delay_count++;
                    continue;
                }
                break;
            }
            AM_LOGI("/sys/class/video/vframe_ready_cnt is %d delay count=%d", vframe_ready_cnt, delay_count);
        }
    }
    if (out->pause_status) {
        AM_LOGW("write in pause status!!");
        out->pause_status = false;
    }
    if (out->hwsync_extractor)
        written_total = header_extractor_write(out->hwsync_extractor, buffer, bytes);
    AM_LOGV("bytes %zu, out->last_frames_position %" PRId64 " frame_sum %" PRId64 " ",
            bytes, out->last_frames_position, out->frame_write_sum);

    if (getprop_bool("vendor.media.audiohal.hwsync") && written_total > 0) {
        aml_audio_dump_audio_bitstreams("/data/audio/audiomain.raw", buffer, written_total);
    }

    if (written_total > 0) {
        AM_LOGV("--out(%p)written %d, write_sum after %" PRId64 "",
                out, written_total, out->frame_write_sum);
        if ((size_t)written_total != bytes)
            AM_LOGE("--written %d, but bytes = %zu", written_total, bytes);
        return written_total;
    } else {
        AM_LOGE("--written %d, but return bytes", written_total);
        //return 1;
        return bytes;
    }
    return written_total;
}

static ssize_t out_write_system(struct audio_stream_out *stream, const void *buffer,
                                    size_t bytes)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    //struct aml_audio_mixer *audio_mixer = adev->audio_mixer;
    struct subMixing *sm = adev->sm;
    struct amlAudioMixer *audio_mixer = sm->mixerData;
    struct timespec tval, new_tval;
    uint64_t us_since_last_write = 0;
    //uint64_t begin_time, end_time;
    ssize_t written = 0;
    size_t remain = 0;
    size_t channel_count = audio_channel_count_from_out_mask(out->hal_channel_mask);
    size_t frame_size = audio_bytes_per_frame(channel_count, out->hal_format);;
    //uint64_t throttle_timeus = THROTLE_TIME_US;//aml_audio_get_throttle_timeus();
    int64_t throttle_timeus = 0;//aml_audio_get_throttle_timeus(bytes);

    if (out->standby) {
        AM_LOGI("io %d: out:%p standby to unstandby", out->io_handle, out);
        out->standby = false;
    }

    if (bytes == 0) {
        AM_LOGW("inval to write bytes 0");
        usleep(512 * 1000 / 48 / frame_size);
        written = 0;
        goto exit;
        //return 0;
    }

    clock_gettime(CLOCK_MONOTONIC, &tval);
    //begin_time = get_systime_ns();
    written = aml_out_write_to_mixer(stream, buffer, bytes);
    if (written >= 0) {
        remain = bytes - written;
        out->frame_write_sum += written / frame_size;
        if (remain > 0) {
            AM_LOGE("INVALID partial written");
        }
        clock_gettime(CLOCK_MONOTONIC, &new_tval);
        if (tval.tv_sec > new_tval.tv_sec)
            AM_LOGE("FATAL ERROR");
        AM_LOGV("++bytes %zu, out->port_index %d", bytes, out->inputPortID);
        //AM_LOGD(" %lld us, %lld", new_tval.tv_sec, tval.tv_sec);

        us_since_last_write = (uint64_t)(new_tval.tv_sec - out->timestamp.tv_sec) * 1000000 +
                (uint64_t)(new_tval.tv_nsec - out->timestamp.tv_nsec) / 1000;
        //out->timestamp = new_tval;

        int used_this_write = (new_tval.tv_sec - tval.tv_sec) * 1000000 +
                (new_tval.tv_nsec - tval.tv_nsec) / 1000;
        int target_us = bytes * 1000 / frame_size / 48;

        AM_LOGV("time spent on write %" PRId64 " us, written %zd", us_since_last_write, written);
        AM_LOGV("used_this_write %d us, target %d us", used_this_write, target_us);
        throttle_timeus = target_us - us_since_last_write;
        if (throttle_timeus > 0 && throttle_timeus < 200000) {
            AM_LOGV("throttle time %" PRId64 " us", throttle_timeus);
            if (throttle_timeus > 1800) {
                //usleep(throttle_timeus - 1800);
                AM_LOGV("actual throttle %" PRId64 " us, since last %" PRId64 " us",
                        throttle_timeus, us_since_last_write);
            } else {
                AM_LOGV("%" PRId64 " us, but un-throttle", throttle_timeus);
            }
        } else if (throttle_timeus != 0) {
            // first time write, sleep
            //usleep(target_us - 100);
            AM_LOGV("invalid throttle time %" PRId64 " us, us since last %" PRId64 " us", throttle_timeus, us_since_last_write);
            AM_LOGV("\n\n");
        }
    } else {
        AM_LOGE("write fail, err = %zd", written);
    }

    // TODO: means first write, need check this by method
    if (us_since_last_write > 500000) {
        usleep(bytes * 1000 / 48 / frame_size);
        AM_LOGV("invalid duration %" PRIu64 " us", us_since_last_write);
        //AM_LOGE("last   write %ld s,  %ld ms", out->timestamp.tv_sec, out->timestamp.tv_nsec/1000000);
        //AM_LOGE("before write %ld s,  %ld ms", tval.tv_sec, tval.tv_nsec/1000000);
        //AM_LOGE("after  write %ld s,  %ld ms", new_tval.tv_sec, new_tval.tv_nsec/1000000);
    }

exit:
    // update new timestamp
    pthread_mutex_lock(&out->apts_update_lock);
    clock_gettime(CLOCK_MONOTONIC, &out->timestamp);
    out->lasttimestamp.tv_sec = out->timestamp.tv_sec;
    out->lasttimestamp.tv_nsec = out->timestamp.tv_nsec;
    if (written >= 0) {
        uint32_t latency_frames = mixer_get_inport_latency_frames(audio_mixer, out->inputPortID);
                //+ mixer_get_outport_latency_frames(audio_mixer);
        if (out->frame_write_sum > latency_frames)
            out->last_frames_position = out->frame_write_sum - latency_frames;
        else
            out->last_frames_position = out->frame_write_sum;

        if (0) {
            AM_LOGI("last position %" PRId64 ", latency_frames %d", out->last_frames_position, latency_frames);
        }
    }
    pthread_mutex_unlock(&out->apts_update_lock);
    return written;
}

static ssize_t out_write_direct_pcm(struct audio_stream_out *stream, const void *buffer,
                                    size_t bytes)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    struct subMixing *sm = adev->sm;
    struct amlAudioMixer *audio_mixer = sm->mixerData;
    struct timespec tval, new_tval;
    uint64_t us_since_last_write = 0;
    //uint64_t begin_time, end_time;
    ssize_t written = 0;
    size_t remain = 0;
    int frame_size = 4; // currently, npcm decoder will output 2ch 16bit pcm
    int64_t throttle_timeus = 0;//aml_audio_get_throttle_timeus(bytes);
    int channels = 2;
    int sample_size = 2;
    float volume[8];
    float last_volume[8];
    volume[0]   = out->volume_l;
    volume[1]   = out->volume_r;
    last_volume[0]   = out->last_volume_l;
    last_volume[1]   = out->last_volume_r;


    if (out->standby) {
        init_mixer_input_port(sm->mixerData, &out->audioCfg, out->flags,
            on_notify_cbk, out, on_input_avail_cbk, out,
            NULL, NULL, 1.0);
        AM_LOGI("io %d: out:%p usecase:%s standby to unstandby, port:%s", out->io_handle, out, usecase2Str(out->usecase),
            mixerInputType2Str(get_input_port_type(&out->audioCfg, out->flags)));
        out->standby = false;
        out->audio_data_handle_state = AUDIO_DATA_HANDLE_START;
    }

    /*
     * if out->aml_dec isn't NULL, lpcm5.1/7.1 will be downmix to 2ch.
     * then output_stream information is incorrect.
    */
    if (audio_is_linear_pcm(out->hal_format) && out->aml_dec == NULL) {
        frame_size = out->hal_frame_size;
        channels = audio_channel_count_from_out_mask(out->hal_channel_mask);
        sample_size = audio_bytes_per_sample(out->hal_format);
    }

    clock_gettime(CLOCK_MONOTONIC, &tval);

    /*
     * if out->aml_dec isn't NULL, volume will be processed by function
     * (aml_audio_nonms12_render -> aml_audio_stream_volume_process)
    */
    if (out->aml_dec == NULL) {
        /*
        android only support max stereo stream volume configuration,we have to reuse left volume as
        C/LFE/Ls/Rs/Lrs/Rrs volume
        */
        if (channels > 2) {
            for (int ch = 2; ch < channels; ch ++) {
                last_volume[ch] = last_volume[0];
                volume[ch] = volume[0];
            }
        }
        if (adev->debug_flag) {
            AM_LOGI("last_volume=%f volume=%f channels=%d bytes=%zu", last_volume[0], volume[0], channels, bytes);
        }
        apply_volume_fade(last_volume, volume, (void *)buffer, sample_size, channels, bytes);
        out->last_volume_l = out->volume_l;
        out->last_volume_r = out->volume_r;
    }

    //begin_time = get_systime_ns();
    written = aml_out_write_to_mixer(stream, buffer, bytes);
    if (written >= 0) {
        remain = bytes - written;
        out->frame_write_sum += written / frame_size;
        if (remain > 0) {
            AM_LOGE("INVALID partial written");
        }
        clock_gettime(CLOCK_MONOTONIC, &new_tval);
        if (tval.tv_sec > new_tval.tv_sec)
            AM_LOGE("FATAL ERROR");
        AM_LOGV("++bytes %zu, out->port_index %d", bytes, out->inputPortID);
        //AM_LOGD(" %lld us, %lld", new_tval.tv_sec, tval.tv_sec);

        us_since_last_write = llabs((new_tval.tv_sec - out->timestamp.tv_sec) * 1000000 +
                (new_tval.tv_nsec - out->timestamp.tv_nsec) / 1000);
        //out->timestamp = new_tval;

        int used_this_write = (new_tval.tv_sec - tval.tv_sec) * 1000000 +
                (new_tval.tv_nsec - tval.tv_nsec) / 1000;
        int target_us = bytes * 1000 / frame_size / 48;

        AM_LOGV("time spent on write %" PRId64 " us, written %zd", us_since_last_write, written);
        AM_LOGV("used_this_write %d us, target %d us", used_this_write, target_us);
        throttle_timeus = target_us - us_since_last_write;
        if (throttle_timeus > 0 && throttle_timeus < 200000) {
            AM_LOGV("throttle time %" PRId64 " us", throttle_timeus);
            if (throttle_timeus > 1800) {
                usleep(throttle_timeus - 1800);
                AM_LOGV("actual throttle %" PRId64 " us, since last %" PRId64 " us",
                        throttle_timeus, us_since_last_write);
            } else {
                AM_LOGV("%" PRId64 " us, but un-throttle", throttle_timeus);
            }
        } else if (throttle_timeus != 0) {
            // first time write, sleep
            //usleep(target_us - 100);
            AM_LOGV("invalid throttle time %" PRId64 " us, us since last %" PRId64 " us", throttle_timeus, us_since_last_write);
            AM_LOGV("\n\n");
        }
    } else {
        AM_LOGE("write fail, err = %zd", written);
    }

    // TODO: means first write, need check this by method
    if (us_since_last_write > 500000) {
        usleep(bytes * 1000 / 48 / frame_size);
        AM_LOGV("invalid duration %" PRIu64 " us", us_since_last_write);
        //AM_LOGE("last   write %ld s,  %ld ms", out->timestamp.tv_sec, out->timestamp.tv_nsec/1000000);
        //AM_LOGE("before write %ld s,  %ld ms", tval.tv_sec, tval.tv_nsec/1000000);
        //AM_LOGE("after  write %ld s,  %ld ms", new_tval.tv_sec, new_tval.tv_nsec/1000000);
    }

exit:
    // update new timestamp
    pthread_mutex_lock(&out->apts_update_lock);
    clock_gettime(CLOCK_MONOTONIC, &out->timestamp);
    out->lasttimestamp.tv_sec = out->timestamp.tv_sec;
    out->lasttimestamp.tv_nsec = out->timestamp.tv_nsec;
    if (written >= 0) {
        uint32_t latency_frames = mixer_get_inport_latency_frames(audio_mixer, out->inputPortID);
                + mixer_get_outport_latency_frames(audio_mixer);
        if (out->frame_write_sum > latency_frames)
            out->last_frames_position = out->frame_write_sum - latency_frames;
        else
            out->last_frames_position = out->frame_write_sum;

        if (0) {
            AM_LOGI("last position %" PRId64 ", latency_frames %d", out->last_frames_position, latency_frames);
        }
    }
    pthread_mutex_unlock(&out->apts_update_lock);

    return written;
}

static int on_notify_cbk(void *data)
{
    struct aml_stream_out *out = data;
    pthread_cond_broadcast(&out->cond);
    return 0;
}

static int on_input_avail_cbk(void *data)
{
    struct aml_stream_out *out = data;
    pthread_cond_broadcast(&out->cond);
    return 0;
}

int out_get_presentation_position_port(
        const struct audio_stream_out *stream,
        uint64_t *frames,
        struct timespec *timestamp)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    struct subMixing *sm = adev->sm;
    struct amlAudioMixer *audio_mixer = sm->mixerData;
    uint64_t frames_written_hw = out->frame_write_sum;
    int ret = 0;
    int tuning_latency_frame= 0;
    int frame_latency = 0;
    R_CHECK_POINTER_LEGAL(-EINVAL, frames, "");
    R_CHECK_POINTER_LEGAL(-EINVAL, timestamp, "");
    bool is_earc = 0;//(ATTEND_TYPE_EARC == aml_audio_earctx_get_type(adev));

    /* add this code for VTS. */
    if (0 == frames_written_hw) {
        *frames = frames_written_hw;
        *timestamp = out->timestamp;
        return ret;
    }

    if (out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
        pthread_mutex_lock(&out->apts_update_lock);
        ret = mixer_get_presentation_position(audio_mixer, out->inputPortID, frames, timestamp);
        pthread_mutex_unlock(&out->apts_update_lock);
        struct timespec adjusted_timestamp;
        // libaudioclient code expects HAL position to lag behind server position.
        // If the two are the same, it resets timestamp to the current time.
        // As a temporary work-around, get around this by subtracting one
        // frame from both position and timestamp.
        int64_t frame_diff_for_client = 1;
        int64_t time_diff_for_client = NSEC_PER_SEC / out->hal_rate;
        int64_t adjusted_nanos = (long long)timestamp->tv_sec * NSEC_PER_SEC + (long long)timestamp->tv_nsec - time_diff_for_client;
        int64_t pre_time_nanos = out->last_timestamp_reported.tv_sec * NSEC_PER_SEC + out->last_timestamp_reported.tv_nsec;
        if (adjusted_nanos < 0) {
           adjusted_nanos = 0;
        } else if (adjusted_nanos < pre_time_nanos) {
            adjusted_nanos = pre_time_nanos;
        }
        adjusted_timestamp.tv_sec = adjusted_nanos / NSEC_PER_SEC;
        adjusted_timestamp.tv_nsec = adjusted_nanos % NSEC_PER_SEC;
        AM_LOGV("adjusted_nanos: %" PRId64 ", frame drift: %" PRId64 ", hal_rate: %u", adjusted_nanos,
            frame_diff_for_client, out->hal_rate);
        if (*frames > frame_diff_for_client) {
            *frames -= frame_diff_for_client;
        }
        *timestamp = adjusted_timestamp;
    } else if (!is_dev_patch_running(adev)) {
        if ((out->hw_sync_mode || out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC)
            && (!out->frame_write_sum_updated || out->is_insert_zero_data || out->pause_status || out->standby)) {
            pthread_mutex_lock(&out->apts_update_lock);
            *frames = frames_written_hw;
            *timestamp = out->timestamp;
            pthread_mutex_unlock(&out->apts_update_lock);
        } else {
            pthread_mutex_lock(&out->apts_update_lock);
            ret = mixer_get_presentation_position(audio_mixer,
                out->inputPortID, frames, timestamp);
            pthread_mutex_unlock(&out->apts_update_lock);
            // convert the frames for resample in AudioHal
            if (out->hal_rate != MM_FULL_POWER_SAMPLING_RATE) {
                *frames = (*frames * out->hal_rate) / MM_FULL_POWER_SAMPLING_RATE;
            }

        }
        if (adev->debug_flag)
            AM_LOGD("standby:%d pause:%d frame_write_sum_updated:%d frames:%"PRIu64" frame_write_sum:%"PRIu64"",
                out->standby, out->pause_status, out->frame_write_sum_updated, *frames, out->frame_write_sum);
    } else {
        pthread_mutex_lock(&out->apts_update_lock);
        *frames = frames_written_hw;
        *timestamp = out->timestamp;
        pthread_mutex_unlock(&out->apts_update_lock);
    }

    int latency_ms = 0;
    if (ret == 0) {
        latency_ms = aml_audio_get_latency_offset(adev->cur_out_devices,
                                                         out->hal_internal_format,
                                                         adev->sink_format,
                                                         adev->ms12.dolby_ms12_enable,
                                                         is_earc);
        if (!adev->is_netflix && (out->hw_sync_mode || out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC)) {
            latency_ms = 0;//this is pcm format, it should be 0ms for latency.
        }

        frame_latency = latency_ms * (out->hal_rate / MSEC_PER_SEC);
        if (frame_latency < 0 && *frames < abs(frame_latency)) {
            *frames = 0;
        } else {
            *frames += frame_latency ;
        }


        if (adev->debug_flag) {
            AM_LOGI("tuning_latency_ms:%d, frame_latency:%d", latency_ms, frame_latency);
        }
        out->last_frames_position = *frames;
    } else {
        *frames = out->last_frames_position;
        AM_LOGW("pts not valid yet");
    }

    {
        if (adev->debug_flag) {
            AM_LOGI("io %d: out:%p frames:%"PRIu64" sec:%ld nanosec:%ld", out->io_handle, out,
                *frames, timestamp->tv_sec, timestamp->tv_nsec);
        }
        int64_t  frame_diff_ms =  (*frames - out->last_frame_reported) * MSEC_PER_SEC / out->hal_rate;
        int64_t pre_time_nanos = (long long)out->last_timestamp_reported.tv_sec * NSEC_PER_SEC + (long long)out->last_timestamp_reported.tv_nsec;
        int64_t cur_time_nanos = (long long)timestamp->tv_sec * NSEC_PER_SEC + (long long)timestamp->tv_nsec;
        int delay  = 0;
        if (cur_time_nanos < pre_time_nanos) {
            AM_LOGW("timestamp loopback. pre_time:%" PRId64 " ms, cur_time:%" PRId64 "ms",
                pre_time_nanos / NSEC_PER_MSEC, cur_time_nanos / NSEC_PER_MSEC);
        }

        int64_t system_time_ms = 0;
        system_time_ms = ((int64_t)timestamp->tv_sec * 1000 + (int64_t)timestamp->tv_nsec / 1000000) -
            ((int64_t)out->last_timestamp_reported.tv_sec * 1000 + (int64_t)out->last_timestamp_reported.tv_nsec / 1000000);

        int64_t jitter_diff = frame_diff_ms - system_time_ms;
        out->jitter_ms = jitter_diff;
        if (audio_is_linear_pcm(out->hal_format) && audio_stream_out_frame_size(stream) && !out->hw_sync_mode) {
            delay = out->input_bytes_size / audio_stream_out_frame_size(stream) - *frames;
        }
        out->audio_delay = delay;
        if  (llabs(jitter_diff) > JITTER_DURATION_MS && adev->debug_flag) {
            AM_LOGI("io %d: out:%p jitter out last pos info: %"PRIu64", sec:%ld, nanosec:%ld", out->io_handle, out,
                out->last_frame_reported, out->last_timestamp_reported.tv_sec, out->last_timestamp_reported.tv_nsec);
            AM_LOGI("jitter system time diff %"PRIu64" ms, position diff %"PRIu64" ms, jitter %"PRId64" ms",
                system_time_ms, frame_diff_ms, jitter_diff);
        }

        aml_stream_out_info_print(out, frames, timestamp);

        out->last_frame_reported = *frames;
        out->last_timestamp_reported = *timestamp;
    }

    return ret;
}

static int out_standby_subMixingPCM(struct audio_stream *stream);
static int initSubMixingInputPcm(
        struct audio_config *config,
        struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;
    struct subMixing *sm = adev->sm;
    bool hwsync_lpcm = false;
    int flags = out->flags;
    int channel_count = popcount(config->channel_mask);

    hwsync_lpcm = (flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC && config->sample_rate <= 48000 &&
               audio_is_linear_pcm(config->format) && channel_count <= 2);
    AM_LOGI("++ io %d: out %p, flags %#x, hwsync lpcm %d", out->io_handle, out, flags, hwsync_lpcm);
    out->audioCfg = *config;

    if (!out->is_tv_src_stream) {
        out->stream.write = out_write_subMixingPCM;
        out->stream.pause = out_pause_subMixingPCM;
        out->stream.resume = out_resume_subMixingPCM;
        out->stream.flush = out_flush_subMixingPCM;
        out->stream.common.standby = out_standby_subMixingPCM;
    }

    list_init(&out->mdata_list);
    if (hwsync_lpcm) {
        AM_LOGI("lpcm case");
        mixer_set_continuous_output(sm->mixerData, true);
    }
    return 0;
}

static int deleteSubMixingInputPcm(struct aml_stream_out *out)
{
    struct aml_audio_device *adev = out->dev;
    struct subMixing *sm = adev->sm;
    struct amlAudioMixer *audio_mixer = sm->mixerData;
    struct audio_config *config = &out->audioCfg;
    bool hwsync_lpcm = false;
    int flags = out->flags;
    int channel_count = popcount(config->channel_mask);

    hwsync_lpcm = (flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC && config->sample_rate <= 48000 &&
               audio_is_linear_pcm(config->format) && channel_count <= 2);

    AM_LOGI("cnt_stream_using_mixer %d", sm->cnt_stream_using_mixer);
    struct meta_data_list *mdata_list;
    struct listnode *item;

    if (out->hw_sync_mode) {
        pthread_mutex_lock(&out->mdata_lock);
        while (!list_empty(&out->mdata_list)) {
            item = list_head(&out->mdata_list);
            mdata_list = node_to_item(item, struct meta_data_list, list);
            list_remove(item);
            //AM_LOGI("free meta data list=%p", mdata_list);
            aml_audio_free(mdata_list);
        }
        pthread_mutex_unlock(&out->mdata_lock);
    }

    if (hwsync_lpcm) {
        AM_LOGI("lpcm case");
        mixer_set_continuous_output(sm->mixerData, false);
    }
    return 0;
}

int initSubMixingInput(struct aml_stream_out *out,
        struct audio_config *config)
{
    R_CHECK_POINTER_LEGAL(-EINVAL, out, "");
    R_CHECK_POINTER_LEGAL(-EINVAL, config, "");
    struct aml_audio_device *adev = out->dev;
    struct subMixing *sm = adev->sm;
    int ret = 0;

    if (sm->type == MIXER_LPCM) {
        ret = initSubMixingInputPcm(config, out);
    } else if (sm->type == MIXER_MS12) {
        //ret = initSubMixingInputMS12(sm, config, out);
        AM_LOGE("MS12 not supported yet");
        ret = -1;
    }

    return ret;
};

int deleteSubMixingInput(struct aml_stream_out *out)
{
    R_CHECK_POINTER_LEGAL(-EINVAL, out, "");
    struct aml_audio_device *adev = out->dev;
    struct subMixing *sm = adev->sm;
    int ret = 0;

    if (sm->type == MIXER_LPCM) {
        ret = deleteSubMixingInputPcm(out);
    } else if (sm->type == MIXER_MS12) {
        //ret = deleteSubMixingInputMS12(out);
        AM_LOGE("MS12 not supported yet");
        ret = -1;
    }

    return ret;
}

#if 0
int subMixerWriteInport(
        struct subMixing *sm,
        void *buf,
        size_t bytes,
        aml_mixer_input_port_type_e port_index)
{
    int ret = 0;
    if (sm->type == MIXER_LPCM) {
        ret = mixer_write_inport(sm->mixerData, port_index, buf, bytes);
    } else {
        ret = -EINVAL;
        AM_LOGE("not support");
    }

    return 0;
};

int mainWriteMS12(
            struct subMixing *sm,
            void *buf,
            size_t bytes)
{
    (void *)buf;
    (void *)bytes;
    struct dolby_ms12_desc *ms12 = (struct dolby_ms12_desc *)sm->mixerData;
    return 0;
}

int sysWriteMS12(
            struct subMixing *sm,
            void *buf,
            size_t bytes)
{
    (void *)buf;
    (void *)bytes;
    struct dolby_ms12_desc *ms12 = (struct dolby_ms12_desc *)sm->mixerData;
    return 0;
}
#endif
/* mixing only support pcm 16&32 format now */
static int newSubMixingFactory(
            struct subMixing **smixer,
            enum MIXER_TYPE type,
            void *data)
{
    (void *)data;
    struct subMixing *sm = NULL;
    int res = 0;

    AM_LOGI("type %d", type);
    sm = aml_audio_calloc(1, sizeof(struct subMixing));
    R_CHECK_POINTER_LEGAL(-ENOMEM, sm, "No mem!");

    switch (type) {
    case MIXER_LPCM:
        sm->type = MIXER_LPCM;
        strncpy(sm->name, "LPCM", 16);
        //sm->writeMain = mainWritePCM;
        //sm->writeSys = sysWritePCM;
        //sm->mixerData = data;
        break;
    case MIXER_MS12:
        sm->type = MIXER_MS12;
        strncpy(sm->name, "MS12", 16);
        //sm->writeMain = mainWriteMS12;
        //sm->writeSys = sysWriteMS12;
        //sm->mixerData = data;
        break;
    default:
        AM_LOGE("type %d not support!", type);
        break;
    };

    *smixer = sm;
exit:
    return res;
};

static void deleteSubMixing(struct subMixing *sm)
{
    AM_LOGI("++");
    if (sm != NULL) {
        aml_audio_free(sm);
    }
}

int initHalSubMixing(struct subMixing **smixer,
        enum MIXER_TYPE type,
        struct aml_audio_device *adev,
        bool isTV)
{
    int ret = 0;

    ALOGI("type %d, isTV %d", type, isTV);
    R_CHECK_POINTER_LEGAL(-EINVAL, smixer, "");
    ret = newSubMixingFactory(smixer, type, NULL);
    R_CHECK_RET(ret, "fail to new mixer");
    ret = initSubMixingOutput(*smixer, adev);
    if (ret < 0) {
        AM_LOGE("fail to init mixer");
        goto err1;
    }
    return 0;
err1:
    deleteSubMixing(*smixer);
    return ret;
}

int deleteHalSubMixing(struct subMixing *smixer)
{
    releaseSubMixingOutput(smixer);
    deleteSubMixing(smixer);
    return 0;
}
#if 0
int mainWritePCM(
        struct subMixing *mixer,
        void *buf,
        size_t bytes)
{
    (void *)buf;
    (void *)bytes;
    struct amlAudioMixer *am = (struct amlAudioMixer *)mixer->mixerData;
    return 0;
}

int sysWritePCM(
        struct subMixing *mixer,
        void *buf,
        size_t bytes)
{
    (void *)buf;
    (void *)bytes;
    struct amlAudioMixer *am = (struct amlAudioMixer *)mixer->mixerData;
    return 0;
}

int subWrite(
        struct subMixing *mixer,
        void *buf,
        size_t bytes)
{
    (void *)buf;
    (void *)bytes;
    struct amlAudioMixer *am = (struct amlAudioMixer *)mixer->mixerData;
    return 0;
}
#endif

int outSubMixingWrite(
            struct audio_stream_out *stream,
            const void *buf,
            size_t bytes)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct subMixing *sm = adev->sm;
    struct amlAudioMixer *audio_mixer = sm->mixerData;
    char *buffer = (char *)buf;

    sm->write(sm, buffer, bytes);
    return 0;
}
ssize_t mixer_main_buffer_write_sm (struct audio_stream_out *stream, const void *buffer,
                                 size_t bytes)
{
    struct aml_stream_out       *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device     *adev = aml_out->dev;
    ssize_t                     write_bytes = 0;

    if (buffer == NULL || bytes == 0) {
        AM_LOGW("stream:%p, buffer is null, or bytes:%zu invalid", stream, bytes);
        return -1;
    }

    if (adev->debug_flag) {
        AM_LOGD("io %d: out:%p out_device:%#x bytes:%zu format:%s(%#x) hw_sync:%d", aml_out->io_handle,
            stream, aml_out->out_device, bytes, audioFormat2Str(aml_out->hal_internal_format),
            aml_out->hal_internal_format, aml_out->hw_sync_mode);
    }

    if (popcount(adev->usecase_masks & SUBMIX_USECASE_MASK) > 1) {
        AM_LOGE("use mask:%#x, not support two direct stream", adev->usecase_masks);
        return bytes;
    }

    /* handle HWSYNC audio data*/
    /* tv ddp hwsync : hwsync header had been removed by "mixer_main_buffer_write" */
    if (aml_out->hw_sync_mode && !aml_out->hwsync_header_stripped) {
        write_bytes = out_write_hwsync_lpcm(stream, buffer, bytes);
    } else {
        write_bytes = out_write_direct_pcm(stream, buffer, bytes);
    }

    if (write_bytes > 0) {
        aml_out->input_bytes_size += write_bytes;
    }

    if (aml_out->stream_status == STREAM_STANDBY) {
        aml_out->stream_status = STREAM_HW_WRITING;
    }

    return bytes;
}

ssize_t mixer_aux_buffer_write_sm(struct audio_stream_out *stream, const void *buffer,
                               size_t bytes)
{
    struct aml_stream_out       *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device     *adev = aml_out->dev;
    struct subMixing            *sm = adev->sm;
    size_t                      in_frames = bytes / audio_stream_out_frame_size(stream);
    ssize_t                     bytes_written = 0;
#ifdef DEBUG_TIME
    uint64_t                    us_since_last_write = 0;
    struct timespec             tval_begin, tval_end;
    int64_t                     throttle_timeus = 0;
    clock_gettime(CLOCK_MONOTONIC, &tval_begin);
#endif

    if (buffer == NULL || bytes == 0) {
        AM_LOGW("stream:%p, buffer is null, or bytes:%zu invalid", stream, bytes);
        return -1;
    }

    if (adev->debug_flag) {
        AM_LOGD("io %d: out:%p, out_device:%#x, bytes:%zu", aml_out->io_handle,
            stream, aml_out->out_device, bytes);
    }

    if (adev->cur_out_devices != aml_out->out_device) {
        AM_LOGD("io %d: out:%p, switch from device:%#x to device:%#x", aml_out->io_handle,
             stream, adev->cur_out_devices, aml_out->out_device);
        aml_out->out_device = adev->cur_out_devices;
        aml_out->stream.common.standby(&aml_out->stream.common);
        goto exit;
    } else if (aml_out->out_device == 0) {
        AM_LOGW("output device is none");
        goto exit;
    }

    /* this process will lead audio late about 30ms delay. */
    if (aml_out->standby) {
        char *padding_buf = NULL;
        int padding_bytes = MIXER_FRAME_COUNT * 4 * MIXER_OUT_FRAME_SIZE;
        if (aml_out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP)
            padding_bytes = 0;

        if (adev->is_netflix) {
            padding_bytes = MIXER_FRAME_COUNT * 4 * 2;
            ALOGI("%s : netflix case, padding_bytes change to 16ms", __func__);
        }

        aml_out->audio_data_handle_state = AUDIO_DATA_HANDLE_START;
        //set_thread_affinity();
        init_mixer_input_port(sm->mixerData, &aml_out->audioCfg, aml_out->flags,
            on_notify_cbk, aml_out, on_input_avail_cbk, aml_out,
            NULL, NULL, 1.0);

        AM_LOGI("io %d: out:%p usecase:%s standby to unstandby, input port:%s", aml_out->io_handle,
            stream, usecase2Str(aml_out->usecase),
            mixerInputType2Str(get_input_port_type(&aml_out->audioCfg, aml_out->flags)));
        aml_out->standby = false;
        adev->debug_flag = aml_audio_get_debug_flag();
#ifdef ENABLE_AEC_APP
        aec_set_spk_running(adev->aec, true);
#endif
        /* start padding zero to fill padding data to alsa buffer*/
        padding_buf = aml_audio_calloc(1, MIXER_FRAME_COUNT * 4);
        R_CHECK_POINTER_LEGAL(-ENOMEM, padding_buf, "no memory");
        mixer_set_padding_size(sm->mixerData, aml_out->inputPortID, padding_bytes);
        while (padding_bytes > 0) {
            AM_LOGI("padding_bytes %d", padding_bytes);
            aml_out_write_to_mixer(stream, padding_buf, MIXER_FRAME_COUNT * 4);
            padding_bytes -= MIXER_FRAME_COUNT * 4;
        }
        aml_audio_free(padding_buf);
    }

    bytes_written = aml_out_write_to_mixer(stream, buffer, bytes);

#ifdef DEBUG_TIME
    clock_gettime(CLOCK_MONOTONIC, &tval_end);
    us_since_last_write = (tval_end.tv_sec - aml_out->timestamp.tv_sec) * 1000000 +
            (tval_end.tv_nsec - aml_out->timestamp.tv_nsec) / 1000;
    int used_this_write = (tval_end.tv_sec - tval_begin.tv_sec) * 1000000 +
            (tval_end.tv_nsec - tval_begin.tv_nsec) / 1000;
    int target_us = in_frames * 1000 / 48;

    AM_LOGV("time spent on write %" PRId64 " us, written %d", us_since_last_write, bytes_written);
    AM_LOGV("used_this_write %d us, target %d us", used_this_write, target_us);
    throttle_timeus = target_us - us_since_last_write;

    if (throttle_timeus > 0 && throttle_timeus < 200000) {
        AM_LOGV("throttle time %" PRId64 " us", throttle_timeus);
        if (throttle_timeus > 1800 && aml_out->us_used_last_write < (uint64_t)target_us/2) {
            usleep(throttle_timeus - 1800);
            AM_LOGV("actual throttle %" PRId64 " us3, since last %" PRId64 " us",
                    throttle_timeus, us_since_last_write);
        } else {
            AM_LOGV("%" PRId64 " us, but un-throttle", throttle_timeus);
        }
    } else if (throttle_timeus != 0) {
        AM_LOGV("invalid throttle time %" PRId64 " us, us since last %" PRId64 " us \n\n", throttle_timeus, us_since_last_write);
    }
    aml_out->us_used_last_write = us_since_last_write;
#endif
exit:
    pthread_mutex_lock(&aml_out->apts_update_lock);
    aml_out->frame_write_sum += in_frames;
    aml_out->last_frames_position = aml_out->frame_write_sum;
    clock_gettime(CLOCK_MONOTONIC, &aml_out->timestamp);
    aml_out->lasttimestamp.tv_sec = aml_out->timestamp.tv_sec;
    aml_out->lasttimestamp.tv_nsec = aml_out->timestamp.tv_nsec;
    pthread_mutex_unlock(&aml_out->apts_update_lock);
    AM_LOGV("frame write sum %" PRId64 "", aml_out->frame_write_sum);

    if (bytes > 0) {
        aml_out->input_bytes_size += bytes;
    }

    return bytes;
}

ssize_t mixer_mmap_buffer_write_sm(struct audio_stream_out *stream, const void *buffer, size_t bytes)
{
   struct aml_stream_out    *aml_out = (struct aml_stream_out *) stream;
   struct aml_audio_device  *adev = aml_out->dev;
   struct subMixing         *pstSubMixing = adev->sm;
   ssize_t                  bytes_written = 0;

   if (adev->debug_flag) {
       AM_LOGD("io %d: out:%p, out_device:%#x, bytes:%zu", aml_out->io_handle,
           stream, aml_out->out_device, bytes);
   }

   if (adev->cur_out_devices != aml_out->out_device) {
       AM_LOGD("io %d: out:%p, switch from device:%#x to device:%#x", aml_out->io_handle,
            stream, adev->cur_out_devices, aml_out->out_device);
       aml_out->out_device = adev->cur_out_devices;
       aml_out->stream.common.standby(&aml_out->stream.common);
       return bytes;
   } else if (aml_out->out_device == 0) {
       AM_LOGW("output device is none");
       return bytes;
   }

   if (aml_out->standby) {
       init_mixer_input_port(pstSubMixing->mixerData, &aml_out->audioCfg, aml_out->flags,
           on_notify_cbk, aml_out, on_input_avail_cbk, aml_out, NULL, NULL, 1.0);
       AM_LOGI("io %d: out:%p usecase:%s standby to unstandby, port_index:%s", aml_out->io_handle,
        aml_out, usecase2Str(aml_out->usecase), mixerInputType2Str(get_input_port_type(&aml_out->audioCfg, aml_out->flags)));
       aml_out->standby = false;
   }

   bytes_written = aml_out_write_to_mixer(stream, buffer, bytes);
   if (bytes_written != bytes) {
       AM_LOGW("write to mixer error, written:%zd, bytes:%zu", bytes_written, bytes);
   }

exit:
   return bytes_written;
}

/* must be called with hw device mutexes locked */
static int usecase_change_validate_l_sm(struct aml_stream_out *aml_out, bool is_standby)
{
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct subMixing *sm = aml_dev->sm;
    struct amlAudioMixer *audio_mixer = sm->mixerData;
    bool hw_mix;

    if (is_standby) {
        AM_LOGI("io %d: out:%p cur dev masks:%#x, delete out usecase:%s", aml_out->io_handle, aml_out,
            aml_dev->usecase_masks, usecase2Str(aml_out->usecase));
        /**
         * If called by standby, reset out stream's usecase masks and clear the aml_dev usecase masks.
         * So other active streams could know that usecase have been changed.
         * But keep it's own usecase if out_write is called in the future to exit standby mode.
         */
        aml_out->dev_usecase_masks = 0;
        aml_out->write = NULL;
        aml_dev->usecase_cnt[aml_out->usecase]--;
        if (aml_dev->usecase_cnt[aml_out->usecase] <= 0) {
            AM_LOGI("io %d: out:%p standby unmask usecase %s", aml_out->io_handle, aml_out,usecase2Str(aml_out->usecase));
            aml_dev->usecase_masks &= ~(1 << aml_out->usecase);
        }
        return 0;
    }

    /* No usecase changes, do nothing */
    if (((aml_dev->usecase_masks == aml_out->dev_usecase_masks) && aml_dev->usecase_masks) && (aml_dev->continuous_audio_mode == 0)) {
        if ((STREAM_PCM_NORMAL == aml_out->usecase) && (aml_out->write_func == PROCESS_BUFFER_WRITE)) {
            AM_LOGE("io %d: out:%p wrong write function reset it", aml_out->io_handle, aml_out);
        } else {
            return 0;
        }
    }

    AM_LOGV("++dev masks:%#x, out masks:%#x, out usecase:%s",
           aml_dev->usecase_masks, aml_out->dev_usecase_masks, usecase2Str(aml_out->usecase));

    /* check the usecase validation */
    if (popcount(aml_dev->usecase_masks & SUBMIX_USECASE_MASK) > 1) {
        AM_LOGW("io %d: out:%p invalid dev masks:%#x, out usecase %s!", aml_out->io_handle, aml_out,
              aml_dev->usecase_masks, usecase2Str(aml_out->usecase));
        //return -EINVAL;
    }

    if (((aml_dev->continuous_audio_mode == 1) && (aml_dev->debug_flag > 1)) || \
        (aml_dev->continuous_audio_mode == 0)) {
        AM_LOGI("++++ out:%p continuous:%d dev masks:%#x, out masks:%#x, out usecase %s", aml_out,
            aml_dev->continuous_audio_mode, aml_dev->usecase_masks,
            aml_out->dev_usecase_masks, usecase2Str(aml_out->usecase));
    }

    /* new output case entered, so no masks has been set to the out stream */
    if (!aml_out->dev_usecase_masks) {
        aml_dev->usecase_cnt[aml_out->usecase]++;
        AM_LOGI("io %d: out:%p add usecase %s, cnt %d", aml_out->io_handle, aml_out, usecase2Str(aml_out->usecase),
                aml_dev->usecase_cnt[aml_out->usecase]);
        if ((1 << aml_out->usecase) & aml_dev->usecase_masks) {
            AM_LOGW("io %d: out:%p usecase: %s already exists!!", aml_out->io_handle, aml_out, usecase2Str(aml_out->usecase) );
            //return -EINVAL;
        }

        if (popcount((aml_dev->usecase_masks | (1 << aml_out->usecase)) & SUBMIX_USECASE_MASK) > 1) {
            AM_LOGE("io %d: out:%p usecase masks:%#x, couldn't add new out usecase %s!", aml_out->io_handle, aml_out,
                  aml_dev->usecase_masks, usecase2Str(aml_out->usecase));
            return -EINVAL;
        }
        if (aml_dev->usecase_cnt[aml_out->usecase] == 1) {
            AM_LOGD("io %d: out:%p cur dev masks:%#x, add out usecase:%s", aml_out->io_handle, aml_out,
                aml_dev->usecase_masks, usecase2Str(aml_out->usecase));
            /* add the new output usecase to aml_dev usecase masks */
            aml_dev->usecase_masks |= 1 << aml_out->usecase;
        }
    }

    if (STREAM_PCM_NORMAL == aml_out->usecase) {
        aml_out->write = mixer_aux_buffer_write_sm;
        aml_out->write_func = MIXER_AUX_BUFFER_WRITE_SM;
        AM_LOGV("mixer_aux_buffer_write_sm !");
    } else if (STREAM_PCM_MMAP == aml_out->usecase) {
        aml_out->write = mixer_mmap_buffer_write_sm;
        aml_out->write_func = MIXER_MMAP_BUFFER_WRITE_SM;
        AM_LOGV("mixer_mmap_buffer_write_sm !");
    } else {
        aml_out->write = mixer_main_buffer_write_sm;
        aml_out->write_func = MIXER_MAIN_BUFFER_WRITE_SM;
        AM_LOGV("mixer_main_buffer_write_sm !");
    }

    /* store the new usecase masks in the out stream */
    aml_out->dev_usecase_masks = aml_dev->usecase_masks;
    if (((aml_dev->continuous_audio_mode == 1) && (aml_dev->debug_flag > 1)) || \
        (aml_dev->continuous_audio_mode == 0))
        AM_LOGI("---- out:%p continuous:%d dev masks:%#x, out masks:%#x, out usecase %s", aml_out,
            aml_dev->continuous_audio_mode, aml_dev->usecase_masks, aml_out->dev_usecase_masks, usecase2Str(aml_out->usecase));
    return 0;
}

/* out_write_submixing entrance: every write to submixing goes in here. */
static ssize_t out_write_subMixingPCM(struct audio_stream_out *stream,
                      const void *buffer,
                      size_t bytes)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    ssize_t ret = 0;
    //write_func  write_func_p = NULL;

    AM_LOGV("out_stream(%p) position(%zu)", stream, bytes);
    aml_audio_trace_int("out_write_subMixingPCM", bytes);

    if (aml_audio_trace_debug_level() > 0) {
        if (false == aml_out->pause_status  &&  aml_out->write_count < 2) {
            aml_out->write_time = aml_audio_get_systime() / 1000; //us --> ms
            AM_LOGD("out_stream(%p) bytes(%zu), write_time:%" PRIu64 ", count:%d",
                       stream, bytes, aml_out->write_time, aml_out->write_count);
        }
    }
    aml_out->write_count++;

    check_write_time((struct audio_stream_out *)aml_out, bytes);

    if (aml_out->standby) {
        uint8_t *temp_buf = (uint8_t *)buffer;
        bool is_hwsync_header = hwsync_header_valid(temp_buf);
        //tunnel stream and hwsync is null, prepare the tunnel resource.
        if (is_hwsync_header && aml_out->hwsync == NULL) {
            output_stream_hwsync_prepare(aml_out, adev->hw_sync_id);
            if (aml_out->hwsync == NULL) {
                ALOGE("%s, malloc hwsync failed", __func__);
                return -ENOMEM;
            }
        }

        /* do fade in if former standby fadeout is done */
        if (is_output_device_muted(adev, AUDIO_DEVICE_OUT_SPEAKER, true))
            set_output_device_mute(adev, AUDIO_DEVICE_OUT_SPEAKER, false, true);
    }

    /**
     * deal with the device output changes
     * pthread_mutex_lock(&aml_out->lock);
     * out_device_change_validate_l(aml_out);
     * pthread_mutex_unlock(&aml_out->lock);
     */
    pthread_mutex_lock(&adev->lock);
    ret = usecase_change_validate_l_sm(aml_out, false);
    if (ret < 0) {
        AM_LOGE("failed");
        pthread_mutex_unlock(&adev->lock);
        aml_audio_trace_int("out_write_subMixingPCM", 0);
        return ret;
    }
    adev->active_outputs[aml_out->usecase] = aml_out;
    pthread_mutex_unlock(&adev->lock);
    if (aml_out->write) {
        ret = aml_out->write(stream, buffer, bytes);
    } else {
        AM_LOGE("NULL write function");
    }
    if (ret > 0) {
        aml_out->total_write_size += ret;
    }
    if (adev->debug_flag > 1) {
        AM_LOGI("- io %d: out:%p write_count:%d ret %zd total:%"PRIu64"", aml_out->io_handle, stream, aml_out->write_count,
            ret, aml_out->total_write_size);
    }
    aml_audio_trace_int("out_write_subMixingPCM", 0);
    return ret;
}

int out_standby_subMixingPCM_l(struct audio_stream *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct subMixing *sm = adev->sm;
    struct amlAudioMixer *audio_mixer = sm->mixerData;
    ssize_t ret = 0;

#ifdef ENABLE_AEC_APP
    aec_set_spk_running(adev->aec, false);
#endif
    if (aml_out->inputPortID != -1) {
        delete_mixer_input_port(audio_mixer, aml_out->inputPortID);
        aml_out->inputPortID = -1;
    }

    if (aml_out->hwsync_extractor) {
        delete_hw_avsync_header_extractor(aml_out->hwsync_extractor);
        aml_out->hwsync_extractor = NULL;
    }

    if (adev->debug_flag > 1) {
        AM_LOGI("- io %d: out:%p ret:%zd, %"PRIu64"", aml_out->io_handle, stream, ret, aml_out->total_write_size);
    }
    return 0;
}

int out_standby_subMixingPCM(struct audio_stream *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct subMixing *sm = adev->sm;
    struct amlAudioMixer *audio_mixer = sm->mixerData;
    ssize_t ret = 0;

    AM_LOGD("io %d: out:%p usecase:%s", aml_out->io_handle, stream, usecase2Str(aml_out->usecase));
    /**
     * deal with the device output changes
     * pthread_mutex_lock(&aml_out->lock);
     * out_device_change_validate_l(aml_out);
     * pthread_mutex_unlock(&aml_out->lock);
     */

    aml_audio_trace_int("out_standby_subMixingPCM", 1);
    pthread_mutex_lock(&adev->lock);
    if (aml_out->standby) {
        goto exit;
    }

    ret = usecase_change_validate_l_sm(aml_out, true);
    if (ret < 0) {
        AM_LOGE("failed");
        goto exit;
    }

    out_standby_subMixingPCM_l(stream);
    aml_out->stream_status = STREAM_STANDBY;
    aml_out->standby = true;
#ifdef ENABLE_AEC_APP
    aec_set_spk_running(adev->aec, false);
#endif
    if (aml_out->inputPortID != -1) {
        delete_mixer_input_port(audio_mixer, aml_out->inputPortID);
        aml_out->inputPortID = -1;
    }
    if (aml_out->hwsync_extractor) {
        delete_hw_avsync_header_extractor(aml_out->hwsync_extractor);
        aml_out->hwsync_extractor = NULL;
    }

    if (adev->debug_flag > 1) {
        AM_LOGI("- out:%p ret %zd %"PRIu64"", stream, ret, aml_out->total_write_size);
    }
exit:
    pthread_mutex_unlock(&adev->lock);
    aml_audio_trace_int("out_standby_subMixingPCM", 0);
    return ret;
}

static int out_pause_subMixingPCM(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct subMixing *sm = aml_dev->sm;
    struct amlAudioMixer *audio_mixer = NULL;

    AM_LOGI("+ io %d: out:%p, standby:%d, pause status:%d, usecase:%s", aml_out->io_handle,
            aml_out, aml_out->standby, aml_out->pause_status, usecase2Str(aml_out->usecase));

    aml_audio_trace_int("out_pause_subMixingPCM", 1);
    aml_out->write_count = 0;
    if (aml_audio_trace_debug_level() > 0)
    {
        aml_out->pause_time = aml_audio_get_systime() / 1000; //us --> ms
        if (aml_out->pause_time > aml_out->write_time && (aml_out->pause_time - aml_out->write_time < 5*1000)) { //continually write time less than 5s, audio gap
            AM_LOGD("AudioGap pause_time:%" PRIu64 ",  diff_time(pause - write):%" PRIu64 " ms",
                   aml_out->pause_time, aml_out->pause_time - aml_out->write_time);
        } else {
            AM_LOGD("-------- pause ----------");
        }
    }

    if (aml_out->standby || aml_out->pause_status) {
        AM_LOGW("stream already paused");
        aml_audio_trace_int("out_pause_subMixingPCM", 0);
        return INVALID_STATE;
    }

    if (sm->type != MIXER_LPCM) {
        AM_LOGW("sub mixing type not pcm, type is %d", sm->type);
        aml_audio_trace_int("out_pause_subMixingPCM", 0);
        return 0;
    }

    if (aml_out->inputPortID != -1) {
        audio_mixer = sm->mixerData;
        send_mixer_inport_message(audio_mixer, aml_out->inputPortID, MSG_PAUSE);
    }

    aml_out->pause_status = true;
    AM_LOGI("- out:%p", aml_out);
    aml_audio_trace_int("out_pause_subMixingPCM", 0);
    return 0;
}

static int out_resume_subMixingPCM(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct subMixing *sm = aml_dev->sm;
    struct amlAudioMixer *audio_mixer = NULL;
    int ret = 0;

    AM_LOGI("+ io %d: out:%p, standby:%d, pause status:%d, usecase:%s", aml_out->io_handle,
            aml_out, aml_out->standby,  aml_out->pause_status, usecase2Str(aml_out->usecase));
    aml_audio_trace_int("out_resume_subMixingPCM", 1);
    if (!aml_out->pause_status) {
        AM_LOGW("steam not in pause status");
        aml_audio_trace_int("out_resume_subMixingPCM", 0);
        return INVALID_STATE;
    }

    if (sm->type != MIXER_LPCM) {
        AM_LOGW("sub mixing type not pcm, type is %d", sm->type);
        aml_audio_trace_int("out_resume_subMixingPCM", 0);
        return 0;
    }

    if (aml_out->inputPortID != -1) {
        audio_mixer = sm->mixerData;
        send_mixer_inport_message(audio_mixer, aml_out->inputPortID, MSG_RESUME);
    }

    aml_out->pause_status = false;
    aml_out->need_first_sync = true;
    AM_LOGI("- out:%p", aml_out);
    aml_audio_trace_int("out_resume_subMixingPCM", 0);
    return 0;
}

/* If supported, a stream should always succeed to flush */
static int out_flush_subMixingPCM(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct subMixing *sm = aml_dev->sm;
    struct amlAudioMixer *audio_mixer = NULL;
    int ret = 0;

    AM_LOGI("+ io %d: out:%p, standby:%d, pause status:%d, usecase:%s", aml_out->io_handle,
            aml_out,  aml_out->standby, aml_out->pause_status, usecase2Str(aml_out->usecase));

    aml_audio_trace_int("out_flush_subMixingPCM", 1);
    if (sm->type != MIXER_LPCM) {
        AM_LOGW("sub mixing type not pcm, type is %d", sm->type);
        aml_audio_trace_int("out_flush_subMixingPCM", 0);
        return 0;
    }
    aml_out->frame_write_sum  = 0;
    aml_out->last_frames_position = 0;
    aml_out->spdif_enc_init_frame_write_sum =  0;
    aml_out->frame_skip_sum = 0;
    aml_out->skip_frame = 0;
    aml_out->input_bytes_size = 0;
    //aml_out->pause_status = false;
    if (aml_out->pause_status) {
        struct meta_data_list *mdata_list;
        struct listnode *item;

        //mixer_flush_inport(audio_mixer, out->port_index);
        if (aml_out->hw_sync_mode) {
            pthread_mutex_lock(&aml_out->mdata_lock);
            while (!list_empty(&aml_out->mdata_list)) {
                item = list_head(&aml_out->mdata_list);
                mdata_list = node_to_item(item, struct meta_data_list, list);
                list_remove(item);
                aml_audio_free(mdata_list);
            }
            pthread_mutex_unlock(&aml_out->mdata_lock);
        }
        if (aml_out->inputPortID != -1) {
            audio_mixer = sm->mixerData;
            send_mixer_inport_message(audio_mixer, aml_out->inputPortID, MSG_FLUSH);
        }
        if (!aml_out->standby)
            flush_hw_avsync_header_extractor(aml_out->hwsync_extractor);
        //mixer_set_inport_state(audio_mixer, out->port_index, FLUSHING);
        aml_out->last_frames_position = 0;
        aml_out->first_pts_set = false;
        aml_out->need_first_sync = false;
        aml_out->last_pts = 0;
        aml_out->last_payload_offset = 0;
        //aml_out->pause_status = false;
        //aml_out->standby = true;
    } else {
        AM_LOGW("Need check this case!");
        aml_audio_trace_int("out_flush_subMixingPCM", 0);
        return 0;
    }

    AM_LOGI("- out:%p", aml_out);
    aml_audio_trace_int("out_flush_subMixingPCM", 0);
    return 0;
}

int subMixingOutputRestart(struct aml_audio_device *adev)
{
    struct subMixing *sm = NULL;
    struct amlAudioMixer *audio_mixer = NULL;

    R_CHECK_POINTER_LEGAL(-EINVAL, adev, "");
    sm = adev->sm;
    R_CHECK_POINTER_LEGAL(-EINVAL, sm, "");
    audio_mixer = sm->mixerData;
    R_CHECK_POINTER_LEGAL(-EINVAL, audio_mixer, "");

    return mixer_outport_pcm_restart(audio_mixer);
}

int switchNormalStream(struct aml_stream_out *aml_out, bool on)
{
    AM_LOGI("+ io %d: out:%p, on:%d", aml_out->io_handle, aml_out, on);
    R_CHECK_POINTER_LEGAL(-EINVAL, aml_out, "");
    if (!aml_out->is_normal_pcm) {
        AM_LOGE("not normal pcm stream");
        return -EINVAL;
    }
    if (on) {
        initSubMixingInputPcm(&aml_out->out_cfg, aml_out);
        aml_out->stream.write = out_write_subMixingPCM;
        aml_out->stream.common.standby = out_standby_subMixingPCM;
        out_standby_subMixingPCM((struct audio_stream *)aml_out);
    } else {
        aml_out->stream.write = out_write_new;
        aml_out->stream.common.standby = out_standby_new;
        deleteSubMixingInputPcm(aml_out);
        out_standby_new((struct audio_stream *)aml_out);
    }

    return 0;
}

void subMixingDump(int s32Fd, const struct aml_audio_device *pstAmlDev)
{
    if (NULL == pstAmlDev) {
        dprintf(s32Fd, "[AML_HAL] %s:%d device is NULL !\n", __func__, __LINE__);
        return;
    }
    dprintf(s32Fd, "\n-------------[AML_HAL] subMix -----------------------------------\n");
    mixer_dump(s32Fd, pstAmlDev);
}

int subMixingSetKaraoke(struct aml_audio_device *adev, struct kara_manager *kara)
{
    struct subMixing *sm = adev->sm;
    struct amlAudioMixer *audio_mixer = sm->mixerData;

    return mixer_set_karaoke(audio_mixer, kara);
}

static int subMixingOutMsg(struct aml_audio_device *adev, PORT_MSG msg, void *info, int info_len)
{
    struct subMixing *sm = adev->sm;
    struct amlAudioMixer *audio_mixer = NULL;
    int ret = 0;

    audio_mixer = sm->mixerData;
    send_mixer_outport_message(audio_mixer, MIXER_OUTPUT_PORT_STEREO_PCM, msg, info, info_len);

    return 0;
}

int subMixingSetSinkGain(struct aml_audio_device *adev, void *sink_gain)
{
    return subMixingOutMsg(adev, MSG_SINK_GAIN, &sink_gain, sizeof(sink_gain));
}

int subMixingSetEQData(struct aml_audio_device *adev, void *eq_data)
{
    return subMixingOutMsg(adev, MSG_EQ_DATA, &eq_data, sizeof(eq_data));
}

int subMixingSetSrcGain(struct aml_audio_device *adev, float gain)
{
    return subMixingOutMsg(adev, MSG_SRC_GAIN, &gain, sizeof(gain));
}

int subMixingSetAudioPostprocess(struct aml_audio_device *adev, void **postprocess)
{
    return subMixingOutMsg(adev, MSG_EFFECT, postprocess, sizeof(void *));
}


int subMixingEnableMultiChOutput(struct aml_audio_device *adev, bool enable)
{
    int ret = 0;
    struct subMixing *sm = NULL;
    struct amlAudioMixer *audio_mixer = NULL;

    R_CHECK_POINTER_LEGAL(-EINVAL, adev, "");
    sm = adev->sm;
    R_CHECK_POINTER_LEGAL(-EINVAL, sm, "");
    audio_mixer = sm->mixerData;
    R_CHECK_POINTER_LEGAL(-EINVAL, audio_mixer, "");

    mixer_enable_multich_output(audio_mixer, enable);
    return ret;
}

