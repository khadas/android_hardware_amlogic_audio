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

#define LOG_TAG "audio_hw_hal_mmap"
#define __USE_GNU
//#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <inttypes.h>
#include <ion_4.12.h>

#include "audio_hw.h"
#include "audio_hw_ms12.h"
#include "audio_hw_utils.h"
#include "audio_virtual_buf.h"
#include <cutils/properties.h>
#include <sys/utsname.h>

#include "aml_android_utils.h"
#include "aml_audio_timer.h"
#include "aml_mmap_audio.h"
#include "aml_volume_utils.h"
#include "aml_malloc_debug.h"

#define MMAP_SAMPLE_RATE_HZ             (48000)
#define MMAP_BUFFER_BURSTS_NUM          (4)

#define MMAP_WRITE_SIZE_FRAME           (384) // 8ms
#define MMAP_WRITE_PERIOD_TIME_MS       (MMAP_WRITE_SIZE_FRAME * MSEC_PER_SEC / MMAP_SAMPLE_RATE_HZ)
#define MMAP_WRITE_PERIOD_TIME_NANO     (MMAP_WRITE_SIZE_FRAME * NSEC_PER_SEC / MMAP_SAMPLE_RATE_HZ)

#define MMAP_INPUT_FILE                  "/data/vendor/audiohal/pcm_mmap"


enum aml_mmap_audio_status_t {
    MMAP_INIT,
    MMAP_START,
    MMAP_START_DONE,
    MMAP_STOP,
    MMAP_STOP_DONE,
    MMAP_INVALID = -1,
};

#define AML_MMAP_AUDIO_CLIENT_MAX_NUM     8

typedef struct _aml_mmap_audio_client_st {
    int                             s32AllocId;
    uint8_t                         *pu8TempBuf;
    size_t                          uxTempBufLen;
    uint32_t                        u32BytesAvail;
    uint32_t                        u32FramesAvail;
    struct aml_stream_out           *stStream;
    struct audioCfg                 stCfg;
    enum aml_mmap_audio_status_t    status;
    bool                            bBufferReady;
    pthread_mutex_t                 statusMutex;
} aml_mmap_audio_client_st;


typedef struct _aml_mmap_audio_manager_st {
    aml_mmap_audio_client_st        *pstClientList[AML_MMAP_AUDIO_CLIENT_MAX_NUM];
    uint32_t                        u32ProcessFrames;
    aml_pcm_mixing_st               stMultichMixer;
    pthread_mutex_t                 mutex;
    int32_t                         s32WriteSizeFrame;
    int32_t                         s32BufferBurstNum;
    uint64_t                        u64WritePeriodTimeNano;

    bool                            bMs12;
    bool                            bUseThread;
    aml_mmap_thread_param_st        stThreadParam;
    bool                            bSupportDmaBuffer;
} aml_mmap_audio_manager_st;


#if 0
static void *outMmapThread(void *pArg) {
    struct aml_stream_out       *out = (struct aml_stream_out *) pArg;
    aml_mmap_audio_param_st     *pstParam = (aml_mmap_audio_param_st *)out->pstMmapAudioParam;
    struct audio_virtual_buf    *pstVirtualBuffer = NULL;
    unsigned char               *pu8CurReadAddr = pstParam->pu8MmapAddr;
    unsigned char               *pu8StartAddr = pstParam->pu8MmapAddr;
    unsigned char               *pu8TempBufferAddr = NULL;
    aml_mmap_thread_param_st    *pstThread = &pstParam->stThreadParam;
    unsigned int                u32BurstSizeByte = MMAP_WRITE_SIZE_FRAME * pstParam->u32FrameSize;
    struct timespec timestamp;

    AM_LOGI("enter threadloop bExitThread:%d, bStopPlay:%d, mmap addr:%p, out:%p",
        pstThread->bExitThread, pstThread->bStopPlay, pu8StartAddr, out);
    R_CHECK_POINTER_LEGAL(NULL, pu8StartAddr, "")
    prctl(PR_SET_NAME, (unsigned long)"outMmapThread");
    aml_set_thread_priority("outMmapThread", pstThread->threadId);
    aml_audio_set_cpu23_affinity();

    pu8TempBufferAddr = (unsigned char *)aml_audio_malloc(u32BurstSizeByte);
    while (false == pstThread->bExitThread) {
        if (false == pstThread->bStopPlay) {

            if (pstThread->status == MMAP_START) {
                AM_LOGI("MMAP status: start");
                pu8CurReadAddr = pu8StartAddr;
                pstParam->u32FramePosition = 0;
                clock_gettime(CLOCK_MONOTONIC, &timestamp);
                pstParam->time_nanoseconds = (long long)timestamp.tv_sec * NSEC_PER_SEC + (long long)timestamp.tv_nsec;
                pstThread->status = MMAP_START_DONE;
                if (pstVirtualBuffer) {
                    audio_virtual_buf_reset(pstVirtualBuffer);
                    audio_virtual_buf_process((void *)pstVirtualBuffer, MMAP_WRITE_PERIOD_TIME_NANO * MMAP_BUFFER_BURSTS_NUM);
                }
            }

            if (pstVirtualBuffer == NULL) {
                audio_virtual_buf_open((void **)&pstVirtualBuffer, "aaudio mmap",
                        MMAP_WRITE_PERIOD_TIME_NANO * MMAP_BUFFER_BURSTS_NUM, MMAP_WRITE_PERIOD_TIME_NANO * MMAP_BUFFER_BURSTS_NUM, 0, 0);
                audio_virtual_buf_process((void *)pstVirtualBuffer, MMAP_WRITE_PERIOD_TIME_NANO * MMAP_BUFFER_BURSTS_NUM);
            }
            unsigned int u32RemainSizeByte =  (pstParam->u32BufferSize + pu8StartAddr) - pu8CurReadAddr;
            if (u32RemainSizeByte >= u32BurstSizeByte) {

                memcpy(pu8TempBufferAddr, pu8CurReadAddr, u32BurstSizeByte);
                memset(pu8CurReadAddr, 0, u32BurstSizeByte);
                pu8CurReadAddr += u32BurstSizeByte;
            } else {
                memcpy(pu8TempBufferAddr, pu8CurReadAddr, u32RemainSizeByte);
                memset(pu8CurReadAddr, 0, u32RemainSizeByte);

                memcpy(pu8TempBufferAddr + u32RemainSizeByte, pu8StartAddr, u32BurstSizeByte - u32RemainSizeByte);
                memset(pu8StartAddr, 0, u32BurstSizeByte - u32RemainSizeByte);
                pu8CurReadAddr = pu8StartAddr + u32BurstSizeByte - u32RemainSizeByte;
            }
            pstParam->u32FramePosition += MMAP_WRITE_SIZE_FRAME;
            // Absolute time must be used when get timestamp.
            clock_gettime(CLOCK_MONOTONIC, &timestamp);
            pstParam->time_nanoseconds = (long long)timestamp.tv_sec * NSEC_PER_SEC + (long long)timestamp.tv_nsec;

            if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
                check_audio_level("aaudio_in", pu8TempBufferAddr, u32BurstSizeByte);
            }

            apply_volume(out->volume_l, pu8TempBufferAddr, 2, u32BurstSizeByte);

            if (out->dev->useSubMix) {
                out->stream.write(&out->stream, pu8TempBufferAddr, u32BurstSizeByte);
            } else {
                out_write_new(&out->stream, pu8TempBufferAddr, u32BurstSizeByte);
            }
            if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
                aml_audio_dump_audio_bitstreams(MMAP_INPUT_FILE, pu8TempBufferAddr, u32BurstSizeByte);
            }
            audio_virtual_buf_process((void *)pstVirtualBuffer, MMAP_WRITE_PERIOD_TIME_NANO);
            if (out->dev->debug_flag >= 100) {
                AM_LOGI("CurReadAddr:%p, RemainSize:%d, FramePosition:%d offset=%d", pu8CurReadAddr, u32RemainSizeByte,
                    pstParam->u32FramePosition, pstParam->u32FramePosition%(MMAP_WRITE_SIZE_FRAME * MMAP_BUFFER_BURSTS_NUM));
            }
        } else {
            struct timespec tv;
            clock_gettime(CLOCK_MONOTONIC, &tv);
            // The suspend time set to 30 sec, reduce cpu power consumption.
            // And waiting time can be awakened by out_start func.
            tv.tv_sec += 30;
            pthread_mutex_lock(&pstThread->mutex);
            pthread_cond_timedwait(&pstThread->cond, &pstThread->mutex, &tv);
            pthread_mutex_unlock(&pstThread->mutex);
        }
    }

    if (pstVirtualBuffer != NULL) {
        audio_virtual_buf_close((void **)&pstVirtualBuffer);
    }
    aml_audio_free(pu8TempBufferAddr);
    pu8TempBufferAddr = NULL;
    AM_LOGI(" exit threadloop, out:%p", out);
    return NULL;
}

