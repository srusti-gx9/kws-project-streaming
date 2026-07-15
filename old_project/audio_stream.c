/*
 * audio_stream.c — orchestration of the producer/consumer audio pipeline.
 *
 * Owns:
 *   - one RingBuffer (lock-free SPSC)
 *   - one AudioCapture (ALSA)
 *   - the capture thread  (producer): ALSA read -> ring_buffer_push
 *   - the consumer thread (consumer): ring_buffer_pop -> callback
 *
 * The capture thread NEVER blocks on the consumer: if the ring is full the
 * chunk is dropped (counted) and capture keeps draining the ALSA kernel
 * buffer so we don't induce overruns.
 */
#define _POSIX_C_SOURCE 200809L

#include "audio_stream.h"
#include "audio_capture.h"
#include "ring_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

struct AudioStream {
    AudioStreamConfig cfg;

    RingBuffer    ring;
    AudioCapture *capture;

    AudioCallback callback;
    void         *user_data;

    pthread_t     capture_thread;
    pthread_t     consumer_thread;
    bool          threads_started;

    _Atomic bool  running;

    /* Per-thread scratch chunks — allocated once, reused every iteration. */
    int16_t      *capture_chunk;   /* producer reads ALSA into this  */
    int16_t      *consumer_chunk;  /* consumer pops into this         */

    /* Poll interval for the consumer when the ring is empty (nanoseconds). */
    long          idle_sleep_ns;
};

/* ----------------------------- threads ----------------------------- */

static void *capture_thread_fn(void *arg) {
    AudioStream *s = (AudioStream *)arg;
    const unsigned int frames = s->cfg.chunk_samples; /* mono: 1 sample/frame */

    while (atomic_load_explicit(&s->running, memory_order_acquire)) {
        long got = audio_capture_read(s->capture, s->capture_chunk, frames);
        if (got < 0) {
            /* Unrecoverable ALSA error: stop the whole pipeline. */
            fprintf(stderr, "audio_stream: capture failed, stopping\n");
            atomic_store_explicit(&s->running, false, memory_order_release);
            break;
        }
        /* Push is non-blocking; on full it drops and bumps the metric. */
        ring_buffer_push(&s->ring, s->capture_chunk);
    }
    return NULL;
}

static void *consumer_thread_fn(void *arg) {
    AudioStream *s = (AudioStream *)arg;
    const struct timespec idle = { 0, s->idle_sleep_ns };

    while (atomic_load_explicit(&s->running, memory_order_acquire)) {
        if (ring_buffer_pop(&s->ring, s->consumer_chunk)) {
            if (s->callback) {
                s->callback(s->consumer_chunk,
                            (int)s->cfg.chunk_samples,
                            s->user_data);
            }
        } else {
            /* Empty: brief sleep so we don't spin a core at 100%. */
            nanosleep(&idle, NULL);
        }
    }

    /* Drain whatever is still queued so trailing audio isn't lost on stop. */
    while (ring_buffer_pop(&s->ring, s->consumer_chunk)) {
        if (s->callback) {
            s->callback(s->consumer_chunk, (int)s->cfg.chunk_samples, s->user_data);
        }
    }
    return NULL;
}

/* ----------------------------- lifecycle --------------------------- */

void audio_stream_default_config(AudioStreamConfig *cfg) {
    if (!cfg) return;
    cfg->device        = "default";
    cfg->sample_rate   = 16000;
    cfg->channels      = 1;
    cfg->chunk_samples = 160;   /* 10 ms @ 16 kHz */
    cfg->ring_chunks   = 256;   /* ~2.56 s of slack at 10 ms chunks */
    cfg->alsa_periods  = 4;
    cfg->use_mmap      = false; /* RW (snd_pcm_readi) by default; portable everywhere */
}

