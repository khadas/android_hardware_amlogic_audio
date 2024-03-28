/*
 * GStreamer
 * Copyright (C) 2008 Rov Juvano <rovjuvano@users.sourceforge.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#include <string.h>
#include <stdlib.h>

#define LOG_TAG "audio_hw_hal_scaletmpo"
#include <cutils/log.h>
#include <time.h>

#include "aml_audio_scaletempo.h"
#include "audio_hw_utils.h"
#include "aml_android_utils.h"
#include "aml_malloc_debug.h"
#include "aml_dump_debug.h"

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif


static void dump(const void *buffer, int size, char *file_name)
{
    if (get_debug_value(AML_DUMP_AUDIOHAL_SPEED)) {
        aml_dump_audio_bitstreams(file_name, buffer, size);
    }
}

static unsigned int
best_overlap_offset_float (struct scale_tempo * st)
{
    float *pw, *po, *ppc, *search_start;
    float best_corr = 0;
    unsigned int best_off = 0;
    int i, off;
    unsigned int ret = 0;

    if (1.0 == st->scale) {
        ret = 0;
    } else {
        pw  = st->table_window;
        po  = st->buf_overlap;
        po += st->samples_per_frame;
        ppc = st->buf_pre_corr;
        for (i = st->samples_per_frame; i < st->samples_overlap; i++) {
            *ppc++ = *pw++ * *po++;
    }

    search_start = (float *)st->buf_queue + st->samples_per_frame;
    for (off = 0; off < st->frames_search; off++) {
        float corr = 0;
        float *ps = search_start;
        ppc = st->buf_pre_corr;
        for (i = st->samples_per_frame; i < st->samples_overlap; i++) {
            corr += *ppc++ * *ps++;
        }
        if (corr > best_corr) {
            best_corr = corr;
            best_off  = off;
        }
        search_start += st->samples_per_frame;
    }
    ret = best_off * st->bytes_per_frame;
    }
    return ret;
}

/* buffer padding for loop optimization: sizeof(int) * (loop_size - 1) */
#define UNROLL_PADDING (4*3)
static unsigned int
best_overlap_offset_s16 (struct scale_tempo * st)
{
    int *pw, *ppc;
    short *po, *search_start;
    long long best_corr = 0;
    unsigned int best_off = 0;
    unsigned int off;
    long i;
    unsigned int ret = 0;

    if (1.0 == st->scale) {
        ret = 0;
    } else {
        pw = st->table_window;
        po = st->buf_overlap;
        po += st->samples_per_frame;
        ppc = st->buf_pre_corr;
        for (i = st->samples_per_frame; i < st->samples_overlap; i++) {
            *ppc++ = (*pw++ * *po++) >> 15;
        }

        search_start = (short *) st->buf_queue + st->samples_per_frame;
        for (off = 0; off < st->frames_search; off++) {
            long long corr = 0;
            short *ps = search_start;
            ppc = st->buf_pre_corr;
            ppc += st->samples_overlap - st->samples_per_frame;
            ps += st->samples_overlap - st->samples_per_frame;
            i = -((long) st->samples_overlap - (long) st->samples_per_frame);
            do {
                corr += (long long)ppc[i + 0] * ps[i + 0];
                corr += (long long)ppc[i + 1] * ps[i + 1];
                corr += (long long)ppc[i + 2] * ps[i + 2];
                corr += (long long)ppc[i + 3] * ps[i + 3];
                i += 4;
            } while (i < 0);
            if (corr > best_corr) {
                best_corr = corr;
                best_off = off;
            }
            search_start += st->samples_per_frame;
        }
        ret = best_off * st->bytes_per_frame;
    }

    return ret;
}

static void
output_overlap_float (struct scale_tempo * st,
                      void *         buf_out,
                      unsigned int            bytes_off)
{
    float *pout = buf_out;
    float *pb   = st->table_blend;
    float *po   = st->buf_overlap;
    float *pin  = (float *)(st->buf_queue + bytes_off);
    int i;
    if (1.0 == st->scale) {
        memcpy (pout, st->buf_queue, st->bytes_overlap);
    } else {
        for (i = 0; i < st->samples_overlap; i++) {
            *pout++ = *po - *pb++ * ( *po - *pin++ ); po++;
        }
    }
}