static int outMmapStart(const struct audio_stream_out *stream)
{
    AM_LOGI("stream:%p", stream);
    struct aml_stream_out       *out = (struct aml_stream_out *) stream;
    aml_mmap_audio_param_st     *pstParam = (aml_mmap_audio_param_st *)out->pstMmapAudioParam;

    if (pstParam == NULL || (pstParam->stThreadParam.status != MMAP_INIT && pstParam->stThreadParam.status != MMAP_STOP_DONE)) {
        AM_LOGW("error or mmap no init");
        return -ENODATA;
    }

    if (0 == pstParam->stThreadParam.threadId) {
        AM_LOGE("exit threadloop");
        return -ENOSYS;
    }
    pstParam->u32FramePosition = 0;
    pstParam->stThreadParam.bStopPlay = false;
    pstParam->stThreadParam.status = MMAP_START;
    //dolby_ms12_app_flush();
    pthread_mutex_lock(&pstParam->stThreadParam.mutex);
    pthread_cond_signal(&pstParam->stThreadParam.cond);
    pthread_mutex_unlock(&pstParam->stThreadParam.mutex);
    AM_LOGI("--stream:%p", stream);
    return 0;
}

static int outMmapStop(const struct audio_stream_out *stream)
{
    AM_LOGI("stream:%p", stream);
    struct aml_stream_out       *out = (struct aml_stream_out *) stream;
    aml_mmap_audio_param_st     *pstParam = (aml_mmap_audio_param_st *)out->pstMmapAudioParam;

    if (pstParam == NULL || pstParam->stThreadParam.status != MMAP_START_DONE) {
        AM_LOGW("not start done or mmap not init");
        return -ENODATA;
    }

    //dolby_ms12_app_flush();

    // suspend threadloop.
    pstParam->stThreadParam.status = MMAP_STOP;
    /*sleep some time, to make sure the read thread read all the data*/
    aml_audio_sleep(8 * 1000);
    pstParam->stThreadParam.bStopPlay = true;
    pstParam->stThreadParam.status = MMAP_STOP_DONE;
    memset(pstParam->pu8MmapAddr, 0, pstParam->u32BufferSize);
    pstParam->u32FramePosition = 0;
    AM_LOGI("--stream:%p", stream);
    return 0;
}

static int outMmapCreateBuffer(const struct audio_stream_out *stream,
                                             int32_t min_size_frames,
                                             struct audio_mmap_buffer_info *info)
{
    AM_LOGI("stream:%p, min_size_frames:%d", stream, min_size_frames);
    struct aml_stream_out       *out = (struct aml_stream_out *) stream;
    aml_mmap_audio_param_st     *pstParam = (aml_mmap_audio_param_st *)out->pstMmapAudioParam;
    int ret = 0;
    R_CHECK_POINTER_LEGAL(-ENOSYS, pstParam, "");
    R_CHECK_PARAM_LEGAL(-EINVAL, min_size_frames, -1, INT_MAX - 1, "");

    info->shared_memory_address = pstParam->pu8MmapAddr;
    info->shared_memory_fd = pstParam->s32IonShareFd;
    info->buffer_size_frames = MMAP_WRITE_SIZE_FRAME * MMAP_BUFFER_BURSTS_NUM;
    info->burst_size_frames  = MMAP_WRITE_SIZE_FRAME;
    info->flags |= AUDIO_MMAP_APPLICATION_SHAREABLE;

    aml_mmap_thread_param_st *pstThread = &pstParam->stThreadParam;
    if (pstThread->threadId != 0) {
        AM_LOGW("mmap thread already exist, recreate thread");
        pstThread->bExitThread = true;
        pstThread->bStopPlay = true;
        pthread_mutex_lock(&pstThread->mutex);
        pthread_cond_signal(&pstThread->cond);
        pthread_mutex_unlock(&pstThread->mutex);
        pthread_join(pstThread->threadId, NULL);
        memset(pstThread, 0, sizeof(aml_mmap_thread_param_st));
    } else {
        pthread_mutex_init (&pstThread->mutex, NULL);
    }
    pthread_condattr_init(&pstThread->condAttr);
    pthread_condattr_setclock(&pstThread->condAttr, CLOCK_MONOTONIC);
    pthread_cond_init(&pstThread->cond, &pstThread->condAttr);
    pstThread->bExitThread = false;
    pstThread->bStopPlay = true;
    pstThread->status = MMAP_INIT;
    ret = pthread_create(&pstThread->threadId, NULL, &outMmapThread, out);
    R_CHECK_RET(ret, "Create thread fail!");
    AM_LOGI("mmap_fd:%d, mmap address:%p", info->shared_memory_fd, pstParam->pu8MmapAddr);
    return 0;
}

static int outMmapGetPosition(const struct audio_stream_out *stream,
                                           struct audio_mmap_position *position)
{
    struct aml_stream_out       *out = (struct aml_stream_out *) stream;
    aml_mmap_audio_param_st     *pstParam = (aml_mmap_audio_param_st *)out->pstMmapAudioParam;

    position->time_nanoseconds = pstParam->time_nanoseconds;
    position->position_frames = pstParam->u32FramePosition;

    if (position->position_frames == 0 || pstParam->stThreadParam.status < MMAP_START_DONE) {
        AM_LOGW("status:%d not start done or position:%d is 0",
            pstParam->stThreadParam.status, position->position_frames);
        /*1)if return -ENOSYS, StreamHAL report error "function not implemented"(-38)
            Here should be changed to 0, the cts can pass.
          2)GetMmapPositionOfNonMmapedStream of vts, it should return -ENOSYS that this case can pass.
        */
        if (pstParam->stThreadParam.status == MMAP_INIT && pstParam->is_first_fetch_position) {
            pstParam->is_first_fetch_position = false;
            AM_LOGI("  is first_fetch_position and return -ENOSYS");
            return -ENOSYS;
        } else {
            return 0;
        }
    }
    if (out->dev->debug_flag >= 100) {
        AM_LOGD("stream:%p, position_frames:%d, nano:%lld frame diff=%lu ms time diff=%" PRId64 " ms", stream,
            position->position_frames, (long long)position->time_nanoseconds,
            (position->position_frames - out->last_mmap_position) * MSEC_PER_SEC / MMAP_SAMPLE_RATE_HZ,
            (position->time_nanoseconds - out->last_mmap_nano_second) / NSEC_PER_MSEC);
    }
    out->last_mmap_position = position->position_frames;
    out->last_mmap_nano_second   = position->time_nanoseconds;

    return 0;
}
#endif

static enum aml_mmap_audio_status_t mmap_audio_get_client_status(void *pstMananger, int client_id);
static void mmap_audio_set_buffer_ready(void *pstMananger, int client_id, bool buffer_ready);

