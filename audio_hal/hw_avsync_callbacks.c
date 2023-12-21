
#define LOG_TAG "audio_hw_hal_hwsync"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <inttypes.h>
#include "hw_avsync_callbacks.h"

#include "audio_hwsync.h"
#include "audio_hwsync_wrap.h"

#include "audio_hw.h"
#include "audio_hw_utils.h"
#include "aml_malloc_debug.h"
#include "aml_audio_ms12_sync.h"

enum hwsync_status pcm_check_hwsync_status(uint apts_gap)
{
    enum hwsync_status sync_status;

    if (apts_gap < APTS_DISCONTINUE_THRESHOLD_MIN)
        sync_status = CONTINUATION;
    else if (apts_gap > APTS_DISCONTINUE_THRESHOLD)
        sync_status = RESYNC;
    else
        sync_status = ADJUSTMENT;

    return sync_status;
}

enum hwsync_status pcm_check_hwsync_status1(uint32_t pcr, uint32_t apts)
{
    uint32_t apts_gap = get_pts_gap(pcr, apts);
    enum hwsync_status sync_status;

    if (apts >= pcr) {
        if (apts_gap < APTS_DISCONTINUE_THRESHOLD_MIN_35MS)
            sync_status = CONTINUATION;
        else if (apts_gap > APTS_DISCONTINUE_THRESHOLD)
            sync_status = RESYNC;
        else
            sync_status = ADJUSTMENT;
    } else {
        if (apts_gap < APTS_DISCONTINUE_THRESHOLD_MIN)
            sync_status = CONTINUATION;
        else if (apts_gap > APTS_DISCONTINUE_THRESHOLD)
            sync_status = RESYNC;
        else
            sync_status = ADJUSTMENT;
    }
    return sync_status;
}

