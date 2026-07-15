/*
 * audio_capture.c — MOCK capture backend for testing without ALSA hardware.
 *
 * Drop-in replacement for the real ALSA-based audio_capture.c. Reads PCM
 * S16_LE mono samples from a WAV file (path given via cfg->device) instead
 * of a real microphone, looping back to the start when it reaches EOF, and
 * pacing reads in real time so the rest of the pipeline (ring buffer,
 * threads, callback) behaves exactly as it would against live hardware.
 *
 * The public interface (audio_capture.h) is untouched, so audio_stream.c
 * and main.c require ZERO changes to build against this file instead of
 * the real one.
 *
 * Usage: pass the WAV file path as the "device" string, e.g.
 *   ./audio_stream_demo test.wav 160 256
 */
#include "audio_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct AudioCapture {
    FILE        *fp;
    long         data_start;
    long         data_size;
    long         pos;
    unsigned int channels;
    unsigned int sample_rate;
    struct timespec frame_period;
};

static bool parse_wav_header(FILE *fp, unsigned int want_channels,
                              unsigned int want_rate, long *data_start, long *data_size) {
    unsigned char hdr[12];
    if (fread(hdr, 1, 12, fp) != 12) return false;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "audio_capture(mock): not a RIFF/WAVE file\n");
        return false;
    }

    unsigned int channels = 0, sample_rate = 0, bits = 0;
    bool have_fmt = false;

    for (;;) {
        unsigned char chunk_hdr[8];
        if (fread(chunk_hdr, 1, 8, fp) != 8) break;
        char id[5] = {0};
        memcpy(id, chunk_hdr, 4);
        unsigned int sz = (unsigned int)chunk_hdr[4] | ((unsigned int)chunk_hdr[5] << 8) |
                           ((unsigned int)chunk_hdr[6] << 16) | ((unsigned int)chunk_hdr[7] << 24);

        if (memcmp(id, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            if (sz < 16 || fread(fmt, 1, 16, fp) != 16) return false;
            unsigned int audio_format = fmt[0] | (fmt[1] << 8);
            channels    = fmt[2] | (fmt[3] << 8);
            sample_rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
            bits        = fmt[14] | (fmt[15] << 8);
            if (audio_format != 1) {
                fprintf(stderr, "audio_capture(mock): only PCM WAV supported (got format %u)\n", audio_format);
                return false;
            }
            if (sz > 16) fseek(fp, (long)(sz - 16), SEEK_CUR);
            have_fmt = true;
        } else if (memcmp(id, "data", 4) == 0) {
            if (!have_fmt) return false;
            *data_start = ftell(fp);
            *data_size  = (long)sz;
            break;
        } else {
            fseek(fp, (long)sz, SEEK_CUR);
        }
    }

    if (*data_size <= 0) {
        fprintf(stderr, "audio_capture(mock): no data chunk found\n");
        return false;
    }
    if (bits != 16) {
        fprintf(stderr, "audio_capture(mock): only 16-bit PCM supported (got %u-bit)\n", bits);
        return false;
    }
    if (channels != want_channels) {
        fprintf(stderr, "audio_capture(mock): WARNING file has %u channel(s), config wants %u — using file's channel count\n",
                channels, want_channels);
    }
    if (sample_rate != want_rate) {
        fprintf(stderr, "audio_capture(mock): WARNING file is %u Hz, config wants %u Hz — no resampling done, playback speed/pitch will be off if mismatched\n",
                sample_rate, want_rate);
    }
    return true;
}

AudioCapture *audio_capture_open(AudioCaptureConfig *cfg) {
    if (!cfg || !cfg->device || cfg->channels == 0 ||
        cfg->sample_rate == 0 || cfg->period_frames == 0) {
        fprintf(stderr, "audio_capture(mock): invalid configuration\n");
        return NULL;
    }

    AudioCapture *cap = (AudioCapture *)calloc(1, sizeof(*cap));
    if (!cap) return NULL;

    cap->fp = fopen(cfg->device, "rb");
    if (!cap->fp) {
        fprintf(stderr, "audio_capture(mock): cannot open '%s' as WAV file\n", cfg->device);
        free(cap);
        return NULL;
    }

    if (!parse_wav_header(cap->fp, cfg->channels, cfg->sample_rate,
                           &cap->data_start, &cap->data_size)) {
        fclose(cap->fp);
        free(cap);
        return NULL;
    }

    cap->pos          = 0;
    cap->channels      = cfg->channels;
    cap->sample_rate   = cfg->sample_rate;

    cfg->use_mmap = false;

    double period_seconds = (double)cfg->period_frames / (double)cfg->sample_rate;
    cap->frame_period.tv_sec  = (time_t)period_seconds;
    cap->frame_period.tv_nsec = (long)((period_seconds - (double)cap->frame_period.tv_sec) * 1e9);

    fprintf(stderr,
            "audio_capture(mock): opened '%s' as fake capture device, rate=%u ch=%u period=%u frames (file looping enabled)\n",
            cfg->device, cfg->sample_rate, cfg->channels, cfg->period_frames);
    return cap;
}

bool audio_capture_is_mmap(const AudioCapture *cap) {
    (void)cap;
    return false;
}

long audio_capture_read(AudioCapture *cap, int16_t *dst, unsigned int frames) {
    if (!cap || !dst || frames == 0) return -1;

    size_t bytes_wanted = (size_t)frames * cap->channels * sizeof(int16_t);
    size_t bytes_read_total = 0;
    unsigned char *out = (unsigned char *)dst;

    while (bytes_read_total < bytes_wanted) {
        long bytes_left_in_data = cap->data_size - cap->pos;
        if (bytes_left_in_data <= 0) {
            cap->pos = 0;
            fseek(cap->fp, cap->data_start, SEEK_SET);
            bytes_left_in_data = cap->data_size;
        }

        size_t want = bytes_wanted - bytes_read_total;
        size_t avail = (size_t)bytes_left_in_data;
        size_t to_read = want < avail ? want : avail;

        size_t got = fread(out + bytes_read_total, 1, to_read, cap->fp);
        if (got == 0) {
            cap->pos = cap->data_size;
            continue;
        }
        cap->pos += (long)got;
        bytes_read_total += got;
    }

    nanosleep(&cap->frame_period, NULL);

    return (long)frames;
}

void audio_capture_close(AudioCapture *cap) {
    if (!cap) return;
    if (cap->fp) fclose(cap->fp);
    free(cap);
}
