/*
 * audio_capture.h — thin wrapper over the ALSA PCM capture API.
 *
 * Responsible ONLY for opening a capture device and reading interleaved
 * signed-16-bit little-endian PCM frames, with automatic recovery from
 * xruns (overruns) and stream suspends. It performs no buffering of its
 * own beyond ALSA's kernel ring, and knows nothing about chunks vs. the
 * downstream ring buffer — the caller decides how many frames to read.
 */
#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Opaque-ish handle. Fields are private; do not touch outside audio_capture.c. */
typedef struct AudioCapture AudioCapture;

typedef struct {
    const char  *device;       /* ALSA device name, e.g. "default" or "hw:0,0" */
    unsigned int sample_rate;  /* Hz, e.g. 16000                                */
    unsigned int channels;     /* e.g. 1 for mono                               */
    unsigned int period_frames;/* ALSA period size in frames (== chunk size)    */
    unsigned int periods;      /* number of periods in ALSA's buffer (latency)  */
    bool         use_mmap;     /* request zero-copy MMAP transfer (see below)   */
} AudioCaptureConfig;

/*
 * Allocate and open a capture device with the given configuration.
 * Returns a handle on success, NULL on failure (an error is logged to stderr).
 * On return, the realized sample_rate/period_frames are reflected back into *cfg
 * (ALSA may pick the nearest supported values), and cfg->use_mmap is updated to
 * the transfer mode actually negotiated: if MMAP was requested but the hardware
 * does not support it, the device transparently falls back to RW and use_mmap is
 * cleared. Either way the audio_capture_read() interface is identical.
 */
AudioCapture *audio_capture_open(AudioCaptureConfig *cfg);

/* True if the device is using the zero-copy MMAP transfer path. */
bool audio_capture_is_mmap(const AudioCapture *cap);

/*
 * Read exactly `frames` frames (== frames*channels int16 samples) into `dst`,
 * looping internally over short reads and recovering from xruns/suspends.
 * Returns the number of frames read (== frames) on success, or a negative value
 * on an unrecoverable error.
 */
long audio_capture_read(AudioCapture *cap, int16_t *dst, unsigned int frames);

/* Close the device and free the handle. Safe to call with NULL. */
void audio_capture_close(AudioCapture *cap);

#endif /* AUDIO_CAPTURE_H */