static void *outMmapThread(void *pArg) {
    aml_mmap_audio_manager_st   *pstMananger = (aml_mmap_audio_manager_st *)pArg;
    struct aml_audio_device     *adev = (struct aml_audio_device *)adev_get_handle();
    struct audio_virtual_buf    *pstVirtualBuffer = NULL;
    unsigned char               *pu8TempBufferAddr = NULL;
    size_t                      uxTempBufferLen = 0;
    size_t                      uxReqBufBytes = 0;
    int                         s32ProcessBytes = 0;
    aml_mmap_thread_param_st    *pstThread = NULL;
    unsigned int                u32BurstSizeByte = 0;
    struct audioCfg             stNewCfg  = {0};
    struct audioCfg             stCurrCfg = {0};
    bool                        bConfigUpdate = false;
    struct dolby_ms12_desc      *ms12 = NULL;
    uint64_t                    u64BufferFrameNs = 0;
    int                         ret = 0;

    R_CHECK_POINTER_LEGAL(NULL, adev, "adev")
    R_CHECK_POINTER_LEGAL(NULL, pstMananger, "pstMananger")
    pstThread = &pstMananger->stThreadParam;
    ms12 = &adev->ms12;
    u64BufferFrameNs = pstMananger->u64WritePeriodTimeNano * pstMananger->s32BufferBurstNum;

    prctl(PR_SET_NAME, (unsigned long)"outMmapThread");
    aml_set_thread_priority("outMmapThread", pstThread->threadId);
    aml_audio_set_cpu23_affinity();

    while (false == pstThread->bExitThread) {
        if (mmap_audio_has_active_client(pstMananger)) {
            if (true == pstThread->bStopPlay) {
                if (pstVirtualBuffer) {
                    audio_virtual_buf_reset(pstVirtualBuffer);
                    audio_virtual_buf_process((void *)pstVirtualBuffer, u64BufferFrameNs);
                }
                pstThread->bStopPlay = false;
            }
            if (pstVirtualBuffer == NULL) {
                audio_virtual_buf_open((void **)&pstVirtualBuffer, "aaudio mmap",
                        u64BufferFrameNs, u64BufferFrameNs, 0, 0);
                audio_virtual_buf_process((void *)pstVirtualBuffer, u64BufferFrameNs);
            }

            // data process
            mmap_audio_process_data(pstMananger, pstMananger->s32WriteSizeFrame);

            if (mmap_audio_prepare_merge(pstMananger, &stNewCfg, adev->is_netflix) != 0) {
                AM_LOGE("mmap_audio_prepare_merge fail");
                continue;
            }
            bConfigUpdate = false;
            if (memcmp(&stCurrCfg, &stNewCfg, sizeof(stNewCfg))) {
                AM_LOGI("channelMask update 0x%x -> 0x%x", stCurrCfg.channelMask, stNewCfg.channelMask);
                memcpy(&stCurrCfg, &stNewCfg, sizeof(stNewCfg));
                bConfigUpdate = true;
            }

            uxReqBufBytes = pstMananger->s32WriteSizeFrame * stNewCfg.frame_size;
            ret = aml_audio_check_and_realloc((void **)&pu8TempBufferAddr, &uxTempBufferLen, uxReqBufBytes);
            if ((ret != 0) || (pu8TempBufferAddr == NULL)) {
                AM_LOGE("allocate aaudio_buf(%zu bytes) failed", uxReqBufBytes);
                continue;
            }
            s32ProcessBytes = mmap_audio_merge_data(pstMananger, &stCurrCfg, pu8TempBufferAddr, uxTempBufferLen);

            // data writing, only support ms12
            // for non-dolby, mmap audio is read by submix directly.
            if (s32ProcessBytes > 0 && pstMananger->bMs12) {
                size_t used_bytes = 0;
                dolby_ms12_multi_app_process(ms12, \
                    pu8TempBufferAddr, s32ProcessBytes, &used_bytes, &stCurrCfg, bConfigUpdate);

                if (used_bytes != s32ProcessBytes) {
                    AM_LOGW("ms12 only consume %zu, input %d", used_bytes, s32ProcessBytes);
                }
            }

            // virtual buffer sleep
            audio_virtual_buf_process((void *)pstVirtualBuffer, pstMananger->u64WritePeriodTimeNano);
        } else {
            struct timespec tv;
            clock_gettime(CLOCK_MONOTONIC, &tv);
            // The suspend time set to 30 sec, reduce cpu power consumption.
            // And waiting time can be awakened by out_start func.
            tv.tv_sec += 30;
            pstThread->bStopPlay = true;
            memset(&stCurrCfg, 0, sizeof(stCurrCfg));
            pthread_mutex_lock(&pstThread->mutex);
            pthread_cond_timedwait(&pstThread->cond, &pstThread->mutex, &tv);
            pthread_mutex_unlock(&pstThread->mutex);
        }
    }

    if (pstVirtualBuffer != NULL) {
        audio_virtual_buf_close((void **)&pstVirtualBuffer);
    }
    aml_audio_free(pu8TempBufferAddr);
    pu8TempBufferAddr = NULL;
    AM_LOGI(" exit threadloop, pstMananger:%p", pstMananger);
    return NULL;
}


static int outMmapStart(const struct audio_stream_out *stream)
{
    AM_LOGI("stream:%p", stream);
    struct aml_stream_out    *out = (struct aml_stream_out *) stream;
    aml_mmap_audio_param_st  *pstParam = (aml_mmap_audio_param_st *)out->pstMmapAudioParam;
    aml_mmap_audio_manager_st *pstMmapMananger = out->mmap_audio_manager;
    aml_mmap_thread_param_st *pstThread = &pstMmapMananger->stThreadParam;

    pstParam->u32FramePosition = 0;
    pstParam->s64BufferEmptyNs = 0;
    if (mmap_audio_start_client(pstMmapMananger, out->mmap_audio_client_id) != 0) {
        AM_LOGE("mmap_audio_start_client failed(client_id %d)", out->mmap_audio_client_id);
        return -ENOSYS;
    }

    if (pstMmapMananger->bUseThread) {
        pthread_mutex_lock(&pstThread->mutex);
        pthread_cond_signal(&pstThread->cond);
        pthread_mutex_unlock(&pstThread->mutex);
    }
    AM_LOGI("--stream:%p", stream);
    return 0;
}


static int outMmapStop(const struct audio_stream_out *stream)
{
    AM_LOGI("stream:%p", stream);
    struct aml_stream_out    *out = (struct aml_stream_out *) stream;
    aml_mmap_audio_param_st  *pstParam = (aml_mmap_audio_param_st *)out->pstMmapAudioParam;
    void *mmap_audio_manager = out->mmap_audio_manager;
    int mmap_audio_client_id = out->mmap_audio_client_id;

    if (mmap_audio_stop_client(mmap_audio_manager, mmap_audio_client_id) != 0) {
        AM_LOGE("mmap_audio_stop_client failed(client_id %d)", mmap_audio_client_id);
        return -ENOSYS;
    }

    /*
     * sleep some time, to make sure the read thread read all the data
     * currently, nts aaudio server buffer is about 16ms(minus 1ms for system jitter)
    */
    if (pstParam->time_nanoseconds > 0) {
        pstParam->s64BufferEmptyNs = pstParam->time_nanoseconds + 15 * 1000 * 1000;
        aml_audio_sleep(8 * 1000);
    } else {
        pstParam->s64BufferEmptyNs = 0;
    }
    /*
    if (mmap_audio_stop_client_complete(mmap_audio_manager, mmap_audio_client_id) != 0) {
        AM_LOGE("mmap_audio_stop_client failed(client_id %d)", mmap_audio_client_id);
        return -ENOSYS;
    }
    memset(pstParam->pu8MmapAddr, 0, pstParam->u32BufferSize);
    pstParam->u32FramePosition = 0;
    */
    AM_LOGI("--stream:%p", stream);
    return 0;
}

static int outMmapCreateBuffer(const struct audio_stream_out *stream,
                                             int32_t min_size_frames,
                                             struct audio_mmap_buffer_info *info)
{
    AM_LOGI("stream:%p, min_size_frames:%d", stream, min_size_frames);
    struct aml_stream_out       *out = (struct aml_stream_out *) stream;
    aml_mmap_audio_param_st     *pstParam = (aml_mmap_audio_param_st *)out->pstMmapAudioParam;
    aml_mmap_audio_manager_st   *pstMmapMananger = out->mmap_audio_manager;
    int ret = 0;
    int write_size_frame = 0;
    int buffer_burst_num = 0;
    R_CHECK_POINTER_LEGAL(-ENOSYS, pstParam, "");
    R_CHECK_PARAM_LEGAL(-EINVAL, min_size_frames, -1, INT_MAX - 1, "");

