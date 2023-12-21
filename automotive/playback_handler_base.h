
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

#ifndef PLAYBACK_HANDLER_BASE_H_
#define PLAYBACK_HANDLER_BASE_H_

#include <sys/types.h>
#include <time.h>

typedef struct playback_handler_base
{
    size_t (*write)(void *handle, const void* buffer, size_t bytes);
    int (*open)(void *handle, uint64_t written_frames);
    int (*close)(void *handle);
    int (*get_presentation_position)(void *handle, uint64_t *frames, struct timespec *timestamp);
} PlaybackHandlerBase;

void init_playback_handle_base(struct playback_handler_base *playback_handle);

#endif