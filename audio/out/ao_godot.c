/*
 * Godot callback audio output driver
 *
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>

#include "mpv_talloc.h"

#include "audio/format.h"
#include "common/common.h"
#include "common/global.h"
#include "common/msg.h"
#include "mpv/godot_audio.h"
#include "options/m_option.h"
#include "osdep/threads.h"
#include "osdep/timer.h"
#include "player/client.h"
#include "ao.h"
#include "internal.h"

struct priv {
    mp_thread thread;
    mp_mutex lock;
    mp_cond wakeup;
    bool thread_created;
    bool shutting_down;
    bool started;
    bool paused;
    int chunk_samples;
    int buffer_samples;
    uint8_t *buffer;
};

static MP_THREAD_VOID audio_thread(void *arg)
{
    struct ao *ao = arg;
    struct priv *p = ao->priv;

    mp_thread_set_name("ao/godot");

    while (true) {
        mp_mutex_lock(&p->lock);
        while (!p->shutting_down && (!p->started || p->paused))
            mp_cond_wait(&p->wakeup, &p->lock);
        bool shutting_down = p->shutting_down;
        int chunk_samples = p->chunk_samples;
        mp_mutex_unlock(&p->lock);

        if (shutting_down)
            break;

        int64_t chunk_duration = MP_TIME_S_TO_NS(chunk_samples) / ao->samplerate;
        int64_t end_time_ns = mp_time_ns() + 2 * chunk_duration;
        void *planes[] = {p->buffer};
        int samples = ao_read_data(ao, planes, chunk_samples, end_time_ns,
                                   NULL, true, true);

        mpv_godot_audio_callback_fn callback = NULL;
        void *user_data = NULL;
        mp_godot_audio_get_callback(ao->global, &user_data, &callback);
        if (callback && samples > 0) {
            mpv_godot_audio_frame frame = {
                .format = MPV_GODOT_AUDIO_FORMAT_F32,
                .samples = samples,
                .sample_rate = ao->samplerate,
                .channels = ao->channels.num,
                .end_time_ns = end_time_ns,
                .data = p->buffer,
                .size = (size_t)samples * ao->sstride,
            };
            callback(user_data, &frame);
        }

        mp_sleep_ns(chunk_duration);
    }

    MP_THREAD_RETURN();
}

static int init(struct ao *ao)
{
    struct priv *p = ao->priv;
    mpv_godot_audio_callback_fn callback = NULL;
    void *user_data = NULL;
    mp_godot_audio_get_callback(ao->global, &user_data, &callback);
    if (!callback) {
        MP_FATAL(ao, "godot audio callback not configured\n");
        return -1;
    }

    struct mp_chmap_sel sel = {.tmp = ao};
    mp_chmap_sel_add_any(&sel);
    if (!ao_chmap_sel_adjust(ao, &sel, &ao->channels))
        return -1;

    ao->format = AF_FORMAT_FLOAT;

    p->chunk_samples = MPMAX(256, ao->samplerate / 100);
    p->chunk_samples = MPMIN(p->chunk_samples, 2048);
    p->buffer_samples = p->chunk_samples * 4;
    ao->device_buffer = p->buffer_samples;

    p->buffer = talloc_size(p, (size_t)p->chunk_samples * ao->channels.num *
                                   af_fmt_to_bytes(ao->format));
    if (!p->buffer)
        return -1;

    mp_mutex_init(&p->lock);
    mp_cond_init(&p->wakeup);
    if (mp_thread_create(&p->thread, audio_thread, ao)) {
        mp_cond_destroy(&p->wakeup);
        mp_mutex_destroy(&p->lock);
        return -1;
    }
    p->thread_created = true;
    return 0;
}

static void uninit(struct ao *ao)
{
    struct priv *p = ao->priv;
    if (!p)
        return;

    if (p->thread_created) {
        mp_mutex_lock(&p->lock);
        p->shutting_down = true;
        mp_cond_broadcast(&p->wakeup);
        mp_mutex_unlock(&p->lock);
        mp_thread_join(p->thread);
    }

    mp_cond_destroy(&p->wakeup);
    mp_mutex_destroy(&p->lock);
}

static void reset(struct ao *ao)
{
    struct priv *p = ao->priv;
    mp_mutex_lock(&p->lock);
    p->started = false;
    p->paused = false;
    mp_cond_broadcast(&p->wakeup);
    mp_mutex_unlock(&p->lock);
}

static bool set_pause(struct ao *ao, bool paused)
{
    struct priv *p = ao->priv;
    mp_mutex_lock(&p->lock);
    p->paused = paused;
    mp_cond_broadcast(&p->wakeup);
    mp_mutex_unlock(&p->lock);
    return true;
}

static void start(struct ao *ao)
{
    struct priv *p = ao->priv;
    mp_mutex_lock(&p->lock);
    p->started = true;
    p->paused = false;
    mp_cond_broadcast(&p->wakeup);
    mp_mutex_unlock(&p->lock);
}

const struct ao_driver audio_out_godot = {
    .description = "Godot audio callback",
    .name = "godot",
    .init = init,
    .uninit = uninit,
    .reset = reset,
    .set_pause = set_pause,
    .start = start,
    .priv_size = sizeof(struct priv),
    .options = (const struct m_option[]) {
        {0}
    },
    .options_prefix = "godot",
};