    mmap_audio_get_burst_info(pstMmapMananger, &write_size_frame, &buffer_burst_num);
    mmap_audio_set_buffer_ready(pstMmapMananger, out->mmap_audio_client_id, true);

    info->shared_memory_address = pstParam->pu8MmapAddr;
    if (pstMmapMananger->bSupportDmaBuffer) {
        info->shared_memory_fd = pstParam->s32DmaFd;
    } else {
        info->shared_memory_fd = pstParam->s32IonShareFd;
    }
    info->buffer_size_frames = write_size_frame * buffer_burst_num;
    info->burst_size_frames  = write_size_frame;
    info->flags |= AUDIO_MMAP_APPLICATION_SHAREABLE;
    AM_LOGI("mmap_fd:%d, mmap address:%p", info->shared_memory_fd, pstParam->pu8MmapAddr);
    return 0;
}

static int outMmapGetPosition(const struct audio_stream_out *stream,
                                           struct audio_mmap_position *position)
{
    struct aml_stream_out       *out = (struct aml_stream_out *) stream;
    aml_mmap_audio_param_st     *pstParam = (aml_mmap_audio_param_st *)out->pstMmapAudioParam;
    enum aml_mmap_audio_status_t status = MMAP_INVALID;
    int compensate_frames = 2 * out->hal_rate / 1000;  // experience value : 2 ms

    position->time_nanoseconds = pstParam->time_nanoseconds;
    position->position_frames = pstParam->u32FramePosition;
    status = mmap_audio_get_client_status(out->mmap_audio_manager, out->mmap_audio_client_id);

    // nts llp aaudio threshold is high, let audiohal has more empty room.
    if (out->dev->is_netflix && out->aaudio_low_latency && (position->position_frames > compensate_frames)) {
        position->position_frames -= compensate_frames;
    }

    if (position->position_frames == 0 || status < MMAP_START_DONE) {
        AM_LOGW("status:%d not start done or position:%d is 0",
            status, position->position_frames);
        /*1)if return -ENOSYS, StreamHAL report error "function not implemented"(-38)
            Here should be changed to 0, the cts can pass.
          2)GetMmapPositionOfNonMmapedStream of vts, it should return -ENOSYS that this case can pass.
        */
        if ((status == MMAP_INIT && pstParam->is_first_fetch_position) || status == MMAP_INVALID) {
            pstParam->is_first_fetch_position = false;
            AM_LOGI("  is first_fetch_position and return -ENOSYS");
            return -ENOSYS;
        } else {
            return 0;
        }
    }
    if (out->dev->debug_flag) {
        AM_LOGD("stream:%p, position_frames:%d, nano:%lld frame diff=%lu ms time diff=%" PRId64 " ms", stream,
            position->position_frames, (long long)position->time_nanoseconds,
            (position->position_frames - out->last_mmap_position) * MSEC_PER_SEC / MMAP_SAMPLE_RATE_HZ,
            (position->time_nanoseconds - out->last_mmap_nano_second) / NSEC_PER_MSEC);
    }
    out->last_mmap_position = position->position_frames;
    out->last_mmap_nano_second   = position->time_nanoseconds;

    return 0;
}


static int ion_buffer_allocate_new (aml_mmap_audio_param_st     *pstParam) {
    int ret  = 0;
    int num_heaps = 0;
    unsigned int heap_mask = 0;

    AM_LOGI("enter");

    ret = ion_query_heap_cnt(pstParam->s32IonFd, &num_heaps);
    if (ret < 0) {
        AM_LOGE("ion_query_heap_cnt fail! no ion heaps for alloc!!! ret:%#x", ret);
        return -ENOMEM;
    }
    struct ion_heap_data * const heaps = (struct ion_heap_data *) aml_audio_calloc (1, num_heaps * sizeof(struct ion_heap_data));
    if (num_heaps <= 0 || heaps == NULL) {
        AM_LOGE("heaps is NULL or no heaps, num_heaps:%d", num_heaps);
        aml_audio_free(heaps);
        return -ENOMEM;
    }

    ret = ion_query_get_heaps(pstParam->s32IonFd, num_heaps, heaps);
    if (ret < 0) {
        AM_LOGE("ion_query_get_heaps fail! no ion heaps for alloc!!! ret:%#x", ret);
        aml_audio_free(heaps);
        return -ENOMEM;
    }
    for (int i = 0; i != num_heaps; ++i) {
        if ((1 << heaps[i].type) == ION_HEAP_SYSTEM_MASK) {
            heap_mask = 1 << heaps[i].heap_id;
            break;
        }
    }
    aml_audio_free(heaps);
    if (heap_mask == 0) {
        AM_LOGE("don't find match heap!!!");
        return -ENOMEM;
    }

    ret = ion_alloc_fd(pstParam->s32IonFd, pstParam->u32BufferSize, 0, heap_mask, 0, &pstParam->s32IonShareFd);
    if (ret < 0) {
       AM_LOGE("ion_alloc_fd failed, u32BufferSize:%d, ret:%#x, errno:%d", pstParam->u32BufferSize, ret, errno);
       return -ENOMEM;
    }

    pstParam->pu8MmapAddr = mmap(NULL, pstParam->u32BufferSize,  PROT_WRITE | PROT_READ,
                                   MAP_SHARED, pstParam->s32IonShareFd, 0);
    AM_LOGI("s32IonFd:%d, s32IonShareFd:%d, b_size:%d, frame size:%d, add:%p",
        pstParam->s32IonFd, pstParam->s32IonShareFd, pstParam->u32BufferSize, pstParam->u32FrameSize, pstParam->pu8MmapAddr);

    return 0;
}

static int ion_buffer_allocate_legacy (aml_mmap_audio_param_st     *pstParam) {
    int ret = 0;
    AM_LOGI("enter");

    ret = ion_alloc(pstParam->s32IonFd, pstParam->u32BufferSize, 32, ION_HEAP_SYSTEM_MASK, 0,
                        &pstParam->hIonHandle);
    if (ret < 0) {
        ALOGE("[%s:%d] ion_alloc fail ret:%#x", __func__, __LINE__, ret);
        return -1;
    }
    ret = ion_share(pstParam->s32IonFd, pstParam->hIonHandle, &pstParam->s32IonShareFd);
    if (ret < 0) {
        ALOGE("[%s:%d] ion_share fail ret:%#x", __func__, __LINE__, ret);
        return -1;
    }

    pstParam->pu8MmapAddr = mmap(NULL, pstParam->u32BufferSize,  PROT_WRITE | PROT_READ,
                                   MAP_SHARED, pstParam->s32IonShareFd, 0);
    ALOGI("[%s:%d] s32IonFd:%d, s32IonShareFd:%d, pu8MmapAddr:%p", __func__, __LINE__,
        pstParam->s32IonFd, pstParam->s32IonShareFd, pstParam->pu8MmapAddr);
    return 0;
}


