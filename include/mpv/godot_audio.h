/* Copyright (C) 2026 the mpv developers
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef MPV_CLIENT_API_GODOT_AUDIO_H_
#define MPV_CLIENT_API_GODOT_AUDIO_H_

#include <stddef.h>
#include <stdint.h>

#include "client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Warning: this API is not stable yet.
 *
 * This API lets embedders receive decoded PCM from a custom `ao=godot`
 * backend. The callback is invoked from mpv's audio output thread and must not
 * call libmpv APIs directly.
 */

enum mpv_godot_audio_format {
    MPV_GODOT_AUDIO_FORMAT_UNKNOWN = 0,
    MPV_GODOT_AUDIO_FORMAT_F32 = 1,
};

typedef struct mpv_godot_audio_frame {
    /**
     * One of mpv_godot_audio_format.
     */
    int format;
    /**
     * Interleaved PCM frame count.
     */
    int samples;
    int sample_rate;
    int channels;
    /**
     * Presentation time estimate in mp_time_ns() units for the end of this
     * buffer.
     */
    int64_t end_time_ns;
    /**
     * Interleaved PCM payload.
     */
    const void *data;
    size_t size;
} mpv_godot_audio_frame;

typedef void (*mpv_godot_audio_callback_fn)(void *user_data,
                                            const mpv_godot_audio_frame *frame);

/**
 * Set or clear the Godot audio callback for the mpv instance associated with
 * the passed handle. This is consumed by the custom `ao=godot` backend.
 *
 * Passing a NULL callback disables delivery.
 */
MPV_EXPORT int mpv_godot_audio_set_callback(mpv_handle *ctx, void *user_data,
                                            mpv_godot_audio_callback_fn cb);

#ifdef __cplusplus
}
#endif

#endif
