/*
 * Copyright (C) 2010 Amlogic Corporation.
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



#ifndef _AUDIO_HWSYNC_H_
#define _AUDIO_HWSYNC_H_

#include <stdbool.h>

#define SYSTIME_CORRECTION_THRESHOLD        (90000*10/100)
#define NSEC_PER_SECOND 1000000000ULL
#define HW_SYNC_STATE_HEADER           0
#define HW_SYNC_STATE_PAYLOAD          1
#define HW_SYNC_STATE_RESYNC           2
#define HW_SYNC_STATE_METADATA_HEADER  3
#define HW_SYNC_STATE_METADATA_UNITS   4

#define HW_SYNC_VERSION_SIZE     4

#define HW_AVSYNC_HEADER_SIZE_V1 16
#define HW_AVSYNC_HEADER_SIZE_V2 20
#define HW_AVSYNC_HEADER_SIZE_V3 10
#define HW_AVSYNC_METADATA_HEADER_SIZE 6
#define HW_AVSYNC_METADATA_UNIT_HEADER_SIZE 4
#define HW_AVSYNC_METADATA_UNIT_TUNER_SIZE  9
#define HW_AVSYNC_METADATA_UNIT_DVB_AD_SIZE 6

#define HW_AVSYNC_PAYLOAD_HEADER_SIZE 6




#define HW_AVSYNC_FLAG_TIMESTAMP_PRESENT  1 << 15
#define HW_AVSYNC_FLAG_METADATA_PRESENT   1 << 14
#define HW_AVSYNC_FLAG_PAYLOAD_PRESENT    1 << 13
#define HW_AVSYNC_FLAG_UNIT_START_PRESENT 1 << 12
#define HW_AVSYNC_FLAG_ERROR_PRESENT      1 << 11


/*head size is calculated with mOffset = ((int) Math.ceil(HEADER_V2_SIZE_BYTES / frameSizeInBytes)) * frameSizeInBytes;
 *current we only support to 8ch, the headsize is 32
 */
#define HW_AVSYNC_MAX_HEADER_SIZE  32  /*max 8ch */


//TODO: After precisely calc the pts, change it back to 1s
#define APTS_DISCONTINUE_THRESHOLD          (90000/10*11)
#define APTS_DISCONTINUE_THRESHOLD_MIN    (90000/1000*100)
#define APTS_DISCONTINUE_THRESHOLD_MIN_35MS    (90000/1000*35)

#define APTS_DISCONTINUE_THRESHOLD_MAX    (5*90000)

#define HWSYNC_APTS_NUM     512
#define HWSYNC_MAX_BODY_SIZE  (32768)  ///< Will do fine tune according to the bitstream.
#define HWSYNC_MAX_METADATA_UNIT_SIZE  (16384) ///< Will do fine tune according to the bitstream.


enum hwsync_status {
    CONTINUATION,  // good sync condition
    ADJUSTMENT,    // can be adjusted by discarding or padding data
    RESYNC,        // pts need resync
};

enum tsync_status {
    TSYNC_STATUS_INIT,
    TSYNC_STATUS_RUNNING,
    TSYNC_STATUS_PAUSED,
    TSYNC_STATUS_STOP
};

typedef struct apts_tab {
    int  valid;
    size_t offset;
    uint64_t pts;
} apts_tab_t;

/*
 *TUNER FRAMEWORK Encapsulation DEFINITION
 */
typedef enum aml_hw_avsync_metadata_type {
    /*ENCAPSULATION_METADATA_TYPE_NONE = 0,
    ENCAPSULATION_METADATA_TYPE_FRAMEWORK_TUNER = 1,
    ENCAPSULATION_METADATA_TYPE_DVB_AD_DESCRIPTOR = 2,*/
    ENCAPSULATION_METADATA_TYPE_AD_PLACEMENT = 10002,
} aml_hw_avsync_metadata_type_t;

/*
typedef enum audio_encapsulation_mode {
    AUDIO_ENCAPSULATION_MODE_NONE              = 0,
    AUDIO_ENCAPSULATION_MODE_ELEMENTARY_STREAM = 1,
    AUDIO_ENCAPSULATION_MODE_HANDLE            = 2,
} audio_encapsulation_mode_t;
*/
typedef enum metadata_type_ad_placement {
    PLACEMENT_NORMAL = 0,
    PLACEMENT_RIGHT = 1,
    PLACEMENT_LEFT = 2,
} metadata_type_ad_placement_t;

typedef enum metadata_broadcast_type {
    AUDIO_BROADCAST_MAIN = 0,
    AUDIO_BROADCAST_AUDIO_DESCRIPTION = 1,
 } metadata_broadcast_type_t;