static int dma_buffer_allocate (aml_mmap_audio_param_st *pstParam) {
    int ret = 0;
    AM_LOGI("enter");
    const char* heap_name = kDmabufSystemUncachedHeapName;

    BufferAllocator* bufferAllocator = CreateDmabufHeapBufferAllocator();
    if (!bufferAllocator) {
        AM_LOGE("unable to get allocator\n");
        return -1;
    }
    pstParam->pstBufferAllocator = bufferAllocator;

    /*
     * Legacy ion devices may have hardcoded heap IDs that do not
     * match the ion UAPI header. Map heap name 'system' to a heap mask
     * of all 1s so that these devices will allocate from the first
     * available heap when asked to allocate from a heap of name 'system'.
     */
    ret = MapDmabufHeapNameToIonHeap(bufferAllocator, heap_name,
                                     "" /* no mapping for non-legacy */,
                                     0 /* no mapping for non-legacy ion */,
                                     ~0 /* legacy ion heap mask */, 0 /* legacy ion heap flag */);
    if (ret < 0) {
        AM_LOGE("MapDmabufHeapNameToIonHeap failed: %d\n", ret);
        return -1;
    }

    pstParam->s32DmaFd = DmabufHeapAlloc(bufferAllocator, heap_name, pstParam->u32BufferSize, 0, 0);
    if (pstParam->s32DmaFd < 0) {
        printf("Alloc failed: %d\n", pstParam->s32DmaFd);
        return -1;
     }

    pstParam->pu8MmapAddr = mmap(NULL, pstParam->u32BufferSize,  PROT_WRITE | PROT_READ,
                                   MAP_SHARED, pstParam->s32DmaFd, 0);

    AM_LOGI("s32DmaFd:%d, pu8MmapAddr:%p", pstParam->s32DmaFd,pstParam->pu8MmapAddr);
    return 0;
}


int outMmapInit(struct aml_stream_out *out)
{
    AM_LOGI("stream:%p", out);
    int                         ret = 0;
    int                         write_size_frame = 0;
    int                         buffer_burst_num = 0;
    aml_mmap_audio_param_st     *pstParam = NULL;
    struct aml_audio_device     *adev = (struct aml_audio_device *)adev_get_handle();
    aml_mmap_audio_manager_st   *pstMmapMananger = NULL;

    if (adev == NULL) {
        AM_LOGE("adev is NULL");
        return -1;
    }
    pstMmapMananger = adev->mmap_audio_manager;

    out->stream.start = outMmapStart;
    out->stream.stop = outMmapStop;
    out->stream.create_mmap_buffer = outMmapCreateBuffer;
    out->stream.get_mmap_position = outMmapGetPosition;

    mmap_audio_get_burst_info(pstMmapMananger, &write_size_frame, &buffer_burst_num);

    if (out->pstMmapAudioParam) {
       AM_LOGW("already init, can't again init");
       return 0;
    }
    out->pstMmapAudioParam = (aml_mmap_audio_param_st *)aml_audio_malloc(sizeof(aml_mmap_audio_param_st));
    pstParam = out->pstMmapAudioParam;
    R_CHECK_POINTER_LEGAL(-1, pstParam, "mmap memory malloc fail");
    memset(pstParam, 0, sizeof(aml_mmap_audio_param_st));
    pstParam->u32FrameSize = audio_bytes_per_frame(out->config.channels, AUDIO_FORMAT_PCM_16_BIT);
    pstParam->u32BufferSize = buffer_burst_num * write_size_frame *pstParam->u32FrameSize;

    out->mmap_audio_client_id = -1;
    if (!pstMmapMananger->bSupportDmaBuffer) {
        pstParam->s32IonFd = ion_open();
        if (pstParam->s32IonFd < 0) {
           AM_LOGE("ion_open fail! s32IonFd:%d", pstParam->s32IonFd);
           return -1;
        }
        if (ion_is_legacy(pstParam->s32IonFd)) {
            ret = ion_buffer_allocate_legacy(pstParam);
        } else {
            ret = ion_buffer_allocate_new(pstParam);
        }
    } else {
        ret = dma_buffer_allocate(pstParam);
    }
    if (ret != 0) {
        AM_LOGE("allocate %s buffer failed !", pstMmapMananger->bSupportDmaBuffer ? "DMA" : "ION");
        return -1;
    }

    pstParam->is_first_fetch_position = true;

    if (mmap_audio_register_client(pstMmapMananger, out) < 0) {
        AM_LOGE("mmap_audio_register_client fail !");
        return -1;
    }
    return ret;
}

int outMmapDeInit(struct aml_stream_out *out)
{
    AM_LOGI("stream:%p", out);
    aml_mmap_audio_param_st     *pstParam = (aml_mmap_audio_param_st *)out->pstMmapAudioParam;
    aml_mmap_audio_manager_st   *pstMmapMananger = out->mmap_audio_manager;
    R_CHECK_POINTER_LEGAL(0, pstParam, "uninitialized, can't deinit");

    mmap_audio_unregister_client(pstMmapMananger, out->mmap_audio_client_id);
    munmap(pstParam->pu8MmapAddr, pstParam->u32BufferSize);

    if (pstMmapMananger->bSupportDmaBuffer) {
        close(pstParam->s32DmaFd);
        if (pstParam->pstBufferAllocator) {
            FreeDmabufHeapBufferAllocator(pstParam->pstBufferAllocator);
        }
        pstParam->s32DmaFd = -1;
        pstParam->pstBufferAllocator = NULL;
    } else {
        close(pstParam->s32IonShareFd);
        if (ion_is_legacy(pstParam->s32IonFd))
            ion_free(pstParam->s32IonFd, pstParam->hIonHandle);
        ion_close(pstParam->s32IonFd);
    }
    aml_audio_free(pstParam);
    out->pstMmapAudioParam = NULL;
    return 0;
}



void* mmap_audio_new_manager(bool is_ms12)
{
    int ret = 0;
    struct utsname info;
    int kernel_version_major = 4;
    int kernel_version_minor = 9;
    char buf[PROPERTY_VALUE_MAX];
    aml_mmap_audio_manager_st *pstMananger = NULL;

    pstMananger = (aml_mmap_audio_manager_st *)aml_audio_calloc(1, sizeof(aml_mmap_audio_manager_st));
    if (pstMananger == NULL) {
        AM_LOGE("calloc aml_mmap_audio_manager_st failed !");
        return NULL;
    }
    pthread_mutex_init(&pstMananger->mutex, NULL);
    pstMananger->bMs12 = is_ms12;
    pstMananger->s32WriteSizeFrame = MMAP_WRITE_SIZE_FRAME;
    pstMananger->s32BufferBurstNum = MMAP_BUFFER_BURSTS_NUM;
    pstMananger->u64WritePeriodTimeNano = MMAP_WRITE_PERIOD_TIME_NANO;

    if (is_ms12) {
        pstMananger->bUseThread = true;
        AM_LOGI("use thread to move data");
    } else {
        pstMananger->bUseThread = false;
    }

    if (uname(&info) || sscanf(info.release, "%d.%d", &kernel_version_major, &kernel_version_minor) <= 0) {
        ALOGW("Could not get linux version: %s", strerror(errno));
    }

    if (kernel_version_major >= 5 || kernel_version_minor >= 15) {
        AM_LOGI("kernel %d.%d use DMA buffer", kernel_version_major, kernel_version_minor);
        pstMananger->bSupportDmaBuffer = true;
    } else {
        AM_LOGI("kernel %d.%d use ION buffer", kernel_version_major, kernel_version_minor);
        pstMananger->bSupportDmaBuffer = false;
    }

    if (pstMananger->bUseThread) {
        aml_mmap_thread_param_st *pstThread = &pstMananger->stThreadParam;
        if (pstThread->threadId != 0) {
            AM_LOGW("mmap thread already exist, recreate thread");
            pstThread->bExitThread = true;
            pstThread->bStopPlay = true;
            pthread_mutex_lock(&pstThread->mutex);
            pthread_cond_signal(&pstThread->cond);
            pthread_mutex_unlock(&pstThread->mutex);
            pthread_join(pstThread->threadId, NULL);
            memset(pstThread, 0, sizeof(aml_mmap_thread_param_st));
        } else {
            pthread_mutex_init (&pstThread->mutex, NULL);
        }
        pthread_condattr_init(&pstThread->condAttr);
        pthread_condattr_setclock(&pstThread->condAttr, CLOCK_MONOTONIC);
        pthread_cond_init(&pstThread->cond, &pstThread->condAttr);
        pstThread->bExitThread = false;
        pstThread->bStopPlay = true;
        pstThread->status = MMAP_INIT;
        ret = pthread_create(&pstThread->threadId, NULL, &outMmapThread, pstMananger);
        if (ret != 0) {
            AM_LOGE("create mmap thread failed !");
            pstMananger->bUseThread = false;
        }
    }
    return pstMananger;
}

