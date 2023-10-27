/*
* Copyright (C) 2023 Amlogic Corporation.
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


#define LOG_TAG "audio_hw_hal_asyncwrite"
//#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cutils/log.h>
#include <sys/prctl.h>
#include <errno.h>
#include <inttypes.h>
#include <cutils/list.h>
#include <audio_utils/primitives.h>

#include "audio_hw_utils.h"
#include "aml_audio_timer.h"
#include "aml_ringbuffer.h"
#include "aml_async_write.h"
#include "aml_audio_spdifdec.h"

#ifdef AML_ASYNC_WRITE_COMPRESS_ENABLE
#include "zlib.h"
#endif


// uint : bytes
#define  BUFFER_DEFAULT_SIZE    (32 * 1024)
#define  TEMP_BUFFER_SIZE       (32 * 1024)
#define  BUFFER_MAX_SIZE        (2 * 1024 * 1024) // 2 MBytes

/*
 * DDP/MAT IEC's data rate is high and have many zero. We can reduce storage
 * by compress it, but it doesn't happen when data is PCM or others.
 * So we only compress DDP/MAT IEC data currently.
*/
#define  IEC_DETECT_SIZE        (24 * 1024)
#define  COMPRESS_OUT_SIZE      (32 * 1024)

// If buffer expired, its memory will be recycled
#define  BUFFER_EXPIRE_MS       (15*1000)  // 15s

#define  FILENAME_MAX_LEN        256


#ifndef ALIGN
#define ALIGN(size, align) ((size + align - 1) & (~(align - 1)))
#endif

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

struct buffer_item {
    struct listnode  list_node;
    char             filename[FILENAME_MAX_LEN];
    ring_buffer_t    ring_buffer;
    int              to_remove;
    int              is_writing;
    uint64_t         last_update_ms;
    int              file_error_count;
    uint64_t         file_error_ms;

    int              is_compress;
    uint8_t          *compress_buf;
#ifdef AML_ASYNC_WRITE_COMPRESS_ENABLE
    int              compress_level;
    z_stream         compress_stream;
    int              compress_buflen;
#endif
};


struct aml_async_writer {
    pthread_t        writer_threadID;
    struct listnode  buffer_list_head;
    pthread_mutex_t  writer_mutex;
    uint8_t          *temp_bufptr;
    int              temp_buflen;

    pthread_mutex_t   wake_mutex;
    pthread_cond_t    wake_cond;
    int               is_standby;
};


static struct aml_async_writer worker1;


#ifdef AML_ASYNC_WRITE_COMPRESS_ENABLE
static int compress_init(struct buffer_item *buf_item)
{
    int ret = 0;
    uint8_t *out_buf = NULL;
    int out_buflen = COMPRESS_OUT_SIZE;

    if (buf_item == NULL) {
        return -1;
    }

    int level = buf_item->compress_level;
    z_stream *p_stream = &buf_item->compress_stream;

    out_buf = (uint8_t *)aml_audio_calloc(1, out_buflen);
    if (out_buf == NULL) {
        buf_item->compress_buf = NULL;
        ALOGE("%s : can not malloc %d bytes ! (%s)", __func__, out_buflen, strerror(errno));
        return -1;
    }

    /* allocate deflate state */
    p_stream->zalloc = Z_NULL;
    p_stream->zfree = Z_NULL;
    p_stream->opaque = Z_NULL;
    ret = deflateInit(p_stream, level);
    if (ret != Z_OK) {
        aml_audio_free(out_buf);
        buf_item->compress_buf = NULL;
        ALOGE("%s failed with %d", __func__, ret);
        return -1;
    }

    buf_item->compress_buf = out_buf;
    buf_item->compress_buflen = out_buflen;
    ALOGI("%s : %s successfully", __func__, buf_item->filename);
    return ret;
}