typedef struct hw_avsync_metadata_unit_info {
    uint32_t stream_id;
    uint32_t flags;
    uint8_t  broadcast_type;
} hw_avsync_metadata_unit_info_t;
typedef struct hw_avsync_metadata_dvb_ad{
    uint8_t mAdPan;
    uint8_t mAdFade;
    uint8_t mAdVersionTextTag;
    uint8_t mAdGainCenter;      // only valid if VersionTextTag == 0x32
    uint8_t mAdGainFront;        // only valid if VersionTextTag == 0x32
    uint8_t mAdGainSurround;  // only valid if VersionTextTag == 0x32
} hw_avsync_metadata_dvb_ad_t;


typedef struct  audio_hwsync {
    uint8_t hw_sync_header[HW_AVSYNC_MAX_HEADER_SIZE];
    uint8_t hw_sync_metadata_header[HW_AVSYNC_METADATA_HEADER_SIZE];
    uint8_t hw_sync_metadata_unit_header[HW_AVSYNC_METADATA_UNIT_HEADER_SIZE];
	uint16_t header_flags;
    size_t hw_sync_header_cnt;
    size_t hw_sync_metadata_header_cnt;
    size_t hw_sync_metadata_unit_header_cnt;
    uint8_t hw_sync_payload_header[HW_AVSYNC_PAYLOAD_HEADER_SIZE];
    size_t hw_sync_payload_header_cnt;
    uint32_t hw_sync_payload_unit_size;
    uint32_t hw_sync_payload_unit_cnt;
    uint16_t hw_sync_encapsulation_mode;
    int hw_sync_state;
    uint32_t hw_sync_body_cnt;
    uint32_t hw_sync_frame_size;
    uint32_t hw_sync_metadata_total_size;
    uint32_t hw_sync_metadata_size_cnt;
    uint16_t hw_sync_metadata_units_cnt;
    uint16_t hw_sync_metadata_unit_type;
    uint16_t hw_sync_metadata_unit_size;
    uint16_t hw_sync_metadata_unit_size_cnt;
    hw_avsync_metadata_unit_info_t current_metadata_unit;
    hw_avsync_metadata_dvb_ad_t metadata_dvb_ad_info;
    uint8_t hw_sync_metadata_placement;
    int      bvariable_frame_size;
    uint8_t hw_sync_body_buf[HWSYNC_MAX_BODY_SIZE];
    uint8_t hw_sync_metadata_unit_buf[HWSYNC_MAX_METADATA_UNIT_SIZE];
    uint8_t body_align[64];
    uint8_t body_align_cnt;
    bool first_apts_flag;//flag to indicate set first apts
    uint64_t first_apts;
    uint64_t last_apts_from_header;
    apts_tab_t pts_tab[HWSYNC_APTS_NUM];
    pthread_mutex_t lock;
    size_t payload_offset;
    struct aml_stream_out  *aout;
    int tsync_fd;
    int version_num;
    bool use_mediasync;
    void* mediasync;
    int hwsync_id;
    uint64_t last_output_pts;
    struct timespec  last_timestamp;
    bool wait_video_done;
} audio_hwsync_t;
static inline bool hwsync_header_valid(uint8_t *header)
{
    return (header[0] == 0x55) &&
           (header[1] == 0x55) &&
           (header[2] == 0x00) &&
           (header[3] == 0x01 || header[3] == 0x02 || header[3] == 0x03);
}

static inline uint64_t hwsync_header_get_pts(uint8_t *header)
{
    return (((uint64_t)header[8]) << 56) |
           (((uint64_t)header[9]) << 48) |
           (((uint64_t)header[10]) << 40) |
           (((uint64_t)header[11]) << 32) |
           (((uint64_t)header[12]) << 24) |
           (((uint64_t)header[13]) << 16) |
           (((uint64_t)header[14]) << 8) |
           ((uint64_t)header[15]);
}


static inline uint64_t hwsync_header_get_pts_v3(uint8_t *header)
{
    return (((uint64_t)header[10]) << 56) |
           (((uint64_t)header[11]) << 48) |
           (((uint64_t)header[12]) << 40) |
           (((uint64_t)header[13]) << 32) |
           (((uint64_t)header[14]) << 24) |
           (((uint64_t)header[15]) << 16) |
           (((uint64_t)header[16]) << 8) |
           ((uint64_t)header[17]);
}

static inline uint32_t hwsync_header_get_size(uint8_t *header)
{
    return (((uint32_t)header[4]) << 24) |
           (((uint32_t)header[5]) << 16) |
           (((uint32_t)header[6]) << 8) |
           ((uint32_t)header[7]);
}