static void
output_overlap_s16 (struct scale_tempo * st, void * buf_out, unsigned int bytes_off)
{
    short *pout = buf_out;
    int *pb = st->table_blend;
    short *po = st->buf_overlap;
    short *pin = (short *) (st->buf_queue + bytes_off);
    int i;

    if (1.0 == st->scale) {
        memcpy (pout, st->buf_queue, st->bytes_overlap);
    } else {
        for (i = 0; i < st->samples_overlap; i++) {
            *pout++ = *po - ((*pb++ * (*po - *pin++)) >> 16);
            po++;
        }
    }
}

static unsigned int
fill_queue (struct scale_tempo * st, void * buf_in, int insize, unsigned int offset)
{
    unsigned int bytes_in = insize - offset;
    unsigned int offset_unchanged = offset;
    char* data = (char*) buf_in;

    if (st->bytes_to_slide > 0) {
        if (st->bytes_to_slide < st->bytes_queued) {
            unsigned int bytes_in_move = st->bytes_queued - st->bytes_to_slide;
            memmove (st->buf_queue, st->buf_queue + st->bytes_to_slide,
                    bytes_in_move);
            st->bytes_to_slide = 0;
            st->bytes_queued = bytes_in_move;
    } else {
        unsigned int bytes_in_skip;
        st->bytes_to_slide -= st->bytes_queued;
        bytes_in_skip = min (st->bytes_to_slide, bytes_in);
        st->bytes_queued = 0;
        st->bytes_to_slide -= bytes_in_skip;
        offset += bytes_in_skip;
        bytes_in -= bytes_in_skip;
        }
    }

    if (bytes_in > 0) {
        unsigned int bytes_in_copy =
            min (st->bytes_queue_max - st->bytes_queued, bytes_in);
        memcpy (st->buf_queue + st->bytes_queued, data + offset, bytes_in_copy);
        st->bytes_queued += bytes_in_copy;
        offset += bytes_in_copy;
}

    return offset - offset_unchanged;
}

