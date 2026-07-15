/*
 * audio_stream.h — public API of the real-time audio streaming layer.
 *
 * This is the ONLY header a downstream consumer (e.g. the future Sherpa-ONNX
 * keyword-spotting consumer) needs to include. It ties together:
 *
 *      ALSA capture  -->  capture thread  -->  lock-free ring buffer
 *                                                   |
 *                                                   v
 *                          consumer thread  -->  AudioCallback(samples, n)
 *
 * The layer delivers raw PCM only. It has no notion of keywords, wake words,
 * ONNX, neural networks or feature extraction.
 *
 * Delivered format (configurable, defaults shown):
 *   - signed 16-bit little-endian PCM
 *   - 16000 Hz
 *   - 1 channel (mono)
 *   - one callback invocation per chunk of `chunk_samples` samples
 */
#ifndef AUDIO_STREAM_H
#define AUDIO_STREAM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Callback invoked once per audio chunk on the consumer thread.
 *   samples     : pointer to chunk_samples interleaved int16 PCM samples
 *                 (mono => chunk_samples == frames). Valid only for the
 *                 duration of the call; copy if you need to retain it.
 *   num_samples : number of int16 samples (== chunk_samples)
 *   user_data   : opaque pointer supplied to audio_stream_start()
 *
 * The matching downstream sherpa-onnx call converts these int16 samples to
 * float [-1, 1] and forwards them to SherpaOnnxOnlineStreamAcceptWaveform().
 * See README "Future integration with Sherpa-ONNX".
 */
typedef void (*AudioCallback)(const int16_t *samples, int num_samples, void *user_data);

typedef struct {
    const char  *device;        /* ALSA device, default "default"                 */
    unsigned int sample_rate;   /* default 16000                                   */
    unsigned int channels;      /* default 1 (mono)                                */
    unsigned int chunk_samples; /* per-chunk samples, default 160 (10 ms @ 16 kHz)  */
    size_t       ring_chunks;   /* ring capacity in chunks, default 256            */
    unsigned int alsa_periods;  /* ALSA kernel buffer depth in periods, default 4  */
    bool         use_mmap;      /* request zero-copy ALSA MMAP capture, default 0  */
                                /* (falls back to RW automatically if unsupported) */
} AudioStreamConfig;

/* Fill cfg with the recommended defaults (16 kHz / mono / 10 ms chunks). */
void audio_stream_default_config(AudioStreamConfig *cfg);

typedef struct AudioStream AudioStream;

/*
 * Allocate the stream and all of its buffers (ALSA device, ring buffer, thread
 * scratch). No further allocation happens once streaming starts.
 * Returns NULL on failure.
 */
AudioStream *audio_stream_create(const AudioStreamConfig *cfg);

/*
 * Start streaming: launches the capture and consumer threads. `callback` is
 * invoked on the consumer thread once per chunk; `user_data` is passed through.
 * Returns 0 on success, non-zero on error. Idempotent-safe: starting an already
 * running stream returns an error.
 */
int audio_stream_start(AudioStream *s, AudioCallback callback, void *user_data);

/* Stop streaming: signals both threads and joins them. Safe to call once. */
void audio_stream_stop(AudioStream *s);

/* Destroy the stream and free all memory. Stops first if still running. */
void audio_stream_destroy(AudioStream *s);

/* ----- Metrics (safe to call from any thread while streaming) ----- */
size_t audio_stream_buffer_fill_level(const AudioStream *s);  /* chunks queued      */
size_t audio_stream_dropped_chunks(const AudioStream *s);     /* chunks dropped     */
size_t audio_stream_total_processed(const AudioStream *s);    /* chunks delivered   */
size_t audio_stream_ring_capacity(const AudioStream *s);      /* chunks capacity    */

#endif /* AUDIO_STREAM_H */