static int compress_and_save(struct buffer_item *buf_item, unsigned char *in, int in_bytes, int is_data_end, FILE *fp)
{
    int ret, flush;
    unsigned have;
    z_stream *p_stream = NULL;
    uint8_t *out_buf = NULL;
    int out_buflen = 0;

    if ((buf_item == NULL) || (in == NULL) || (in_bytes == 0) || (fp == NULL)) {
        return -1;
    }

    p_stream = &buf_item->compress_stream;
    out_buf = buf_item->compress_buf;
    out_buflen = buf_item->compress_buflen;

    if (in_bytes > out_buflen) {
        ALOGE("in_bytes(%d > %d) is too large!", in_bytes, out_buflen);
        return -1;
    }

    p_stream->avail_in = in_bytes;
    flush = is_data_end ? Z_FINISH : Z_NO_FLUSH;
    p_stream->next_in = in;

    /* run deflate() on input until output buffer not full, finish
       compression if all of source has been read in */
    do {
        p_stream->avail_out = out_buflen;
        p_stream->next_out = out_buf;
        ret = deflate(p_stream, flush);    /* no bad return value */
        if (ret == Z_STREAM_ERROR) {
            ALOGE("%s deflate stream error !", __func__);
            return ret;
        }
        have = out_buflen - p_stream->avail_out;

        //if (fwrite(out, 1, have, dest) != have || ferror(dest))
        fwrite(out_buf, 1, have, fp);

    } while (p_stream->avail_out == 0);

    if (flush == Z_FINISH) {
        /* clean up and return */
        (void)deflateEnd(p_stream);

        aml_audio_free(buf_item->compress_buf);
        buf_item->compress_buf = NULL;
        buf_item->compress_buflen = 0;
    }
    return Z_OK;
}


static void compress_deinit(struct buffer_item *buf_item)
{
    if (buf_item == NULL) {
        return;
    }

    FILE *fp = fopen(buf_item->filename, "a+");
    if (fp) {
        unsigned char zero_buf[32];
        compress_and_save(buf_item, zero_buf, sizeof(zero_buf), 1, fp);
        fclose(fp);
    }

    if (buf_item->compress_buf) {
        (void)deflateEnd(&buf_item->compress_stream);
        aml_audio_free(buf_item->compress_buf);
        buf_item->compress_buf = NULL;
    }
    buf_item->compress_buflen = 0;
}
#else
static int compress_init(struct buffer_item *buf_item)
{
    UNUSED(buf_item);
    return 0;
}

static int compress_and_save(struct buffer_item *buf_item, unsigned char *in, int in_bytes, int is_data_end, FILE *fp)
{
    UNUSED(buf_item);
    UNUSED(in);
    UNUSED(in_bytes);
    UNUSED(is_data_end);
    UNUSED(fp);
    return 0;
}

static void compress_deinit(struct buffer_item *buf_item)
{
    UNUSED(buf_item);
}
#endif


static void handle_buffer_write(struct aml_async_writer *p_worker, struct buffer_item *buf_item, uint64_t system_ms)
{
    struct ring_buffer *rbuffer = &buf_item->ring_buffer;
    int bufsize = TEMP_BUFFER_SIZE;
    int handle_bytes = 0;
    int write_bytes = 0;

    if ((p_worker == NULL) || (buf_item == NULL)) {
        return;
    }

    int avail_bytes = get_buffer_read_space(rbuffer);
    if (avail_bytes <= 0) {
        ALOGV("Not data to read !");
        return;
    }

    FILE *fp = fopen(buf_item->filename, "a+");
    if (fp == NULL) {
        if (buf_item->file_error_count == 0) {
            buf_item->file_error_ms = system_ms;
        }
        buf_item->file_error_count++;

        if ((buf_item->file_error_count < 5) || (buf_item->file_error_count % 30 == 0)) {
            // reduce error message
            ALOGE("%s : open file %s failed ! (%s)", __func__, buf_item->filename, strerror(errno));
        }
        return;
    } else {
        if (buf_item->file_error_count != 0) {
            ring_buffer_clear(rbuffer);
            buf_item->file_error_count = 0;
            buf_item->file_error_ms = 0;
            ALOGI("%s : open file %s success, clear the stale data", __func__, buf_item->filename);
            fclose(fp);
            return;
        }
    }
    buf_item->last_update_ms = system_ms;

    if (p_worker->temp_bufptr == NULL) {
        uint8_t *ptr = (uint8_t *)aml_audio_calloc(1, bufsize);
        if (ptr == NULL) {
            ALOGE("%s : can not malloc %d bytes ! (%s)", __func__, bufsize, strerror(errno));
            return;
        }
        p_worker->temp_bufptr = ptr;
        p_worker->temp_buflen = bufsize;
        ALOGI("%s : malloc temp_buf ok", __func__);
    }

    while (write_bytes < avail_bytes) {
        handle_bytes = avail_bytes - write_bytes;
        if (handle_bytes > bufsize) {
            handle_bytes = bufsize;
        }
        if (ring_buffer_read(rbuffer, p_worker->temp_bufptr, handle_bytes) != handle_bytes) {
            ALOGE("%s : %s ring_buffer_read error!", __func__, buf_item->filename);
            break;
        }

        if (buf_item->is_compress) {
            if (buf_item->compress_buf == NULL) {
                compress_init(buf_item);
            }
            if (buf_item->compress_buf) {
                compress_and_save(buf_item, p_worker->temp_bufptr, handle_bytes, 0, fp);
                ALOGV("%s : %s compress ....", __func__, buf_item->filename);
            }
        } else {
            fwrite(p_worker->temp_bufptr, 1, handle_bytes, fp);
        }

        write_bytes += handle_bytes;
    }

    fclose(fp);
}