static void
reinit_buffers (struct scale_tempo * st)
{
    int i, j;
    unsigned int frames_overlap;
    unsigned int new_size;

    unsigned int frames_stride = st->ms_stride * st->sample_rate / 1000.0;
    st->bytes_stride = frames_stride * st->bytes_per_frame;

    /* overlap */
    frames_overlap = frames_stride * st->percent_overlap;
    if (frames_overlap < 1) {     /* if no overlap */
        st->bytes_overlap = 0;
        st->bytes_standing = st->bytes_stride;
        st->samples_standing = st->bytes_standing / st->bytes_per_sample;
        st->output_overlap = NULL;
    } else {
        unsigned int prev_overlap = st->bytes_overlap;
        st->bytes_overlap = frames_overlap * st->bytes_per_frame;
        st->samples_overlap = frames_overlap * st->samples_per_frame;
        st->bytes_standing = st->bytes_stride - st->bytes_overlap;
        st->samples_standing = st->bytes_standing / st->bytes_per_sample;
        st->buf_overlap = aml_audio_realloc(st->buf_overlap, st->bytes_overlap);
        if (!st->buf_overlap) {
            ALOGE("%s %d: scale_tempo %p, OOM", __func__, __LINE__, st);
        return;
    }
    /* S16 uses int blend table, floats/doubles use their respective type */
    st->table_blend =
        aml_audio_realloc(st->table_blend,
        st->samples_overlap * (st->format ==
            FORMAT_S16 ? 4 : st->bytes_per_sample));
    if (!st->table_blend) {
        ALOGE("%s %d: scale_tempo %p, OOM", __func__, __LINE__, st);
        return;
    }
    if (st->bytes_overlap > prev_overlap) {
        memset ((unsigned char *) st->buf_overlap + prev_overlap, 0,
          st->bytes_overlap - prev_overlap);
    }
    if (st->format == FORMAT_S16) {
        int *pb = st->table_blend;
        long long blend = 0;
        for (i = 0; i < frames_overlap; i++) {
            int v = blend / frames_overlap;
            for (j = 0; j < st->samples_per_frame; j++) {
                *pb++ = v;
            }
            blend += 65535;         /* 2^16 */
        }
        st->output_overlap = output_overlap_s16;
    } else {
        float *pb = st->table_blend;
        float t = (float)frames_overlap;
        for (i=0; i<frames_overlap; i++) {
            float v = i / t;
            for (j=0; j < st->samples_per_frame; j++) {
                *pb++ = v;
            }
        }
        st->output_overlap = output_overlap_float;
        }
    }

    /* best overlap */
    st->frames_search =
        (frames_overlap <= 1) ? 0 : st->ms_search * st->sample_rate / 1000.0;
    if (st->frames_search < 1) {  /* if no search */
        st->best_overlap_offset = NULL;
    } else {
        /* S16 uses int buffer, floats/doubles use their respective type */
        unsigned int bytes_pre_corr =
                (st->samples_overlap - st->samples_per_frame) * (st->format ==
                FORMAT_S16 ? 4 : st->bytes_per_sample);
        st->buf_pre_corr =
                aml_audio_realloc(st->buf_pre_corr, bytes_pre_corr + UNROLL_PADDING);
        if (!st->buf_pre_corr) {
            ALOGE("%s %d: scale_tempo %p, OOM", __func__, __LINE__, st);
            return;
        }

        st->table_window = aml_audio_realloc(st->table_window, bytes_pre_corr);

        if (!st->table_window) {
            ALOGE("%s %d: scale_tempo %p, OOM", __func__, __LINE__, st);
            return;
        }

        if (st->format == FORMAT_S16) {
            long long t = frames_overlap;
            int n = 8589934588LL / (t * t);        /* 4 * (2^31 - 1) / t^2 */
            int *pw;

            memset ((unsigned char *) st->buf_pre_corr + bytes_pre_corr, 0, UNROLL_PADDING);
            pw = st->table_window;
            for (i = 1; i < frames_overlap; i++) {
                int v = (i * (t - i) * n) >> 15;
                for (j = 0; j < st->samples_per_frame; j++) {
                    *pw++ = v;
                }
            }
            st->best_overlap_offset = best_overlap_offset_s16;
        } else {
            float *pw = st->table_window;
            for (i=1; i<frames_overlap; i++) {
                float v = i * (frames_overlap - i);
                    for (j=0; j < st->samples_per_frame; j++) {
                    *pw++ = v;
                }
            }
            st->best_overlap_offset = best_overlap_offset_float;
        }
    }

    new_size =
        (st->frames_search + frames_stride +
        frames_overlap) * st->bytes_per_frame;
    if (st->bytes_queued > new_size) {
        if (st->bytes_to_slide > st->bytes_queued) {
            st->bytes_to_slide -= st->bytes_queued;
            st->bytes_queued = 0;
        } else {
            unsigned int new_queued = min (st->bytes_queued - st->bytes_to_slide, new_size);
            memmove (st->buf_queue,
                st->buf_queue + st->bytes_queued - new_queued, new_queued);
            st->bytes_to_slide = 0;
            st->bytes_queued = new_queued;
        }
    }

    st->bytes_queue_max = new_size;
    st->buf_queue = aml_audio_realloc(st->buf_queue, st->bytes_queue_max);
    if (!st->buf_queue) {
        ALOGE("%s %d: scale_tempo %p, OOM", __func__, __LINE__, st);
        return;
    }

    st->bytes_stride_scaled = st->bytes_stride * st->scale;
    st->frames_stride_scaled = st->bytes_stride_scaled / st->bytes_per_frame;

    st->buf_output = aml_audio_malloc(st->bytes_stride * 8);
    if (!st->buf_output) {
        ALOGE("%s %d: scale_tempo %p, OOM", __func__, __LINE__, st);
        return;
    }
    st->bytes_to_output = 0;
    st->output_max_bytes = st->bytes_stride * 8;

    st->buf_input = (char*) aml_audio_malloc(st->bytes_stride * 8);
    if (!st->buf_input) {
        ALOGE("%s %d: scale_tempo %p, OOM", __func__, __LINE__, st);
        return;
    }

    st->buf_input_size = st->bytes_stride * 8;

    ALOGI
        ("%.3f scale, %.3f stride_in, %i stride_out, %i standing, %i overlap, %i search, %i queue, format %d",
        st->scale, st->frames_stride_scaled,
        (int) (st->bytes_stride / st->bytes_per_frame),
        (int) (st->bytes_standing / st->bytes_per_frame),
        (int) (st->bytes_overlap / st->bytes_per_frame), st->frames_search,
        (int) (st->bytes_queue_max / st->bytes_per_frame),
        st->format);
    st->reinit_buffers = false;
}

