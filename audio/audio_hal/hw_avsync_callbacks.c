
#define LOG_TAG "audio_hwsync_cbks"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <cutils/log.h>
#include "hw_avsync_callbacks.h"
#include "audio_hwsync.h"
#include "audio_hw.h"
#include "audio_hw_utils.h"

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
    uint32_t pts32 = 0;
    uint64_t pts = 0;
    uint64_t aligned_offset = 0;
    uint32_t frame_size = 0;
    uint32_t sample_rate = 48000;
    uint64_t pts_delta = 0;
    int ret = 0;

    if (!cookie || !header) {
        ALOGE("NULL pointer");
        return -EINVAL;
    }
    ALOGV("%s(), pout %p", __func__, out);

    frame_size = audio_stream_out_frame_size(&out->stream);
    sample_rate = out->audioCfg.sample_rate;

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
            ALOGV("%s(), offset %lld, checkout payload offset %lld",
                        __func__, offset, mdata_list->mdata.payload_offset);
            ALOGV("%s(), frame_size %d, pts %lldms",
                        __func__, header->frame_size, header->pts/1000000);
        }
    }
    ALOGV("offset =%lld aligned_offset=%lld frame size=%d samplerate=%d", offset, aligned_offset,frame_size,sample_rate);
    if (offset >= aligned_offset && mdata_list) {
        pts = header->pts;
        pts_delta = (offset - aligned_offset) * 1000000000LL/(frame_size * sample_rate);
        pts += pts_delta;
        out->last_pts = pts;
        out->last_payload_offset = offset;
        list_remove(&mdata_list->list);
        free(mdata_list);
    } else if (offset > out->last_payload_offset) {
        pts_delta = (offset - out->last_payload_offset) * 1000000000LL/(frame_size * sample_rate);
        pts = out->last_pts + pts_delta;
    } else {
        ret = -EINVAL;
        goto err_lock;
    }

    pts32 = (uint32_t)(pts / 1000000 * 90);
    pthread_mutex_unlock(&out->mdata_lock);

    /*if stream is already paused, we don't need to av sync, it may cause pcr reset*/
    if (out->pause_status) {
        ALOGW("%s(), write in pause status", __func__);
        return -EINVAL;
    }


    if (!out->first_pts_set) {
        uint32_t latency = 0;
        int vframe_ready_cnt = 0;
        int delay_count = 0;
        hwsync_header_construct(header);
        pts32 -= latency*90;
        ALOGD("%s(), set tsync start pts %d, latency %d, last position %lld",
            __func__, pts32, latency, out->last_frames_postion);
        while (delay_count < 10) {
            vframe_ready_cnt = get_sysfs_int("/sys/class/video/vframe_ready_cnt");
            ALOGV("/sys/class/video/vframe_ready_cnt is %d", vframe_ready_cnt);
            if (vframe_ready_cnt < 2) {
                usleep(10000);
                delay_count++;
                continue;
            }
            break;
        }
        aml_hwsync_set_tsync_start_pts(pts32);
        out->first_pts_set = true;
        //*delay_ms = 40;
    } else {
        enum hwsync_status sync_status = CONTINUATION;
        struct hw_avsync_header_extractor *hwsync_extractor;
        struct aml_audio_device *adev = out->dev;
        uint32_t pcr = 0;
        uint32_t apts_gap;
        // adjust pts based on latency which is only the outport latency
        uint64_t latency = out_get_outport_latency((struct audio_stream_out *)out) * 90;
        latency += 40 * 90;
        // check PTS discontinue, which may happen when audio track switching
        // discontinue means PTS calculated based on first_apts and frame_write_sum
        // does not match the timestamp of next audio samples
        if (pts32 > latency) {
            pts32 -= latency;
        } else {
            pts32 = 0;
        }

        hwsync_extractor = out->hwsync_extractor;
        hwsync_extractor = out->hwsync_extractor;
        ret = aml_hwsync_get_tsync_pts_by_handle(adev->tsync_fd, &pcr);
        if (ret != 0) {
            ALOGE("%s() get tsync(fd %d) pts failed err %d",
                    __func__, adev->tsync_fd, ret);
        }
        if (out->debug_stream)
            ALOGD("%s()audio pts %dms, pcr %dms, latency %lldms, diff %dms",
                __func__, pts32/90, pcr/90, latency/90,
                (pts32 > pcr) ? (pts32 - pcr)/90 : (pcr - pts32)/90);
        apts_gap = get_pts_gap(pcr, pts32);
        if (out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
            sync_status = pcm_check_hwsync_status(apts_gap);
        } else {
            sync_status = pcm_check_hwsync_status1(pcr, pts32);
        }
        if (out->need_first_sync) {
            /*when resume, we need do exactly sync fisrt*/
            out->need_first_sync = false;
            sync_status = ADJUSTMENT;
        }
        // limit the gap handle to 0.5~5 s.
        if (sync_status == ADJUSTMENT) {
            // two cases: apts leading or pcr leading
            // apts leading needs inserting frame and pcr leading neads discarding frame
            if (pts32 > pcr) {
                int insert_size = 0;

                insert_size = apts_gap / 90 * 48 * 4;
                insert_size = insert_size & (~63);
                ALOGI("%s(), pcrscr %dms adjusted_apts %dms", __func__, pcr/90, pts32/90);
                ALOGI("audio gap: pcr < apts %d ms, need insert data %d\n", apts_gap / 90, insert_size);
                *delay_ms = apts_gap / 90;
            } else {
                ALOGW("audio gap: pcr > apts %dms", apts_gap / 90);
                *delay_ms = -(int)apts_gap / 90;
                aml_hwsync_reset_tsync_pcrscr(pts32);
            }
        } else if (sync_status == RESYNC){
            ALOGI("%s(), tsync -> reset pcrscr %dms -> %dms",
                    __func__, pcr/90, pts32/90);
            /*during video stop, pcr has been reset to 0 by video,
              we need ignore such pcr value*/
            if (pcr != 0) {
                int ret_val = aml_hwsync_reset_tsync_pcrscr(pts32);
                if (ret_val < 0) {
                    ALOGE("unable to open file %s,err: %s", TSYNC_APTS, strerror(errno));
                }
            }
        }
    }

    return 0;
err_lock:
    pthread_mutex_unlock(&out->mdata_lock);
    return ret;
}