static void aml_async_handle_remove_files(struct aml_async_writer *p_worker)
{
    int buf_num = 0;
    struct listnode *node = NULL, *tmp_node = NULL;

    if (p_worker == NULL) {
        return;
    }

    pthread_mutex_lock(&p_worker->writer_mutex);

    list_for_each_safe(node, tmp_node, &p_worker->buffer_list_head) {
        struct buffer_item *buf_item = (struct buffer_item *)node;
        if (buf_item->is_writing) {
            buf_item->to_remove = 0;
        }
        if (buf_item->to_remove) {
            ALOGI("remove file resource %s", buf_item->filename);
            list_remove(node);
            ring_buffer_release(&buf_item->ring_buffer);
            aml_audio_free(buf_item);
        }
        buf_num++;
    }

    // free temp_buf
    if ((buf_num == 0) && p_worker->temp_bufptr) {
        aml_audio_free(p_worker->temp_bufptr);
        p_worker->temp_bufptr = NULL;
        ALOGI("%s : free temp_buf ok", __func__);
    }

    pthread_mutex_unlock(&p_worker->writer_mutex);
}


static int aml_async_buffer_is_inactive(struct buffer_item *buf_item, uint64_t curr_system_ms)
{
    int duration_ms = 0;
    if (buf_item == NULL) {
        return 0;
    }

    // while file open error, don't remove/create buffer item frequently
    if (buf_item->file_error_ms && (curr_system_ms > buf_item->file_error_ms)) {
        duration_ms = curr_system_ms - buf_item->file_error_ms;
        if (duration_ms > BUFFER_EXPIRE_MS) {
            return 1;
        } else {
            return 0;
        }
    }

    if (curr_system_ms > buf_item->last_update_ms) {
        duration_ms = curr_system_ms - buf_item->last_update_ms;
        if (duration_ms > BUFFER_EXPIRE_MS) {
            return 1;
        }
    }
    return 0;
}


static void *async_write_threadloop(void *data)
{
    struct aml_async_writer *p_worker = (struct aml_async_writer *)data;

    if (p_worker == NULL) {
        return NULL;
    }
    prctl(PR_SET_NAME, (unsigned long)"async_write_thread");

    while (1) {
        uint64_t start_ms = aml_gettime()/1000;
        int delay_ms = 60;    // 60 ms
        int buf_num = 0;
        uint64_t cost_ms = 0;
        struct listnode *node = NULL;

        pthread_mutex_lock(&p_worker->writer_mutex);
        node = p_worker->buffer_list_head.next;
        pthread_mutex_unlock(&p_worker->writer_mutex);

        while (node != &p_worker->buffer_list_head) {
            struct buffer_item *buf_item = (struct buffer_item *)node;
            handle_buffer_write(p_worker, buf_item, start_ms);

            if (aml_async_buffer_is_inactive(buf_item, start_ms)) {
                buf_item->to_remove = 1;
                if (buf_item->is_compress) {
                    compress_deinit(buf_item);
                }
            }
            buf_num++;

            pthread_mutex_lock(&p_worker->writer_mutex);
            node = node->next;
            pthread_mutex_unlock(&p_worker->writer_mutex);
        }
        ALOGV("%s buffer number %d", __func__, buf_num);

        // check if need to remove file resource
        aml_async_handle_remove_files(p_worker);

        if (buf_num == 0) {
            p_worker->is_standby = true;

            ALOGI("%s enter standby", __func__);
            pthread_mutex_lock(&p_worker->wake_mutex);
            pthread_cond_wait(&p_worker->wake_cond, &p_worker->wake_mutex);
            pthread_mutex_unlock(&p_worker->wake_mutex);
            ALOGI("%s leave standby", __func__);
        } else {
            p_worker->is_standby = false;

            cost_ms = aml_gettime()/1000 - start_ms;
            if (cost_ms >= 30) {
                ALOGI("%s use %"PRId64" ms", __func__, cost_ms);
                delay_ms -= (cost_ms - 10);
                if (delay_ms < 10) {
                    delay_ms = 10;
                }
            }
            aml_audio_sleep(delay_ms * 1000);
        }
    }
}