/* GstBaseTransform method implementations */
static int hal_scaletempo_transform (struct scale_tempo * st,
    void * inbuf, int insize, void * outbuf)
{
    char *pout;
    unsigned int offset_in = 0, bytes_out = 0;
    unsigned int offset_out = 0;
    unsigned long long offset_out_pts = 0;

    pout = outbuf;
    offset_in = fill_queue (st, inbuf, insize, 0);
    while (st->bytes_queued >= st->bytes_queue_max) {
        unsigned int bytes_off = 0;
        double frames_to_slide;
        unsigned int frames_to_stride_whole;

        /* output stride */
        if (st->output_overlap) {
            if (st->best_overlap_offset) {
                bytes_off = st->best_overlap_offset (st);
            }
            st->output_overlap (st, pout, bytes_off);
        }
        memcpy (pout + st->bytes_overlap,
            st->buf_queue + bytes_off + st->bytes_overlap, st->bytes_standing);
        pout += st->bytes_stride;
        bytes_out += st->bytes_stride;

        /* input stride */
        memcpy (st->buf_overlap,
            st->buf_queue + bytes_off + st->bytes_stride, st->bytes_overlap);
        frames_to_slide = st->frames_stride_scaled + st->frames_stride_error;
        frames_to_stride_whole = (int) frames_to_slide;
        st->bytes_to_slide = frames_to_stride_whole * st->bytes_per_frame;
        st->frames_stride_error = frames_to_slide - frames_to_stride_whole;

        offset_in += fill_queue (st, inbuf, insize, offset_in);
    }

    st->bytes_to_output += bytes_out;
    if (st->bytes_to_output > st->output_max_bytes)
    {
        st->bytes_to_output = st->output_max_bytes;
        ALOGE("%s, error!! this should not occur!!\n", __FUNCTION__);
    }
    //ALOGI ("offset_out %d bytes_out %d queued %d slide %d max %d, input size:%d",
    //       offset_out, bytes_out, st->bytes_queued,
    //       st->bytes_to_slide, st->bytes_queue_max, insize);
    //aml_dump_audio_bitstreams("/data/vendor/audiohal/hal_st_transform.raw", outbuf, bytes_out);
    return 0;
}

static bool hal_scaletempo_transform_size (struct scale_tempo * scaletempo,
    int size, int * othersize)
{
    int bytes_to_out;
    if (scaletempo->reinit_buffers)
        reinit_buffers (scaletempo);

    bytes_to_out = size + scaletempo->bytes_queued - scaletempo->bytes_to_slide;
    if (bytes_to_out < (int) scaletempo->bytes_queue_max) {
        //ALOGI ("size %d bytes_to_out %d queued %d slide %d max %d",
        //    size, bytes_to_out, scaletempo->bytes_queued,
        //    scaletempo->bytes_to_slide, scaletempo->bytes_queue_max);
        *othersize = 0;
    } else {
        /* while (total_buffered - stride_length * n >= queue_max) n++ */
        *othersize = scaletempo->bytes_stride * ((unsigned int) (
            (bytes_to_out - scaletempo->bytes_queue_max +
                /* rounding protection */ scaletempo->bytes_per_frame)
            / scaletempo->bytes_stride_scaled) + 1);
    }

    return true;
}