static inline uint32_t hwsync_header_get_offset(uint8_t *header)
{
    return (((uint32_t)header[16]) << 24) |
           (((uint32_t)header[17]) << 16) |
           (((uint32_t)header[18]) << 8) |
           ((uint32_t)header[19]);
}

static inline uint16_t hwsync_header_get_flags(uint8_t *header)
{
    return (((uint16_t)header[8]) << 8) |
           ((uint16_t)header[9]);
}

static inline uint32_t hwsync_metadata_get_size(uint8_t *header)
{
    return (((uint32_t)header[0]) << 24) |
           (((uint32_t)header[1]) << 16) |
           (((uint32_t)header[2]) << 8) |
           ((uint32_t)header[3]);
}

static inline uint16_t hwsync_metadata_get_units_cnt(uint8_t *header)
{
    return (((uint16_t)header[4]) << 8) |
           ((uint16_t)header[5]);
}

static inline uint16_t hwsync_metadata_get_unit_type(uint8_t *header)
{
    return (((uint16_t)header[0]) << 8) |
           ((uint16_t)header[1]);
}
static inline uint16_t hwsync_metadata_get_unit_size(uint8_t *header)
{
    return (((uint16_t)header[2]) << 8) |
           ((uint16_t)header[3]);
}

static inline void hwsync_metadata_get_unit_info(uint8_t *metadata,hw_avsync_metadata_unit_info_t *current_metadata_unit)
{
    if (metadata != NULL) {
        current_metadata_unit->stream_id = (((uint32_t)metadata[0]) << 24) |
                                           (((uint32_t)metadata[1]) << 16) |
                                           (((uint32_t)metadata[2]) << 8) |
                                           ((uint32_t)metadata[3]);
        current_metadata_unit->flags = (((uint32_t)metadata[4]) << 24) |
                                       (((uint32_t)metadata[5]) << 16) |
                                       (((uint32_t)metadata[6]) << 8) |
                                       ((uint32_t)metadata[7]);
        current_metadata_unit->broadcast_type = ((uint8_t)metadata[8]);

    } else {
        ALOGI("metadata NULL !!!");
    }
}
static inline uint16_t hwsync_metadata_get_placement (uint8_t *header)
{
    return (uint16_t)header[0];
}

static inline uint32_t hwsync_payload_get_size(uint8_t *header)
{
    return (((uint32_t)header[0]) << 24) |
           (((uint32_t)header[1]) << 16) |
           (((uint32_t)header[2]) << 8) |
           ((uint32_t)header[3]);
}

static inline uint16_t hwsync_payload_get_encapsulation_mode (uint8_t *header)
{
    return (((uint16_t)header[4]) << 8) |
           ((uint16_t)header[5]);
}

static inline void hwsync_metadata_get_ad_info(uint8_t *metadata,hw_avsync_metadata_dvb_ad_t *hw_avsync_metadata_dvb_ad)
{
    if (metadata != NULL) {
        hw_avsync_metadata_dvb_ad->mAdPan =  (uint8_t)metadata[0];
        hw_avsync_metadata_dvb_ad->mAdFade = (uint8_t)metadata[1];
        hw_avsync_metadata_dvb_ad->mAdVersionTextTag= (uint8_t)metadata[2];
        hw_avsync_metadata_dvb_ad->mAdGainCenter= (uint8_t)metadata[3];
        hw_avsync_metadata_dvb_ad->mAdGainFront= (uint8_t)metadata[4];
        hw_avsync_metadata_dvb_ad->mAdGainSurround= (uint8_t)metadata[5];
    } else {
        ALOGI("metadata NULL !!!");
    }
}



static inline uint64_t get_pts_gap(uint64_t a, uint64_t b)
{
    if (a >= b) {
        return (a - b);
    } else {
        return (b - a);
    }
}


int aml_audio_hwsync_find_frame(audio_hwsync_t *p_hwsync,
        const void *in_buffer, size_t in_bytes,
        uint64_t *cur_pts, int *outsize);
int aml_audio_hwsync_set_first_pts(audio_hwsync_t *p_hwsync, uint64_t pts);
int aml_audio_hwsync_checkin_apts(audio_hwsync_t *p_hwsync, size_t offset, uint64_t apts);
int aml_audio_hwsync_lookup_apts(audio_hwsync_t *p_hwsync, size_t offset, uint64_t *p_apts);
int aml_audio_hwsync_audio_process(audio_hwsync_t *p_hwsync, size_t offset, int frame_len, int *p_adjust_ms);
void aml_audio_hwsync_init(audio_hwsync_t *p_hwsync, struct aml_stream_out  *out);

void* aml_audio_hwsync_create(void);
void aml_audio_hwsync_release(audio_hwsync_t *p_hwsync);

int aml_audio_hwsync_open(void);
int aml_audio_hwsync_close(void);

#endif