int create_async_write_thread(void)
{
    int ret = 0;
    int pcm_id = 0;
    struct aml_async_writer *p_worker = &worker1;

    if (pthread_mutex_init (&p_worker->writer_mutex, NULL) != 0) {
        ALOGE("%s  pthread_mutex_init fail, errno:%s", __func__, strerror(errno));
        return -1;
    }
    if (pthread_mutex_init (&p_worker->wake_mutex, NULL) != 0) {
        ALOGE("%s  pthread_mutex_init fail, errno:%s", __func__, strerror(errno));
        return -1;
    }
    if (pthread_cond_init(&p_worker->wake_cond, NULL) != 0) {
        ALOGE("%s  pthread_cond_init fail, errno:%s", __func__, strerror(errno));
        return -1;
    }

    list_init(&p_worker->buffer_list_head);
    p_worker->temp_bufptr = NULL;
    p_worker->temp_buflen = 0;
    p_worker->is_standby  = true;

    ret = pthread_create(&p_worker->writer_threadID, NULL, &async_write_threadloop, p_worker);
    if (ret != 0) {
        ALOGE("%s: Create output thread failed, errno:%s", __func__, strerror(errno));
        return ret;
    }

    ALOGI("%s successfully !", __func__);
    return ret;
}


static struct buffer_item* aml_async_get_buffer_item(const char *filename)
{
    int ret = 0;
    int buffer_size = 0;
    struct aml_async_writer *p_worker = &worker1;
    struct listnode *node = NULL, *tmp_node = NULL;

    if ((filename == NULL) || (strlen(filename) >= FILENAME_MAX_LEN) || (strlen(filename) == 0)) {
        ALOGV("%s invalid filename(\'%s\')", __FUNCTION__, filename);
        return NULL;
    }
    if (p_worker->is_standby) {
        pthread_cond_signal(&p_worker->wake_cond);
    }

    pthread_mutex_lock(&p_worker->writer_mutex);
    list_for_each_safe(node, tmp_node, &p_worker->buffer_list_head) {
        struct buffer_item *buf_item = (struct buffer_item *)node;

        if (strcmp(buf_item->filename, filename) == 0) {
            buf_item->is_writing = true;
            pthread_mutex_unlock(&p_worker->writer_mutex);
            return buf_item;
        }
    }
    pthread_mutex_unlock(&p_worker->writer_mutex);

    // insert a buffer item and initialized it
    struct buffer_item *new_buf_item = (struct buffer_item *)aml_audio_calloc(1, sizeof(struct buffer_item));
    if (new_buf_item == NULL) {
        ALOGE("%s  calloc a new buffer item failed !", __FUNCTION__);
        return NULL;
    }
    strcpy(new_buf_item->filename, filename);
    new_buf_item->to_remove = 0;
    buffer_size = BUFFER_DEFAULT_SIZE;

    ret = ring_buffer_alloc(&new_buf_item->ring_buffer, buffer_size);
    if (ret < 0) {
        ALOGE("%s  init audio ringbuffer failed (buffer_size = %d)!", __FUNCTION__, buffer_size);
        aml_audio_free(new_buf_item);
        return NULL;
    }

    list_init(&new_buf_item->list_node);
    pthread_mutex_lock(&p_worker->writer_mutex);
    list_add_tail(&p_worker->buffer_list_head, &new_buf_item->list_node);
    new_buf_item->is_writing = true;
    pthread_mutex_unlock(&p_worker->writer_mutex);

    ALOGI("%s : %s successfully", __func__, new_buf_item->filename);
    return new_buf_item;
}


