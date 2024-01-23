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

#ifndef _HAL_SCALETEMPO_H_
#define _HAL_SCALETEMPO_H_

#include <stdbool.h>
#include <pthread.h>

struct scale_tempo
{
    double scale;
    void *queued_buf;

    /* parameters */
    unsigned int ms_stride;
    double percent_overlap;
    unsigned int ms_search;

    /* caps */
    int format;
    unsigned int samples_per_frame;      /* AKA number of channels */
    unsigned int bytes_per_sample;
    unsigned int bytes_per_frame;
    unsigned int sample_rate;
    int channel;

    /* stride */
    double frames_stride_scaled;
    double frames_stride_error;
    unsigned int bytes_stride;
    double bytes_stride_scaled;
    unsigned int bytes_queue_max;
    unsigned int bytes_queued;
    unsigned int bytes_to_slide;
    char *buf_queue;

    /* overlap */
    unsigned int samples_overlap;
    unsigned int samples_standing;
    unsigned int bytes_overlap;
    unsigned int bytes_standing;
    void * buf_overlap;
    void * table_blend;
    void (*output_overlap) (struct scale_tempo * scaletempo, void * out_buf, unsigned int bytes_off);

    /* best overlap */
    unsigned int frames_search;
    void * buf_pre_corr;
    void * table_window;
    unsigned int (*best_overlap_offset) (struct scale_tempo * scaletempo);

    long long      segment_start;
    /* threads */
    bool reinit_buffers;

    /* output */
    /* we always output to buf_output firstly*/
    char * buf_output;
    int bytes_to_output;
    int output_max_bytes;

    /*input*/
    char * buf_input;
    int buf_input_size;

    pthread_mutex_t mutex;

    unsigned int input_sample_total;
    unsigned int output_sample_total;
    long long cost_time;
};

enum {
    FORMAT_S16 = 0,
    FORMAT_F32 = 1,
};

typedef struct scale_tempo_buffer_s
{
    /** Channel count */
    unsigned    nchannel;

    /** Distance, in units of the chosen data type, between consecutive samples
     *  of the same channel. For DLB_BUFFER_OCTET_PACKED, this is in units of
     *  unsigned char. For interleaved audio data, set this equal to nchannel.
     *  For separate channel buffers, set this to 1.
     */
    ptrdiff_t   nstride;

    /** One of the DLB_BUFFER_* codes */
    int         data_type;

    /** Array of pointers to data, one for each channel. */
    void** ppdata;
} scale_tempo_buffer;

typedef struct aml_scaletempo_info {
    int sr ;   /** the decoded data samplerate*/
    int ch ;   /** the decoded data channels*/
    int sample_size; /**the decoded sample bit width*/
    int data_type;
    scale_tempo_buffer * inputbuffer;
    int input_samples;
    scale_tempo_buffer * outputbuffer;
    int output_samples;
} aml_scaletempo_info_t;

int hal_scaletempo_init (struct scale_tempo ** scaletempo);
bool hal_scaletempo_release (struct scale_tempo * scaletempo);
void hal_scaletempo_update_rate (struct scale_tempo * scaletempo, double rate);
void hal_scaletempo_force_init(struct scale_tempo * scaletempo);
int hal_scaletempo_process(struct scale_tempo* scaletempo, aml_scaletempo_info_t * info);

#endif
