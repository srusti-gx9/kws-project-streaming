/*
 * ring_buffer.c — implementation of the lock-free SPSC chunk ring buffer.
 *
 * Memory ordering rationale (the only subtle part):
 *
 *   Producer (push):
 *     1. load read_index  with acquire   -> see the consumer's latest progress,
 *                                            so we know how much free space exists
 *                                            and never overwrite an unread slot.
 *     2. memcpy the chunk into slot[write & mask]   (plain stores)
 *     3. store write_index with release  -> publishes the data; any consumer that
 *                                            later loads this write_index with
 *                                            acquire is guaranteed to see the copy.
 *
 *   Consumer (pop):
 *     1. load write_index with acquire   -> see published data.
 *     2. memcpy out of slot[read & mask] (plain loads)
 *     3. store read_index with release   -> frees the slot for the producer.
 *
 * Because only the producer ever writes write_index and only the consumer ever
 * writes read_index, there is no contended atomic RMW and no lock on the hot path.
 */
#include "ring_buffer.h"

#include <stdlib.h>
#include <string.h>

/* Round v up to the next power of two (>= 1). */
static size_t next_pow2(size_t v) {
    if (v < 2) return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
#if SIZE_MAX > 0xFFFFFFFFu
    v |= v >> 32;
#endif
    return v + 1;
}

bool ring_buffer_init(RingBuffer *rb, size_t chunk_samples, size_t requested_capacity) {
    if (!rb || chunk_samples == 0 || requested_capacity == 0) {
        return false;
    }

    memset(rb, 0, sizeof(*rb));

    size_t capacity = next_pow2(requested_capacity);

    /* Overflow-safe size check for capacity * chunk_samples * sizeof(int16_t). */
    if (chunk_samples > SIZE_MAX / capacity) {
        return false;
    }
    size_t total_samples = capacity * chunk_samples;
    if (total_samples > SIZE_MAX / sizeof(int16_t)) {
        return false;
    }

    rb->slots = (int16_t *)malloc(total_samples * sizeof(int16_t));
    if (!rb->slots) {
        return false;
    }

    rb->chunk_samples = chunk_samples;
    rb->capacity      = capacity;
    rb->mask          = capacity - 1;

    atomic_init(&rb->write_index, 0);
    atomic_init(&rb->read_index, 0);
    atomic_init(&rb->dropped_chunks, 0);
    atomic_init(&rb->total_chunks_processed, 0);

    return true;
}

void ring_buffer_destroy(RingBuffer *rb) {
    if (!rb) return;
    free(rb->slots);
    rb->slots = NULL;
}

bool ring_buffer_push(RingBuffer *rb, const int16_t *chunk) {
    const size_t write = atomic_load_explicit(&rb->write_index, memory_order_relaxed);
    const size_t read  = atomic_load_explicit(&rb->read_index,  memory_order_acquire);

    /* Free-running counters: in-flight count == write - read. */
    if (write - read >= rb->capacity) {
        atomic_fetch_add_explicit(&rb->dropped_chunks, 1, memory_order_relaxed);
        return false; /* buffer full — drop, never block the producer */
    }

    int16_t *dst = rb->slots + (write & rb->mask) * rb->chunk_samples;
    memcpy(dst, chunk, rb->chunk_samples * sizeof(int16_t));

    /* Publish the slot. Release pairs with the consumer's acquire load. */
    atomic_store_explicit(&rb->write_index, write + 1, memory_order_release);
    return true;
}

bool ring_buffer_pop(RingBuffer *rb, int16_t *out) {
    const size_t read  = atomic_load_explicit(&rb->read_index,  memory_order_relaxed);
    const size_t write = atomic_load_explicit(&rb->write_index, memory_order_acquire);

    if (write == read) {
        return false; /* empty */
    }

    const int16_t *src = rb->slots + (read & rb->mask) * rb->chunk_samples;
    memcpy(out, src, rb->chunk_samples * sizeof(int16_t));

    /* Free the slot. Release pairs with the producer's acquire load of read_index. */
    atomic_store_explicit(&rb->read_index, read + 1, memory_order_release);
    atomic_fetch_add_explicit(&rb->total_chunks_processed, 1, memory_order_relaxed);
    return true;
}

size_t ring_buffer_fill_level(const RingBuffer *rb) {
    const size_t write = atomic_load_explicit(&rb->write_index, memory_order_acquire);
    const size_t read  = atomic_load_explicit(&rb->read_index,  memory_order_acquire);
    return write - read;
}

size_t ring_buffer_dropped(const RingBuffer *rb) {
    return atomic_load_explicit(&rb->dropped_chunks, memory_order_relaxed);
}

size_t ring_buffer_processed(const RingBuffer *rb) {
    return atomic_load_explicit(&rb->total_chunks_processed, memory_order_relaxed);
}

size_t ring_buffer_capacity(const RingBuffer *rb) {
    return rb->capacity;
}
