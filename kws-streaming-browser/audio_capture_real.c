/*
 * audio_capture.c — ALSA PCM capture wrapper.
 *
 * Uses the libasound (ALSA) PCM API:
 *   snd_pcm_open / snd_pcm_hw_params_* / snd_pcm_readi / snd_pcm_recover / snd_pcm_close
 *
 * Capture is configured as:
 *   - SND_PCM_STREAM_CAPTURE
 *   - SND_PCM_ACCESS_RW_INTERLEAVED
 *   - SND_PCM_FORMAT_S16_LE  (signed 16-bit little-endian)
 * with caller-supplied rate, channels and period size.
 *
 * Per the ALSA PCM docs, snd_pcm_readi() returns the number of frames read
 * (>0), or a negative errno. -EPIPE (overrun), -ESTRPIPE (suspend) and -EINTR
 * are recoverable via snd_pcm_recover().
 */
#include "audio_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <alsa/asoundlib.h>

struct AudioCapture {
    snd_pcm_t   *pcm;
    unsigned int channels;
    bool         mmap;        /* true: MMAP transfer path; false: RW (readi) */
};

AudioCapture *audio_capture_real_open(AudioCaptureConfig *cfg) {
    if (!cfg || !cfg->device || cfg->channels == 0 ||
        cfg->sample_rate == 0 || cfg->period_frames == 0) {
        fprintf(stderr, "audio_capture: invalid configuration\n");
        return NULL;
    }

    AudioCapture *cap = (AudioCapture *)calloc(1, sizeof(*cap));
    if (!cap) {
        return NULL;
    }

    int err;
    if ((err = snd_pcm_open(&cap->pcm, cfg->device,
                            SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "audio_capture: cannot open '%s': %s\n",
                cfg->device, snd_strerror(err));
        free(cap);
        return NULL;
    }

    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(cap->pcm, hw);

    /* Negotiate the transfer method. MMAP is zero-copy but not all hardware
     * supports it (set_access returns < 0, often -ENOSYS), so fall back to RW. */
    cap->mmap = false;
    if (cfg->use_mmap) {
        if (snd_pcm_hw_params_set_access(cap->pcm, hw,
                    SND_PCM_ACCESS_MMAP_INTERLEAVED) == 0) {
            cap->mmap = true;
        } else {
            fprintf(stderr, "audio_capture: MMAP not supported by '%s', "
                            "falling back to RW (readi)\n", cfg->device);
        }
    }
    if (!cap->mmap) {
        if ((err = snd_pcm_hw_params_set_access(cap->pcm, hw,
                        SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
            fprintf(stderr, "audio_capture: set_access: %s\n", snd_strerror(err));
            goto fail;
        }
    }
    if ((err = snd_pcm_hw_params_set_format(cap->pcm, hw,
                    SND_PCM_FORMAT_S16_LE)) < 0) {
        fprintf(stderr, "audio_capture: set_format S16_LE: %s\n", snd_strerror(err));
        goto fail;
    }
    if ((err = snd_pcm_hw_params_set_channels(cap->pcm, hw, cfg->channels)) < 0) {
        fprintf(stderr, "audio_capture: set_channels %u: %s\n",
                cfg->channels, snd_strerror(err));
        goto fail;
    }

    /* Sample rate: ask for the exact rate, accept the nearest ALSA can do. */
    unsigned int rate = cfg->sample_rate;
    int dir = 0;
    if ((err = snd_pcm_hw_params_set_rate_near(cap->pcm, hw, &rate, &dir)) < 0) {
        fprintf(stderr, "audio_capture: set_rate_near %u: %s\n",
                cfg->sample_rate, snd_strerror(err));
        goto fail;
    }
    if (rate != cfg->sample_rate) {
        fprintf(stderr, "audio_capture: WARNING requested %u Hz, got %u Hz\n",
                cfg->sample_rate, rate);
    }

    /* Period size == one capture chunk. */
    snd_pcm_uframes_t period = cfg->period_frames;
    if ((err = snd_pcm_hw_params_set_period_size_near(cap->pcm, hw, &period, &dir)) < 0) {
        fprintf(stderr, "audio_capture: set_period_size_near: %s\n", snd_strerror(err));
        goto fail;
    }

    /* Total kernel ring buffer = `periods` periods. More periods => more slack
     * against scheduling jitter, at the cost of higher worst-case latency. */
    unsigned int periods = cfg->periods ? cfg->periods : 4;
    snd_pcm_uframes_t buffer_frames = period * periods;
    if ((err = snd_pcm_hw_params_set_buffer_size_near(cap->pcm, hw, &buffer_frames)) < 0) {
        fprintf(stderr, "audio_capture: set_buffer_size_near: %s\n", snd_strerror(err));
        goto fail;
    }

    if ((err = snd_pcm_hw_params(cap->pcm, hw)) < 0) {
        fprintf(stderr, "audio_capture: hw_params: %s\n", snd_strerror(err));
        goto fail;
    }

    /* Reflect realized values back to the caller. */
    cfg->sample_rate   = rate;
    cfg->period_frames = (unsigned int)period;
    cfg->use_mmap      = cap->mmap;
    cap->channels      = cfg->channels;

    if ((err = snd_pcm_prepare(cap->pcm)) < 0) {
        fprintf(stderr, "audio_capture: prepare: %s\n", snd_strerror(err));
        goto fail;
    }

    /* In MMAP mode the capture stream must be started explicitly; in RW mode
     * snd_pcm_readi() starts it implicitly on first read. */
    if (cap->mmap) {
        if ((err = snd_pcm_start(cap->pcm)) < 0) {
            fprintf(stderr, "audio_capture: start: %s\n", snd_strerror(err));
            goto fail;
        }
    }

    fprintf(stderr,
            "audio_capture: opened '%s' rate=%u ch=%u period=%lu frames buffer=%lu frames mode=%s\n",
            cfg->device, rate, cfg->channels,
            (unsigned long)period, (unsigned long)buffer_frames,
            cap->mmap ? "MMAP" : "RW");
    return cap;

fail:
    snd_pcm_close(cap->pcm);
    free(cap);
    return NULL;
}

/* RW transfer path: snd_pcm_readi() copies frames into the caller's buffer. */
static long read_rw(AudioCapture *cap, int16_t *dst, unsigned int frames) {
    unsigned int remaining = frames;
    int16_t *p = dst;

    while (remaining > 0) {
        snd_pcm_sframes_t n = snd_pcm_readi(cap->pcm, p, remaining);

        if (n > 0) {
            remaining -= (unsigned int)n;
            p += (size_t)n * cap->channels;
            continue;
        }

        if (n == 0 || n == -EAGAIN) {
            continue; /* nothing yet (only relevant in non-blocking mode) */
        }

        /* n < 0: try to recover from xrun (-EPIPE), suspend (-ESTRPIPE), -EINTR.
         * snd_pcm_recover() with silent=1 handles these and re-prepares the PCM. */
        int err = snd_pcm_recover(cap->pcm, (int)n, 1);
        if (err < 0) {
            fprintf(stderr, "audio_capture: read error: %s\n", snd_strerror((int)n));
            return -1; /* unrecoverable */
        }
        /* Recovered: the in-progress frames were lost; loop and keep reading. */
    }

    return (long)frames;
}

/* MMAP transfer path: zero-copy access to the kernel ring via
 * snd_pcm_avail_update / snd_pcm_mmap_begin / snd_pcm_mmap_commit. We still copy
 * each chunk into the caller's buffer (the ring-buffer slot), but avoid the
 * extra userspace staging copy that readi performs internally. */
static long read_mmap(AudioCapture *cap, int16_t *dst, unsigned int frames) {
    unsigned int remaining = frames;
    int16_t *p = dst;

    while (remaining > 0) {
        snd_pcm_sframes_t avail = snd_pcm_avail_update(cap->pcm);
        if (avail < 0) {
            int err = snd_pcm_recover(cap->pcm, (int)avail, 1);
            if (err < 0) {
                fprintf(stderr, "audio_capture: avail_update: %s\n",
                        snd_strerror((int)avail));
                return -1;
            }
            snd_pcm_start(cap->pcm); /* re-arm capture after recovery */
            continue;
        }
        if (avail == 0) {
            /* Block until at least one period is ready (100 ms timeout guards
             * against a wedged device; on timeout we simply re-poll). */
            int w = snd_pcm_wait(cap->pcm, 100);
            if (w < 0) {
                int err = snd_pcm_recover(cap->pcm, w, 1);
                if (err < 0) {
                    fprintf(stderr, "audio_capture: wait: %s\n", snd_strerror(w));
                    return -1;
                }
                snd_pcm_start(cap->pcm);
            }
            continue;
        }

        const snd_pcm_channel_area_t *areas;
        snd_pcm_uframes_t offset;
        snd_pcm_uframes_t want = remaining;            /* upper bound to map      */
        if ((snd_pcm_uframes_t)avail < want) want = (snd_pcm_uframes_t)avail;

        int err = snd_pcm_mmap_begin(cap->pcm, &areas, &offset, &want);
        if (err < 0) {
            if (snd_pcm_recover(cap->pcm, err, 1) < 0) {
                fprintf(stderr, "audio_capture: mmap_begin: %s\n", snd_strerror(err));
                return -1;
            }
            snd_pcm_start(cap->pcm);
            continue;
        }

        if (want > 0) {
            /* Interleaved: one contiguous block. areas[0].step is bits per FRAME
             * (channels * 16), areas[0].first is the channel-0 bit offset (0). */
            const unsigned int step_bytes  = areas[0].step / 8;   /* bytes/frame */
            const unsigned int first_bytes = areas[0].first / 8;
            const char *src = (const char *)areas[0].addr + first_bytes
                            + (size_t)offset * step_bytes;
            memcpy(p, src, (size_t)want * step_bytes);
            p += (size_t)want * cap->channels;
        }

        snd_pcm_sframes_t committed = snd_pcm_mmap_commit(cap->pcm, offset, want);
        if (committed < 0 || (snd_pcm_uframes_t)committed != want) {
            int err2 = snd_pcm_recover(cap->pcm,
                          committed < 0 ? (int)committed : -EPIPE, 1);
            if (err2 < 0) {
                fprintf(stderr, "audio_capture: mmap_commit: %s\n",
                        snd_strerror((int)committed));
                return -1;
            }
            snd_pcm_start(cap->pcm);
            /* Roll back the optimistic pointer advance; re-read what we lost. */
            p -= (size_t)want * cap->channels;
            continue;
        }

        remaining -= (unsigned int)want;
    }

    return (long)frames;
}

long audio_capture_real_read(AudioCapture *cap, int16_t *dst, unsigned int frames) {
    if (!cap || !dst || frames == 0) {
        return -1;
    }
    return cap->mmap ? read_mmap(cap, dst, frames)
                     : read_rw(cap, dst, frames);
}

bool audio_capture_real_is_mmap(const AudioCapture *cap) {
    return cap && cap->mmap;
}

void audio_capture_real_close(AudioCapture *cap) {
    if (!cap) return;
    if (cap->pcm) {
        snd_pcm_drop(cap->pcm);
        snd_pcm_close(cap->pcm);
    }
    free(cap);
}
