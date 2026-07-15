#include "audio_capture.h"
#include "audio_capture_real.h"
#include "audio_capture_mock.h"

#include <string.h>

static bool g_mock_mode = false;

AudioCapture *audio_capture_open(AudioCaptureConfig *cfg)
{
    if (cfg && cfg->device &&
        strcmp(cfg->device, "default") != 0)
    {
        g_mock_mode = true;
        return audio_capture_mock_open(cfg);
    }

    g_mock_mode = false;
    return audio_capture_real_open(cfg);
}

long audio_capture_read(AudioCapture *cap,
                        int16_t *dst,
                        unsigned int frames)
{
    if (g_mock_mode)
        return audio_capture_mock_read(cap, dst, frames);

    return audio_capture_real_read(cap, dst, frames);
}

void audio_capture_close(AudioCapture *cap)
{
    if (g_mock_mode)
        audio_capture_mock_close(cap);
    else
        audio_capture_real_close(cap);
}

bool audio_capture_is_mmap(const AudioCapture *cap)
{
    if (g_mock_mode)
        return audio_capture_mock_is_mmap(cap);

    return audio_capture_real_is_mmap(cap);
}