void hal_scaletempo_update_rate (struct scale_tempo * scaletempo, double rate)
{
    double temp;
    pthread_mutex_lock(&scaletempo->mutex);
    if (rate > 1.0)
        temp = rate - 1.0;
    else
        temp = 1.0 - rate;
    if (scaletempo->scale != rate) {
        if (temp < 1e-10) {
            scaletempo->scale = 1.0;
            scaletempo->bytes_stride_scaled =
                scaletempo->bytes_stride;
            scaletempo->frames_stride_scaled =
                scaletempo->bytes_stride_scaled / scaletempo->bytes_per_frame;
            scaletempo->bytes_to_slide = 0;
        } else {
            scaletempo->scale = rate;
            scaletempo->bytes_stride_scaled =
                scaletempo->bytes_stride * scaletempo->scale;
            scaletempo->frames_stride_scaled =
                scaletempo->bytes_stride_scaled / scaletempo->bytes_per_frame;
            ALOGI ("%s %.3f scale, %.3f stride_in, %i stride_out", __FUNCTION__,
                scaletempo->scale, scaletempo->frames_stride_scaled,
                (int) (scaletempo->bytes_stride / scaletempo->bytes_per_frame));

            scaletempo->bytes_to_slide = 0;
            scaletempo->input_sample_total = 0;
            scaletempo->output_sample_total = 0;
            scaletempo->cost_time = 0;
        }
    }

    pthread_mutex_unlock(&scaletempo->mutex);
}

static bool hal_scaletempo_set_info (struct scale_tempo * scaletempo, int nch, int rate, int sample_size, int format)
{
    int bps = sample_size;
    if (rate != scaletempo->sample_rate
      || nch != scaletempo->samples_per_frame
      || bps != scaletempo->bytes_per_sample || format != scaletempo->format) {
        scaletempo->sample_rate = rate;
        scaletempo->samples_per_frame = nch;
        scaletempo->bytes_per_sample = bps;
        scaletempo->bytes_per_frame = nch * bps;
        scaletempo->format = format;
        scaletempo->channel = nch;
        scaletempo->reinit_buffers = true;
    }

    return true;
}

void hal_scaletempo_force_init(struct scale_tempo * scaletempo)
{
    struct scale_tempo * st = scaletempo;

    ALOGI("%s %d: scale_tempo %p", __func__, __LINE__, scaletempo);
    if (!st) {
        return;
    }

    pthread_mutex_lock(&st->mutex);

    /* defaults */
    st->ms_stride = 16;
    st->percent_overlap = 0.2;
    st->ms_search = 6;

    /* uninitialized */
    st->scale = 1.0;
    st->frames_stride_error = 0;
    st->bytes_stride = 0;
    st->bytes_queued = 0;
    st->bytes_to_slide = 0;
    st->segment_start = 0;

    st->reinit_buffers = true;
    st->sample_rate = 48000;
    st->samples_per_frame = 8;
    st->bytes_per_sample = 4;
    st->bytes_per_frame = 4 * 8;
    st->format = FORMAT_F32;
    st->channel = 8;

    pthread_mutex_unlock(&st->mutex);
}

bool hal_scaletempo_release (struct scale_tempo * scaletempo)
{
    ALOGI("%s %d: scale_tempo %p", __func__, __LINE__, scaletempo);
    pthread_mutex_lock(&scaletempo->mutex);
    if (scaletempo->buf_queue != NULL) {
        aml_audio_free(scaletempo->buf_queue);
        scaletempo->buf_queue = NULL;
    }
    if (scaletempo->buf_overlap != NULL) {
        aml_audio_free(scaletempo->buf_overlap);
        scaletempo->buf_overlap = NULL;
    }
    if (scaletempo->table_blend != NULL) {
        aml_audio_free(scaletempo->table_blend);
        scaletempo->table_blend = NULL;
    }
    if (scaletempo->buf_pre_corr != NULL) {
        aml_audio_free(scaletempo->buf_pre_corr);
        scaletempo->buf_pre_corr = NULL;
    }
    if (scaletempo->table_window != NULL) {
        aml_audio_free(scaletempo->table_window);
        scaletempo->table_window = NULL;
    }
    if (scaletempo->buf_output != NULL) {
        aml_audio_free(scaletempo->buf_output);
        scaletempo->buf_output = NULL;
    }

    if (scaletempo->buf_input != NULL) {
        aml_audio_free(scaletempo->buf_input);
        scaletempo->buf_input = NULL;
    }

    pthread_mutex_unlock(&scaletempo->mutex);
    scaletempo->reinit_buffers = true;
    pthread_mutex_destroy(&scaletempo->mutex);

    aml_audio_free(scaletempo);

    return true;
}