static void _aml_async_dump_data(const void *buffer, int bytes, struct buffer_item* buf_item)
{
    if ((buffer == NULL) || (bytes == 0) || (buf_item == NULL)) {
        return;
    }
    if (buf_item->file_error_count) {
        int error_count = buf_item->file_error_count;
        if ((error_count < 5) || (error_count % 30 == 0)) {
            // reduce error message
            ALOGE("%s(), %s open failed ! drop %d bytes!", __func__, buf_item->filename, bytes);
        }
        return;
    }

    ring_buffer_t* ringbuf = &buf_item->ring_buffer;
    int avail_bytes = get_buffer_write_space(ringbuf);
    int buffer_size = ringbuf->size;

    if ((avail_bytes < bytes) && (buffer_size < BUFFER_MAX_SIZE)) {
        // Align to BUFFER_DEFAULT_SIZE, try to reduce alloc times.
        int increase_size = 0;
        if ((bytes >= BUFFER_DEFAULT_SIZE) || (buffer_size >= BUFFER_DEFAULT_SIZE*12)) {
            // large data writing, should increase faster !
            increase_size = ALIGN((bytes - avail_bytes), BUFFER_DEFAULT_SIZE*8);
        } else if ((bytes >= BUFFER_DEFAULT_SIZE/2) || (buffer_size >= BUFFER_DEFAULT_SIZE*6)) {
            increase_size = ALIGN((bytes - avail_bytes), BUFFER_DEFAULT_SIZE*4);
        } else {
            increase_size = ALIGN((bytes - avail_bytes), BUFFER_DEFAULT_SIZE);
        }

        int new_buffer_size = buffer_size + increase_size;
        if (new_buffer_size > BUFFER_MAX_SIZE) {
            new_buffer_size = BUFFER_MAX_SIZE;
        }

        if (ring_buffer_realloc(ringbuf, new_buffer_size) == 0) {
            avail_bytes = get_buffer_write_space(ringbuf);
            buffer_size = ringbuf->size;
            ALOGI("%s : %s buffer size change to %d", __func__, buf_item->filename, buffer_size);
        }
    }

#ifdef AML_ASYNC_WRITE_COMPRESS_ENABLE
    if (bytes >= IEC_DETECT_SIZE) {
        int value = get_debug_value(AML_DUMP_AUDIOHAL_ASYNC_WRITE);
        if (value & AML_ASYNC_WRITE_TRY_WITH_COMPRESS) {
            int package_size = 0;
            int payload_size = 0;
            audio_format_t format = AUDIO_FORMAT_INVALID;

            aml_spdif_decoder_get_iec61937_info(buffer, bytes, &package_size, &payload_size, &format);
            if ((format == AUDIO_FORMAT_MAT) || (format == AUDIO_FORMAT_E_AC3)) {
                buf_item->is_compress = true;
                buf_item->compress_level = Z_BEST_SPEED;
            }
        }
    }
#endif

    if (avail_bytes >= bytes) {
        int write_ret = ring_buffer_write(ringbuf, (unsigned char *)buffer, bytes, UNCOVER_WRITE);
        if (write_ret != bytes) {
            ALOGE("%s(), %s buffer write %d bytes fail!", __func__, buf_item->filename, bytes);
        }
    } else {
        ALOGE("%s(), %s buffer not enough space (drop bytes:%d) !", __func__, buf_item->filename, bytes);
    }
}


void aml_async_dump_data(const void *data_ptr, int data_size, const char *file_name)
{
    struct buffer_item* buf_item = aml_async_get_buffer_item(file_name);

    if (buf_item != NULL) {
        _aml_async_dump_data(data_ptr, data_size, buf_item);
        buf_item->is_writing = false;
    }
}



void aml_async_remove_file(const char *file_name)
{
    struct aml_async_writer *p_worker = &worker1;
    if (file_name == NULL) {
        return;
    }

    pthread_mutex_lock(&p_worker->writer_mutex);

    struct listnode *node = NULL, *tmp_node = NULL;
    list_for_each_safe(node, tmp_node, &p_worker->buffer_list_head) {
        struct buffer_item *buf_item = (struct buffer_item *)node;

        if (strcmp(buf_item->filename, file_name) == 0) {
            buf_item->to_remove = 1;
            pthread_mutex_unlock(&p_worker->writer_mutex);
            return;
        }
    }

    pthread_mutex_unlock(&p_worker->writer_mutex);
}


