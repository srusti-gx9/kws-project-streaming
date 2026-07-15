/*
 * ring_buffer.h — Lock-free single-producer / single-consumer (SPSC) ring buffer.
 *
 * Stores fixed-size chunks of signed 16-bit PCM samples. Designed for exactly
 * one producer thread (the ALSA capture thread) and one consumer thread (the
 * audio consumer thread). Under that constraint it is fully lock-free: the
 * producer mutates only write_index, the consumer mutates only read_index, and
 * the two communicate through C11 atomics with acquire/release ordering.
 *
 * This module knows nothing about ALSA, keyword spotting, ONNX, or callbacks.
 * It is a generic int16 chunk queue.
 */
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef struct {
    int16_t       *slots;          /* capacity * chunk_samples contiguous int16 samples   */
    size_t         chunk_samples;  /* number of int16 samples per chunk                   */
    size_t         capacity;       /* number of chunk slots (always a power of two)        */
    size_t         mask;           /* capacity - 1, used to wrap free-running indices      */

    /* Free-running counters. write only touched by producer, read only by consumer. */
    _Atomic size_t write_index;
    _Atomic size_t read_index;

    /* Metrics. */
    _Atomic size_t dropped_chunks;          /* pushes rejected because the buffer was full */
    _Atomic size_t total_chunks_processed;  /* successful pops by the consumer             */
} RingBuffer;

/*
 * Initialize a ring buffer.
 *   chunk_samples       : samples per chunk (e.g. 160 for 10 ms @ 16 kHz mono)
 *   requested_capacity  : desired number of chunk slots; rounded UP to a power of two
 * Allocates all backing memory once. Returns true on success, false on bad args / OOM.
 */
bool ring_buffer_init(RingBuffer *rb, size_t chunk_samples, size_t requested_capacity);

/* Release backing memory. Safe to call on a zeroed/failed buffer. */
void ring_buffer_destroy(RingBuffer *rb);

/*
 * Push one chunk (producer side only). Copies exactly chunk_samples int16 from `chunk`.
 * Returns true if stored, false if the buffer was full (chunk dropped, metric bumped).
 * Constant time, never blocks.
 */
bool ring_buffer_push(RingBuffer *rb, const int16_t *chunk);

/*
 * Pop one chunk (consumer side only). Copies chunk_samples int16 into `out`.
 * Returns true if a chunk was available, false if the buffer was empty.
 * Constant time, never blocks.
 */
bool ring_buffer_pop(RingBuffer *rb, int16_t *out);

/* Current number of chunks waiting to be consumed (snapshot, may change immediately). */
size_t ring_buffer_fill_level(const RingBuffer *rb);

/* Total chunks dropped due to a full buffer. */
size_t ring_buffer_dropped(const RingBuffer *rb);

/* Total chunks successfully delivered to the consumer. */
size_t ring_buffer_processed(const RingBuffer *rb);

/* Maximum number of chunks the buffer can hold. */
size_t ring_buffer_capacity(const RingBuffer *rb);

#endif /* RING_BUFFER_H */