int hal_scaletempo_init (struct scale_tempo ** scaletempo)
{
    struct scale_tempo * st;

    if (scaletempo == NULL) {
        return -1;
    }

    st = calloc(1, sizeof(struct scale_tempo));
    ALOGI("%s %d: scale_tempo %p", __func__, __LINE__, st);
    if (!st) {
        ALOGE("%s %d: scale_tempo %p init fail, return", __func__, __LINE__, st);
        return -1;
    }
    /* defaults */
    st->ms_stride = 16;
    st->percent_overlap = 0.2;
    st->ms_search = 6;

    /* uninitialized */
    st->scale = 1.0;
    st->frames_stride_error = 0;
    st->bytes_stride = 0;
    st->bytes_queued = 0;
    st->bytes_to_slide = 0;
    st->segment_start = 0;

    st->reinit_buffers = true;
    st->sample_rate = 48000;
    st->samples_per_frame = 8;
    st->bytes_per_sample = 4;
    st->bytes_per_frame = 4 * 8;
    st->format = FORMAT_F32;
    st->channel = 8;

    pthread_mutex_init(&st->mutex, NULL);

    *scaletempo = st;

    return 0;
}

static int hal_scaletempo_get_process_samples(struct scale_tempo* scaletempo, int input_samples, int *output_samples)
{
    int input_size = input_samples * scaletempo->bytes_per_frame;
    int out_size = 0;

    hal_scaletempo_transform_size(scaletempo, input_size, &out_size);

    out_size += scaletempo->bytes_to_output;
    if (out_size < *output_samples * scaletempo->bytes_per_frame)
        *output_samples = 0;

    if (out_size > scaletempo->output_max_bytes)
        return 0;
    else
        return input_samples;
}

static long long hal_scaletempo_timediffns(struct timespec t1, struct timespec t2)
{
    long long nsec;

    nsec = (long long)(t1.tv_sec - t2.tv_sec) * 1000 * 1000000;
    nsec += (long long)(t1.tv_nsec - t2.tv_nsec);

    return nsec;
}