AudioStream *audio_stream_create(const AudioStreamConfig *cfg) {
    if (!cfg || cfg->chunk_samples == 0 || cfg->channels == 0 ||
        cfg->sample_rate == 0 || cfg->ring_chunks == 0) {
        fprintf(stderr, "audio_stream: invalid configuration\n");
        return NULL;
    }
    if (cfg->channels != 1) {
        /* The chunk/sample math below assumes mono. Keep the layer honest. */
        fprintf(stderr, "audio_stream: only mono (channels=1) is supported\n");
        return NULL;
    }

    AudioStream *s = (AudioStream *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->cfg = *cfg;
    atomic_init(&s->running, false);

    /* Consumer idle poll = ~1/4 of a chunk period, bounding added latency. */
    double chunk_seconds = (double)cfg->chunk_samples / (double)cfg->sample_rate;
    s->idle_sleep_ns = (long)(chunk_seconds * 1e9 / 4.0);
    if (s->idle_sleep_ns < 100000)  s->idle_sleep_ns = 100000;   /* >= 0.1 ms */
    if (s->idle_sleep_ns > 5000000) s->idle_sleep_ns = 5000000;  /* <= 5 ms   */

    if (!ring_buffer_init(&s->ring, cfg->chunk_samples, cfg->ring_chunks)) {
        fprintf(stderr, "audio_stream: ring buffer init failed\n");
        free(s);
        return NULL;
    }

    s->capture_chunk  = (int16_t *)malloc(cfg->chunk_samples * sizeof(int16_t));
    s->consumer_chunk = (int16_t *)malloc(cfg->chunk_samples * sizeof(int16_t));
    if (!s->capture_chunk || !s->consumer_chunk) {
        audio_stream_destroy(s);
        return NULL;
    }

    AudioCaptureConfig ccfg = {
        .device        = cfg->device,
        .sample_rate   = cfg->sample_rate,
        .channels      = cfg->channels,
        .period_frames = cfg->chunk_samples,
        .periods       = cfg->alsa_periods,
        .use_mmap      = cfg->use_mmap,
    };
    s->capture = audio_capture_open(&ccfg);
    if (!s->capture) {
        audio_stream_destroy(s);
        return NULL;
    }

    return s;
}

int audio_stream_start(AudioStream *s, AudioCallback callback, void *user_data) {
    if (!s) return -1;
    if (atomic_load_explicit(&s->running, memory_order_acquire)) {
        fprintf(stderr, "audio_stream: already running\n");
        return -1;
    }

    s->callback  = callback;
    s->user_data = user_data;
    atomic_store_explicit(&s->running, true, memory_order_release);

    /* Start the consumer first so it's ready to drain as capture begins. */
    if (pthread_create(&s->consumer_thread, NULL, consumer_thread_fn, s) != 0) {
        fprintf(stderr, "audio_stream: failed to start consumer thread\n");
        atomic_store_explicit(&s->running, false, memory_order_release);
        return -1;
    }
    if (pthread_create(&s->capture_thread, NULL, capture_thread_fn, s) != 0) {
        fprintf(stderr, "audio_stream: failed to start capture thread\n");
        atomic_store_explicit(&s->running, false, memory_order_release);
        pthread_join(s->consumer_thread, NULL);
        return -1;
    }

    s->threads_started = true;
    return 0;
}

void audio_stream_stop(AudioStream *s) {
    if (!s || !s->threads_started) return;

    atomic_store_explicit(&s->running, false, memory_order_release);

    /* Capture thread returns within ~one period (it polls `running` between
     * blocking reads). Consumer wakes within idle_sleep_ns then drains. */
    pthread_join(s->capture_thread, NULL);
    pthread_join(s->consumer_thread, NULL);
    s->threads_started = false;
}

void audio_stream_destroy(AudioStream *s) {
    if (!s) return;
    audio_stream_stop(s);
    audio_capture_close(s->capture);
    ring_buffer_destroy(&s->ring);
    free(s->capture_chunk);
    free(s->consumer_chunk);
    free(s);
}

/* ----------------------------- metrics ----------------------------- */

size_t audio_stream_buffer_fill_level(const AudioStream *s) {
    return s ? ring_buffer_fill_level(&s->ring) : 0;
}
size_t audio_stream_dropped_chunks(const AudioStream *s) {
    return s ? ring_buffer_dropped(&s->ring) : 0;
}
size_t audio_stream_total_processed(const AudioStream *s) {
    return s ? ring_buffer_processed(&s->ring) : 0;
}
size_t audio_stream_ring_capacity(const AudioStream *s) {
    return s ? ring_buffer_capacity(&s->ring) : 0;
}