void mmap_audio_free_manager(void *pstMananger)
{
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;
    if (pstMmapMananger == NULL) {
        return;
    }

    if (pstMmapMananger->bUseThread) {
        aml_mmap_thread_param_st *pstThread = &pstMmapMananger->stThreadParam;
        pstThread->bExitThread = true;
        pthread_mutex_lock(&pstThread->mutex);
        pthread_cond_signal(&pstThread->cond);
        pthread_mutex_unlock(&pstThread->mutex);
        if (pstThread->threadId != 0) {
           pthread_join(pstThread->threadId, NULL);
        }
        pstMmapMananger->bUseThread = false;
    }
    deinit_aml_pcm_mixer(&pstMmapMananger->stMultichMixer);
    aml_audio_free(pstMananger);
}

int mmap_audio_get_burst_info(void *pstMananger, int32_t *ps32WriteSizeFrame, int32_t *ps32BufferBurstNum)
{
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;
    if (pstMmapMananger == NULL) {
        return -1;
    }

    *ps32WriteSizeFrame = pstMmapMananger->s32WriteSizeFrame;
    *ps32BufferBurstNum = pstMmapMananger->s32BufferBurstNum;
    return 0;
}

int mmap_audio_register_client(void *pstMananger, struct aml_stream_out *aml_out)
{
    int index = 0;
    int alloc_id = 0;
    struct audioCfg audio_cfg;
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;
    aml_mmap_audio_client_st *pstMmapClient = NULL;
    if (pstMananger == NULL || aml_out == NULL) {
        return -1;
    }
    if (!(aml_out->flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) {
        AM_LOGE("aml_out %p is not a mmap output stream", aml_out);
        return -1;
    }

    memset(&audio_cfg, 0, sizeof(audio_cfg));
    audio_cfg.sampleRate = aml_out->hal_rate;
    audio_cfg.channelCnt = aml_out->hal_ch;
    audio_cfg.channelMask = aml_out->hal_channel_mask;
    audio_cfg.format = aml_out->hal_format;
    audio_cfg.frame_size = aml_out->hal_frame_size;

    pthread_mutex_lock(&pstMmapMananger->mutex);
    for (index = 0; index < AML_MMAP_AUDIO_CLIENT_MAX_NUM; index++) {
        if (pstMmapMananger->pstClientList[index] == NULL) {
            break;
        }
    }
    if (index >= AML_MMAP_AUDIO_CLIENT_MAX_NUM) {
        AM_LOGE("no available client id !");
        pthread_mutex_unlock(&pstMmapMananger->mutex);
        return -1;
    }

    pstMmapClient = (aml_mmap_audio_client_st *)aml_audio_calloc(1, sizeof(aml_mmap_audio_client_st));
    if (pstMmapClient == NULL) {
        AM_LOGE("calloc aml_mmap_audio_client_st failed !");
        pthread_mutex_unlock(&pstMmapMananger->mutex);
        return -1;
    }
    pstMmapClient->s32AllocId = index;
    pstMmapClient->pu8TempBuf = NULL;
    pstMmapClient->uxTempBufLen = 0;
    pstMmapClient->u32BytesAvail = 0;
    pstMmapClient->u32FramesAvail = 0;
    pstMmapClient->stStream = aml_out;
    memcpy(&pstMmapClient->stCfg, &audio_cfg, sizeof(audio_cfg));
    pstMmapClient->status = MMAP_INIT;
    pstMmapClient->bBufferReady = false;
    pstMmapMananger->pstClientList[index] = pstMmapClient;
    aml_out->mmap_audio_manager = pstMmapMananger;
    aml_out->mmap_audio_client_id = index;
    pthread_mutex_init(&pstMmapClient->statusMutex, NULL);
    pthread_mutex_unlock(&pstMmapMananger->mutex);
    AM_LOGI("OK ! stream %p, client id %d",  aml_out, index);

    return index;
}


void mmap_audio_unregister_client(void *pstMananger, int client_id)
{
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;
    aml_mmap_audio_client_st *pstMmapClient = NULL;
    if (pstMananger == NULL || client_id < 0 || client_id >= AML_MMAP_AUDIO_CLIENT_MAX_NUM) {
        AM_LOGE("invalid param : pstMananger %p, client_id %d", pstMananger, client_id);
        return;
    }

    pthread_mutex_lock(&pstMmapMananger->mutex);
    pstMmapClient = pstMmapMananger->pstClientList[client_id];
    if (pstMmapClient == NULL) {
        AM_LOGE("client_id %d has been release !", client_id);
        pthread_mutex_unlock(&pstMmapMananger->mutex);
        return;
    }
    if (pstMmapClient->pu8TempBuf) {
        aml_audio_free(pstMmapClient->pu8TempBuf);
        pstMmapClient->pu8TempBuf = NULL;
        pstMmapClient->uxTempBufLen = 0;
    }
    pthread_mutex_destroy(&pstMmapClient->statusMutex);
    AM_LOGI("OK ! stream %p, client id %d",  pstMmapClient->stStream, client_id);
    aml_audio_free(pstMmapClient);
    pstMmapMananger->pstClientList[client_id] = NULL;
    pthread_mutex_unlock(&pstMmapMananger->mutex);
}


int mmap_audio_start_client(void *pstMananger, int client_id)
{
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;
    aml_mmap_audio_client_st *pstMmapClient = NULL;

    if (pstMananger == NULL || client_id < 0 || client_id >= AML_MMAP_AUDIO_CLIENT_MAX_NUM) {
        AM_LOGE("invalid param : pstMananger %p, client_id %d", pstMananger, client_id);
        return -1;
    }

    pstMmapClient = pstMmapMananger->pstClientList[client_id];
    if (pstMmapClient == NULL) {
        AM_LOGW("no mmap client");
        return -ENODATA;
    }
    if (!pstMmapClient->bBufferReady) {
        AM_LOGW("client_id %d, buffer not ready!", client_id);
        return -ENODATA;
    }
    if (pstMmapClient->status != MMAP_INIT && pstMmapClient->status != MMAP_STOP_DONE) {
        AM_LOGE("mmap status is %d !", pstMmapClient->status);
        return -ENODATA;
    }

    pthread_mutex_lock(&pstMmapClient->statusMutex);
    pstMmapClient->status = MMAP_START;
    pthread_mutex_unlock(&pstMmapClient->statusMutex);
    return 0;
}

int mmap_audio_stop_client(void *pstMananger, int client_id)
{
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;
    aml_mmap_audio_client_st *pstMmapClient = NULL;

    if (pstMmapMananger == NULL || client_id < 0 || client_id >= AML_MMAP_AUDIO_CLIENT_MAX_NUM) {
        AM_LOGE("invalid param : pstMananger %p, client_id %d", pstMmapMananger, client_id);
        return -1;
    }
    pstMmapClient = pstMmapMananger->pstClientList[client_id];
    if (pstMmapClient == NULL || pstMmapClient->status != MMAP_START_DONE) {
        AM_LOGW("not start done or mmap not init");
        return -ENODATA;
    }
    pthread_mutex_lock(&pstMmapClient->statusMutex);
    pstMmapClient->status = MMAP_STOP;
    pthread_mutex_unlock(&pstMmapClient->statusMutex);
    AM_LOGI("stream %p, client id %d, stop",  pstMmapClient->stStream, client_id);
    return 0;
}

int mmap_audio_stop_client_complete(void *pstMananger, int client_id)
{
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;
    aml_mmap_audio_client_st *pstMmapClient = NULL;

    if (pstMmapMananger == NULL || client_id < 0 || client_id >= AML_MMAP_AUDIO_CLIENT_MAX_NUM) {
        AM_LOGE("invalid param : pstMananger %p, client_id %d", pstMmapMananger, client_id);
        return -1;
    }
    pstMmapClient = pstMmapMananger->pstClientList[client_id];
    if (pstMmapClient == NULL || pstMmapClient->status != MMAP_STOP) {
        AM_LOGW("not in stop status");
        return -ENODATA;
    }
    pthread_mutex_lock(&pstMmapClient->statusMutex);
    pstMmapClient->status = MMAP_STOP_DONE;
    pthread_mutex_unlock(&pstMmapClient->statusMutex);
    AM_LOGI("stream %p, client id %d, stop_done",  pstMmapClient->stStream, client_id);
    return 0;
}


static bool inline mmap_audio_client_is_active(aml_mmap_audio_client_st *pstMmapClient)
{
    if (pstMmapClient == NULL) {
        return false;
    }
    enum aml_mmap_audio_status_t status = pstMmapClient->status;
    if (!pstMmapClient->bBufferReady) {
        return false;
    }
    return (status == MMAP_START || status == MMAP_START_DONE || status == MMAP_STOP);
}

static enum aml_mmap_audio_status_t mmap_audio_get_client_status(void *pstMananger, int client_id)
{
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;
    aml_mmap_audio_client_st *pstMmapClient = NULL;
    enum aml_mmap_audio_status_t status = MMAP_INVALID;

    if (pstMananger == NULL || client_id < 0 || client_id >= AML_MMAP_AUDIO_CLIENT_MAX_NUM) {
        AM_LOGE("invalid param : pstMananger %p, client_id %d", pstMananger, client_id);
        return status;
    }
    pstMmapClient = pstMmapMananger->pstClientList[client_id];
    if (pstMmapClient != NULL) {
        status = pstMmapClient->status;
    }
    return status;
}

static void mmap_audio_set_buffer_ready(void *pstMananger, int client_id, bool buffer_ready)
{
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;
    aml_mmap_audio_client_st *pstMmapClient = NULL;

    if (pstMananger == NULL || client_id < 0 || client_id >= AML_MMAP_AUDIO_CLIENT_MAX_NUM) {
        AM_LOGE("invalid param : pstMananger %p, client_id %d", pstMananger, client_id);
        return;
    }
    pstMmapClient = pstMmapMananger->pstClientList[client_id];
    if (pstMmapClient != NULL) {
        pstMmapClient->bBufferReady = buffer_ready;
    }
}


static int mmap_audio_process_client_data(aml_mmap_audio_client_st *pstMmapClient, int frames)
{
    if (pstMmapClient == NULL || frames <= 0) {
        return -1;
    }
    int                       ret = 0;
    struct aml_stream_out    *out = pstMmapClient->stStream;
    aml_mmap_audio_param_st  *pstParam = (aml_mmap_audio_param_st *)out->pstMmapAudioParam;
    unsigned char            *pu8CurReadAddr = pstParam->pu8CurReadAddr;
    unsigned char            *pu8StartAddr = pstParam->pu8MmapAddr;
    unsigned int             u32BurstSizeByte = frames * pstParam->u32FrameSize;
    unsigned char            *pu8TempBufferAddr = NULL;
    struct timespec          timestamp;

    if (!mmap_audio_client_is_active(pstMmapClient)) {
        pstMmapClient->u32BytesAvail = 0;
        pstMmapClient->u32FramesAvail = 0;
        return 0;
    }

    ret = aml_audio_check_and_realloc((void **)&pstMmapClient->pu8TempBuf, &pstMmapClient->uxTempBufLen, u32BurstSizeByte);
    if ((ret != 0) || (pstMmapClient->pu8TempBuf == NULL)) {
        AM_LOGE("allocate tempBuf(%d bytes) failed", u32BurstSizeByte);
        return -1;
    }
    pu8TempBufferAddr = pstMmapClient->pu8TempBuf;

    if (pstMmapClient->status == MMAP_START) {
       AM_LOGI("client %d ====> start", pstMmapClient->s32AllocId);
       pu8CurReadAddr = pu8StartAddr;
       pstParam->u32FramePosition = 0;
       clock_gettime(CLOCK_MONOTONIC, &timestamp);
       pstParam->time_nanoseconds = (long long)timestamp.tv_sec * NSEC_PER_SEC + (long long)timestamp.tv_nsec;
       pstMmapClient->status = MMAP_START_DONE;
    }

    unsigned int u32RemainSizeByte =  (pstParam->u32BufferSize + pu8StartAddr) - pu8CurReadAddr;
    if (u32RemainSizeByte >= u32BurstSizeByte) {

        memcpy(pu8TempBufferAddr, pu8CurReadAddr, u32BurstSizeByte);
        memset(pu8CurReadAddr, 0, u32BurstSizeByte);
        pu8CurReadAddr += u32BurstSizeByte;
    } else {
        memcpy(pu8TempBufferAddr, pu8CurReadAddr, u32RemainSizeByte);
        memset(pu8CurReadAddr, 0, u32RemainSizeByte);

        memcpy(pu8TempBufferAddr + u32RemainSizeByte, pu8StartAddr, u32BurstSizeByte - u32RemainSizeByte);
        memset(pu8StartAddr, 0, u32BurstSizeByte - u32RemainSizeByte);
        pu8CurReadAddr = pu8StartAddr + u32BurstSizeByte - u32RemainSizeByte;
    }
    pstParam->u32FramePosition += frames;
    pstParam->pu8CurReadAddr = pu8CurReadAddr;
    // Absolute time must be used when get timestamp.
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    pstParam->time_nanoseconds = (long long)timestamp.tv_sec * NSEC_PER_SEC + (long long)timestamp.tv_nsec;

    if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
        char aaudio_name[64];
        memset(aaudio_name, 0, sizeof(aaudio_name));
        snprintf(aaudio_name, sizeof(aaudio_name)-1, "aaudio_in_%d", pstMmapClient->s32AllocId);
        check_audio_level(aaudio_name, pu8TempBufferAddr, u32BurstSizeByte);
    }
    if (aml_getprop_bool("vendor.media.audiohal.outdump")) {
        char filepath[64];
        memset(filepath, 0, sizeof(filepath));
        snprintf(filepath, sizeof(filepath)-1, "%s_%d", MMAP_INPUT_FILE, pstMmapClient->s32AllocId);
        aml_audio_dump_audio_bitstreams(filepath, pu8TempBufferAddr, u32BurstSizeByte);
    }

    apply_volume(out->volume_l, pu8TempBufferAddr, 2, u32BurstSizeByte);
    pstMmapClient->u32BytesAvail = u32BurstSizeByte;
    pstMmapClient->u32FramesAvail = frames;

    // NTS AUDIO-AGGRPLAYDELAY
    // All ui audio data should be played and stop-start latency still be short.
    if (pstMmapClient->status == MMAP_STOP) {
        int64_t interval_ns = pstParam->time_nanoseconds - pstParam->s64BufferEmptyNs;

        pthread_mutex_lock(&pstMmapClient->statusMutex);
        if (pstMmapClient->status == MMAP_STOP && interval_ns >= 0) {
            pstMmapClient->status = MMAP_STOP_DONE;
            memset(pstParam->pu8MmapAddr, 0, pstParam->u32BufferSize);
            pstParam->u32FramePosition = 0;
            pstParam->s64BufferEmptyNs = 0;
            AM_LOGI("stream %p, client id %d, stop_done", out, pstMmapClient->s32AllocId);
        }
        pthread_mutex_unlock(&pstMmapClient->statusMutex);
    }

    if (out->dev->debug_flag) {
        int buffer_frames = pstParam->u32BufferSize/pstParam->u32FrameSize;
        AM_LOGI("Id %d, CurReadAddr:%p, RemainSize:%d, FramePosition:%d offset=%d",
            pstMmapClient->s32AllocId, pu8CurReadAddr, u32RemainSizeByte, pstParam->u32FramePosition,
            pstParam->u32FramePosition % buffer_frames);
    }
    return frames;
}


