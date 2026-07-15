#ifndef AUDIO_CAPTURE_MOCK_H
#define AUDIO_CAPTURE_MOCK_H

#include "audio_capture.h"

AudioCapture *audio_capture_mock_open(AudioCaptureConfig *cfg);

long audio_capture_mock_read(AudioCapture *cap,
                             int16_t *dst,
                             unsigned int frames);

void audio_capture_mock_close(AudioCapture *cap);

bool audio_capture_mock_is_mmap(const AudioCapture *cap);

#endif
