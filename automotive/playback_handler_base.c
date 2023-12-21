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
#define LOG_TAG "audio_hw_automotive_playback_handler"
#define LOG_NDEBUG 0

#include <stdio.h>
#include <sys/types.h>

#include "playback_handler_base.h"

static size_t dummy_playback_write(void *handle __unused, const void* buffer __unused, size_t bytes __unused)
{
    return 0;
}

static int dummy_playback_open(void *handle __unused, uint64_t written_frames __unused)
{
    return 0;
}

static int dummy_playback_close(void *handle __unused)
{
    return 0;
}

static int dummy_get_presentation_position(void *handle __unused, uint64_t *frames __unused, struct timespec *timestamp __unused)
{
    return 0;
}

void init_playback_handle_base(struct playback_handler_base *playback_handle)
{
    playback_handle->open = dummy_playback_open;
    playback_handle->close = dummy_playback_close;
    playback_handle->write = dummy_playback_write;
    playback_handle->get_presentation_position = dummy_get_presentation_position;
}