int hal_scaletempo_process(struct scale_tempo* scaletempo, aml_scaletempo_info_t * info)
{
    void* input_buffer = NULL;
    int input_size = 0;
    int i = 0, j = 0;
    int output_size = 0;
    float *sample;
    float* p_buffer;
    scale_tempo_buffer *p_in_buffer, *p_out_buffer;
    struct timespec start_ts, end_ts;
    static int count = 0;
    int n_samples_to_process = 0;
    int input_samples = 0;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start_ts);
    pthread_mutex_lock(&scaletempo->mutex);

    p_in_buffer = info->inputbuffer;
    p_out_buffer = info->outputbuffer;
    input_samples = info->input_samples;

    if (fabs(scaletempo->scale - 1.0) < 1e-10) {
        n_samples_to_process = min(info->input_samples, info->output_samples);
        if (n_samples_to_process > 0) {
            for (i = 0; j < info->ch; j++)
            {
                memcpy(p_out_buffer->ppdata[j], p_in_buffer->ppdata[j], n_samples_to_process * info->sample_size);

            }
            info->input_samples = n_samples_to_process;
            info->output_samples = n_samples_to_process;
            ALOGV("%s process samples =%d", __func__, n_samples_to_process);
        }
    } else {
        //ALOGI("%s %d: scale_tempo %p, input sample:%d, output sample:%d, ch %d, sr %d, samplesize:%d", __func__, __LINE__,
        //        scaletempo, info->input_samples, info->output_samples, info->ch, info->sr, info->sample_size);
        hal_scaletempo_set_info(scaletempo, info->ch, info->sr, info->sample_size, FORMAT_F32);

        info->input_samples = hal_scaletempo_get_process_samples(scaletempo, info->input_samples, &info->output_samples);


        input_size = info->input_samples * scaletempo->bytes_per_frame;
        output_size = info->output_samples * scaletempo->bytes_per_frame;
        if (info->input_samples > 0) {
            if (input_size > scaletempo->buf_input_size) {
                free(scaletempo->buf_input);
                scaletempo->buf_input = (char*) malloc(input_size);
                if (!scaletempo->buf_input) {
                    ALOGE("%s %d: scale_tempo %p buf_input fail, return", __func__, __LINE__, scaletempo);
                    pthread_mutex_unlock(&scaletempo->mutex);
                    return -1;
                }
                scaletempo->buf_input_size = input_size;
            }

            input_buffer = scaletempo->buf_input;
            if (!input_buffer) {
                ALOGE("%s:error, malloc failed", __FUNCTION__);
                pthread_mutex_unlock(&scaletempo->mutex);
                return 0;
            }

            dump(p_in_buffer->ppdata[0], info->input_samples * sizeof(float), "/data/vendor/ms12/tempo_in.raw");

            sample = (float*)input_buffer;
            for (i = 0; i < info->input_samples; i++) {
                for (j = 0; j < scaletempo->channel; j++) {
                    p_buffer = (float*)p_in_buffer->ppdata[j];
                    sample[i * scaletempo->channel +j] = p_buffer[i];
                }
            }
            hal_scaletempo_transform(scaletempo, input_buffer, input_size, scaletempo->buf_output + scaletempo->bytes_to_output);
        }

        if (scaletempo->bytes_to_output >= output_size) {
            sample = (float *)scaletempo->buf_output;
            if (info->output_samples) {
                for (i = 0; i < info->output_samples; i++) {
                    for (j = 0; j < scaletempo->channel; j++) {
                        p_buffer = (float*)p_out_buffer->ppdata[j];
                        p_buffer[i] = sample[i * scaletempo->channel + j];
                    }
                }

                if (scaletempo->bytes_to_output - output_size > 0) {
                    memmove(scaletempo->buf_output, scaletempo->buf_output + output_size, scaletempo->bytes_to_output - output_size);
                }
                scaletempo->bytes_to_output -= output_size;
            }
        } else {
            ALOGE("%s, error!! this should not occur!! bytes_to_output=%d output_size=%d\n", __FUNCTION__, scaletempo->bytes_to_output, output_size);
            info->input_samples = input_samples;
            info->output_samples = 0;
            scaletempo->frames_stride_error = 0;
            scaletempo->bytes_queued = 0;
            scaletempo->bytes_to_slide = 0;
            scaletempo->segment_start = 0;
        }
    }
    if (info->output_samples > 0) {
        dump(p_out_buffer->ppdata[0], info->output_samples * sizeof(float), "/data/vendor/ms12/tempo_out.raw");
    }

    scaletempo->input_sample_total += info->input_samples;
    scaletempo->output_sample_total += info->output_samples;
    clock_gettime(CLOCK_MONOTONIC_RAW, &end_ts);

    scaletempo->cost_time += hal_scaletempo_timediffns(end_ts, start_ts);
    if (count ++ % 10000 == 0) {
        ALOGI("%s %d: scale_tempo %p, input sample:%d, output sample:%d, rate:%f, total input: %u, total output:%u, cost time:%llu ms",
            __func__, __LINE__,
            scaletempo, info->input_samples, info->output_samples, scaletempo->scale, scaletempo->input_sample_total,
            scaletempo->output_sample_total, scaletempo->cost_time/1000000);
    }
    pthread_mutex_unlock(&scaletempo->mutex);

    return 0;
}