int on_meta_data_cbk(void *cookie,
        uint64_t offset, struct hw_avsync_header *header, int *delay_ms)
{
    struct aml_stream_out *out = cookie;
    struct meta_data_list *mdata_list = NULL;
    struct listnode *item;
    uint64_t pts64 = 0;
    uint64_t pts = 0;
    uint64_t aligned_offset = 0;
    uint32_t frame_size = 0;
    uint32_t sample_rate = 48000;
    uint64_t pts_delta = 0;
    int ret = 0;
    uint64_t pcr = 0;
    int pcr_pts_gap = 0;
    int insert_size = 0;
    int32_t tuning_latency = aml_audio_get_hwsync_latency_offset(false);

    if (!cookie || !header) {
        ALOGE("NULL pointer");
        return -EINVAL;
    }
    ALOGV("%s(), pout %p", __func__, out);

    if (out->dev && out->dev->is_netflix) {
        tuning_latency = aml_audio_get_nonms12_tunnel_latency(&out->stream, AUDIO_FORMAT_PCM_16_BIT)/48;
    }

    frame_size = audio_stream_out_frame_size(&out->stream);
    //sample_rate = out->audioCfg.sample_rate;
    if (out->audioCfg.sample_rate != sample_rate)
        offset = (offset * 1000) /(1000 * sample_rate / out->audioCfg.sample_rate);

    pthread_mutex_lock(&out->mdata_lock);
    if (!list_empty(&out->mdata_list)) {
        item = list_head(&out->mdata_list);
        mdata_list = node_to_item(item, struct meta_data_list, list);
        if (!mdata_list) {
            ALOGE("%s(), fatal err, no meta data!", __func__);
            ret = -EINVAL;
            goto err_lock;
        }
        header->frame_size = mdata_list->mdata.frame_size;
        header->pts = mdata_list->mdata.pts;
        aligned_offset = mdata_list->mdata.payload_offset;
        if (out->debug_stream) {
            ALOGV("%s(), offset %" PRId64 ", checkout payload offset %" PRId64 "",
                        __func__, offset, mdata_list->mdata.payload_offset);
            ALOGV("%s(), frame_size %d, pts %" PRId64 "ms",
                        __func__, header->frame_size, header->pts/1000000);
        }
    }
    ALOGV("offset =%" PRId64 " aligned_offset=%" PRId64 " frame size=%d samplerate=%d", offset, aligned_offset,frame_size,sample_rate);
    if (offset >= aligned_offset && mdata_list) {
        pts = header->pts;
        pts_delta = (offset - aligned_offset) * 1000000000LL/(frame_size * sample_rate);
        pts += pts_delta;
        out->last_pts = pts;
        out->last_payload_offset = offset;
        list_remove(&mdata_list->list);
        aml_audio_free(mdata_list);
        ALOGV("head pts =%" PRId64 " delta =%" PRId64 " pts =%" PRId64 " ",header->pts, pts_delta, pts);
    } else if ((offset > out->last_payload_offset) && out->last_pts != 0) {
        pts_delta = (offset - out->last_payload_offset) * 1000000000LL/(frame_size * sample_rate);
        pts = out->last_pts + pts_delta;
        ALOGV("last pts=%" PRId64 " delta=%" PRId64 " pts=%" PRId64 " ", out->last_pts, pts_delta, pts);
    } else {
        ret = -EINVAL;
        goto err_lock;
    }

    pts64 = pts / 1000000 * 90;
    pthread_mutex_unlock(&out->mdata_lock);

    /*if stream is already paused, we don't need to av sync, it may cause pcr reset*/
    if (out->pause_status) {
        ALOGW("%s(), write in pause status", __func__);
        if (out->hwsync && out->hwsync->use_mediasync) {
            if(out->first_pts_set == true)
                out->first_pts_set = false;
        }
        return -EINVAL;
    }
    if (!out->dev) {
          ALOGE("out->dev is NULL pointer");
          ret = -EINVAL;
          return ret;
    }

    if (out->dev->is_netflix) {
        if (!out->alsa_running_status && out->write_count < 20) {
            ALOGW("%s(), stream %p alsa is not running ...", __func__, out);
            return -EINVAL;
        }
    }

    if (out->hwsync && out->hwsync->use_mediasync) {
        if (!out->first_pts_set) {
            int32_t latency = 0;
            int delay_count = 0;
            hwsync_header_construct(header);
            latency = (int32_t)out_get_outport_latency((struct audio_stream_out *)out) * 90;
            ALOGD("%s(), out:%p latency %d, tuning_latency %d", __func__, out, latency/90, tuning_latency);
            latency += tuning_latency * 90;

            ALOGD("%s(), out:%p set media start pts %" PRId64 ", latency %d, last position %" PRId64 "",
                __func__, out, pts64, latency, out->last_frames_position);
            if (latency < 0) {
                pts64 += abs(latency);
            } else {
                if (pts64 < latency) {
                    ALOGI("pts = %" PRIu64 " latency=%d", pts64/90, latency);
                    return 0;
                }
                pts64 -= latency;
            }

             /*if the pts is zero, to avoid video pcr not set issue, we just set it as 1ms*/
            if (pts64 == 0) {
                pts64 = 1 * 90;
            }

            //ALOGI("%s =============== can drop============", __FUNCTION__);
            aml_hwsync_wait_video_start(out->hwsync);
            aml_hwsync_wait_video_drop(out->hwsync, pts64);
            aml_audio_hwsync_set_first_pts(out->hwsync, pts64);

            out->first_pts_set = true;
            //*delay_ms = 40;
            //aml_hwsync_reset_tsync_pcrscr(out->hwsync, pts64);
        } else {
            enum hwsync_status sync_status = CONTINUATION;
            struct hw_avsync_header_extractor *hwsync_extractor;
            struct aml_audio_device *adev = out->dev;
            uint64_t pcr = 0;
            uint32_t apts_gap;
            // adjust pts based on latency which is only the outport latency
            int32_t latency = (int32_t)out_get_outport_latency((struct audio_stream_out *)out) * 90;
            latency += tuning_latency * 90;
            // check PTS discontinue, which may happen when audio track switching
            // discontinue means PTS calculated based on first_apts and frame_write_sum
            // does not match the timestamp of next audio samples
            if (latency < 0) {
                pts64 += abs(latency);
            } else {
                if (pts64 > latency) {
                    pts64 -= latency;
                } else {
                    pts64 = 0;
                }
            }

            hwsync_extractor = out->hwsync_extractor;
        }

        {
            if (out->hwsync->hwsync_need_resume) {
                aml_hwsync_wrap_set_resume(out->hwsync);
                out->hwsync->hwsync_need_resume = false;
            }
        }

        /*if the pts is zero, to avoid video pcr not set issue, we just set it as 1ms*/
        if (pts64 == 0) {
            pts64 = 1 * 90;
        }

        aml_hwsync_wrap_get_pts(out->hwsync, &pcr);
        pcr_pts_gap = ((int)(pts64 - pcr)) / 90;

        {
            struct timespec current_timestamp;
            clock_gettime(CLOCK_MONOTONIC, &current_timestamp);
            int64_t time_diff = calc_time_interval_us(&out->last_avsync_timestamp, &current_timestamp);
            if (time_diff >= (TIME_DIFF_THRESHOLD * USEC_PER_SEC)) {
                ALOGI("[avsync, %p]tunnel pcm time_diff[%"PRIu64"]us status:%d start_pts[%"PRIu64"]ms, current_pts[%"PRIu64"]ms current_pcr[%"PRIu64"]ms diff[%d]ms",
                    out->hwsync,
                    time_diff,
                    out->stream_status,
                    out->hwsync->first_apts / 90,
                    pts64 / 90,
                    pcr / 90,
                    pcr_pts_gap);
                out->last_avsync_timestamp = current_timestamp;
            }
        }

        if (abs(pcr_pts_gap) > (APTS_DISCONTINUE_THRESHOLD_MIN_35MS/90) && pts64 > pcr && pcr != 0) {
            bool amaster_mode = true;
            aml_hwsync_wrap_is_amaster(out->hwsync, &amaster_mode);
            if (!amaster_mode) {
                ALOGE("%s not amaster mode", __func__);
                return 0;
            }
            int insert_size = 0;
            int frame_size = 4;
            if (audio_is_linear_pcm(out->hal_format) && out->hal_frame_size > 0) {
                frame_size = out->hal_frame_size;
            }
            insert_size = pcr_pts_gap * 48 * frame_size;
            insert_size = insert_size & (~63);
            ALOGI("%s(), pcrscr %" PRIu64 " ms adjusted_apts %" PRIu64 " ms", __func__, pcr/90, pts64/90);
            ALOGI("audio gap: pcr < apts %d ms, need insert data %d\n", pcr_pts_gap, insert_size);
            *delay_ms = pcr_pts_gap;
            out->is_insert_zero_data = true;
        } else {
            aml_hwsync_wrap_reset_pcrscr(out->hwsync, pts64);
            out->is_insert_zero_data = false;
        }

        if (abs(pcr_pts_gap) > 100) {
            ALOGI("[avsync, %p] tunnel pcm pts[%"PRIu64"]ms pcr[%"PRIu64"]ms diff[%d]ms need_insert[%d]bytes",
                out->hwsync,
                pts64/90,
                pcr/90,
                pcr_pts_gap,
                insert_size);
        }



        return 0;
    }


    /*old sync method*/
    if (!out->first_pts_set) {
        int32_t latency = 0;
        hwsync_header_construct(header);
        latency = (int32_t)out_get_outport_latency((struct audio_stream_out *)out) * 90;
        latency += tuning_latency * 90;
        ALOGD("%s(), set tsync start pts %" PRIu64 ", latency %d, last position %" PRId64 "",
            __func__, pts64, latency, out->last_frames_position);
        if (latency < 0) {
            pts64 += abs(latency);
        } else {
            if (pts64 < latency) {
                ALOGI("pts64 = %" PRIu64 " latency=%d", pts64/90, latency);
                return 0;
            }
            pts64 -= latency;
        }
        if (out->hwsync) {
            aml_hwsync_wrap_set_tsync_init(out->hwsync);
            aml_hwsync_wrap_set_start_pts(out->hwsync, pts64);
        }
        out->first_pts_set = true;
    } else {
        enum hwsync_status sync_status = CONTINUATION;
        struct hw_avsync_header_extractor *hwsync_extractor;
        struct aml_audio_device *adev = out->dev;
        uint32_t apts_gap;
        // adjust pts based on latency which is only the outport latency
        int32_t latency = (int32_t)out_get_outport_latency((struct audio_stream_out *)out) * 90;
        latency += tuning_latency * 90;
        // check PTS discontinue, which may happen when audio track switching
        // discontinue means PTS calculated based on first_apts and frame_write_sum
        // does not match the timestamp of next audio samples
        if (latency < 0) {
            pts64 += abs(latency);
        } else {
            if (pts64 > latency) {
                pts64 -= latency;
            } else {
                pts64 = 0;
            }
        }

        hwsync_extractor = out->hwsync_extractor;
        hwsync_extractor = out->hwsync_extractor;
        ret = aml_hwsync_wrap_get_tsync_pts_by_handle(adev->tsync_fd, &pcr);
        if (ret != 0) {
            ALOGE("%s() get tsync(fd %d) pts failed err %d",
                    __func__, adev->tsync_fd, ret);
        }
        if (out->debug_stream)
            ALOGD("%s()audio pts %" PRIu64 " ms, pcr %" PRIu64 " ms, latency %d ms, diff %" PRIu64 " ms",
                __func__, pts64/90, pcr/90, latency/90,
                (pts64 > pcr) ? (pts64 - pcr)/90 : (pcr - pts64)/90);
        apts_gap = get_pts_gap(pcr, pts64);
        if (out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
            sync_status = pcm_check_hwsync_status(apts_gap);
        } else {
            sync_status = pcm_check_hwsync_status1(pcr, pts64);
        }
        if (out->need_first_sync) {
            /*when resume, we need do exactly sync first*/
            out->need_first_sync = false;
            sync_status = ADJUSTMENT;
        } else if (pcr == 0) {
            /*during video stop, pcr has been reset by video
              we need ignore such pcr value*/
            ALOGI("pcr is reset by video");
            sync_status = CONTINUATION;
        }
        // limit the gap handle to 0.5~5 s.
        if (sync_status == ADJUSTMENT) {
            // two cases: apts leading or pcr leading
            // apts leading needs inserting frame and pcr leading needs discarding frame
            if (pts64 > pcr) {
                int insert_size = 0;
                int frame_size = 4;
                if (audio_is_linear_pcm(out->hal_format) && out->hal_frame_size > 0) {
                    frame_size = out->hal_frame_size;
                }

                insert_size = apts_gap / 90 * 48 * frame_size;
                insert_size = insert_size & (~63);
                ALOGI("%s(), pcrscr %" PRIu64 " ms adjusted_apts %" PRIu64 " ms", __func__, pcr/90, pts64/90);
                ALOGI("audio gap: pcr < apts %d ms, need insert data %d\n", apts_gap / 90, insert_size);
                *delay_ms = apts_gap / 90;
            } else {
                ALOGW("audio gap: pcr > apts %dms", apts_gap / 90);
                *delay_ms = -(int)apts_gap / 90;
                aml_hwsync_wrap_reset_pcrscr(out->hwsync, pts64);
            }
        } else if (sync_status == RESYNC){
            ALOGI("%s(), tsync -> reset pcrscr %" PRIu64 " ms -> %" PRIu64 " ms",
                    __func__, pcr/90, pts64/90);
            /*during video stop, pcr has been reset to 0 by video,
              we need ignore such pcr value*/
            if (pcr != 0) {
                int ret_val = aml_hwsync_wrap_reset_pcrscr(out->hwsync, pts64);
                if (ret_val < 0) {
                    ALOGE("aml_hwsync_wrap_reset_pcrscr,err: %s", strerror(errno));
                }
            }
        }
        {
            if (out->hwsync && out->hwsync->hwsync_need_resume) {
                aml_hwsync_wrap_set_resume(out->hwsync);
                out->hwsync->hwsync_need_resume = false;
            }
        }
    }

    return 0;
err_lock:
    pthread_mutex_unlock(&out->mdata_lock);
    return ret;
}