int mmap_audio_process_data(void *pstMananger, int frames)
{
    int client_id = 0;
    int ret_frames = 0;
    int process_frames = 0;
    aml_mmap_audio_client_st *pstMmapClient = NULL;
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;
    if (pstMmapMananger == NULL) {
        return -1;
    }

    pstMmapMananger->u32ProcessFrames = 0;
    for (client_id = 0; client_id < AML_MMAP_AUDIO_CLIENT_MAX_NUM; client_id++) {
        pstMmapClient = pstMmapMananger->pstClientList[client_id];
        if (pstMmapClient && pstMmapClient->stStream) {
            process_frames = mmap_audio_process_client_data(pstMmapClient, frames);
            if (process_frames > 0) {
                ret_frames = process_frames;
                pstMmapMananger->u32ProcessFrames = process_frames;
            }
        }
    }
    return ret_frames;
}


int mmap_audio_prepare_merge(void *pstMananger, struct audioCfg *pstMergeCfg, bool is_netflix)
{
    int client_id = 0;
    bool is_empty = true;
    uint32_t sampleRate = 0;
    struct audioCfg *pstClientCfg = NULL;
    uint32_t in_max_ch_count = 1;
    audio_channel_mask_t in_max_ch_mask = AUDIO_CHANNEL_OUT_MONO;
    aml_mmap_audio_client_st *pstMmapClient = NULL;
    aml_pcm_mixing_st         *pstMultichMixer = NULL;
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;

    if (pstMmapMananger == NULL || pstMergeCfg == NULL) {
        return -1;
    }

    for (client_id = 0; client_id < AML_MMAP_AUDIO_CLIENT_MAX_NUM; client_id++) {
        pstMmapClient = pstMmapMananger->pstClientList[client_id];
        if (pstMmapClient == NULL) {
            continue;
        }
        pstClientCfg = &pstMmapClient->stCfg;
        sampleRate = pstClientCfg->sampleRate;
        if (sampleRate != 48000) {
            AM_LOGE("not support sampleRate %d, client id %d", sampleRate, client_id);
            return -1;
        }
        if (pstClientCfg->channelCnt > in_max_ch_count) {
            in_max_ch_count = pstClientCfg->channelCnt;
            in_max_ch_mask = pstClientCfg->channelMask;
        }
        is_empty = false;
    }

    if (is_netflix && in_max_ch_count <= 6) {
        in_max_ch_count = 6;
        in_max_ch_mask = AUDIO_CHANNEL_OUT_5POINT1;
    } else if (is_empty) {
        in_max_ch_count = 2;
        in_max_ch_mask = AUDIO_CHANNEL_OUT_STEREO;
    }

    memset(pstMergeCfg, 0, sizeof(*pstMergeCfg));
    pstMergeCfg->sampleRate = 48000;
    pstMergeCfg->channelCnt = in_max_ch_count;
    pstMergeCfg->channelMask = in_max_ch_mask;
    pstMergeCfg->format = AUDIO_FORMAT_PCM_16_BIT;
    pstMergeCfg->frame_size = audio_bytes_per_frame(pstMergeCfg->channelCnt, pstMergeCfg->format);
    return 0;
}


