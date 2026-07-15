#ifndef AUDIO_CAPTURE_REAL_H
#define AUDIO_CAPTURE_REAL_H

#include "audio_capture.h"

AudioCapture *audio_capture_real_open(AudioCaptureConfig *cfg);

long audio_capture_real_read(AudioCapture *cap,
                             int16_t *dst,
                             unsigned int frames);

void audio_capture_real_close(AudioCapture *cap);

bool audio_capture_real_is_mmap(const AudioCapture *cap);

#endif