/*
 *  Just dump one channel PCM, to reduce file writing sizes.
*/
static int _aml_async_dump_1ch_16bit_pcm(const void *data_ptr, int data_size, audio_format_t format,
                                         int channel_num, int channel_select, struct buffer_item* buf_item)
{
    int i = 0;
    short conv_buf[256];
    short *p_data16 = (short*) data_ptr;
    int   *p_data32 = (int*) data_ptr;
    float *p_float  = (float*) data_ptr;
    uint8_t *p_data8 = (uint8_t*) data_ptr;

    int frame_size = audio_bytes_per_frame(channel_num, format);
    int buf_frames = AUDIO_ARRAY_SIZE(conv_buf);
    int remain_frames = data_size/frame_size;
    int handle_frames = 0;

    if ((buf_item == NULL) || (data_ptr == NULL) || (data_size == 0) || (channel_num <= 0)) {
        ALOGI("%s : invalid parameters", __func__);
        return -1;
    }

    memset(conv_buf, 0, sizeof(conv_buf));

    // reference : memcpy_by_audio_format
    while (remain_frames > 0) {
        handle_frames = (remain_frames > buf_frames ? buf_frames : remain_frames);

        switch (format) {
            case AUDIO_FORMAT_PCM_FLOAT :
                for (i = 0; i < handle_frames; i++) {
                    conv_buf[i] = clamp16_from_float(p_float[i*channel_num + channel_select]);
                }
                p_float += i*channel_num;
                break;
            case AUDIO_FORMAT_PCM_8_BIT :
                for (i = 0; i < handle_frames; i++) {
                    // (int16_t)(*--src - 0x80) << 8;
                    conv_buf[i] = (int16_t)(p_data8[i*channel_num + channel_select] - 0x80) << 8;
                }
                p_data8 += i*channel_num;
                break;
            case AUDIO_FORMAT_PCM_16_BIT :
                for (i = 0; i < handle_frames; i++) {
                    conv_buf[i] = p_data16[i*channel_num + channel_select];
                }
                p_data16 += i*channel_num;
                break;
            case AUDIO_FORMAT_PCM_24_BIT_PACKED :
                for (i = 0; i < handle_frames; i++) {
                    uint8_t *p_u8 = p_data8 + (i*channel_num + channel_select)*3;
                #if HAVE_BIG_ENDIAN
                    conv_buf[i] = p_u8[1] | (p_u8[0] << 8);
                #else
                    conv_buf[i] = p_u8[1] | (p_u8[2] << 8);
                #endif
                }
                p_data8 += i*channel_num*3;
                break;
            case AUDIO_FORMAT_PCM_32_BIT :
                for (i = 0; i < handle_frames; i++) {
                    conv_buf[i] = (p_data32[i*channel_num + channel_select] >> 16);
                }
                p_data32 += i*channel_num;
                break;
            case AUDIO_FORMAT_PCM_8_24_BIT :
                for (i = 0; i < handle_frames; i++) {
                    // *dst++ = clamp16(*src++ >> 8);
                    conv_buf[i] = clamp16(p_data32[i*channel_num + channel_select] >> 8);
                }
                p_data32 += i*channel_num;
                break;
            default:
                break;
        }

        _aml_async_dump_data(conv_buf, handle_frames*2, buf_item);
        remain_frames -= handle_frames;
    }
    return 0;
}


void aml_async_dump_1ch_16bit_pcm(const void *data_ptr, int data_size, audio_format_t format,
                              int channel_num, int channel_select, const char *file_name)
{
    struct buffer_item* buf_item = aml_async_get_buffer_item(file_name);

    if (buf_item != NULL) {
        _aml_async_dump_1ch_16bit_pcm(data_ptr, data_size, format, channel_num, channel_select, buf_item);
        buf_item->is_writing = false;
    }
}


void aml_async_dump_iec_payload(const void *data_ptr, int data_size, const char *file_name)
{
    int ret = 0;
    int32_t package_size = 0;
    int32_t payload_size = 0;
    uint32_t format = AUDIO_FORMAT_INVALID;
    uint8_t *payload_ptr = (uint8_t *)data_ptr + 8;

    if (data_size <= 8) {
        ALOGE("%s : invalid data_size(%d) !", __func__, data_size);
        return;
    }

    ret = aml_spdif_decoder_get_iec61937_info(data_ptr, data_size, &package_size, &payload_size, &format);
    if (ret != 0) {
        ALOGE("%s : aml_spdif_decoder_get_iec61937_info failed !", __func__);
        return;
    }
    if (payload_size > (data_size - 8)) {
        ALOGE("%s : invalid data, payload_size(%d) > data_size(%d) - 8 !", __func__, payload_size, (data_size - 8));
        return;
    }

    aml_async_dump_data(payload_ptr, payload_size, file_name);
}