int mmap_audio_merge_data(void *pstMananger, const struct audioCfg *pstMergeCfg, uint8_t *pu8DataBuf, uint32_t u32DataBytes)
{
    int ret = 0;
    int client_id = 0;
    uint32_t u32CopyBytes = 0;
    uint8_t *mixed_data_ptr = NULL;
    uint32_t mixed_data_size = 0;
    uint32_t u32ProcessFrames = 0;
    aml_mmap_audio_client_st  *pstMmapClient = NULL;
    aml_pcm_mixing_st         *pstMultichMixer = NULL;
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;

    if (pstMmapMananger == NULL
        || pstMmapMananger->u32ProcessFrames == 0
        || pstMergeCfg->frame_size == 0
        || pu8DataBuf == NULL
        || u32DataBytes == 0) {
        return -1;
    }
    if (pstMmapMananger->u32ProcessFrames != pstMmapMananger->s32WriteSizeFrame) {
        AM_LOGW("MMAP writeSizeFrame(%d) != processFrames(%d), maybe abnormal !",
            pstMmapMananger->s32WriteSizeFrame, pstMmapMananger->u32ProcessFrames);
    }

    u32ProcessFrames = pstMmapMananger->u32ProcessFrames;
    pstMultichMixer = &pstMmapMananger->stMultichMixer;
    if (memcmp(&pstMultichMixer->cfg, pstMergeCfg, sizeof(*pstMergeCfg))
        || pstMultichMixer->mixed_frames < u32ProcessFrames) {
        AM_LOGI("multichMixer config change, re-init");
        deinit_aml_pcm_mixer(pstMultichMixer);
        if (init_aml_pcm_mixer(pstMultichMixer, pstMergeCfg, u32ProcessFrames)) {
            AM_LOGI("init multichMixer failed !");
            return -1;
        }
    }
    memset(pstMultichMixer->mixed_buf, 0, pstMultichMixer->mixed_buf_size);

    for (client_id = 0; client_id < AML_MMAP_AUDIO_CLIENT_MAX_NUM; client_id++) {
        pstMmapClient = pstMmapMananger->pstClientList[client_id];
        if (pstMmapClient && pstMmapClient->u32FramesAvail) {

            do_mixing_multi_ch(pstMultichMixer, pstMmapClient->pu8TempBuf, \
                        pstMmapClient->u32FramesAvail, &pstMmapClient->stCfg);
            pstMmapClient->u32FramesAvail = 0;
            pstMmapClient->u32BytesAvail = 0;
        }
    }
    pstMmapMananger->u32ProcessFrames = 0;
    mixed_data_ptr = pstMultichMixer->mixed_buf;
    mixed_data_size = u32ProcessFrames * pstMergeCfg->frame_size;

    u32CopyBytes = mixed_data_size;
    if (u32CopyBytes > u32DataBytes) {
        AM_LOGW("buffer too small, truncate data bytes %d -> %d", u32CopyBytes, u32DataBytes);
        u32CopyBytes = u32DataBytes;
    }
    memcpy(pu8DataBuf, mixed_data_ptr, u32CopyBytes);

    return u32CopyBytes;
}


bool mmap_audio_has_active_client(void *pstMananger)
{
    int client_id = 0;
    int ret_frames = 0;
    int process_frames = 0;
    aml_mmap_audio_client_st *pstMmapClient = NULL;
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;
    if (pstMmapMananger == NULL) {
        return false;
    }

    for (client_id = 0; client_id < AML_MMAP_AUDIO_CLIENT_MAX_NUM; client_id++) {
        pstMmapClient = pstMmapMananger->pstClientList[client_id];
        if (pstMmapClient && pstMmapClient->stStream) {
            if (mmap_audio_client_is_active(pstMmapClient)) {
                return true;
            }
        }
    }
    return false;
}


int mmap_audio_max_client_num()
{
    return AML_MMAP_AUDIO_CLIENT_MAX_NUM;
}


int mmap_audio_read_client_data(void *pstMananger, int client_id, uint8_t *pu8DataBuf, uint32_t u32BufBytes)
{
    int ret = 0;
    uint32_t u32CopyBytes = 0;
    aml_mmap_audio_client_st *pstMmapClient = NULL;
    aml_mmap_audio_manager_st *pstMmapMananger = pstMananger;
    uint32_t u32ProcessFrames = 0;

    if (pstMmapMananger == NULL) {
        return -1;
    }
    if (client_id < 0 || client_id >= AML_MMAP_AUDIO_CLIENT_MAX_NUM) {
        AM_LOGE("invalid client_id %d", client_id);
        return -1;
    }
    pstMmapClient = pstMmapMananger->pstClientList[client_id];
    if (pstMmapClient == NULL || pstMmapClient->u32BytesAvail == 0) {
        return 0;
    }

    u32CopyBytes = pstMmapClient->u32BytesAvail;
    if (u32CopyBytes > u32BufBytes) {
        AM_LOGW("buffer too small, truncate data bytes %d -> %d", u32CopyBytes, u32BufBytes);
        u32CopyBytes = u32BufBytes;
    }

    memcpy(pu8DataBuf, pstMmapClient->pu8TempBuf, u32CopyBytes);
    pstMmapClient->u32BytesAvail = 0;
    pstMmapClient->u32FramesAvail = 0;
    return u32CopyBytes;
}

