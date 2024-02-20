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

#ifndef _AML_MMAP_AUDIO_H_
#define _AML_MMAP_AUDIO_H_

#include <ion/ion.h>

#if ANDROID_PLATFORM_SDK_VERSION >= 31
#include <BufferAllocator/BufferAllocatorWrapper.h>
#endif

typedef struct AML_MMAP_THREAD_PARAM {
    pthread_t               threadId;
    bool                    bExitThread;
    bool                    bStopPlay;
    int                     status;
    pthread_condattr_t      condAttr;
    pthread_mutex_t         mutex;
    pthread_cond_t          cond;
} aml_mmap_thread_param_st;

typedef struct AML_MMAP_AUDIO_PARAM {
    unsigned char               *pu8MmapAddr;
    unsigned char               *pu8CurReadAddr;

    // ION Buffer information
    ion_user_handle_t           hIonHandle;
    int                         s32IonFd;
    int                         s32IonShareFd;

    // DMA Buffer information
    BufferAllocator*            pstBufferAllocator;
    int                         s32DmaFd;

    unsigned int                u32FramePosition;
    unsigned int                u32FrameSize;
    unsigned int                u32BufferSize;
    int64_t                     time_nanoseconds;
    int64_t                     s64BufferEmptyNs;
    /*This variable is used for mark mmap stream position function invoked by first time.
    **It was fixed for VTS case(GetMmapPositionOfNonMmapedStream).
    */
    bool is_first_fetch_position;
} aml_mmap_audio_param_st;



int outMmapInit(struct aml_stream_out *out);
int outMmapDeInit(struct aml_stream_out *out);


void* mmap_audio_new_manager(bool is_ms12);
void mmap_audio_free_manager(void *pstMananger);
int mmap_audio_register_client(void *pstMananger, struct aml_stream_out *aml_out);
void mmap_audio_unregister_client(void *pstMananger, int client_id);
int mmap_audio_start_client(void *pstMananger, int client_id);
int mmap_audio_stop_client(void *pstMananger, int client_id);
int mmap_audio_stop_client_complete(void *pstMananger, int client_id);
int mmap_audio_process_data(void *pstMananger, int frames);
int mmap_audio_prepare_merge(void *pstMananger, struct audioCfg *pstMergeCfg, bool is_netflix);
int mmap_audio_merge_data(void *pstMananger, const struct audioCfg *pstMergeCfg, uint8_t *pu8DataBuf, uint32_t u32DataBytes);
int mmap_audio_get_burst_info(void *pstMananger, int32_t *ps32WriteSizeFrame, int32_t *ps32BufferBurstNum);


bool mmap_audio_has_active_client(void *pstMananger);
int mmap_audio_max_client_num();

#endif
